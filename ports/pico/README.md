<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# OYK-Node Raspberry Pi Pico (RP2040 / RP2350) Port

Embedded firmware port based on the Teensy TinyTMUX architecture targeting:

| Board (`PICO_BOARD`) | MCU | Flash | SRAM | PSRAM | Wireless / Bluetooth | Output UF2 |
|---|---|---|---|---|---|---|
| `pico_w` | RP2040 | 2 MB | 264 KB | None | Onboard CYW43439 | `oyk-pico-pico_w.uf2` |
| `pico2_w` | RP2350A | 4 MB | 520 KB | None | Onboard CYW43439 | `oyk-pico-pico2_w.uf2` |
| `waveshare_core2350b` | RP2350B | 16 MB | 520 KB | Auto (up to 8 MB, QMI CS=47) | Optional external RM2 (`CYW43_PIN_WL_DYNAMIC`) | `oyk-pico-waveshare_core2350b.uf2` |
| `pimoroni_pga2350` | RP2350B | 16 MB | 520 KB | 8 MB (QMI CS=47) | Optional external RM2 (`CYW43_PIN_WL_DYNAMIC`) | `oyk-pico-pimoroni_pga2350.uf2` |
| `picocalc_pico2_w` | RP2350A | 4 MB | 520 KB | 8 MB carrier SPI (`CS=GP20`) | Onboard CYW43439 | `oyk-pico-picocalc_pico2_w.uf2` |
| `picocalc_pico_w` | RP2040 | 2 MB | 264 KB | 8 MB carrier SPI (`CS=GP20`) | Onboard CYW43439 (16-color VT100) | `oyk-pico-picocalc_pico_w.uf2` |
| `picocalc_pico2` | RP2350A | 4 MB | 520 KB | 8 MB carrier SPI (`CS=GP20`) | None (`ser0` UART0 only) | `oyk-pico-picocalc_pico2.uf2` |
| `picocalc_pico` | RP2040 | 2 MB | 264 KB | 8 MB carrier SPI (`CS=GP20`) | None (`ser0` UART0 only, 16-color VT100) | `oyk-pico-picocalc_pico.uf2` |

## Hardware Capabilities

- **Operator Console**: USB-CDC serial (`115200` / virtual CDC) on standard Pico boards, OR standalone **320x320 IPS LCD (`SPI1` GP10..GP15, ST7365P/ILI9488) + STM32F103 I2C1 QWERTY keyboard (`0x1F`, GP6/7) + dual-PWM BEL audio (`GP26`/`GP27`)** on ClockworkPi PicoCalc targets powered by the `third_party/oyk-vt100` (`dkgrizzly/oyk-vt100`) embedded VT100 engine (`4x8` 80x40 default, `Ctrl+Alt+F` toggle to `6x10` 53x32 zoomed with `Alt+H`/`Alt+L` horizontal panning, `Alt+Up/Down/Left/Right` LCD/keyboard backlights, and live STM32 `BAT:<n>%` status). Supports DA1/DA2 terminal inquiry upgrading dynamically across `VT100`, `VT220`, `VT340`, or `xterm-256color`, full theme support (`themes.cfg`), 7-tab Settings, Add Interface 4-step wizard, Edit Interface, Edit Peer, Terminate, Ban/Unban, Storage Manager (`FORMAT` confirmation), PSRAM Diagnostics, and `BOOTSEL` UF2 reboot (`reset_usb_boot`).
- **Hardware UARTs**: Standard Pico boards expose 2 UARTs (`ser0` = `uart0` TX=0/RX=1/CTS=2/RTS=3, `ser1` = `uart1` TX=4/RX=5/CTS=6/RTS=7). ClockworkPi PicoCalc targets expose `ser0` (`uart0` TX=0/RX=1 without hardware CTS/RTS) because `GP2..GP5` are wired to onboard SPI PSRAM and `GP6..GP7` to I2C1.
- **Optional Raspberry Pi RM2 Radio (`waveshare_core2350b` & `pimoroni_pga2350`)**: Dynamically configures CYW43 SPI pins (`REG_ON=23, DATA=24, CS=25, CLK=29`). If the RM2 module is not attached, initialization degrades cleanly with non-fatal status `"RM2 ABSENT (non-fatal)"` while USB console, UART0/1, PSRAM, and storage continue running normally.
- **Storage & PSRAM**: Optional SPI0 microSD (`SCK=18, TX=19, RX=16, CS=17`) falling back cleanly to a QSPI flash partition (`flash_safe_execute`), plus RP2350 QMI PSRAM with SRAM fallback.

## Building

Opt-in CMake option `BUILD_PICO_PORT` (default `OFF`):

```bash
cmake -S . -B build-pico-host -DBUILD_PICO_PORT=ON
cmake --build build-pico-host --target oyk-pico -j
```

Or build a single board directly:

```bash
cmake -S ports/pico -B build-core2350b -DPICO_BOARD=waveshare_core2350b
cmake --build build-core2350b -j
```
