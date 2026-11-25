/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * OYK Amateur Radio Packet Node - Teensy 4.1 / IMXRT1060 (TinyTMUX) Entry Point.
 *
 * Ties together:
 *   - Board HAL (IMXRT1062 clocks, Pin 18 100Mbps LED, OCOTP MAC, 8 TinyTMUX LPUARTs)
 *   - TinyUSB (MIT) Dual USB-CDC ACM (CDC0 = VT100 Operator Console, CDC1 = Auxiliary)
 *   - lwIP (BSD-3-Clause) + OykPppLink (10/100 Ethernet AXUDP/AXIP/Telnet + RadioFoundry PPP)
 *   - OykPortableNode (AX.25 L2, BBS SSID 11 + Sandboxed Files Area, CHAT SSID 12, Ban/Appeals)
 */

#include "../compat/oyk_portable.hpp"
#include "board_tinytmux.hpp"
#include "lwip_net_bridge.hpp"
#include "tinytmux_console.hpp"
#include "tinytmux_psram.hpp"

#include <stdio.h>
#include <string.h>

#if defined(OYK_HAS_TINYUSB)
#include "tusb.h"
#endif

static OykPortableNode g_node;
static TinyTmuxNetBridge g_net;
static TinyTmuxConsole g_console;

static size_t usb_cdc0_console_tx(const char* data, size_t len, void* /*ctx*/) {
#if defined(OYK_HAS_TINYUSB)
    if (!tud_cdc_n_connected(0)) return 0;
    size_t sent = 0;
    while (sent < len) {
        uint32_t avail = tud_cdc_n_write_available(0);
        if (avail == 0) {
            tud_cdc_n_write_flush(0);
            tud_task();
            continue;
        }
        uint32_t chunk = static_cast<uint32_t>(len - sent);
        if (chunk > avail) chunk = avail;
        uint32_t n = tud_cdc_n_write(0, data + sent, chunk);
        sent += n;
    }
    tud_cdc_n_write_flush(0);
    return sent;
#else
    (void)data;
    return len;
#endif
}

static void local_term_trampoline(oyk_u32 sid, int event_type,
                                  const oyk_u8* data, size_t len,
                                  const char* reason, void* /*ctx*/) {
    g_console.on_local_term_event(sid, event_type, data, len, reason);
}

static void build_default_tinytmux_config(OykConfig* cfg) {
    oyk_config_init_default(cfg);
    oyk_parse_callsign("N0CALL-7", &cfg->callsign);
    oyk_parse_callsign("OYKNOD", &cfg->alias);
    strcpy(cfg->info_text, "OYK Packet Node (Teensy 4.1 / TinyTMUX IMXRT1060)");

    /* Default interfaces: Ethernet AXUDP, Ethernet AXIP, RadioFoundry PPP on ser0, KISS on ser1 */
    cfg->iface_count = 4;

    /* 0: 10/100 Ethernet AXUDP */
    memset(&cfg->ifaces[0], 0, sizeof(cfg->ifaces[0]));
    strcpy(cfg->ifaces[0].id, "enet0");
    strcpy(cfg->ifaces[0].type, "axudp");
    strcpy(cfg->ifaces[0].description, "10/100 Ethernet AXUDP");
    strcpy(cfg->ifaces[0].host, "0.0.0.0");
    cfg->ifaces[0].port = 10093;

    /* 1: 10/100 Ethernet AXIP (IP Protocol 93) */
    memset(&cfg->ifaces[1], 0, sizeof(cfg->ifaces[1]));
    strcpy(cfg->ifaces[1].id, "axip0");
    strcpy(cfg->ifaces[1].type, "axip");
    strcpy(cfg->ifaces[1].description, "10/100 Ethernet AXIP");
    strcpy(cfg->ifaces[1].host, "0.0.0.0");

    /* 2: RadioFoundry PPP link on ser0 (LPUART6: RX=0, TX=1, CTS=33, RTS=6) */
    memset(&cfg->ifaces[2], 0, sizeof(cfg->ifaces[2]));
    strcpy(cfg->ifaces[2].id, "ppp0");
    strcpy(cfg->ifaces[2].type, "ppp");
    strcpy(cfg->ifaces[2].description, "RadioFoundry PPP (AXUDP/AXIP)");
    strcpy(cfg->ifaces[2].device, "ser0");
    strcpy(cfg->ifaces[2].host, "10.44.0.1");
    cfg->ifaces[2].baud = 115200UL;
    cfg->ifaces[2].port = 10093;

    /* 3: KISS Serial TNC on ser1 (LPUART4: RX=7, TX=8, CTS=2, RTS=9) */
    memset(&cfg->ifaces[3], 0, sizeof(cfg->ifaces[3]));
    strcpy(cfg->ifaces[3].id, "kiss0");
    strcpy(cfg->ifaces[3].type, "kiss-serial");
    strcpy(cfg->ifaces[3].description, "VHF KISS TNC on ser1");
    strcpy(cfg->ifaces[3].device, "ser1");
    cfg->ifaces[3].baud = 9600UL;
    cfg->ifaces[3].kiss_port = 0;
}

int main(void) {
    tinytmux_board_init();
    tinytmux_board_set_usbh_vbus(1);

#if defined(OYK_HAS_TINYUSB)
    tud_init(BOARD_TUD_RHPORT);
#if defined(CFG_TUH_ENABLED) && CFG_TUH_ENABLED
    tuh_init(BOARD_TUH_RHPORT);
#endif
#endif

    static OykConfig cfg;
    char err[96];
    if (!oyk_config_load("node.cfg", &cfg, err, sizeof(err))) {
        build_default_tinytmux_config(&cfg);
    }

    g_node.init(&cfg, TinyTmuxNetBridge::node_tx_callback, &g_net, err, sizeof(err));
    g_node.set_local_event_callback(local_term_trampoline, NULL);

    oyk_u32 now_ms = tinytmux_board_millis();
    g_net.init(&g_node, now_ms);
    g_console.init(&g_node, usb_cdc0_console_tx, NULL);
    g_console.set_net_bridge(&g_net);
    g_console.set_now_ms(now_ms);

    const TinyTmuxPsramInfo* psram = tinytmux_board_psram_info();
    if (psram) {
        char psram_tag[8], psram_msg[64];
        tinytmux_psram_format_short(psram, psram_tag, sizeof(psram_tag));
        sprintf(psram_msg, "FlexSPI2 PSRAM: %s (%lu MB usable, %d/%d chips)",
                psram_tag,
                static_cast<unsigned long>(psram->usable_bytes / (1024UL * 1024UL)),
                psram->usable_chip_count, psram->chip_count);
        g_node.log_message(OYK_LOG_INFO, psram_msg);
    }

    oyk_u8 mac[6];
    tinytmux_board_read_mac(mac);
    char mac_msg[64];
    sprintf(mac_msg, "IMXRT1060 MAC %02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    g_node.log_message(OYK_LOG_INFO, mac_msg);

    oyk_u32 last_tick_ms = now_ms;
    oyk_u32 last_draw_ms = now_ms;
    int cdc0_was_connected = 0;

    for (;;) {
        now_ms = tinytmux_board_millis();
        g_console.set_now_ms(now_ms);

#if defined(OYK_HAS_TINYUSB)
        tud_task();
        int cdc0_conn = tud_cdc_n_connected(0) ? 1 : 0;
        if (cdc0_conn && !cdc0_was_connected) {
            g_console.render();
        }
        cdc0_was_connected = cdc0_conn;

        if (cdc0_conn && tud_cdc_n_available(0) > 0) {
            oyk_u8 rx[64];
            uint32_t n = tud_cdc_n_read(0, rx, sizeof(rx));
            if (n > 0) {
                g_console.feed_input(rx, n);
            }
        }
#else
        (void)cdc0_was_connected;
#endif

        g_net.poll(now_ms);

        if ((now_ms - last_tick_ms) >= 1000UL) {
            last_tick_ms = now_ms;
            g_node.tick();
        }
        if ((now_ms - last_draw_ms) >= 500UL) {
            last_draw_ms = now_ms;
#if defined(OYK_HAS_TINYUSB)
            if (tud_cdc_n_connected(0) && g_console.view_mode() == TMUX_VIEW_CONSOLE) {
                g_console.render();
            }
#endif
        }
    }

    return 0;
}
