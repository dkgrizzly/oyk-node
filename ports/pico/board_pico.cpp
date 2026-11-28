/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "board_pico.hpp"
#include "pico_psram.hpp"
#include "pico_radio.hpp"

#include <string.h>

static const PicoBoardProfile s_profiles[PICO_TOTAL_BOARD_PROFILES] = {
    {
        "pico_w",
        "Raspberry Pi Pico W",
        PICO_MCU_RP2040,
        2UL * 1024UL * 1024UL,
        264UL * 1024UL,
        0UL,
        -1,
        PICO_RADIO_ONBOARD_CYW43,
        255U,
        18U, 19U, 16U, 17U,
        0U, 2U
    },
    {
        "pico2_w",
        "Raspberry Pi Pico 2 W",
        PICO_MCU_RP2350A,
        4UL * 1024UL * 1024UL,
        520UL * 1024UL,
        0UL,
        -1,
        PICO_RADIO_ONBOARD_CYW43,
        255U,
        18U, 19U, 16U, 17U,
        0U, 2U
    },
    {
        "waveshare_core2350b",
        "Waveshare Core2350B",
        PICO_MCU_RP2350B,
        16UL * 1024UL * 1024UL,
        520UL * 1024UL,
        8UL * 1024UL * 1024UL,
        47,
        PICO_RADIO_OPTIONAL_RM2,
        39U,
        18U, 19U, 16U, 17U,
        0U, 2U
    },
    {
        "pimoroni_pga2350",
        "Pimoroni PGA2350",
        PICO_MCU_RP2350B,
        16UL * 1024UL * 1024UL,
        520UL * 1024UL,
        8UL * 1024UL * 1024UL,
        47,
        PICO_RADIO_OPTIONAL_RM2,
        25U,
        18U, 19U, 16U, 17U,
        0U, 2U
    },
    {
        "picocalc_pico2_w",
        "ClockworkPi PicoCalc (Pico 2 W)",
        PICO_MCU_RP2350A,
        4UL * 1024UL * 1024UL,
        520UL * 1024UL,
        8UL * 1024UL * 1024UL,
        20,
        PICO_RADIO_ONBOARD_CYW43,
        255U,
        18U, 19U, 16U, 17U,
        1U, 1U
    },
    {
        "picocalc_pico_w",
        "ClockworkPi PicoCalc (Pico W)",
        PICO_MCU_RP2040,
        2UL * 1024UL * 1024UL,
        264UL * 1024UL,
        8UL * 1024UL * 1024UL,
        20,
        PICO_RADIO_ONBOARD_CYW43,
        255U,
        18U, 19U, 16U, 17U,
        1U, 1U
    },
    {
        "picocalc_pico2",
        "ClockworkPi PicoCalc (Pico 2)",
        PICO_MCU_RP2350A,
        4UL * 1024UL * 1024UL,
        520UL * 1024UL,
        8UL * 1024UL * 1024UL,
        20,
        PICO_RADIO_NONE,
        25U,
        18U, 19U, 16U, 17U,
        1U, 1U
    },
    {
        "picocalc_pico",
        "ClockworkPi PicoCalc (Pico)",
        PICO_MCU_RP2040,
        2UL * 1024UL * 1024UL,
        264UL * 1024UL,
        8UL * 1024UL * 1024UL,
        20,
        PICO_RADIO_NONE,
        25U,
        18U, 19U, 16U, 17U,
        1U, 1U
    }
};

static const PicoUartMap s_uart_map[OYK_PICO_UART_COUNT] = {
    { 0, 0, 0U, 1U, 2U, 3U, "ser0" },
    { 1, 1, 4U, 5U, 6U, 7U, "ser1" }
};

static const PicoBoardProfile* s_active_profile = NULL;

const PicoBoardProfile* pico_board_profiles() {
    return s_profiles;
}

const PicoBoardProfile* pico_board_profile_by_id(const char* board_id) {
    if (!board_id || !board_id[0]) return NULL;
    for (int i = 0; i < PICO_TOTAL_BOARD_PROFILES; ++i) {
        if (strcmp(s_profiles[i].board_id, board_id) == 0) {
            return &s_profiles[i];
        }
    }
    return NULL;
}

const PicoBoardProfile* pico_current_board_profile() {
    if (s_active_profile) return s_active_profile;
    const PicoBoardProfile* p = pico_board_profile_by_id(OYK_PICO_BOARD_ID);
    return p ? p : &s_profiles[1];
}

void pico_board_set_sim_profile(const char* board_id) {
    const PicoBoardProfile* p = pico_board_profile_by_id(board_id);
    if (p) s_active_profile = p;
}

const PicoUartMap* pico_uart_table() {
    return s_uart_map;
}

const PicoUartMap* pico_uart_info(int logical_port) {
    const PicoBoardProfile* prof = pico_current_board_profile();
    int limit = prof ? static_cast<int>(prof->uart_count) : OYK_PICO_UART_COUNT;
    if (limit > OYK_PICO_UART_COUNT) limit = OYK_PICO_UART_COUNT;
    if (logical_port < 0 || logical_port >= limit) return NULL;
    return &s_uart_map[logical_port];
}

void pico_decode_unique_id_mac(const oyk_u8 unique_id[8], oyk_u8 mac_out[6]) {
    if (!mac_out || !unique_id) return;
    /* Locally administered unicast IEEE 802 MAC derived from Pico 64-bit flash unique ID */
    mac_out[0] = static_cast<oyk_u8>((unique_id[0] & 0xFCU) | 0x02U);
    mac_out[1] = static_cast<oyk_u8>(unique_id[1] ^ unique_id[7]);
    mac_out[2] = unique_id[2];
    mac_out[3] = unique_id[3];
    mac_out[4] = unique_id[4];
    mac_out[5] = static_cast<oyk_u8>(unique_id[5] ^ unique_id[6]);
}

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE

#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "pico/bootrom.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

static oyk_u8 s_uart_inited[OYK_PICO_UART_COUNT] = {0, 0};

static uart_inst_t* pico_hw_uart(oyk_u8 idx) {
    return (idx == 0) ? uart0 : uart1;
}

void pico_board_init() {
    const PicoBoardProfile* prof = pico_current_board_profile();
    if (prof->status_led_pin < 48U) {
        gpio_init(prof->status_led_pin);
        gpio_set_dir(prof->status_led_pin, GPIO_OUT);
        gpio_put(prof->status_led_pin, 1);
    }
    pico_psram_init_and_test();
}

void pico_board_read_mac(oyk_u8 mac_out[6]) {
    pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid);
    pico_decode_unique_id_mac(uid.id, mac_out);
}

void pico_board_set_status_led(int on) {
    const PicoBoardProfile* prof = pico_current_board_profile();
    if (prof->status_led_pin < 48U) {
        gpio_put(prof->status_led_pin, on ? 1 : 0);
    } else {
        pico_radio_set_wl_led(on);
    }
}

void pico_board_set_link_led(int up) {
    pico_board_set_status_led(up);
}

const PicoPsramInfo* pico_board_psram_info() {
    return pico_psram_info();
}

oyk_u32 pico_board_millis() {
    return static_cast<oyk_u32>(to_ms_since_boot(get_absolute_time()));
}

void pico_board_enter_bootloader() {
    stdio_flush();
    sleep_ms(200);
    reset_usb_boot(0, 0);
}

int pico_board_sim_bootloader_count() {
    return 0;
}

int pico_uart_init(int logical_port, const PicoUartConfig* cfg) {
    const PicoUartMap* m = pico_uart_info(logical_port);
    const PicoBoardProfile* prof = pico_current_board_profile();
    if (!m || !cfg || cfg->baudrate == 0) return 0;
    uart_inst_t* u = pico_hw_uart(m->hw_uart_index);
    uart_init(u, static_cast<uint>(cfg->baudrate));
    gpio_set_function(m->pin_tx, GPIO_FUNC_UART);
    gpio_set_function(m->pin_rx, GPIO_FUNC_UART);
    if (cfg->cts_rts && (!prof || !prof->has_picocalc_hw)) {
        gpio_set_function(m->pin_cts, GPIO_FUNC_UART);
        gpio_set_function(m->pin_rts, GPIO_FUNC_UART);
        uart_set_hw_flow(u, true, true);
    } else {
        uart_set_hw_flow(u, false, false);
    }
    if (m->hw_uart_index < OYK_PICO_UART_COUNT) {
        s_uart_inited[m->hw_uart_index] = 1U;
    }
    return 1;
}

size_t pico_uart_write(int logical_port, const oyk_u8* data, size_t len) {
    const PicoUartMap* m = pico_uart_info(logical_port);
    if (!m || !data || len == 0) return 0;
    if (m->hw_uart_index >= OYK_PICO_UART_COUNT || !s_uart_inited[m->hw_uart_index]) return 0;
    uart_inst_t* u = pico_hw_uart(m->hw_uart_index);
    for (size_t i = 0; i < len; ++i) {
        absolute_time_t dl = make_timeout_time_us(5000);
        while (!uart_is_writable(u)) {
            if (time_reached(dl)) return i;
            tight_loop_contents();
        }
        uart_putc_raw(u, data[i]);
    }
    return len;
}

size_t pico_uart_read(int logical_port, oyk_u8* out, size_t max_len) {
    const PicoUartMap* m = pico_uart_info(logical_port);
    if (!m || !out || max_len == 0) return 0;
    if (m->hw_uart_index >= OYK_PICO_UART_COUNT || !s_uart_inited[m->hw_uart_index]) return 0;
    uart_inst_t* u = pico_hw_uart(m->hw_uart_index);
    size_t got = 0;
    while (got < max_len && uart_is_readable(u)) {
        out[got++] = uart_getc(u);
    }
    return got;
}

void pico_uart_set_sim_tx_hook(PicoUartSimTxFn /*cb*/, void* /*ctx*/) {}

#else

/* Host simulation stubs */
static int s_host_status_led = 0;
static int s_host_link_led = 0;
static int s_host_bootloader_calls = 0;
static PicoUartSimTxFn s_host_tx_cb = NULL;
static void* s_host_tx_ctx = NULL;
static oyk_u8 s_host_rx[OYK_PICO_UART_COUNT][512];
static size_t s_host_rx_len[OYK_PICO_UART_COUNT] = {0, 0};

void pico_board_init() {
    s_host_status_led = 1;
    s_host_link_led = 0;
    pico_psram_init_and_test();
}

void pico_board_read_mac(oyk_u8 mac_out[6]) {
    static const oyk_u8 kUid[8] = {0xE6, 0x61, 0x41, 0x03, 0x2B, 0x55, 0x19, 0x07};
    pico_decode_unique_id_mac(kUid, mac_out);
}

void pico_board_set_status_led(int on) {
    s_host_status_led = on ? 1 : 0;
}

void pico_board_set_link_led(int up) {
    s_host_link_led = up ? 1 : 0;
}

const PicoPsramInfo* pico_board_psram_info() {
    return pico_psram_info();
}

oyk_u32 pico_board_millis() {
    return 1000UL;
}

void pico_board_enter_bootloader() {
    ++s_host_bootloader_calls;
}

int pico_board_sim_bootloader_count() {
    return s_host_bootloader_calls;
}

int pico_uart_init(int logical_port, const PicoUartConfig* cfg) {
    const PicoUartMap* m = pico_uart_info(logical_port);
    if (!m || !cfg || cfg->baudrate == 0) return 0;
    s_host_rx_len[logical_port] = 0;
    return 1;
}

size_t pico_uart_write(int logical_port, const oyk_u8* data, size_t len) {
    const PicoUartMap* m = pico_uart_info(logical_port);
    if (!m || !data || len == 0) return 0;
    if (s_host_tx_cb) {
        s_host_tx_cb(logical_port, data, len, s_host_tx_ctx);
    }
    return len;
}

size_t pico_uart_read(int logical_port, oyk_u8* out, size_t max_len) {
    const PicoUartMap* m = pico_uart_info(logical_port);
    if (!m || !out || max_len == 0) return 0;
    size_t n = s_host_rx_len[logical_port];
    if (n > max_len) n = max_len;
    if (n > 0) {
        memcpy(out, s_host_rx[logical_port], n);
        size_t rem = s_host_rx_len[logical_port] - n;
        if (rem > 0) memmove(s_host_rx[logical_port], s_host_rx[logical_port] + n, rem);
        s_host_rx_len[logical_port] = rem;
    }
    return n;
}

void pico_uart_set_sim_tx_hook(PicoUartSimTxFn cb, void* ctx) {
    s_host_tx_cb = cb;
    s_host_tx_ctx = ctx;
}

#endif
