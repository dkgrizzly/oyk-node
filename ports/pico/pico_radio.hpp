/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_PICO_RADIO_HPP
#define OYK_PICO_RADIO_HPP

#include "oyk_pico_config.h"
#include "oyk_portable.hpp"

struct PicoRadioConfig {
    int enable_wifi;
    int enable_bt;
    char wifi_ssid[48];
    char wifi_pass[64];
    char bt_name[32];
    oyk_u8 rm2_reg_on;
    oyk_u8 rm2_data;
    oyk_u8 rm2_host_wake;
    oyk_u8 rm2_clock;
    oyk_u8 rm2_cs;
};

struct PicoRadioState {
    int supported;
    int initialized;
    int module_present;     /* 1 = CYW43/RM2 responded; 0 = RM2 missing (non-fatal) */
    int wifi_enabled;
    int wifi_connected;
    int bt_enabled;
    int bt_connected;
    oyk_u32 ip_addr_net;
    oyk_u8 mac[6];
    char status_text[64];
};

typedef void (*PicoBtKissRxFn)(const oyk_u8* data, size_t len, void* ctx);

void pico_radio_default_config(PicoRadioConfig* cfg);
int pico_radio_init(const PicoRadioConfig* cfg, int sim_rm2_present);
void pico_radio_deinit();
void pico_radio_poll();
int pico_radio_connect_wifi(const char* ssid, const char* pass);
void pico_radio_set_wl_led(int on);
void pico_radio_set_bt_rx_callback(PicoBtKissRxFn cb, void* ctx);
int pico_radio_bt_send(const oyk_u8* data, size_t len);
void pico_radio_sim_inject_bt(const oyk_u8* data, size_t len);
void pico_radio_sim_set_present(int present);
const PicoRadioState* pico_radio_state();

#endif /* OYK_PICO_RADIO_HPP */
