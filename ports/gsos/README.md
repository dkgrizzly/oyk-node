<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Apple IIGS GS/OS Port (`OYKNODE.SYS16`)

Native 65C816 GS/OS (`$B3` OMF v2 S16) port of **oyk-node**, modeled on the Classic Macintosh port (`ports/macclassic/`) and rendered via QuickDraw II in Super Hi-Res 640x200 mode.

## Features

- **Native Menu Bar & Desktop Chrome**: Top GS/OS menu bar, floating modals with drop shadows and `[X]` close box, all 7 canonical Settings tabs (`Node`, `BBS`, `CHAT`, `Ifaces`, `Peers`, `Rig`, `Prn`), and all 13 `Node` actions.
- **OS-Native Default Theme & Whole-Screen Live Preview**: Defaults to `"native"` Apple IIGS Finder/Platinum palette and repaints the entire screen (menu bar, desktop console, foreground modal, and per-scanline SCB role preview swatches) live across `"native"`, `"dark"`, `"light"`, and custom RGB edits.
- **Hardware & Transport Support**:
  - Runtime detection of **Marinetti TCP/IP** (User Toolset `#54` via `GetTSPtr`)
  - Built-in **Zilog 8530 SCC** serial KISS ports (`.A` Modem / `.B` Printer)
  - Graceful fallback when Marinetti is not loaded.
- **Automated Headless Testing & UI Gallery**: Uses `vbcc` 65816 (`vc +iigs`) and headless `GSplus` (`Xvfb`) with `ports/gsos/mkprodos.py` for ProDOS image injection and Super Hi-Res BMP extraction.

## Building

```sh
make -C ports/gsos
```

Or via CMake:

```sh
cmake -S . -B build -DBUILD_GSOS_PORT=ON
cmake --build build -j
```

## Headless Emulator Tests & UI Gallery Captures

Ensure `OYK_GSPLUS_DIR` (or `~/gsplus`) contains `ROM.01`, `System.Disk.po`, and `gsplus/gsplus`:

```sh
./ports/gsos/run_gsplus_test.sh --unit-tests
./ports/gsos/run_gsplus_test.sh --capture /tmp/gsos_caps
```
