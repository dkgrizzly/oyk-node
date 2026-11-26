<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# OYK-Node for Classic Macintosh (System 6.0.8 – Mac OS 9.2.2)

Native **68k + PowerPC Fat Application** built with Retro68 (`m68k-apple-macos` +
`powerpc-apple-macos` PEF), bringing full OYK-Node feature parity and shared
`oyk_gui` / `oyk_term` workflows to vintage Macintosh hardware.

## Supported Systems & Transports

- **OS Range**: Macintosh System Software 6.0.8 through Mac OS 9.2.2
- **Display Adaptation**:
  - **512x342 1-bit Monochrome QuickDraw**: Macintosh Plus, SE, Classic
  - **640x480+ Color QuickDraw (8-bit / Direct Color)**: Macintosh II, Quadra, PowerBook, Power Macintosh (with OYK Themes)
- **Networking & Serial Drivers (runtime auto-detected)**:
  - **Macintosh Serial Driver** (`.AIn/.AOut` Modem port, `.BIn/.BOut` Printer port) for KISS TNC frames & CAT control
  - **MacTCP (`.IPP` driver)** for AXUDP & TCP/IP
  - **Open Transport (`gestaltOpenTpt`)** on System 7.5+ / OS 8–9 / PowerPC, falling back gracefully to MacTCP or Serial-only KISS

## Building

```sh
cd ports/macclassic
make RETRO68_PREFIX="$HOME/opt/Retro68" all
```

Outputs `build_macclassic/OYKNode-Fat.bin` (MacBinary II) and `build_macclassic/OYKNode-Fat.dsk` (boot-ready HFS image), plus `OYKTest.bin` (68k unit test runner).

## Automated Headless Verification (Mini vMac)

```sh
./ports/macclassic/run_minivmac_test.sh --unit-tests
./ports/macclassic/run_minivmac_test.sh --capture-bw /tmp/mac_bw
./ports/macclassic/run_minivmac_test.sh --capture-color /tmp/mac_color
```
