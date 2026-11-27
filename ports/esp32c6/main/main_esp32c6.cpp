/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_esp32c6_config.h"
#include "oyk_portable.hpp"
#include "board_esp32c6.hpp"
#include "esp32c6_lora.hpp"
#include "esp32c6_oled.hpp"
#include "esp32c6_fs.hpp"
#include "esp32c6_radio.hpp"
#include "esp32c6_net_bridge.hpp"
#include "esp32c6_console.hpp"

#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static OykPortableNode*   g_c6l_node = NULL;
static Esp32C6NetBridge*  g_c6l_net = NULL;
static Esp32C6Console*    g_c6l_console = NULL;

static size_t c6l_usb_console_tx(const char* data, size_t len, void* /*ctx*/) {
    if (!data || len == 0) return 0;
    int wr = usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(20));
    return (wr > 0) ? static_cast<size_t>(wr) : 0;
}

static void c6l_local_term_trampoline(oyk_u32 sid, int ev,
                                      const oyk_u8* data, size_t len,
                                      const char* reason, void* /*ctx*/) {
    if (g_c6l_console) g_c6l_console->on_local_term_event(sid, ev, data, len, reason);
}

static void build_default_esp32c6_config(OykConfig* cfg) {
    oyk_config_init_default(cfg);
    oyk_parse_callsign("N0CALL-7", &cfg->callsign);
    oyk_parse_callsign("OYKNOD", &cfg->alias);
    strcpy(cfg->info_text, "OYK Packet Node (M5Stack Unit C6L)");
    cfg->bbs_enabled = 1;
    cfg->bbs_ssid = 11;
    oyk_parse_callsign("OYKBBS", &cfg->bbs_alias);
    cfg->chat_enabled = 1;
    cfg->chat_ssid = 12;
    oyk_parse_callsign("OYKCHT", &cfg->chat_alias);

    cfg->iface_count = 4;
    memset(&cfg->ifaces[0], 0, sizeof(cfg->ifaces[0]));
    strcpy(cfg->ifaces[0].id, "lora0");
    strcpy(cfg->ifaces[0].type, "kiss-serial");
    strcpy(cfg->ifaces[0].device, "lora0");
    strcpy(cfg->ifaces[0].description, "SX1262 LoRa KISS");

    memset(&cfg->ifaces[1], 0, sizeof(cfg->ifaces[1]));
    strcpy(cfg->ifaces[1].id, "wifi0");
    strcpy(cfg->ifaces[1].type, "axudp");
    strcpy(cfg->ifaces[1].description, "Wi-Fi 6 AXUDP");
    strcpy(cfg->ifaces[1].host, "0.0.0.0");
    cfg->ifaces[1].port = 10093;

    memset(&cfg->ifaces[2], 0, sizeof(cfg->ifaces[2]));
    strcpy(cfg->ifaces[2].id, "ppp0");
    strcpy(cfg->ifaces[2].type, "ppp");
    strcpy(cfg->ifaces[2].device, "ser0");
    strcpy(cfg->ifaces[2].host, "10.44.0.1");
    cfg->ifaces[2].port = 10093;
    cfg->ifaces[2].baud = 115200UL;

    memset(&cfg->ifaces[3], 0, sizeof(cfg->ifaces[3]));
    strcpy(cfg->ifaces[3].id, "bt0");
    strcpy(cfg->ifaces[3].type, "kiss-bt");
    strcpy(cfg->ifaces[3].description, "BLE 5 KISS Service");
}

extern "C" void app_main(void) {
    usb_serial_jtag_driver_config_t ucfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&ucfg);

    esp32c6_board_init();
    esp32c6_fs_init();
    esp32c6_oled_init();
    esp32c6_board_buzzer_beep(1760U, 30U);

    static OykConfig cfg;
    build_default_esp32c6_config(&cfg);

    g_c6l_node = new OykPortableNode();
    g_c6l_net = new Esp32C6NetBridge();
    g_c6l_console = new Esp32C6Console();

    char err[96];
    g_c6l_node->init(&cfg, Esp32C6NetBridge::tx_callback_trampoline, g_c6l_net, err, sizeof(err));
    g_c6l_node->set_local_event_callback(c6l_local_term_trampoline, NULL);

    Esp32C6RadioConfig rcfg; esp32c6_radio_default_config(&rcfg);
    Esp32C6LoraConfig lcfg; esp32c6_lora_default_config(&lcfg);
    g_c6l_net->init(g_c6l_node, &rcfg, &lcfg);
    g_c6l_console->init(g_c6l_node, g_c6l_net, c6l_usb_console_tx, NULL);
    g_c6l_console->on_usb_connect();

    oyk_u32 last_oled_ms = 0;
    while (true) {
        oyk_u32 now = esp32c6_board_millis();
        oyk_u8 rx[64];
        int n = usb_serial_jtag_read_bytes(rx, sizeof(rx), 0);
        if (n > 0) g_c6l_console->feed_input(rx, static_cast<size_t>(n));
        if (esp32c6_board_poll_button_press(now)) {
            esp32c6_oled_next_page();
            esp32c6_board_buzzer_beep(2200U, 15U);
            last_oled_ms = 0;
        }
        g_c6l_net->poll(now);
        g_c6l_node->tick();
        g_c6l_console->tick(now);
        if (last_oled_ms == 0 || (now - last_oled_ms) >= 500UL) {
            Esp32C6OledSnapshot snap;
            g_c6l_console->render_oled(&snap);
            last_oled_ms = now;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
