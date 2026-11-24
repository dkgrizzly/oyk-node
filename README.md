<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# oyk-node

A modular Amateur Radio AX.25 Packet Node written in C++ with native local operator consoles for Linux (`ncurses` and X11 `GTK+ 2.0`), MS-DOS, and Windows.

## Features

- **AX.25 v2.0 Layer 2 Engine**: Modulo-8 connected-mode sessions (`SABM`, `UA`, `DM`, `DISC`, `I`, `RR`, `RNR`, `REJ`, `UI`), windowing, `T1`/`N2` retries, idle timeout, and CRC-16-CCITT (X.25) FCS.
- **Modular Radio Interfaces**: Built-in drivers for `axudp`, `axip` (IP protocol 93 / RFC 1226), `kiss-serial`, `kiss-tcp` (client and server modes), and incoming `telnet`.
- **Packet Node Commands**: `HELP`/`?`, `INFO`, `PORTS`, `USERS`, `MHEARD`, cross-port `CONNECT <port> <callsign>`, and `BYE`/`QUIT`, with an extension hook for future standalone BBS and Chat executables.
- **Logging & Ban Management**: Severity-bitmask file/console logging with daily rotation, persistent `banlist.cfg`, soft/hard bans, and restricted notification & appeal sessions.
- **Local Operator Interfaces**: Consistent top `Node` menu bar (`Allow/Deny Incoming`, `Terminate Connection`, `Ban/Unban Callsign`, `Exit`), per-interface active connection counts, last 5 new callsigns heard, live session list, and activity log across `ncurses`, X11 `GTK+ 2.0`, MS-DOS, and Windows 16/32/64-bit.

## Dependencies

- C++20 compiler (`g++` >= 11 or `clang++` >= 14)
- CMake >= 3.16
- `libncurses-dev` (Debian/Ubuntu: `sudo apt install libncurses-dev`)
- `libgtk2.0-dev` *(optional)* — if installed, CMake automatically builds the X11 GTK+ 2.0 GUI (`oyk-node-gtk`)

## Building

```bash
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

- **X11 GTK+ 2.0 GUI (`oyk-node-gtk`)**: Built automatically when `libgtk2.0-dev` is installed.
- **Headless / no ncurses**: Pass `-DBUILD_NCURSES_UI=OFF`.
- **MS-DOS & Windows Ports** *(opt-in)*: Disabled by default; enable with `-DBUILD_MSDOS_PORT=ON`, `-DBUILD_WIN16_PORT=ON`, or `-DBUILD_WINDOWS_PORT=ON` (see [Porting & Testing Guide](docs/dev/porting-and-testing.md)).

## Running

Copy the starter config files from [configs/](configs/) (see [Configuration Reference](docs/user/configuration.md) for locations and options), or start immediately with CLI overrides:

```bash
# Start with auto-discovered node.cfg (or built-in defaults)
./build/oyk-node

# Launch X11 GTK+ 2.0 GUI (if built)
./build/oyk-node-gtk

# Quick first-node deployment with CLI overrides
./build/oyk-node --callsign K7OYK-7 --alias OYKNOD --port 10093

# Specify an explicit config file
./build/oyk-node -c /path/to/node.cfg

# Headless mode (no ncurses UI)
./build/oyk-node --headless
```

## Documentation

- **User Documentation** ([docs/user/](docs/user/README.md)) — Everything needed to deploy, configure, and operate `oyk-node`:
  - [Getting Started](docs/user/getting-started.md)
  - [Configuration Reference](docs/user/configuration.md)
  - [Operator Guide](docs/user/operator-guide.md)
- **Developer Documentation** ([docs/dev/](docs/dev/README.md)) — Architecture, drivers, porting, testing, and contributor rules:
  - [Architecture Overview](docs/dev/architecture.md)
  - [Implementing Radio Interfaces](docs/dev/radio-interfaces.md)
  - [Porting & Testing Guide](docs/dev/porting-and-testing.md)
  - [AI Use Disclosure](docs/dev/ai-disclosure.md)
  - [Agent & Contributor Guidelines](AGENTS.md)
- **Specifications & Standards** ([docs/specs/](docs/specs/README.md)):
  - [Project Specification](docs/specs/oyk-node-spec.md)
  - [Referenced Standards & RFCs](docs/specs/README.md)

## AI Use Disclosure

Portions of this project's source code, tests, build files, and documentation were developed with the assistance of AI coding tools under human direction, review, and verification. See [docs/dev/ai-disclosure.md](docs/dev/ai-disclosure.md) and [AGENTS.md](AGENTS.md) for details.

## License

Copyright (C) 2026 KR4OYK - David Kuder. See [AUTHORS](AUTHORS).

This project is licensed under the **GNU General Public License v3.0** (`GPL-3.0-or-later`). See [LICENSE](LICENSE) for the full license text.
