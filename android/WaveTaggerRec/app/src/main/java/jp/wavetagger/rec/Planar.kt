package jp.wavetagger.rec

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Sample layout helpers. The screens get planar float32 blocks: all of the left channel, then all of the right channel. */
object Planar {
    /**
     * Interleaved (L R L R ...) to planar. With [swap] the two channels change places (stereo only).
     * [out] must hold at least `frames * channels` values. Returns the number of frames.
     */
    fun planarise(interleaved: FloatArray, count: Int, channels: Int, swap: Boolean, out: FloatArray): Int {
        val frames = count / channels
        if (channels == 2) {
            val l = if (swap) 1 else 0
            val r = 1 - l
            for (i in 0 until frames) {
                out[i] = interleaved[i * 2 + l]
                out[frames + i] = interleaved[i * 2 + r]
            }
        } else {
            for (c in 0 until channels)
                for (i in 0 until frames)
                    out[c * frames + i] = interleaved[i * channels + c]
        }
        return frames
    }

    /** Little-endian float32 bytes of the first `frames * channels` values (what a JS Float32Array reads). */
    fun toBytes(planar: FloatArray, frames: Int, channels: Int): ByteArray {
        val buf = ByteBuffer.allocate(frames * channels * 4).order(ByteOrder.LITTLE_ENDIAN)
        buf.asFloatBuffer().put(planar, 0, frames * channels)
        return buf.array()
    }
}
