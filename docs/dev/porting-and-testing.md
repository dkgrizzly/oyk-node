# Porting & Testing Guide

## Portable C++98 Core (`ports/compat/`)

To support 16-bit MS-DOS (IBM XT 8088 / NEC V20, 640KB RAM) and 16/32/64-bit Windows (Windows 3.1 through Windows 11) without STL/exception/Vista-runtime bloat, a self-contained C++98 core lives in:
- [ports/compat/oyk_portable.hpp](../../ports/compat/oyk_portable.hpp)
- [ports/compat/oyk_portable.cpp](../../ports/compat/oyk_portable.cpp)

Design rules for `ports/compat/`:
- Strict C++98 (no C++11+ features, no STL containers, no exceptions, no RTTI).
- Fixed-width integer aliases (`oyk_u8`, `oyk_u16`, `oyk_u32`) safe when `sizeof(int) == 2`.
- Static bounded tables (`OYK_MAX_SESSIONS`, `OYK_MAX_BANS`, `OYK_MAX_CONSOLE_LOGS`, etc.) sized for 640KB conventional RAM.
- Cooperative single-threaded polling (`on_rx_frame()`, `tick()`, `OykTxCallback`).
- Binary-mode config I/O (`"rb"`/`"wb"`) accepting `CRLF`, `CR`, and `LF`, defaulting to `CRLF` on DOS/Windows.

## Platform Build Options

| Target | CMake Option | Default | Notes |
| --- | --- | --- | --- |
| Linux ncurses (`oyk-node`) | `BUILD_NCURSES_UI` | `ON` | Requires `libncurses-dev` |
| X11 GTK+ 2.0 (`oyk-node-gtk`) | `BUILD_GTK2_UI` | `ON` (auto) | Built automatically if `pkg-config` finds `gtk+-2.0` |
| MS-DOS (`oyk-node-dos`, `oyk-dos-tests-build`) | `BUILD_MSDOS_PORT` | `OFF` | OpenWatcom v2/1.9 + mTCP (`-DMTCP_DIR=~/mTCP`) |
| Windows 3.1 16-bit (`oyk-node-win16`) | `BUILD_WIN16_PORT` | `OFF` | OpenWatcom `wmake -f makefile.win16` |
| Windows 32/64-bit (`oyk-node-win`) | `BUILD_WINDOWS_PORT` | `OFF` | MinGW-w64, MSVC, or OpenWatcom `makefile.win32` |

## Running Tests

### Native Linux / C++20 & Strict C++98 Suites
```bash
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure -V
```
This runs:
1. `oyk_node_tests` ([tests/test_node.cpp](../../tests/test_node.cpp)) — 13 end-to-end and unit test groups covering AX.25, CRC-16, KISS, AXUDP, AXIP, KISS Serial (real PTY), KISS TCP (client+server), Telnet (with cross-port AXUDP connect), logging & daily rotation, banlist format & flags, restricted ban/appeal sessions, and config EOL preservation.
2. `oyk_portable_tests` ([tests/test_portable.cpp](../../tests/test_portable.cpp)) — compiled in strict `-std=c++98 -Wall -Wextra -Wpedantic` mode verifying the shared DOS/Windows portable core.

### 16-Bit MS-DOS Unit Tests in Headless QEMU
```bash
export WATCOM=$HOME/open-watcom-v2/rel
export PATH=$WATCOM/binl64:$PATH
export INCLUDE=$WATCOM/h

cmake -B build \
  -DBUILD_MSDOS_PORT=ON \
  -DMTCP_DIR=$HOME/mTCP \
  -DFREEDOS_BOOT_IMG=/path/to/freedos_boot.img
cmake --build build --target oyk-dos-tests-build
ctest --test-dir build -R oyk_dos_qemu_tests --output-on-failure -V
```
See [ports/dos/README.md](../../ports/dos/README.md) for full FreeDOS / EMS 4.0 / mTCP setup details.
