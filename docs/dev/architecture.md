<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Architecture Overview

## Layered Design

`oyk-node` separates protocol framing, transport drivers, node session management, and operator UIs into cleanly decoupled modules:

```
+-------------------------------------------------------------------+
|  Local Operator UIs                                               |
|  - NcursesUI (src/ui/ncurses_ui.cpp)                              |
|  - Gtk2UI    (src/ui/gtk2_ui.cpp)                                 |
|  - DOS Video RAM UI (ports/dos/dos_main.cpp)                      |
|  - Win16/Win32/Win64 Native GUI (ports/windows/win_main.cpp)      |
+-------------------------------------------------------------------+
                                | polls immutable NodeSnapshot
                                v
+-------------------------------------------------------------------+
|  Node Engine (include/oyk/node/, src/node/)                       |
|  - NodeEngine: sessions, bridging, MHEARD, last-5-new, controls   |
|  - Session: AX.25 v2.0 modulo-8 L2 state machine                  |
|  - CommandHandler: node shell + external app command hooks        |
|  - BanList: banlist.cfg persistence, flags, appeal sanitization   |
|  - Logger: severity bitmask routing, consolelevel, daily rotation |
+-------------------------------------------------------------------+
                                | RadioInterface + InterfaceRegistry
                                v
+-------------------------------------------------------------------+
|  Radio / Transport Drivers (include/oyk/radio/, src/radio/)       |
|  - AxudpInterface       ("axudp")                                 |
|  - AxipInterface        ("axip", IP protocol 93)                  |
|  - KissSerialInterface  ("kiss-serial")                           |
|  - KissTcpInterface     ("kiss-tcp", client & server modes)       |
|  - TelnetInterface      ("telnet", incoming-only synthetic L2)    |
+-------------------------------------------------------------------+
                                |
                                v
+-------------------------------------------------------------------+
|  AX.25 & Core Utilities (include/oyk/ax25/, include/oyk/core/)    |
|  - Callsign, Frame, CRC-16-CCITT FCS, KissDecoder                 |
|  - LineEnding (CRLF / CR / LF config I/O preservation)            |
+-------------------------------------------------------------------+
```

## Threading & Lock Discipline (C++20 Core)

- Each `RadioInterface` runs its own non-blocking `poll()` I/O thread with a self-pipe wake mechanism so `stop()` returns immediately without hanging on socket or serial timeouts.
- `NodeEngine` runs a 100ms timer thread handling AX.25 `T1`/`N2` retries, idle timeouts, periodic UI ID beacons, and daily log rotation checks.
- **Deadlock Prevention**:
  - `state_mutex_` protects sessions, heard tables, ban list, and interface registry pointers.
  - All outbound frame transmissions (`PendingTx`) and shell command executions (`PendingCommand`) are queued while holding `state_mutex_`, then dispatched **after releasing `state_mutex_`**.
  - UI threads (`NcursesUI`, `Gtk2UI`) only call `NodeEngine::snapshot()`, which returns a value-copied `NodeSnapshot` so rendering never holds locks.
  - `TelnetInterface` queues synthetic `RR`/`UA`/`DM` replies onto a deferred queue delivered on its own I/O thread, preventing synchronous re-entry from `send_frame()` back into `NodeEngine::on_radio_frame()`.

## Future Standalone BBS & Chat Integration

`CommandHandler::register_external_command(name, description, handler)` provides the extension point for launching or bridging future standalone BBS and Chat executables without modifying `NodeEngine`, `Session`, or any UI layer.
