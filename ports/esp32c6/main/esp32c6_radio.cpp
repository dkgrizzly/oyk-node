/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "esp32c6_radio.hpp"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#endif

static Esp32C6RadioState   s_rstate;
static Esp32C6BleKissRxFn  s_ble_cb = NULL;
static void*               s_ble_ctx = NULL;

void esp32c6_radio_default_config(Esp32C6RadioConfig* cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->enable_wifi = 1;
    cfg->enable_ble = 1;
    strcpy(cfg->ble_name, "OYK-C6L-KISS");
}

int esp32c6_radio_init(const Esp32C6RadioConfig* cfg) {
    Esp32C6RadioConfig def;
    if (!cfg) {
        esp32c6_radio_default_config(&def);
        cfg = &def;
    }
    memset(&s_rstate, 0, sizeof(s_rstate));
    s_rstate.initialized = 1;
    s_rstate.wifi_enabled = cfg->enable_wifi ? 1 : 0;
    s_rstate.ble_enabled = cfg->enable_ble ? 1 : 0;
    s_rstate.mac[0] = 0x24; s_rstate.mac[1] = 0x58; s_rstate.mac[2] = 0x7C;
    s_rstate.mac[3] = 0xC6; s_rstate.mac[4] = 0x01; s_rstate.mac[5] = 0x01;
    s_rstate.ip_addr_net = 0x0104A8C0UL; /* 192.168.4.1 net byte order */
#ifdef ESP_PLATFORM
    esp_err_t nerr = nvs_flash_init();
    if (nerr == ESP_ERR_NVS_NO_FREE_PAGES || nerr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    esp_netif_init();
    esp_event_loop_create_default();
    if (cfg->enable_wifi) {
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
        if (esp_wifi_init(&wcfg) == ESP_OK) {
            esp_wifi_set_mode(WIFI_MODE_STA);
            esp_wifi_start();
        }
    }
#endif
    if (cfg->wifi_ssid[0]) {
        esp32c6_radio_connect_wifi(cfg->wifi_ssid, cfg->wifi_pass);
    } else {
        strcpy(s_rstate.status_text, "Wi-Fi 6 Ready + BLE 5 Adv");
    }
    return 1;
}

void esp32c6_radio_deinit() {
    s_rstate.initialized = 0;
    s_rstate.wifi_connected = 0;
    s_rstate.ble_connected = 0;
    strcpy(s_rstate.status_text, "RADIO OFF");
}

void esp32c6_radio_poll() {}

int esp32c6_radio_connect_wifi(const char* ssid, const char* /*pass*/) {
    if (!s_rstate.initialized || !ssid || !ssid[0]) return 0;
    s_rstate.wifi_connected = 1;
    s_rstate.ip_addr_net = 0x0A002C0AUL; /* 10.44.0.10 net byte order */
    strcpy(s_rstate.status_text, "Wi-Fi 6 UP");
    return 1;
}

void esp32c6_radio_set_ble_rx_callback(Esp32C6BleKissRxFn cb, void* ctx) {
    s_ble_cb = cb;
    s_ble_ctx = ctx;
}

int esp32c6_radio_ble_send(const oyk_u8* data, size_t len) {
    if (!s_rstate.initialized || !s_rstate.ble_enabled || !data || len == 0) return 0;
    return 1;
}

void esp32c6_radio_sim_inject_ble(const oyk_u8* data, size_t len) {
    if (!s_rstate.initialized || !data || len == 0) return;
    s_rstate.ble_connected = 1;
    if (s_ble_cb) s_ble_cb(data, len, s_ble_ctx);
}

const Esp32C6RadioState* esp32c6_radio_state() { return &s_rstate; }
