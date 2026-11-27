/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_LYRA_RADIO_HPP
#define OYK_LYRA_RADIO_HPP

#include "oyk_lyra_config.h"
#include "oyk_portable.hpp"
#include "board_lyra.hpp"

struct LyraRadioConfig {
    int enable_net;
    int enable_wifi;
    int enable_ble;
    int enable_rndis;
    char wifi_ssid[48];
    char wifi_pass[64];
    char bt_name[32];
};

struct LyraRadioState {
    LyraNetKind net_kind;
    int initialized;
    int ip_transport_ready;
    int rmii_link_up;
    int wifi_enabled;
    int wifi_connected;
    int ble_enabled;
    int ble_connected;
    int usb_rndis_up;
    oyk_u32 ip_addr_net;
    oyk_u8 mac[6];
    char status_text[64];
};

typedef void (*LyraBleKissRxFn)(const oyk_u8* data, size_t len, void* ctx);

void lyra_radio_default_config(LyraRadioConfig* cfg);
int lyra_radio_init(const LyraRadioConfig* cfg, int sim_net_up);
void lyra_radio_deinit();
void lyra_radio_poll();
int lyra_radio_connect_wifi(const char* ssid, const char* pass);
void lyra_radio_set_ble_rx_callback(LyraBleKissRxFn cb, void* ctx);
int lyra_radio_ble_send(const oyk_u8* data, size_t len);
void lyra_radio_sim_set_link_up(int up);
void lyra_radio_sim_inject_ble(const oyk_u8* data, size_t len);
const LyraRadioState* lyra_radio_state();

#endif /* OYK_LYRA_RADIO_HPP */
