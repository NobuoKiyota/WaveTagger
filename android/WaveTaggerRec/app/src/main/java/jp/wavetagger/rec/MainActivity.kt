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
    private var recording = false // the screen is recording: the left/right assignment must not change now
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
                "setRecording" -> { recording = args.optBoolean("on"); if (!recording) updateSwap(); resolve(id, true, "{}") }
                "getState" -> resolve(id, true, state().toString())
                "getSettings" -> resolve(id, true, settings().toString())
                "setSettings" -> {
                    prefs.edit()
                        .putString("source", args.optString("source", "MIC"))
                        .putBoolean("swap", args.optBoolean("swap", false))
                        .apply()
                    updateSwap()
                    resolve(id, true, settings().toString())
                }
                else -> resolve(id, false, "unknown method: $method")
            }
        } catch (e: Throwable) {
            resolve(id, false, e.message ?: e.toString())
        }
    }

    private fun settings() = JSONObject()
        .put("source", prefs.getString("source", "MIC"))
        .put("swap", prefs.getBoolean("swap", false))

    private fun state() = JSONObject()
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
        Capture.swap = (rotation == Surface.ROTATION_90) != prefs.getBoolean("swap", false)
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
        private const val MAX_INFLIGHT = 6
    }
}
