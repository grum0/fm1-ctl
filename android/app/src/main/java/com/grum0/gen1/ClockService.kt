package com.grum0.gen1

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.drawable.Icon
import android.os.IBinder
import android.os.PowerManager

/**
 * Keeps the app alive, and the processor awake, while the screen is off or another app is in front: the clock and the
 * generators run in this app's own thread (cpp/gen1_jni.c), which Android would otherwise stop. It starts when the
 * app is opened and ends with "Stop and quit" on its notification, or when the app is swiped away.
 */
class ClockService : Service() {
    private var wake: PowerManager.WakeLock? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL, "Clock", NotificationManager.IMPORTANCE_LOW).apply {
                description = "Shown while GEN-1 keeps its clock and parts running"
            },
        )
        wake = (getSystemService(Context.POWER_SERVICE) as PowerManager)
            .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "gen1:clock")
            .apply { setReferenceCounted(false) }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_QUIT) {
            quit()
            return START_NOT_STICKY
        }
        val open = PendingIntent.getActivity(
            this, 0,
            Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_IMMUTABLE,
        )
        val stop = PendingIntent.getService(
            this, 1,
            Intent(this, ClockService::class.java).setAction(ACTION_QUIT),
            PendingIntent.FLAG_IMMUTABLE,
        )
        val note = Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_stat)
            .setContentTitle("GEN-1 is running")
            .setContentText("The clock and the parts keep going with the screen off.")
            .setContentIntent(open)
            .setOngoing(true)
            .addAction(
                Notification.Action.Builder(Icon.createWithResource(this, R.drawable.ic_stat), "Stop and quit", stop).build(),
            )
            .build()
        try {
            startForeground(1, note, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
        } catch (e: Exception) {
            // (not allowed just now.) Safe to stop: the service is started with startService, not
            // startForegroundService, so Android does not insist on it going to the foreground.
            stopSelf()
            return START_NOT_STICKY
        }
        if (wake?.isHeld != true) wake?.acquire(12L * 60 * 60 * 1000)
        return START_NOT_STICKY
    }

    /** The app swiped away from the recent apps: stop the music with it. */
    override fun onTaskRemoved(rootIntent: Intent?) {
        quit()
        super.onTaskRemoved(rootIntent)
    }

    override fun onDestroy() {
        if (wake?.isHeld == true) wake?.release()
        super.onDestroy()
    }

    private fun quit() {
        Native.nPlay(0)
        Native.nPanic()
        (application as? Gen1App)?.store?.save()
        if (wake?.isHeld == true) wake?.release()
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    companion object {
        private const val CHANNEL = "clock"
        private const val ACTION_QUIT = "com.grum0.gen1.QUIT"

        fun start(ctx: Context) {
            // Started from the app on screen (onStart, Play), where a plain start is allowed; the service then goes to
            // the foreground itself. (startForegroundService would make Android stop the app if startForeground
            // then failed: review finding 11.) From the background the start is refused and nothing happens.
            runCatching { ctx.startService(Intent(ctx, ClockService::class.java)) }
        }
    }
}
