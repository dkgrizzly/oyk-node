// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

package com.kr4oyk.oyknode

import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Canvas
import android.os.Looper
import android.view.View
import androidx.activity.ComponentActivity
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.unit.dp
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.io.File
import java.io.FileOutputStream

@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [34], qualifiers = "w411dp-h891dp-xxhdpi")
class OykUiGalleryTest {

    @get:Rule
    val composeRule = createComposeRule()

    private val sampleCfg = NodeConfigState(themeName = "dark")
    private val sampleSessions = listOf(
        SessionState(1L, "W3TRM-1", "axudp0", "00:05:12", "NODE SHELL")
    )
    private val sampleNewHeard = listOf(
        HeardStationState("W3TRM-1", "axudp0", "18:31:25"),
        HeardStationState("R3MOT-7", "axudp0", "18:29:04")
    )
    private val sampleBans = listOf(
        BanEntryState("B4NTST", hardBan = false, appealAllowed = true, liftAt = "permanent", reason = "Banned by sysop.")
    )

    private fun captureComposableToPng(name: String, content: @Composable () -> Unit) {
        val widthPx = 1080
        val heightPx = 2280
        val controller = Robolectric.buildActivity(ComponentActivity::class.java).setup()
        val activity = controller.get()
        try {
            val view = ComposeView(activity)
            view.setLayerType(View.LAYER_TYPE_SOFTWARE, null)
            view.setContent {
                OykAndroidTheme(cfg = sampleCfg) {
                    content()
                }
            }
            activity.setContentView(view)
            val mainShadow = Shadows.shadowOf(Looper.getMainLooper())
            mainShadow.idle()

            val wSpec = View.MeasureSpec.makeMeasureSpec(widthPx, View.MeasureSpec.EXACTLY)
            val hSpec = View.MeasureSpec.makeMeasureSpec(heightPx, View.MeasureSpec.EXACTLY)
            view.measure(wSpec, hSpec)
            view.layout(0, 0, widthPx, heightPx)
            mainShadow.idle()

            val bmp = Bitmap.createBitmap(widthPx, heightPx, Bitmap.Config.ARGB_8888)
            val canvas = Canvas(bmp)
            view.draw(canvas)

            // Confirm the frame actually rendered UI pixels (at least 2 distinct RGB values).
            val firstPixel = bmp.getPixel(40, 40)
            var foundDistinct = false
            outer@ for (y in 20 until heightPx step 25) {
                for (x in 20 until widthPx step 25) {
                    if (bmp.getPixel(x, y) != firstPixel) {
                        foundDistinct = true
                        break@outer
                    }
                }
            }
            assertTrue("Captured bitmap for android_${name}.png rendered blank/uniform", foundDistinct)

            val capDir = System.getenv("OYK_UI_CAPTURE_DIR")
            if (!capDir.isNullOrBlank()) {
                val outDir = File(capDir).apply { mkdirs() }
                FileOutputStream(File(outDir, "android_${name}.png")).use { out ->
                    bmp.compress(Bitmap.CompressFormat.PNG, 100, out)
                }
            }
            bmp.recycle()
        } finally {
            controller.pause().stop().destroy()
        }
    }

    @Composable
    private fun ModalOverlayFrame(modal: @Composable () -> Unit) {
        Box(modifier = Modifier.fillMaxSize()) {
            ConsoleScreen(
                cfg = sampleCfg,
                sessions = sampleSessions,
                newHeard = sampleNewHeard,
                bans = sampleBans,
                usbStatus = "USB PPP ready",
                nodeRunning = true,
                acceptIncoming = true,
                onToggleIncoming = {},
                onOpenSettings = {},
                onOpenTerminate = {},
                onOpenBan = {},
                onOpenTheme = {},
                onOpenExit = {},
                onQuickUsbConnect = {}
            )
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black.copy(alpha = 0.58f))
                    .padding(20.dp),
                contentAlignment = Alignment.Center
            ) {
                modal()
            }
        }
    }

    @Test
    fun onboardingAndPermissionDeclinedHintWorkCorrectly() {
        val step = mutableStateOf(0)
        composeRule.setContent {
            OykAndroidTheme(cfg = sampleCfg) {
                if (step.value == 0) {
                    OnboardingPermissionScreen(onContinue = { step.value = 1 })
                } else {
                    SettingsScreen(
                        cfg = sampleCfg,
                        initialTab = SettingsTab.NODE,
                        storagePermissionState = StoragePermissionState.DECLINED,
                        missingPermissionHint = "SD Card import/export requires storage permission. Enable it in Settings.",
                        sdStatusMessage = "",
                        onUpdateCfg = {},
                        onImportFromSd = {},
                        onExportToSd = {},
                        onOpenTheme = {},
                        onAddIface = {},
                        onEditIface = {},
                        onAddPeer = {},
                        onEditPeer = {},
                        onBack = {}
                    )
                }
            }
        }
        composeRule.onNodeWithText("Why we ask for SD Card / Storage Access:").assertIsDisplayed()
        step.value = 1
        composeRule.onNodeWithText("Import from SD").assertIsNotEnabled()
        composeRule.onNodeWithText("Export to SD").assertIsNotEnabled()
        composeRule.onNodeWithText(
            "SD Card import/export requires storage permission. Enable it in Settings."
        ).assertIsDisplayed()
    }

    @Test
    fun consoleDisplaysLastFiveNewCallsignsHeardCard() {
        composeRule.setContent {
            OykAndroidTheme(cfg = sampleCfg) {
                ConsoleScreen(
                    cfg = sampleCfg,
                    sessions = sampleSessions,
                    newHeard = sampleNewHeard,
                    bans = sampleBans,
                    usbStatus = "USB PPP ready",
                    nodeRunning = true,
                    acceptIncoming = true,
                    onToggleIncoming = {},
                    onOpenSettings = {},
                    onOpenTerminate = {},
                    onOpenBan = {},
                    onOpenTheme = {},
                    onOpenExit = {},
                    onQuickUsbConnect = {}
                )
            }
        }
        composeRule.onNodeWithText("Last 5 New Callsigns Heard (MHEARD)").assertIsDisplayed()
        composeRule.onNodeWithText("1. W3TRM-1 (axudp0)").assertIsDisplayed()
    }

    @Test
    fun captureAllAndroidGalleryScreens() {
        captureComposableToPng("main") {
            ConsoleScreen(
                cfg = sampleCfg,
                sessions = sampleSessions,
                newHeard = sampleNewHeard,
                bans = sampleBans,
                usbStatus = "USB PPP ready",
                nodeRunning = true,
                acceptIncoming = true,
                onToggleIncoming = {},
                onOpenSettings = {},
                onOpenTerminate = {},
                onOpenBan = {},
                onOpenTheme = {},
                onOpenExit = {},
                onQuickUsbConnect = {}
            )
        }
        captureComposableToPng("menu") {
            ModalOverlayFrame {
                NodeMenuCard(acceptIncoming = true, onAction = {}, onDismiss = {})
            }
        }
        fun captureSettingsTab(flow: String, tab: SettingsTab, perm: StoragePermissionState, hint: String) {
            captureComposableToPng(flow) {
                SettingsScreen(
                    cfg = sampleCfg,
                    initialTab = tab,
                    storagePermissionState = perm,
                    missingPermissionHint = hint,
                    sdStatusMessage = "",
                    onUpdateCfg = {},
                    onImportFromSd = {},
                    onExportToSd = {},
                    onOpenTheme = {},
                    onAddIface = {},
                    onEditIface = {},
                    onAddPeer = {},
                    onEditPeer = {},
                    onBack = {}
                )
            }
        }
        captureSettingsTab(
            "settings",
            SettingsTab.NODE,
            StoragePermissionState.DECLINED,
            "Storage permission declined: enable storage/document access in Settings to import or export configs."
        )
        captureSettingsTab("settings_bbs", SettingsTab.BBS, StoragePermissionState.GRANTED, "")
        captureSettingsTab("settings_chat", SettingsTab.CHAT, StoragePermissionState.GRANTED, "")
        captureSettingsTab("settings_ifaces", SettingsTab.INTERFACES, StoragePermissionState.GRANTED, "")
        captureSettingsTab("peers", SettingsTab.PEERS, StoragePermissionState.GRANTED, "")
        captureSettingsTab("settings_rig", SettingsTab.RIG, StoragePermissionState.GRANTED, "")
        captureSettingsTab("settings_printer", SettingsTab.PRINTER, StoragePermissionState.GRANTED, "")
        for (step in 1..4) {
            captureComposableToPng("add_iface_$step") {
                ModalOverlayFrame {
                    AddIfaceWizardCard(
                        existingIfaces = sampleCfg.interfaces,
                        initialStep = step,
                        onDismiss = {},
                        onCommit = {}
                    )
                }
            }
        }
        captureComposableToPng("edit_iface") {
            ModalOverlayFrame {
                EditIfaceCard(initial = sampleCfg.interfaces.first(), onDismiss = {}, onSave = {})
            }
        }
        captureComposableToPng("edit_peer") {
            ModalOverlayFrame {
                EditPeerCard(
                    initial = sampleCfg.peers.first(),
                    availableIfaces = sampleCfg.interfaces.map { it.id },
                    onDismiss = {},
                    onSave = {}
                )
            }
        }
        captureComposableToPng("terminate") {
            ModalOverlayFrame {
                TerminateCard(sessions = sampleSessions, onDismiss = {}, onTerminate = {})
            }
        }
        captureComposableToPng("ban") {
            ModalOverlayFrame {
                BanCard(bans = sampleBans, onDismiss = {}, onBan = {}, onUnban = {})
            }
        }
        captureComposableToPng("exit") {
            ModalOverlayFrame {
                ExitCard(autoLearnedCount = 1, onDismiss = {}, onSaveAndExit = {}, onDiscardAndExit = {}, onReviewPeers = {})
            }
        }
        captureComposableToPng("theme") {
            ModalOverlayFrame {
                ThemeSettingsCard(cfg = sampleCfg, systemDark = true, onDismiss = {}, onSave = { _, _ -> })
            }
        }
        captureComposableToPng("terminal") {
            ModalOverlayFrame {
                TerminalCard(
                    remoteLabel = "KR4OYK-1 BBS (LOCAL)",
                    lines = listOf(
                        "*** Local Packet Terminal (YAPP ready)",
                        "*** Connected to KR4OYK-1 BBS",
                        "Welcome to KR4OYK-1 Packet BBS (OYKNOD)",
                        "KR4OYK-1> LIST",
                        "Msg#  Size  From     Subject",
                        "1     248   W3TRM-1  Welcome to OYK Node",
                        "KR4OYK-1> "
                    ),
                    inputText = "R 1",
                    onSend = {},
                    onBack = {}
                )
            }
        }
        captureComposableToPng("terminal_help") {
            ModalOverlayFrame {
                TerminalHelpCard(onDismiss = {})
            }
        }
        val tmpRoot = File(System.getProperty("java.io.tmpdir") ?: "/tmp", "oyk_android_fm_gallery")
        val incDir = File(tmpRoot, "incoming").apply { deleteRecursively(); mkdirs() }
        File(incDir, "W3TRM-1").apply { mkdirs(); File(this, "fieldday.txt").writeText("FD 2026\n") }
        val filesDir = File(tmpRoot, "files").apply { deleteRecursively(); mkdirs() }
        File(filesDir, "bulletins").apply { mkdirs(); File(this, "welcome.txt").writeText("Welcome\n") }
        File(filesDir, "readme.txt").writeText("Readme\n")
        captureComposableToPng("files") {
            ModalOverlayFrame {
                FilesCard(
                    incomingRoot = incDir,
                    filesRoot = filesDir,
                    onDismiss = {}
                )
            }
        }
        captureComposableToPng("about") {
            ModalOverlayFrame {
                AboutCard(callsign = sampleCfg.callsign, alias = sampleCfg.alias, onDismiss = {})
            }
        }
    }

    @Test
    fun filesCardSwitchesBetweenPortraitColumnAndLandscapeRow() {
        val isLand = mutableStateOf(false)
        composeRule.setContent {
            val baseCfg = LocalConfiguration.current
            val effCfg = Configuration(baseCfg).apply {
                orientation = if (isLand.value) Configuration.ORIENTATION_LANDSCAPE else Configuration.ORIENTATION_PORTRAIT
            }
            CompositionLocalProvider(LocalConfiguration provides effCfg) {
                OykAndroidTheme(cfg = sampleCfg) {
                    FilesCard(
                        incomingFiles = listOf(Triple("W3TRM-1", "fieldday.txt", "1.4 KB")),
                        approvedFiles = listOf(Pair("bulletins/welcome.txt", "420 B")),
                        onDismiss = {}
                    )
                }
            }
        }
        composeRule.onNodeWithTag("files_layout_column").assertIsDisplayed()
        isLand.value = true
        composeRule.waitForIdle()
        composeRule.onNodeWithTag("files_layout_row").assertIsDisplayed()
    }
}
