// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

package com.kr4oyk.oyknode

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder

/**
 * Foreground service hosting the OYK node runtime.
 * Performs a silent, graceful shutdown without saving unconfirmed auto-learned peers
 * on `onTaskRemoved` (swipe away / Close All Apps) or device shutdown/reboot.
 */
class OykNodeService : Service() {
    companion object {
        const val CHANNEL_ID = "oyk_node_service"
        const val NOTIF_ID = 4401
        const val ACTION_START = "com.kr4oyk.oyknode.START"
        const val ACTION_STOP = "com.kr4oyk.oyknode.STOP"

        @Volatile
        var shutdownCount: Int = 0
            internal set

        @Volatile
        var running: Boolean = false
            internal set

        fun gracefulSilentShutdown() {
            if (NativeNodeBridge.isNativeLoaded()) {
                NativeNodeBridge.nativeStop()
            }
            running = false
            shutdownCount += 1
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        ensureChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            gracefulSilentShutdown()
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
            return START_NOT_STICKY
        }

        val repo = OykConfigRepository(this)
        val cfgFile = repo.primaryConfigFile
        if (!cfgFile.exists()) {
            repo.saveInternal(NodeConfigState())
        }
        startForeground(NOTIF_ID, buildNotification())
        if (NativeNodeBridge.isNativeLoaded() && !NativeNodeBridge.nativeIsRunning()) {
            NativeNodeBridge.nativeStart(cfgFile.absolutePath)
        }
        running = true
        return START_NOT_STICKY
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        // Silent graceful teardown when user swipes away the task or hits "Close All".
        gracefulSilentShutdown()
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
        super.onTaskRemoved(rootIntent)
    }

    override fun onDestroy() {
        gracefulSilentShutdown()
        super.onDestroy()
    }

    private fun ensureChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val nm = getSystemService(NotificationManager::class.java)
            if (nm.getNotificationChannel(CHANNEL_ID) == null) {
                nm.createNotificationChannel(
                    NotificationChannel(
                        CHANNEL_ID,
                        getString(R.string.service_channel_name),
                        NotificationManager.IMPORTANCE_LOW
                    )
                )
            }
        }
    }

    private fun buildNotification(): Notification {
        val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Notification.Builder(this, CHANNEL_ID)
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(this)
        }
        return builder
            .setContentTitle(getString(R.string.service_notification_title))
            .setContentText("AXUDP / USB PPP Packet Node")
            .setSmallIcon(R.drawable.ic_oyk_notif)
            .setOngoing(true)
            .build()
    }
}
