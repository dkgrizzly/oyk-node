<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# OYK Packet Node — TI-99/4A Port (`ports/ti99/`)

Strict C99 / 16-bit big-endian (`TMS9900`) port targeting expanded **TI-99/4A** systems.

## Required Hardware Baseline

- **TI-99/4A Console** + **32 KB Expansion RAM**
- **SAMS 1 MB Memory Expansion** (`CRU >1E00`, 256 x 4 KB banked pages mapped at `>4000–>401E`)
- **FinalGROM99** banked 8 KB cartridge image (`>6000` header + C runtime trampoline)
- **TIPI** (`PI.UDP` / `PI.TCP` / `TIPI.` storage DSR) and/or **TI RS-232** (`TMS9902` KISS TNC)
- **F18A** VGA replacement VDP (80x24 color text auto-detected) with automatic **TMS9918A** 40x24 fallback

## Building

```bash
# Host strict C99 verification & 23-screen F18A/9918 TUI test
make -C ports/ti99 test

# Cartridge cross-build (requires tms9900-gcc + xdt99 xas99.py)
make -C ports/ti99 cart
```
