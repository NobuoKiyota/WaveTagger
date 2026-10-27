package jp.wavetagger.rec

import android.content.Context
import android.content.Intent
import android.util.Log
import androidx.core.content.ContextCompat
import org.json.JSONArray
import org.json.JSONObject

/**
 * Ends a recording. It is called from the screen and from the notification's stop button (the phone may be locked, the page not running),
 * so everything that follows a recording lives here and not in the activity.
 */
object RecordingControl {
    private const val TAG = "WTRec"
    @Volatile private var last: JSONObject? = null

    /** The result of the stop that just happened (a second call, e.g. from the page after the notification stopped it, gets the same result). */
    @Synchronized
    fun stop(context: Context, restartService: Boolean = true): JSONObject {
        if (!Recorder.isRecording) return last ?: throw IllegalStateException("録音していません")
        val app = context.applicationContext
        val r = Recorder.stop(Capture.info?.source ?: "")
        val result = JSONObject().put("files", JSONArray(r.files)).put("seconds", r.seconds).put("peak", r.peak.toDouble())
            .put("silent", r.silent).put("bad", r.badSamples).put("error", r.error ?: JSONObject.NULL)
        last = result
        if (restartService) ContextCompat.startForegroundService(app, Intent(app, CaptureService::class.java)) // back to the plain notification
        Thread { copyToDownloads(app) }.start()
        if (!r.silent && UploadWorker.auto(app)) UploadWorker.enqueue(app) // to Drive when there is a network (and Google is connected)
        return result
    }

    /** A copy in Download/WaveTagger for listening back and for getting the files to the PC (the setting `copyDownloads`, on by default). */
    fun copyToDownloads(context: Context) {
        val prefs = context.getSharedPreferences(UploadWorker.PREFS, Context.MODE_PRIVATE)
        if (prefs.getBoolean("copyDownloads", true)) try { Downloads.copyMissing(context) } catch (e: Throwable) { Log.w(TAG, "copy failed: $e") }
    }
}
