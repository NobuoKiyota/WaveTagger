package jp.wavetagger.rec

import java.io.File
import kotlin.math.abs

/**
 * Writes the blocks from [Capture] to WAV files in [Recordings.dir]. It lives in the process, not in the screen, so a recording
 * goes on with the screen off. While recording a file is called `<stamp>.wav.part`; stopping renames it, and a `.part` left behind
 * by a crash is repaired the next time the app starts ([Recordings.recover]).
 *
 * A recording is split into a new file after [splitSeconds] or [splitBytes] (the PC side refuses 4 GB WAV files).
 */
object Recorder {
    var splitSeconds = 3600L
    var splitBytes = 2_000_000_000L

    class Result(val files: List<String>, val seconds: Double, val peak: Float, val silent: Boolean, val badSamples: Long, val error: String?)

    private class Active(val dir: File, val stamp: String, val channels: Int, val rate: Int, val bits: Int, val comment: String) {
        var writer: WavWriter? = null
        var part = 0
        val files = ArrayList<File>()
        var frames = 0L
        var sinceHeader = 0
        var peak = 0f
        var bad = 0L
        var error: String? = null
    }

    @Volatile private var active: Active? = null

    val isRecording: Boolean get() = active != null

    @Synchronized
    fun start(dir: File, stamp: String, channels: Int, rate: Int, bits: Int, comment: String): String {
        check(active == null) { "already recording" }
        dir.mkdirs()
        val a = Active(dir, stamp, channels, rate, bits, comment)
        openNext(a)
        active = a
        return a.files[0].name.removeSuffix(".part")
    }

    private fun openNext(a: Active) {
        a.writer?.close()
        a.part++
        val name = if (a.part == 1) "${a.stamp}.wav" else "${a.stamp}_${a.part}.wav"
        val file = File(a.dir, "$name.part")
        a.files.add(file)
        a.writer = WavWriter(file, a.channels, a.rate, a.bits, a.comment + (if (a.part > 1) "; part=${a.part}" else ""))
    }

    /** Called on the capture thread for every block. Never throws: a disk error is kept and reported when recording stops. */
    fun onBlock(planar: FloatArray, channels: Int, frames: Int) {
        val a = active ?: return
        if (a.error != null) return
        try {
            val total = frames * channels
            for (i in 0 until total) {
                val v = planar[i]
                if (v != v || v == Float.POSITIVE_INFINITY || v == Float.NEGATIVE_INFINITY) { planar[i] = 0f; a.bad++ }
                else { val m = abs(v); if (m > a.peak) a.peak = m }
            }
            var w = a.writer!!
            if (w.frames >= splitSeconds * a.rate || w.dataBytes + frames * channels * a.bits / 8 > splitBytes) {
                openNext(a)
                w = a.writer!!
            }
            w.write(planar, frames)
            a.frames += frames
            a.sinceHeader += frames
            if (a.sinceHeader >= a.rate) { w.updateHeader(); a.sinceHeader = 0 }
        } catch (e: Throwable) {
            a.error = e.message ?: e.toString()
        }
    }

    /** Ends the recording. A recording in which nothing but zeros arrived is deleted ([Result.silent]). */
    @Synchronized
    fun stop(source: String): Result {
        val a = active ?: throw IllegalStateException("not recording")
        active = null
        try { a.writer?.close() } catch (e: Throwable) { if (a.error == null) a.error = e.message }
        val seconds = a.frames.toDouble() / a.rate
        if (!(a.peak > 1e-6f)) {
            a.files.forEach { it.delete() }
            return Result(emptyList(), seconds, a.peak, true, a.bad, a.error)
        }
        val names = ArrayList<String>()
        for (part in a.files) {
            val done = File(part.parentFile, part.name.removeSuffix(".part"))
            if (part.renameTo(done)) {
                names.add(done.name)
                val dataBytes = done.length() - WavWriter.dataOffset(done)
                val partSeconds = dataBytes.toDouble() / (a.channels * a.bits / 8) / a.rate
                Recordings.write(done, Recordings.Meta(done.name, partSeconds, a.channels, a.rate, a.bits, a.peak.toDouble(), source,
                    "wait", System.currentTimeMillis(), 0L, false))
            }
        }
        return Result(names, seconds, a.peak, false, a.bad, a.error)
    }
}
