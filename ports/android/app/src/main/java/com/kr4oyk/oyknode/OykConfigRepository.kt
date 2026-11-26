// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

package com.kr4oyk.oyknode

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Environment
import androidx.core.content.ContextCompat
import java.io.File

/**
 * Manages active node configuration in Android internal storage (`filesDir/oyk/`)
 * with optional lazy-permission import/export to SD card (real or emulated).
 */
class OykConfigRepository(private val context: Context) {
    companion object {
        private const val PREFS_NAME = "oyk_android_prefs"
        private const val KEY_ONBOARDING_SHOWN = "onboarding_shown"
        private const val KEY_STORAGE_DECLINED = "storage_permission_declined"
        private const val KEY_USB_PERMISSION_HANDLED = "usb_handled"
    }

    private val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
    val internalConfigDir: File = File(context.filesDir, "oyk").apply { mkdirs() }
    val primaryConfigFile: File = File(internalConfigDir, "node.cfg")

    var isOnboardingShown: Boolean
        get() = prefs.getBoolean(KEY_ONBOARDING_SHOWN, false)
        set(value) = prefs.edit().putBoolean(KEY_ONBOARDING_SHOWN, value).apply()

    fun storagePermissionState(): StoragePermissionState {
        if (hasSdStoragePermission()) return StoragePermissionState.GRANTED
        return if (prefs.getBoolean(KEY_STORAGE_DECLINED, false)) {
            StoragePermissionState.DECLINED
        } else {
            StoragePermissionState.UNREQUESTED
        }
    }

    fun markStoragePermissionResult(granted: Boolean) {
        prefs.edit().putBoolean(KEY_STORAGE_DECLINED, !granted).apply()
    }

    fun hasSdStoragePermission(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (BuildConfig.IS_SIDELOAD) {
                Environment.isExternalStorageManager()
            } else {
                // Play Store uses SAF document pickers on API 30+, which do not need MANAGE_EXTERNAL_STORAGE.
                !prefs.getBoolean(KEY_STORAGE_DECLINED, false)
            }
        } else {
            val readOk = ContextCompat.checkSelfPermission(
                context,
                Manifest.permission.READ_EXTERNAL_STORAGE
            ) == PackageManager.PERMISSION_GRANTED
            val writeOk = ContextCompat.checkSelfPermission(
                context,
                Manifest.permission.WRITE_EXTERNAL_STORAGE
            ) == PackageManager.PERMISSION_GRANTED
            readOk && writeOk
        }
    }

    fun defaultSdCardDir(): File {
        @Suppress("DEPRECATION")
        val ext = Environment.getExternalStorageDirectory()
        return File(ext, "OYKNode")
    }

    fun saveInternal(cfg: NodeConfigState): File {
        internalConfigDir.mkdirs()
        writeConfigBundle(cfg, internalConfigDir)
        return primaryConfigFile
    }

    fun loadInternal(): NodeConfigState {
        if (!primaryConfigFile.exists()) {
            val def = NodeConfigState()
            saveInternal(def)
            return def
        }
        return parseConfigBundle(primaryConfigFile)
    }

    fun exportToDirectory(cfg: NodeConfigState, destDir: File): File {
        destDir.mkdirs()
        writeConfigBundle(cfg, destDir)
        return File(destDir, "node.cfg")
    }

    fun importFromFile(srcNodeCfg: File): NodeConfigState {
        val loaded = parseConfigBundle(srcNodeCfg)
        saveInternal(loaded)
        return loaded
    }

    fun serializeNodeCfg(cfg: NodeConfigState): String {
        val sb = StringBuilder()
        sb.append("# SPDX-License-Identifier: GPL-3.0-or-later\n")
        sb.append("[node]\n")
        sb.append("callsign = ${cfg.callsign}\n")
        sb.append("alias = ${cfg.alias}\n")
        sb.append("welcome = ${cfg.welcomeText}\n")
        sb.append("info = ${cfg.infoText}\n")
        sb.append("banlist = ${cfg.banlistFile}\n")
        if (cfg.interfacesFile.isNotBlank()) sb.append("interfaces = ${cfg.interfacesFile}\n")
        if (cfg.peersFile.isNotBlank()) sb.append("peers = ${cfg.peersFile}\n")
        if (cfg.themesFile.isNotBlank()) sb.append("themes = ${cfg.themesFile}\n")
        sb.append("theme = ${cfg.themeName}\n\n")

        sb.append("[bbs]\n")
        sb.append("enabled = ${cfg.bbsEnabled}\n")
        sb.append("ssid = ${cfg.bbsSsid}\n")
        sb.append("alias = ${cfg.bbsAlias}\n\n")

        sb.append("[chat]\n")
        sb.append("enabled = ${cfg.chatEnabled}\n")
        sb.append("ssid = ${cfg.chatSsid}\n")
        sb.append("alias = ${cfg.chatAlias}\n")
        sb.append("topic = ${cfg.chatTopic}\n\n")

        sb.append("[rig]\n")
        sb.append("enabled = ${cfg.rigEnabled}\n")
        sb.append("host = ${cfg.rigHost}\n")
        sb.append("port = ${cfg.rigPort}\n")
        return sb.toString()
    }

    private fun serializeInterfaces(ifaces: List<InterfaceConfigState>): String {
        val sb = StringBuilder()
        for (ic in ifaces) {
            sb.append("\n[interface ${ic.id}]\n")
            sb.append("type = ${ic.type}\n")
            if (ic.description.isNotBlank()) sb.append("description = ${ic.description}\n")
            if (ic.device.isNotBlank()) {
                sb.append("device = ${ic.device}\n")
                sb.append("baud = ${ic.baud}\n")
            }
            if (ic.host.isNotBlank()) sb.append("bind_host = ${ic.host}\n")
            if (ic.port > 0) sb.append("bind_port = ${ic.port}\n")
            if (ic.netmask.isNotBlank()) sb.append("netmask = ${ic.netmask}\n")
            sb.append("allow_inbound = ${ic.allowInbound}\n")
            sb.append("allow_outbound = ${ic.allowOutbound}\n")
            sb.append("allow_same_interface = ${ic.allowSameInterface}\n")
            sb.append("learned_peer_policy = ${ic.learnedPeerPolicy}\n")
        }
        return sb.toString()
    }

    private fun serializePeers(peers: List<PeerConfigState>): String {
        val sb = StringBuilder()
        for (p in peers.filter { !it.autoLearned }) {
            sb.append("\n[peer ${p.id}]\n")
            sb.append("callsign = ${p.callsign}\n")
            if (p.description.isNotBlank()) sb.append("description = ${p.description}\n")
            sb.append("interface = ${p.ifaceId}\n")
            sb.append("host = ${p.host}\n")
            if (p.port > 0) sb.append("port = ${p.port}\n")
            sb.append("allow_inbound = ${p.allowInbound}\n")
            sb.append("allow_outbound = ${p.allowOutbound}\n")
        }
        return sb.toString()
    }

    fun serializeThemes(themes: List<ThemeConfigState>): String {
        val sb = StringBuilder()
        for (th in themes) {
            if (BuiltinThemes.isBuiltin(th.name)) continue
            sb.append("\n[theme ${th.name}]\n")
            sb.append("base = ${if (th.isDarkBase) "dark" else "light"}\n")
            for (role in ThemeRole.entries) {
                val hex = th.colors[role] ?: BuiltinThemes.DARK_COLORS[role] ?: "#000000"
                sb.append("${role.key} = $hex\n")
            }
        }
        return sb.toString()
    }

    private fun writeConfigBundle(cfg: NodeConfigState, dir: File) {
        val nodeText = StringBuilder(serializeNodeCfg(cfg))
        val ifPath = cfg.interfacesFile.trim()
        val prPath = cfg.peersFile.trim()
        val thPath = cfg.themesFile.trim()

        val ifText = serializeInterfaces(cfg.interfaces)
        val prText = serializePeers(cfg.peers)
        val thText = serializeThemes(cfg.customThemes)

        val buckets = linkedMapOf<String, StringBuilder>()
        fun appendBucket(rel: String, body: String) {
            if (rel.isEmpty()) {
                nodeText.append(body)
            } else {
                buckets.getOrPut(rel) { StringBuilder() }.append(body)
            }
        }
        appendBucket(ifPath, ifText)
        appendBucket(prPath, prText)
        appendBucket(thPath, thText)

        File(dir, "node.cfg").writeText(nodeText.toString(), Charsets.UTF_8)
        for ((rel, content) in buckets) {
            val target = File(dir, rel)
            target.parentFile?.mkdirs()
            target.writeText(content.toString(), Charsets.UTF_8)
        }
    }

    private fun stripInlineCommentPreservingHex(value: String): String {
        val t = value.trim()
        // Hex #RRGGBB color literal starts with '#' directly after '=': preserve it!
        val idx = t.indexOf(" #")
        return if (idx >= 0) t.substring(0, idx).trim() else t
    }

    fun parseConfigBundle(nodeFile: File): NodeConfigState {
        val baseDir = nodeFile.parentFile ?: internalConfigDir
        val lines = mutableListOf<String>()
        if (nodeFile.exists()) lines += nodeFile.readLines(Charsets.UTF_8)

        var cfg = NodeConfigState(interfaces = emptyList(), peers = emptyList(), customThemes = emptyList())
        val parsedIncludes = linkedSetOf<String>()
        val ifaces = mutableListOf<InterfaceConfigState>()
        val peers = mutableListOf<PeerConfigState>()
        val themes = mutableListOf<ThemeConfigState>()

        fun processLines(srcLines: List<String>, depth: Int) {
            var curSection = ""
            var curId = ""
            var curIface: InterfaceConfigState? = null
            var curPeer: PeerConfigState? = null
            var curTheme: ThemeConfigState? = null
            var themeColors = linkedMapOf<ThemeRole, String>()

            fun flush() {
                curIface?.let { ifaces += it }
                curPeer?.let { peers += it }
                curTheme?.let { themes += it.copy(colors = themeColors.toMap()) }
                curIface = null
                curPeer = null
                curTheme = null
                themeColors = linkedMapOf()
            }

            for (raw in srcLines) {
                val line = raw.trim()
                if (line.isEmpty() || line.startsWith("#") || line.startsWith(";")) continue
                if (line.startsWith("[") && line.endsWith("]")) {
                    flush()
                    val hdr = line.substring(1, line.length - 1).trim()
                    val space = hdr.indexOf(' ')
                    curSection = (if (space >= 0) hdr.substring(0, space) else hdr).lowercase()
                    curId = if (space >= 0) hdr.substring(space + 1).trim() else ""
                    when (curSection) {
                        "interface" -> if (curId.isNotEmpty()) curIface = InterfaceConfigState(id = curId)
                        "peer" -> if (curId.isNotEmpty()) curPeer = PeerConfigState(id = curId, callsign = curId.uppercase())
                        "theme" -> if (curId.isNotEmpty() && !BuiltinThemes.isBuiltin(curId)) {
                            curTheme = ThemeConfigState(name = curId, isDarkBase = true, colors = BuiltinThemes.DARK_COLORS)
                            themeColors = LinkedHashMap(BuiltinThemes.DARK_COLORS)
                        }
                    }
                    continue
                }
                val eq = line.indexOf('=')
                if (eq < 0) continue
                val key = line.substring(0, eq).trim().lowercase()
                val valRaw = stripInlineCommentPreservingHex(line.substring(eq + 1))

                when (curSection) {
                    "node" -> when (key) {
                        "callsign" -> cfg = cfg.copy(callsign = valRaw)
                        "alias" -> cfg = cfg.copy(alias = valRaw)
                        "welcome" -> cfg = cfg.copy(welcomeText = valRaw)
                        "info" -> cfg = cfg.copy(infoText = valRaw)
                        "banlist" -> cfg = cfg.copy(banlistFile = valRaw)
                        "theme" -> cfg = cfg.copy(themeName = valRaw)
                        "interfaces", "peers", "themes" -> {
                            cfg = when (key) {
                                "interfaces" -> cfg.copy(interfacesFile = valRaw)
                                "peers" -> cfg.copy(peersFile = valRaw)
                                else -> cfg.copy(themesFile = valRaw)
                            }
                            if (depth == 0 && valRaw.isNotEmpty() && parsedIncludes.add(valRaw)) {
                                val inc = File(baseDir, valRaw)
                                if (inc.exists()) processLines(inc.readLines(Charsets.UTF_8), depth + 1)
                            }
                        }
                    }
                    "bbs" -> when (key) {
                        "enabled" -> cfg = cfg.copy(bbsEnabled = valRaw.equals("true", true))
                        "ssid" -> cfg = cfg.copy(bbsSsid = valRaw.toIntOrNull() ?: cfg.bbsSsid)
                        "alias" -> cfg = cfg.copy(bbsAlias = valRaw)
                    }
                    "chat" -> when (key) {
                        "enabled" -> cfg = cfg.copy(chatEnabled = valRaw.equals("true", true))
                        "ssid" -> cfg = cfg.copy(chatSsid = valRaw.toIntOrNull() ?: cfg.chatSsid)
                        "alias" -> cfg = cfg.copy(chatAlias = valRaw)
                        "topic" -> cfg = cfg.copy(chatTopic = valRaw)
                    }
                    "rig" -> when (key) {
                        "enabled" -> cfg = cfg.copy(rigEnabled = valRaw.equals("true", true))
                        "host" -> cfg = cfg.copy(rigHost = valRaw)
                        "port" -> cfg = cfg.copy(rigPort = valRaw.toIntOrNull() ?: cfg.rigPort)
                    }
                    "interface" -> curIface = curIface?.let { ic ->
                        when (key) {
                            "type" -> ic.copy(type = valRaw)
                            "description" -> ic.copy(description = valRaw)
                            "bind_host", "host" -> ic.copy(host = valRaw)
                            "bind_port", "port" -> ic.copy(port = valRaw.toIntOrNull() ?: ic.port)
                            "netmask" -> ic.copy(netmask = valRaw)
                            "device" -> ic.copy(device = valRaw)
                            "baud" -> ic.copy(baud = valRaw.toIntOrNull() ?: ic.baud)
                            "allow_inbound" -> ic.copy(allowInbound = valRaw.equals("true", true))
                            "allow_outbound" -> ic.copy(allowOutbound = valRaw.equals("true", true))
                            "allow_same_interface" -> ic.copy(allowSameInterface = valRaw.equals("true", true))
                            "learned_peer_policy" -> ic.copy(learnedPeerPolicy = valRaw)
                            else -> ic
                        }
                    }
                    "peer" -> curPeer = curPeer?.let { p ->
                        when (key) {
                            "callsign" -> p.copy(callsign = valRaw)
                            "description" -> p.copy(description = valRaw)
                            "interface" -> p.copy(ifaceId = valRaw)
                            "host" -> p.copy(host = valRaw)
                            "port" -> p.copy(port = valRaw.toIntOrNull() ?: p.port)
                            "allow_inbound" -> p.copy(allowInbound = valRaw.equals("true", true))
                            "allow_outbound" -> p.copy(allowOutbound = valRaw.equals("true", true))
                            else -> p
                        }
                    }
                    "theme" -> curTheme?.let { th ->
                        if (key == "base") {
                            curTheme = th.copy(isDarkBase = !valRaw.equals("light", ignoreCase = true))
                        } else {
                            val role = ThemeRole.entries.firstOrNull { it.key == key }
                            if (role != null && BuiltinThemes.isValidHexColor(valRaw)) {
                                themeColors[role] = BuiltinThemes.normalizeHexColor(valRaw)
                            }
                        }
                    }
                }
            }
            flush()
        }

        processLines(lines, 0)
        return cfg.copy(
            interfaces = if (ifaces.isEmpty()) NodeConfigState().interfaces else ifaces,
            peers = peers,
            customThemes = themes
        )
    }
}
