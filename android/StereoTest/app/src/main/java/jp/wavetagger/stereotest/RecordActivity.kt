package jp.wavetagger.stereotest

import android.Manifest
import android.app.Activity
import android.content.ContentValues
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.media.audiofx.AcousticEchoCanceler
import android.media.audiofx.AudioEffect
import android.media.audiofx.AutomaticGainControl
import android.media.audiofx.NoiseSuppressor
import android.view.Surface
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.widget.Button
import android.widget.CheckBox
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.TextView
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.abs
import kotlin.math.log10
import kotlin.math.roundToInt

/**
 * Live left/right meters for one audio source, and a 24-bit stereo WAV written to Download/WaveTagger/ so it can be listened to
 * and opened on the PC. Left is channel 0, right is channel 1 (the checkbox swaps them).
 */
class RecordActivity : Activity() {
    private val ui = Handler(Looper.getMainLooper())
    private val rate = 48000

    private lateinit var group: RadioGroup
    private lateinit var swapBox: CheckBox
    private lateinit var startButton: Button
    private lateinit var recButton: Button
    private lateinit var barL: ProgressBar
    private lateinit var barR: ProgressBar
    private lateinit var textL: TextView
    private lateinit var textR: TextView
    private lateinit var info: TextView

    @Volatile private var running = false
    @Volatile private var swap = false
    private var thread: Thread? = null

    private val lock = Any()
    private var writer: WavWriter? = null
    private var holdL = 0.0
    private var holdR = 0.0
    private var holdAt = 0L
    private lateinit var rotationText: TextView
    private val effects = ArrayList<AudioEffect>() // kept alive for as long as the microphone is open

    private class WavWriter(val uri: Uri, val name: String, private val out: FileOutputStream, private val rate: Int) {
        var frames = 0L
        private val channel = out.channel

        init {
            out.write(ByteArray(44)) // the header is written at the end, when the length is known
        }

        fun write(data: FloatArray, count: Int) {
            val bytes = ByteArray(count * 3)
            var p = 0
            for (i in 0 until count) {
                var s = data[i]
                if (s > 1f) s = 1f else if (s < -1f) s = -1f
                val v = (s * 8388607f).roundToInt()
                bytes[p++] = (v and 0xFF).toByte()
                bytes[p++] = ((v shr 8) and 0xFF).toByte()
                bytes[p++] = ((v shr 16) and 0xFF).toByte()
            }
            out.write(bytes)
            frames += count / 2
        }

        fun finish() {
            val dataBytes = frames * 6
            val h = ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN)
            h.put("RIFF".toByteArray()); h.putInt((36 + dataBytes).toInt()); h.put("WAVE".toByteArray())
            h.put("fmt ".toByteArray()); h.putInt(16); h.putShort(1); h.putShort(2)
            h.putInt(rate); h.putInt(rate * 6); h.putShort(6); h.putShort(24)
            h.put("data".toByteArray()); h.putInt(dataBytes.toInt())
            h.flip()
            channel.write(h, 0)
            out.flush()
            out.close()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        group = RadioGroup(this).apply {
            orientation = RadioGroup.HORIZONTAL
            addView(RadioButton(this@RecordActivity).apply { text = "MIC"; id = 1001; isChecked = true })
            addView(RadioButton(this@RecordActivity).apply { text = "CAMCORDER"; id = 1002 })
        }
        swapBox = CheckBox(this).apply {
            text = "左右を入れ替える"
            setOnCheckedChangeListener { _, on -> swap = on }
        }
        startButton = Button(this).apply { text = "メーター開始"; setOnClickListener { toggleCapture() } }
        recButton = Button(this).apply { text = "録音"; isEnabled = false; setOnClickListener { toggleRecording() } }
        barL = bar(); barR = bar()
        textL = label("L  --"); textR = label("R  --")
        rotationText = label("向き --").apply { textSize = 14f }
        info = TextView(this).apply { typeface = Typeface.MONOSPACE; textSize = 11f; setTextIsSelectable(true) }

        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(24, 24, 24, 24)
            addView(TextView(this@RecordActivity).apply { text = "入力元" })
            addView(group)
            addView(swapBox)
            addView(startButton)
            addView(rotationText)
            addView(textL); addView(barL)
            addView(textR); addView(barR)
            addView(recButton)
            addView(info)
        })
        info.text = "メーターを開始して、スマホの下端 / 上端を指でこすって、どちらのメーターが動くか見てください。\n録音すると Download/WaveTagger/ に 24bit WAV が保存されます。"
    }

    private fun bar() = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 600 }
    private fun label(s: String) = TextView(this).apply { text = s; typeface = Typeface.MONOSPACE; textSize = 16f }

    private fun toggleCapture() {
        if (running) { stopCapture(); return }
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 1)
            return
        }
        startCapture()
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED) startCapture()
    }

    private fun startCapture() {
        val source = if (group.checkedRadioButtonId == 1002) MediaRecorder.AudioSource.CAMCORDER else MediaRecorder.AudioSource.MIC
        val sourceName = if (source == MediaRecorder.AudioSource.CAMCORDER) "CAMCORDER" else "MIC"
        val min = AudioRecord.getMinBufferSize(rate, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_FLOAT)
        val rec = AudioRecord.Builder()
            .setAudioSource(source)
            .setAudioFormat(AudioFormat.Builder().setSampleRate(rate).setChannelMask(AudioFormat.CHANNEL_IN_STEREO).setEncoding(AudioFormat.ENCODING_PCM_FLOAT).build())
            .setBufferSizeInBytes(min * 4)
            .build()
        if (rec.state != AudioRecord.STATE_INITIALIZED) {
            rec.release()
            info.text = "マイクを開けません"
            return
        }
        running = true
        startButton.text = "メーター停止"
        recButton.isEnabled = true
        for (i in 0 until group.childCount) group.getChildAt(i).isEnabled = false
        holdL = 0.0; holdR = 0.0

        thread = Thread {
            val data = FloatArray(8192)
            var routed = false
            var t0 = System.currentTimeMillis()
            try {
                rec.startRecording()
                while (running) {
                    val n = rec.read(data, 0, data.size, AudioRecord.READ_BLOCKING)
                    if (n <= 0) break
                    if (swap) {
                        var i = 0
                        while (i + 1 < n) { val t = data[i]; data[i] = data[i + 1]; data[i + 1] = t; i += 2 }
                    }
                    var pl = 0.0; var pr = 0.0
                    var i = 0
                    while (i + 1 < n) {
                        val l = abs(data[i].toDouble()); val r = abs(data[i + 1].toDouble())
                        if (l > pl) pl = l
                        if (r > pr) pr = r
                        i += 2
                    }
                    synchronized(lock) { writer?.write(data, n) }
                    ui.post { meters(pl, pr) }
                    if (!routed && System.currentTimeMillis() - t0 > 800) {
                        routed = true
                        val route = describe(rec, sourceName)
                        ui.post { info.text = route }
                    }
                }
                rec.stop()
            } catch (e: Throwable) {
                ui.post { info.text = "エラー: $e" }
            } finally {
                for (e in effects) e.release()
                effects.clear()
                rec.release()
                ui.post { captureEnded() }
            }
        }.also { it.start() }
    }

    private fun describe(rec: AudioRecord, sourceName: String): String {
        val sb = StringBuilder("入力元 $sourceName  チャンネル=${rec.channelCount}\n")
        sb.append(disableEffects(rec))
        sb.append("向き: ${rotationName()}\n")
        sb.append("経路: ${rec.routedDevice?.let { it.productName.toString() + " (type" + it.type + ")" } ?: "(不明)"}\n")
        try {
            val active = rec.activeMicrophones
            sb.append("使われているマイク ${active.size} 個: ")
            for (m in active) sb.append("[${m.description} ch=${m.channelMapping.joinToString("/") { "${it.first}:${it.second}" }}] ")
        } catch (e: Throwable) {
            sb.append("使われているマイク: 取得できません")
        }
        return sb.toString()
    }

    private fun db(v: Double) = if (v > 1e-9) 20 * log10(v) else -180.0

    private fun rotationName(): String = when (windowManager.defaultDisplay.rotation) {
        Surface.ROTATION_0 -> "縦 (0°)"
        Surface.ROTATION_90 -> "横 (90°: 本体の上端が左、下端が右)"
        Surface.ROTATION_180 -> "縦逆さ (180°)"
        else -> "横 (270°: 本体の上端が右、下端が左)"
    }

    /** Asks the platform for each effect on this recording session and tries to switch it off. Returns what happened. */
    private fun disableEffects(rec: AudioRecord): String {
        for (e in effects) e.release()
        effects.clear()
        val sb = StringBuilder("端末側のエフェクト:\n")
        val kinds = listOf(
            Triple("自動音量調整 (AGC)", AutomaticGainControl.isAvailable()) { AutomaticGainControl.create(rec.audioSessionId) as AudioEffect? },
            Triple("ノイズ抑制 (NS)", NoiseSuppressor.isAvailable()) { NoiseSuppressor.create(rec.audioSessionId) as AudioEffect? },
            Triple("エコー除去 (AEC)", AcousticEchoCanceler.isAvailable()) { AcousticEchoCanceler.create(rec.audioSessionId) as AudioEffect? },
        )
        for ((name, available, make) in kinds) {
            if (!available) {
                sb.append("  $name: この端末には無い\n")
                continue
            }
            val e = make()
            if (e == null) {
                sb.append("  $name: あるが作れない\n")
                continue
            }
            val before = e.enabled
            val code = e.setEnabled(false)
            sb.append("  $name: 既定=${if (before) "有効" else "無効"} → 無効にする結果=${if (code == AudioEffect.SUCCESS) "成功" else "失敗($code)"} 今=${if (e.enabled) "有効" else "無効"}\n")
            effects.add(e)
        }
        return sb.toString()
    }

    private fun meters(pl: Double, pr: Double) {
        rotationText.text = "向き: " + rotationName()
        val now = System.currentTimeMillis()
        if (now - holdAt > 1500) { holdL = pl; holdR = pr; holdAt = now } // the held peak is renewed every 1.5 s
        if (pl > holdL) holdL = pl
        if (pr > holdR) holdR = pr
        barL.progress = ((db(pl) + 60) * 10).coerceIn(0.0, 600.0).toInt()
        barR.progress = ((db(pr) + 60) * 10).coerceIn(0.0, 600.0).toInt()
        textL.text = String.format("L  %6.1f dBFS  (保持 %6.1f)", db(pl), db(holdL))
        textR.text = String.format("R  %6.1f dBFS  (保持 %6.1f)", db(pr), db(holdR))
    }

    private fun stopCapture() {
        if (writer != null) toggleRecording()
        running = false
    }

    private fun captureEnded() {
        startButton.text = "メーター開始"
        recButton.isEnabled = false
        for (i in 0 until group.childCount) group.getChildAt(i).isEnabled = true
        barL.progress = 0; barR.progress = 0
    }

    private fun toggleRecording() {
        val current = synchronized(lock) { writer }
        if (current == null) {
            val name = SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.US).format(Date()) +
                "_" + (if (group.checkedRadioButtonId == 1002) "CAMCORDER" else "MIC") + (if (swap) "_swapped" else "") + ".wav"
            val values = ContentValues().apply {
                put(MediaStore.Downloads.DISPLAY_NAME, name)
                put(MediaStore.Downloads.MIME_TYPE, "audio/wav")
                put(MediaStore.Downloads.RELATIVE_PATH, "Download/WaveTagger")
                put(MediaStore.Downloads.IS_PENDING, 1)
            }
            val uri = contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
            val pfd = uri?.let { contentResolver.openFileDescriptor(it, "rw") }
            if (uri == null || pfd == null) { info.append("\n保存先を作れません"); return }
            val w = WavWriter(uri, name, FileOutputStream(pfd.fileDescriptor), rate)
            synchronized(lock) { writer = w }
            recButton.text = "録音停止"
            info.append("\n録音中: $name")
        } else {
            synchronized(lock) { writer = null }
            current.finish()
            val done = ContentValues().apply { put(MediaStore.Downloads.IS_PENDING, 0) }
            contentResolver.update(current.uri, done, null, null)
            recButton.text = "録音"
            info.append(String.format("\n保存しました: Download/WaveTagger/%s  (%.1f 秒)", current.name, current.frames.toDouble() / rate))
        }
    }

    override fun onDestroy() {
        running = false
        super.onDestroy()
    }
}
