/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "pico_radio.hpp"
#include "board_pico.hpp"

#include <stdio.h>
#include <string.h>

static PicoRadioState s_radio = {};
static PicoBtKissRxFn s_bt_rx_cb = NULL;
static void* s_bt_rx_ctx = NULL;
static int s_sim_present = -1;

void pico_radio_default_config(PicoRadioConfig* cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->enable_wifi = 1;
    cfg->enable_bt = 1;
    strcpy(cfg->bt_name, "OYK-Node-Pico");
    cfg->rm2_reg_on = OYK_RM2_DEFAULT_REG_ON_PIN;
    cfg->rm2_data = OYK_RM2_DEFAULT_DATA_PIN;
    cfg->rm2_host_wake = OYK_RM2_DEFAULT_HOST_WAKE_PIN;
    cfg->rm2_clock = OYK_RM2_DEFAULT_CLOCK_PIN;
    cfg->rm2_cs = OYK_RM2_DEFAULT_CS_PIN;
}

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE && defined(PICO_CYW43_SUPPORTED) && PICO_CYW43_SUPPORTED

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/cyw43_driver.h"
#if defined(OYK_PICO_HAVE_BTSTACK) && OYK_PICO_HAVE_BTSTACK
#include "pico/btstack_cyw43.h"
#include "btstack.h"
#endif

static int s_bt_initialized = 0;

int pico_radio_init(const PicoRadioConfig* cfg, int /*sim_rm2_present*/) {
    PicoRadioConfig local_cfg;
    if (cfg) {
        local_cfg = *cfg;
    } else {
        pico_radio_default_config(&local_cfg);
    }

    memset(&s_radio, 0, sizeof(s_radio));
    s_radio.supported = 1;
    pico_board_read_mac(s_radio.mac);
    s_bt_initialized = 0;

#if defined(CYW43_PIN_WL_DYNAMIC) && CYW43_PIN_WL_DYNAMIC
    const PicoBoardProfile* prof = pico_current_board_profile();
    if (prof->radio_mode == PICO_RADIO_OPTIONAL_RM2) {
        uint pins[CYW43_PIN_INDEX_WL_COUNT];
        pins[CYW43_PIN_INDEX_WL_REG_ON] = local_cfg.rm2_reg_on;
        pins[CYW43_PIN_INDEX_WL_DATA_OUT] = local_cfg.rm2_data;
        pins[CYW43_PIN_INDEX_WL_DATA_IN] = local_cfg.rm2_data;
        pins[CYW43_PIN_INDEX_WL_HOST_WAKE] = local_cfg.rm2_host_wake;
        pins[CYW43_PIN_INDEX_WL_CLOCK] = local_cfg.rm2_clock;
        pins[CYW43_PIN_INDEX_WL_CS] = local_cfg.rm2_cs;
        if (cyw43_set_pins_wl(pins) != PICO_OK) {
            strcpy(s_radio.status_text, "RM2 PIN CONFIG INVALID (non-fatal)");
            return 0;
        }
    }
#endif

    if (cyw43_arch_init() != 0) {
        cyw43_arch_deinit();
        s_radio.initialized = 0;
        s_radio.module_present = 0;
        strcpy(s_radio.status_text, "RM2 ABSENT (non-fatal)");
        return 0;
    }

    s_radio.initialized = 1;
    s_radio.module_present = 1;
    memcpy(s_radio.mac, cyw43_state.mac, 6);

    if (local_cfg.enable_wifi) {
        cyw43_arch_enable_sta_mode();
        s_radio.wifi_enabled = 1;
        if (local_cfg.wifi_ssid[0]) {
            cyw43_arch_wifi_connect_async(
                local_cfg.wifi_ssid,
                local_cfg.wifi_pass[0] ? local_cfg.wifi_pass : NULL,
                local_cfg.wifi_pass[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
        }
    }

#if defined(OYK_PICO_HAVE_BTSTACK) && OYK_PICO_HAVE_BTSTACK
    if (local_cfg.enable_bt) {
        if (btstack_cyw43_init(cyw43_arch_async_context())) {
            s_bt_initialized = 1;
            s_radio.bt_enabled = 1;
            gap_set_local_name(local_cfg.bt_name[0] ? local_cfg.bt_name : "OYK-Node-Pico");
            gap_discoverable_control(1);
            hci_power_control(HCI_POWER_ON);
        }
    }
#endif

    strcpy(s_radio.status_text,
           (pico_current_board_profile()->radio_mode == PICO_RADIO_OPTIONAL_RM2)
               ? "RM2 READY"
               : "CYW43 READY");
    return 1;
}

void pico_radio_deinit() {
    if (!s_radio.initialized) return;
#if defined(OYK_PICO_HAVE_BTSTACK) && OYK_PICO_HAVE_BTSTACK
    if (s_bt_initialized) {
        btstack_cyw43_deinit(cyw43_arch_async_context());
        s_bt_initialized = 0;
    }
#endif
    cyw43_arch_deinit();
    s_radio.initialized = 0;
    s_radio.module_present = 0;
    s_radio.wifi_connected = 0;
    s_radio.bt_connected = 0;
}

void pico_radio_poll() {
    if (!s_radio.initialized || !s_radio.module_present) return;
    cyw43_arch_poll();
    if (s_radio.wifi_enabled) {
        int link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        s_radio.wifi_connected = (link == CYW43_LINK_UP) ? 1 : 0;
        if (s_radio.wifi_connected) {
            s_radio.ip_addr_net = cyw43_state.netif[CYW43_ITF_STA].ip_addr.addr;
        }
    }
}

int pico_radio_connect_wifi(const char* ssid, const char* pass) {
    if (!s_radio.initialized || !s_radio.module_present || !ssid || !ssid[0]) return 0;
    if (!s_radio.wifi_enabled) {
        cyw43_arch_enable_sta_mode();
        s_radio.wifi_enabled = 1;
    }
    return (cyw43_arch_wifi_connect_async(
                ssid,
                (pass && pass[0]) ? pass : NULL,
                (pass && pass[0]) ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN) == 0)
               ? 1
               : 0;
}

void pico_radio_set_wl_led(int on) {
#ifdef CYW43_WL_GPIO_LED_PIN
    if (s_radio.initialized && s_radio.module_present) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
    }
#else
    (void)on;
#endif
}

int pico_radio_bt_send(const oyk_u8* data, size_t len) {
    if (!s_radio.initialized || !s_radio.bt_enabled || !data || len == 0) return 0;
    return static_cast<int>(len);
}

void pico_radio_sim_inject_bt(const oyk_u8* /*data*/, size_t /*len*/) {}
void pico_radio_sim_set_present(int /*present*/) {}

#else

/* Host test simulation */
void pico_radio_sim_set_present(int present) {
    s_sim_present = present ? 1 : 0;
}

int pico_radio_init(const PicoRadioConfig* cfg, int sim_rm2_present) {
    PicoRadioConfig local_cfg;
    if (cfg) local_cfg = *cfg;
    else pico_radio_default_config(&local_cfg);

    memset(&s_radio, 0, sizeof(s_radio));
    s_radio.supported = 1;
    pico_board_read_mac(s_radio.mac);

    const PicoBoardProfile* prof = pico_current_board_profile();
    int present = 1;
    if (prof->radio_mode == PICO_RADIO_OPTIONAL_RM2) {
        present = (s_sim_present >= 0) ? s_sim_present : sim_rm2_present;
    }
    if (!present) {
        s_radio.initialized = 0;
        s_radio.module_present = 0;
        strcpy(s_radio.status_text, "RM2 ABSENT (non-fatal)");
        return 0;
    }

    s_radio.initialized = 1;
    s_radio.module_present = 1;
    s_radio.wifi_enabled = local_cfg.enable_wifi ? 1 : 0;
    s_radio.bt_enabled = local_cfg.enable_bt ? 1 : 0;
    s_radio.wifi_connected = local_cfg.wifi_ssid[0] ? 1 : 0;
    s_radio.ip_addr_net = 0x0A00002AUL;
    strcpy(s_radio.status_text,
           (prof->radio_mode == PICO_RADIO_OPTIONAL_RM2) ? "RM2 READY" : "CYW43 READY");
    return 1;
}

void pico_radio_deinit() {
    s_radio.initialized = 0;
    s_radio.module_present = 0;
    s_radio.wifi_connected = 0;
    s_radio.bt_connected = 0;
}

void pico_radio_poll() {}

int pico_radio_connect_wifi(const char* ssid, const char* /*pass*/) {
    if (!s_radio.module_present || !ssid || !ssid[0]) return 0;
    s_radio.wifi_enabled = 1;
    s_radio.wifi_connected = 1;
    return 1;
}

void pico_radio_set_wl_led(int /*on*/) {}

int pico_radio_bt_send(const oyk_u8* data, size_t len) {
    if (!s_radio.module_present || !s_radio.bt_enabled || !data || len == 0) return 0;
    return static_cast<int>(len);
}

void pico_radio_sim_inject_bt(const oyk_u8* data, size_t len) {
    if (s_bt_rx_cb && data && len > 0) {
        s_bt_rx_cb(data, len, s_bt_rx_ctx);
    }
}

#endif

void pico_radio_set_bt_rx_callback(PicoBtKissRxFn cb, void* ctx) {
    s_bt_rx_cb = cb;
    s_bt_rx_ctx = ctx;
}

const PicoRadioState* pico_radio_state() {
    return &s_radio;
}
