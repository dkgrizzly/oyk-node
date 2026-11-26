/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * TinyTMUX / Teensy 4.1 (NXP IMXRT1062) Hardware Board Pinout & HAL.
 *
 * Pinout matches ~/TinyTMUX/TinyTMUX.ino:
 *   ser0 -> Serial1 (LPUART6): RX=0,  TX=1,  CTS=33, RTS=6
 *   ser1 -> Serial2 (LPUART4): RX=7,  TX=8,  CTS=2,  RTS=9
 *   ser2 -> Serial6 (LPUART1): RX=25, TX=24, CTS=3,  RTS=26
 *   ser3 -> Serial7 (LPUART7): RX=28, TX=29, CTS=4,  RTS=27
 *   ser4 -> Serial8 (LPUART5): RX=34, TX=35, CTS=5,  RTS=36
 *   ser5 -> Serial3 (LPUART2): RX=15, TX=14, CTS=30, RTS=41
 *   ser6 -> Serial4 (LPUART3): RX=16, TX=17, CTS=31, RTS=40
 *   ser7 -> Serial5 (LPUART8): RX=21, TX=20, CTS=32, RTS=22
 *   Pin 18 -> 100 Mbps Ethernet Link Speed LED
 */

#include "board_tinytmux.hpp"
#include "tinytmux_psram.hpp"

#include <string.h>

static const TinyTmuxUartMap s_uart_map[TINYTMUX_UART_COUNT] = {
    { 0, 1, 6, IMXRT_LPUART6_BASE,  0,  1, 33,  6, "ser0" },
    { 1, 2, 4, IMXRT_LPUART4_BASE,  7,  8,  2,  9, "ser1" },
    { 2, 6, 1, IMXRT_LPUART1_BASE, 25, 24,  3, 26, "ser2" },
    { 3, 7, 7, IMXRT_LPUART7_BASE, 28, 29,  4, 27, "ser3" },
    { 4, 8, 5, IMXRT_LPUART5_BASE, 34, 35,  5, 36, "ser4" },
    { 5, 3, 2, IMXRT_LPUART2_BASE, 15, 14, 30, 41, "ser5" },
    { 6, 4, 3, IMXRT_LPUART3_BASE, 16, 17, 31, 40, "ser6" },
    { 7, 5, 8, IMXRT_LPUART8_BASE, 21, 20, 32, 22, "ser7" }
};

const TinyTmuxUartMap* tinytmux_uart_table() {
    return s_uart_map;
}

const TinyTmuxUartMap* tinytmux_uart_info(int logical_port) {
    if (logical_port < 0 || logical_port >= TINYTMUX_UART_COUNT) return NULL;
    return &s_uart_map[logical_port];
}

void tinytmux_decode_ocotp_mac(oyk_u32 ocotp_mac0, oyk_u32 ocotp_mac1, oyk_u8 mac_out[6]) {
    if (!mac_out) return;
    /* Matches IMXRT1060 fuse layout used by Teensy 4.1 */
    mac_out[0] = static_cast<oyk_u8>((ocotp_mac1 >> 8) & 0xFFU);
    mac_out[1] = static_cast<oyk_u8>(ocotp_mac1 & 0xFFU);
    mac_out[2] = static_cast<oyk_u8>((ocotp_mac0 >> 24) & 0xFFU);
    mac_out[3] = static_cast<oyk_u8>((ocotp_mac0 >> 16) & 0xFFU);
    mac_out[4] = static_cast<oyk_u8>((ocotp_mac0 >> 8) & 0xFFU);
    mac_out[5] = static_cast<oyk_u8>(ocotp_mac0 & 0xFFU);
}

#if defined(TEENSYDUINO)

#include <Arduino.h>

static HardwareSerialIMXRT* teensy_serial_by_num(oyk_u8 teensy_serial) {
    switch (teensy_serial) {
        case 1: return &Serial1;
        case 2: return &Serial2;
        case 3: return &Serial3;
        case 4: return &Serial4;
        case 5: return &Serial5;
        case 6: return &Serial6;
        case 7: return &Serial7;
        case 8: return &Serial8;
        default: return NULL;
    }
}

void tinytmux_board_init() {
    pinMode(13, OUTPUT);
    digitalWrite(13, HIGH);
    pinMode(TINYTMUX_LINK_LED_PIN, OUTPUT);
    digitalWrite(TINYTMUX_LINK_LED_PIN, LOW);
    tinytmux_psram_init_and_test();
}

void tinytmux_board_read_mac(oyk_u8 mac_out[6]) {
    tinytmux_decode_ocotp_mac(static_cast<oyk_u32>(HW_OCOTP_MAC0),
                              static_cast<oyk_u32>(HW_OCOTP_MAC1),
                              mac_out);
}

void tinytmux_board_set_status_led(int on) {
    digitalWrite(13, on ? HIGH : LOW);
}

void tinytmux_board_set_link_led(int speed_100mbps) {
    digitalWrite(TINYTMUX_LINK_LED_PIN, speed_100mbps ? HIGH : LOW);
}

void tinytmux_board_set_usbh_vbus(int on) {
    /* Managed safely by Teensyduino USBHost_t36::begin(). */
    (void)on;
}

const TinyTmuxPsramInfo* tinytmux_board_psram_info() {
    return tinytmux_psram_info();
}

oyk_u32 tinytmux_board_millis() {
    return static_cast<oyk_u32>(millis());
}

int tinytmux_uart_init(int logical_port, const TinyTmuxUartConfig* cfg) {
    const TinyTmuxUartMap* m = tinytmux_uart_info(logical_port);
    if (!m || !cfg || cfg->baudrate == 0) return 0;
    HardwareSerialIMXRT* sp = teensy_serial_by_num(m->teensy_serial);
    if (!sp) return 0;

    sp->begin(static_cast<uint32_t>(cfg->baudrate));
    if (cfg->cts_rts) {
        sp->attachRts(m->pin_rts);
        sp->attachCts(m->pin_cts);
    }
    return 1;
}

size_t tinytmux_uart_write(int logical_port, const oyk_u8* data, size_t len) {
    const TinyTmuxUartMap* m = tinytmux_uart_info(logical_port);
    if (!m || !data || len == 0) return 0;
    HardwareSerialIMXRT* sp = teensy_serial_by_num(m->teensy_serial);
    if (!sp) return 0;
    return sp->write(data, len);
}

size_t tinytmux_uart_read(int logical_port, oyk_u8* out, size_t max_len) {
    const TinyTmuxUartMap* m = tinytmux_uart_info(logical_port);
    if (!m || !out || max_len == 0) return 0;
    HardwareSerialIMXRT* sp = teensy_serial_by_num(m->teensy_serial);
    if (!sp) return 0;

    size_t got = 0;
    while (got < max_len && sp->available() > 0) {
        int ch = sp->read();
        if (ch < 0) break;
        out[got++] = static_cast<oyk_u8>(ch & 0xFF);
    }
    return got;
}

extern "C" void _reboot_Teensyduino_(void);

void tinytmux_board_enter_bootloader() {
    Serial.send_now();
    Serial.flush();
    SerialUSB1.flush();
    delay(250);
    _reboot_Teensyduino_();
}

int tinytmux_board_sim_bootloader_count() {
    return 0;
}

void tinytmux_uart_set_sim_tx_hook(TinyTmuxUartSimTxFn /*cb*/, void* /*ctx*/) {}

extern "C" void tinytmux_early_puts(const char* /*s*/) {}
extern "C" void tinytmux_early_puthex(oyk_u32 /*v*/) {}

#else

/* Host stub implementation for Linux unit tests & simulation */
static int s_host_status_led = 0;
static int s_host_led = 0;
static int s_host_usbh_vbus = 0;
static oyk_u32 s_host_ms = 0;
static int s_host_bootloader_calls = 0;
static TinyTmuxUartSimTxFn s_host_uart_tx_cb = NULL;
static void* s_host_uart_tx_ctx = NULL;

void tinytmux_board_init() {
    s_host_status_led = 0;
    s_host_led = 0;
    s_host_usbh_vbus = 0;
    s_host_ms = 0;
    tinytmux_psram_init_and_test();
}

void tinytmux_board_read_mac(oyk_u8 mac_out[6]) {
    tinytmux_decode_ocotp_mac(0x004F594BUL, 0x000004E9UL, mac_out);
}

void tinytmux_board_set_status_led(int on) {
    s_host_status_led = on ? 1 : 0;
    (void)s_host_status_led;
}

void tinytmux_board_set_link_led(int speed_100mbps) {
    s_host_led = speed_100mbps ? 1 : 0;
}

void tinytmux_board_set_usbh_vbus(int on) {
    s_host_usbh_vbus = on ? 1 : 0;
    (void)s_host_usbh_vbus;
}

const TinyTmuxPsramInfo* tinytmux_board_psram_info() {
    return tinytmux_psram_info();
}

oyk_u32 tinytmux_board_millis() {
    return s_host_ms;
}

void tinytmux_board_enter_bootloader() {
    s_host_bootloader_calls++;
}

int tinytmux_board_sim_bootloader_count() {
    return s_host_bootloader_calls;
}

int tinytmux_uart_init(int logical_port, const TinyTmuxUartConfig* cfg) {
    return (tinytmux_uart_info(logical_port) != NULL && cfg != NULL && cfg->baudrate > 0) ? 1 : 0;
}

size_t tinytmux_uart_write(int logical_port, const oyk_u8* data, size_t len) {
    if (s_host_uart_tx_cb && data && len > 0) {
        s_host_uart_tx_cb(logical_port, data, len, s_host_uart_tx_ctx);
    }
    return len;
}

void tinytmux_uart_set_sim_tx_hook(TinyTmuxUartSimTxFn cb, void* ctx) {
    s_host_uart_tx_cb = cb;
    s_host_uart_tx_ctx = ctx;
}

size_t tinytmux_uart_read(int /*logical_port*/, oyk_u8* /*out*/, size_t /*max_len*/) {
    return 0;
}

extern "C" void tinytmux_early_puts(const char* /*s*/) {}
extern "C" void tinytmux_early_puthex(oyk_u32 /*v*/) {}

#endif
