<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# OYK-Node Luckfox Lyra Port (Rockchip RK3506 RT-Thread SMP + PicoCalc)

Strict C++98 / `<= 1,024 B` stack-safe embedded packet node port for **Luckfox Lyra** (`RK3506G2` / `RK3506B`, triple ARM Cortex-A7 @ 1.2 GHz + Cortex-M0) running **RT-Thread SMP + Rockchip `rk-hal`**, supporting both standalone boards and **ClockworkPi PicoCalc** drop-in replacement builds.

## Supported Board Profiles (7)

| Board ID | SoC / RAM | SPI NAND | Network Transport | Console / UI | UARTs |
| --- | --- | --- | --- | --- | --- |
| `lyra` | RK3506G2 (128 MB) | None | USB OTG RNDIS + PPP (`ser0`) | `uart0` / USB VT100 (`[24..50]x[80..132]`) | `ser0`, `ser1` |
| `lyra_b` | RK3506G2 (128 MB) | 256 MB | USB OTG RNDIS + PPP (`ser0`) | `uart0` / USB VT100 (`[24..50]x[80..132]`) | `ser0`, `ser1` |
| `lyra_plus` | RK3506G2 (128 MB) | 256 MB | 10/100 RMII Ethernet (`e0`) | `uart0` / USB VT100 (`[24..50]x[80..132]`) | `ser0`, `ser1` |
| `lyra_zero_w` | RK3506B (512 MB) | 256 MB | Wi-Fi 6 + BLE 5.2 (`w0` / `ble0`) | `uart0` / USB VT100 (`[24..50]x[80..132]`) | `ser0`, `ser1` |
| `picocalc_lyra` | RK3506G2 (128 MB) | None | USB OTG RNDIS + PPP (`ser0`) | 320x320 LCD (`SPI0`) + I2C2 KBD (`0x1F`) | `ser0` |
| `picocalc_lyra_b` | RK3506G2 (128 MB) | 256 MB | USB OTG RNDIS + PPP (`ser0`) | 320x320 LCD (`SPI0`) + I2C2 KBD (`0x1F`) | `ser0` |
| `picocalc_lyra_plus` | RK3506G2 (128 MB) | 256 MB | 10/100 RMII Ethernet (`e0`) | 320x320 LCD (`SPI0`) + I2C2 KBD (`0x1F`) | `ser0` |

## RK3506G2 / PicoCalc Hardware Mapping

- **320x320 ILI9488/ST7365P LCD**: `SPI0` Mode 0 (up to 80 MHz), `CS=GPIO0_PA4`, `DC=GPIO0_PA3`, `RST=GPIO0_PA2`, driven via `third_party/oyk-vt100` (`4x8` 80x40 default, `Ctrl+Alt+F` toggle to `6x10` 53x32 zoomed with `Ctrl+Alt+Left/Right` or `Alt+H/L` panning).
- **STM32F103 Southbridge**: `I2C2` (`0x1F`, 400 kHz, `SCL=GPIO0_PB2`, `SDA=GPIO0_PB3`) providing QWERTY matrix FIFO (`0x09`), LCD/KBD backlight (`0x05`/`0x0A`), battery telemetry (`0x0B` -> `BAT:<n>%` banner), and clean power-off timer (`0x0E`). Sole local OYK-Node console on `picocalc_lyra*` profiles (`uart0` stays free for RT-Thread FinSH / boot logs).
- **Audio PWM (`RM_IO12`)**: Disabled by default (`OYK_LYRA_ENABLE_AUDIO=0` / `PKG_OYK_NODE_PICOCALC_AUDIO=n`) to prevent baseline electrical crackle on unmodified Lyra + PicoCalc hardware; opt-in via Kconfig for hardware-modded units.
- **Storage**: RT-Thread `DFS` mounts MicroSD (`/sd`, ELM FatFS) first and falls back to 256 MB SPI NAND (`/flash`, LittleFS) on boards with onboard NAND, with explicit `FORMAT` confirmation protection.

## Building & Verification

### Full RT-Thread SMP Firmware (`RK3506_BSP_ROOT` + `scons`)

```bash
# Link or reference ports/lyra inside your RK3506 RT-Thread BSP packages tree,
# select PKG_USING_OYK_NODE_LYRA in menuconfig, then:
cmake -S . -B build-lyra -DBUILD_LYRA_PORT=ON -DRK3506_BSP_ROOT=/path/to/sdk-bsp-rk3506
cmake --build build-lyra --target oyk_lyra_rtthread -j
```

### Offline ARMv7 Cortex-A7 Cross-Compile & Host Parity Verification

```bash
# Host unit tests & 23-screen PicoCalc/standalone parity checks:
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure

# ARMv7 Cortex-A7 (-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard) cross-compile + <=1024 B stack check:
cmake -S . -B build-lyra -DBUILD_LYRA_PORT=ON
cmake --build build-lyra --target oyk_lyra_arm_verify -j
```
