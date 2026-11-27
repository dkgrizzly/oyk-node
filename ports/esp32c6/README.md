<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# OYK-Node ESP32-C6 Port (M5Stack Unit C6L)

Strict C++98 / 1,024 B stack-safe embedded packet node port for the **M5Stack Unit C6L** (ESP32-C6 RISC-V 160 MHz, 16 MB SPI Flash, SX1262 LoRa, 64x48 SSD1306 SPI OLED, Wi-Fi 6 + BLE 5.0, WS2812C RGB LED, passive buzzer, and Grove HY2.0-4P UART `ser0`).

## M5Stack Unit C6L Hardware Mapping

| Subsystem | Pins / Interface | Notes |
| --- | --- | --- |
| **Shared SPI2 Bus** | `SCK=GPIO20`, `MOSI=GPIO21`, `MISO=GPIO22` | Shared between SX1262 LoRa & SSD1306 OLED |
| **SX1262 LoRa** | `CS=GPIO23`, `DIO1=GPIO7`, `BUSY=GPIO19` | `PI4IO` `P5=LNA_EN`, `P6=ANT_SW`, `P7=NRST` |
| **SSD1306 64x48 OLED** | `CS=GPIO6`, `DC=GPIO18`, `RST=GPIO15` | 4 status pages cycled by User Button (`P0`) |
| **PI4IOE5V6408 (`0x43`)** | `SDA=GPIO10`, `SCL=GPIO8`, `INT=GPIO3` | User Button + SX1262 RF switch/LNA/reset |
| **WS2812C RGB LED** | `GPIO2` (RMT) | Idle/RX/TX/Error visual status indicator |
| **Passive Buzzer** | `GPIO11` (LEDC PWM) | Boot, incoming connect & alert chirp |
| **Grove Port A (`ser0`)** | `TX=GPIO5`, `RX=GPIO4` | RadioFoundry PPP or external KISS TNC |
| **Console** | USB Serial/JTAG | Dynamic VT340/xterm-256color TUI (`[24..50]x[80..132]`) |

## Building Firmware (ESP-IDF v5.4+)

```bash
source "$HOME/opt/esp-idf/export.sh"
cd ports/esp32c6
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Host Verification (CTest)

```bash
cmake -S . -B build && cmake --build build --target oyk_esp32c6_tests -j
./build/oyk_esp32c6_tests
```
