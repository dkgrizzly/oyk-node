<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# oyk-node

A simple, modular Amateur Radio AX.25 Packet Node written in modern C++20 with an `ncurses` local operator console.

## Features

- **AX.25 v2.0 Layer 2 Engine**: Modulo-8 connected-mode sessions (`SABM`, `UA`, `DM`, `DISC`, `I`, `RR`, `RNR`, `REJ`, `UI`), windowing, `T1`/`N2` retransmission timers, idle timeout, and CRC-16-CCITT (X.25) FCS.
- **Modular Radio Interface Framework**: Abstract `oyk::radio::RadioInterface` + `InterfaceRegistry` factory pattern with built-in drivers:
  - **`axudp`**: AXUDP over UDP with CRC-16-CCITT FCS, static/broadcast peers, and dynamic peer auto-learning (compatible with LinBPQ, BPQ32, Direwolf, JNOS, `ax25ipd`).
  - **`axip`**: AXIP (RFC 1226, IP protocol 93) raw IPv4 encapsulation with CRC-16-CCITT FCS and peer auto-learning. Requires root or `CAP_NET_RAW` (`sudo setcap cap_net_raw+ep ./build/oyk-node`).
  - **`kiss-serial`**: KISS TNC over serial TTY/PTY (`device`, `baud`, `kiss_port`, optional `txdelay`/`persistence`/`slottime`/`txtail`/`fullduplex`).
  - **`kiss-tcp`**: KISS over TCP/IP in `client` mode (e.g. Direwolf port 8001, with auto-reconnect) or `server` mode.
  - **`telnet`**: Incoming-only Telnet console (`bind_host`, `bind_port`, `max_clients`) with RFC 854 IAC handling and callsign login, bridged cleanly into the node's session engine (supporting MHEARD, last 5 new callsigns, allow/deny incoming, terminate, ban, and cross-port `CONNECT`).
- **Minimum Packet Node Commands**: `HELP`/`?`, `INFO`, `PORTS`, `USERS`, `MHEARD`, cross-port `CONNECT <port> <callsign>`, and `BYE`/`QUIT`, plus an extension hook (`CommandHandler::register_external_command`) for future standalone BBS and Chat executables.
- **Local `ncurses` Operator UI**:
  - Live table of all radio interfaces showing the **number of active connections on each interface** and RX/TX packet counts.
  - **Last 5 new callsigns heard** (distinct stations as first heard, newest first, with port and timestamp).
  - Live active connections list and scrolling node activity log.
  - Interactive menu bar (`1`-`4` or arrow keys + `Enter`):
    1. **Allow / Deny** new incoming connections
    2. **Terminate** a specific active connection
    3. **Ban / Unban** a callsign (base callsign bans all SSIDs; `CALL-N` bans a specific SSID; immediately disconnects active matching sessions)
    4. **Exit** the program cleanly (sends `DISC` to active sessions and restores terminal state)

## Dependencies

- C++20 compiler (`g++` >= 11 or `clang++` >= 14)
- CMake >= 3.16
- `libncurses-dev` (on Debian/Ubuntu: `sudo apt install libncurses-dev`)

## Building

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

- **X11 GTK+ 2.0 GUI (`oyk-node-gtk`)**: Built automatically by CMake whenever `libgtk2.0-dev` (`gtk+-2.0`) is installed.
- **Headless / no ncurses**: Pass `-DBUILD_NCURSES_UI=OFF`.
- **MS-DOS Port** (IBM XT 8088 / NEC V20, 640KB RAM + optional LIM EMS 4.0, OpenWatcom 1.9 + Michael Brutman's mTCP): Only built when explicitly requested (`-DBUILD_MSDOS_PORT=ON -DMTCP_DIR=/path/to/mtcp`, or `wmake` inside [ports/dos](ports/dos/README.md)).
- **Windows Ports** (16-bit Windows 3.1, 32-bit Windows 95–ME & NT 3.51–11, 64-bit Windows XP x64–11 with native GUIs): Only built when explicitly requested (`-DBUILD_WIN16_PORT=ON` / `-DBUILD_WINDOWS_PORT=ON`, or makefiles inside [ports/windows](ports/windows/README.md)).

## Configuration

When `-c` / `--config` is not passed on the command line, `oyk-node` automatically searches for a configuration file in the following order:

1. `~/.oyk/node.cfg`
2. `/etc/oyk/node.cfg`
3. `./node.cfg`

If none of those files exist, it falls back to built-in defaults (`N0CALL-7`, AXUDP on UDP `10093`). Example configuration files are located in [configs/node.cfg](configs/node.cfg) and [configs/banlist.cfg](configs/banlist.cfg).

### Config File Line Endings

- All platforms accept `CRLF`, `CR`, and `LF` line endings when reading config files (`node.cfg`, `banlist.cfg`).
- MS-DOS and Windows builds write `CRLF` by default; POSIX builds write `LF` by default.
- Config files are written back to disk preserving the line-ending format they had when read. The first line ending encountered in `node.cfg` takes precedence and overrides any mixed line endings.
- Config file line endings never alter AX.25, KISS, AXUDP, AXIP, or Telnet wire protocol specifications.

### Logging (`logpath`, `consolelevel`, `[logfiles]`, Daily Rotation)

Severity bitmask flags:

| Value | Name |
| --- | --- |
| `0x00` | `SILENT` |
| `0x01` | `ERROR` |
| `0x02` | `WARNING` |
| `0x04` | `INFO` |
| `0x10` | `KICK` |
| `0x20` | `BAN` |
| `0x40` | `APPEAL` |
| `0x80` | `DEBUG` |

- `logpath = logs/` in `[node]` sets the directory for all log files.
- `consolelevel = 0x77` in `[node]` filters the local console (defaults to all levels except `DEBUG`).
- `[logfiles]` maps filenames under `logpath` to severity bitmasks (accepts `0x..`, `0b..`, decimal, or pipe-separated names like `ERROR|WARNING|INFO|KICK|BAN`). Defaults: `node.log = 0x37` (`ERROR|WARNING|INFO|KICK|BAN`) and `appeals.log = 0x40` (`APPEAL`).
- Logs rotate daily automatically (`<file>.YYYYMMDD`).

### Ban List (`banlist.cfg`) & Restricted Appeal Sessions

- Stored in `banlist.cfg` in the same directory as `node.cfg` (override with `banlist = <path>` in `[node]`).
- Entry syntax: `CALLSIGN[-SSID]=YYYYMMDD-HHMMSS,YYYYMMDD-HHMMSS|permanent,flags,reason`
- Flags bitfield (`0b...`, `0x...`, or decimal):
  - `0b0001` (`0x01`): Appeal allowed
  - `0b0010` (`0x02`): User has been notified
  - `0b0100` (`0x04`): Appeal already used
  - `0b1000` (`0x08`): Hard ban
- **Unnotified banned users** connect into a restricted session showing their ban details and reason, may submit a single appeal (sanitized to max 140 base printable ASCII chars, logged to `appeals.log` and the local console), and are then disconnected.
- **Soft-banned users** (hard-ban bit clear) may connect once per calendar day to check their ban status and submit their appeal if allowed and unused.
- **Hard-banned users** (hard-ban bit set) are rejected with `DM` on all connections after initial notification.

## Running

```bash
# Auto-discovers ~/.oyk/node.cfg -> /etc/oyk/node.cfg -> ./node.cfg
./build/oyk-node

# Override callsign, alias, or UDP port on the CLI
./build/oyk-node --callsign K7OYK-7 --alias OYKNOD --port 10093

# Or specify an explicit config file
./build/oyk-node -c /path/to/node.cfg

# Optional headless mode (no ncurses)
./build/oyk-node --headless
```

## Documentation

- **User Documentation**:
  - [Getting Started](docs/user/getting-started.md)
  - [Configuration Reference](docs/user/configuration.md)
  - [Operator Guide](docs/user/operator-guide.md)
- **Developer Documentation**:
  - [Architecture Overview](docs/dev/architecture.md)
  - [Implementing Radio Interfaces](docs/dev/radio-interfaces.md)
  - [Porting & Testing Guide](docs/dev/porting-and-testing.md)
  - [AI Use Disclosure](docs/dev/ai-disclosure.md)
  - [Agent & Contributor Guidelines](AGENTS.md)
- **Specifications & Standards**:
  - [Project Specification](docs/specs/oyk-node-spec.md)
  - [Referenced Standards & RFCs](docs/specs/README.md)

## Adding Future Radio Interfaces

1. Subclass `oyk::radio::RadioInterface` in `include/oyk/radio/` and `src/radio/`.
2. Register your driver factory with `oyk::radio::InterfaceRegistry::instance().register_type("kiss-tcp", ...)`.
3. Add an `[interface <id>]` section with `type = kiss-tcp` to your config file.

## AI Use Disclosure

Portions of this project's source code, tests, build files, and documentation were developed with the assistance of AI coding tools under human direction, review, and verification. See [docs/dev/ai-disclosure.md](docs/dev/ai-disclosure.md) and [AGENTS.md](AGENTS.md) for details.

## License

Copyright (C) 2026 KR4OYK - David Kuder. See [AUTHORS](AUTHORS).

This project is licensed under the **GNU General Public License v3.0** (`GPL-3.0-or-later`). See [LICENSE](LICENSE) for the full license text.
