/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_BOARD_PICO_HPP
#define OYK_BOARD_PICO_HPP

#include "oyk_pico_config.h"
#include "oyk_portable.hpp"

#define PICO_TOTAL_BOARD_PROFILES 4

enum PicoMcuKind {
    PICO_MCU_RP2040 = 0,
    PICO_MCU_RP2350A,
    PICO_MCU_RP2350B
};

enum PicoRadioMode {
    PICO_RADIO_ONBOARD_CYW43 = 0,
    PICO_RADIO_OPTIONAL_RM2
};

enum PicoPortRole {
    PICO_ROLE_UNUSED = 0,
    PICO_ROLE_PPP_RADIO,
    PICO_ROLE_KISS_TNC,
    PICO_ROLE_RIG,
    PICO_ROLE_PRINTER
};

struct PicoBoardProfile {
    const char* board_id;
    const char* board_label;
    PicoMcuKind mcu;
    oyk_u32 flash_bytes;
    oyk_u32 sram_bytes;
    oyk_u32 max_psram_bytes;
    int psram_cs_pin;
    PicoRadioMode radio_mode;
    oyk_u8 status_led_pin;   /* 255 = CYW43 WL_GPIO0 */
    oyk_u8 sd_sck_pin;
    oyk_u8 sd_tx_pin;
    oyk_u8 sd_rx_pin;
    oyk_u8 sd_cs_pin;
};

struct PicoUartMap {
    oyk_u8 logical_port;     /* 0..1 -> ser0..ser1 */
    oyk_u8 hw_uart_index;    /* 0 (uart0) or 1 (uart1) */
    oyk_u8 pin_tx;
    oyk_u8 pin_rx;
    oyk_u8 pin_cts;
    oyk_u8 pin_rts;
    const char* name;        /* "ser0", "ser1" */
};

struct PicoUartConfig {
    oyk_u32 baudrate;
    oyk_u8 cts_rts;
    PicoPortRole role;
};

struct PicoPsramInfo;

const PicoBoardProfile* pico_board_profiles();
const PicoBoardProfile* pico_board_profile_by_id(const char* board_id);
const PicoBoardProfile* pico_current_board_profile();

const PicoUartMap* pico_uart_table();
const PicoUartMap* pico_uart_info(int logical_port);

void pico_decode_unique_id_mac(const oyk_u8 unique_id[8], oyk_u8 mac_out[6]);

void pico_board_init();
void pico_board_read_mac(oyk_u8 mac_out[6]);
void pico_board_set_status_led(int on);
void pico_board_set_link_led(int up);
const PicoPsramInfo* pico_board_psram_info();
oyk_u32 pico_board_millis();
void pico_board_enter_bootloader();
int pico_board_sim_bootloader_count();
void pico_board_set_sim_profile(const char* board_id);

int pico_uart_init(int logical_port, const PicoUartConfig* cfg);
size_t pico_uart_write(int logical_port, const oyk_u8* data, size_t len);
size_t pico_uart_read(int logical_port, oyk_u8* out, size_t max_len);

typedef void (*PicoUartSimTxFn)(int logical_port, const oyk_u8* data, size_t len, void* ctx);
void pico_uart_set_sim_tx_hook(PicoUartSimTxFn cb, void* ctx);

#endif /* OYK_BOARD_PICO_HPP */
