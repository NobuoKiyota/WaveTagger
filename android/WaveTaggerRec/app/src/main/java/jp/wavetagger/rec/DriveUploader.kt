package jp.wavetagger.rec

import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.net.HttpURLConnection
import java.net.URL
import org.json.JSONArray
import org.json.JSONObject

/**
 * Google Drive resumable upload (the same method as docs/app.js): one session per file, the file goes up in chunks, and a broken
 * transfer is continued from what Drive already has instead of starting again.
 */
class DriveUploader(private val base: String = "https://www.googleapis.com", private val chunkSize: Int = 8 * 1024 * 1024) {
    /** The token is no longer accepted (or consent was withdrawn): sending must stop until the user connects again. */
    class AuthException(message: String) : Exception(message)

    /** A network or server problem: try again later (the work manager does that). */
    class RetryException(message: String, cause: Throwable? = null) : Exception(message, cause)

    /** A problem that trying again will not fix (the folder does not exist, no permission). */
    class FatalException(message: String) : Exception(message)

    /**
     * Uploads [file] into the Drive folder [folderId] and returns the Drive file id. [sessionUrl] is the session of an earlier attempt
     * (or null); [onSession] is told about every new session so the caller can keep it for the next attempt (null = forget it).
     */
    fun upload(file: File, name: String, folderId: String, token: String, sessionUrl: String?, onSession: (String?) -> Unit): String {
        val total = file.length()
        var session = sessionUrl
        var offset = 0L
        if (session != null) {
            val state = status(session, total, token)
            if (state == null) { session = null; onSession(null) } // the session is gone: start again
            else if (state.id != null) { onSession(null); return state.id }
            else offset = state.next
        }
        if (session == null) {
            session = start(name, folderId, total, token)
            onSession(session)
        }
        RandomAccessFile(file, "r").use { f ->
            val buffer = ByteArray(chunkSize)
            while (true) {
                val len = minOf(chunkSize.toLong(), total - offset).toInt()
                f.seek(offset)
                f.readFully(buffer, 0, len)
                val range = if (total == 0L) "bytes */0" else "bytes $offset-${offset + len - 1}/$total"
                val c = open(session, "PUT", token)
                c.setRequestProperty("Content-Range", range)
                c.setFixedLengthStreamingMode(len)
                c.doOutput = true
                val code = try {
                    c.outputStream.use { it.write(buffer, 0, len) }
                    c.responseCode
                } catch (e: IOException) {
                    throw RetryException("送信が途切れました: ${e.message}", e)
                }
                when (code) {
                    200, 201 -> { onSession(null); return idOf(c) }
                    308 -> offset = nextOffset(c)
                    else -> fail(c, code)
                }
            }
        }
    }

    private class State(val next: Long, val id: String?)

    /** Asks Drive how much of the file it has. Null if the session does not exist any more. */
    private fun status(session: String, total: Long, token: String): State? {
        val c = open(session, "PUT", token)
        c.setRequestProperty("Content-Range", "bytes */$total")
        c.setFixedLengthStreamingMode(0)
        c.doOutput = true
        val code = try { c.outputStream.close(); c.responseCode } catch (e: IOException) { throw RetryException("送信状況を確認できません: ${e.message}", e) }
        return when (code) {
            200, 201 -> State(total, idOf(c))
            308 -> State(nextOffset(c), null)
            404, 410 -> null
            else -> { fail(c, code); null }
        }
    }

    private fun start(name: String, folderId: String, total: Long, token: String): String {
        val c = open("$base/upload/drive/v3/files?uploadType=resumable&supportsAllDrives=true", "POST", token)
        c.setRequestProperty("Content-Type", "application/json; charset=UTF-8")
        c.setRequestProperty("X-Upload-Content-Type", "audio/wav")
        c.setRequestProperty("X-Upload-Content-Length", total.toString())
        val body = JSONObject().put("name", name).put("parents", JSONArray().put(folderId)).toString().toByteArray(Charsets.UTF_8)
        c.setFixedLengthStreamingMode(body.size)
        c.doOutput = true
        val code = try { c.outputStream.use { it.write(body) }; c.responseCode } catch (e: IOException) { throw RetryException("送信を始められません: ${e.message}", e) }
        if (code != 200) fail(c, code)
        return c.getHeaderField("Location") ?: throw RetryException("Drive が送信先を返しませんでした")
    }

    private fun open(url: String, method: String, token: String): HttpURLConnection {
        val c = URL(url).openConnection() as HttpURLConnection
        c.requestMethod = method
        c.connectTimeout = 20_000
        c.readTimeout = 60_000
        c.setRequestProperty("Authorization", "Bearer $token")
        return c
    }

    private fun nextOffset(c: HttpURLConnection): Long {
        val range = c.getHeaderField("Range") ?: return 0L // nothing received yet
        return range.substringAfter('-').trim().toLong() + 1
    }

    private fun idOf(c: HttpURLConnection): String = try {
        JSONObject(c.inputStream.bufferedReader().readText()).optString("id", "")
    } catch (_: Throwable) { "" }

    private fun fail(c: HttpURLConnection, code: Int): Nothing {
        val detail = try { (c.errorStream ?: c.inputStream).bufferedReader().readText().take(200) } catch (_: Throwable) { "" }
        throw when {
            code == 401 -> AuthException("Google の認証が切れました")
            code == 403 && detail.contains("rateLimit", ignoreCase = true) -> RetryException("Drive の回数制限です")
            code == 403 || code == 404 -> FatalException("Drive に書き込めません ($code): フォルダの指定と権限を確認してください")
            code == 408 || code == 429 || code >= 500 -> RetryException("Drive が一時的に応答しません ($code)")
            else -> FatalException("Drive が受け付けません ($code) $detail")
        }
    }
}
