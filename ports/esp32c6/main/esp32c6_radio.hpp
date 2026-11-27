/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_ESP32C6_RADIO_HPP
#define OYK_ESP32C6_RADIO_HPP

#include "oyk_esp32c6_config.h"
#include "oyk_portable.hpp"

struct Esp32C6RadioConfig {
    int enable_wifi;
    int enable_ble;
    char wifi_ssid[48];
    char wifi_pass[64];
    char ble_name[32];
};

struct Esp32C6RadioState {
    int initialized;
    int wifi_enabled;
    int wifi_connected;
    int ble_enabled;
    int ble_connected;
    oyk_u32 ip_addr_net;
    oyk_u8 mac[6];
    char status_text[64];
};

typedef void (*Esp32C6BleKissRxFn)(const oyk_u8* data, size_t len, void* ctx);

void esp32c6_radio_default_config(Esp32C6RadioConfig* cfg);
int  esp32c6_radio_init(const Esp32C6RadioConfig* cfg);
void esp32c6_radio_deinit();
void esp32c6_radio_poll();
int  esp32c6_radio_connect_wifi(const char* ssid, const char* pass);
void esp32c6_radio_set_ble_rx_callback(Esp32C6BleKissRxFn cb, void* ctx);
int  esp32c6_radio_ble_send(const oyk_u8* data, size_t len);
void esp32c6_radio_sim_inject_ble(const oyk_u8* data, size_t len);
const Esp32C6RadioState* esp32c6_radio_state();

#endif /* OYK_ESP32C6_RADIO_HPP */
