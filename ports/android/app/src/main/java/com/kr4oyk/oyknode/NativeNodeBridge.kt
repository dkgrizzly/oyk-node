// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

package com.kr4oyk.oyknode

import android.util.Log

/**
 * JNI bridge to the C++20 OYK NodeEngine and C++98 OykPppLink USB PPP engine.
 * Degrades cleanly under JVM Robolectric unit tests when the native .so is absent.
 */
object NativeNodeBridge {
    private const val TAG = "OykNativeBridge"

    @Volatile
    private var loaded: Boolean = false

    init {
        try {
            System.loadLibrary("oyk_android")
            loaded = true
        } catch (t: UnsatisfiedLinkError) {
            Log.d(TAG, "liboyk_android.so unavailable in current JVM process: ${t.message}")
            loaded = false
        }
    }

    fun isNativeLoaded(): Boolean = loaded

    @JvmStatic
    external fun nativeStart(configPath: String): Boolean

    @JvmStatic
    external fun nativeStop()

    @JvmStatic
    external fun nativeIsRunning(): Boolean

    @JvmStatic
    external fun nativeGetStatusJson(): String

    @JvmStatic
    external fun nativeTerminateSession(sessionId: Long): Boolean

    @JvmStatic
    external fun nativeBanCallsign(callsign: String, reason: String): Boolean

    @JvmStatic
    external fun nativeUnbanCallsign(callsign: String): Boolean

    @JvmStatic
    external fun nativeSendTerminalCommand(command: String): String

    @JvmStatic
    external fun nativePppInit(localIp: String, peerIp: String, udpPort: Int): Boolean

    @JvmStatic
    external fun nativePppFeedRx(rxBytes: ByteArray): ByteArray

    @JvmStatic
    external fun nativePppTick(nowMs: Long): ByteArray
}
