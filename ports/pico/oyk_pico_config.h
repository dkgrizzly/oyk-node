/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_PICO_CONFIG_H
#define OYK_PICO_CONFIG_H

/*
 * Feature and target configuration macros for the Raspberry Pi Pico
 * (RP2040 & RP2350) port supporting:
 *   - pico_w               (RP2040, onboard CYW43439 Wi-Fi + BLE/BT Classic)
 *   - pico2_w              (RP2350A, onboard CYW43439 Wi-Fi + BLE/BT Classic)
 *   - waveshare_core2350b  (RP2350B, optional RM2 CYW43 module + QMI PSRAM + 16MB Flash)
 *   - pimoroni_pga2350     (RP2350B, optional RM2 CYW43 module + 8MB PSRAM + 16MB Flash)
 */

#ifndef OYK_PICO_PORT
#define OYK_PICO_PORT 1
#endif

#ifndef OYK_PICO_UART_COUNT
#define OYK_PICO_UART_COUNT 2
#endif

#ifndef OYK_PICO_USB_CDC_COUNT
#define OYK_PICO_USB_CDC_COUNT 1
#endif

/* Identify selected Pico SDK board at compile time (overridable in host tests) */
#ifndef OYK_PICO_BOARD_ID
  #if defined(RASPBERRYPI_PICO_W)
    #define OYK_PICO_BOARD_ID "pico_w"
  #elif defined(RASPBERRYPI_PICO2_W)
    #define OYK_PICO_BOARD_ID "pico2_w"
  #elif defined(WAVESHARE_CORE2350B)
    #define OYK_PICO_BOARD_ID "waveshare_core2350b"
  #elif defined(PIMORONI_PGA2350)
    #define OYK_PICO_BOARD_ID "pimoroni_pga2350"
  #else
    #define OYK_PICO_BOARD_ID "pico2_w"
  #endif
#endif

/* Default Raspberry Pi RM2 external wiring for RP2350B carriers (configurable/optional) */
#ifndef OYK_RM2_DEFAULT_REG_ON_PIN
#define OYK_RM2_DEFAULT_REG_ON_PIN 23U
#endif
#ifndef OYK_RM2_DEFAULT_DATA_PIN
#define OYK_RM2_DEFAULT_DATA_PIN 24U
#endif
#ifndef OYK_RM2_DEFAULT_HOST_WAKE_PIN
#define OYK_RM2_DEFAULT_HOST_WAKE_PIN 24U
#endif
#ifndef OYK_RM2_DEFAULT_CS_PIN
#define OYK_RM2_DEFAULT_CS_PIN 25U
#endif
#ifndef OYK_RM2_DEFAULT_CLOCK_PIN
#define OYK_RM2_DEFAULT_CLOCK_PIN 29U
#endif

/* Optional SPI microSD default pins (SPI0) */
#ifndef OYK_PICO_SD_SCK_PIN
#define OYK_PICO_SD_SCK_PIN 18U
#endif
#ifndef OYK_PICO_SD_TX_PIN
#define OYK_PICO_SD_TX_PIN 19U
#endif
#ifndef OYK_PICO_SD_RX_PIN
#define OYK_PICO_SD_RX_PIN 16U
#endif
#ifndef OYK_PICO_SD_CS_PIN
#define OYK_PICO_SD_CS_PIN 17U
#endif

#ifndef OYK_FLASHMEM
#define OYK_FLASHMEM
#endif

#endif /* OYK_PICO_CONFIG_H */
