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

#if defined(OYK_TARGET_IMXRT1060)

/* Minimal bare-metal IMXRT1062 LPUART register offsets */
#define LPUART_BAUD_OFF  0x10UL
#define LPUART_STAT_OFF  0x14UL
#define LPUART_CTRL_OFF  0x18UL
#define LPUART_DATA_OFF  0x1CUL
#define LPUART_MODIR_OFF 0x28UL
#define LPUART_FIFO_OFF  0x2CUL

#define LPUART_STAT_TDRE (1UL << 23)
#define LPUART_STAT_RDRF (1UL << 21)
#define LPUART_CTRL_TE   (1UL << 19)
#define LPUART_CTRL_RE   (1UL << 18)
#define LPUART_MODIR_TXCTSE (1UL << 0)
#define LPUART_MODIR_RXRTSE (1UL << 3)
#define LPUART_FIFO_RXFE (1UL << 3)
#define LPUART_FIFO_TXFE (1UL << 7)

static volatile oyk_u32 s_systick_ms = 0;

extern "C" void SysTick_Handler(void) {
    s_systick_ms++;
}

static volatile oyk_u32* reg32(oyk_u32 addr) {
    return reinterpret_cast<volatile oyk_u32*>(addr);
}

void tinytmux_board_init() {
    /*
     * Clock & IOMUXC initialization is handled by board startup / Teensy core
     * or MCUXpresso SDK clocks; enable LPUART RX/TX FIFOs on configure.
     */
    s_systick_ms = 0;
}

void tinytmux_board_read_mac(oyk_u8 mac_out[6]) {
    oyk_u32 mac0 = *reg32(IMXRT_OCOTP_MAC0);
    oyk_u32 mac1 = *reg32(IMXRT_OCOTP_MAC1);
    tinytmux_decode_ocotp_mac(mac0, mac1, mac_out);
}

void tinytmux_board_set_link_led(int speed_100mbps) {
    /* Teensy 4.1 Pin 18 -> GPIO_AD_B1_01 -> GPIO1_IO17 (DR_SET / DR_CLEAR) */
    const oyk_u32 GPIO1_DR_SET   = 0x401B8084UL;
    const oyk_u32 GPIO1_DR_CLEAR = 0x401B8088UL;
    const oyk_u32 mask = (1UL << 17);
    if (speed_100mbps) {
        *reg32(GPIO1_DR_SET) = mask;
    } else {
        *reg32(GPIO1_DR_CLEAR) = mask;
    }
}

oyk_u32 tinytmux_board_millis() {
    return s_systick_ms;
}

int tinytmux_uart_init(int logical_port, const TinyTmuxUartConfig* cfg) {
    const TinyTmuxUartMap* m = tinytmux_uart_info(logical_port);
    if (!m || !cfg || cfg->baudrate == 0) return 0;

    oyk_u32 base = m->reg_base;
    /* Disable TX/RX while configuring */
    *reg32(base + LPUART_CTRL_OFF) &= ~(LPUART_CTRL_TE | LPUART_CTRL_RE);

    /* Default IMXRT1060 UART_CLK_ROOT = 24 MHz (OSC) with OSR = 16 (OSR field = 15) */
    oyk_u32 sbr = 24000000UL / (16UL * cfg->baudrate);
    if (sbr == 0) sbr = 1;
    if (sbr > 0x1FFFUL) sbr = 0x1FFFUL;
    *reg32(base + LPUART_BAUD_OFF) = (15UL << 24) | (sbr & 0x1FFFUL);

    /* Enable RX/TX hardware FIFOs */
    *reg32(base + LPUART_FIFO_OFF) |= (LPUART_FIFO_RXFE | LPUART_FIFO_TXFE);

    /* Hardware CTS/RTS flow control */
    if (cfg->cts_rts) {
        *reg32(base + LPUART_MODIR_OFF) |= (LPUART_MODIR_TXCTSE | LPUART_MODIR_RXRTSE);
    } else {
        *reg32(base + LPUART_MODIR_OFF) &= ~(LPUART_MODIR_TXCTSE | LPUART_MODIR_RXRTSE);
    }

    /* Enable transmitter and receiver */
    *reg32(base + LPUART_CTRL_OFF) |= (LPUART_CTRL_TE | LPUART_CTRL_RE);
    return 1;
}

size_t tinytmux_uart_write(int logical_port, const oyk_u8* data, size_t len) {
    const TinyTmuxUartMap* m = tinytmux_uart_info(logical_port);
    if (!m || !data) return 0;
    oyk_u32 base = m->reg_base;
    for (size_t i = 0; i < len; ++i) {
        while (((*reg32(base + LPUART_STAT_OFF)) & LPUART_STAT_TDRE) == 0) {
        }
        *reg32(base + LPUART_DATA_OFF) = static_cast<oyk_u32>(data[i]);
    }
    return len;
}

size_t tinytmux_uart_read(int logical_port, oyk_u8* out, size_t max_len) {
    const TinyTmuxUartMap* m = tinytmux_uart_info(logical_port);
    if (!m || !out) return 0;
    oyk_u32 base = m->reg_base;
    size_t got = 0;
    while (got < max_len && (((*reg32(base + LPUART_STAT_OFF)) & LPUART_STAT_RDRF) != 0)) {
        out[got++] = static_cast<oyk_u8>((*reg32(base + LPUART_DATA_OFF)) & 0xFFU);
    }
    return got;
}

#else

/* Host stub implementation for Linux unit tests & simulation */
static int s_host_led = 0;
static oyk_u32 s_host_ms = 0;

void tinytmux_board_init() {
    s_host_led = 0;
    s_host_ms = 0;
}

void tinytmux_board_read_mac(oyk_u8 mac_out[6]) {
    tinytmux_decode_ocotp_mac(0x004F594BUL, 0x000004E9UL, mac_out);
}

void tinytmux_board_set_link_led(int speed_100mbps) {
    s_host_led = speed_100mbps ? 1 : 0;
}

oyk_u32 tinytmux_board_millis() {
    return s_host_ms;
}

int tinytmux_uart_init(int logical_port, const TinyTmuxUartConfig* cfg) {
    return (tinytmux_uart_info(logical_port) != NULL && cfg != NULL && cfg->baudrate > 0) ? 1 : 0;
}

size_t tinytmux_uart_write(int /*logical_port*/, const oyk_u8* /*data*/, size_t len) {
    return len;
}

size_t tinytmux_uart_read(int /*logical_port*/, oyk_u8* /*out*/, size_t /*max_len*/) {
    return 0;
}

#endif
