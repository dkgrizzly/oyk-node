// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

package com.kr4oyk.oyknode

import android.content.Context
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import android.os.SystemClock
import android.util.Log
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Unprivileged Android USB Host serial/PPP transport for RadioFoundry radios
 * (no root or VpnService required). Bridges raw USB bulk endpoints to C++98 OykPppLink.
 */
class OykUsbPppTransport(context: Context) {
    companion object {
        private const val TAG = "OykUsbPpp"
    }

    private val usbManager = context.getSystemService(Context.USB_SERVICE) as? UsbManager
    private var connection: UsbDeviceConnection? = null
    private var claimedIface: UsbInterface? = null
    private var epIn: UsbEndpoint? = null
    private var epOut: UsbEndpoint? = null
    private val running = AtomicBoolean(false)
    private var ioThread: Thread? = null

    fun listCandidateDevices(): List<UsbDevice> {
        val mgr = usbManager ?: return emptyList()
        return mgr.deviceList.values.filter { dev ->
            dev.deviceClass == UsbConstants.USB_CLASS_COMM ||
                (0 until dev.interfaceCount).any { idx ->
                    val intf = dev.getInterface(idx)
                    intf.interfaceClass == UsbConstants.USB_CLASS_CDC_DATA ||
                        intf.interfaceClass == UsbConstants.USB_CLASS_COMM ||
                        intf.interfaceClass == UsbConstants.USB_CLASS_VENDOR_SPEC
                }
        }
    }

    fun start(device: UsbDevice, localIp: String, peerIp: String, udpPort: Int): Boolean {
        val mgr = usbManager ?: return false
        if (!mgr.hasPermission(device)) return false
        stop()

        var dataIface: UsbInterface? = null
        var inEp: UsbEndpoint? = null
        var outEp: UsbEndpoint? = null

        for (i in 0 until device.interfaceCount) {
            val intf = device.getInterface(i)
            var candIn: UsbEndpoint? = null
            var candOut: UsbEndpoint? = null
            for (e in 0 until intf.endpointCount) {
                val ep = intf.getEndpoint(e)
                if (ep.type == UsbConstants.USB_ENDPOINT_XFER_BULK) {
                    if (ep.direction == UsbConstants.USB_DIR_IN) candIn = ep
                    else candOut = ep
                }
            }
            if (candIn != null && candOut != null) {
                dataIface = intf
                inEp = candIn
                outEp = candOut
                break
            }
        }

        val intf = dataIface ?: return false
        val conn = mgr.openDevice(device) ?: return false
        if (!conn.claimInterface(intf, true)) {
            conn.close()
            return false
        }

        // Send CDC-ACM SET_LINE_CODING (115200 8N1) & SET_CONTROL_LINE_STATE (DTR+RTS) best-effort.
        val lineCoding = byteArrayOf(
            0x00, 0xC2.toByte(), 0x01, 0x00, // 115200 LE
            0x00, 0x00, 0x08
        )
        conn.controlTransfer(0x21, 0x20, 0, intf.id, lineCoding, lineCoding.size, 250)
        conn.controlTransfer(0x21, 0x22, 0x03, intf.id, null, 0, 250)

        connection = conn
        claimedIface = intf
        epIn = inEp
        epOut = outEp

        if (NativeNodeBridge.isNativeLoaded()) {
            NativeNodeBridge.nativePppInit(localIp, peerIp, udpPort)
        }

        running.set(true)
        ioThread = Thread({
            val rxBuf = ByteArray(1024)
            var lastTick = SystemClock.elapsedRealtime()
            while (running.get()) {
                val n = conn.bulkTransfer(inEp, rxBuf, rxBuf.size, 100)
                if (n > 0 && NativeNodeBridge.isNativeLoaded()) {
                    val tx = NativeNodeBridge.nativePppFeedRx(rxBuf.copyOf(n))
                    if (tx.isNotEmpty()) {
                        conn.bulkTransfer(outEp, tx, tx.size, 200)
                    }
                }
                val now = SystemClock.elapsedRealtime()
                if (now - lastTick >= 200L) {
                    lastTick = now
                    if (NativeNodeBridge.isNativeLoaded()) {
                        val tx = NativeNodeBridge.nativePppTick(now)
                        if (tx.isNotEmpty()) {
                            conn.bulkTransfer(outEp, tx, tx.size, 200)
                        }
                    }
                }
            }
        }, "oyk-usb-ppp").apply { start() }
        return true
    }

    fun stop() {
        running.set(false)
        ioThread?.join(400L)
        ioThread = null
        try {
            claimedIface?.let { connection?.releaseInterface(it) }
            connection?.close()
        } catch (t: Throwable) {
            Log.w(TAG, "USB close warning: ${t.message}")
        } finally {
            claimedIface = null
            connection = null
            epIn = null
            epOut = null
        }
    }
}
