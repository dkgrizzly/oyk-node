// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

package com.kr4oyk.oyknode

import android.content.res.Configuration
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.io.File

@Composable
fun OnboardingPermissionScreen(
    onContinue: () -> Unit
) {
    Surface(modifier = Modifier.fillMaxSize()) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(24.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.Center
        ) {
            Icon(
                imageVector = Icons.Default.SdStorage,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(48.dp)
            )
            Spacer(Modifier.height(16.dp))
            Text("Welcome to OYK Packet Node", style = MaterialTheme.typography.headlineSmall)
            Spacer(Modifier.height(12.dp))
            Text(
                "Why we ask for SD Card / Storage Access:",
                style = MaterialTheme.typography.titleMedium
            )
            Spacer(Modifier.height(6.dp))
            Text(
                "OYK Packet Node stores your active node configuration inside Android's private app storage. " +
                    "We only request SD card (or shared storage) access when you tap Import or Export configuration in Settings.",
                style = MaterialTheme.typography.bodyMedium
            )
            Spacer(Modifier.height(12.dp))
            Card(
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
                modifier = Modifier.fillMaxWidth()
            ) {
                Column(modifier = Modifier.padding(14.dp)) {
                    Text("If you decline this optional permission later:", fontWeight = FontWeight.Bold)
                    Spacer(Modifier.height(4.dp))
                    Text("- Your packet node, BBS, Chat, USB PPP radio, and internal node.cfg keep working normally.")
                    Text("- Only SD Card Import/Export features in Settings will be unavailable and marked disabled with a hint showing which permission is needed.")
                }
            }
            Spacer(Modifier.height(20.dp))
            Button(
                onClick = onContinue,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text("Continue to Node Console")
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConsoleScreen(
    cfg: NodeConfigState,
    sessions: List<SessionState>,
    newHeard: List<HeardStationState> = emptyList(),
    bans: List<BanEntryState>,
    usbStatus: String,
    nodeRunning: Boolean,
    acceptIncoming: Boolean,
    modalOpen: Boolean = false,
    menuOpen: Boolean = false,
    onToggleMenu: () -> Unit = {},
    onMenuAction: (String) -> Unit = {},
    onToggleIncoming: () -> Unit,
    onOpenSettings: (SettingsTab) -> Unit,
    onOpenTerminate: () -> Unit,
    onOpenBan: () -> Unit,
    onOpenExit: () -> Unit,
    onQuickUsbConnect: () -> Unit
) {
    val isLandscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
    val badgeBg = if (MaterialTheme.colorScheme.surface.luminance() < 0.5f) {
        Color(0xFFE2E8F0)
    } else {
        Color(0xFFBAE6FD)
    }

    Scaffold(
        modifier = Modifier
            .fillMaxSize()
            .windowInsetsPadding(WindowInsets.displayCutout),
        topBar = {
            TopAppBar(
                navigationIcon = {
                    IconButton(
                        onClick = { if (!modalOpen) onToggleMenu() },
                        modifier = Modifier
                            .padding(start = 4.dp)
                            .testTag("node_menu_toggle")
                    ) {
                        Box(
                            modifier = Modifier
                                .size(38.dp)
                                .clip(CircleShape)
                                .background(badgeBg)
                                .border(1.5.dp, MaterialTheme.colorScheme.primary, CircleShape)
                                .testTag("node_menu_icon_badge"),
                            contentAlignment = Alignment.Center
                        ) {
                            Image(
                                painter = painterResource(id = R.drawable.ic_oyk_logo),
                                contentDescription = "Node Menu",
                                modifier = Modifier.size(24.dp)
                            )
                        }
                    }
                },
                title = {
                    Column {
                        Text(
                            "${cfg.callsign} (${cfg.alias})",
                            style = MaterialTheme.typography.titleMedium,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                        Text(
                            text = if (isLandscape) {
                                "Conn: ${sessions.size} | IN: ${if (acceptIncoming) "ALLOW" else "DENY"} | USB: $usbStatus"
                            } else {
                                "Conn: ${sessions.size} | IN: ${if (acceptIncoming) "ALLOW" else "DENY"}"
                            },
                            style = MaterialTheme.typography.labelSmall,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                },
                actions = {
                    IconButton(onClick = onToggleIncoming) {
                        Icon(
                            if (acceptIncoming) Icons.Default.Wifi else Icons.Default.WifiOff,
                            contentDescription = "Toggle Incoming"
                        )
                    }
                    IconButton(onClick = { onOpenSettings(SettingsTab.NODE) }) {
                        Icon(Icons.Default.Settings, contentDescription = "Settings")
                    }
                    if (isLandscape) {
                        IconButton(onClick = onOpenExit) {
                            Icon(Icons.Default.PowerSettingsNew, contentDescription = "Exit")
                        }
                    }
                }
            )
        }
    ) { pad ->
        Box(modifier = Modifier.fillMaxSize().padding(pad)) {
            BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
                val twoCol = isLandscape || maxWidth >= 600.dp
                LazyColumn(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(12.dp)
                        .testTag(if (twoCol) "console_layout_landscape" else "console_layout_portrait"),
                    verticalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    item {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            AssistChip(
                                onClick = onOpenTerminate,
                                label = { Text("Terminate") },
                                leadingIcon = { Icon(Icons.Default.Close, contentDescription = null) }
                            )
                            AssistChip(
                                onClick = onOpenBan,
                                label = { Text("Ban (${bans.size})") },
                                leadingIcon = { Icon(Icons.Default.Block, contentDescription = null) }
                            )
                            AssistChip(
                                onClick = onQuickUsbConnect,
                                label = { Text("USB PPP") },
                                leadingIcon = { Icon(Icons.Default.Usb, contentDescription = null) }
                            )
                        }
                    }

                    if (twoCol) {
                        item {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.spacedBy(10.dp)
                            ) {
                                InterfacesConsoleCard(cfg, modifier = Modifier.weight(1f))
                                MheardConsoleCard(newHeard, modifier = Modifier.weight(1f))
                            }
                        }
                        item {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.spacedBy(10.dp)
                            ) {
                                ActiveConnectionsConsoleCard(sessions, nodeRunning, modifier = Modifier.weight(1f))
                                EventLogConsoleCard(cfg, modifier = Modifier.weight(1f))
                            }
                        }
                    } else {
                        item { InterfacesConsoleCard(cfg, modifier = Modifier.fillMaxWidth()) }
                        item { MheardConsoleCard(newHeard, modifier = Modifier.fillMaxWidth()) }
                        item { ActiveConnectionsConsoleCard(sessions, nodeRunning, modifier = Modifier.fillMaxWidth()) }
                        item { EventLogConsoleCard(cfg, modifier = Modifier.fillMaxWidth()) }
                    }
                }
            }

            if (menuOpen && !modalOpen) {
                BackHandler(enabled = true) { onToggleMenu() }
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .background(Color.Black.copy(alpha = 0.50f))
                        .clickable(onClick = onToggleMenu)
                        .testTag("node_menu_scrim")
                )
                Surface(
                    color = MaterialTheme.colorScheme.surface,
                    tonalElevation = 8.dp,
                    shadowElevation = 12.dp,
                    modifier = Modifier
                        .width(280.dp)
                        .fillMaxHeight()
                        .align(Alignment.CenterStart)
                        .testTag("node_menu_drawer")
                ) {
                    NodeMenuDrawerContent(
                        acceptIncoming = acceptIncoming,
                        onAction = onMenuAction
                    )
                }
            }
        }
    }
}

@Composable
private fun InterfacesConsoleCard(cfg: NodeConfigState, modifier: Modifier = Modifier) {
    Card(modifier = modifier) {
        Column(modifier = Modifier.padding(12.dp)) {
            Text("Interfaces (${cfg.interfaces.size})", style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(6.dp))
            cfg.interfaces.forEach { ic ->
                Row(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("${ic.id} (${ic.type})", fontFamily = FontFamily.Monospace)
                    Text("C:${ic.activeConnections} ${ic.status}", style = MaterialTheme.typography.bodySmall)
                }
            }
        }
    }
}

@Composable
private fun MheardConsoleCard(newHeard: List<HeardStationState>, modifier: Modifier = Modifier) {
    Card(modifier = modifier) {
        Column(modifier = Modifier.padding(12.dp)) {
            Text("Last 5 New Callsigns Heard (MHEARD)", style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(6.dp))
            if (newHeard.isEmpty()) {
                Text("No stations heard yet.", style = MaterialTheme.typography.bodySmall)
            } else {
                newHeard.take(5).forEachIndexed { idx, h ->
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text("${idx + 1}. ${h.callsign} (${h.ifaceId})", fontFamily = FontFamily.Monospace)
                        Text(h.heardHms, style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
        }
    }
}

@Composable
private fun ActiveConnectionsConsoleCard(
    sessions: List<SessionState>,
    nodeRunning: Boolean,
    modifier: Modifier = Modifier
) {
    Card(modifier = modifier) {
        Column(modifier = Modifier.padding(12.dp)) {
            Text("Active Connections (${sessions.size}) | Node: ${if (nodeRunning) "RUNNING" else "STOPPED"}",
                style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(6.dp))
            if (sessions.isEmpty()) {
                Text("No active connections.", style = MaterialTheme.typography.bodySmall)
            } else {
                sessions.forEach { s ->
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text("#${s.id} ${s.callsign} (${s.ifaceId})", fontWeight = FontWeight.Medium)
                        Text("${s.sinceHms} ${s.role}", style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
        }
    }
}

@Composable
private fun EventLogConsoleCard(cfg: NodeConfigState, modifier: Modifier = Modifier) {
    Card(modifier = modifier) {
        Column(modifier = Modifier.padding(12.dp)) {
            Text("Status & Event Log", style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(4.dp))
            Text("[INFO] Active theme: ${cfg.themeName}", fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
            Text("[INFO] Config storage: internal filesDir/oyk/node.cfg", fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
            Text("[INFO] Listening AXUDP on 0.0.0.0:10093", fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Composable
fun TerminalScreen(
    remoteLabel: String,
    lines: List<String>,
    inputText: String,
    eolLabel: String = "CR",
    wrapEnabled: Boolean = true,
    onInputChange: (String) -> Unit = {},
    onSend: () -> Unit,
    onSpecialKey: (String) -> Unit = {},
    onOpenHelp: () -> Unit = {},
    onBack: () -> Unit
) {
    val isLandscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
    var ctrlArmed by remember { mutableStateOf(false) }
    var altArmed by remember { mutableStateOf(false) }
    var ctrlAPrefix by remember { mutableStateOf(false) }

    fun handleToolbarKey(key: String) {
        when (key) {
            "CTRL" -> {
                ctrlArmed = !ctrlArmed
                if (ctrlArmed) altArmed = false
            }
            "ALT" -> {
                altArmed = !altArmed
                if (altArmed) ctrlArmed = false
            }
            "ESC" -> {
                ctrlArmed = false
                altArmed = false
                ctrlAPrefix = false
                onSpecialKey("ESC")
            }
            "F1" -> onOpenHelp()
            "F9" -> onBack()
            else -> onSpecialKey(key)
        }
    }

    fun handleTypedInput(next: String) {
        if ((ctrlArmed || altArmed || ctrlAPrefix) && next.length > inputText.length) {
            val ch = next.last()
            val upper = ch.uppercaseChar()
            if (ctrlAPrefix) {
                ctrlAPrefix = false
                when (upper) {
                    'Z', '?', 'H' -> onOpenHelp()
                    'E' -> onSpecialKey("CTRL_A_E")
                    'W' -> onSpecialKey("CTRL_A_W")
                    'Q' -> onBack()
                    else -> onSpecialKey("CTRL_A_$upper")
                }
                return
            }
            if (ctrlArmed) {
                ctrlArmed = false
                if (upper == 'A') {
                    ctrlAPrefix = true
                } else {
                    onSpecialKey("CTRL_$upper")
                }
                return
            }
            if (altArmed) {
                altArmed = false
                onSpecialKey("ALT_$upper")
                return
            }
        }
        onInputChange(next)
    }

    BackHandler(onBack = onBack)
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .windowInsetsPadding(WindowInsets.displayCutout)
            .statusBarsPadding()
            .navigationBarsPadding()
            .imePadding()
            .testTag("terminal_fullscreen")
    ) {
        Surface(
            color = MaterialTheme.colorScheme.surface,
            contentColor = MaterialTheme.colorScheme.onSurface,
            modifier = Modifier.fillMaxWidth()
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(40.dp)
                    .padding(horizontal = 4.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                IconButton(onClick = onBack, modifier = Modifier.size(34.dp)) {
                    Icon(
                        Icons.AutoMirrored.Filled.ArrowBack,
                        contentDescription = "Console",
                        tint = MaterialTheme.colorScheme.onSurface,
                        modifier = Modifier.size(18.dp)
                    )
                }
                Text(
                    "Terminal: $remoteLabel",
                    color = MaterialTheme.colorScheme.onSurface,
                    style = MaterialTheme.typography.titleSmall,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f).padding(horizontal = 4.dp)
                )
                TextButton(
                    onClick = onOpenHelp,
                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 2.dp),
                    modifier = Modifier.height(30.dp)
                ) {
                    Text("Help", style = MaterialTheme.typography.labelMedium)
                }
            }
        }
        HorizontalDivider(thickness = 1.dp)
        Box(
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
                .background(Color(0xFF0F172A))
                .padding(horizontal = 4.dp, vertical = 3.dp)
        ) {
            Column(
                modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(1.dp)
            ) {
                lines.forEach { ln ->
                    Text(
                        ln,
                        color = Color(0xFFE2E8F0),
                        fontFamily = FontFamily.Monospace,
                        softWrap = wrapEnabled,
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }
        }
        val modBadge = buildString {
            if (ctrlAPrefix) append(" | [Ctrl-A]")
            if (ctrlArmed) append(" | [CTRL]")
            if (altArmed) append(" | [ALT]")
        }
        Surface(
            color = MaterialTheme.colorScheme.secondaryContainer,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(
                "Ctrl-A Z:Help | EOL:$eolLabel | Wrap:${if (wrapEnabled) "ON" else "OFF"}$modBadge",
                fontFamily = FontFamily.Monospace,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSecondaryContainer,
                modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp)
            )
        }
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(MaterialTheme.colorScheme.surface)
                .padding(horizontal = 4.dp, vertical = 3.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            Box(
                modifier = Modifier
                    .weight(1f)
                    .height(36.dp)
                    .background(Color(0xFF1E293B), RoundedCornerShape(4.dp))
                    .border(1.dp, MaterialTheme.colorScheme.outline, RoundedCornerShape(4.dp))
                    .padding(horizontal = 8.dp),
                contentAlignment = Alignment.CenterStart
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(
                        "> ",
                        color = Color(0xFF38BDF8),
                        fontFamily = FontFamily.Monospace,
                        style = MaterialTheme.typography.bodySmall
                    )
                    Box(modifier = Modifier.weight(1f)) {
                        if (inputText.isEmpty()) {
                            Text(
                                "Command",
                                color = Color(0xFF64748B),
                                fontFamily = FontFamily.Monospace,
                                style = MaterialTheme.typography.bodySmall
                            )
                        }
                        BasicTextField(
                            value = inputText,
                            onValueChange = ::handleTypedInput,
                            singleLine = true,
                            textStyle = TextStyle(
                                color = Color(0xFFF8FAFC),
                                fontFamily = FontFamily.Monospace,
                                fontSize = 13.sp
                            ),
                            cursorBrush = SolidColor(Color(0xFF38BDF8)),
                            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Send),
                            keyboardActions = KeyboardActions(onSend = { onSend() }),
                            modifier = Modifier.fillMaxWidth().testTag("terminal_input")
                        )
                    }
                }
            }
            Button(
                onClick = onSend,
                contentPadding = PaddingValues(horizontal = 12.dp, vertical = 4.dp),
                modifier = Modifier.height(36.dp)
            ) {
                Text("Send", style = MaterialTheme.typography.labelMedium)
            }
        }
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF0B1120))
                .padding(horizontal = 2.dp, vertical = 2.dp)
                .testTag("terminal_extra_keys"),
            verticalArrangement = Arrangement.spacedBy(2.dp)
        ) {
            val navKeys = listOf(
                "Ctrl" to "CTRL",
                "Alt" to "ALT",
                "Esc" to "ESC",
                "Tab" to "TAB",
                "\u2190" to "LEFT",
                "\u2191" to "UP",
                "\u2193" to "DOWN",
                "\u2192" to "RIGHT"
            )
            val fnKeys = (1..12).map { "F$it" to "F$it" }
            if (isLandscape) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(2.dp)
                ) {
                    (navKeys + fnKeys).forEach { (label, code) ->
                        val active = (code == "CTRL" && ctrlArmed) || (code == "ALT" && altArmed)
                        TerminalSoftKeyChip(
                            label = label,
                            active = active,
                            tag = "term_key_${code.lowercase()}",
                            onClick = { handleToolbarKey(code) }
                        )
                    }
                }
            } else {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(2.dp)
                ) {
                    navKeys.forEach { (label, code) ->
                        val active = (code == "CTRL" && ctrlArmed) || (code == "ALT" && altArmed)
                        TerminalSoftKeyChip(
                            label = label,
                            active = active,
                            tag = "term_key_${code.lowercase()}",
                            modifier = Modifier.weight(1f),
                            onClick = { handleToolbarKey(code) }
                        )
                    }
                }
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(2.dp)
                ) {
                    fnKeys.forEach { (label, code) ->
                        TerminalSoftKeyChip(
                            label = label,
                            active = false,
                            tag = "term_key_${code.lowercase()}",
                            onClick = { handleToolbarKey(code) }
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun TerminalSoftKeyChip(
    label: String,
    active: Boolean,
    tag: String,
    modifier: Modifier = Modifier,
    onClick: () -> Unit
) {
    Surface(
        color = if (active) Color(0xFF0284C7) else Color(0xFF1E293B),
        shape = RoundedCornerShape(4.dp),
        modifier = modifier
            .height(28.dp)
            .border(
                1.dp,
                if (active) Color(0xFF38BDF8) else Color(0xFF334155),
                RoundedCornerShape(4.dp)
            )
            .clickable(onClick = onClick)
            .testTag(tag)
    ) {
        Box(
            contentAlignment = Alignment.Center,
            modifier = Modifier.padding(horizontal = 7.dp)
        ) {
            Text(
                label,
                color = Color(0xFFF8FAFC),
                fontFamily = FontFamily.Monospace,
                fontWeight = if (active) FontWeight.Bold else FontWeight.Medium,
                fontSize = 11.sp,
                maxLines = 1,
                softWrap = false
            )
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FilesScreen(
    incomingFiles: List<Triple<String, String, String>> = emptyList(),
    approvedFiles: List<Pair<String, String>> = emptyList(),
    incomingRoot: File? = null,
    filesRoot: File? = null,
    onBack: () -> Unit
) {
    BackHandler(onBack = onBack)
    var incRel by remember { mutableStateOf("") }
    var filesRel by remember { mutableStateOf("") }
    var activePane by remember { mutableIntStateOf(0) }
    var incSel by remember { mutableIntStateOf(0) }
    var filesSel by remember { mutableIntStateOf(0) }
    var refreshNonce by remember { mutableIntStateOf(0) }
    var feedback by remember { mutableStateOf("Tap folder to navigate; panes are jailed to their root.") }

    val incEntries: List<OykBbsDirEntryState> = remember(incomingRoot, incRel, refreshNonce, incomingFiles) {
        if (incomingRoot != null) {
            OykJailedFileBrowser.listDir(incomingRoot, incRel)?.second ?: emptyList()
        } else {
            incomingFiles.map { (u, n, _) -> OykBbsDirEntryState("$u / $n", 1024L, isDir = false) }
        }
    }
    val filesEntries: List<OykBbsDirEntryState> = remember(filesRoot, filesRel, refreshNonce, approvedFiles) {
        if (filesRoot != null) {
            OykJailedFileBrowser.listDir(filesRoot, filesRel)?.second ?: emptyList()
        } else {
            approvedFiles.map { (rel, _) -> OykBbsDirEntryState(rel, 512L, isDir = false) }
        }
    }
    val isLandscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE

    Scaffold(
        modifier = Modifier
            .fillMaxSize()
            .windowInsetsPadding(WindowInsets.displayCutout)
            .testTag("files_fullscreen"),
        topBar = {
            TopAppBar(
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Console")
                    }
                },
                title = {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Image(
                            painter = painterResource(id = R.drawable.ic_oyk_logo),
                            contentDescription = "OYK Logo",
                            modifier = Modifier.size(24.dp)
                        )
                        Text("Manage BBS Files Area", style = MaterialTheme.typography.titleMedium)
                    }
                }
            )
        }
    ) { pad ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(pad)
                .padding(12.dp)
        ) {
            Text(feedback, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.secondary)
            Spacer(Modifier.height(8.dp))
            BoxWithConstraints(modifier = Modifier.weight(1f).fillMaxWidth()) {
                val useSideBySide = isLandscape || maxWidth >= 560.dp
                if (useSideBySide) {
                    Row(
                        modifier = Modifier.fillMaxSize().testTag("files_layout_row"),
                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        FilePaneBox(
                            title = "Incoming: /$incRel",
                            entries = incEntries,
                            selectedIdx = incSel,
                            active = activePane == 0,
                            onSelect = { activePane = 0; incSel = it },
                            onOpenDir = { child ->
                                incomingRoot?.let { r ->
                                    OykJailedFileBrowser.enterSubdir(r, incRel, child)
                                        .onSuccess { incRel = it; incSel = 0; feedback = "Incoming: /$it" }
                                        .onFailure { feedback = it.message ?: "Blocked" }
                                }
                            },
                            modifier = Modifier.weight(1f).fillMaxHeight().testTag("files_pane_incoming")
                        )
                        FilePaneBox(
                            title = "Files: /$filesRel",
                            entries = filesEntries,
                            selectedIdx = filesSel,
                            active = activePane == 1,
                            onSelect = { activePane = 1; filesSel = it },
                            onOpenDir = { child ->
                                filesRoot?.let { r ->
                                    OykJailedFileBrowser.enterSubdir(r, filesRel, child)
                                        .onSuccess { filesRel = it; filesSel = 0; feedback = "Files: /$it" }
                                        .onFailure { feedback = it.message ?: "Blocked" }
                                }
                            },
                            modifier = Modifier.weight(1f).fillMaxHeight().testTag("files_pane_approved")
                        )
                    }
                } else {
                    Column(
                        modifier = Modifier.fillMaxSize().testTag("files_layout_column"),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        FilePaneBox(
                            title = "Incoming: /$incRel",
                            entries = incEntries,
                            selectedIdx = incSel,
                            active = activePane == 0,
                            onSelect = { activePane = 0; incSel = it },
                            onOpenDir = { child ->
                                incomingRoot?.let { r ->
                                    OykJailedFileBrowser.enterSubdir(r, incRel, child)
                                        .onSuccess { incRel = it; incSel = 0; feedback = "Incoming: /$it" }
                                        .onFailure { feedback = it.message ?: "Blocked" }
                                }
                            },
                            modifier = Modifier.weight(1f).fillMaxWidth().testTag("files_pane_incoming")
                        )
                        FilePaneBox(
                            title = "Files: /$filesRel",
                            entries = filesEntries,
                            selectedIdx = filesSel,
                            active = activePane == 1,
                            onSelect = { activePane = 1; filesSel = it },
                            onOpenDir = { child ->
                                filesRoot?.let { r ->
                                    OykJailedFileBrowser.enterSubdir(r, filesRel, child)
                                        .onSuccess { filesRel = it; filesSel = 0; feedback = "Files: /$it" }
                                        .onFailure { feedback = it.message ?: "Blocked" }
                                }
                            },
                            modifier = Modifier.weight(1f).fillMaxWidth().testTag("files_pane_approved")
                        )
                    }
                }
            }
            Spacer(Modifier.height(10.dp))
            Row(
                modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                OutlinedButton(onClick = { refreshNonce++ }) { Text("Upload") }
                Button(onClick = {
                    val e = incEntries.getOrNull(incSel)
                    if (incomingRoot != null && filesRoot != null && e != null && !e.isDir) {
                        val rel = if (incRel.isEmpty()) e.name else "$incRel/${e.name}"
                        OykJailedFileBrowser.approveRelFile(incomingRoot, rel, filesRoot, filesRel)
                            .onSuccess { refreshNonce++; feedback = "Approved ${e.name}" }
                            .onFailure { feedback = it.message ?: "Error" }
                    }
                }) { Text("Approve") }
                OutlinedButton(onClick = { refreshNonce++ }) { Text("Rename") }
                OutlinedButton(onClick = {
                    val targetRoot = if (activePane == 0) incomingRoot else filesRoot
                    val curRel = if (activePane == 0) incRel else filesRel
                    if (targetRoot != null) {
                        OykJailedFileBrowser.mkdir(targetRoot, curRel, "new_dir")
                            .onSuccess { refreshNonce++; feedback = "Created folder" }
                    }
                }) { Text("New Dir") }
                OutlinedButton(onClick = {
                    val targetRoot = if (activePane == 0) incomingRoot else filesRoot
                    val curRel = if (activePane == 0) incRel else filesRel
                    val e = (if (activePane == 0) incEntries else filesEntries).getOrNull(if (activePane == 0) incSel else filesSel)
                    if (targetRoot != null && e != null && !e.isParent) {
                        OykJailedFileBrowser.deleteEntry(targetRoot, curRel, e.name)
                            .onSuccess { refreshNonce++; feedback = "Deleted ${e.name}" }
                    }
                }) { Text("Delete") }
                OutlinedButton(onClick = { feedback = "Previewing selected file" }) { Text("View") }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    cfg: NodeConfigState,
    initialTab: SettingsTab = SettingsTab.NODE,
    storagePermissionState: StoragePermissionState,
    missingPermissionHint: String,
    sdStatusMessage: String,
    systemDark: Boolean = isSystemInDarkTheme(),
    onUpdateCfg: (NodeConfigState) -> Unit,
    onImportFromSd: () -> Unit,
    onExportToSd: () -> Unit,
    onAddIface: () -> Unit,
    onEditIface: (InterfaceConfigState) -> Unit,
    onAddPeer: () -> Unit,
    onEditPeer: (PeerConfigState) -> Unit,
    onSaveTheme: (selectedTheme: String, updatedCustomThemes: List<ThemeConfigState>) -> Unit = { _, _ -> },
    onPreviewTheme: (selectedTheme: String, updatedCustomThemes: List<ThemeConfigState>) -> Unit = { _, _ -> },
    onRevertTheme: () -> Unit = {},
    onBack: () -> Unit
) {
    var tab by remember(initialTab) { mutableStateOf(initialTab) }
    val sdControlsEnabled = storagePermissionState != StoragePermissionState.DECLINED

    BackHandler(enabled = true) {
        onRevertTheme()
        onBack()
    }

    Scaffold(
        modifier = Modifier
            .fillMaxSize()
            .windowInsetsPadding(WindowInsets.displayCutout),
        topBar = {
            TopAppBar(
                title = { Text("Settings — ${tab.title}") },
                navigationIcon = {
                    IconButton(onClick = {
                        onRevertTheme()
                        onBack()
                    }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        }
    ) { pad ->
        Column(modifier = Modifier.fillMaxSize().padding(pad)) {
            ScrollableTabRow(selectedTabIndex = tab.ordinal) {
                SettingsTab.entries.forEach { t ->
                    Tab(
                        selected = tab == t,
                        onClick = { tab = t },
                        text = { Text(t.title) }
                    )
                }
            }
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(14.dp)
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                when (tab) {
                    SettingsTab.NODE -> {
                        OutlinedTextField(
                            value = cfg.callsign,
                            onValueChange = { onUpdateCfg(cfg.copy(callsign = it.uppercase())) },
                            label = { Text("Node Callsign") },
                            modifier = Modifier.fillMaxWidth()
                        )
                        OutlinedTextField(
                            value = cfg.alias,
                            onValueChange = { onUpdateCfg(cfg.copy(alias = it.uppercase())) },
                            label = { Text("Node Alias") },
                            modifier = Modifier.fillMaxWidth()
                        )
                        OutlinedTextField(
                            value = cfg.interfacesFile,
                            onValueChange = { onUpdateCfg(cfg.copy(interfacesFile = it)) },
                            label = { Text("Interfaces File (optional)") },
                            modifier = Modifier.fillMaxWidth()
                        )
                        OutlinedTextField(
                            value = cfg.peersFile,
                            onValueChange = { onUpdateCfg(cfg.copy(peersFile = it)) },
                            label = { Text("Peers File (optional)") },
                            modifier = Modifier.fillMaxWidth()
                        )
                        OutlinedTextField(
                            value = cfg.themesFile,
                            onValueChange = { onUpdateCfg(cfg.copy(themesFile = it)) },
                            label = { Text("Themes File (optional)") },
                            modifier = Modifier.fillMaxWidth()
                        )
                        HorizontalDivider()
                        Text("SD Card Config Backup & Restore", style = MaterialTheme.typography.titleSmall)
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Button(
                                enabled = sdControlsEnabled,
                                onClick = onImportFromSd,
                                modifier = Modifier.weight(1f)
                            ) { Text("Import from SD") }
                            OutlinedButton(
                                enabled = sdControlsEnabled,
                                onClick = onExportToSd,
                                modifier = Modifier.weight(1f)
                            ) { Text("Export to SD") }
                        }
                        if (!sdControlsEnabled) {
                            Card(
                                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.errorContainer),
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Text(
                                    missingPermissionHint,
                                    color = MaterialTheme.colorScheme.onErrorContainer,
                                    style = MaterialTheme.typography.bodySmall,
                                    modifier = Modifier.padding(10.dp)
                                )
                            }
                        } else if (sdStatusMessage.isNotBlank()) {
                            Text(sdStatusMessage, style = MaterialTheme.typography.bodySmall)
                        }
                    }
                    SettingsTab.BBS -> {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Checkbox(
                                checked = cfg.bbsEnabled,
                                onCheckedChange = { onUpdateCfg(cfg.copy(bbsEnabled = it)) }
                            )
                            Text("Enable Built-in BBS (${cfg.bbsAlias}-${cfg.bbsSsid})")
                        }
                        OutlinedTextField(
                            value = cfg.bbsAlias,
                            onValueChange = { onUpdateCfg(cfg.copy(bbsAlias = it.uppercase())) },
                            label = { Text("BBS Alias") },
                            modifier = Modifier.fillMaxWidth()
                        )
                        OutlinedTextField(
                            value = cfg.bbsSsid.toString(),
                            onValueChange = { onUpdateCfg(cfg.copy(bbsSsid = it.toIntOrNull() ?: cfg.bbsSsid)) },
                            label = { Text("BBS SSID (0-15)") },
                            modifier = Modifier.fillMaxWidth()
                        )
                    }
                    SettingsTab.CHAT -> {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Checkbox(
                                checked = cfg.chatEnabled,
                                onCheckedChange = { onUpdateCfg(cfg.copy(chatEnabled = it)) }
                            )
                            Text("Enable Built-in Chat (${cfg.chatAlias}-${cfg.chatSsid})")
                        }
                        OutlinedTextField(
                            value = cfg.chatAlias,
                            onValueChange = { onUpdateCfg(cfg.copy(chatAlias = it.uppercase())) },
                            label = { Text("Chat Alias") },
                            modifier = Modifier.fillMaxWidth()
                        )
                        OutlinedTextField(
                            value = cfg.chatTopic,
                            onValueChange = { onUpdateCfg(cfg.copy(chatTopic = it)) },
                            label = { Text("Chat Lobby Topic") },
                            modifier = Modifier.fillMaxWidth()
                        )
                    }
                    SettingsTab.RIG -> {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Checkbox(
                                checked = cfg.rigEnabled,
                                onCheckedChange = { onUpdateCfg(cfg.copy(rigEnabled = it)) }
                            )
                            Text("Enable RadioFoundry / Hamlib Rig Control")
                        }
                        OutlinedTextField(
                            value = cfg.rigHost,
                            onValueChange = { onUpdateCfg(cfg.copy(rigHost = it)) },
                            label = { Text("Rig Host") },
                            modifier = Modifier.fillMaxWidth()
                        )
                        OutlinedTextField(
                            value = cfg.rigPort.toString(),
                            onValueChange = { onUpdateCfg(cfg.copy(rigPort = it.toIntOrNull() ?: cfg.rigPort)) },
                            label = { Text("Rig Control Port") },
                            modifier = Modifier.fillMaxWidth()
                        )
                    }
                    SettingsTab.PRINTER -> {
                        OutlinedTextField(
                            value = cfg.printerProtocol,
                            onValueChange = { onUpdateCfg(cfg.copy(printerProtocol = it)) },
                            label = { Text("Printer Protocol (generic / mx80 / escp2)") },
                            modifier = Modifier.fillMaxWidth()
                        )
                        OutlinedTextField(
                            value = cfg.printerBaud.toString(),
                            onValueChange = { onUpdateCfg(cfg.copy(printerBaud = it.toIntOrNull() ?: cfg.printerBaud)) },
                            label = { Text("Serial Baud Rate") },
                            modifier = Modifier.fillMaxWidth()
                        )
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Checkbox(
                                checked = cfg.printerCondensed,
                                onCheckedChange = { onUpdateCfg(cfg.copy(printerCondensed = it)) }
                            )
                            Text("Condensed Print Mode (ESC SI)")
                        }
                    }
                    SettingsTab.INTERFACES -> {
                        Button(onClick = onAddIface) { Text("Add Interface...") }
                        cfg.interfaces.forEach { ic ->
                            Card(modifier = Modifier.fillMaxWidth()) {
                                Row(
                                    modifier = Modifier.fillMaxWidth().padding(12.dp),
                                    horizontalArrangement = Arrangement.SpaceBetween,
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Column(modifier = Modifier.weight(1f)) {
                                        Text("${ic.id} (${ic.type})", fontWeight = FontWeight.Bold)
                                        Text("${ic.host}:${ic.port} mask ${ic.netmask} | learned=${ic.learnedPeerPolicy}", style = MaterialTheme.typography.bodySmall)
                                    }
                                    TextButton(onClick = { onEditIface(ic) }) { Text("Edit") }
                                }
                            }
                        }
                    }
                    SettingsTab.PEERS -> {
                        Button(onClick = onAddPeer) { Text("Add Peer...") }
                        cfg.peers.forEach { peer ->
                            Card(modifier = Modifier.fillMaxWidth()) {
                                Row(
                                    modifier = Modifier.fillMaxWidth().padding(12.dp),
                                    horizontalArrangement = Arrangement.SpaceBetween,
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Column(modifier = Modifier.weight(1f)) {
                                        Text("${peer.id}: ${peer.callsign} via ${peer.ifaceId}", fontWeight = FontWeight.Bold)
                                        Text("${peer.host}:${peer.port} (in=${peer.allowInbound}, out=${peer.allowOutbound})", style = MaterialTheme.typography.bodySmall)
                                    }
                                    TextButton(onClick = { onEditPeer(peer) }) { Text("Edit") }
                                }
                            }
                        }
                    }
                    SettingsTab.THEME -> {
                        ThemeSettingsCard(
                            cfg = cfg,
                            systemDark = systemDark,
                            onDismiss = {
                                onRevertTheme()
                                onBack()
                            },
                            onPreview = onPreviewTheme,
                            onSave = { chosenName, customList ->
                                onSaveTheme(chosenName, customList)
                                onBack()
                            }
                        )
                    }
                }
            }
        }
    }
}
