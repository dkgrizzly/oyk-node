/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_LYRA_RTDEVICE_STUB_H
#define OYK_LYRA_RTDEVICE_STUB_H

/* Minimal RT-Thread device driver API declarations (GPIO, UART, I2C, SPI, PWM). */
#include "rtthread.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PIN_LOW                  0
#define PIN_HIGH                 1
#define PIN_MODE_OUTPUT          0

void rt_pin_mode(long pin, long mode);
void rt_pin_write(long pin, long value);

struct serial_configure {
    unsigned long baud_rate;
};
#define RT_SERIAL_CONFIG_DEFAULT { 115200UL }

#define RT_I2C_WR                0x0000U
#define RT_I2C_RD                0x0001U

struct rt_i2c_msg {
    unsigned short addr;
    unsigned short flags;
    unsigned short len;
    unsigned char* buf;
};

struct rt_i2c_bus_device {
    int dummy;
};

size_t rt_i2c_transfer(struct rt_i2c_bus_device* bus, struct rt_i2c_msg msgs[], rt_uint32_t num);

#define RT_SPI_MASTER            0x00U
#define RT_SPI_MODE_0            0x00U
#define RT_SPI_MSB               0x08U

struct rt_spi_configuration {
    unsigned char mode;
    unsigned char data_width;
    unsigned short reserved;
    unsigned long max_hz;
};

struct rt_spi_device {
    int dummy;
};

rt_err_t rt_spi_configure(struct rt_spi_device* device, struct rt_spi_configuration* cfg);
size_t rt_spi_send(struct rt_spi_device* device, const void* send_buf, size_t length);

struct rt_device_pwm {
    int dummy;
};

rt_err_t rt_pwm_set(struct rt_device_pwm* device, int channel, rt_uint32_t period, rt_uint32_t pulse);
rt_err_t rt_pwm_enable(struct rt_device_pwm* device, int channel);
rt_err_t rt_pwm_disable(struct rt_device_pwm* device, int channel);

#ifdef __cplusplus
}
#endif

#endif /* OYK_LYRA_RTDEVICE_STUB_H */
