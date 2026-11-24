<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# MS-DOS Port (IBM XT 8088 / NEC V20, 640KB RAM + Optional EMS 4.0)

This port builds `oyknode.exe` and the 16-bit unit test binary `testdos.exe` for real-mode MS-DOS using **OpenWatcom v2** (or **OpenWatcom 1.9**) and **Michael Brutman's mTCP** TCP/IP stack.

## Hardware & Software Targets

- **CPU**: Intel 8088 / 8086 (`CPU=-0`, default) or NEC V20 / 80186 (`CPU=-1`).
- **RAM**: Designed for 640KB conventional RAM using fixed-size static tables (no STL, no exceptions, no RTTI) and Large model (`-ml -os`).
- **Optional EMS 4.0**: Automatically detects LIM EMS 4.0 (`INT 67h` / `EMMXXXX0`) at startup and allocates 4 logical 16KB pages (64KB) when available; runs cleanly without EMS if no driver/board is present.
- **Networking**: Uses Michael Brutman's mTCP (`MTCPCFG` + Crynwr Packet Driver).
- **Config files**: Accepts CRLF, CR, and LF line endings, defaults to CRLF when writing new config files, and preserves the original line ending style (with `node.cfg`'s first line ending taking precedence over mixed line endings).

## Building on Linux with OpenWatcom v2

Once `~/open-watcom-v2` finishes building:

```bash
export WATCOM=$HOME/open-watcom-v2/rel
export PATH=$WATCOM/binl64:$PATH
export INCLUDE=$WATCOM/h

# Build 16-bit MS-DOS unit tests (standalone, no mTCP required):
cd ports/dos
wmake -f makefile testdos.exe

# Build full MS-DOS packet node linked with ~/mTCP:
wmake -f makefile oyknode.exe MTCP_DIR=$HOME/mTCP

# Optional NEC V20 optimization:
wmake -f makefile all MTCP_DIR=$HOME/mTCP CPU=-1
```

## Running 16-Bit MS-DOS Unit Tests in `qemu-system-i386`

`run_qemu_tests.sh` runs `testdos.exe` headlessly inside `qemu-system-i386` using `mtools` (`mformat`, `mcopy`, `mtype`) — no root privileges, no loop mounts, and no X11 display required.

### 1. Run Standalone (Auto-Offers FreeDOS Download)
When run interactively without `--boot-img`, the script offers to download the official FreeDOS 1.4 Floppy Edition archive into `ports/dos/freedos/` (excluded via `.gitignore`) and extract `x86BOOT.img` automatically:

```bash
./ports/dos/run_qemu_tests.sh
```

Or download non-interactively in one step:

```bash
./ports/dos/run_qemu_tests.sh --download-freedos
```

### 2. Or Run via CMake + CTest
Once `ports/dos/freedos/x86BOOT.img` exists (or via `-DFREEDOS_BOOT_IMG=...`):

```bash
cmake -B build -DBUILD_MSDOS_PORT=ON -DMTCP_DIR=$HOME/mTCP
cmake --build build --target oyk-dos-tests-build
ctest --test-dir build -R oyk_dos_qemu_tests --output-on-failure -V
```
