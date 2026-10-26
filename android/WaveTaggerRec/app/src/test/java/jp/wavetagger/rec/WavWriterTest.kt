package jp.wavetagger.rec

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class WavWriterTest {
    private lateinit var dir: File

    @Before fun setUp() {
        dir = File.createTempFile("wtrec", "").also { it.delete(); it.mkdirs() }
        Recordings.dir = dir
    }

    @After fun tearDown() { dir.deleteRecursively(); Recorder.splitSeconds = 3600L }

    /** A planar stereo block whose samples all sit on the 16 bit grid (as the phone's microphone delivers them). */
    private fun block(frames: Int, seedL: Int = 1): FloatArray {
        val b = FloatArray(frames * 2)
        for (i in 0 until frames) {
            b[i] = (((i * 37 + seedL) % 2001) - 1000) / 32768f * 20
            b[frames + i] = (((i * 53 + 7) % 3001) - 1500) / 32768f * 10
        }
        return b
    }

    private fun samples(file: File, bits: Int, channels: Int): IntArray {
        val start = WavWriter.dataOffset(file).toInt()
        val bytes = file.readBytes()
        val bb = ByteBuffer.wrap(bytes, start, bytes.size - start).order(ByteOrder.LITTLE_ENDIAN)
        val n = (bytes.size - start) / (bits / 8)
        return IntArray(n) { if (bits == 24) { val lo = bb.get().toInt() and 0xFF; val mid = bb.get().toInt() and 0xFF; (bb.get().toInt() shl 16) or (mid shl 8) or lo } else bb.short.toInt() }
    }

    @Test fun header24IsValidAndSamplesAreExact() {
        val f = File(dir, "a.wav")
        val w = WavWriter(f, 2, 48000, 24, "test")
        val blk = block(2048)
        w.write(blk, 2048)
        w.close()
        val b = ByteBuffer.wrap(f.readBytes()).order(ByteOrder.LITTLE_ENDIAN)
        assertEquals("RIFF", String(f.readBytes(), 0, 4))
        assertEquals(f.length() - 8, b.getInt(4).toLong() and 0xFFFFFFFFL)
        val start = WavWriter.dataOffset(f).toInt()
        assertEquals(2048 * 2 * 3, b.getInt(start - 4))
        assertEquals(2, b.getShort(22).toInt()); assertEquals(48000, b.getInt(24)); assertEquals(24, b.getShort(34).toInt())
        val s = samples(f, 24, 2)
        for (i in 0 until 2048) {
            assertEquals(Math.round(blk[i] * 8388608f), s[i * 2])           // left
            assertEquals(Math.round(blk[2048 + i] * 8388608f), s[i * 2 + 1]) // right
        }
    }

    @Test fun sixteenBitIsLosslessForASixteenBitSignal() {
        val f = File(dir, "b.wav")
        val blk = block(1000)
        WavWriter(f, 2, 48000, 16, "t").also { it.write(blk, 1000); it.close() }
        val s = samples(f, 16, 2)
        for (i in 0 until 1000) {
            assertEquals(Math.round(blk[i] * 32768f), s[i * 2])
            assertEquals(Math.round(blk[1000 + i] * 32768f), s[i * 2 + 1])
        }
    }

    @Test fun sixteenBitDithersAFinerSignalByAtMostOneStep() {
        val f = File(dir, "c.wav")
        val blk = FloatArray(2000) { 0.1f * Math.sin(it * 0.01).toFloat() + 0.000013f } // not on the 16 bit grid
        WavWriter(f, 1, 48000, 16, "t").also { it.write(blk, 2000); it.close() }
        val s = samples(f, 16, 1)
        var differs = 0
        for (i in 0 until 2000) {
            val exact = blk[i] * 32768f
            assertTrue(Math.abs(s[i] - exact) <= 1.5f)
            if (s[i] != Math.round(exact)) differs++
        }
        assertTrue("dither should change some samples", differs > 0)
    }

    @Test fun anInterruptedFileIsRecoveredToWholeFrames() {
        val f = File(dir, "d.wav")
        val w = WavWriter(f, 2, 48000, 24, "t")
        w.write(block(4096), 4096)
        w.updateHeader() // what the recorder does once a second
        w.write(block(4096), 4096) // after the last update: the header does not know about it
        val full = f.length()
        java.io.RandomAccessFile(f, "rw").use { it.setLength(full - 4) } // cut in the middle of a frame (a crash)
        val frames = WavWriter.recover(f)
        assertEquals((4096 * 2 - 1).toLong(), frames)
        val b = ByteBuffer.wrap(f.readBytes()).order(ByteOrder.LITTLE_ENDIAN)
        val start = WavWriter.dataOffset(f).toInt()
        assertEquals(frames * 6, (b.getInt(start - 4).toLong() and 0xFFFFFFFFL))
        assertEquals(f.length() - 8, (b.getInt(4).toLong() and 0xFFFFFFFFL))
    }

    @Test fun recorderSplitsRenamesAndDeletesSilence() {
        Recorder.splitSeconds = 1
        Recorder.start(dir, "2026-01-01_00-00-00", 2, 8000, 24, "t")
        repeat(5) { Recorder.onBlock(block(4000, it + 1), 2, 4000) } // 20000 frames = 2.5 s at 8 kHz
        val r = Recorder.stop("MIC")
        assertFalse(r.silent)
        assertEquals(3, r.files.size)
        assertEquals("2026-01-01_00-00-00.wav", r.files[0]); assertEquals("2026-01-01_00-00-00_2.wav", r.files[1])
        assertTrue(File(dir, r.files[2]).exists()); assertFalse(File(dir, r.files[0] + ".part").exists())
        assertEquals(2.5, r.seconds, 1e-9)
        assertEquals(3, Recordings.list().size)
        assertTrue(Recordings.list().all { it.status == "wait" })

        Recorder.start(dir, "silent", 2, 8000, 24, "t")
        Recorder.onBlock(FloatArray(2000), 2, 1000)
        val s = Recorder.stop("MIC")
        assertTrue(s.silent)
        assertFalse(File(dir, "silent.wav").exists()); assertFalse(File(dir, "silent.wav.part").exists())
    }

    @Test fun sentRecordingsAreRemovedAfterTheKeepTimeButWaitingOnesNever() {
        Recorder.start(dir, "keep", 2, 8000, 24, "t"); Recorder.onBlock(block(800), 2, 800); Recorder.stop("MIC")
        Recorder.start(dir, "sent", 2, 8000, 24, "t"); Recorder.onBlock(block(800), 2, 800); Recorder.stop("MIC")
        val day = 86_400_000L
        val now = 100 * day
        Recordings.markSent("sent.wav", now - 8 * day)
        assertEquals(0, Recordings.cleanup(9, now)) // 8 days old, keep 9
        assertEquals(1, Recordings.cleanup(7, now))
        assertFalse(File(dir, "sent.wav").exists()); assertFalse(File(dir, "sent.json").exists())
        assertTrue(File(dir, "keep.wav").exists()) // never sent: never removed
        assertFalse(Recordings.delete("keep.wav", false))
        assertFalse(Recordings.delete("../keep.wav", false))
    }

    @Test fun crashLeftoversAreRecoveredIntoTheHistory() {
        val live = File(dir, "live.tmp")
        val w = WavWriter(live, 2, 48000, 24, "t")
        w.write(block(2048), 2048); w.updateHeader()
        w.write(block(2048), 2048)
        // a copy taken now is what a killed process leaves behind: the header knows 2048 frames, the file holds 4096
        val part = File(dir, "crash.wav.part")
        live.copyTo(part)
        w.close()
        live.delete()
        val names = Recordings.recover()
        assertEquals(listOf("crash_recovered.wav"), names)
        val m = Recordings.list().single()
        assertTrue(m.recovered); assertEquals(24, m.bits); assertEquals(4096 / 48000.0, m.seconds, 1e-9)
        assertFalse(part.exists())
    }
}
