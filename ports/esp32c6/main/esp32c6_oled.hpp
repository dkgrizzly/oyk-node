/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_ESP32C6_OLED_HPP
#define OYK_ESP32C6_OLED_HPP

#include "oyk_esp32c6_config.h"
#include "oyk_portable.hpp"

enum Esp32C6OledPage {
    C6L_OLED_PAGE_NODE = 0,
    C6L_OLED_PAGE_LORA,
    C6L_OLED_PAGE_NET,
    C6L_OLED_PAGE_SESSIONS,
    C6L_OLED_PAGE_COUNT
};

struct Esp32C6OledSnapshot {
    char callsign[16];
    char alias[16];
    int accept_incoming;
    int active_conns;
    oyk_u32 uptime_sec;
    oyk_u32 lora_freq_hz;
    oyk_u32 lora_rx_pkts;
    oyk_u32 lora_tx_pkts;
    int last_rssi_dbm;
    int wifi_connected;
    int bt_enabled;
    oyk_u32 wifi_ip_net;
    oyk_u32 grove_baud;
    char last_heard[16];
};

void esp32c6_oled_init();
void esp32c6_oled_set_page(int page);
int  esp32c6_oled_page();
int  esp32c6_oled_next_page();
void esp32c6_oled_set_enabled(int on);
int  esp32c6_oled_enabled();
void esp32c6_oled_render(const Esp32C6OledSnapshot* snap);
const oyk_u8* esp32c6_oled_framebuffer();
size_t esp32c6_oled_framebuffer_bytes();
int  esp32c6_oled_pixel(int x, int y);

#endif /* OYK_ESP32C6_OLED_HPP */
