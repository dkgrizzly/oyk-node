/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_lyra_config.h"
#include "oyk_portable.hpp"
#include "board_lyra.hpp"
#include "lyra_fs.hpp"
#include "lyra_radio.hpp"
#include "lyra_net_bridge.hpp"
#include "lyra_console.hpp"

#if defined(OYK_LYRA_IS_PICOCALC) && OYK_LYRA_IS_PICOCALC
#include "lyra_picocalc_console.hpp"
#endif

#include <stdio.h>
#include <string.h>

static OykPortableNode* g_lyra_node = NULL;
static LyraNetBridge* g_lyra_net = NULL;
static LyraConsole* g_lyra_console = NULL;

static void lyra_local_term_trampoline(oyk_u32 sid, int event_type,
                                       const oyk_u8* data, size_t len,
                                       const char* reason, void* /*ctx*/) {
    if (g_lyra_console) {
        g_lyra_console->on_local_term_event(sid, event_type, data, len, reason);
    }
}

static void build_default_lyra_config(OykConfig* cfg) {
    const LyraBoardProfile* prof = lyra_board_current_profile();
    oyk_config_init_default(cfg);
    oyk_parse_callsign("N0CALL-7", &cfg->callsign);
    oyk_parse_callsign("OYKNOD", &cfg->alias);
    sprintf(cfg->info_text, "OYK Packet Node (%s)", prof->display_name);

    cfg->bbs_enabled = 1;
    cfg->bbs_ssid = 11;
    oyk_parse_callsign("OYKBBS", &cfg->bbs_alias);
    strcpy(cfg->bbs_mail_path, "mail/");
    strcpy(cfg->bbs_files_path, "files/");

    cfg->chat_enabled = 1;
    cfg->chat_ssid = 12;
    oyk_parse_callsign("OYKCHT", &cfg->chat_alias);
    strcpy(cfg->chat_topic, "Lobby");

    cfg->iface_count = 0;

    /* Interface 0: Primary IP transport (RMII Eth / Wi-Fi 6 / USB RNDIS) */
    {
        OykIfaceConfig& ic = cfg->ifaces[cfg->iface_count++];
        memset(&ic, 0, sizeof(ic));
        strcpy(ic.id, (prof->net_kind == LYRA_NET_WIFI6_BLE) ? "wifi0"
                      : ((prof->net_kind == LYRA_NET_RMII_ETH) ? "eth0" : "rndis0"));
        strcpy(ic.type, "axudp");
        strcpy(ic.description, "RK3506 lwIP/SAL AXUDP");
        strcpy(ic.host, "0.0.0.0");
        ic.port = 10093;
    }

    /* Interface 1: PPP over ser0 */
    {
        OykIfaceConfig& ic = cfg->ifaces[cfg->iface_count++];
        memset(&ic, 0, sizeof(ic));
        strcpy(ic.id, "ppp0");
        strcpy(ic.type, "ppp");
        strcpy(ic.description, "RadioFoundry PPP (ser0)");
        strcpy(ic.device, "ser0");
        strcpy(ic.host, "10.44.0.1");
        ic.port = 10093;
        ic.baud = 115200UL;
    }

    /* Interface 2 (standalone): KISS TNC on ser1; (lyra_zero_w): BLE KISS */
    if (!prof->has_picocalc_hw && prof->uart_count > 1U) {
        OykIfaceConfig& ic = cfg->ifaces[cfg->iface_count++];
        memset(&ic, 0, sizeof(ic));
        strcpy(ic.id, "kiss0");
        strcpy(ic.type, "kiss-serial");
        strcpy(ic.description, "VHF KISS TNC (ser1)");
        strcpy(ic.device, "ser1");
        ic.baud = 9600UL;
    }
    if (prof->net_kind == LYRA_NET_WIFI6_BLE && cfg->iface_count < OYK_MAX_INTERFACES) {
        OykIfaceConfig& ic = cfg->ifaces[cfg->iface_count++];
        memset(&ic, 0, sizeof(ic));
        strcpy(ic.id, "ble0");
        strcpy(ic.type, "kiss-bt");
        strcpy(ic.description, "Lyra Zero W BLE 5.2 KISS");
    }
}

#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD

#include <rtthread.h>
#include <rtdevice.h>

static volatile int g_lyra_running = 0;

#if !defined(OYK_LYRA_IS_PICOCALC) || !OYK_LYRA_IS_PICOCALC
static rt_device_t g_console_uart = RT_NULL;

static size_t rt_uart_console_tx(const char* data, size_t len, void* /*ctx*/) {
    if (!g_console_uart || !data || len == 0U) return 0U;
    rt_ssize_t written = rt_device_write(g_console_uart, 0, data, len);
    return (written > 0) ? static_cast<size_t>(written) : 0U;
}
#endif

static void oyk_lyra_net_thread_entry(void* /*parameter*/) {
    while (g_lyra_running) {
        const oyk_u32 now = lyra_uptime_ms();
        if (g_lyra_net) g_lyra_net->poll(now);
        if (g_lyra_node) g_lyra_node->tick();
        rt_thread_mdelay(2);
    }
}

static void oyk_lyra_ui_thread_entry(void* /*parameter*/) {
#if defined(OYK_LYRA_IS_PICOCALC) && OYK_LYRA_IS_PICOCALC
    static LyraPicoCalcConsole s_pcalc_console;
    g_lyra_console->init(g_lyra_node, g_lyra_net, LyraPicoCalcConsole::tx_trampoline, &s_pcalc_console);
    s_pcalc_console.init(g_lyra_console);
    s_pcalc_console.connect_and_probe();
    while (g_lyra_running) {
        const oyk_u32 now = lyra_uptime_ms();
        g_lyra_console->tick(now);
        s_pcalc_console.poll_tick(now);
        rt_thread_mdelay(4);
    }
#else
    g_console_uart = rt_console_get_device();
    g_lyra_console->init(g_lyra_node, g_lyra_net, rt_uart_console_tx, NULL);
    g_lyra_console->on_usb_connect();
    while (g_lyra_running) {
        if (g_console_uart) {
            oyk_u8 rx_buf[64];
            rt_ssize_t rx_n = rt_device_read(g_console_uart, 0, rx_buf, sizeof(rx_buf));
            if (rx_n > 0) {
                g_lyra_console->feed_input(rx_buf, static_cast<size_t>(rx_n));
            }
        }
        g_lyra_console->tick(lyra_uptime_ms());
        rt_thread_mdelay(4);
    }
#endif
}

extern "C" int oyk_lyra_start(void) {
    if (g_lyra_running) return 0;

    lyra_board_init();
    lyra_fs_init(1, 0, 0);

    OykConfig* cfg = new OykConfig();
    build_default_lyra_config(cfg);
    if (lyra_fs_exists("node.cfg")) {
        char err[96];
        OykConfig* loaded = new OykConfig();
        if (oyk_config_load("node.cfg", loaded, err, sizeof(err))) {
            *cfg = *loaded;
        }
        delete loaded;
    }

    static OykPortableNode s_node_storage;
    static LyraNetBridge s_net_storage;
    static LyraConsole s_console_storage;
    g_lyra_node = &s_node_storage;
    g_lyra_net = &s_net_storage;
    g_lyra_console = &s_console_storage;

    char err[96];
    g_lyra_node->init(cfg, LyraNetBridge::tx_callback_trampoline, g_lyra_net, err, sizeof(err));
    delete cfg;
    g_lyra_node->set_local_event_callback(lyra_local_term_trampoline, NULL);

    LyraRadioConfig rcfg;
    lyra_radio_default_config(&rcfg);
    g_lyra_net->init(g_lyra_node, &rcfg, 1);

    g_lyra_running = 1;
    rt_thread_t t_net = rt_thread_create("oyk_net", oyk_lyra_net_thread_entry, RT_NULL, 4096U, 15U, 10U);
    rt_thread_t t_ui  = rt_thread_create("oyk_ui",  oyk_lyra_ui_thread_entry,  RT_NULL, 4096U, 16U, 10U);
    if (t_net) rt_thread_startup(t_net);
    if (t_ui)  rt_thread_startup(t_ui);
    return 0;
}

INIT_APP_EXPORT(oyk_lyra_start);
MSH_CMD_EXPORT_ALIAS(oyk_lyra_start, oyk_node, Start OYK Amateur Radio Packet Node);

#endif /* OYK_LYRA_RTTHREAD */
