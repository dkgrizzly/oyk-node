/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_BOARD_LYRA_HPP
#define OYK_BOARD_LYRA_HPP

#include "oyk_lyra_config.h"
#include "oyk_portable.hpp"

#define LYRA_TOTAL_BOARD_PROFILES 7

enum LyraSocKind {
    LYRA_SOC_RK3506G2 = 0,
    LYRA_SOC_RK3506B
};

enum LyraNetKind {
    LYRA_NET_USB_RNDIS_PPP = 0,
    LYRA_NET_RMII_ETH,
    LYRA_NET_WIFI6_BLE
};

enum LyraPortRole {
    LYRA_ROLE_UNUSED = 0,
    LYRA_ROLE_PPP_RADIO,
    LYRA_ROLE_KISS_TNC,
    LYRA_ROLE_RIG,
    LYRA_ROLE_PRINTER
};

struct LyraBoardProfile {
    const char* id;
    const char* display_name;
    LyraSocKind soc;
    oyk_u16 ram_mb;
    oyk_u16 nand_mb;
    LyraNetKind net_kind;
    oyk_u8 has_picocalc_hw;
    oyk_u8 uart_count;
    oyk_u8 has_audio_default_off;
    oyk_u8 lcd_cs_pin;
    oyk_u8 lcd_dc_pin;
    oyk_u8 lcd_rst_pin;
    oyk_u8 kbd_scl_pin;
    oyk_u8 kbd_sda_pin;
};

struct LyraUartMap {
    oyk_u8 logical_port;     /* 0..1 -> ser0..ser1 */
    oyk_u8 hw_uart_index;    /* 1 (uart1) or 2 (uart2) */
    oyk_u32 mmio_base;       /* 0xFF0B0000 / 0xFF0C0000 */
    const char* rt_dev_name; /* "uart1" / "uart2" */
    const char* name;        /* "ser0", "ser1" */
};

struct LyraUartConfig {
    oyk_u32 baudrate;
    oyk_u8 cts_rts;
    LyraPortRole role;
};

int lyra_board_profile_count();
const LyraBoardProfile* lyra_board_profile_at(int idx);
const LyraBoardProfile* lyra_board_profile_by_id(const char* board_id);
const LyraBoardProfile* lyra_board_current_profile();
void lyra_board_set_sim_profile(const char* board_id);

const LyraUartMap* lyra_uart_table();
const LyraUartMap* lyra_uart_info(int logical_port);

void lyra_board_init();
void lyra_board_init_pinmux();
void lyra_board_read_mac(oyk_u8 mac_out[6]);
oyk_u32 lyra_uptime_ms();
void lyra_board_reboot();
void lyra_board_poweroff(oyk_u8 delay_sec);
int lyra_board_sim_reboot_count();
int lyra_board_sim_poweroff_count();

int lyra_uart_init(int logical_port, const LyraUartConfig* cfg);
size_t lyra_uart_write(int logical_port, const oyk_u8* data, size_t len);
size_t lyra_uart_read(int logical_port, oyk_u8* out, size_t max_len);

typedef void (*LyraUartSimTxFn)(int logical_port, const oyk_u8* data, size_t len, void* ctx);
void lyra_uart_set_sim_tx_hook(LyraUartSimTxFn cb, void* ctx);
void lyra_uart_sim_inject_rx(int logical_port, const oyk_u8* data, size_t len);

#endif /* OYK_BOARD_LYRA_HPP */
