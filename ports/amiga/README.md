<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Amiga Workbench Port (`OYKNode`)

Native Motorola 68000 C++98 Workbench port of **oyk-node** targeting **AmigaOS 3.1** (v39/v40) with clean runtime compatibility down to **Kickstart/Workbench 2.04** (v36-38) and **Kickstart/Workbench 1.3** (v34), plus **AROS m68k**.

## Features & Platform Policy

- **Workbench Default (Private Screen Disabled by Default)**: Opens cleanly on the public `Workbench` screen by default (`amiga_private_screen = 0`). Operators may optionally enable a dedicated private screen via `[node] amiga_private_screen = 1` in `NODE.CFG`, CLI `--private-screen`, or the Workbench icon ToolType `PRIVATESCREEN`.
- **Adaptive OS-Native Theme Policy**:
  - When the active screen has **fewer than 32 colors** (`depth < 5`, such as standard 4-color or 16-color Workbench), the UI automatically locks to the **OS native Workbench pens** (`DrawInfo` / `ViewPort->ColorMap`) so shared palette pens are respected cleanly.
  - When the active screen has **32 or more colors** (`depth >= 5`, 32/64-color AGA or RTG), OYK theme presets (`native`, `dark`, `light`, `green`, `custom`) unlock with whole-screen live preview (`SetRGB32` on KS 3.1+, `SetRGB4` on KS 1.3/2.04).
- **Kickstart Runtime Adaptation**:
  - **OS 3.1 (`>= 39`)**: `LockPubScreen`, `GadTools`, `GetBitMapAttr`, `GetRGB32`/`SetRGB32`, AGA 32/64+ color screens.
  - **OS 2.04 (`36..38`)**: `LockPubScreen`, `GadTools` visual info/bevels, `GetRGB4`/`SetRGB4`.
  - **OS 1.3 (`< 36`)**: Pure Intuition `OpenScreen`/`GetScreenData`/`OpenWindow`, Intuition menus & bevel drawing without requiring `gadtools.library`.
- **Networking & Serial**: Runtime probe for `bsdsocket.library` (AmiTCP/Roadshow/UAE) and `serial.device` KISS serial ports.

## Building

```sh
make -C ports/amiga
```

Or via CMake:

```sh
cmake -S . -B build -DBUILD_AMIGA_PORT=ON
cmake --build build -j
```

## Headless AROS Tests & UI Gallery Captures (`FS-UAE`)

```sh
./ports/amiga/run_fsuae_test.sh --unit-tests
./ports/amiga/run_fsuae_test.sh --capture /tmp/amiga_caps
```
