// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

package com.kr4oyk.oyknode

import android.Manifest
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import java.io.File

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val repo = OykConfigRepository(this)
        val usbPpp = OykUsbPppTransport(this)

        startService(Intent(this, OykNodeService::class.java).apply {
            action = OykNodeService.ACTION_START
        })

        setContent {
            var cfg by remember { mutableStateOf(repo.loadInternal()) }
            var showOnboarding by remember { mutableStateOf(!repo.isOnboardingShown) }
            var activeScreen by remember { mutableStateOf("console") }
            var menuOpen by remember { mutableStateOf(false) }
            var settingsTab by remember { mutableStateOf(SettingsTab.NODE) }
            var showAddIface by remember { mutableStateOf(false) }
            var editingIface by remember { mutableStateOf<InterfaceConfigState?>(null) }
            var editingPeer by remember { mutableStateOf<PeerConfigState?>(null) }
            var showTerminate by remember { mutableStateOf(false) }
            var showBan by remember { mutableStateOf(false) }
            var showAbout by remember { mutableStateOf(false) }
            var showTermHelp by remember { mutableStateOf(false) }
            var termInput by remember { mutableStateOf("") }
            var termLines by remember {
                mutableStateOf(
                    listOf(
                        "*** Local Packet Terminal (YAPP / YAPP-C ready)",
                        "*** Tap Help or press Ctrl-A Z for hotkeys."
                    )
                )
            }
            var previewTheme by remember { mutableStateOf<Pair<String, List<ThemeConfigState>>?>(null) }
            var showExit by remember { mutableStateOf(false) }
            var acceptIncoming by remember { mutableStateOf(true) }
            var sessions by remember { mutableStateOf<List<SessionState>>(emptyList()) }
            var newHeard by remember { mutableStateOf<List<HeardStationState>>(emptyList()) }
            var bans by remember { mutableStateOf<List<BanEntryState>>(emptyList()) }
            var permState by remember { mutableStateOf(repo.storagePermissionState()) }
            var pendingSdAction by remember { mutableStateOf<String?>(null) }
            var sdStatus by remember { mutableStateOf("") }
            var usbStatus by remember { mutableStateOf("Ready") }

            val hintText = if (BuildConfig.IS_SIDELOAD && Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                "SD Card import/export requires All Files Access (MANAGE_EXTERNAL_STORAGE). Enable it in Android Settings to unlock SD controls."
            } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                "SD Card document access was declined. Re-grant folder/document access when prompted to enable SD Import/Export."
            } else {
                "SD Card import/export requires READ_EXTERNAL_STORAGE / WRITE_EXTERNAL_STORAGE permission. Grant storage access in App Settings."
            }

            fun performSdAction(kind: String) {
                val sdDir = repo.defaultSdCardDir()
                if (kind == "export") {
                    val outFile = repo.exportToDirectory(cfg, sdDir)
                    sdStatus = "Exported config to ${outFile.absolutePath}"
                } else {
                    val src = File(sdDir, "node.cfg")
                    if (src.exists()) {
                        cfg = repo.importFromFile(src)
                        sdStatus = "Imported config from ${src.absolutePath}"
                    } else {
                        sdStatus = "No node.cfg found in ${sdDir.absolutePath}"
                    }
                }
            }

            val legacyPermLauncher = rememberLauncherForActivityResult(
                ActivityResultContracts.RequestMultiplePermissions()
            ) { result ->
                val ok = result.values.all { it }
                repo.markStoragePermissionResult(ok)
                permState = repo.storagePermissionState()
                if (ok) pendingSdAction?.let { performSdAction(it) }
                pendingSdAction = null
            }

            val manageAllLauncher = rememberLauncherForActivityResult(
                ActivityResultContracts.StartActivityForResult()
            ) {
                val ok = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && Environment.isExternalStorageManager()
                repo.markStoragePermissionResult(ok)
                permState = repo.storagePermissionState()
                if (ok) pendingSdAction?.let { performSdAction(it) }
                pendingSdAction = null
            }

            val safExportLauncher = rememberLauncherForActivityResult(
                ActivityResultContracts.CreateDocument("text/plain")
            ) { uri: Uri? ->
                if (uri != null) {
                    repo.markStoragePermissionResult(true)
                    permState = repo.storagePermissionState()
                    contentResolver.openOutputStream(uri)?.use { out ->
                        out.write(repo.serializeNodeCfg(cfg).toByteArray(Charsets.UTF_8))
                    }
                    sdStatus = "Exported node.cfg via Document Picker"
                } else {
                    repo.markStoragePermissionResult(false)
                    permState = repo.storagePermissionState()
                }
            }

            val safImportLauncher = rememberLauncherForActivityResult(
                ActivityResultContracts.OpenDocument()
            ) { uri: Uri? ->
                if (uri != null) {
                    repo.markStoragePermissionResult(true)
                    permState = repo.storagePermissionState()
                    val tmp = File(cacheDir, "imported_node.cfg")
                    contentResolver.openInputStream(uri)?.use { inp ->
                        tmp.writeBytes(inp.readBytes())
                    }
                    cfg = repo.importFromFile(tmp)
                    sdStatus = "Imported node.cfg via Document Picker"
                } else {
                    repo.markStoragePermissionResult(false)
                    permState = repo.storagePermissionState()
                }
            }

            fun triggerLazySdPermissionThen(kind: String) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    if (BuildConfig.IS_SIDELOAD) {
                        if (Environment.isExternalStorageManager()) {
                            performSdAction(kind)
                        } else {
                            pendingSdAction = kind
                            val intent = Intent(
                                Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                Uri.parse("package:$packageName")
                            )
                            manageAllLauncher.launch(intent)
                        }
                    } else {
                        if (kind == "export") safExportLauncher.launch("node.cfg")
                        else safImportLauncher.launch(arrayOf("text/*", "*/*"))
                    }
                } else {
                    if (repo.hasSdStoragePermission()) {
                        performSdAction(kind)
                    } else {
                        pendingSdAction = kind
                        legacyPermLauncher.launch(
                            arrayOf(
                                Manifest.permission.READ_EXTERNAL_STORAGE,
                                Manifest.permission.WRITE_EXTERNAL_STORAGE
                            )
                        )
                    }
                }
            }

            val activePaletteCfg = previewTheme?.let {
                cfg.copy(themeName = it.first, customThemes = it.second)
            } ?: cfg
            OykAndroidTheme(cfg = activePaletteCfg) {
                val sysDark = isSystemInDarkTheme()
                val anyModalOpen = showAddIface || editingIface != null || editingPeer != null ||
                    showTerminate || showBan || showExit || showAbout || showTermHelp
                when {
                    showOnboarding -> OnboardingPermissionScreen(
                        onContinue = {
                            repo.isOnboardingShown = true
                            showOnboarding = false
                        }
                    )
                    activeScreen == "settings" -> SettingsScreen(
                        cfg = cfg,
                        initialTab = settingsTab,
                        storagePermissionState = permState,
                        missingPermissionHint = hintText,
                        sdStatusMessage = sdStatus,
                        systemDark = sysDark,
                        onUpdateCfg = { updated ->
                            cfg = updated
                            repo.saveInternal(updated)
                        },
                        onImportFromSd = { triggerLazySdPermissionThen("import") },
                        onExportToSd = { triggerLazySdPermissionThen("export") },
                        onAddIface = { showAddIface = true },
                        onEditIface = { editingIface = it },
                        onAddPeer = {
                            editingPeer = PeerConfigState(
                                id = "peer${cfg.peers.size}",
                                callsign = "",
                                ifaceId = cfg.interfaces.firstOrNull()?.id ?: "axudp0",
                                host = ""
                            )
                        },
                        onEditPeer = { editingPeer = it },
                        onPreviewTheme = { chosenName, customList ->
                            previewTheme = chosenName to customList
                        },
                        onSaveTheme = { chosenName, customList ->
                            previewTheme = null
                            cfg = cfg.copy(themeName = chosenName, customThemes = customList)
                            repo.saveInternal(cfg)
                        },
                        onRevertTheme = { previewTheme = null },
                        onBack = {
                            previewTheme = null
                            activeScreen = "console"
                        }
                    )
                    activeScreen == "terminal" -> TerminalScreen(
                        remoteLabel = "${cfg.callsign} (${cfg.alias})",
                        lines = termLines,
                        inputText = termInput,
                        onInputChange = { termInput = it },
                        onSend = {
                            if (termInput.isNotEmpty()) {
                                termLines = termLines + "> $termInput"
                                termInput = ""
                            }
                        },
                        onOpenHelp = { showTermHelp = true },
                        onBack = { activeScreen = "console" }
                    )
                    activeScreen == "files" -> {
                        val baseOykDir = File(filesDir, "oyk")
                        val incDir = File(baseOykDir, "incoming").apply { mkdirs() }
                        val bbsDir = File(baseOykDir, "files").apply { mkdirs() }
                        FilesScreen(
                            incomingRoot = incDir,
                            filesRoot = bbsDir,
                            onBack = { activeScreen = "console" }
                        )
                    }
                    else -> ConsoleScreen(
                        cfg = cfg,
                        sessions = sessions,
                        newHeard = newHeard,
                        bans = bans,
                        usbStatus = usbStatus,
                        nodeRunning = OykNodeService.running,
                        acceptIncoming = acceptIncoming,
                        modalOpen = anyModalOpen,
                        menuOpen = menuOpen,
                        onToggleMenu = { if (!anyModalOpen) menuOpen = !menuOpen },
                        onMenuAction = { label ->
                            menuOpen = false
                            when (label) {
                                "Allow Incoming Connections" -> acceptIncoming = true
                                "Deny Incoming Connections" -> acceptIncoming = false
                                "Local Terminal..." -> activeScreen = "terminal"
                                "Manage Files Area..." -> activeScreen = "files"
                                "Radio Control..." -> { settingsTab = SettingsTab.RIG; activeScreen = "settings" }
                                "Settings..." -> { settingsTab = SettingsTab.NODE; activeScreen = "settings" }
                                "Terminate Connection..." -> showTerminate = true
                                "Ban / Unban Callsign..." -> showBan = true
                                "About OYK-Node..." -> showAbout = true
                                "Exit" -> showExit = true
                            }
                        },
                        onToggleIncoming = { acceptIncoming = !acceptIncoming },
                        onOpenSettings = { t ->
                            settingsTab = t
                            activeScreen = "settings"
                        },
                        onOpenTerminate = { showTerminate = true },
                        onOpenBan = { showBan = true },
                        onOpenExit = { showExit = true },
                        onQuickUsbConnect = {
                            val devs = usbPpp.listCandidateDevices()
                            usbStatus = if (devs.isEmpty()) "No USB radio attached" else "${devs.size} USB CDC found"
                        }
                    )
                }

                if (showAddIface) {
                    AddIfaceWizardDialog(
                        existingIfaces = cfg.interfaces,
                        onDismiss = { showAddIface = false },
                        onCommit = { created ->
                            cfg = cfg.copy(interfaces = cfg.interfaces + created)
                            repo.saveInternal(cfg)
                            showAddIface = false
                        }
                    )
                }
                editingIface?.let { target ->
                    EditIfaceDialog(
                        initial = target,
                        onDismiss = { editingIface = null },
                        onSave = { updated ->
                            cfg = cfg.copy(interfaces = cfg.interfaces.map { if (it.id == target.id) updated else it })
                            repo.saveInternal(cfg)
                            editingIface = null
                        }
                    )
                }
                editingPeer?.let { target ->
                    EditPeerDialog(
                        initial = target,
                        availableIfaces = cfg.interfaces.map { it.id },
                        onDismiss = { editingPeer = null },
                        onSave = { updated ->
                            val exists = cfg.peers.any { it.id == target.id }
                            val next = if (exists) {
                                cfg.peers.map { if (it.id == target.id) updated else it }
                            } else {
                                cfg.peers + updated
                            }
                            cfg = cfg.copy(peers = next)
                            repo.saveInternal(cfg)
                            editingPeer = null
                        }
                    )
                }
                if (showTerminate) {
                    TerminateDialog(
                        sessions = sessions,
                        onDismiss = { showTerminate = false },
                        onTerminate = { sid ->
                            if (NativeNodeBridge.isNativeLoaded()) NativeNodeBridge.nativeTerminateSession(sid)
                            sessions = sessions.filterNot { it.id == sid }
                            showTerminate = false
                        }
                    )
                }
                if (showBan) {
                    BanDialog(
                        bans = bans,
                        onDismiss = { showBan = false },
                        onBan = { b ->
                            if (NativeNodeBridge.isNativeLoaded()) NativeNodeBridge.nativeBanCallsign(b.callsign, b.reason)
                            bans = bans.filterNot { it.callsign == b.callsign } + b
                        },
                        onUnban = { call ->
                            if (NativeNodeBridge.isNativeLoaded()) NativeNodeBridge.nativeUnbanCallsign(call)
                            bans = bans.filterNot { it.callsign == call }
                        }
                    )
                }
                if (showExit) {
                    val autoCount = cfg.peers.count { it.autoLearned }
                    ExitDialog(
                        autoLearnedCount = autoCount,
                        onDismiss = { showExit = false },
                        onSaveAndExit = {
                            cfg = cfg.copy(peers = cfg.peers.map { it.copy(autoLearned = false) })
                            repo.saveInternal(cfg)
                            OykNodeService.gracefulSilentShutdown()
                            finishAndRemoveTask()
                        },
                        onDiscardAndExit = {
                            cfg = cfg.copy(peers = cfg.peers.filterNot { it.autoLearned })
                            OykNodeService.gracefulSilentShutdown()
                            finishAndRemoveTask()
                        },
                        onReviewPeers = {
                            showExit = false
                            settingsTab = SettingsTab.PEERS
                            activeScreen = "settings"
                        }
                    )
                }
                if (showTermHelp) {
                    Dialog(onDismissRequest = { showTermHelp = false }) {
                        TerminalHelpCard(onDismiss = { showTermHelp = false })
                    }
                }
                if (showAbout) {
                    Dialog(onDismissRequest = { showAbout = false }) {
                        AboutCard(
                            callsign = cfg.callsign,
                            alias = cfg.alias,
                            onDismiss = { showAbout = false }
                        )
                    }
                }
            }
        }
    }
}
