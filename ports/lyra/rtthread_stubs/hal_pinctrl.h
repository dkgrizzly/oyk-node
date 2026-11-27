/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_LYRA_HAL_PINCTRL_STUB_H
#define OYK_LYRA_HAL_PINCTRL_STUB_H

#ifdef __cplusplus
extern "C" {
#endif

enum eGPIO_bankId {
    GPIO_BANK0 = 0
};

#define GPIO_PIN_A2  2U
#define GPIO_PIN_A3  3U
#define GPIO_PIN_A4  4U
#define GPIO_PIN_B2  10U
#define GPIO_PIN_B3  11U
#define GPIO_PIN_B4  12U
#define GPIO_PIN_B5  13U
#define GPIO_PIN_B6  14U
#define GPIO_PIN_B7  15U

#define RMIO_GPIO0_A2 0U
#define RMIO_GPIO0_A3 0U
#define RMIO_GPIO0_A4 0U
#define RMIO_I2C2_SCL 1U
#define RMIO_I2C2_SDA 2U
#define RMIO_UART1_TX 3U
#define RMIO_UART1_RX 4U
#define RMIO_UART2_TX 5U
#define RMIO_UART2_RX 6U

int HAL_PINCTRL_SetRMIO(enum eGPIO_bankId bank, unsigned int pin, unsigned int rmio_func);

#ifdef __cplusplus
}
#endif

#endif /* OYK_LYRA_HAL_PINCTRL_STUB_H */
