/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * OYK Amateur Radio Packet Node - Teensy 4.1 / TinyTMUX (Teensyduino 1.62)
 *
 * Build target: teensy:avr:teensy41:usb=serial2,speed=600,opt=o2std,keys=en-us
 *   - Serial    (USB-CDC0): VT100 Operator Console
 *   - SerialUSB1 (USB-CDC1): Auxiliary Status / Diagnostics
 *   - Serial1..8 (LPUART):  ser0..ser7 hardware UARTs (PPP / KISS TNC)
 *   - SD (BUILTIN_SDCARD):  4-bit SDIO microSD card (/node.cfg, mail/, files/, logs/)
 *   - Ethernet:             10/100 RMII PHY (QNEthernet primary, NativeEthernet fallback)
 *   - USB Host (USB_OTG2):  USBHost_t36 (USBHub + USBSerial_BigBuffer + CDC-ECM/NCM)
 */

#include "oyk_tinytmux_config.h"
#include "oyk_portable.hpp"
#include "board_tinytmux.hpp"
#include "lwip_net_bridge.hpp"
#include "tinytmux_console.hpp"
#include "tinytmux_enet.hpp"
#include "tinytmux_fs.hpp"
#include "tinytmux_psram.hpp"

#include <Arduino.h>
#include <SD.h>
#include <USBHost_t36.h>

#if defined(OYK_USE_NATIVE_ETHERNET)
#include <NativeEthernet.h>
#else
#include <QNEthernet.h>
#endif

#include <stdio.h>
#include <string.h>
#include <strings.h>

static OykPortableNode g_node;
static TinyTmuxNetBridge g_net;
static TinyTmuxConsole g_console;
static bool g_cdc0_connected = false;
static oyk_u32 g_last_blink_ms = 0;
static int g_blink_state = 1;

static size_t usb_cdc0_console_tx(const char* data, size_t len, void* /*ctx*/) {
    if (!data || len == 0 || !Serial) return 0;
    size_t sent = 0;
    unsigned long start = millis();
    while (sent < len) {
        int avail = Serial.availableForWrite();
        if (avail <= 0) {
            if ((millis() - start) > 25UL) break;
            yield();
            continue;
        }
        size_t chunk = len - sent;
        if (chunk > static_cast<size_t>(avail)) {
            chunk = static_cast<size_t>(avail);
        }
        size_t wrote = Serial.write(reinterpret_cast<const uint8_t*>(data + sent), chunk);
        if (wrote == 0) break;
        sent += wrote;
        start = millis();
    }
    return sent;
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
    strcpy(cfg->info_text, "OYK Packet Node (Teensy 4.1 / TinyTMUX Teensyduino)");

    cfg->bbs_enabled = 1;
    cfg->bbs_ssid = 11;
    oyk_parse_callsign("OYKBBS", &cfg->bbs_alias);
    strcpy(cfg->bbs_mail_path, "mail/");
    strcpy(cfg->bbs_files_path, "files/");

    cfg->chat_enabled = 1;
    cfg->chat_ssid = 12;
    oyk_parse_callsign("OYKCHT", &cfg->chat_alias);
    strcpy(cfg->chat_topic, "Lobby");

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

static bool ser0_claimed_by_config(const OykConfig* cfg) {
    if (!cfg) return false;
    for (int i = 0; i < cfg->iface_count; ++i) {
        if (strcasecmp(cfg->ifaces[i].device, "ser0") == 0) {
            return true;
        }
    }
    return false;
}

static void print_ser0_boot_summary(const OykConfig* cfg) {
    oyk_u8 mac[6];
    tinytmux_board_read_mac(mac);
    const TinyTmuxPsramInfo* ps = tinytmux_board_psram_info();
    char ps_short[12];
    tinytmux_psram_format_short(ps, ps_short, sizeof(ps_short));
    char call_str[16];
    oyk_format_callsign(&cfg->callsign, call_str, sizeof(call_str));

    Serial1.begin(115200);
    Serial1.printf("\r\nOYK Packet Node (Teensy 4.1 / TinyTMUX Teensyduino)\r\n");
    Serial1.printf("Callsign: %s  PSRAM: %s  SD: %s  MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   call_str,
                   ps_short,
                   tinytmux_fs_status_label(),
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void print_aux_status(Print& out) {
    char call_str[16], usb_short[12], ps_short[12];
    oyk_format_callsign(&g_node.config().callsign, call_str, sizeof(call_str));
    g_net.usb_host()->format_short_status(usb_short, sizeof(usb_short));
    tinytmux_psram_format_short(tinytmux_board_psram_info(), ps_short, sizeof(ps_short));
    oyk_u32 ip = tinytmux_enet_current_ip();
    out.printf("OYK %s | SD=%s | PSRAM=%s | ENET=%s (%u.%u.%u.%u) | USBH=%s | SESS=%d\r\n",
               call_str,
               tinytmux_fs_status_label(),
               ps_short,
               tinytmux_enet_link_is_up() ? (tinytmux_enet_speed_is_100m() ? "100M" : "10M") : "DOWN",
               static_cast<unsigned>(ip & 0xFFU),
               static_cast<unsigned>((ip >> 8) & 0xFFU),
               static_cast<unsigned>((ip >> 16) & 0xFFU),
               static_cast<unsigned>((ip >> 24) & 0xFFU),
               usb_short,
               g_node.total_active_connections());
}

void setup() {
    delay(50);
    Serial.begin(115200);
    SerialUSB1.begin(115200);

    tinytmux_board_init();
    int sd_ok = tinytmux_fs_mount();

    OykConfig cfg;
    build_default_tinytmux_config(&cfg);

    char cfg_err[128];
    cfg_err[0] = '\0';
    if (sd_ok) {
        oyk_config_load("/node.cfg", &cfg, cfg_err, sizeof(cfg_err));
    }

    if (!ser0_claimed_by_config(&cfg)) {
        print_ser0_boot_summary(&cfg);
    }

    char init_err[128];
    init_err[0] = '\0';
    g_node.init(&cfg, TinyTmuxNetBridge::node_tx_callback, &g_net, init_err, sizeof(init_err));
    g_node.set_local_event_callback(local_term_trampoline, NULL);

    oyk_u32 now = tinytmux_board_millis();
    g_net.init(&g_node, now);
    g_console.init(&g_node, usb_cdc0_console_tx, NULL);
    g_console.set_net_bridge(&g_net);
    g_console.set_now_ms(now);

    g_blink_state = 1;
    g_last_blink_ms = now;
    tinytmux_board_set_status_led(g_blink_state);
}

void loop() {
    oyk_u32 now = tinytmux_board_millis();
    g_console.tick(now);

    bool cdc0_now = static_cast<bool>(Serial);
    if (cdc0_now && !g_cdc0_connected) {
        g_cdc0_connected = true;
        g_console.render();
    } else if (!cdc0_now) {
        g_cdc0_connected = false;
    }

    if (Serial.available() > 0) {
        oyk_u8 rx[256];
        size_t n = 0;
        while (n < sizeof(rx) && Serial.available() > 0) {
            int ch = Serial.read();
            if (ch < 0) break;
            rx[n++] = static_cast<oyk_u8>(ch & 0xFF);
        }
        if (n > 0) {
            g_console.feed_input(rx, n);
        }
    }

    if (SerialUSB1.available() > 0) {
        bool dump_status = false;
        while (SerialUSB1.available() > 0) {
            int ch = SerialUSB1.read();
            if (ch == '?' || ch == 's' || ch == 'S' || ch == '\r' || ch == '\n') {
                dump_status = true;
            }
        }
        if (dump_status && static_cast<bool>(SerialUSB1)) {
            print_aux_status(SerialUSB1);
        }
    }

    g_net.poll(now);
    g_node.tick();

    if ((now - g_last_blink_ms) >= 500UL) {
        g_last_blink_ms = now;
        g_blink_state = !g_blink_state;
        tinytmux_board_set_status_led(g_blink_state);
    }
}
