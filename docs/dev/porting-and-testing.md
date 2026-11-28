<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Porting & Testing Guide

## Portable C++98 Core & Middleware (`ports/compat/`)

To support constrained 16-bit and vintage targets (640 KB MS-DOS, Windows 3.1, 68k Classic Mac, `65C816` Apple IIGS GS/OS, `m68k` Amiga & Atari ST) as well as Teensy 4.1 and Raspberry Pi Pico, a self-contained C++98 engine and shared UI middleware layer lives in [ports/compat/](../../ports/compat/):

- `oyk_portable.{hpp,cpp}` — AX.25, KISS, sessions, BBS, CHAT, banlist, config EOL I/O
- `oyk_gui.{hpp,cpp}` & `oyk_gui_theme.cpp` — Cross-port 7-tab Settings schemas, wizards, table columns, OS-native themes, live whole-screen preview
- `oyk_tui.{hpp,cpp}` — Shared 80x25..132x50 TUI geometry & hit-testing (`[X]` close button, modal centering)
- `oyk_term.{hpp,cpp}` — Shared EOL auto-detection, flush-left word-wrap, and `Ctrl+A` escape-hotkey table
- `oyk_yapp.{hpp,cpp}`, `oyk_rig.{hpp,cpp}`, `oyk_printer.{hpp,cpp}` — YAPP/YAPP-C transfers, Hamlib `rigctld`/RadioFoundry CAT, direct ESC/P/MX-80 formatting

Design rules for `ports/compat/`:
- Strict C++98 (`-std=c++98`), zero STL containers, zero exceptions, zero RTTI, `16`-bit safe (`sizeof(int) == 2`).
- Static stack frame cap of **<= 1,024 bytes** per function enforced by `python3 scripts/check_stack.py --limit 1024`.
- Binary-mode config I/O (`"rb"`/`"wb"`) accepting `CRLF`, `CR`, and `LF`, preserving original EOLs cleanly.

## Platform Build Options

See [Build Configuration Reference](build-config.md) for the complete table of all 16 root CMake flags (`BUILD_NCURSES_UI`, `BUILD_GTK2_UI`, `OYK_WERROR`, `OYK_SANITIZE`, and all 11 opt-in `BUILD_*_PORT` flags) plus port-specific Makefiles and SDK variables.

## Running Tests

### Native Host Suite (11 CTest Targets)

```bash
./scripts/ci.sh --quick
# Or manually:
cmake -B build -DOYK_WERROR=ON
cmake --build build -j
ctest --test-dir build --output-on-failure -V
```

| CTest Target | Scope |
| --- | --- |
| `oyk_node_tests` | Core C++20 AX.25, CRC-16, KISS, AXUDP, AXIP, KISS Serial/TCP, Telnet, BBS, CHAT, logging, ban/appeal, config EOL |
| `oyk_portable_tests` | Strict C++98 portable core, MS-DOS text & DOS-GFX renderer/flows, `oyk_term`, YAPP, rig, printer, Win Service builder |
| `oyk_gui_tests` | Shared `oyk_gui` middleware schemas, wizards, theme controller, live preview, cancel revert |
| `oyk_macos_bridge_tests` | Dual-core macOS AppKit bridge & OS-native theme resolution |
| `oyk_gsos_tests` | Apple IIGS `65C816` `gsos_core` / QuickDraw II theme & terminal host verification |
| `oyk_amiga_tests` | Amiga Kickstart `1.3`/`2.04`/`3.1` policy, `< 32`-color native lock, `amiga_private_screen` default off |
| `oyk_atarist_tests` | Atari ST/TT display tiers, VDI RGB conversion, 4-plane hardware-VDI color mapping |
| `oyk_att3b2_tests` | AT&T 3B2 SVR3.2 C89 `sysv` core parity, VT100 23-screen check, `OYK_NO_MOTIF` verification |
| `oyk_ti99_tests` | TI-99/4A C99 big-endian AX.25/KISS, SAMS 1 MB paging, F18A 80-col / 9918 40-col 23-screen parity |
| `oyk_openvms_tests` | OpenVMS `SMG$` / VT100 23-screen parity & VAX/Alpha/IA64/x86-64 C89 `sysv` core verification |
| `oyk_os2_tests` | OS/2 2.0+ `SO32DLL` fallback, `COM1`–`COM4` KISS validation, and 23-screen VIO/PM schema parity |
| `oyk_tinytmux_tests` | Teensy 4.1 8-UART carrier, PSRAM, FS hooks, USB Host RadioFoundry, PPP, Ethernet bridge, VT100 console |
| `oyk_pico_tests` | RP2040/RP2350 profiles, PSRAM/Flash fallback, RM2 optional radio, 2-UART console |
| `oyk_esp32c6_tests` | M5Stack Unit C6L pinout, `PI4IO` RF/button, SX1262 LoRa KISS, 64x48 OLED 4 pages, LittleFS, VT340 TUI |
| `oyk_fuzz_tests` | Deterministic seeded mutation fuzzing across 7 untrusted wire/config parsers (`OYK_FUZZ_ITERS`, `OYK_FUZZ_SEED`) |
| `oyk_ncurses_ui_tests` | End-to-end PTY UI test for `oyk-node` |

### Emulator & Device Runners

- **MS-DOS 16-bit (QEMU + FreeDOS)**: `./ports/dos/run_qemu_tests.sh` (`oyk_dos_qemu_tests`, see [ports/dos/README.md](../../ports/dos/README.md))
- **Classic Macintosh (Mini vMac + System Software)**: `./ports/macclassic/run_minivmac_test.sh --unit-tests`
- **Apple IIGS GS/OS (GSplus + ProDOS)**: `./ports/gsos/run_gsplus_test.sh --unit-tests`
- **Amiga Workbench (FS-UAE + AROS m68k)**: `./ports/amiga/run_fsuae_test.sh --unit-tests`
- **Atari ST / TT GEM (Hatari + EmuTOS)**: `./ports/atarist/run_hatari_test.sh --unit-tests`
- **AT&T 3B2 SVR3.2 (Telnet + Local FTP)**: `OYK_3B2_HOST=... OYK_3B2_PORT=... OYK_3B2_USER=... ./ports/att3b2/run_3b2_remote.sh --build --unit-tests`
- **Android (Robolectric)**: `(cd ports/android && ./gradlew testDebugUnitTest)`
- **UI Gallery & Cross-TUI Parity**: `./scripts/generate_ui_gallery.sh`
