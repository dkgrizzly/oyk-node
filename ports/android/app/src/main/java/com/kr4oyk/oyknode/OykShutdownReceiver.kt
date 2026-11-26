// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

package com.kr4oyk.oyknode

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/**
 * Handles Android OS power-off and reboot broadcasts for silent graceful teardown.
 */
class OykShutdownReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context?, intent: Intent?) {
        val act = intent?.action ?: return
        if (act == Intent.ACTION_SHUTDOWN ||
            act == Intent.ACTION_REBOOT ||
            act == "android.intent.action.QUICKBOOT_POWEROFF"
        ) {
            OykNodeService.gracefulSilentShutdown()
            context?.stopService(Intent(context, OykNodeService::class.java))
        }
    }
}
