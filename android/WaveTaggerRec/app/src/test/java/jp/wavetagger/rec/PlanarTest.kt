package jp.wavetagger.rec

import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

class PlanarTest {
    private val interleaved = floatArrayOf(1f, -1f, 2f, -2f, 3f, -3f) // L = 1 2 3, R = -1 -2 -3

    @Test fun stereoIsSplitIntoPlanes() {
        val out = FloatArray(6)
        assertEquals(3, Planar.planarise(interleaved, 6, 2, false, out))
        assertArrayEquals(floatArrayOf(1f, 2f, 3f, -1f, -2f, -3f), out, 0f)
    }

    @Test fun swapExchangesTheChannels() {
        val out = FloatArray(6)
        Planar.planarise(interleaved, 6, 2, true, out)
        assertArrayEquals(floatArrayOf(-1f, -2f, -3f, 1f, 2f, 3f), out, 0f)
    }

    @Test fun monoIsCopiedAndIgnoresSwap() {
        val out = FloatArray(3)
        assertEquals(3, Planar.planarise(floatArrayOf(0.1f, 0.2f, 0.3f), 3, 1, true, out))
        assertArrayEquals(floatArrayOf(0.1f, 0.2f, 0.3f), out, 0f)
    }

    @Test fun anIncompleteFrameIsDropped() {
        val out = FloatArray(6)
        assertEquals(2, Planar.planarise(interleaved, 5, 2, false, out))
    }

    @Test fun bytesAreLittleEndianFloats() {
        val bytes = Planar.toBytes(floatArrayOf(1f, 2f, 3f, 4f), 2, 2)
        assertEquals(16, bytes.size)
        val back = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).asFloatBuffer()
        assertEquals(1f, back.get(0), 0f)
        assertEquals(4f, back.get(3), 0f)
    }
}
