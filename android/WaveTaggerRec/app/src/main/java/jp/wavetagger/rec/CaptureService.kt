package jp.wavetagger.rec

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.IBinder
import android.os.PowerManager

/** Keeps the process alive (and the microphone allowed) while the microphone is open, with a notification the user can see. */
class CaptureService : Service() {
    private var wake: PowerManager.WakeLock? = null
    private var startedAt = 0L

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        wake?.let { if (it.isHeld) it.release() }
        wake = null
        super.onDestroy()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) { // the notification's stop button: works with the screen off and the page not running
            try { RecordingControl.stop(this, restartService = false) } catch (e: Throwable) { /* it was not recording */ }
            if (!MainActivity.visible) { // nobody is looking at the screen: the microphone is not needed any more either
                Capture.close()
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
                return START_NOT_STICKY
            }
        }
        val recording = Recorder.isRecording
        if (recording && wake == null) { // the CPU must not sleep while a recording is being written with the screen off
            wake = getSystemService(PowerManager::class.java).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "wtrec:record").also { it.acquire(6 * 3600_000L) }
            startedAt = System.currentTimeMillis()
        } else if (!recording) {
            wake?.let { if (it.isHeld) it.release() }
            wake = null
        }
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(NotificationChannel(CHANNEL, "録音", NotificationManager.IMPORTANCE_LOW))

        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val builder = Notification.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.ic_btn_speak_now)
            .setContentTitle("WaveTagger Rec")
            .setContentText(if (recording) "録音中" else "マイクを使っています")
            .setContentIntent(open)
            .setOngoing(true)
        if (recording) {
            val stop = PendingIntent.getService(this, 2, Intent(this, CaptureService::class.java).setAction(ACTION_STOP), PendingIntent.FLAG_IMMUTABLE)
            builder.setWhen(startedAt).setShowWhen(true).setUsesChronometer(true) // the time since the recording began, also on the lock screen
                .addAction(Notification.Action.Builder(null, "停止", stop).build())
        }
        startForeground(NOTIFICATION_ID, builder.build(), ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE)
        return START_NOT_STICKY
    }

    companion object {
        const val EXTRA_RECORDING = "recording"
        const val ACTION_STOP = "jp.wavetagger.rec.STOP"
        private const val CHANNEL = "rec"
        private const val NOTIFICATION_ID = 1
    }
}
