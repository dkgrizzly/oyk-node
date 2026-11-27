/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "lyra_radio.hpp"

#include <stdio.h>
#include <string.h>

static LyraRadioState s_lyra_radio = {};
static LyraBleKissRxFn s_lyra_ble_cb = NULL;
static void* s_lyra_ble_ctx = NULL;

void lyra_radio_default_config(LyraRadioConfig* cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->enable_net = 1;
    cfg->enable_wifi = 1;
    cfg->enable_ble = 1;
    cfg->enable_rndis = 1;
    strcpy(cfg->bt_name, "OYK-Node-Lyra");
}

#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD

#include <rtthread.h>
#include <rtdevice.h>

static rt_device_t s_ble_dev = RT_NULL;

int lyra_radio_init(const LyraRadioConfig* cfg, int /*sim_net_up*/) {
    LyraRadioConfig local_cfg;
    if (cfg) local_cfg = *cfg;
    else lyra_radio_default_config(&local_cfg);

    const LyraBoardProfile* prof = lyra_board_current_profile();
    memset(&s_lyra_radio, 0, sizeof(s_lyra_radio));
    s_lyra_radio.net_kind = prof->net_kind;
    lyra_board_read_mac(s_lyra_radio.mac);
    s_lyra_radio.initialized = 1;

    if (!local_cfg.enable_net) {
        strcpy(s_lyra_radio.status_text, "NET DISABLED (UART KISS OK)");
        return 0;
    }

    if (prof->net_kind == LYRA_NET_RMII_ETH) {
        rt_device_t eth = rt_device_find("e0");
        if (eth) {
            s_lyra_radio.rmii_link_up = 1;
            s_lyra_radio.ip_transport_ready = 1;
            strcpy(s_lyra_radio.status_text, "RMII ETH READY");
        } else {
            strcpy(s_lyra_radio.status_text, "RMII LINK DOWN (UART KISS OK)");
        }
    } else if (prof->net_kind == LYRA_NET_WIFI6_BLE) {
        rt_device_t wlan = rt_device_find("w0");
        if (wlan && local_cfg.enable_wifi) {
            s_lyra_radio.wifi_enabled = 1;
            s_lyra_radio.wifi_connected = local_cfg.wifi_ssid[0] ? 1 : 0;
            s_lyra_radio.ip_transport_ready = 1;
        }
        if (local_cfg.enable_ble) {
            s_ble_dev = rt_device_find("ble0");
            if (s_ble_dev) s_lyra_radio.ble_enabled = 1;
        }
        strcpy(s_lyra_radio.status_text,
               s_lyra_radio.ip_transport_ready ? "WIFI6/BLE READY" : "WIFI DOWN (UART OK)");
    } else {
        rt_device_t rndis = rt_device_find("u0");
        if (rndis && local_cfg.enable_rndis) {
            s_lyra_radio.usb_rndis_up = 1;
            s_lyra_radio.ip_transport_ready = 1;
            strcpy(s_lyra_radio.status_text, "USB RNDIS + PPP READY");
        } else {
            strcpy(s_lyra_radio.status_text, "RNDIS DETACHED (PPP/UART OK)");
        }
    }
    return s_lyra_radio.ip_transport_ready;
}

void lyra_radio_deinit() {
    s_ble_dev = RT_NULL;
    s_lyra_radio.initialized = 0;
    s_lyra_radio.ip_transport_ready = 0;
    s_lyra_radio.rmii_link_up = 0;
    s_lyra_radio.wifi_connected = 0;
    s_lyra_radio.ble_connected = 0;
    s_lyra_radio.usb_rndis_up = 0;
}

void lyra_radio_poll() {
    if (!s_lyra_radio.initialized) return;
    if (s_lyra_radio.ble_enabled && s_ble_dev && s_lyra_ble_cb) {
        oyk_u8 buf[128];
        rt_ssize_t n = rt_device_read(s_ble_dev, 0, buf, sizeof(buf));
        if (n > 0) {
            s_lyra_ble_cb(buf, static_cast<size_t>(n), s_lyra_ble_ctx);
        }
    }
}

int lyra_radio_connect_wifi(const char* ssid, const char* /*pass*/) {
    if (!s_lyra_radio.initialized || s_lyra_radio.net_kind != LYRA_NET_WIFI6_BLE || !ssid || !ssid[0]) {
        return 0;
    }
    s_lyra_radio.wifi_enabled = 1;
    s_lyra_radio.wifi_connected = 1;
    s_lyra_radio.ip_transport_ready = 1;
    return 1;
}

int lyra_radio_ble_send(const oyk_u8* data, size_t len) {
    if (!s_lyra_radio.ble_enabled || !s_ble_dev || !data || len == 0) return 0;
    rt_ssize_t written = rt_device_write(s_ble_dev, 0, data, len);
    return (written > 0) ? static_cast<int>(written) : 0;
}

void lyra_radio_sim_set_link_up(int /*up*/) {}
void lyra_radio_sim_inject_ble(const oyk_u8* /*data*/, size_t /*len*/) {}

#else

static int s_lyra_sim_net_up = -1;

void lyra_radio_sim_set_link_up(int up) {
    s_lyra_sim_net_up = (up < 0) ? -1 : (up ? 1 : 0);
}

int lyra_radio_init(const LyraRadioConfig* cfg, int sim_net_up) {
    LyraRadioConfig local_cfg;
    if (cfg) local_cfg = *cfg;
    else lyra_radio_default_config(&local_cfg);

    const LyraBoardProfile* prof = lyra_board_current_profile();
    memset(&s_lyra_radio, 0, sizeof(s_lyra_radio));
    s_lyra_radio.net_kind = prof->net_kind;
    lyra_board_read_mac(s_lyra_radio.mac);
    s_lyra_radio.initialized = 1;

    int up = (s_lyra_sim_net_up >= 0) ? s_lyra_sim_net_up : sim_net_up;
    if (!local_cfg.enable_net || !up) {
        s_lyra_radio.ip_transport_ready = 0;
        if (prof->net_kind == LYRA_NET_RMII_ETH) {
            strcpy(s_lyra_radio.status_text, "RMII LINK DOWN (UART KISS OK)");
        } else if (prof->net_kind == LYRA_NET_WIFI6_BLE) {
            strcpy(s_lyra_radio.status_text, "WIFI DOWN (UART OK)");
        } else {
            strcpy(s_lyra_radio.status_text, "RNDIS DETACHED (PPP/UART OK)");
        }
        return 0;
    }

    s_lyra_radio.ip_transport_ready = 1;
    s_lyra_radio.ip_addr_net = 0x0A000035UL;
    if (prof->net_kind == LYRA_NET_RMII_ETH) {
        s_lyra_radio.rmii_link_up = 1;
        strcpy(s_lyra_radio.status_text, "RMII ETH READY");
    } else if (prof->net_kind == LYRA_NET_WIFI6_BLE) {
        s_lyra_radio.wifi_enabled = local_cfg.enable_wifi ? 1 : 0;
        s_lyra_radio.wifi_connected = local_cfg.wifi_ssid[0] ? 1 : 0;
        s_lyra_radio.ble_enabled = local_cfg.enable_ble ? 1 : 0;
        strcpy(s_lyra_radio.status_text, "WIFI6/BLE READY");
    } else {
        s_lyra_radio.usb_rndis_up = local_cfg.enable_rndis ? 1 : 0;
        strcpy(s_lyra_radio.status_text, "USB RNDIS + PPP READY");
    }
    return 1;
}

void lyra_radio_deinit() {
    s_lyra_radio.initialized = 0;
    s_lyra_radio.ip_transport_ready = 0;
    s_lyra_radio.rmii_link_up = 0;
    s_lyra_radio.wifi_connected = 0;
    s_lyra_radio.ble_connected = 0;
    s_lyra_radio.usb_rndis_up = 0;
}

void lyra_radio_poll() {}

int lyra_radio_connect_wifi(const char* ssid, const char* /*pass*/) {
    if (!s_lyra_radio.initialized || s_lyra_radio.net_kind != LYRA_NET_WIFI6_BLE || !ssid || !ssid[0]) {
        return 0;
    }
    s_lyra_radio.wifi_enabled = 1;
    s_lyra_radio.wifi_connected = 1;
    s_lyra_radio.ip_transport_ready = 1;
    return 1;
}

int lyra_radio_ble_send(const oyk_u8* data, size_t len) {
    if (!s_lyra_radio.ble_enabled || !data || len == 0) return 0;
    return static_cast<int>(len);
}

void lyra_radio_sim_inject_ble(const oyk_u8* data, size_t len) {
    if (s_lyra_ble_cb && data && len > 0) {
        s_lyra_ble_cb(data, len, s_lyra_ble_ctx);
    }
}

#endif

void lyra_radio_set_ble_rx_callback(LyraBleKissRxFn cb, void* ctx) {
    s_lyra_ble_cb = cb;
    s_lyra_ble_ctx = ctx;
}

const LyraRadioState* lyra_radio_state() {
    return &s_lyra_radio;
}
