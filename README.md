<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# oyk-node

A modular Amateur Radio AX.25 Packet Node written in C++ with native local operator consoles spanning modern desktops, embedded microcontrollers, mobile, and classic retro computers.

## Supported Platforms & Ports

See the full [Feature Matrix](docs/user/feature-matrix.md) (`●` / `◐` / `○` / `—` across all **14 build targets**) and [UI Gallery](docs/user/ui-gallery.md) for side-by-side screenshots.

| Category | Target / Port | UI / Hardware Stack | Documentation |
| --- | --- | --- | --- |
| **Desktop** | Linux / POSIX Console | `ncurses` TUI + `--headless` daemon (`C++20`) | [Getting Started](docs/user/getting-started.md) |
| **Desktop** | Linux X11 GUI | `GTK+ 2.0` (`C++20`) | [Getting Started](docs/user/getting-started.md) |
| **Desktop** | Windows 32/64-bit & Win 3.1 16-bit | Native WinAPI (`Winsock 1.1`, Win32 Service) | [ports/windows/README.md](ports/windows/README.md) |
| **Desktop** | macOS (`arm64` / `x86_64` / `ppc`) | Cocoa AppKit (`C++20` + OS X 10.4–10.6 `ppc` `C++98`) | [ports/macos/README.md](ports/macos/README.md) |
| **Embedded** | Teensy 4.1 (`IMXRT1062` / TinyTMUX) | VT100/VT220 USB Serial, 8 UARTs, `QNEthernet`, USBHost RadioFoundry PPP | [ports/tinytmux/README.md](ports/tinytmux/README.md) |
| **Embedded** | Raspberry Pi Pico (`RP2040` / `RP2350`) | VT100/VT220 USB CDC, 2 UARTs, `CYW43439`/`RM2` Wi-Fi + BTstack | [ports/pico/README.md](ports/pico/README.md) |
| **Embedded** | ESP32-C6 (`M5Stack Unit C6L`) | SX1262 LoRa KISS, 64x48 SPI OLED, Wi-Fi 6 + BLE 5, VT340 USB JTAG | [ports/esp32c6/README.md](ports/esp32c6/README.md) |
| **Mobile** | Android | Jetpack Compose + Material 3 + C++20 NDK `NodeEngine` | [ports/android/](ports/android/) |
| **Retro** | MS-DOS Text (8088/V20 640 KB) | Direct MDA/CGA/EGA/VGA video RAM (`INT 33h`, optional mTCP/PPP) | [ports/dos/README.md](ports/dos/README.md) |
| **Retro** | MS-DOS Graphical (386+ XMS/EMS4) | EGA / VGA / VBE 80x30–128x48 smooth-scrolling desktop (`oyknodeg.exe`) | [ports/dos/README.md](ports/dos/README.md) |
| **Retro** | Classic Macintosh (System 6.0.8–OS 9.2.2) | 68k + PPC Fat, QuickDraw B&W/Color, `MacTCP`/`OpenTransport` | [ports/macclassic/README.md](ports/macclassic/README.md) |
| **Retro** | Apple IIGS GS/OS (`65C816` S16) | QuickDraw II `640x200` Super Hi-Res, `Marinetti #54` + SCC `.A`/`.B` | [ports/gsos/README.md](ports/gsos/README.md) |
| **Retro** | Amiga Workbench (`1.3` / `2.04` / `3.1` / AROS) | Intuition GUI, `< 32`-color WB lock / `>= 32`-color themes, `bsdsocket` | [ports/amiga/README.md](ports/amiga/README.md) |
| **Retro** | Atari ST / STE / TT / Falcon GEM | GEM AES/VDI (`640x400` Mono + `640x480` 16-Color), `MiNTnet`/`STinG` | [ports/atarist/README.md](ports/atarist/README.md) |

## Features

- **AX.25 v2.0 Layer 2 Engine**: Modulo-8 connected-mode sessions (`SABM`, `UA`, `DM`, `DISC`, `I`, `RR`, `RNR`, `REJ`, `UI`), windowing, `T1`/`N2` retries, idle timeout, and CRC-16-CCITT (X.25) FCS.
- **Modular Radio Interfaces**: Built-in drivers for `axudp`, `axip` (IP protocol 93 / RFC 1226), `kiss-serial`, `kiss-tcp` (client and server modes), `telnet`, and embedded `ppp` / USB Host (`usb-cdc-net`, `usb-cdc-serial`).
- **Built-in BBS & Packet CHAT**: Integrated BBS mail & downloadable files area (SSID `-11` / `OYKBBS`) with sysop incoming file approval, plus multi-user Packet CHAT room (SSID `-12` / `OYKCHT`).
- **Logging, Bans & Hardware Peripherals**: Severity-bitmask logging with daily rotation, direct line-by-line printer output (`generic`, `mx80`, `escp2`), Hamlib `rigctld` / RadioFoundry CAT control, persistent `banlist.cfg`, soft/hard bans, and restricted appeal sessions.
- **Unified Operator UI & Local Terminal**: Consistent 7-tab `Node Settings` (`1:Node`..`7:Prn`), interactive 4-step interface wizard, `Ctrl+A` escape-hotkey Local Terminal with YAPP/YAPP-C transfers, auto line-ending detection + word-wrap, and OS-native default palettes with whole-screen live theme preview.

## Dependencies (Host Linux)

- C++20 compiler (`g++` >= 11 or `clang++` >= 14)
- CMake >= 3.16
- `libncurses-dev` (Debian/Ubuntu: `sudo apt install libncurses-dev`)
- `libgtk2.0-dev` *(optional)* — if installed, CMake automatically builds `oyk-node-gtk`

## Building & Running

```bash
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

```bash
# Start with auto-discovered node.cfg (or built-in defaults)
./build/oyk-node

# Launch X11 GTK+ 2.0 GUI (if built)
./build/oyk-node-gtk

# Quick deployment with CLI overrides or headless daemon mode
./build/oyk-node --callsign K7OYK-7 --alias OYKNOD --port 10093
./build/oyk-node -c /path/to/node.cfg
./build/oyk-node --headless
```

All cross-targets (`BUILD_*_PORT`) default to `OFF`; see [Build Configuration Reference](docs/dev/build-config.md) and [Continuous Integration & Checks](docs/dev/ci.md) (`./scripts/ci.sh --quick`).

## Documentation

- **User Documentation** ([docs/user/](docs/user/README.md)):
  - [Getting Started](docs/user/getting-started.md)
  - [Configuration Reference](docs/user/configuration.md)
  - [Operator Guide](docs/user/operator-guide.md)
  - [Port Feature Matrix](docs/user/feature-matrix.md)
  - [UI Gallery](docs/user/ui-gallery.md)
- **Developer Documentation** ([docs/dev/](docs/dev/README.md)):
  - [Architecture Overview](docs/dev/architecture.md)
  - [Build Configuration Reference](docs/dev/build-config.md)
  - [Implementing Radio Interfaces](docs/dev/radio-interfaces.md)
  - [Porting & Testing Guide](docs/dev/porting-and-testing.md)
  - [Continuous Integration & Verification](docs/dev/ci.md)
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
