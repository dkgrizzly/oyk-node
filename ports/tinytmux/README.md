<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# OYK Packet Node — Freescale/NXP i.MX RT1060 (Teensy 4.1 / TinyTMUX) Port

This port targets the **Teensy 4.1** (**NXP/Freescale MIMXRT1062DVJ6A**, ARM Cortex-M7F @ 600 MHz, 1 MB SRAM, 8 MB QSPI Flash, 10/100 Ethernet PHY, USB High-Speed OTG, microSD) on the **TinyTMUX** 8-UART carrier board.

## Features

- **Dual Network Transports (Simultaneous)**:
  - **10/100 Ethernet Jack** (`IMXRT1060 ENET` + `DP83825I` RMII PHY, Pin 18 100 Mbps speed LED) via **lwIP**: AXUDP (UDP), AXIP (IP protocol 93 / RFC 1226), and Incoming Telnet (TCP).
  - **Teensy 4.1 USB Host (`USB_OTG2` / `RHPORT1`)** for **RadioFoundry** radios (direct or via USB hub, `usbh0`..`usbh3`):
    - **USB CDC-ECM / CDC-NCM Network (`usb-cdc-net`)**: Ethernet, ARP, IPv4/UDP, ICMP Echo, AXUDP, AXIP, plus RadioFoundry UDP 4191 zone/channel control.
    - **USB CDC-ACM Serial (`usb-cdc-serial`)**: RFC 1661/1662 PPP or KISS over USB CDC-ACM.
  - **PPP over Serial** (`ser0`..`ser7`) for **RadioFoundry** firmware radios: RFC 1661 LCP, RFC 1662 Async HDLC framing & FCS-16, RFC 1332 IPCP IPv4 negotiation, ICMP Echo Reply (`ping`), carrying both **AXUDP**, **AXIP (IP proto 93)**, and RadioFoundry UDP 4191 control.
  - **KISS Serial TNC** support on any remaining UART (`ser0`..`ser7`).
- **Rig Control (`oyk_rig`)**: Hamlib `rigctld` (TCP 4532) and RadioFoundry / takRF Zone & Channel Control (UDP 4191) across Ethernet, UART PPP, and USB Host transports.
- **FlexSPI2 QSPI PSRAM Startup Detection & Self-Test**: Probes CS0 (`0x70000000`, 8 MB) and CS1 (`0x70800000`, 8 MB) on boot for 0, 1 (8 MB), or 2 (16 MB) Teensy 4.1 PSRAM chips, runs a fast non-blocking multi-pattern memory test, disables any failing chip gracefully while booting normally on internal SRAM, and exposes live diagnostics/re-test (`P`) in the console.
- **Dual USB-CDC ACM + Full VT100 Operator Console** via **TinyUSB** (`USB_OTG1` / `RHPORT0`):
  - `usb0` (`CDC0`): Full 80×24 ANSI/VT100 Operator Console matching the Linux `ncurses`, GTK, Windows, and MS-DOS layouts (`Node` menu with `A`/`D`/`L`/`F` 2-tab Files Area / `R` Radio Control / `S` 5-tab Settings with live `apply_config()` / `T` Terminate / `B` Ban/Unban / `P` PSRAM Diagnostics).
  - `usb1` (`CDC1`): Auxiliary KISS / diagnostic / secondary terminal stream.
- **Full OYK Portable Core (`ports/compat/`)**:
  - AX.25 v2.0 connected-mode L2, persistent `banlist.cfg` & ban appeals, Packet BBS (SSID 11) with sandboxed Files Area & YAPP, and multi-user Packet CHAT (SSID 12) in ~24 KB of RAM.

## TinyTMUX Teensy 4.1 Hardware Pinout

Matches the `TinyTMUX` board layout (XBAR-mapped CTS and hardware RTS flow control on all 8 ports):

| Logical Port | Teensy Serial | IMXRT1060 Peripheral | Base Address | RX Pin | TX Pin | CTS Pin | RTS Pin |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `ser0` | `Serial1` | `LPUART6` | `0x40198000` | 0 | 1 | 33 | 6 |
| `ser1` | `Serial2` | `LPUART4` | `0x40190000` | 7 | 8 | 2 | 9 |
| `ser2` | `Serial6` | `LPUART1` | `0x40184000` | 25 | 24 | 3 | 26 |
| `ser3` | `Serial7` | `LPUART7` | `0x4019C000` | 28 | 29 | 4 | 27 |
| `ser4` | `Serial8` | `LPUART5` | `0x40194000` | 34 | 35 | 5 | 36 |
| `ser5` | `Serial3` | `LPUART2` | `0x40188000` | 15 | 14 | 30 | 41 |
| `ser6` | `Serial4` | `LPUART3` | `0x4018C000` | 16 | 17 | 31 | 40 |
| `ser7` | `Serial5` | `LPUART8` | `0x401A0000` | 21 | 20 | 32 | 22 |

- **Ethernet Link Speed LED**: Pin `18` (`GPIO_AD_B1_01` / `GPIO1_IO17`), driven HIGH for 100 Mbps and LOW for 10 Mbps / link down.
- **Ethernet MAC Address**: Read directly from IMXRT1060 OCOTP fuses (`HW_OCOTP_MAC0` / `HW_OCOTP_MAC1`).

## Dependencies & License Compatibility

- **lwIP** (`LWIP_DIR`): **3-clause BSD License** — compatible with **GPL-3.0-or-later**. Configured via [lwipopts.h](lwipopts.h) with `NO_SYS=1`, `LWIP_RAW=1` (AXIP proto 93), `LWIP_UDP=1` (AXUDP), `LWIP_TCP=1` (Telnet), `LWIP_DHCP=1`, `PPP_SUPPORT=1`, and `PPPOS_SUPPORT=1`.
- **TinyUSB** (`TINYUSB_DIR`): **MIT License** — compatible with **GPL-3.0-or-later**. Configured via [tusb_config.h](tusb_config.h) and [usb_descriptors.c](usb_descriptors.c) for `OPT_MCU_MIMXRT10XX` High-Speed Dual USB-CDC ACM.

## Building for Teensy 4.1 (`arm-none-eabi-gcc`)

Install the GNU Arm Embedded Toolchain (`gcc-arm-none-eabi`), clone `lwip` and `tinyusb`, then build either from the repository root (`-DBUILD_TINYTMUX_PORT=ON`):

```bash
cmake -B build-tinytmux -DBUILD_TINYTMUX_PORT=ON
cmake --build build-tinytmux --target oyk-tinytmux
```

Or invoke [ports/tinytmux/CMakeLists.txt](CMakeLists.txt) directly when passing custom `LWIP_DIR` / `TINYUSB_DIR` paths:

```bash
cmake -S ports/tinytmux -B build-tinytmux \
      -DCMAKE_TOOLCHAIN_FILE=ports/tinytmux/arm-imxrt1062.cmake \
      -DLWIP_DIR=/path/to/lwip \
      -DTINYUSB_DIR=/path/to/tinyusb
cmake --build build-tinytmux -j
```

Flash `build-tinytmux/oyk-tinytmux.hex` to the Teensy 4.1 with `teensy_loader_cli`:

```bash
teensy_loader_cli --mcu=TEENSY41 -w -v build-tinytmux/oyk-tinytmux.hex
```

## Host Simulation & Unit Tests (Linux)

The host unit test target `oyk_tinytmux_tests` compiles the TinyTMUX board pinout, FlexSPI2 QSPI PSRAM probe/self-test, TinyUSB Host RadioFoundry transport (`usb-cdc-net` and `usb-cdc-serial`), RFC 1661/1662/1332 PPP engine, simultaneous Ethernet + RadioFoundry AXUDP/AXIP/rig control bridge, and ANSI/VT100 operator console natively under strict C++98 and runs under CTest:

```bash
cmake -B build && cmake --build build -j && ctest --test-dir build --output-on-failure -V
```
