package jp.wavetagger.rec

import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.abs

/**
 * A PCM WAV file written while recording. The header (with an INFO comment) is fixed in size, so the two length fields can be
 * patched in place: [updateHeader] makes the file valid up to what has been written so far (call it about once a second).
 *
 * 24 bit scales by 2^23, 16 bit by 2^15: a signal that already sits on the 16 bit grid (what the phone's microphone delivers, even as
 * float) is written without any change. A 16 bit file made from a finer signal gets TPDF dither.
 */
class WavWriter(val file: File, val channels: Int, val rate: Int, val bits: Int, comment: String) {
    private val raf = RandomAccessFile(file, "rw")
    private val io = raf.channel
    private val bytesPerFrame = channels * bits / 8
    val headerSize: Int
    var dataBytes = 0L
        private set
    val frames: Long get() = dataBytes / bytesPerFrame

    private var seed = 0x9E3779B9L
    private var scratch = ByteBuffer.allocate(0)

    init {
        require(bits == 16 || bits == 24)
        raf.setLength(0)
        val head = header(channels, rate, bits, comment)
        headerSize = head.size
        io.write(ByteBuffer.wrap(head))
    }

    /** [planar]: all of channel 0 for [frames] frames, then all of channel 1 (the layout of the blocks from [Capture]). */
    fun write(planar: FloatArray, frames: Int) {
        val need = frames * bytesPerFrame
        if (scratch.capacity() < need) scratch = ByteBuffer.allocate(need).order(ByteOrder.LITTLE_ENDIAN)
        scratch.clear()
        val dither = bits == 16 && !onGrid16(planar, frames * channels)
        for (i in 0 until frames) {
            for (c in 0 until channels) {
                val x = planar[c * frames + i]
                if (bits == 24) {
                    var v = Math.round(x * 8388608f)
                    if (v > 8388607) v = 8388607 else if (v < -8388608) v = -8388608
                    scratch.put(v.toByte()).put((v shr 8).toByte()).put((v shr 16).toByte())
                } else {
                    var v = Math.round(x * 32768f + (if (dither) rnd() - rnd() else 0f))
                    if (v > 32767) v = 32767 else if (v < -32768) v = -32768
                    scratch.put(v.toByte()).put((v shr 8).toByte())
                }
            }
        }
        scratch.flip()
        while (scratch.hasRemaining()) io.write(scratch)
        dataBytes += need
    }

    private fun rnd(): Float {
        seed = (seed * 1664525L + 1013904223L) and 0xFFFFFFFFL
        return seed / 4294967296f
    }

    /** Makes the length fields match what is written, and pushes it to the disk. */
    fun updateHeader() {
        val b = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN)
        b.putInt(0, (headerSize - 8 + dataBytes).toInt())
        io.write(b, 4)
        b.clear(); b.putInt(0, dataBytes.toInt())
        io.write(b, (headerSize - 4).toLong())
        io.force(false)
    }

    fun close() {
        try { updateHeader() } finally { raf.close() }
    }

    companion object {
        /** True when every sample is (within float error) a whole number of 16 bit steps. */
        fun onGrid16(a: FloatArray, count: Int): Boolean {
            for (i in 0 until count) {
                val s = a[i] * 32768f
                if (abs(s - Math.round(s)) > 0.01f) return false
            }
            return true
        }

        private fun pad(text: String): ByteArray {
            val raw = text.toByteArray(Charsets.UTF_8) + 0
            return if (raw.size % 2 == 0) raw else raw + 0
        }

        fun header(channels: Int, rate: Int, bits: Int, comment: String): ByteArray {
            val software = pad("WaveTagger Rec")
            val note = pad(comment)
            val info = 4 + (8 + software.size) + (8 + note.size) // "INFO" + ISFT + ICMT
            val out = ByteBuffer.allocate(12 + 24 + 8 + info + 8).order(ByteOrder.LITTLE_ENDIAN)
            val bytesPerFrame = channels * bits / 8
            out.put("RIFF".toByteArray()).putInt(0).put("WAVE".toByteArray())
            out.put("fmt ".toByteArray()).putInt(16).putShort(1).putShort(channels.toShort())
                .putInt(rate).putInt(rate * bytesPerFrame).putShort(bytesPerFrame.toShort()).putShort(bits.toShort())
            out.put("LIST".toByteArray()).putInt(info).put("INFO".toByteArray())
            out.put("ISFT".toByteArray()).putInt(software.size).put(software)
            out.put("ICMT".toByteArray()).putInt(note.size).put(note)
            out.put("data".toByteArray()).putInt(0)
            return out.array()
        }

        /** Where the sample data of a WAV starts, or -1 if the file is not a WAV this writer could have made. */
        fun dataOffset(file: File): Long {
            RandomAccessFile(file, "r").use { f ->
                if (f.length() < 12) return -1
                val head = ByteArray(12); f.readFully(head)
                if (String(head, 0, 4) != "RIFF" || String(head, 8, 4) != "WAVE") return -1
                var pos = 12L
                val h = ByteArray(8)
                while (pos + 8 <= f.length()) {
                    f.seek(pos); f.readFully(h)
                    val size = ByteBuffer.wrap(h, 4, 4).order(ByteOrder.LITTLE_ENDIAN).int.toLong() and 0xFFFFFFFFL
                    if (String(h, 0, 4) == "data") return pos + 8
                    pos += 8 + size + (size and 1)
                }
            }
            return -1
        }

        /**
         * Repairs a file whose recording was cut off: the length fields are set from the real file size (a partial last frame is cut).
         * Returns the number of frames, or -1 if it is not a recoverable WAV.
         */
        fun recover(file: File): Long {
            val start = dataOffset(file)
            if (start < 0) return -1
            RandomAccessFile(file, "rw").use { f ->
                val fmt = ByteArray(24); f.seek(12); f.readFully(fmt)
                val b = ByteBuffer.wrap(fmt).order(ByteOrder.LITTLE_ENDIAN)
                val channels = b.getShort(10).toInt()
                val bits = b.getShort(22).toInt()
                val frameBytes = channels * bits / 8
                if (frameBytes <= 0) return -1
                val frames = (f.length() - start) / frameBytes
                val data = frames * frameBytes
                f.setLength(start + data)
                val w = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN)
                w.putInt(0, (start - 8 + data).toInt()); f.seek(4); f.write(w.array())
                w.putInt(0, data.toInt()); f.seek(start - 4); f.write(w.array())
                return frames
            }
        }
    }
}
