// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

package com.kr4oyk.oyknode

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

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
    bans: List<BanEntryState>,
    usbStatus: String,
    nodeRunning: Boolean,
    acceptIncoming: Boolean,
    onToggleIncoming: () -> Unit,
    onOpenSettings: (SettingsTab) -> Unit,
    onOpenTerminate: () -> Unit,
    onOpenBan: () -> Unit,
    onOpenTheme: () -> Unit,
    onOpenExit: () -> Unit,
    onQuickUsbConnect: () -> Unit
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text("${cfg.callsign} (${cfg.alias})", style = MaterialTheme.typography.titleMedium)
                        Text(
                            "Conn: ${sessions.size} | IN: ${if (acceptIncoming) "ALLOW" else "DENY"} | USB: $usbStatus",
                            style = MaterialTheme.typography.labelSmall
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
                    IconButton(onClick = onOpenTheme) {
                        Icon(Icons.Default.Palette, contentDescription = "Theme")
                    }
                    IconButton(onClick = { onOpenSettings(SettingsTab.NODE) }) {
                        Icon(Icons.Default.Settings, contentDescription = "Settings")
                    }
                    IconButton(onClick = onOpenExit) {
                        Icon(Icons.Default.PowerSettingsNew, contentDescription = "Exit")
                    }
                }
            )
        }
    ) { pad ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(pad)
                .padding(12.dp),
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

            item {
                Card(modifier = Modifier.fillMaxWidth()) {
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

            item {
                Card(modifier = Modifier.fillMaxWidth()) {
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

            item {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(12.dp)) {
                        Text("Status & Event Log", style = MaterialTheme.typography.titleSmall)
                        Spacer(Modifier.height(4.dp))
                        Text("[INFO] Active theme: ${cfg.themeName}", fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
                        Text("[INFO] Config storage: internal filesDir/oyk/node.cfg", fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
                        Text("[INFO] Listening AXUDP on 0.0.0.0:10093", fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
                    }
                }
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
    onUpdateCfg: (NodeConfigState) -> Unit,
    onImportFromSd: () -> Unit,
    onExportToSd: () -> Unit,
    onOpenTheme: () -> Unit,
    onAddIface: () -> Unit,
    onEditIface: (InterfaceConfigState) -> Unit,
    onAddPeer: () -> Unit,
    onEditPeer: (PeerConfigState) -> Unit,
    onBack: () -> Unit
) {
    var tab by remember(initialTab) { mutableStateOf(initialTab) }
    val sdControlsEnabled = storagePermissionState != StoragePermissionState.DECLINED

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Settings — ${tab.title}") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    TextButton(onClick = onOpenTheme) { Text("Theme...") }
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
                    SettingsTab.LOCAL -> {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Checkbox(
                                checked = cfg.bbsEnabled,
                                onCheckedChange = { onUpdateCfg(cfg.copy(bbsEnabled = it)) }
                            )
                            Text("Enable Built-in BBS (${cfg.bbsAlias} -${cfg.bbsSsid})")
                        }
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Checkbox(
                                checked = cfg.chatEnabled,
                                onCheckedChange = { onUpdateCfg(cfg.copy(chatEnabled = it)) }
                            )
                            Text("Enable Built-in Chat (${cfg.chatAlias} -${cfg.chatSsid})")
                        }
                        OutlinedTextField(
                            value = cfg.chatTopic,
                            onValueChange = { onUpdateCfg(cfg.copy(chatTopic = it)) },
                            label = { Text("Chat Lobby Topic") },
                            modifier = Modifier.fillMaxWidth()
                        )
                    }
                    SettingsTab.RADIO -> {
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
                }
            }
        }
    }
}
