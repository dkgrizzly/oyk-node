// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

package com.kr4oyk.oyknode

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog

@Composable
fun AddIfaceWizardCard(
    existingIfaces: List<InterfaceConfigState>,
    initialStep: Int = 1,
    onDismiss: () -> Unit,
    onCommit: (InterfaceConfigState) -> Unit
) {
    val drivers = listOf("axudp", "axip", "ppp", "kiss-serial", "kiss-tcp", "telnet")
    var step by remember { mutableIntStateOf(initialStep.coerceIn(1, 4)) }
    var selectedType by remember { mutableStateOf("axudp") }
    var id by remember { mutableStateOf(deriveDefaultIfaceId(existingIfaces, "axudp")) }
    var description by remember { mutableStateOf("New AXUDP Interface") }
    var host by remember { mutableStateOf("0.0.0.0") }
    var port by remember { mutableStateOf("10094") }
    var netmask by remember { mutableStateOf("255.255.255.0") }
    var device by remember { mutableStateOf("usb0") }
    var allowInbound by remember { mutableStateOf(true) }
    var allowOutbound by remember { mutableStateOf(true) }
    var allowSameIface by remember { mutableStateOf(true) }
    var learnedPolicy by remember { mutableStateOf("reject") }

    Surface(
        shape = RoundedCornerShape(16.dp),
        tonalElevation = 6.dp,
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(modifier = Modifier.padding(18.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("Add Interface", style = MaterialTheme.typography.titleLarge)
                Surface(
                    color = MaterialTheme.colorScheme.primaryContainer,
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text(
                        "Step $step/4",
                        style = MaterialTheme.typography.labelMedium,
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                    )
                }
            }
            Spacer(Modifier.height(6.dp))
            LinearProgressIndicator(
                progress = { step / 4f },
                modifier = Modifier.fillMaxWidth()
            )
            Spacer(Modifier.height(12.dp))

            when (step) {
                1 -> {
                    Text("Choose driver protocol:", style = MaterialTheme.typography.titleSmall)
                    Spacer(Modifier.height(8.dp))
                    drivers.forEach { drv ->
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable {
                                    selectedType = drv
                                    id = deriveDefaultIfaceId(existingIfaces, drv)
                                    allowOutbound = drv != "telnet"
                                    description = "New ${drv.uppercase()} Interface"
                                }
                                .padding(vertical = 4.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            RadioButton(
                                selected = selectedType == drv,
                                onClick = {
                                    selectedType = drv
                                    id = deriveDefaultIfaceId(existingIfaces, drv)
                                    allowOutbound = drv != "telnet"
                                    description = "New ${drv.uppercase()} Interface"
                                }
                            )
                            Text(drv, style = MaterialTheme.typography.bodyMedium)
                        }
                    }
                }
                2 -> {
                    Text("Identity & Transport Endpoint", style = MaterialTheme.typography.titleSmall)
                    Spacer(Modifier.height(8.dp))
                    OutlinedTextField(
                        value = id,
                        onValueChange = { id = it },
                        label = { Text("Interface ID") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth()
                    )
                    Spacer(Modifier.height(8.dp))
                    OutlinedTextField(
                        value = description,
                        onValueChange = { description = it },
                        label = { Text("Description") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth()
                    )
                    Spacer(Modifier.height(8.dp))
                    if (selectedType == "kiss-serial" || selectedType == "ppp") {
                        OutlinedTextField(
                            value = device,
                            onValueChange = { device = it },
                            label = { Text("Serial / USB Device") },
                            singleLine = true,
                            modifier = Modifier.fillMaxWidth()
                        )
                        Spacer(Modifier.height(8.dp))
                    }
                    OutlinedTextField(
                        value = host,
                        onValueChange = { host = it },
                        label = { Text("Bind Host / Peer Host") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth()
                    )
                    if (selectedType == "axudp" || selectedType == "axip") {
                        Spacer(Modifier.height(8.dp))
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            OutlinedTextField(
                                value = port,
                                onValueChange = { port = it },
                                label = { Text("Port") },
                                singleLine = true,
                                modifier = Modifier.weight(1f)
                            )
                            OutlinedTextField(
                                value = netmask,
                                onValueChange = { netmask = it },
                                label = { Text("Netmask") },
                                singleLine = true,
                                modifier = Modifier.weight(1.4f)
                            )
                        }
                    }
                }
                3 -> {
                    Text("Direction, Digipeating & Learned Policy", style = MaterialTheme.typography.titleSmall)
                    Spacer(Modifier.height(8.dp))
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Checkbox(checked = allowInbound, onCheckedChange = { allowInbound = it })
                        Text("Allow Inbound Connections")
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Checkbox(
                            checked = allowOutbound && selectedType != "telnet",
                            enabled = selectedType != "telnet",
                            onCheckedChange = { allowOutbound = it }
                        )
                        Text(if (selectedType == "telnet") "Allow Outbound (Disabled for Telnet)" else "Allow Outbound Connections")
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Checkbox(checked = allowSameIface, onCheckedChange = { allowSameIface = it })
                        Text("Allow Outbound on Same Interface")
                    }
                    Spacer(Modifier.height(8.dp))
                    Text("Learned Peer Policy: $learnedPolicy", style = MaterialTheme.typography.labelLarge)
                }
                else -> {
                    Text("Review Interface Summary", style = MaterialTheme.typography.titleSmall)
                    Spacer(Modifier.height(8.dp))
                    Card(modifier = Modifier.fillMaxWidth()) {
                        Column(modifier = Modifier.padding(12.dp)) {
                            Text("ID: $id ($selectedType)", fontWeight = FontWeight.Bold)
                            Text("Endpoint: $host:$port")
                            Text("Netmask: $netmask")
                            Text("Inbound: $allowInbound | Outbound: $allowOutbound")
                            Text("Same-Iface: $allowSameIface | Learned: $learnedPolicy")
                        }
                    }
                }
            }

            Spacer(Modifier.height(14.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                TextButton(onClick = onDismiss) { Text("Cancel") }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (step > 1) {
                        OutlinedButton(onClick = { step -= 1 }) { Text("Back") }
                    }
                    Button(onClick = {
                        if (step < 4) {
                            step += 1
                        } else {
                            onCommit(
                                InterfaceConfigState(
                                    id = id.ifBlank { "axudp1" },
                                    type = selectedType,
                                    description = description,
                                    host = host,
                                    port = port.toIntOrNull() ?: 10093,
                                    netmask = netmask,
                                    device = device,
                                    allowInbound = allowInbound,
                                    allowOutbound = allowOutbound && selectedType != "telnet",
                                    allowSameInterface = allowSameIface,
                                    learnedPeerPolicy = learnedPolicy,
                                    status = "UP"
                                )
                            )
                        }
                    }) {
                        Text(if (step < 4) "Next" else "Create")
                    }
                }
            }
        }
    }
}

@Composable
fun AddIfaceWizardDialog(
    existingIfaces: List<InterfaceConfigState>,
    initialStep: Int = 1,
    onDismiss: () -> Unit,
    onCommit: (InterfaceConfigState) -> Unit
) {
    Dialog(onDismissRequest = onDismiss) {
        AddIfaceWizardCard(existingIfaces, initialStep, onDismiss, onCommit)
    }
}

fun deriveDefaultIfaceId(existing: List<InterfaceConfigState>, type: String): String {
    val prefix = when (type) {
        "kiss-serial", "kiss-tcp" -> "kiss"
        else -> type
    }
    for (idx in 0 until 64) {
        val cand = "$prefix$idx"
        if (existing.none { it.id.equals(cand, true) }) return cand
    }
    return "${prefix}0"
}

@Composable
fun EditIfaceCard(
    initial: InterfaceConfigState,
    onDismiss: () -> Unit,
    onSave: (InterfaceConfigState) -> Unit
) {
    var description by remember { mutableStateOf(initial.description) }
    var host by remember { mutableStateOf(initial.host) }
    var port by remember { mutableStateOf(initial.port.toString()) }
    var netmask by remember { mutableStateOf(initial.netmask) }
    var allowInbound by remember { mutableStateOf(initial.allowInbound) }
    var allowOutbound by remember { mutableStateOf(initial.allowOutbound) }
    var allowSameIface by remember { mutableStateOf(initial.allowSameInterface) }
    var learnedPolicy by remember { mutableStateOf(initial.learnedPeerPolicy) }

    Surface(shape = RoundedCornerShape(16.dp), tonalElevation = 6.dp) {
        Column(
            modifier = Modifier
                .padding(18.dp)
                .verticalScroll(rememberScrollState())
        ) {
            Text("Edit Interface: ${initial.id} (${initial.type})", style = MaterialTheme.typography.titleLarge)
            Spacer(Modifier.height(10.dp))
            OutlinedTextField(value = description, onValueChange = { description = it }, label = { Text("Description") }, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(8.dp))
            OutlinedTextField(value = host, onValueChange = { host = it }, label = { Text("Bind Host") }, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(value = port, onValueChange = { port = it }, label = { Text("Bind Port") }, modifier = Modifier.weight(1f))
                OutlinedTextField(value = netmask, onValueChange = { netmask = it }, label = { Text("Netmask") }, modifier = Modifier.weight(1.3f))
            }
            Spacer(Modifier.height(8.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = allowInbound, onCheckedChange = { allowInbound = it })
                Text("Allow Inbound")
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(
                    checked = allowOutbound && initial.type != "telnet",
                    enabled = initial.type != "telnet",
                    onCheckedChange = { allowOutbound = it }
                )
                Text("Allow Outbound")
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = allowSameIface, onCheckedChange = { allowSameIface = it })
                Text("Allow Same Interface")
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("Learned Peer Policy", style = MaterialTheme.typography.bodyMedium)
                OutlinedButton(onClick = {
                    learnedPolicy = when (learnedPolicy) {
                        "reject" -> "inbound"
                        "inbound" -> "allow"
                        else -> "reject"
                    }
                }) {
                    Text(learnedPolicy.uppercase())
                }
            }
            Spacer(Modifier.height(12.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                TextButton(onClick = onDismiss) { Text("Cancel") }
                Spacer(Modifier.width(8.dp))
                Button(onClick = {
                    onSave(
                        initial.copy(
                            description = description,
                            host = host,
                            port = port.toIntOrNull() ?: initial.port,
                            netmask = netmask,
                            allowInbound = allowInbound,
                            allowOutbound = allowOutbound && initial.type != "telnet",
                            allowSameInterface = allowSameIface,
                            learnedPeerPolicy = learnedPolicy
                        )
                    )
                }) { Text("Save Interface") }
            }
        }
    }
}

@Composable
fun EditIfaceDialog(
    initial: InterfaceConfigState,
    onDismiss: () -> Unit,
    onSave: (InterfaceConfigState) -> Unit
) {
    Dialog(onDismissRequest = onDismiss) {
        EditIfaceCard(initial, onDismiss, onSave)
    }
}

@Composable
fun EditPeerCard(
    initial: PeerConfigState,
    availableIfaces: List<String>,
    onDismiss: () -> Unit,
    onSave: (PeerConfigState) -> Unit
) {
    var peerId by remember { mutableStateOf(initial.id) }
    var callsign by remember { mutableStateOf(initial.callsign) }
    var desc by remember { mutableStateOf(initial.description) }
    var ifaceId by remember { mutableStateOf(initial.ifaceId.ifBlank { availableIfaces.firstOrNull() ?: "axudp0" }) }
    var host by remember { mutableStateOf(initial.host) }
    var port by remember { mutableStateOf(initial.port.toString()) }
    var allowInbound by remember { mutableStateOf(initial.allowInbound) }
    var allowOutbound by remember { mutableStateOf(initial.allowOutbound) }
    val validHost = PeerValidator.isValidHostOrDmrId(host)

    Surface(shape = RoundedCornerShape(16.dp), tonalElevation = 6.dp) {
        Column(
            modifier = Modifier
                .padding(18.dp)
                .verticalScroll(rememberScrollState())
        ) {
            Text("Edit Peer", style = MaterialTheme.typography.titleLarge)
            Spacer(Modifier.height(8.dp))
            OutlinedTextField(value = peerId, onValueChange = { peerId = it }, label = { Text("Peer Instance ID") }, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(6.dp))
            OutlinedTextField(value = callsign, onValueChange = { callsign = it.uppercase() }, label = { Text("Callsign") }, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(6.dp))
            OutlinedTextField(value = desc, onValueChange = { desc = it }, label = { Text("Description") }, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(6.dp))
            OutlinedTextField(value = ifaceId, onValueChange = { ifaceId = it }, label = { Text("Interface") }, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(6.dp))
            OutlinedTextField(
                value = host,
                onValueChange = { host = it },
                isError = !validHost,
                label = { Text("Host / IPv4 / 24-bit DMR ID") },
                modifier = Modifier.fillMaxWidth()
            )
            Spacer(Modifier.height(6.dp))
            OutlinedTextField(value = port, onValueChange = { port = it }, label = { Text("Port") }, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(6.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = allowInbound, onCheckedChange = { allowInbound = it })
                Text("Allow Inbound")
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = allowOutbound, onCheckedChange = { allowOutbound = it })
                Text("Allow Outbound")
            }
            Spacer(Modifier.height(10.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                TextButton(onClick = onDismiss) { Text("Cancel") }
                Spacer(Modifier.width(8.dp))
                Button(
                    enabled = validHost && callsign.isNotBlank(),
                    onClick = {
                        onSave(
                            initial.copy(
                                id = peerId.ifBlank { callsign.lowercase() },
                                callsign = callsign,
                                description = desc,
                                ifaceId = ifaceId,
                                host = host.trim(),
                                port = port.toIntOrNull() ?: 10093,
                                allowInbound = allowInbound,
                                allowOutbound = allowOutbound,
                                autoLearned = false
                            )
                        )
                    }
                ) { Text("Save Peer") }
            }
        }
    }
}

@Composable
fun EditPeerDialog(
    initial: PeerConfigState,
    availableIfaces: List<String>,
    onDismiss: () -> Unit,
    onSave: (PeerConfigState) -> Unit
) {
    Dialog(onDismissRequest = onDismiss) {
        EditPeerCard(initial, availableIfaces, onDismiss, onSave)
    }
}

@Composable
fun TerminateCard(
    sessions: List<SessionState>,
    onDismiss: () -> Unit,
    onTerminate: (Long) -> Unit
) {
    var selectedId by remember { mutableStateOf(sessions.firstOrNull()?.id ?: 0L) }
    Surface(shape = RoundedCornerShape(16.dp), tonalElevation = 6.dp) {
        Column(modifier = Modifier.padding(18.dp)) {
            Text("Terminate Active Connection", style = MaterialTheme.typography.titleLarge)
            Spacer(Modifier.height(10.dp))
            if (sessions.isEmpty()) {
                Text("No active connections.", style = MaterialTheme.typography.bodyMedium)
            } else {
                LazyColumn(modifier = Modifier.heightIn(max = 220.dp)) {
                    items(sessions) { s ->
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { selectedId = s.id }
                                .padding(vertical = 6.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            RadioButton(selected = selectedId == s.id, onClick = { selectedId = s.id })
                            Column {
                                Text("#${s.id}  ${s.callsign}  (${s.ifaceId})", fontWeight = FontWeight.SemiBold)
                                Text("${s.sinceHms} - ${s.role}", style = MaterialTheme.typography.bodySmall)
                            }
                        }
                    }
                }
            }
            Spacer(Modifier.height(12.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                TextButton(onClick = onDismiss) { Text("Close") }
                Spacer(Modifier.width(8.dp))
                Button(
                    enabled = selectedId > 0L,
                    onClick = { onTerminate(selectedId) },
                    colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error)
                ) { Text("Terminate Selected") }
            }
        }
    }
}

@Composable
fun TerminateDialog(
    sessions: List<SessionState>,
    onDismiss: () -> Unit,
    onTerminate: (Long) -> Unit
) {
    Dialog(onDismissRequest = onDismiss) {
        TerminateCard(sessions, onDismiss, onTerminate)
    }
}

@Composable
fun BanCard(
    bans: List<BanEntryState>,
    onDismiss: () -> Unit,
    onBan: (BanEntryState) -> Unit,
    onUnban: (String) -> Unit
) {
    var callsign by remember { mutableStateOf("B4NTST") }
    var liftSpec by remember { mutableStateOf("permanent") }
    var reason by remember { mutableStateOf("Banned by sysop.") }
    var hardBan by remember { mutableStateOf(false) }
    var appealAllowed by remember { mutableStateOf(true) }

    Surface(shape = RoundedCornerShape(16.dp), tonalElevation = 6.dp) {
        Column(
            modifier = Modifier
                .padding(18.dp)
                .verticalScroll(rememberScrollState())
        ) {
            Text("Ban / Unban Callsign (banlist.cfg)", style = MaterialTheme.typography.titleLarge)
            Spacer(Modifier.height(8.dp))
            OutlinedTextField(value = callsign, onValueChange = { callsign = it.uppercase() }, label = { Text("Callsign") }, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(6.dp))
            OutlinedTextField(value = liftSpec, onValueChange = { liftSpec = it }, label = { Text("Lift At (permanent / 12h / YYYY-MM-DD)") }, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(6.dp))
            OutlinedTextField(value = reason, onValueChange = { reason = it }, label = { Text("Reason") }, modifier = Modifier.fillMaxWidth())
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = hardBan, onCheckedChange = { hardBan = it })
                Text("Hard Ban")
                Spacer(Modifier.width(12.dp))
                Checkbox(checked = appealAllowed, onCheckedChange = { appealAllowed = it })
                Text("Appeal Allowed")
            }
            Spacer(Modifier.height(8.dp))
            Text("Active Bans (${bans.size})", style = MaterialTheme.typography.titleSmall)
            bans.forEach { b ->
                Row(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text("${b.callsign} [${if (b.hardBan) "HARD" else "SOFT"}] (${b.liftAt})", fontWeight = FontWeight.Medium)
                        Text(b.reason, style = MaterialTheme.typography.bodySmall)
                    }
                    TextButton(onClick = { onUnban(b.callsign) }) { Text("Unban") }
                }
            }
            Spacer(Modifier.height(10.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                TextButton(onClick = onDismiss) { Text("Close") }
                Spacer(Modifier.width(8.dp))
                Button(
                    enabled = callsign.isNotBlank(),
                    onClick = {
                        onBan(BanEntryState(callsign, hardBan, appealAllowed, liftSpec, reason))
                    }
                ) { Text("Ban Callsign") }
            }
        }
    }
}

@Composable
fun BanDialog(
    bans: List<BanEntryState>,
    onDismiss: () -> Unit,
    onBan: (BanEntryState) -> Unit,
    onUnban: (String) -> Unit
) {
    Dialog(onDismissRequest = onDismiss) {
        BanCard(bans, onDismiss, onBan, onUnban)
    }
}

@Composable
fun ThemeSettingsCard(
    cfg: NodeConfigState,
    systemDark: Boolean,
    onDismiss: () -> Unit,
    onSave: (selectedTheme: String, updatedCustomThemes: List<ThemeConfigState>) -> Unit,
    onPreview: (selectedTheme: String, updatedCustomThemes: List<ThemeConfigState>) -> Unit = { _, _ -> }
) {
    var selectedName by remember { mutableStateOf(cfg.themeName.ifBlank { "native" }) }
    var customs by remember { mutableStateOf(cfg.customThemes) }
    val allNames = remember(customs) {
        listOf("native", "dark", "light") + customs.map { it.name }
    }
    var selectedRole by remember { mutableStateOf(ThemeRole.BG) }
    LaunchedEffect(selectedName, customs) {
        onPreview(selectedName, customs)
    }
    val activePalette = remember(selectedName, customs, systemDark) {
        when (selectedName.lowercase()) {
            "native" -> BuiltinThemes.builtinPalette("native", systemDark)
            "dark" -> BuiltinThemes.builtinPalette("dark", true)
            "light" -> BuiltinThemes.builtinPalette("light", false)
            else -> customs.firstOrNull { it.name.equals(selectedName, true) }
                ?: BuiltinThemes.builtinPalette("native", systemDark)
        }
    }
    var hexInput by remember(selectedName, selectedRole) {
        mutableStateOf(activePalette.colors[selectedRole] ?: "#10141A")
    }

    Surface(shape = RoundedCornerShape(16.dp), tonalElevation = 6.dp) {
        Column(modifier = Modifier.padding(18.dp)) {
            Text("Theme Settings", style = MaterialTheme.typography.titleLarge)
            Text(
                "Built-in native/dark/light are never written; editing clones custom-dark or custom-light.",
                style = MaterialTheme.typography.bodySmall
            )
            Spacer(Modifier.height(8.dp))
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                allNames.take(4).forEach { nm ->
                    FilterChip(
                        selected = selectedName.equals(nm, true),
                        onClick = { selectedName = nm },
                        label = { Text(nm, maxLines = 1, softWrap = false) }
                    )
                }
            }
            Spacer(Modifier.height(8.dp))
            LazyColumn(modifier = Modifier.heightIn(max = 220.dp)) {
                items(ThemeRole.entries) { role ->
                    val colHex = activePalette.colors[role] ?: "#10141A"
                    val swatch = parseComposeColor(colHex, Color.Black)
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable {
                                selectedRole = role
                                hexInput = colHex
                            }
                            .padding(vertical = 4.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            "${role.label} (${role.key})",
                            fontWeight = if (selectedRole == role) FontWeight.Bold else FontWeight.Normal,
                            modifier = Modifier.weight(1f)
                        )
                        Text(colHex, fontFamily = FontFamily.Monospace, modifier = Modifier.padding(end = 8.dp))
                        Box(
                            modifier = Modifier
                                .size(28.dp, 18.dp)
                                .background(swatch, RoundedCornerShape(4.dp))
                                .border(1.dp, MaterialTheme.colorScheme.outline, RoundedCornerShape(4.dp))
                        )
                    }
                }
            }
            Spacer(Modifier.height(8.dp))
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedTextField(
                    value = hexInput,
                    onValueChange = { hexInput = it },
                    label = { Text("Color #RRGGBB (${selectedRole.key})") },
                    singleLine = true,
                    modifier = Modifier.weight(1f)
                )
                Button(
                    enabled = BuiltinThemes.isValidHexColor(hexInput),
                    onClick = {
                        val norm = BuiltinThemes.normalizeHexColor(hexInput)
                        if (BuiltinThemes.isBuiltin(selectedName)) {
                            val cloneDark = if (selectedName.equals("light", true)) false else activePalette.isDarkBase
                            val cloneName = BuiltinThemes.makeCustomCloneName(customs, cloneDark)
                            val newMap = activePalette.colors.toMutableMap().apply { put(selectedRole, norm) }
                            customs = customs + ThemeConfigState(cloneName, cloneDark, newMap)
                            selectedName = cloneName
                        } else {
                            customs = customs.map { th ->
                                if (th.name.equals(selectedName, true)) {
                                    th.copy(colors = th.colors.toMutableMap().apply { put(selectedRole, norm) })
                                } else th
                            }
                        }
                    }
                ) { Text("Apply Color") }
            }
            Spacer(Modifier.height(12.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                TextButton(onClick = onDismiss) { Text("Cancel") }
                Spacer(Modifier.width(8.dp))
                Button(onClick = { onSave(selectedName, customs) }) { Text("Save & Apply") }
            }
        }
    }
}

@Composable
fun ThemeSettingsDialog(
    cfg: NodeConfigState,
    systemDark: Boolean,
    onDismiss: () -> Unit,
    onSave: (selectedTheme: String, updatedCustomThemes: List<ThemeConfigState>) -> Unit,
    onPreview: (selectedTheme: String, updatedCustomThemes: List<ThemeConfigState>) -> Unit = { _, _ -> }
) {
    Dialog(onDismissRequest = onDismiss) {
        ThemeSettingsCard(cfg, systemDark, onDismiss, onSave, onPreview)
    }
}

@Composable
fun ExitCard(
    autoLearnedCount: Int,
    onDismiss: () -> Unit,
    onSaveAndExit: () -> Unit,
    onDiscardAndExit: () -> Unit,
    onReviewPeers: () -> Unit
) {
    Surface(shape = RoundedCornerShape(16.dp), tonalElevation = 6.dp) {
        Column(modifier = Modifier.padding(20.dp)) {
            Text(
                if (autoLearnedCount > 0) "Auto-Learned Peers ($autoLearnedCount)" else "Exit OYK Packet Node",
                style = MaterialTheme.typography.titleLarge
            )
            Spacer(Modifier.height(10.dp))
            Text(
                if (autoLearnedCount > 0) {
                    "$autoLearnedCount auto-learned peer(s) were added during this session. " +
                        "Save them to your peers configuration, discard them, or review in Peers?"
                } else {
                    "Shut down OYK Packet Node and disconnect all active sessions?"
                },
                style = MaterialTheme.typography.bodyMedium
            )
            Spacer(Modifier.height(14.dp))
            if (autoLearnedCount > 0) {
                Column(modifier = Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = onSaveAndExit,
                        modifier = Modifier.fillMaxWidth()
                    ) { Text("Save & Exit", maxLines = 1, softWrap = false) }
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        OutlinedButton(onClick = onReviewPeers) { Text("Review", maxLines = 1, softWrap = false) }
                        TextButton(onClick = onDiscardAndExit) { Text("Discard", maxLines = 1, softWrap = false) }
                        TextButton(onClick = onDismiss) { Text("Cancel", maxLines = 1, softWrap = false) }
                    }
                }
            } else {
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                    TextButton(onClick = onDismiss) { Text("Cancel") }
                    Spacer(Modifier.width(8.dp))
                    Button(onClick = onDiscardAndExit) { Text("Shutdown") }
                }
            }
        }
    }
}

@Composable
fun ExitDialog(
    autoLearnedCount: Int,
    onDismiss: () -> Unit,
    onSaveAndExit: () -> Unit,
    onDiscardAndExit: () -> Unit,
    onReviewPeers: () -> Unit
) {
    Dialog(onDismissRequest = onDismiss) {
        ExitCard(autoLearnedCount, onDismiss, onSaveAndExit, onDiscardAndExit, onReviewPeers)
    }
}

@Composable
fun TerminalCard(
    remoteLabel: String,
    lines: List<String>,
    inputText: String,
    onSend: () -> Unit,
    onBack: () -> Unit
) {
    Surface(shape = RoundedCornerShape(16.dp), tonalElevation = 6.dp) {
        Column(modifier = Modifier.fillMaxWidth().padding(16.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column {
                    Text("Local Packet Terminal", style = MaterialTheme.typography.titleMedium)
                    Text("Session: $remoteLabel", style = MaterialTheme.typography.labelSmall)
                }
                TextButton(onClick = onBack) { Text("Console") }
            }
            Spacer(Modifier.height(8.dp))
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(Color(0xFF0F172A), RoundedCornerShape(8.dp))
                    .border(1.dp, MaterialTheme.colorScheme.outline, RoundedCornerShape(8.dp))
                    .padding(10.dp)
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    lines.forEach { ln ->
                        Text(
                            ln,
                            color = Color(0xFFE2E8F0),
                            fontFamily = FontFamily.Monospace,
                            softWrap = true,
                            style = MaterialTheme.typography.bodySmall
                        )
                    }
                }
            }
            Spacer(Modifier.height(8.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(
                    value = inputText,
                    onValueChange = {},
                    label = { Text("Command") },
                    singleLine = true,
                    modifier = Modifier.weight(1f)
                )
                Button(onClick = onSend, modifier = Modifier.align(Alignment.CenterVertically)) {
                    Text("Send")
                }
            }
        }
    }
}

@Composable
fun FilesCard(
    incomingFiles: List<Triple<String, String, String>>,
    approvedFiles: List<Pair<String, String>>,
    onDismiss: () -> Unit
) {
    Surface(shape = RoundedCornerShape(16.dp), tonalElevation = 6.dp) {
        Column(modifier = Modifier.fillMaxWidth().padding(16.dp)) {
            Text("Manage BBS Files Area", style = MaterialTheme.typography.titleLarge)
            Spacer(Modifier.height(6.dp))
            Text(
                "Pending Incoming (${incomingFiles.size})  |  Approved (${approvedFiles.size})",
                style = MaterialTheme.typography.labelMedium
            )
            Spacer(Modifier.height(8.dp))
            Text("Pending Incoming Uploads", style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(4.dp))
            incomingFiles.forEach { (uploader, name, size) ->
                Row(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("$uploader / $name", fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
                    Text(size, style = MaterialTheme.typography.bodySmall)
                }
            }
            Spacer(Modifier.height(8.dp))
            HorizontalDivider()
            Spacer(Modifier.height(8.dp))
            Text("Approved BBS Files", style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(4.dp))
            approvedFiles.forEach { (rel, size) ->
                Row(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("/$rel", fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
                    Text(size, style = MaterialTheme.typography.bodySmall)
                }
            }
            Spacer(Modifier.height(12.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                Button(onClick = onDismiss) { Text("Close") }
            }
        }
    }
}

@Composable
fun NodeMenuCard(
    acceptIncoming: Boolean,
    onAction: (String) -> Unit,
    onDismiss: () -> Unit
) {
    val entries = listOf(
        "Allow Incoming Connections",
        "Deny Incoming Connections",
        "Local Terminal...",
        "Manage Files Area...",
        "Radio Control...",
        "Settings...",
        "Peers...",
        "Theme...",
        "Terminate Connection...",
        "Ban / Unban Callsign...",
        "About OYK-Node...",
        "Exit"
    )
    Surface(shape = RoundedCornerShape(16.dp), tonalElevation = 6.dp) {
        Column(modifier = Modifier.fillMaxWidth().padding(16.dp)) {
            Text(
                "Node Menu (${if (acceptIncoming) "INCOMING: ALLOW" else "INCOMING: DENY"})",
                style = MaterialTheme.typography.titleLarge
            )
            Spacer(Modifier.height(8.dp))
            entries.forEachIndexed { idx, label ->
                if (idx == 2 || idx == 11) {
                    HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
                }
                TextButton(
                    onClick = { onAction(label) },
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(label, modifier = Modifier.fillMaxWidth())
                }
            }
            Spacer(Modifier.height(6.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                OutlinedButton(onClick = onDismiss) { Text("Close") }
            }
        }
    }
}

@Composable
fun AboutCard(
    callsign: String,
    alias: String,
    onDismiss: () -> Unit
) {
    Surface(shape = RoundedCornerShape(16.dp), tonalElevation = 6.dp) {
        Column(modifier = Modifier.fillMaxWidth().padding(18.dp)) {
            Text("About OYK Amateur Radio Packet Node", style = MaterialTheme.typography.titleLarge)
            Spacer(Modifier.height(8.dp))
            Text("Station: $callsign ($alias)", style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(6.dp))
            Text(
                "OYK Amateur Radio Packet Node (OYK-Node)\n" +
                    "Copyright (C) 2026 KR4OYK - David Kuder\n" +
                    "License: GPL-3.0-or-later\n" +
                    "AX.25 v2.2 / KISS / AXUDP / AXIP / Telnet / YAPP / Hamlib",
                style = MaterialTheme.typography.bodyMedium
            )
            Spacer(Modifier.height(14.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                Button(onClick = onDismiss) { Text("OK") }
            }
        }
    }
}
