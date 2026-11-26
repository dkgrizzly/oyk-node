// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

package com.kr4oyk.oyknode

/**
 * UI, configuration, and node runtime state models mirroring the C++20/C++98 OYK-Node schema.
 */
enum class ThemeRole(val key: String, val label: String) {
    BG("bg", "Background"),
    FG("fg", "Foreground"),
    PANEL_BG("panel_bg", "Panel Bg"),
    PANEL_FG("panel_fg", "Panel Fg"),
    BORDER("border", "Border"),
    HEADER_BG("header_bg", "Header Bg"),
    HEADER_FG("header_fg", "Header Fg"),
    ACCENT("accent", "Accent"),
    STATUS_BG("status_bg", "Status Bg"),
    STATUS_FG("status_fg", "Status Fg"),
    SELECTION_BG("selection_bg", "Selection Bg"),
    SELECTION_FG("selection_fg", "Selection Fg"),
    INPUT_BG("input_bg", "Input Bg"),
    INPUT_FG("input_fg", "Input Fg"),
    BUTTON_BG("button_bg", "Button Bg"),
    BUTTON_FG("button_fg", "Button Fg"),
    OK("ok", "OK Status"),
    WARN("warn", "Warning"),
    ERROR("error", "Error")
}

data class ThemeConfigState(
    val name: String,
    val isDarkBase: Boolean,
    val colors: Map<ThemeRole, String>
)

object BuiltinThemes {
    val DARK_COLORS: Map<ThemeRole, String> = mapOf(
        ThemeRole.BG to "#10141A",
        ThemeRole.FG to "#E2E8F0",
        ThemeRole.PANEL_BG to "#18202A",
        ThemeRole.PANEL_FG to "#E2E8F0",
        ThemeRole.BORDER to "#38BDF8",
        ThemeRole.HEADER_BG to "#1E3A5F",
        ThemeRole.HEADER_FG to "#FFFFFF",
        ThemeRole.ACCENT to "#38BDF8",
        ThemeRole.STATUS_BG to "#18202A",
        ThemeRole.STATUS_FG to "#FACC15",
        ThemeRole.SELECTION_BG to "#0EA5E9",
        ThemeRole.SELECTION_FG to "#FFFFFF",
        ThemeRole.INPUT_BG to "#0F172A",
        ThemeRole.INPUT_FG to "#F8FAFC",
        ThemeRole.BUTTON_BG to "#263445",
        ThemeRole.BUTTON_FG to "#F8FAFC",
        ThemeRole.OK to "#4ADE80",
        ThemeRole.WARN to "#FACC15",
        ThemeRole.ERROR to "#F87171"
    )

    val LIGHT_COLORS: Map<ThemeRole, String> = mapOf(
        ThemeRole.BG to "#F4F6F9",
        ThemeRole.FG to "#111827",
        ThemeRole.PANEL_BG to "#FFFFFF",
        ThemeRole.PANEL_FG to "#111827",
        ThemeRole.BORDER to "#1D4ED8",
        ThemeRole.HEADER_BG to "#1E40AF",
        ThemeRole.HEADER_FG to "#FFFFFF",
        ThemeRole.ACCENT to "#0284C7",
        ThemeRole.STATUS_BG to "#E2E8F0",
        ThemeRole.STATUS_FG to "#1E293B",
        ThemeRole.SELECTION_BG to "#2563EB",
        ThemeRole.SELECTION_FG to "#FFFFFF",
        ThemeRole.INPUT_BG to "#FFFFFF",
        ThemeRole.INPUT_FG to "#0F172A",
        ThemeRole.BUTTON_BG to "#E5E7EB",
        ThemeRole.BUTTON_FG to "#111827",
        ThemeRole.OK to "#15803D",
        ThemeRole.WARN to "#B45309",
        ThemeRole.ERROR to "#B91C1C"
    )

    fun isBuiltin(name: String): Boolean {
        val low = name.trim().lowercase()
        return low.isEmpty() || low == "native" || low == "dark" || low == "light" || low == "auto"
    }

    fun builtinPalette(name: String, systemDark: Boolean): ThemeConfigState {
        return when (name.trim().lowercase()) {
            "light" -> ThemeConfigState("light", false, LIGHT_COLORS)
            "dark" -> ThemeConfigState("dark", true, DARK_COLORS)
            else -> ThemeConfigState("native", systemDark, if (systemDark) DARK_COLORS else LIGHT_COLORS)
        }
    }

    fun makeCustomCloneName(existing: Collection<ThemeConfigState>, isDarkBase: Boolean): String {
        val prefix = if (isDarkBase) "custom-dark" else "custom-light"
        for (idx in 1 until 1000) {
            val cand = if (idx == 1) prefix else "$prefix-$idx"
            if (existing.none { it.name.equals(cand, ignoreCase = true) }) {
                return cand
            }
        }
        return prefix
    }

    fun isValidHexColor(text: String): Boolean {
        val cleaned = text.trim().removePrefix("#")
        return cleaned.length == 6 && cleaned.all { it in '0'..'9' || it in 'a'..'f' || it in 'A'..'F' }
    }

    fun normalizeHexColor(text: String): String {
        val cleaned = text.trim().removePrefix("#").uppercase()
        return "#$cleaned"
    }
}

data class InterfaceConfigState(
    val id: String,
    val type: String = "axudp",
    val description: String = "",
    val host: String = "0.0.0.0",
    val port: Int = 10093,
    val netmask: String = "255.255.255.0",
    val device: String = "",
    val baud: Int = 115200,
    val allowInbound: Boolean = true,
    val allowOutbound: Boolean = true,
    val allowSameInterface: Boolean = true,
    val learnedPeerPolicy: String = "reject",
    val status: String = "UP",
    val activeConnections: Int = 0
)

data class PeerConfigState(
    val id: String,
    val callsign: String,
    val description: String = "",
    val ifaceId: String = "axudp0",
    val host: String = "12.34.56.78",
    val port: Int = 10093,
    val allowInbound: Boolean = true,
    val allowOutbound: Boolean = true,
    val autoLearned: Boolean = false
)

data class SessionState(
    val id: Long,
    val callsign: String,
    val ifaceId: String,
    val sinceHms: String,
    val role: String
)

data class BanEntryState(
    val callsign: String,
    val hardBan: Boolean,
    val appealAllowed: Boolean,
    val liftAt: String,
    val reason: String
)

data class NodeConfigState(
    val callsign: String = "KR4OYK-7",
    val alias: String = "OYKNOD",
    val infoText: String = "OYK Packet Node (Android)",
    val welcomeText: String = "Welcome to KR4OYK-7 Packet Node",
    val banlistFile: String = "banlist.cfg",
    val interfacesFile: String = "interfaces.cfg",
    val peersFile: String = "peers.cfg",
    val themesFile: String = "themes.cfg",
    val themeName: String = "native",
    val bbsEnabled: Boolean = true,
    val bbsSsid: Int = 11,
    val bbsAlias: String = "OYKBBS",
    val chatEnabled: Boolean = true,
    val chatSsid: Int = 12,
    val chatAlias: String = "OYKCHT",
    val chatTopic: String = "Lobby",
    val rigEnabled: Boolean = false,
    val rigHost: String = "127.0.0.1",
    val rigPort: Int = 4532,
    val interfaces: List<InterfaceConfigState> = listOf(
        InterfaceConfigState(
            id = "axudp0",
            type = "axudp",
            description = "Default AXUDP",
            host = "0.0.0.0",
            port = 10093,
            netmask = "255.255.255.0",
            status = "UP (UDP :10093)",
            activeConnections = 1
        ),
        InterfaceConfigState(
            id = "usbppp0",
            type = "ppp",
            description = "RadioFoundry USB PPP",
            device = "usb0",
            host = "10.44.0.1",
            port = 10093,
            baud = 115200,
            netmask = "255.0.0.0",
            allowSameInterface = false,
            learnedPeerPolicy = "reject",
            status = "USB STANDBY"
        )
    ),
    val peers: List<PeerConfigState> = listOf(
        PeerConfigState(
            id = "remote0",
            callsign = "R3MOT-7",
            description = "Remote PoPT Node",
            ifaceId = "axudp0",
            host = "12.34.56.78",
            port = 10093
        )
    ),
    val customThemes: List<ThemeConfigState> = emptyList()
)

enum class StoragePermissionState {
    UNREQUESTED,
    GRANTED,
    DECLINED
}

enum class SettingsTab(val title: String) {
    NODE("Node"),
    LOCAL("Local"),
    RADIO("Radio"),
    INTERFACES("Ifaces"),
    PEERS("Peers")
}

object PeerValidator {
    fun isValidHostOrDmrId(host: String): Boolean {
        val h = host.trim()
        if (h.isEmpty()) return false
        if (h.all { it in '0'..'9' }) {
            val dmrId = h.toLongOrNull() ?: return false
            return dmrId in 1L..16777215L
        }
        return h.all { it in 'a'..'z' || it in 'A'..'Z' || it in '0'..'9' || it == '.' || it == '-' }
    }

    fun dmrIdToIpv4(dmrId: Long): String {
        require(dmrId in 1L..16777215L) { "DMR ID out of 24-bit range: $dmrId" }
        val b1 = (dmrId shr 16) and 0xFF
        val b2 = (dmrId shr 8) and 0xFF
        val b3 = dmrId and 0xFF
        return "44.$b1.$b2.$b3"
    }
}
