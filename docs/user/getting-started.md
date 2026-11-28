<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Getting Started with oyk-node

**oyk-node** is a modular Amateur Radio AX.25 Packet Node supporting multiple radio/transport interfaces (`axudp`, `axip`, `kiss-serial`, `kiss-tcp`, `telnet`, and embedded `ppp`), built-in BBS/CHAT services, configurable severity logging with daily rotation, persistent ban/appeal handling, and native operator interfaces across desktop, embedded, mobile, and vintage computing platforms (see the [Port Feature Matrix](feature-matrix.md)).

## Requirements (Linux / POSIX)

- C++20 compiler (`g++` >= 11 or `clang++` >= 14)
- CMake >= 3.16
- `libncurses-dev` (for the terminal UI `oyk-node`)
- `libgtk2.0-dev` *(optional)* — if installed, CMake automatically builds the X11 GUI `oyk-node-gtk`

On Debian / Ubuntu:

```bash
sudo apt install build-essential cmake libncurses-dev
# Optional X11 GTK+ 2.0 GUI:
sudo apt install libgtk2.0-dev pkg-config
```

## Building

```bash
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

This produces:
- `build/oyk-node` — ncurses local text-mode operator console (also supports `--headless`)
- `build/oyk-node-gtk` — X11 GTK+ 2.0 GUI (automatically built when `libgtk2.0-dev` is present)

To build without ncurses:

```bash
cmake -B build -DBUILD_NCURSES_UI=OFF
cmake --build build -j
```

## Initial Configuration

See [Configuration Reference](configuration.md) for config file search locations, starter templates in `configs/`, radio interface setup, logging, and `banlist.cfg`.

## Running

```bash
# Auto-discovers ~/.oyk/node.cfg -> /etc/oyk/node.cfg -> ./node.cfg
./build/oyk-node

# Launch X11 GTK+ 2.0 GUI (if built), optionally specifying initial window size/position
./build/oyk-node-gtk
./build/oyk-node-gtk --geometry 1024x768
./build/oyk-node-gtk --geometry 1024x768+40+40

# Override config path or quick parameters from the CLI
# (Supported identically by oyk-node, oykw16/32/64.exe, and MS-DOS oyknode.exe)
./build/oyk-node -c /path/to/node.cfg
./build/oyk-node --callsign K7OYK-7 --alias OYKNOD --port 10093

# Headless / daemon mode (logs to stdout, clean exit on SIGINT/SIGTERM)
./build/oyk-node --headless
```

### Enabling AXIP (IP Protocol 93) Without Running as Root

Linux requires `CAP_NET_RAW` to open raw IP protocol 93 sockets:

```bash
sudo setcap cap_net_raw+ep ./build/oyk-node
```

## Optional Desktop, Embedded, Mobile & Retro Ports

Cross-platform targets are opt-in and disabled by default (`BUILD_*_PORT=OFF` in CMake; see [Build Configuration Reference](../dev/build-config.md)):

- **Windows (Win 3.1 16-bit, Win 95–11 32/64-bit)**: Native WinAPI GUI + Win32 Service — [ports/windows/README.md](../../ports/windows/README.md)
- **macOS (`arm64` / `x86_64` / `ppc`)**: Native AppKit `.app` bundle (OS X 10.4–15+) — [ports/macos/README.md](../../ports/macos/README.md)
- **Teensy 4.1 / TinyTMUX (`IMXRT1062`)**: 8-UART carrier board, `QNEthernet`, RadioFoundry USB Host + PPP, VT100/VT220 console — [ports/tinytmux/README.md](../../ports/tinytmux/README.md)
- **Raspberry Pi Pico & PicoCalc (`RP2040` / `RP2350`)**: `pico_w`, `pico2_w`, `waveshare_core2350b`, `pimoroni_pga2350`, `picocalc_pico2_w`, `picocalc_pico_w`, `picocalc_pico2`, `picocalc_pico` UF2 firmware — [ports/pico/README.md](../../ports/pico/README.md)
- **Luckfox Lyra (`RK3506G2` / `RK3506B` RT-Thread SMP + PicoCalc)**: `lyra`, `lyra_b`, `lyra_plus`, `lyra_zero_w`, `picocalc_lyra`, `picocalc_lyra_b`, `picocalc_lyra_plus` (RMII Ethernet, Wi-Fi 6 + BLE, USB RNDIS/PPP, `DFS` SD/NAND, opt-in BEL audio) — [ports/lyra/README.md](../../ports/lyra/README.md)
- **ESP32-C6 (`M5Stack Unit C6L`)**: SX1262 LoRa KISS, 64x48 SPI OLED, Wi-Fi 6 + BLE 5, 16 MB Flash LittleFS — [ports/esp32c6/README.md](../../ports/esp32c6/README.md)
- **Android**: Jetpack Compose + Material 3 + NDK C++20 engine (`./gradlew :app:assembleSideloadDebug :app:testPlayStoreDebugUnitTest`) — [ports/android/README.md](../../ports/android/README.md)
- **MS-DOS Text & Graphical (`oyknode.exe`, `oyknodeg.exe`)**: 8088/V20 640 KB real-mode + 386+ EGA/VGA/VBE graphical builds — [ports/dos/README.md](../../ports/dos/README.md)
- **Classic Macintosh (System 6.0.8–Mac OS 9.2.2)**: 68k + PPC Fat application (`Retro68`) — [ports/macclassic/README.md](../../ports/macclassic/README.md)
- **Apple IIGS GS/OS (`65C816` S16)**: QuickDraw II `640x200` Super Hi-Res + Marinetti/SCC — [ports/gsos/README.md](../../ports/gsos/README.md)
- **Amiga Workbench (`OS 1.3` / `2.04` / `3.1` / AROS)**: Adaptive native-lock/32+ color theme & optional private screen — [ports/amiga/README.md](../../ports/amiga/README.md)
- **Atari ST / STE / TT / Falcon GEM**: `OYKNODE.PRG` for ST High Monochrome (`640x400`) & 16-color VDI (`640x480`) — [ports/atarist/README.md](../../ports/atarist/README.md)
