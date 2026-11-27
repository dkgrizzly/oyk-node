/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "esp32c6_oled.hpp"

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "driver/spi_master.h"
#endif

#define C6L_FB_BYTES ((OYK_C6L_OLED_WIDTH * OYK_C6L_OLED_HEIGHT) / 8)

static oyk_u8 s_fb[C6L_FB_BYTES];
static int    s_page = C6L_OLED_PAGE_NODE;
static int    s_oled_on = 1;
#ifdef ESP_PLATFORM
static spi_device_handle_t s_oled_spi = NULL;
#endif

/* Minimal 3x5 uppercase glyph table so 64x48 OLED renders 12 chars x 7 lines crisply */
static oyk_u16 oled_glyph3x5(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
    switch (c) {
        case '0': return 0x7B6FU; case '1': return 0x2492U; case '2': return 0x73E7U;
        case '3': return 0x7397U; case '4': return 0x5BC9U; case '5': return 0x79CFU;
        case '6': return 0x79EFU; case '7': return 0x7249U; case '8': return 0x7BEFU;
        case '9': return 0x7BCFU; case 'A': return 0x2BEBU; case 'B': return 0x6AEBU;
        case 'C': return 0x7927U; case 'D': return 0x6B6EU; case 'E': return 0x79C7U;
        case 'F': return 0x79C4U; case 'G': return 0x79AFU; case 'H': return 0x5BEBU;
        case 'I': return 0x7497U; case 'J': return 0x124EU; case 'K': return 0x5AEBU;
        case 'L': return 0x4927U; case 'M': return 0x5FEBU; case 'N': return 0x5F6BU;
        case 'O': return 0x7B6FU; case 'P': return 0x7BC4U; case 'Q': return 0x7B6BU;
        case 'R': return 0x7AEBU; case 'S': return 0x799EU; case 'T': return 0x7492U;
        case 'U': return 0x5B6FU; case 'V': return 0x5B5AU; case 'W': return 0x5B7FU;
        case 'X': return 0x5555U; case 'Y': return 0x5592U; case 'Z': return 0x72A7U;
        case '-': return 0x01C0U; case ':': return 0x0410U; case '.': return 0x0002U;
        case '/': return 0x12A4U; case '|': return 0x2492U; case '+': return 0x05D0U;
        default:  return 0x0000U;
    }
}

static void fb_set_px(int x, int y, int on) {
    if (x < 0 || x >= OYK_C6L_OLED_WIDTH || y < 0 || y >= OYK_C6L_OLED_HEIGHT) return;
    int byte_idx = (y / 8) * OYK_C6L_OLED_WIDTH + x;
    oyk_u8 mask = static_cast<oyk_u8>(1U << (y & 7));
    if (on) s_fb[byte_idx] |= mask;
    else s_fb[byte_idx] &= static_cast<oyk_u8>(~mask);
}

static void fb_draw_char(int x, int y, char c) {
    oyk_u16 g = oled_glyph3x5(c);
    for (int r = 0; r < 5; ++r) {
        int shift = (4 - r) * 3;
        for (int col = 0; col < 3; ++col) {
            if ((g >> (shift + (2 - col))) & 1U) {
                fb_set_px(x + col, y + r, 1);
            }
        }
    }
}

static void fb_draw_text(int x, int y, const char* txt) {
    if (!txt) return;
    for (int i = 0; txt[i] && (x + i * 4 + 3) <= OYK_C6L_OLED_WIDTH; ++i) {
        fb_draw_char(x + i * 4, y, txt[i]);
    }
}

static void oled_flush_hw() {
#ifdef ESP_PLATFORM
    if (!s_oled_spi) return;
    for (int page = 0; page < 6; ++page) {
        oyk_u8 cmds[3];
        cmds[0] = static_cast<oyk_u8>(0xB0U | page);
        cmds[1] = 0x00U;
        cmds[2] = 0x10U;
        gpio_set_level(static_cast<gpio_num_t>(OYK_C6L_OLED_DC_PIN), 0);
        spi_transaction_t tc;
        memset(&tc, 0, sizeof(tc));
        tc.length = 24;
        tc.tx_buffer = cmds;
        spi_device_polling_transmit(s_oled_spi, &tc);

        gpio_set_level(static_cast<gpio_num_t>(OYK_C6L_OLED_DC_PIN), 1);
        spi_transaction_t td;
        memset(&td, 0, sizeof(td));
        td.length = OYK_C6L_OLED_WIDTH * 8;
        td.tx_buffer = s_fb + page * OYK_C6L_OLED_WIDTH;
        spi_device_polling_transmit(s_oled_spi, &td);
    }
#endif
}

void esp32c6_oled_init() {
    memset(s_fb, 0, sizeof(s_fb));
    s_page = C6L_OLED_PAGE_NODE;
    s_oled_on = 1;
#ifdef ESP_PLATFORM
    gpio_set_direction(static_cast<gpio_num_t>(OYK_C6L_OLED_DC_PIN), GPIO_MODE_OUTPUT);
    gpio_set_direction(static_cast<gpio_num_t>(OYK_C6L_OLED_RST_PIN), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(OYK_C6L_OLED_RST_PIN), 0);
    gpio_set_level(static_cast<gpio_num_t>(OYK_C6L_OLED_RST_PIN), 1);
    if (!s_oled_spi) {
        spi_device_interface_config_t dcfg;
        memset(&dcfg, 0, sizeof(dcfg));
        dcfg.clock_speed_hz = 4000000;
        dcfg.mode = 0;
        dcfg.spics_io_num = static_cast<int>(OYK_C6L_OLED_CS_PIN);
        dcfg.queue_size = 2;
        spi_bus_add_device(SPI2_HOST, &dcfg, &s_oled_spi);
    }
    const oyk_u8 init_seq[6] = { 0xAEU, 0x8DU, 0x14U, 0x20U, 0x02U, 0xAFU };
    gpio_set_level(static_cast<gpio_num_t>(OYK_C6L_OLED_DC_PIN), 0);
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = sizeof(init_seq) * 8U;
    t.tx_buffer = init_seq;
    if (s_oled_spi) spi_device_polling_transmit(s_oled_spi, &t);
#endif
}

void esp32c6_oled_set_page(int page) {
    if (page < 0) page = 0;
    s_page = page % C6L_OLED_PAGE_COUNT;
}

int esp32c6_oled_page() { return s_page; }

int esp32c6_oled_next_page() {
    s_page = (s_page + 1) % C6L_OLED_PAGE_COUNT;
    return s_page;
}

void esp32c6_oled_set_enabled(int on) {
    s_oled_on = on ? 1 : 0;
}

int esp32c6_oled_enabled() { return s_oled_on; }

void esp32c6_oled_render(const Esp32C6OledSnapshot* snap) {
    memset(s_fb, 0, sizeof(s_fb));
    if (!s_oled_on || !snap) {
        oled_flush_hw();
        return;
    }
    char line[48];
    /* Top inverted banner header: 64x7 px */
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < OYK_C6L_OLED_WIDTH; ++x) fb_set_px(x, y, 1);
    }
    static const char* kHdr[C6L_OLED_PAGE_COUNT] = {
        "OYK NODE P1/4", "LORA SX1262 P2", "NET/GROVE P3", "SESS/HRD P4"
    };
    const char* h = kHdr[s_page % C6L_OLED_PAGE_COUNT];
    for (int i = 0; h[i] && (2 + i * 4 + 3) <= OYK_C6L_OLED_WIDTH; ++i) {
        oyk_u16 g = oled_glyph3x5(h[i]);
        for (int r = 0; r < 5; ++r) {
            int sh = (4 - r) * 3;
            for (int c = 0; c < 3; ++c) {
                if ((g >> (sh + (2 - c))) & 1U) fb_set_px(2 + i * 4 + c, 1 + r, 0);
            }
        }
    }

    if (s_page == C6L_OLED_PAGE_NODE) {
        fb_draw_text(2, 10, snap->callsign);
        fb_draw_text(2, 18, snap->alias);
        sprintf(line, "IN:%s", snap->accept_incoming ? "ALLOW" : "DENY");
        fb_draw_text(2, 26, line);
        sprintf(line, "CON:%d UP:%lus", snap->active_conns, static_cast<unsigned long>(snap->uptime_sec));
        fb_draw_text(2, 34, line);
    } else if (s_page == C6L_OLED_PAGE_LORA) {
        sprintf(line, "%.3fM", static_cast<double>(snap->lora_freq_hz) / 1000000.0);
        fb_draw_text(2, 10, line);
        sprintf(line, "RX:%lu TX:%lu", static_cast<unsigned long>(snap->lora_rx_pkts),
                static_cast<unsigned long>(snap->lora_tx_pkts));
        fb_draw_text(2, 18, line);
        sprintf(line, "RSSI:%d", snap->last_rssi_dbm);
        fb_draw_text(2, 26, line);
        fb_draw_text(2, 34, "LNA:ON ANT:ON");
    } else if (s_page == C6L_OLED_PAGE_NET) {
        sprintf(line, "WIFI:%s", snap->wifi_connected ? "UP" : "STBY");
        fb_draw_text(2, 10, line);
        oyk_u32 ip = snap->wifi_ip_net;
        sprintf(line, "%lu.%lu.%lu.%lu",
                static_cast<unsigned long>(ip & 0xFFU),
                static_cast<unsigned long>((ip >> 8) & 0xFFU),
                static_cast<unsigned long>((ip >> 16) & 0xFFU),
                static_cast<unsigned long>((ip >> 24) & 0xFFU));
        fb_draw_text(2, 18, line);
        sprintf(line, "BLE5:%s", snap->bt_enabled ? "ADV" : "OFF");
        fb_draw_text(2, 26, line);
        sprintf(line, "SER0:%lu", static_cast<unsigned long>(snap->grove_baud));
        fb_draw_text(2, 34, line);
    } else {
        sprintf(line, "CONNS:%d", snap->active_conns);
        fb_draw_text(2, 10, line);
        fb_draw_text(2, 20, "LAST HEARD:");
        fb_draw_text(2, 28, snap->last_heard[0] ? snap->last_heard : "NONE");
    }
    oled_flush_hw();
}

const oyk_u8* esp32c6_oled_framebuffer() { return s_fb; }
size_t esp32c6_oled_framebuffer_bytes() { return sizeof(s_fb); }

int esp32c6_oled_pixel(int x, int y) {
    if (x < 0 || x >= OYK_C6L_OLED_WIDTH || y < 0 || y >= OYK_C6L_OLED_HEIGHT) return 0;
    int byte_idx = (y / 8) * OYK_C6L_OLED_WIDTH + x;
    return (s_fb[byte_idx] & static_cast<oyk_u8>(1U << (y & 7))) ? 1 : 0;
}
