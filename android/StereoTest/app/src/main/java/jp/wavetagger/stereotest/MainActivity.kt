package jp.wavetagger.stereotest

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.media.AudioDeviceInfo
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import kotlin.math.abs
import kotlin.math.log10
import kotlin.math.sqrt

/**
 * Does the built-in microphone give two different channels, and through which audio source?
 * Each source is recorded for 6 seconds as stereo; the left and right channels are compared.
 */
class MainActivity : Activity() {
    private lateinit var text: TextView
    private lateinit var button: Button
    private val ui = Handler(Looper.getMainLooper())
    private var running = false

    private class Source(val id: Int, val name: String)

    private val sources = listOf(
        Source(MediaRecorder.AudioSource.MIC, "MIC"),
        Source(MediaRecorder.AudioSource.CAMCORDER, "CAMCORDER"),
        Source(MediaRecorder.AudioSource.UNPROCESSED, "UNPROCESSED"),
        Source(MediaRecorder.AudioSource.VOICE_RECOGNITION, "VOICE_RECOGNITION"),
        Source(MediaRecorder.AudioSource.VOICE_COMMUNICATION, "VOICE_COMMUNICATION"),
    )

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        button = Button(this).apply {
            this.text = "テスト開始 (入力元ごとに 6 秒)"
            setOnClickListener { start() }
        }
        val recorderButton = Button(this).apply {
            this.text = "ステレオ録音 (メーターと WAV 保存)"
            setOnClickListener { startActivity(Intent(this@MainActivity, RecordActivity::class.java)) }
        }
        text = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 11f
            setTextIsSelectable(true)
        }
        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(24, 24, 24, 24)
            addView(recorderButton)
            addView(button)
            addView(
                ScrollView(this@MainActivity).apply { addView(text) },
                LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f),
            )
        }
        setContentView(column)
        log(deviceInfo())
        log("\n録音中の 6 秒間に、スマホの左端を指でトントン → 右端をトントン、の順に叩いてください\n(声でも構いません。左右の差が出るほど判定しやすくなります)\n")
    }

    private fun log(s: String) {
        ui.post { text.append(s + "\n") }
    }

    private fun start() {
        if (running) return
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 1)
            return
        }
        running = true
        button.isEnabled = false
        Thread {
            for ((i, s) in sources.withIndex()) {
                ui.post { button.text = "録音中: ${s.name} (${i + 1}/${sources.size})" }
                try {
                    log(testSource(s))
                } catch (e: Throwable) {
                    log("[${s.name}] エラー: $e")
                }
                Thread.sleep(600)
            }
            log("\n== 終了 ==  この画面を長押しでコピーできます")
            ui.post {
                button.text = "もう一度テスト"
                button.isEnabled = true
                running = false
            }
        }.start()
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED) start()
        else log("マイクの許可がありません")
    }

    private fun deviceInfo(): String {
        val sb = StringBuilder()
        sb.append("${Build.MANUFACTURER} ${Build.MODEL}  Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})\n")
        val am = getSystemService(AUDIO_SERVICE) as AudioManager
        sb.append("入力デバイス:\n")
        for (d in am.getDevices(AudioManager.GET_DEVICES_INPUTS)) {
            sb.append("  ${typeName(d.type)} \"${d.productName}\" ch=${d.channelCounts.joinToString(",")} rate=${d.sampleRates.joinToString(",")}\n")
        }
        try {
            val mics = am.microphones
            sb.append("マイク ${mics.size} 個:\n")
            for (m in mics) {
                val p = m.position
                sb.append("  ${m.description} id=${m.id} 位置(${p.x},${p.y},${p.z}) 向き=${m.directionality} 場所=${m.location}\n")
            }
        } catch (e: Throwable) {
            sb.append("マイク情報を取得できません: $e\n")
        }
        return sb.toString()
    }

    private fun typeName(t: Int) = when (t) {
        AudioDeviceInfo.TYPE_BUILTIN_MIC -> "内蔵マイク"
        AudioDeviceInfo.TYPE_USB_DEVICE -> "USB"
        AudioDeviceInfo.TYPE_USB_HEADSET -> "USB ヘッドセット"
        AudioDeviceInfo.TYPE_WIRED_HEADSET -> "有線ヘッドセット"
        AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> "Bluetooth"
        else -> "type$t"
    }

    private fun db(v: Double) = if (v > 1e-9) 20 * log10(v) else -180.0
    private fun f1(v: Double) = String.format("%.1f", v)

    private fun testSource(s: Source): String {
        val rate = 48000
        val seconds = 6
        val mask = AudioFormat.CHANNEL_IN_STEREO
        var rec: AudioRecord? = null
        var encName = ""
        for ((enc, name) in listOf(AudioFormat.ENCODING_PCM_FLOAT to "float32", AudioFormat.ENCODING_PCM_16BIT to "int16")) {
            val min = AudioRecord.getMinBufferSize(rate, mask, enc)
            if (min <= 0) continue
            val r = try {
                AudioRecord.Builder()
                    .setAudioSource(s.id)
                    .setAudioFormat(AudioFormat.Builder().setSampleRate(rate).setChannelMask(mask).setEncoding(enc).build())
                    .setBufferSizeInBytes(min * 4)
                    .build()
            } catch (e: Throwable) {
                null
            }
            if (r != null && r.state == AudioRecord.STATE_INITIALIZED) {
                rec = r
                encName = name
                break
            }
            r?.release()
        }
        if (rec == null) return "[${s.name}] 開けません (ステレオ ${rate} Hz)"

        val frames = rate * seconds
        val data = FloatArray(frames * 2)
        var got = 0
        val chunk = FloatArray(4096)
        val chunk16 = ShortArray(4096)
        val notes = StringBuilder()
        try {
            rec.startRecording()
            notes.append("  経路: ${rec.routedDevice?.let { typeName(it.type) + " " + it.productName } ?: "?"}  チャンネル=${rec.channelCount}  形式=$encName\n")
            if (Build.VERSION.SDK_INT >= 29) {
                try {
                    val active = rec.activeMicrophones
                    notes.append("  使われているマイク: ${active.size} 個 ")
                    for (m in active) notes.append("[${m.description} ch=${m.channelMapping.joinToString("/") { "${it.first}:${it.second}" }}] ")
                    notes.append("\n")
                } catch (e: Throwable) {
                    notes.append("  使われているマイク: 取得できません ($e)\n")
                }
            }
            while (got < data.size) {
                val want = minOf(chunk.size, data.size - got)
                val n = if (encName == "float32") {
                    val k = rec.read(chunk, 0, want, AudioRecord.READ_BLOCKING)
                    if (k > 0) System.arraycopy(chunk, 0, data, got, k)
                    k
                } else {
                    val k = rec.read(chunk16, 0, want)
                    for (j in 0 until maxOf(k, 0)) data[got + j] = chunk16[j] / 32768f
                    k
                }
                if (n <= 0) break
                got += n
            }
            rec.stop()
        } finally {
            rec.release()
        }
        val n = got / 2
        if (n < rate) return "[${s.name}] 録音できたのは $n フレームだけでした\n$notes"
        return "[${s.name}]\n$notes${analyse(data, n, rate)}"
    }

    private fun analyse(d: FloatArray, n: Int, rate: Int): String {
        var ll = 0.0; var rr = 0.0; var lr = 0.0
        var pl = 0.0; var pr = 0.0; var maxDiff = 0.0
        for (i in 0 until n) {
            val l = d[2 * i].toDouble(); val r = d[2 * i + 1].toDouble()
            ll += l * l; rr += r * r; lr += l * r
            if (abs(l) > pl) pl = abs(l)
            if (abs(r) > pr) pr = abs(r)
            val df = abs(l - r)
            if (df > maxDiff) maxDiff = df
        }
        val corr = if (ll > 0 && rr > 0) lr / sqrt(ll * rr) else 0.0

        // level difference (L - R, dB) per quarter second, among the windows that have a sound: how much it moves when the sound moves
        val win = rate / 4
        var minD = 1e9; var maxD = -1e9; var loud = 0
        var w = 0
        while (w + win <= n) {
            var a = 0.0; var b = 0.0
            for (i in w until w + win) { a += d[2 * i] * d[2 * i].toDouble(); b += d[2 * i + 1] * d[2 * i + 1].toDouble() }
            val ra = sqrt(a / win); val rb = sqrt(b / win)
            if (db(maxOf(ra, rb)) > -50) {
                val diff = db(ra) - db(rb)
                if (diff < minD) minD = diff
                if (diff > maxD) maxD = diff
                loud++
            }
            w += win
        }

        val verdict = when {
            pl < 1e-4 && pr < 1e-4 -> "無音でした (マイクの音が届いていません)"
            maxDiff < 1e-6 -> "左右が完全に同一 = モノラルの複製 (ステレオではない)"
            corr > 0.995 && (maxD - minD) < 1.0 -> "ほぼ同一 (ステレオとして使えない)"
            else -> "左右が別の音 = ステレオの可能性が高い"
        }
        val sb = StringBuilder()
        sb.append("  ピーク L ${f1(db(pl))} / R ${f1(db(pr))} dBFS   相関 ${String.format("%.3f", corr)}   最大差 ${f1(db(maxDiff))} dBFS\n")
        if (loud > 0) sb.append("  L-R レベル差 ${f1(minD)} 〜 ${f1(maxD)} dB (音のある ${loud} 区間)\n")
        sb.append("  判定: $verdict\n")
        return sb.toString()
    }
}
