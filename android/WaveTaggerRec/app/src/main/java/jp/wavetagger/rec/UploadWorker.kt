package jp.wavetagger.rec

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import androidx.work.Constraints
import androidx.work.ExistingWorkPolicy
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.Worker
import androidx.work.WorkerParameters
import java.io.File

/**
 * Sends the recordings that are not in Drive yet, one after the other, when there is a network. It goes on without the screen, retries
 * by itself (with growing waits) after a network or server problem, and stops with a notification when Google's consent is needed.
 * A recording is only ever marked as sent after Drive confirmed the whole file.
 */
class UploadWorker(private val context: Context, params: WorkerParameters) : Worker(context, params) {

    override fun doWork(): Result {
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val folder = prefs.getString("folder", "") ?: ""
        Recordings.init(context.filesDir)
        val todo = Recordings.list().filter { it.status != "done" && File(Recordings.dir, it.name).exists() }
        if (todo.isEmpty()) return Result.success()
        if (folder.isEmpty()) { prefs.edit().putString("uploadError", "保存先の Drive フォルダが未設定です").apply(); return Result.failure() }

        val token = when (val a = GoogleAuth.silent(context)) {
            is GoogleAuth.Outcome.Token -> a.value
            is GoogleAuth.Outcome.NeedsConsent -> return needConnect(prefs)
            is GoogleAuth.Outcome.Failed -> { prefs.edit().putString("uploadError", a.message).apply(); return Result.retry() }
        }
        prefs.edit().putBoolean("authOk", true).apply()

        val uploader = DriveUploader()
        for (m in todo) {
            val file = File(Recordings.dir, m.name)
            val key = "session:${m.name}:${file.length()}"
            try {
                uploader.upload(file, m.name, folder, token, prefs.getString(key, null)) { s ->
                    prefs.edit().apply { if (s == null) remove(key) else putString(key, s) }.apply()
                }
                Recordings.markSent(m.name)
                prefs.edit().remove("uploadError").apply()
            } catch (e: DriveUploader.AuthException) {
                return needConnect(prefs)
            } catch (e: DriveUploader.RetryException) {
                prefs.edit().putString("uploadError", e.message).apply()
                return Result.retry()
            } catch (e: DriveUploader.FatalException) {
                prefs.edit().putString("uploadError", e.message).apply()
                return Result.failure() // not retried: it would fail the same way; the history says why
            }
        }
        return Result.success()
    }

    private fun needConnect(prefs: android.content.SharedPreferences): Result {
        prefs.edit().putBoolean("authOk", false).putString("uploadError", "Google への接続が必要です").apply()
        val manager = context.getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(NotificationChannel(CHANNEL, "送信", NotificationManager.IMPORTANCE_DEFAULT))
        val open = PendingIntent.getActivity(context, 1, Intent(context, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        manager.notify(NOTIFICATION_ID, Notification.Builder(context, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_sys_upload)
            .setContentTitle("WaveTagger Rec")
            .setContentText("Google への接続が必要です。開いて「Google に接続」を押してください")
            .setContentIntent(open).setAutoCancel(true).build())
        return Result.failure()
    }

    companion object {
        const val PREFS = "wtrec"
        private const val CHANNEL = "upload"
        private const val NOTIFICATION_ID = 2

        /** The setting "send by itself when a recording is stopped" (on by default). A manual send always works. */
        fun auto(context: Context): Boolean = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean("autoUpload", true)

        /** (Re)starts the sending. Safe to call at any time: a half-sent file is continued, not sent twice. */
        fun enqueue(context: Context) {
            val request = OneTimeWorkRequestBuilder<UploadWorker>()
                .setConstraints(Constraints.Builder().setRequiredNetworkType(NetworkType.CONNECTED).build())
                .build()
            WorkManager.getInstance(context).enqueueUniqueWork("upload", ExistingWorkPolicy.REPLACE, request)
        }
    }
}
