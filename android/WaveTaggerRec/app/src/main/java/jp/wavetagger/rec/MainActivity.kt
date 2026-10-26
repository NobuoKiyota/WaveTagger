package jp.wavetagger.rec

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Base64
import android.util.Log
import android.view.Surface
import android.webkit.ConsoleMessage
import android.webkit.JavascriptInterface
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.core.content.ContextCompat
import androidx.webkit.WebMessageCompat
import androidx.webkit.WebViewAssetLoader
import androidx.webkit.WebViewCompat
import androidx.webkit.WebViewFeature
import org.json.JSONObject
import java.util.concurrent.atomic.AtomicInteger

/**
 * The shell: the browser version's screens (docs/, copied into assets at build time) in a WebView, and a bridge to the microphone.
 * Only the bundled page is ever shown; any other address goes to the phone's own browser.
 */
class MainActivity : Activity() {
    private lateinit var web: WebView
    private lateinit var prefs: SharedPreferences
    private val ui = Handler(Looper.getMainLooper())

    private var visible = false
    private var recording = Recorder.isRecording // the screen is recording: the left/right assignment must not change now
    private var pendingOpen: Pair<Int, String>? = null

    private val inflight = AtomicInteger(0)
    @Volatile private var dropped = 0L
    @Volatile private var sentBlocks = 0L
    @Volatile private var sentBytes = 0L
    private var arrayBuffers = false

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: android.os.Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = getSharedPreferences("wtrec", MODE_PRIVATE)
        Recordings.init(filesDir)
        if (!Recorder.isRecording) Thread { // a recording cut off by a crash is repaired; sent ones are removed after the keep time
            Recordings.recover()
            Recordings.cleanup(prefs.getInt("keepDays", 7))
            copyToDownloads()
            if (prefs.getBoolean("authOk", false) && Recordings.list().any { it.status != "done" }) UploadWorker.enqueue(this)
        }.start()

        val loader = WebViewAssetLoader.Builder()
            .setDomain(DOMAIN)
            .addPathHandler("/assets/", WebViewAssetLoader.AssetsPathHandler(this))
            .build()

        web = WebView(this)
        web.settings.apply {
            javaScriptEnabled = true
            domStorageEnabled = true
            allowFileAccess = false
            allowContentAccess = false
            mediaPlaybackRequiresUserGesture = false
        }
        web.webViewClient = object : WebViewClient() {
            override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? =
                loader.shouldInterceptRequest(request.url)

            override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
                if (request.url.host == DOMAIN) return false
                try { startActivity(Intent(Intent.ACTION_VIEW, request.url)) } catch (_: Throwable) { /* no browser */ }
                return true // never navigate away from the bundled page
            }
        }
        web.webChromeClient = object : WebChromeClient() {
            override fun onConsoleMessage(m: ConsoleMessage): Boolean {
                Log.d(TAG, "js: ${m.message()} (${m.sourceId()}:${m.lineNumber()})")
                return true
            }
        }
        web.addJavascriptInterface(Bridge(), "WTNativeBridge")
        arrayBuffers = WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_ARRAY_BUFFER)
        setContentView(web)
        web.loadUrl("https://$DOMAIN/assets/web/index.html")

        Capture.listener = { planar, channels, frames -> onBlock(planar, channels, frames) }
        updateSwap()
    }

    // ---------------------------------------------------------------- the bridge (called from the page; the work is done on the UI thread)
    inner class Bridge {
        @JavascriptInterface
        fun call(id: Int, method: String, json: String) {
            ui.post { handle(id, method, json) }
        }
    }

    private fun resolve(id: Int, ok: Boolean, json: String) {
        web.evaluateJavascript("window.__wtResolve($id,$ok,${JSONObject.quote(json)})", null)
    }

    private fun handle(id: Int, method: String, json: String) {
        try {
            val args = JSONObject(json.ifEmpty { "{}" })
            when (method) {
                "open" -> openMicrophone(id, json)
                "close" -> { closeMicrophone(); resolve(id, true, "{}") }
                "startRecording" -> resolve(id, true, startRecording(args.optInt("bits", 24)).toString())
                "stopRecording" -> resolve(id, true, stopRecording().toString())
                "listRecordings" -> resolve(id, true, listRecordings().toString())
                "deleteRecording" -> resolve(id, true, JSONObject().put("deleted", Recordings.delete(args.optString("name"), false)).toString())
                "getState" -> resolve(id, true, state().toString())
                "getSettings" -> resolve(id, true, settings().toString())
                "setSettings" -> {
                    prefs.edit().apply {
                        putString("source", args.optString("source", prefs.getString("source", "MIC")))
                        if (args.has("folder")) putString("folder", args.optString("folder").trim())
                    }.apply()
                    resolve(id, true, settings().toString())
                }
                "connect" -> connectGoogle(id)
                "upload" -> { UploadWorker.enqueue(this); resolve(id, true, state().toString()) }
                "flipSwap" -> { // the automatic left/right adjustment found this rotation reversed
                    if (recording) throw IllegalStateException("録音中は変えられません")
                    val key = "flip" + rotationName()
                    prefs.edit().putBoolean(key, !prefs.getBoolean(key, false)).apply()
                    updateSwap()
                    resolve(id, true, state().toString())
                }
                else -> resolve(id, false, "unknown method: $method")
            }
        } catch (e: Throwable) {
            resolve(id, false, e.message ?: e.toString())
        }
    }

    private fun settings() = JSONObject()
        .put("source", prefs.getString("source", "MIC"))
        .put("flip90", prefs.getBoolean("flip90", false))
        .put("flip270", prefs.getBoolean("flip270", false))
        .put("folder", prefs.getString("folder", ""))

    // ---------------------------------------------------------------- Google (consent screen only when Google asks for it)
    private var pendingConnect: Int? = null

    private fun connectGoogle(id: Int) {
        GoogleAuth.client(this).authorize(GoogleAuth.request())
            .addOnSuccessListener { r ->
                val pending = r.pendingIntent
                if (r.hasResolution() && pending != null) {
                    pendingConnect = id
                    try { startIntentSenderForResult(pending.intentSender, REQUEST_GOOGLE, null, 0, 0, 0) }
                    catch (e: Throwable) { pendingConnect = null; resolve(id, false, e.message ?: e.toString()) }
                } else {
                    prefs.edit().putBoolean("authOk", true).remove("uploadError").apply()
                    UploadWorker.enqueue(this)
                    resolve(id, true, state().toString())
                }
            }
            .addOnFailureListener { e -> resolve(id, false, "Google に接続できません: " + (e.message ?: e.toString())) }
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQUEST_GOOGLE) return
        val id = pendingConnect
        pendingConnect = null
        fun fail(why: String) {
            prefs.edit().putString("uploadError", why).apply()
            if (id != null) resolve(id, false, why)
        }
        try {
            val r = GoogleAuth.client(this).getAuthorizationResultFromIntent(data)
            if (r.accessToken != null) {
                prefs.edit().putBoolean("authOk", true).remove("uploadError").apply()
                UploadWorker.enqueue(this)
                if (id != null) resolve(id, true, state().toString())
            } else fail("Google の許可が得られませんでした (resultCode=$resultCode, トークンなし)")
        } catch (e: Throwable) {
            val code = (e as? com.google.android.gms.common.api.ApiException)?.statusCode
            fail("Google の許可が得られませんでした (resultCode=$resultCode" + (if (code != null) ", status=$code" else "") +
                (if (e.message != null) ", " + e.message else "") + ")")
        }
    }

    private fun state() = JSONObject()
        .put("folderSet", !prefs.getString("folder", "").isNullOrEmpty())
        .put("authOk", prefs.getBoolean("authOk", false))
        .put("uploadError", prefs.getString("uploadError", "") ?: "")
        .put("waiting", Recordings.list().count { it.status != "done" })
        .put("open", Capture.isOpen)
        .put("recording", recording)
        .put("transport", if (arrayBuffers) "arraybuffer" else "base64")
        .put("blocks", sentBlocks)
        .put("bytes", sentBytes)
        .put("dropped", dropped)
        .put("rotation", rotationName())
        .put("swapped", Capture.swap)

    // ---------------------------------------------------------------- the microphone
    private fun openMicrophone(id: Int, json: String) {
        val needed = arrayListOf(Manifest.permission.RECORD_AUDIO)
        if (Build.VERSION.SDK_INT >= 33) needed.add(Manifest.permission.POST_NOTIFICATIONS)
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            pendingOpen = id to json
            requestPermissions(needed.toTypedArray(), REQUEST_AUDIO)
            return
        }
        doOpen(id, json)
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        val pending = pendingOpen ?: return
        pendingOpen = null
        val index = permissions.indexOf(Manifest.permission.RECORD_AUDIO)
        if (index >= 0 && grantResults[index] == PackageManager.PERMISSION_GRANTED) doOpen(pending.first, pending.second)
        else resolve(pending.first, false, "マイクの使用が許可されていません")
    }

    private fun doOpen(id: Int, json: String) {
        val args = JSONObject(json.ifEmpty { "{}" })
        val source = args.optString("source", "").ifEmpty { prefs.getString("source", "MIC")!! }
        try {
            ContextCompat.startForegroundService(this, Intent(this, CaptureService::class.java))
            updateSwap()
            val info = Capture.open(source, args.optBoolean("stereo", true), args.optInt("rate", 48000))
            resolve(id, true, JSONObject()
                .put("channels", info.channels).put("rate", info.rate).put("source", info.source)
                .put("label", info.label).put("effects", info.effects)
                .put("transport", if (arrayBuffers) "arraybuffer" else "base64")
                .put("rotation", rotationName()).put("swapped", Capture.swap)
                .toString())
        } catch (e: Throwable) {
            stopService(Intent(this, CaptureService::class.java))
            resolve(id, false, e.message ?: e.toString())
        }
    }

    private fun closeMicrophone() {
        Capture.close()
        stopService(Intent(this, CaptureService::class.java))
    }

    // ---------------------------------------------------------------- recording (the files are written by Recorder, which does not depend on the screen)
    private fun startRecording(bits: Int): JSONObject {
        val info = Capture.info ?: throw IllegalStateException("マイクが開いていません")
        if (recording) throw IllegalStateException("すでに録音中です")
        updateSwap() // the left/right assignment is fixed from here to the end of the recording
        val comment = "device=${Build.MANUFACTURER} ${Build.MODEL}; source=${info.source}; mics=2 built-in (AB, about 14 cm apart)" +
            "; swap=${Capture.swap}; rotation=${rotationName()}; effects=${info.effects.ifEmpty { "-" }}" +
            "; content=16-bit precision (the phone delivers a 16-bit signal)"
        val stamp = java.text.SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", java.util.Locale.US).format(java.util.Date())
        val name = Recorder.start(Recordings.dir, stamp, info.channels, info.rate, if (bits == 16) 16 else 24, comment)
        recording = true
        ContextCompat.startForegroundService(this, Intent(this, CaptureService::class.java).putExtra(CaptureService.EXTRA_RECORDING, true))
        return JSONObject().put("name", name)
    }

    private fun stopRecording(): JSONObject {
        val source = Capture.info?.source ?: ""
        val r = Recorder.stop(source)
        recording = false
        updateSwap()
        ContextCompat.startForegroundService(this, Intent(this, CaptureService::class.java)) // back to the plain notification
        Thread { copyToDownloads() }.start()
        if (!r.silent) UploadWorker.enqueue(this) // to Drive when there is a network (and Google is connected)
        return JSONObject().put("files", org.json.JSONArray(r.files)).put("seconds", r.seconds).put("peak", r.peak.toDouble())
            .put("silent", r.silent).put("bad", r.badSamples).put("error", r.error ?: JSONObject.NULL)
    }

    /** A copy in Download/WaveTagger for listening back and for getting the files to the PC (the setting `copyDownloads`, on by default). */
    private fun copyToDownloads() {
        if (prefs.getBoolean("copyDownloads", true)) try { Downloads.copyMissing(this) } catch (e: Throwable) { Log.w(TAG, "copy failed: $e") }
    }

    private fun listRecordings(): org.json.JSONArray {
        val a = org.json.JSONArray()
        for (m in Recordings.list()) a.put(JSONObject()
            .put("name", m.name).put("seconds", m.seconds).put("channels", m.channels).put("rate", m.rate).put("bits", m.bits)
            .put("status", m.status).put("recovered", m.recovered).put("createdAt", m.createdAt))
        return a
    }

    // ---------------------------------------------------------------- blocks to the page
    private fun onBlock(planar: FloatArray, channels: Int, frames: Int) {
        if (!visible) return // the page does not run in the background: nothing is queued up for it
        if (inflight.get() >= MAX_INFLIGHT) { dropped++; return }
        inflight.incrementAndGet()
        val bytes = Planar.toBytes(planar, frames, channels)
        ui.post {
            try {
                if (arrayBuffers) {
                    WebViewCompat.postWebMessage(web, WebMessageCompat(bytes), Uri.parse("https://$DOMAIN"))
                } else {
                    web.evaluateJavascript("window.__wtBlock('" + Base64.encodeToString(bytes, Base64.NO_WRAP) + "')", null)
                }
                sentBlocks++
                sentBytes += bytes.size
            } finally {
                inflight.decrementAndGet()
            }
        }
    }

    // ---------------------------------------------------------------- left / right
    /**
     * Held in landscape, the two microphones (bottom end and top end of the phone) are left and right. Turning the phone over
     * (the other landscape) puts them the other way round, so the channels change places.
     * NOT CALIBRATED YET: which rotation needs the swap is a guess for the Galaxy S21 until the M0 measurement (the "swap" setting corrects it).
     */
    private fun updateSwap() {
        if (recording) return // never change the assignment in the middle of a recording
        val rotation = windowManager.defaultDisplay.rotation
        // the guess (swap at 90°) corrected by what the automatic adjustment found for this very rotation
        Capture.swap = (rotation == Surface.ROTATION_90) != prefs.getBoolean("flip" + rotationName(), false)
    }

    private fun rotationName() = when (windowManager.defaultDisplay.rotation) {
        Surface.ROTATION_0 -> "0"
        Surface.ROTATION_90 -> "90"
        Surface.ROTATION_180 -> "180"
        else -> "270"
    }

    override fun onConfigurationChanged(newConfig: android.content.res.Configuration) {
        super.onConfigurationChanged(newConfig)
        updateSwap()
    }

    // ---------------------------------------------------------------- lifecycle
    override fun onStart() {
        super.onStart()
        visible = true
        web.onResume()
    }

    override fun onStop() {
        visible = false
        // a level check does not run in the background (battery, the microphone-in-use indicator); a recording is M2's business
        if (Capture.isOpen && !recording) {
            closeMicrophone()
            web.evaluateJavascript("window.__wtClosed && window.__wtClosed()", null)
        }
        super.onStop()
    }

    override fun onDestroy() {
        Capture.listener = null
        if (!recording) closeMicrophone()
        web.destroy()
        super.onDestroy()
    }

    companion object {
        private const val TAG = "WTRec"
        private const val DOMAIN = "appassets.androidplatform.net"
        private const val REQUEST_AUDIO = 7
        private const val REQUEST_GOOGLE = 8
        private const val MAX_INFLIGHT = 6
    }
}
