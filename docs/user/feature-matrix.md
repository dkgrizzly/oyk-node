<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Port Feature Matrix

Comparison of features, transports, operator UI workflows, and verification coverage across all **15 `oyk-node` build targets** (13 port families).

## Legend

| Symbol | Meaning |
| --- | --- |
| `●` | Full support — implemented and verified in build/tests |
| `◐` | Partial / conditional — depends on hardware, OS version, or optional stack (see footnotes) |
| `○` | Planned / roadmap |
| `—` | Not applicable or hardware-limited on target platform |

---

## 1. Desktop & Server Ports

| Feature / Subsystem | [Linux ncurses](../../README.md) | [Linux GTK+ 2.0](../../README.md) | [Windows 32/64](../../ports/windows/README.md) | [Windows 3.1 16-bit](../../ports/windows/README.md) | [macOS Cocoa](../../ports/macos/README.md) |
| --- | --- | --- | --- | --- | --- |
| **Core Engine** | C++20 multi-thread | C++20 multi-thread | C++98 cooperative | C++98 cooperative | ◐ C++20 (`arm64`/`x86_64`), C++98 (`ppc`) [1] |
| **AX.25 v2.0 L2 & Bridging** | ● | ● | ● | ● | ● |
| **Built-in BBS (Mail + Files) & CHAT** | ● | ● | ● | ● | ● |
| **Hard/Soft Bans, Appeals & Rotation** | ● | ● | ● | ● | ● |
| **Config Line Endings (`CRLF`/`CR`/`LF`)** | ● | ● | ● | ● | ● |
| **`axudp` / `axip` (IP Proto 93)** | ● | ● | ◐ `axudp` (`Winsock`) [2] | ◐ `axudp` (`Winsock 1.1`) [2] | ● (`arm64`/`x86_64`), ◐ `axudp` (`ppc`) |
| **`kiss-serial` / `kiss-tcp` / `telnet`** | ● | ● | ● | ● | ● |
| **`oyk_gui` + `oyk_term` (`Ctrl+A` / YAPP)** | ● | ● | ● | ● | ● |
| **7-Tab Settings, Wizards & File Approve** | ● | ● | ● | ● | ● |
| **OS-Native Default + Whole-Screen Preview** | ● | ● | ● | ● | ● |
| **Mouse / Pointer Support** | ● (SGR/X10) | ● | ● | ● | ● |
| **Headless / Daemon / Service Mode** | ● (`--headless`) | — | ● (Win32 Service) | — | ○ |
| **Hamlib `rigctld` / Direct Printer Log** | ● | ● | ● | ● (`OpenComm`) | ● |
| **Automated UI Gallery Capture** | ● (`ncurses`) | ● (`gtk`) | ● (`win` under Wine) | ○ | ● (`macos`) |

---

## 2. Embedded & Mobile Ports

| Feature / Subsystem | [Teensy 4.1 TinyTMUX](../../ports/tinytmux/README.md) | [Raspberry Pi Pico](../../ports/pico/README.md) | [Luckfox Lyra](../../ports/lyra/README.md) | [ESP32-C6 Unit C6L](../../ports/esp32c6/README.md) | [Android Jetpack Compose](../../ports/android/README.md) |
| --- | --- | --- | --- | --- | --- |
| **Target Hardware** | IMXRT1062 (600 MHz, 8 UART) | RP2040 / RP2350 (`pico_w`, `pico2_w`, `2350B`, `PicoCalc`) | RK3506G2/B (`lyra*`, `lyra_zero_w`, `picocalc_lyra*`) | ESP32-C6 + SX1262 + 64x48 OLED | Android NDK + Compose |
| **Core Engine** | C++98 cooperative | C++98 cooperative | C++98 (`RT-Thread SMP + rk-hal`) | C++98 cooperative (`ESP-IDF 5.4`) | C++20 `NodeEngine` + JNI |
| **AX.25 L2 / BBS / CHAT / Bans** | ● | ● | ● | ● | ● |
| **`axudp` / `axip` / `kiss-tcp` / `telnet`** | ● (`QNEthernet`/lwIP + `raw_new(93)`) | ● (lwIP `CYW43439` / optional `RM2`) [3] | ● (`SAL`/`lwIP`: RMII Eth, Wi-Fi 6, USB RNDIS) | ● (Wi-Fi 6 lwIP) | ● (POSIX NDK sockets) |
| **`kiss-serial` / UARTs** | ● (`ser0`–`ser7` + RTS/CTS) | ● (`ser0`–`ser1`) | ● (`ser0`–`ser1` standalone; `ser0` PicoCalc) | ● (SX1262 `lora0` + Grove `ser0`) | ◐ USB/BLE KISS roadmap [4] |
| **PPP & USB Host (RadioFoundry UDP 4191)** | ● (`PPP` + `USBHost_t36` ECM/NCM/ACM) | ● (`PPP` + `BTstack` SPP/BLE) | ● (`PPP` + BLE 5.2 + USB OTG RNDIS) | ● (Grove `ser0` PPP + BLE 5 KISS) | — |
| **Console / UI Model** | VT100/VT220 USB Serial | VT100/VT220 USB CDC + PicoCalc 320x320 LCD/KBD (`oyk-vt100`) | VT100/xterm256 `uart0` + PicoCalc 320x320 LCD/KBD (`oyk-vt100`) | VT340 USB Serial/JTAG + 4-Page OLED | Material 3 Compose + Drawer |
| **`oyk_gui` + `oyk_term` (`Ctrl+A` / YAPP)** | ● | ● | ● | ● | ● (Material mobile layout) [5] |
| **Themes + Whole-Screen Live Preview** | ● | ● | ● | ● | ● |
| **Storage / Memory Extensions** | microSD + QSPI CS1 + 4 MB LittleFS + PSRAM | microSD + LittleFS + QMI PSRAM (`2350B`) | DFS microSD FatFS + 256 MB SPI NAND LittleFS | 16 MB Flash LittleFS + `FORMAT` guard | App-scoped storage |
| **Hardware Diagnostics & Bootloader UI** | ● (March C- PSRAM, Format, HalfKay) | ● (PSRAM test, Format, `BOOTSEL`) | ● (RK3506 SoC diag, Format, Reboot/STM32 Power-Off) | ● (`PI4IO` RF, WS2812C, Buzzer, Format) | — |
| **Hamlib `rigctld` / Direct Printer Log** | ● (TCP + USB Printer / Serial) | ● (TCP + Serial Printer) | ● (TCP + Serial Printer) | ● (TCP `rigctld` + `ser0` Printer) | ● (TCP `rigctld`) |
| **Host Tests & UI Gallery Capture** | ● (`oyk_tinytmux_tests`, `tinytmux`) | ● (`oyk_pico_tests`, `pico`, `picocalc`) | ● (`oyk_lyra_tests`, `oyk_lyra_tui_parity_test`, ARMv7 verify) | ● (`oyk_esp32c6_tests`, `esp32c6`) | ● (Robolectric `android`) |

---

## 3. Vintage & Retro Desktop Ports

| Feature / Subsystem | [MS-DOS Text](../../ports/dos/README.md) | [MS-DOS GFX](../../ports/dos/README.md) | [Classic Mac](../../ports/macclassic/README.md) | [Apple IIGS GS/OS](../../ports/gsos/README.md) | [Amiga Workbench](../../ports/amiga/README.md) | [Atari ST / TT GEM](../../ports/atarist/README.md) |
| --- | --- | --- | --- | --- | --- | --- |
| **Target System & CPU** | 8088/V20 (640 KB) | 386+ (XMS/EMS4) | 68k / PPC Fat (Sys 6.0.8–9.2.2) | 65C816 (`$B3` S16) | 68k (OS 1.3 / 2.04 / 3.1 / AROS) | 68k (`OYKNODE.PRG`) |
| **AX.25 L2 / BBS / CHAT / Bans** | ● | ● | ● | ◐ Core node + incoming [6] | ● | ● |
| **Config EOL Preservation** | ● (`CRLF` default) | ● (`CRLF` default) | ● (`CR` / `LF` / `CRLF`) | ● | ● | ● |
| **Network Stack (`axudp`)** | ◐ Optional mTCP / PPP mini-stack [7] | ◐ Optional mTCP / PPP [7] | ◐ `MacTCP` / `OpenTransport` [8] | ◐ `Marinetti #54` [8] | ◐ `bsdsocket.library` [8] | ◐ `MiNTnet` / `STinG` [8] |
| **Serial KISS (`kiss-serial`)** | ● (`8250`/`16550`, `USBDOS`) | ● (`8250`/`16550`, `USBDOS`) | ● (`Modem`/`Printer` SCC) | ● (SCC `.A`/`.B`) | ● (`serial.device`) | ● (BIOS `AUX:` MFP/SCC) |
| **`oyk_gui` 7-Tab Settings & Wizards** | ● | ● | ● | ● | ● | ● |
| **Local Terminal (`Ctrl+A` / Wrap / YAPP)** | ● | ● | ● | ● (Core / Term) | ● | ● |
| **Display & Resolution Adaptation** | ◐ MDA 80x25 / CGA-VGA | ● EGA/VGA/VBE 80x30–128x48 | ● 512x342 B&W + Color QD | ● 640x200 SHR | ● Native WB / Private Screen [9] | ● 640x400 Mono + 640x480x16 VDI |
| **Themes + Whole-Screen Live Preview** | ● | ● | ● (1-bit + Color `native`) | ● (SHR OS `native` + themes) | ◐ `< 32` colors WB lock; `>= 32` themes [9] | ● (Mono invert + VDI `vs_color`) |
| **Mouse / Pointer Support** | ● (`INT 33h`) | ● (`INT 33h`) | ● (Classic Toolbox) | ● (Event Manager) | ● (Intuition `IDCMP`) | ● (GEM `evnt_multi`) |
| **Direct Printer / `rigctld` CAT** | ● (`INT 17h`/`COM`) | ● (`INT 17h`/`COM`) | ● (Serial/Printer) | ◐ CAT status | ● | ● |
| **Emulator Tests & UI Gallery** | ● (QEMU FreeDOS, `dos`) | ● (`dos_gfx`) | ● (Mini vMac, `macclassic_*`) | ● (GSplus, `gsos`) | ● (FS-UAE + AROS, `amiga`) | ● (Hatari + EmuTOS, `atarist_*`) |

---

## Footnotes

1. **macOS**: Universal `arm64`/`x86_64` builds link the full multi-threaded C++20 `oyknode_core`; legacy Mac OS X 10.4–10.6 `ppc` builds use `-DOYK_MACOS_PORTABLE_CORE=ON` with `OykPortableNode`.
2. **Windows Winsock**: Built against portable `Winsock 1.1` (`wsock32.lib`) for Windows 3.1 through Windows 11 compatibility; raw `axip` (IP protocol 93) requires POSIX `SOCK_RAW` (`CAP_NET_RAW`) or lwIP `raw_new(93)`.
3. **Raspberry Pi Pico**: Uses onboard CYW43439 Wi-Fi on `pico_w`/`pico2_w` and optional RM2 wireless (`CYW43_PIN_WL_DYNAMIC`) with graceful 2-UART (`ser0`/`ser1`) non-fatal fallback when wireless hardware is absent.
4. **Android**: Supports `axudp`, `axip` (where permitted), `kiss-tcp`, `telnet`, and `rigctld` over NDK sockets cleanly; Android UI adapts layouts to Material 3 guidelines.
5. **UI Parity Exceptions**: Windows and GTK+ 2.0 may use native OS file-selection dialogs rather than the built-in single-pane file selector; Android adapts navigation drawers and sheets to Material Design guidelines.
6. **Apple IIGS GS/OS**: Built around a tight `65C816` bank-safe `gsos_core` (`-maxsegsize=16384` OMF v2 S16) providing node management, config EOL preservation, QuickDraw II UI, theme live preview, terminal helpers, and hardware detection.
7. **MS-DOS Stacks**: Both `oyknode.exe` and `oyknodeg.exe` treat mTCP and Bret Johnson's `USBDOS` stack as optional runtime dependencies—running Packet Driver, built-in PPP mini-stack, or serial KISS cleanly without them.
8. **Retro TCP/IP Detection**: Classic Mac (`MacTCP`/`OpenTransport`), GS/OS (`Marinetti #54`), Amiga (`bsdsocket.library`), and Atari ST (`'MiNT'`/`'STiK'` cookies) detect TCP/IP stacks dynamically at runtime and fall back cleanly to serial KISS when absent.
9. **Amiga Theme & Screen Policy**: On Workbench screens with fewer than 32 colors (`depth < 5`), `oyk-node` locks to OS-native Workbench pens so shared screen colors stay legible; at `>= 32` colors (`depth >= 5`), OYK theme presets and whole-screen live preview unlock. Private screen creation (`amiga_private_screen = 1` / `--private-screen` / `PRIVATESCREEN`) is disabled by default.

See the [UI Gallery](ui-gallery.md) for automated screenshots of every tested workflow across all captured ports, [ports/sysv/](../../ports/sysv/README.md) for the shared strict-C89 System V core, [Motorola 88K SVR4](../../ports/m88k/README.md) for `mc88100` curses TUI + OSF/Motif 1.2, [AT&T 3B2 SVR3.2](../../ports/att3b2/README.md) for the `WE32100` curses/VT100 target, [TI-99/4A](../../ports/ti99/README.md) for SAMS 1 MB + TIPI + F18A/40-col, [OpenVMS](../../ports/openvms/README.md) for VAX / Alpha / Itanium / x86-64 (`SMG$` console + DECwindows Motif 1.2), and [OS/2 2.0+](../../ports/os2/README.md) for 32-bit VIO console + Presentation Manager GUI.
