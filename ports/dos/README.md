<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# MS-DOS Port (IBM XT 8088 / NEC V20, 640KB RAM + Optional EMS 4.0)

This port builds `oyknode.exe` and the 16-bit unit test binary `testdos.exe` for real-mode MS-DOS using **OpenWatcom v2** (or **OpenWatcom 1.9**).

## Optional Stacks (No Hard Build or Runtime Dependencies)

Neither **Michael Brutman's mTCP** nor **Bret Johnson's DOS USB Stack (`USBDOS` / `USBUHCI` / `USBUHCIL`)** is a hard requirement to build or run `oyknode.exe`:

- **Standalone build (`USE_MTCP=0`, or when `mTCP` is absent)**: Builds `oyknode.exe` with zero mTCP source files or headers. Includes a compact built-in RFC 1661/1662 PPP + IPv4/ARP/UDP mini-stack, Crynwr Packet Driver (`INT 60h..7Fh`) probe, and direct `COM1`–`COM4` UART / BIOS `INT 14h` KISS & PPP serial transport.
- **Optional mTCP build (`USE_MTCP=1`, auto-enabled when `MTCP_DIR/src/TCPLIB/PACKET.CPP` exists)**: Links mTCP for full TCP/IP support (AXUDP, Hamlib `rigctld` TCP 4532, RadioFoundry UDP 4191). If `MTCPCFG` or a Packet Driver is missing at runtime, `oyknode.exe` logs a warning and continues running cleanly with available transports.
- **Optional Bret Johnson DOS USB Stack (`USBUHCI` / `USBUHCIL`)**: Probed dynamically at runtime via AMIS `INT 2Dh` (no external USB SDK headers needed at build time). Supports RadioFoundry radios over:
  - **USB CDC Network (`usb-cdc-net`)**: CDC-ECM / CDC-NCM Ethernet frames (via USBDOS + Crynwr Packet Driver or mTCP).
  - **USB CDC Serial (`usb-cdc-serial`)**: CDC-ACM virtual serial (`USBCOM1` / `INT 14h` or `COM1`–`COM4`) carrying RFC 1661/1662 PPP (AXUDP + RadioFoundry UDP 4191) or KISS serial (`kiss-serial`).

## Hardware & Software Targets

- **CPU**: Intel 8088 / 8086 (`CPU=-0`, default) or NEC V20 / 80186 (`CPU=-1`).
- **RAM**: Designed for 640KB conventional RAM using fixed-size static tables (no STL, no exceptions, no RTTI) and Large model (`-ml -os`).
- **Optional EMS 4.0**: Automatically detects LIM EMS 4.0 (`INT 67h` / `EMMXXXX0`) at startup and allocates 4 logical 16KB pages (64KB) when available; runs cleanly without EMS if no driver/board is present.
- **Rig Control**: Supports Hamlib `rigctld` (TCP 4532, when mTCP is active) and RadioFoundry / takRF Zone & Channel Control (UDP 4191, over mTCP, Packet Driver, or USB CDC PPP).
- **Config files**: Accepts CRLF, CR, and LF line endings, defaults to CRLF when writing new config files, and preserves the original line ending style (with `node.cfg`'s first line ending taking precedence over mixed line endings).

## Building on Linux with OpenWatcom v2

```bash
export WATCOM=/opt/watcom
export PATH=$WATCOM/binl64:$PATH
export INCLUDE=$WATCOM/h

cd ports/dos

# 1. Build standalone oyknode.exe with ZERO mTCP dependency:
wmake -f makefile oyknode.exe USE_MTCP=0

# 2. Build oyknode.exe with optional mTCP support (if ~/mTCP is cloned):
wmake -f makefile oyknode.exe MTCP_DIR=$HOME/mTCP USE_MTCP=1

# 3. Build 16-bit MS-DOS unit tests (standalone, no mTCP required):
wmake -f makefile testdos.exe

# Optional NEC V20 optimization:
wmake -f makefile all USE_MTCP=0 CPU=-1
```

On case-sensitive Linux filesystems, `makefile` automatically creates a temporary `mtcp_inc/` symlink directory (excluded via `.gitignore`) mapping lowercase/mixed-case `#include` names used inside mTCP's headers (`types.h`, `Global.Cfg`, etc.) to the uppercase files in `~/mTCP/src/TCPINC/` without modifying `~/mTCP`.

## Running 16-Bit MS-DOS Unit Tests in `qemu-system-i386`

`run_qemu_tests.sh` runs `testdos.exe` headlessly inside `qemu-system-i386` using `mtools` (`mformat`, `mcopy`, `mtype`) — no root privileges, no loop mounts, and no X11 display required.

```bash
./ports/dos/run_qemu_tests.sh
```
