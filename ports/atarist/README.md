<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Atari ST / STE / TT / Falcon GEM Port (`OYKNODE.PRG`)

Native Motorola 68000 C++98 GEM AES/VDI port of **oyk-node** for Atari ST/STE (SM124 640x400 Mono), Atari TT/Falcon, and extended VDI modes (640x480 16-color).

## Features

- **GEM AES/VDI Native Chrome**: Top menu bar, full screen-filling console workspace, floating modals with drop shadows and `[X]` close button, all 7 Settings tabs (`1:Node`..`7:Prn`), and all 13 `Node` menu actions.
- **OS-Native Default Palette & Whole-Screen Preview**: Queries native VDI registers via `vq_color()`, defaults to `"native"`, and repaints the full workspace and active modal live across `"native"`, `"dark"`, `"light"`, and custom RGB edits (updating VDI pens via `vs_color()` in 16-color modes and high-contrast 1-bit inversion in ST High Mono).
- **Hardware & Network Support**: Runtime TOS cookie detection (`'MiNT'` for MiNTnet, `'STiK'` for STinG/STiK, `'_MCH'` machine ID) plus Atari BIOS RS-232 (`AUX:` MFP/SCC) KISS serial support.

## Building

```sh
make -C ports/atarist
```

Or via CMake:

```sh
cmake -S . -B build -DBUILD_ATARIST_PORT=ON
cmake --build build -j
```

## Headless EmuTOS Tests & Dual UI Gallery Captures (`Hatari`)

```sh
./ports/atarist/run_hatari_test.sh --unit-tests
./ports/atarist/run_hatari_test.sh --capture-mono /tmp/at_mono
./ports/atarist/run_hatari_test.sh --capture-color /tmp/at_color
```
