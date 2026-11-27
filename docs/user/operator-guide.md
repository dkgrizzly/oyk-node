<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Operator Guide

## Local Sysop Interface

All local operator consoles — **Linux `ncurses`**, **X11 `GTK+ 2.0`**, **Windows 16/32/64-bit**, **macOS AppKit**, **Android Compose**, **Teensy 4.1 TinyTMUX**, **Raspberry Pi Pico**, **MS-DOS Text & Graphical**, **Classic Macintosh**, **Apple IIGS GS/OS**, **Amiga Workbench**, and **Atari ST GEM** — share consistent workflows driven by shared schemas and state controllers (see the [Port Feature Matrix](feature-matrix.md) and [UI Gallery](ui-gallery.md)):

1. **Top Menu Bar (`Node`)**: Consistent across every platform (`F10` / `M` / `Enter` in text/TUI builds, or click `Node` in GUI builds), exposing **Allow/Deny Incoming**, **Node Settings** (7 canonical tabs: `1:Node`, `2:BBS`, `3:CHAT`, `4:Ifaces`, `5:Peers`, `6:Rig`, `7:Prn`), **Theme Settings** (OS-native default with whole-screen live preview, locked on Amiga `< 32`-color screens), **Local Terminal...**, **Approve Incoming Files...**, **Terminate Connection...**, **Ban / Unban Callsign...**, and **Exit**.
   - **Local Terminal (`Ctrl+A` Escape Prefix + YAPP/YAPP-C)**: Uses Minicom/screen-style `Ctrl+A` hotkeys (`Ctrl+A Z` / `Ctrl+A H` / `?` for hotkey help overlay, `Ctrl+A C` connect, `Ctrl+A L` local node, `Ctrl+A S` YAPP send, `Ctrl+A E` cycle EOL, `Ctrl+A W` word-wrap) so remote slash commands (e.g. `/q` in Packet CHAT) never conflict with local controls. Press `Ctrl+A Q` or `Tab`/`F9` to switch back to the console without closing the session.
   - **Approve Incoming Files...** (`F`): Reviews uploads in `incoming/<CALLSIGN-SSID>/<filename>` and lets the sysop approve/move them into `filespath` or delete/reject them.
   - **Terminate Connection...** (`T`): Select and disconnect an active session by `#ID` (sends AX.25 `DISC` and logs at `KICK` severity).
   - **Ban / Unban Callsign...** (`B`): Manage soft/hard bans, lift duration, reason, and appeal eligibility persisted to `banlist.cfg`.
   - **Exit** (`X` / `Q`): Prompts to review/save auto-learned peers when present, sends `73` sign-off, disconnects sessions, stops radio interfaces, and flushes logs.
2. **Header Bar**: Node callsign/alias, uptime, total active connections, and the **Incoming Policy Badge** (`[INCOMING: ALLOW]` or `[INCOMING: DENY]`).
3. **Radio Interfaces Pane**: Lists every configured interface with its type, status, **active connection count**, and RX/TX frame counters.
4. **Last 5 New Callsigns Heard Pane**: Displays the 5 most recently discovered distinct callsigns (newest first) along with port and first-heard time. Repeated frames from an already-heard callsign do not displace newer callsigns.
5. **Active Connections Pane**: Lists every live session (`#ID`, callsign, interface, direction, L2 state, connect time, and role: `NODE SHELL`, `<-> #ID` bridged, or `BAN NOTICE`).
6. **Node Activity Log & Status Line**: Live tailing log filtered by `consolelevel` (defaults to everything except `DEBUG`), with a single status message line along the bottom.

---

## Ban Behavior & Appeals Workflow

### Initial Notification (All Bans)
When a banned callsign connects for the first time (`NOTIFIED` bit clear), the node admits them to a **restricted session** where normal node commands are disabled:
- The user sees their ban type (`SOFT BAN` or `HARD BAN`), ban date, lift date (or `permanent`), and reason.
- The `NOTIFIED` flag (`0b0010`) is immediately set and saved to `banlist.cfg`.
- If `APPEAL_ALLOWED` (`0b0001`) is set and `APPEAL_USED` (`0b0100`) is clear, the user may submit **one** short appeal message (or type `QUIT` to disconnect without using their appeal).
- If appeals are not allowed, the node disconnects them immediately after delivering the notice.

### Appeal Sanitization & Logging
When a user submits an appeal:
- All control characters and non-ASCII bytes are stripped; only base printable ASCII (`0x20`–`0x7E`) is kept, trimmed, and capped at **140 characters**.
- The `APPEAL_USED` flag (`0b0100`) is set and persisted to `banlist.cfg`.
- Only `<CALLSIGN> <YYYYMMDD-HHMMSS> <appeal_text>` is written as a single line to `appeals.log` (under `logpath`) and displayed on the local sysop console.
- The restricted session is then disconnected.

### Subsequent Connection Attempts
- **Hard Ban (`0b1000` set)**: Once `NOTIFIED` is set, all future `SABM` attempts are immediately rejected with `DM`.
- **Soft Ban (`0b1000` clear)**: Permitted **once per calendar day** to connect to a restricted session to check their ban status (and submit their appeal if allowed and unused). Additional attempts on the same day are rejected with `DM`.

---

## Remote Packet Node Commands

Stations connected to the normal node shell can issue:

| Command | Alias | Description |
| --- | --- | --- |
| `HELP` | `?`, `H` | List available commands (and any future registered apps such as BBS/Chat) |
| `INFO` | `I` | Display node callsign, alias, description, uptime, and connection count |
| `PORTS` | `P` | List radio interfaces, types, status, and active connection counts |
| `USERS` | `U` | List currently active sessions |
| `MHEARD` | `MH` | Display recently heard callsigns, port, packet counts, and last-heard times |
| `BBS` | — | Enter the built-in Packet Bulletin Board System (also reachable directly on SSID 11 / `OYKBBS`) |
| `CHAT` | `CH` | Join the built-in multi-user Packet CHAT room (also reachable directly on SSID 12 / `OYKCHT`) |
| `CONNECT <port> <call>` | `C` | Initiate an outbound AX.25 connection through `<port>` (index or ID) and bridge transparently |
| `BYE` | `B`, `QUIT`, `Q` | Cleanly disconnect from the node |
