# oyk-node Project Specification

This document consolidates the functional, protocol, configuration, logging, ban/appeal, UI, and portability specifications of **oyk-node**.

---

## 1. Scope & Core Packet Node Requirements

- Modular C++ Amateur Radio AX.25 v2.0 packet node with pluggable radio/transport interfaces.
- Minimum remote packet node command set: `HELP`/`?`, `INFO`, `PORTS`, `USERS`, `MHEARD`, cross-port `CONNECT <port> <callsign>`, and `BYE`/`QUIT`.
- Standalone BBS and Chat services will be implemented later as separate executables integrated via external command hooks.

## 2. Radio & Transport Interfaces

1. **AXUDP (`axudp`)**: Raw AX.25 frames + 2-byte little-endian CRC-16-CCITT (X.25) FCS over IPv4 UDP; static peer routes, broadcast peers, and dynamic peer auto-learning.
2. **AXIP (`axip`)**: Raw AX.25 frames + 2-byte little-endian CRC-16-CCITT FCS encapsulated directly in IPv4 protocol `93` per **RFC 1226**.
3. **KISS Serial (`kiss-serial`)**: KISS framing (`FEND=0xC0`, `FESC=0xDB`, `TFEND=0xDC`, `TFESC=0xDD`) over raw 8N1 serial TTY/PTY with configurable port nibble (`0`–`15`) and optional TNC parameters.
4. **KISS TCP/IP (`kiss-tcp`)**: Same KISS framing over TCP/IP supporting both `client` (auto-reconnecting, e.g. Direwolf) and `server` modes.
5. **Telnet (`telnet`, incoming-only)**: Incoming TCP console with minimal RFC 854/858 IAC negotiation and callsign login, bridged into the AX.25 L2 session model. Outbound `CONNECT` onto a `telnet` port is rejected (`DM`).

## 3. Configuration Files & Line-Ending Rules

- **Search Order** (when `-c`/`--config` is omitted):
  1. `~/.oyk/node.cfg`
  2. `/etc/oyk/node.cfg`
  3. `./node.cfg`
- **Banlist Path**: Defaults to `banlist.cfg` in the same directory as the loaded `node.cfg`; overridable via `banlist=` in `[node]`.
- **Line Endings**:
  - Must accept `CRLF`, `CR`, and `LF` when reading config files on all platforms.
  - Must write `CRLF` by default on MS-DOS and Windows, and `LF` by default on POSIX.
  - Must write config files back to disk using the same line ending format they had when read.
  - The **first line ending from `node.cfg`** takes precedence and overrides any mixed line endings found.
  - Config file line endings MUST NOT alter wire protocol line endings (e.g. AX.25 `\r`, Telnet NVT `\r\n`).

## 4. Severity Bitmask Logging & Daily Rotation

- `logpath=` in `[node]` (default `logs/`) specifies the directory for all log files.
- Severity bitmask constants:
  - `0x00` = `SILENT`
  - `0x01` = `ERROR`
  - `0x02` = `WARNING`
  - `0x04` = `INFO`
  - `0x10` = `KICK`
  - `0x20` = `BAN`
  - `0x40` = `APPEAL`
  - `0x80` = `DEBUG`
- `consolelevel=` in `[node]` configures local console filtering; defaults to all levels except `DEBUG` (`0x77`).
- `[logfiles]` maps filenames under `logpath` to severity bitmasks; `node.log` defaults to `ERROR|WARNING|INFO|KICK|BAN` (`0x37`) and `appeals.log` defaults to `APPEAL` (`0x40`).
- Log files rotate daily automatically (`<filename>.YYYYMMDD` on POSIX/Windows, 8.3-safe `MMDD<3ch>.LOG` on MS-DOS).

## 5. Ban List & Restricted Appeal Sessions

- `banlist.cfg` entry syntax:
  `CALLSIGN[-SSID]=YYYYMMDD-HHMMSS,YYYYMMDD-HHMMSS|permanent,flags,reason`
- Example:
  `B4DCAL=20261102-130105,20271102-130105,0b0000,Banned for abusive language.`
- `flags` accepts `0b...` binary, `0x...` hex, or unprefixed decimal:
  - Bit 0 (`0b0001`): Appeal allowed
  - Bit 1 (`0b0010`): User has been notified of the ban
  - Bit 2 (`0b0100`): User has already used their appeal
  - Bit 3 (`0b1000`): Hard ban
- **Unnotified banned callsign**: Allowed into a restricted session displaying ban details and reason; if appeal-allowed is set, may submit one appeal; then disconnected. `NOTIFIED` (and `APPEAL_USED` if submitted) are persisted to `banlist.cfg`.
- **Appeal record**: Sanitized to base printable ASCII (`0x20`–`0x7E`, no control characters), max **140 characters**. Only `<CALLSIGN> <YYYYMMDD-HHMMSS> <appeal_text>` is written to a new line in `appeals.log` and displayed on the local sysop console.
- **Soft-banned callsign** (`!HARD_BAN`): Allowed once per calendar day to connect into a restricted session to check ban status and submit an appeal if allowed and unused.
- **Hard-banned callsign** (`HARD_BAN`): After initial notification (`NOTIFIED`), no further connections are permitted (`DM`).

## 6. Local Operator UIs & Platform Portability

- Every local operator UI displays:
  - Active connection count on each radio interface
  - Last 5 new callsigns heard
  - Active connections & recent console-filtered activity log
  - Menu system to: **Allow / Deny** new incoming connections, **Terminate** a specific connection, **Ban / Unban** a callsign, and **Exit** cleanly.
- **Targets**:
  - Linux/POSIX: `ncurses` TUI (`oyk-node`) + automatic X11 `libgtk-2.0` GUI (`oyk-node-gtk` built whenever `libgtk2.0-dev` is installed).
  - MS-DOS *(opt-in)*: IBM XT (8088 / NEC V20), 640KB RAM, optional LIM EMS 4.0 (`INT 67h`), OpenWatcom v2/1.9 + Michael Brutman's mTCP, headless `qemu-system-i386` test runner.
  - Windows *(opt-in)*: Native WinAPI + Winsock 1.1 GUIs for 16-bit Windows 3.1, 32-bit Windows 95–ME & Windows NT 3.51–11, and 64-bit Windows XP x64–11.
