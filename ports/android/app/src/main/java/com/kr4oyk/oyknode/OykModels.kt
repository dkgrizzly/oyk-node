// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

package com.kr4oyk.oyknode

import java.io.File

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

data class HeardStationState(
    val callsign: String,
    val ifaceId: String,
    val heardHms: String
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
    val printerProtocol: String = "generic",
    val printerBaud: Int = 9600,
    val printerCondensed: Boolean = false,
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
    BBS("BBS"),
    CHAT("Chat"),
    INTERFACES("Ifaces"),
    PEERS("Peers"),
    RIG("Rig"),
    PRINTER("Prn")
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

data class OykBbsDirEntryState(
    val name: String,
    val sizeBytes: Long,
    val isDir: Boolean,
    val isParent: Boolean = false
)

object OykJailedFileBrowser {
    fun normalizeRel(relDir: String): String? {
        val parts = mutableListOf<String>()
        for (raw in relDir.replace('\\', '/').split('/')) {
            if (raw.isEmpty() || raw == ".") continue
            if (raw == "..") {
                if (parts.isEmpty()) return null
                parts.removeAt(parts.lastIndex)
            } else {
                parts.add(raw)
            }
        }
        return parts.joinToString("/")
    }

    private fun resolveInsideJail(root: File, normRel: String): File? {
        val rootCanon = runCatching { root.canonicalFile }.getOrElse { return null }
        val target = if (normRel.isEmpty()) root else File(root, normRel)
        val tgtCanon = runCatching { target.canonicalFile }.getOrElse { return null }
        val rp = rootCanon.path
        val tp = tgtCanon.path
        return if (tp == rp || tp.startsWith(rp + File.separator)) tgtCanon else null
    }

    fun listDir(root: File, relDir: String): Pair<String, List<OykBbsDirEntryState>>? {
        val norm = normalizeRel(relDir) ?: return null
        val dir = resolveInsideJail(root, norm) ?: return null
        if (!dir.exists() || !dir.isDirectory) return norm to emptyList()
        val out = mutableListOf<OykBbsDirEntryState>()
        if (norm.isNotEmpty()) {
            out.add(OykBbsDirEntryState("..", 0L, isDir = true, isParent = true))
        }
        val children = dir.listFiles()?.sortedWith(
            compareBy<File>({ !it.isDirectory }, { it.name.lowercase() })
        ).orEmpty()
        for (f in children) {
            if (f.name == "." || f.name == "..") continue
            val canon = runCatching { f.canonicalFile }.getOrNull() ?: continue
            val rootCanon = root.canonicalFile
            if (canon.path != rootCanon.path && !canon.path.startsWith(rootCanon.path + File.separator)) {
                continue
            }
            out.add(OykBbsDirEntryState(f.name, if (f.isDirectory) 0L else f.length(), f.isDirectory, false))
        }
        return norm to out
    }

    fun enterSubdir(root: File, curRel: String, child: String): Result<String> {
        val normCur = normalizeRel(curRel)
            ?: return Result.failure(IllegalArgumentException("Path outside jail"))
        if (child == "..") {
            if (normCur.isEmpty()) {
                return Result.failure(IllegalStateException("Already at assigned root directory"))
            }
            val slash = normCur.lastIndexOf('/')
            return Result.success(if (slash >= 0) normCur.substring(0, slash) else "")
        }
        if (child.isEmpty() || child == "." || child.contains('/') || child.contains('\\')) {
            return Result.failure(IllegalArgumentException("Invalid directory name"))
        }
        val nextRel = if (normCur.isEmpty()) child else "$normCur/$child"
        val resolved = resolveInsideJail(root, nextRel)
            ?: return Result.failure(SecurityException("Symlink or path escapes jail"))
        if (!resolved.exists() || !resolved.isDirectory) {
            return Result.failure(IllegalArgumentException("Directory does not exist"))
        }
        return Result.success(nextRel)
    }

    fun approveRelFile(incRoot: File, incRel: String, filesRoot: File, destRelDir: String): Result<File> {
        val normInc = normalizeRel(incRel) ?: return Result.failure(SecurityException("Invalid incoming path"))
        val normDstDir = normalizeRel(destRelDir) ?: return Result.failure(SecurityException("Invalid destination path"))
        val src = resolveInsideJail(incRoot, normInc) ?: return Result.failure(SecurityException("Source escapes jail"))
        val dstDir = resolveInsideJail(filesRoot, normDstDir) ?: return Result.failure(SecurityException("Dest escapes jail"))
        if (!src.isFile) return Result.failure(IllegalArgumentException("Select a file"))
        dstDir.mkdirs()
        val dst = File(dstDir, src.name)
        src.copyTo(dst, overwrite = true)
        src.delete()
        return Result.success(dst)
    }

    fun mkdir(root: File, curRel: String, name: String): Result<File> {
        val normCur = normalizeRel(curRel) ?: return Result.failure(SecurityException("Path escapes jail"))
        if (name.isEmpty() || name == "." || name == ".." || name.contains('/') || name.contains('\\')) {
            return Result.failure(IllegalArgumentException("Invalid folder name"))
        }
        val dir = resolveInsideJail(root, normCur) ?: return Result.failure(SecurityException("Path escapes jail"))
        val created = File(dir, name)
        created.mkdirs()
        return Result.success(created)
    }

    fun rename(root: File, curRel: String, oldName: String, newName: String): Result<File> {
        val normCur = normalizeRel(curRel) ?: return Result.failure(SecurityException("Path escapes jail"))
        if (oldName == ".." || newName.isEmpty() || newName == ".." || newName.contains('/') || newName.contains('\\')) {
            return Result.failure(IllegalArgumentException("Invalid name"))
        }
        val dir = resolveInsideJail(root, normCur) ?: return Result.failure(SecurityException("Path escapes jail"))
        val src = File(dir, oldName)
        val dst = File(dir, newName)
        if (!src.exists()) return Result.failure(IllegalArgumentException("Missing entry"))
        if (!src.renameTo(dst)) return Result.failure(IllegalStateException("Rename failed"))
        return Result.success(dst)
    }

    fun deleteEntry(root: File, curRel: String, name: String): Result<Unit> {
        val normCur = normalizeRel(curRel) ?: return Result.failure(SecurityException("Path escapes jail"))
        if (name.isEmpty() || name == "." || name == "..") {
            return Result.failure(IllegalArgumentException("Invalid entry"))
        }
        val target = resolveInsideJail(root, if (normCur.isEmpty()) name else "$normCur/$name")
            ?: return Result.failure(SecurityException("Path escapes jail"))
        if (!target.delete()) return Result.failure(IllegalStateException("Delete failed"))
        return Result.success(Unit)
    }
}
