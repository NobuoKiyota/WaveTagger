package jp.wavetagger.rec

import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.media.audiofx.AcousticEchoCanceler
import android.media.audiofx.AudioEffect
import android.media.audiofx.AutomaticGainControl
import android.media.audiofx.NoiseSuppressor
import kotlin.math.PI
import kotlin.math.sin

/**
 * The one open microphone. A reader thread turns each block into planar float32 and hands it to [listener]
 * (from the reader thread: the receiver must not block).
 *
 * Sources: MIC (two microphones, the real stereo), CAMCORDER (the same two, more sensitive), TEST (a generated signal for checking
 * the screens and the transport without a microphone).
 */
object Capture {
    private const val BLOCK_FRAMES = 2048

    data class Info(val channels: Int, val rate: Int, val source: String, val label: String, val effects: String)

    @Volatile var listener: ((planar: FloatArray, channels: Int, frames: Int) -> Unit)? = null

    /** Whether channels 0 and 1 change places (decided from the screen rotation and the user's setting). */
    @Volatile var swap = false

    @Volatile private var running = false
    private var thread: Thread? = null
    private val effects = ArrayList<AudioEffect>() // kept alive for as long as the microphone is open
    var info: Info? = null
        private set

    val isOpen: Boolean get() = running

    /** Opens the microphone and starts reading. Throws if it cannot be opened. [stereo] false asks for one channel (one microphone). */
    @Synchronized
    fun open(source: String, stereo: Boolean, rate: Int): Info {
        close()
        val result = if (source == "TEST") openTest(stereo, rate) else openMic(source, stereo, rate)
        info = result
        return result
    }

    @Synchronized
    fun close() {
        running = false
        thread?.let { t ->
            try { t.join(1500) } catch (_: InterruptedException) { /* the thread ends on its own */ }
        }
        thread = null
        info = null
    }

    private fun openMic(source: String, stereo: Boolean, rate: Int): Info {
        val src = if (source == "CAMCORDER") MediaRecorder.AudioSource.CAMCORDER else MediaRecorder.AudioSource.MIC
        var wantChannels = if (stereo) 2 else 1
        var rec = build(src, wantChannels, rate)
        if (rec == null && wantChannels == 2) { // a device without a stereo input (the emulator): say so by delivering one channel
            wantChannels = 1
            rec = build(src, 1, rate)
        }
        if (rec == null) throw IllegalStateException("マイクを開けません")

        val channels = rec.channelCount
        val effectsText = disableEffects(rec)
        val label = "内蔵マイク (" + (if (src == MediaRecorder.AudioSource.CAMCORDER) "CAMCORDER" else "MIC") + ")"
        running = true
        thread = Thread({
            val data = FloatArray(BLOCK_FRAMES * channels)
            val planar = FloatArray(BLOCK_FRAMES * channels)
            try {
                rec.startRecording()
                while (running) {
                    val n = rec.read(data, 0, data.size, AudioRecord.READ_BLOCKING)
                    if (n <= 0) break
                    val frames = Planar.planarise(data, n, channels, swap && channels == 2, planar)
                    listener?.invoke(planar, channels, frames)
                }
                rec.stop()
            } catch (_: Throwable) {
                // the screens notice that no more blocks arrive
            } finally {
                running = false
                for (e in effects) e.release()
                effects.clear()
                rec.release()
            }
        }, "capture").also { it.start() }
        return Info(channels, rec.sampleRate, source, label, effectsText)
    }

    private fun build(src: Int, channels: Int, rate: Int): AudioRecord? {
        val mask = if (channels == 2) AudioFormat.CHANNEL_IN_STEREO else AudioFormat.CHANNEL_IN_MONO
        val min = AudioRecord.getMinBufferSize(rate, mask, AudioFormat.ENCODING_PCM_FLOAT)
        if (min <= 0) return null
        return try {
            val rec = AudioRecord.Builder()
                .setAudioSource(src)
                .setAudioFormat(AudioFormat.Builder().setSampleRate(rate).setChannelMask(mask).setEncoding(AudioFormat.ENCODING_PCM_FLOAT).build())
                .setBufferSizeInBytes(maxOf(min * 4, BLOCK_FRAMES * channels * 4 * 4))
                .build()
            if (rec.state == AudioRecord.STATE_INITIALIZED) rec else { rec.release(); null }
        } catch (_: Throwable) {
            null
        }
    }

    /** Switches off the platform's own effects on this session (automatic gain, noise suppression, echo cancelling) and reports what happened. */
    private fun disableEffects(rec: AudioRecord): String {
        val parts = ArrayList<String>()
        val kinds = listOf<Triple<String, Boolean, () -> AudioEffect?>>(
            Triple("AGC", AutomaticGainControl.isAvailable()) { AutomaticGainControl.create(rec.audioSessionId) },
            Triple("NS", NoiseSuppressor.isAvailable()) { NoiseSuppressor.create(rec.audioSessionId) },
            Triple("AEC", AcousticEchoCanceler.isAvailable()) { AcousticEchoCanceler.create(rec.audioSessionId) },
        )
        for ((name, available, make) in kinds) {
            if (!available) { parts.add("$name:なし"); continue }
            val e = make()
            if (e == null) { parts.add("$name:作れない"); continue }
            val code = e.setEnabled(false)
            parts.add("$name:" + if (code == AudioEffect.SUCCESS && !e.enabled) "無効にした" else "切れない")
            effects.add(e)
        }
        return parts.joinToString(" ")
    }

    // ---- test signal: 1 kHz left (-12 dBFS), 700 Hz right (-18 dBFS), paced like a real input
    private fun openTest(stereo: Boolean, rate: Int): Info {
        val channels = if (stereo) 2 else 1
        running = true
        thread = Thread({
            val data = FloatArray(BLOCK_FRAMES * channels)
            val planar = FloatArray(BLOCK_FRAMES * channels)
            var phaseL = 0.0
            var phaseR = 0.0
            val t0 = System.nanoTime()
            var sent = 0L
            while (running) {
                for (i in 0 until BLOCK_FRAMES) {
                    data[i * channels] = (0.25 * sin(phaseL)).toFloat()
                    if (channels == 2) data[i * channels + 1] = (0.125 * sin(phaseR)).toFloat()
                    phaseL += 2 * PI * 1000.0 / rate
                    phaseR += 2 * PI * 700.0 / rate
                }
                val frames = Planar.planarise(data, BLOCK_FRAMES * channels, channels, swap && channels == 2, planar)
                listener?.invoke(planar, channels, frames)
                sent += BLOCK_FRAMES
                val due = t0 + sent * 1_000_000_000L / rate
                val wait = (due - System.nanoTime()) / 1_000_000L
                if (wait > 0) try { Thread.sleep(wait) } catch (_: InterruptedException) { break }
            }
            running = false
        }, "capture-test").also { it.start() }
        return Info(channels, rate, "TEST", "テスト信号 (L 1kHz / R 700Hz)", "")
    }
}
