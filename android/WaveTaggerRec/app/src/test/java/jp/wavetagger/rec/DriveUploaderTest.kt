package jp.wavetagger.rec

import java.io.ByteArrayOutputStream
import java.io.File
import okhttp3.mockwebserver.Dispatcher
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import okhttp3.mockwebserver.RecordedRequest
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test

/** The upload protocol against a small fake of Drive's resumable upload (no network, no account). */
class DriveUploaderTest {
    private lateinit var server: MockWebServer
    private lateinit var base: String
    private val received = ByteArrayOutputStream()
    private var initBody: JSONObject? = null
    private var authSeen: String? = null
    private var failChunkNumber = -1   // the n-th chunk request answers 500 (once)
    private var chunkRequests = 0
    private var sessionKnown = true    // false: the session answers 404 to the status question, then a restart works
    private var reject = 0             // answer every request with this status when not 0

    @Before fun setUp() {
        server = MockWebServer()
        server.dispatcher = object : Dispatcher() {
            override fun dispatch(request: RecordedRequest): MockResponse {
                authSeen = request.getHeader("Authorization")
                if (reject != 0) return MockResponse().setResponseCode(reject).setBody("{}")
                if (request.path!!.startsWith("/upload/drive/v3/files")) {
                    initBody = JSONObject(request.body.readUtf8())
                    return MockResponse().setResponseCode(200).setHeader("Location", "$base/session/1")
                }
                val range = request.getHeader("Content-Range")!!
                val total = range.substringAfter('/').toLong()
                if (range.startsWith("bytes */")) {
                    if (!sessionKnown) { sessionKnown = true; received.reset(); return MockResponse().setResponseCode(404).setBody("{}") }
                    return progress(total)
                }
                chunkRequests++
                if (chunkRequests == failChunkNumber) { failChunkNumber = -1; return MockResponse().setResponseCode(500).setBody("{}") }
                val start = range.removePrefix("bytes ").substringBefore('-').toLong()
                assertEquals("chunks must follow each other", received.size().toLong(), start)
                received.write(request.body.readByteArray())
                return progress(total)
            }
        }
        server.start()
        base = server.url("").toString().trimEnd('/')
    }

    private fun progress(total: Long): MockResponse =
        if (received.size().toLong() >= total) MockResponse().setResponseCode(200).setBody("""{"id":"file-123"}""")
        else MockResponse().setResponseCode(308).also { if (received.size() > 0) it.setHeader("Range", "bytes=0-${received.size() - 1}") }

    @After fun tearDown() { server.shutdown() }

    private fun file(size: Int): File {
        val f = File.createTempFile("wtup", ".wav")
        f.deleteOnExit()
        f.writeBytes(ByteArray(size) { (it * 31 + 7).toByte() })
        return f
    }

    @Test fun uploadsInChunksIntoTheFolder() {
        val f = file(700_000)
        val sessions = ArrayList<String?>()
        val id = DriveUploader(base, 256 * 1024).upload(f, "a.wav", "FOLDER1", "tok", null) { sessions.add(it) }
        assertEquals("file-123", id)
        assertArrayEquals(f.readBytes(), received.toByteArray())
        assertEquals("a.wav", initBody!!.getString("name"))
        assertEquals("FOLDER1", initBody!!.getJSONArray("parents").getString(0))
        assertEquals("Bearer tok", authSeen)
        assertEquals("$base/session/1", sessions.first())
        assertNull(sessions.last()) // finished: nothing to resume
        assertEquals(3, chunkRequests)
    }

    @Test fun aBrokenChunkIsContinuedFromWhatDriveHas() {
        val f = file(700_000)
        failChunkNumber = 2
        var session: String? = null
        try {
            DriveUploader(base, 256 * 1024).upload(f, "b.wav", "F", "tok", null) { session = it }
            fail("the second chunk answers 500")
        } catch (e: DriveUploader.RetryException) { /* expected */ }
        assertEquals("$base/session/1", session)
        assertEquals(256 * 1024, received.size()) // only the first chunk arrived

        val id = DriveUploader(base, 256 * 1024).upload(f, "b.wav", "F", "tok", session) { session = it }
        assertEquals("file-123", id)
        assertArrayEquals(f.readBytes(), received.toByteArray())
        assertNull(session)
    }

    @Test fun aSessionThatIsGoneStartsAgain() {
        val f = file(300_000)
        sessionKnown = false
        val id = DriveUploader(base, 1024 * 1024).upload(f, "c.wav", "F", "tok", "$base/session/1") { }
        assertEquals("file-123", id)
        assertArrayEquals(f.readBytes(), received.toByteArray())
    }

    @Test fun anExpiredTokenIsReportedAsSuch() {
        reject = 401
        try { DriveUploader(base).upload(file(1000), "d.wav", "F", "old", null) { }; fail() } catch (e: DriveUploader.AuthException) { assertTrue(e.message!!.isNotEmpty()) }
    }

    @Test fun serverTroubleIsRetriedButAMissingFolderIsNot() {
        reject = 503
        try { DriveUploader(base).upload(file(1000), "e.wav", "F", "t", null) { }; fail() } catch (e: DriveUploader.RetryException) { }
        reject = 404
        try { DriveUploader(base).upload(file(1000), "e.wav", "F", "t", null) { }; fail() } catch (e: DriveUploader.FatalException) { }
    }
}
