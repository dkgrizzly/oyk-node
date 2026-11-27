/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_ESP32C6_CONFIG_H
#define OYK_ESP32C6_CONFIG_H

/*
 * Hardware profile & verified schematic pinout for M5Stack Unit C6L:
 *   - SoC       : ESP32-C6 (RISC-V 160 MHz + 20 MHz LP, 16 MB SPI Flash, 512 KB HP SRAM)
 *   - Wi-Fi/BLE : 2.4 GHz Wi-Fi 6 (802.11ax) + BLE 5.0 + IEEE 802.15.4
 *   - Shared SPI: SPI2 SCK=20, MOSI=21, MISO=22
 *   - SX1262    : CS(NSS)=23, DIO1=7, BUSY=19; Reset/LNA/ANT via PI4IOE5V6408 (0x43)
 *   - SSD1306   : 64x48 SPI OLED CS=6, DC=18, RST=15
 *   - I2C PI4IO : SDA=10, SCL=8, INT=3, Addr=0x43 (P0=Btn, P5=LNA_EN, P6=ANT_SW, P7=NRST)
 *   - RGB LED   : WS2812C on GPIO 2
 *   - Buzzer    : Passive PWM buzzer on GPIO 11
 *   - Grove A   : HY2.0-4P UART ser0 TX=5, RX=4
 */

#ifndef OYK_ESP32C6_PORT
#define OYK_ESP32C6_PORT 1
#endif

#ifndef OYK_ESP32C6_BOARD_ID
#define OYK_ESP32C6_BOARD_ID "m5stack_unit_c6l"
#endif

#ifndef OYK_ESP32C6_UART_COUNT
#define OYK_ESP32C6_UART_COUNT 1
#endif

/* Shared SPI2 Bus (SX1262 LoRa + SSD1306 OLED) */
#define OYK_C6L_SPI_SCK_PIN    20U
#define OYK_C6L_SPI_MOSI_PIN   21U
#define OYK_C6L_SPI_MISO_PIN   22U

/* SX1262 LoRa Transceiver */
#define OYK_C6L_LORA_CS_PIN    23U
#define OYK_C6L_LORA_DIO1_PIN  7U
#define OYK_C6L_LORA_BUSY_PIN  19U

/* 64x48 Monochrome SSD1306 SPI OLED */
#define OYK_C6L_OLED_CS_PIN    6U
#define OYK_C6L_OLED_DC_PIN    18U
#define OYK_C6L_OLED_RST_PIN   15U
#define OYK_C6L_OLED_WIDTH     64
#define OYK_C6L_OLED_HEIGHT    48

/* Internal I2C Bus & PI4IOE5V6408 I/O Expander */
#define OYK_C6L_I2C_SDA_PIN    10U
#define OYK_C6L_I2C_SCL_PIN    8U
#define OYK_C6L_PI4IO_INT_PIN  3U
#define OYK_C6L_PI4IO_ADDR     0x43U
#define OYK_C6L_PI4IO_BTN_BIT  0x01U /* P0: Active-low User Button */
#define OYK_C6L_PI4IO_LNA_BIT  0x20U /* P5: SX1262 LNA_EN */
#define OYK_C6L_PI4IO_ANT_BIT  0x40U /* P6: SX1262 ANT_SW */
#define OYK_C6L_PI4IO_NRST_BIT 0x80U /* P7: SX1262 NRST (active-low) */

/* Onboard Peripherals */
#define OYK_C6L_RGB_LED_PIN    2U
#define OYK_C6L_BUZZER_PIN     11U

/* External HY2.0-4P Grove Port A (ser0 UART) */
#define OYK_C6L_GROVE_TX_PIN   5U
#define OYK_C6L_GROVE_RX_PIN   4U

#ifndef OYK_FLASHMEM
#define OYK_FLASHMEM
#endif

#endif /* OYK_ESP32C6_CONFIG_H */
