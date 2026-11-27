/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_LYRA_CONFIG_H
#define OYK_LYRA_CONFIG_H

/*
 * Feature and target configuration macros for the Luckfox Lyra
 * (Rockchip RK3506G2 / RK3506B, triple Cortex-A7 + Cortex-M0) port running on
 * RT-Thread SMP + rk-hal:
 *   - lyra               (RK3506G2, 128MB DDR3L, no NAND, USB RNDIS/PPP, 2 UARTs)
 *   - lyra_b             (RK3506G2, 128MB DDR3L, 256MB SPI NAND, USB RNDIS/PPP, 2 UARTs)
 *   - lyra_plus          (RK3506G2, 128MB DDR3L, 256MB SPI NAND, 10/100 RMII Eth, 2 UARTs)
 *   - lyra_zero_w        (RK3506B, 512MB DDR3L, 256MB SPI NAND, Wi-Fi 6 + BLE 5.2, 2 UARTs)
 *   - picocalc_lyra      (RK3506G2 inside ClockworkPi PicoCalc: 320x320 LCD + I2C KBD, ser0)
 *   - picocalc_lyra_b    (RK3506G2 + 256MB NAND inside PicoCalc: LCD + KBD, ser0)
 *   - picocalc_lyra_plus (RK3506G2 + 256MB NAND + RMII Eth inside PicoCalc: LCD + KBD, ser0)
 */

#ifdef __RTTHREAD__
#include "rtconfig_oyk.h"
#ifndef OYK_LYRA_RTTHREAD
#define OYK_LYRA_RTTHREAD 1
#endif
#endif

#ifndef OYK_LYRA_PORT
#define OYK_LYRA_PORT 1
#endif

#ifndef OYK_LYRA_RTTHREAD
#define OYK_LYRA_RTTHREAD 0
#endif

#ifndef OYK_LYRA_UART_COUNT
#define OYK_LYRA_UART_COUNT 2
#endif

#ifndef OYK_LYRA_IS_PICOCALC
#define OYK_LYRA_IS_PICOCALC 0
#endif

/* Opt-in hardware PWM BEL audio (default OFF to prevent crackle on unmodified boards) */
#ifndef OYK_LYRA_ENABLE_AUDIO
#define OYK_LYRA_ENABLE_AUDIO 0
#endif

#ifndef OYK_LYRA_BOARD_ID
  #if OYK_LYRA_IS_PICOCALC
    #define OYK_LYRA_BOARD_ID "picocalc_lyra"
  #else
    #define OYK_LYRA_BOARD_ID "lyra"
  #endif
#endif

/* ClockworkPi PicoCalc + Luckfox Lyra (RK3506) hardware bus & pin mappings */
#define OYK_LYRA_LCD_WIDTH        320U
#define OYK_LYRA_LCD_HEIGHT       320U
#define OYK_LYRA_LCD_SPI_DEV      "spi00"
#define OYK_LYRA_LCD_CS_PIN       4U   /* GPIO0_PA4 */
#define OYK_LYRA_LCD_DC_PIN       3U   /* GPIO0_PA3 */
#define OYK_LYRA_LCD_RST_PIN      2U   /* GPIO0_PA2 */

#define OYK_LYRA_KBD_I2C_DEV      "i2c2"
#define OYK_LYRA_KBD_I2C_ADDR     0x1FU
#define OYK_LYRA_KBD_SCL_PIN      10U  /* GPIO0_PB2 */
#define OYK_LYRA_KBD_SDA_PIN      11U  /* GPIO0_PB3 */

#define OYK_LYRA_AUDIO_PWM_DEV    "pwm0"
#define OYK_LYRA_AUDIO_RMIO_PIN   12U  /* RM_IO12 */

#define OYK_LYRA_CONSOLE_UART_DEV "uart0" /* 0xFF0A0000 (1,500,000 bps debug/FinSH or console) */
#define OYK_LYRA_SER0_UART_DEV    "uart1" /* 0xFF0B0000 -> ser0 */
#define OYK_LYRA_SER1_UART_DEV    "uart2" /* 0xFF0C0000 -> ser1 */

#define OYK_LYRA_SD_MOUNT         "/sd"
#define OYK_LYRA_NAND_MOUNT       "/flash"

#endif /* OYK_LYRA_CONFIG_H */
