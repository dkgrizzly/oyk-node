<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# OYK Packet Node — Teensy 4.1 / TinyTMUX (Teensyduino) Port

This port targets the **Teensy 4.1** (**NXP/Freescale MIMXRT1062DVJ6A**, ARM Cortex-M7F @ 600 MHz, 1 MB SRAM, 8 MB QSPI Flash, 10/100 Ethernet PHY, USB High-Speed OTG, microSD) on the **TinyTMUX** 8-UART carrier board using **Arduino 1.8.19 + Teensyduino 1.62**.

## Features

- **Dual Network Transports (Simultaneous)**:
  - **10/100 Ethernet Jack** (`IMXRT1060 ENET` + `DP83825I` RMII PHY, Pin 18 100 Mbps speed LED) via **QNEthernet** (primary, non-blocking DHCP + lwIP UDP AXUDP + raw IP protocol 93 AXIP + TCP `rigctld`) with **NativeEthernet** fallback.
  - **Teensy 4.1 USB Host (`USB_OTG2`)** via **USBHost_t36** for **RadioFoundry** radios (direct or via `USBHub`, `usbh0`..`usbh3`):
    - **USB CDC-ECM / CDC-NCM Network (`usb-cdc-net`)**: `OykUsbCdcNetDriver` feeding Ethernet, ARP, IPv4/UDP, ICMP Echo, AXUDP, AXIP, plus RadioFoundry UDP 4191 zone/channel control.
    - **USB CDC-ACM / USB-to-Serial (`usb-cdc-serial`)**: `USBSerial_BigBuffer` carrying RFC 1661/1662 PPP or KISS.
  - **PPP over Serial** (`ser0`..`ser7`) for **RadioFoundry** firmware radios: RFC 1661 LCP, RFC 1662 Async HDLC framing & FCS-16, RFC 1332 IPCP IPv4 negotiation, ICMP Echo Reply (`ping`), carrying both **AXUDP**, **AXIP (IP proto 93)**, and RadioFoundry UDP 4191 control.
  - **KISS Serial TNC** support on any remaining UART (`ser0`..`ser7`).
- **Rig Control (`oyk_rig`)**: Hamlib `rigctld` (TCP 4532) and RadioFoundry / takRF Zone & Channel Control (UDP 4191) across Ethernet, UART PPP, and USB Host transports.
- **Direct Line-by-Line Printer Logging (`[printer]`)**: Streams log lines directly to a USB Printer Class (`0x07/0x01`) device or USB-to-parallel adapter on the USB Host port (`usb-printer`), or to a serial printer on `ser0`..`ser7`, supporting `generic`, `mx80` (Epson MX-80), and `escp2` (Epson ESC/P2) protocols without any spooler.
- **FlexSPI2 QSPI PSRAM Startup Detection & Self-Test**: Uses Teensyduino's boot-time `external_psram_size` probe (`0x70000000`, 0/8/16 MB) plus our multi-pattern memory test and exposes live diagnostics/re-test (`P`) in the console.
- **MicroSD (`SD.h` / `BUILTIN_SDCARD`)**: 4-bit SDIO microSD storage (`/node.cfg`, `banlist.cfg`, `logs/`, `mail/`, `files/`, `incoming/`) with POSIX newlib stdio hooks preserving Teensyduino `Print::printf()`.
- **Dual USB-CDC (`USB_DUAL_SERIAL`) + Full VT100 Operator Console**:
  - `Serial` (`usb0`, `/dev/ttyACM0`): Full 80×24 ANSI/VT100 Operator Console matching the Linux `ncurses`, GTK, Windows, and MS-DOS layouts.
  - `SerialUSB1` (`usb1`, `/dev/ttyACM1`): Auxiliary one-line status & diagnostic stream (`?` / `s` / Enter).
  - `Serial1` (`ser0` @ 115200): Prints a brief boot summary only if `ser0` is not configured as a KISS/PPP interface in `node.cfg`.
- **Full OYK Portable Core (`ports/compat/`)**: Exposed as an Arduino library (`oyk_compat`) with zero code duplication.

## TinyTMUX Teensy 4.1 Hardware Pinout

Matches the `TinyTMUX` board layout (hardware CTS/RTS flow control on all 8 ports):

| Logical Port | Teensy Serial | IMXRT1060 Peripheral | RX Pin | TX Pin | CTS Pin | RTS Pin |
| --- | --- | --- | --- | --- | --- | --- |
| `ser0` | `Serial1` | `LPUART6` | 0 | 1 | 33 | 6 |
| `ser1` | `Serial2` | `LPUART4` | 7 | 8 | 2 | 9 |
| `ser2` | `Serial6` | `LPUART1` | 25 | 24 | 3 | 26 |
| `ser3` | `Serial7` | `LPUART7` | 28 | 29 | 4 | 27 |
| `ser4` | `Serial8` | `LPUART5` | 34 | 35 | 5 | 36 |
| `ser5` | `Serial3` | `LPUART2` | 15 | 14 | 30 | 41 |
| `ser6` | `Serial4` | `LPUART3` | 16 | 17 | 31 | 40 |
| `ser7` | `Serial5` | `LPUART8` | 21 | 20 | 32 | 22 |

- **Ethernet Link Speed LED**: Pin `18`, driven HIGH for 100 Mbps and LOW for 10 Mbps / link down.
- **Heartbeat LED**: Pin `13`, toggles every 500 ms in `loop()`.
- **Ethernet MAC Address**: Read directly from IMXRT1060 OCOTP fuses (`HW_OCOTP_MAC0` / `HW_OCOTP_MAC1`).

## Building for Teensy 4.1 (`arduino-builder` / Teensyduino 1.62)

Install **Arduino 1.8.19** with **Teensyduino 1.62** (for example in `~/arduino-1.8.19`). CMake automatically fetches **QNEthernet** (`v0.37.0`) via `FetchContent` unless `-DQNETHERNET_DIR=...` or `-DOYK_TINYTMUX_FETCH_QNETHERNET=OFF` (to build with Teensyduino's built-in `NativeEthernet` fallback) is passed:

```bash
cmake -S ports/tinytmux -B build-t41 -DARDUINO_DIR=$HOME/arduino-1.8.19
cmake --build build-t41 -j
```

Or via the root CMake build:

```bash
cmake -B build-tinytmux -DBUILD_TINYTMUX_PORT=ON -DARDUINO_DIR=$HOME/arduino-1.8.19
cmake --build build-tinytmux --target oyk-tinytmux
```

You can also open `ports/tinytmux/oyk_tinytmux/oyk_tinytmux.ino` in the **Arduino 1.8.19 IDE** (`Tools -> Board -> Teensy 4.1`, `Tools -> USB Type -> Dual Serial`, `Tools -> CPU Speed -> 600 MHz`) with `ports/compat` installed/linked as library `oyk_compat`.

Flash `build-t41/oyk-tinytmux.hex` to the Teensy 4.1:

```bash
teensy_loader_cli --mcu=TEENSY41 -w -v build-t41/oyk-tinytmux.hex
```

## Host Simulation & Unit Tests (Linux)

The host unit test target `oyk_tinytmux_tests` compiles the TinyTMUX board pinout, PSRAM probe/self-test, filesystem hooks, USB Host RadioFoundry transport (`usb-cdc-net` and `usb-cdc-serial`), RFC 1661/1662/1332 PPP engine, simultaneous Ethernet + RadioFoundry AXUDP/AXIP/rig control bridge, and ANSI/VT100 operator console natively under strict C++98 and runs under CTest:

```bash
cmake -B build && cmake --build build -j && ctest --test-dir build --output-on-failure -V
```
