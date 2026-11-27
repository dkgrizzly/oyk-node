/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_pico_config.h"
#include "oyk_portable.hpp"
#include "board_pico.hpp"
#include "pico_psram.hpp"
#include "pico_fs.hpp"
#include "pico_radio.hpp"
#include "pico_net_bridge.hpp"
#include "pico_console.hpp"
#if defined(OYK_PICO_IS_PICOCALC) && OYK_PICO_IS_PICOCALC
#include "pico_picocalc_console.hpp"
#endif

#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

static OykPortableNode* g_node = NULL;
static PicoNetBridge* g_net = NULL;
static PicoConsole* g_console = NULL;
static bool g_usb_connected = false;
static oyk_u32 g_last_blink_ms = 0;
static int g_blink_state = 1;

static size_t usb_cdc_console_tx(const char* data, size_t len, void* /*ctx*/) {
    if (!data || len == 0) return 0;
    for (size_t i = 0; i < len; ++i) {
        putchar_raw(data[i]);
    }
    stdio_flush();
    return len;
}

static void local_term_trampoline(oyk_u32 sid, int event_type,
                                  const oyk_u8* data, size_t len,
                                  const char* reason, void* /*ctx*/) {
    if (g_console) {
        g_console->on_local_term_event(sid, event_type, data, len, reason);
    }
}

static void build_default_pico_config(OykConfig* cfg) {
    const PicoBoardProfile* prof = pico_current_board_profile();
    oyk_config_init_default(cfg);
    oyk_parse_callsign("N0CALL-7", &cfg->callsign);
    oyk_parse_callsign("OYKNOD", &cfg->alias);
    sprintf(cfg->info_text, "OYK Packet Node (%s)", prof->board_label);

    cfg->bbs_enabled = 1;
    cfg->bbs_ssid = 11;
    oyk_parse_callsign("OYKBBS", &cfg->bbs_alias);
    strcpy(cfg->bbs_mail_path, "mail/");
    strcpy(cfg->bbs_files_path, "files/");

    cfg->chat_enabled = 1;
    cfg->chat_ssid = 12;
    oyk_parse_callsign("OYKCHT", &cfg->chat_alias);
    strcpy(cfg->chat_topic, "Lobby");

    if (prof->has_picocalc_hw) {
        if (prof->radio_mode == PICO_RADIO_NONE) {
            cfg->iface_count = 1;
            memset(&cfg->ifaces[0], 0, sizeof(cfg->ifaces[0]));
            strcpy(cfg->ifaces[0].id, "ppp0");
            strcpy(cfg->ifaces[0].type, "ppp");
            strcpy(cfg->ifaces[0].description, "RadioFoundry PPP (ser0)");
            strcpy(cfg->ifaces[0].device, "ser0");
            strcpy(cfg->ifaces[0].host, "10.44.0.1");
            cfg->ifaces[0].port = 10093;
            cfg->ifaces[0].baud = 115200UL;
        } else {
            cfg->iface_count = 3;
            memset(&cfg->ifaces[0], 0, sizeof(cfg->ifaces[0]));
            strcpy(cfg->ifaces[0].id, "wifi0");
            strcpy(cfg->ifaces[0].type, "axudp");
            strcpy(cfg->ifaces[0].description, "CYW43 Wi-Fi AXUDP");
            strcpy(cfg->ifaces[0].host, "0.0.0.0");
            cfg->ifaces[0].port = 10093;

            memset(&cfg->ifaces[1], 0, sizeof(cfg->ifaces[1]));
            strcpy(cfg->ifaces[1].id, "ppp0");
            strcpy(cfg->ifaces[1].type, "ppp");
            strcpy(cfg->ifaces[1].description, "RadioFoundry PPP (ser0)");
            strcpy(cfg->ifaces[1].device, "ser0");
            strcpy(cfg->ifaces[1].host, "10.44.0.1");
            cfg->ifaces[1].port = 10093;
            cfg->ifaces[1].baud = 115200UL;

            memset(&cfg->ifaces[2], 0, sizeof(cfg->ifaces[2]));
            strcpy(cfg->ifaces[2].id, "bt0");
            strcpy(cfg->ifaces[2].type, "kiss-bt");
            strcpy(cfg->ifaces[2].description, "CYW43 Bluetooth SPP/BLE KISS");
        }
        return;
    }

    cfg->iface_count = 4;

    /* 0: CYW43 / RM2 Wi-Fi AXUDP */
    memset(&cfg->ifaces[0], 0, sizeof(cfg->ifaces[0]));
    strcpy(cfg->ifaces[0].id, "wifi0");
    strcpy(cfg->ifaces[0].type, "axudp");
    strcpy(cfg->ifaces[0].description, "CYW43/RM2 Wi-Fi AXUDP");
    strcpy(cfg->ifaces[0].host, "0.0.0.0");
    cfg->ifaces[0].port = 10093;

    /* 1: RadioFoundry PPP on Hardware UART0 (ser0: TX=0 RX=1 CTS=2 RTS=3) */
    memset(&cfg->ifaces[1], 0, sizeof(cfg->ifaces[1]));
    strcpy(cfg->ifaces[1].id, "ppp0");
    strcpy(cfg->ifaces[1].type, "ppp");
    strcpy(cfg->ifaces[1].description, "RadioFoundry PPP (ser0)");
    strcpy(cfg->ifaces[1].device, "ser0");
    strcpy(cfg->ifaces[1].host, "10.44.0.1");
    cfg->ifaces[1].port = 10093;
    cfg->ifaces[1].baud = 115200UL;

    /* 2: KISS TNC on Hardware UART1 (ser1: TX=4 RX=5 CTS=6 RTS=7) */
    memset(&cfg->ifaces[2], 0, sizeof(cfg->ifaces[2]));
    strcpy(cfg->ifaces[2].id, "kiss0");
    strcpy(cfg->ifaces[2].type, "kiss-serial");
    strcpy(cfg->ifaces[2].description, "VHF KISS TNC (ser1)");
    strcpy(cfg->ifaces[2].device, "ser1");
    cfg->ifaces[2].baud = 9600UL;

    /* 3: Bluetooth KISS via CYW43 / RM2 */
    memset(&cfg->ifaces[3], 0, sizeof(cfg->ifaces[3]));
    strcpy(cfg->ifaces[3].id, "bt0");
    strcpy(cfg->ifaces[3].type, "kiss-bt");
    strcpy(cfg->ifaces[3].description, "CYW43/RM2 Bluetooth SPP/BLE KISS");
}

int main() {
    stdio_init_all();
    pico_board_init();
    pico_fs_init(1, 0);

    OykConfig cfg;
    build_default_pico_config(&cfg);
    if (pico_fs_exists("node.cfg")) {
        char err[96];
        OykConfig loaded;
        if (oyk_config_load("node.cfg", &loaded, err, sizeof(err))) {
            cfg = loaded;
        }
    }

    static OykPortableNode s_node_storage;
    static PicoNetBridge s_net_storage;
    static PicoConsole s_console_storage;
    g_node = &s_node_storage;
    g_net = &s_net_storage;
    g_console = &s_console_storage;

    char err[96];
    g_node->init(&cfg, PicoNetBridge::tx_callback_trampoline, g_net, err, sizeof(err));
    g_node->set_local_event_callback(local_term_trampoline, NULL);

    PicoRadioConfig rcfg;
    pico_radio_default_config(&rcfg);
    g_net->init(g_node, &rcfg, 1);

#if defined(OYK_PICO_IS_PICOCALC) && OYK_PICO_IS_PICOCALC
    static PicoCalcConsole s_pcalc_storage;
    g_console->init(g_node, g_net, PicoCalcConsole::tx_trampoline, &s_pcalc_storage);
    s_pcalc_storage.init(g_console);
    s_pcalc_storage.connect_and_probe();

    while (true) {
        const oyk_u32 now = pico_board_millis();
        g_net->poll(now);
        g_node->tick();
        g_console->tick(now);
        s_pcalc_storage.poll_tick(now);

        if ((now - g_last_blink_ms) >= 1000UL) {
            g_last_blink_ms = now;
            g_blink_state = !g_blink_state;
            pico_board_set_status_led(g_blink_state);
        }
        sleep_ms(2);
    }
#else
    g_console->init(g_node, g_net, usb_cdc_console_tx, NULL);

    while (true) {
        const oyk_u32 now = pico_board_millis();
        const bool connected = stdio_usb_connected();
        if (connected && !g_usb_connected) {
            g_usb_connected = true;
            g_console->on_usb_connect();
        } else if (!connected && g_usb_connected) {
            g_usb_connected = false;
        }

        int ch;
        oyk_u8 rx_buf[64];
        size_t rx_n = 0;
        while (rx_n < sizeof(rx_buf) && (ch = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
            rx_buf[rx_n++] = static_cast<oyk_u8>(ch & 0xFF);
        }
        if (rx_n > 0) {
            g_console->feed_input(rx_buf, rx_n);
        }

        g_net->poll(now);
        g_node->tick();
        g_console->tick(now);

        if ((now - g_last_blink_ms) >= 1000UL) {
            g_last_blink_ms = now;
            g_blink_state = !g_blink_state;
            pico_board_set_status_led(g_blink_state);
        }

        sleep_ms(2);
    }
#endif
    return 0;
}
