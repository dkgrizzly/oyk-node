/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_BOARD_ESP32C6_HPP
#define OYK_BOARD_ESP32C6_HPP

#include "oyk_esp32c6_config.h"
#include "oyk_portable.hpp"

struct Esp32C6BoardProfile {
    const char* board_id;
    const char* board_label;
    oyk_u32 flash_bytes;
    oyk_u32 sram_bytes;
    oyk_u8 spi_sck;
    oyk_u8 spi_mosi;
    oyk_u8 spi_miso;
    oyk_u8 lora_cs;
    oyk_u8 lora_dio1;
    oyk_u8 lora_busy;
    oyk_u8 oled_cs;
    oyk_u8 oled_dc;
    oyk_u8 oled_rst;
    oyk_u8 i2c_sda;
    oyk_u8 i2c_scl;
    oyk_u8 pi4io_int;
    oyk_u8 pi4io_addr;
    oyk_u8 rgb_led_pin;
    oyk_u8 buzzer_pin;
    oyk_u8 grove_tx_pin;
    oyk_u8 grove_rx_pin;
};

struct Esp32C6UartMap {
    int logical_port;
    int hw_uart_num;
    oyk_u8 tx_pin;
    oyk_u8 rx_pin;
    const char* device_name;
};

enum Esp32C6LedState {
    C6L_LED_OFF = 0,
    C6L_LED_IDLE,
    C6L_LED_RX,
    C6L_LED_TX,
    C6L_LED_ERROR
};

const Esp32C6BoardProfile* esp32c6_current_board_profile();
const Esp32C6UartMap* esp32c6_uart_info(int logical_port);

void esp32c6_board_init();
void esp32c6_board_set_lora_rf_path(int lna_en, int ant_sw, int nrst_high);
oyk_u8 esp32c6_board_pi4io_out_reg();
oyk_u8 esp32c6_board_pi4io_in_reg();

void esp32c6_board_sim_set_button(int pressed);
int  esp32c6_board_poll_button_press(oyk_u32 now_ms);

void esp32c6_board_set_rgb_led(Esp32C6LedState state, oyk_u8 r, oyk_u8 g, oyk_u8 b);
Esp32C6LedState esp32c6_board_rgb_state();
void esp32c6_board_rgb_color(oyk_u8* r, oyk_u8* g, oyk_u8* b);

void esp32c6_board_buzzer_beep(oyk_u16 freq_hz, oyk_u16 duration_ms);
oyk_u16 esp32c6_board_last_buzzer_freq();
oyk_u16 esp32c6_board_last_buzzer_ms();

void esp32c6_board_set_uart_baud(int logical_port, oyk_u32 baud);
int  esp32c6_board_uart_write(int logical_port, const oyk_u8* data, size_t len);
size_t esp32c6_board_uart_read(int logical_port, oyk_u8* out, size_t max_len);

oyk_u32 esp32c6_board_millis();
void esp32c6_board_enter_bootloader();
int  esp32c6_board_sim_bootloader_count();

#endif /* OYK_BOARD_ESP32C6_HPP */
