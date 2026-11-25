/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_BOARD_TINYTMUX_HPP
#define OYK_BOARD_TINYTMUX_HPP

/*
 * Board Hardware Abstraction for Freescale/NXP i.MX RT1060 (Teensy 4.1)
 * matching the TinyTMUX 8-UART + Dual USB-CDC + 10/100 Ethernet board layout.
 */

#include "../compat/oyk_portable.hpp"

#define TINYTMUX_UART_COUNT          8
#define TINYTMUX_USB_CDC_COUNT       2
#define TINYTMUX_TOTAL_SERIAL_PORTS  10
#define TINYTMUX_LINK_LED_PIN        18U /* HIGH on 100 Mbps link, LOW on 10 Mbps / down */

/* IMXRT1062 Peripheral Base Addresses (NXP i.MX RT1060 Reference Manual) */
#define IMXRT_LPUART1_BASE  0x40184000UL
#define IMXRT_LPUART2_BASE  0x40188000UL
#define IMXRT_LPUART3_BASE  0x4018C000UL
#define IMXRT_LPUART4_BASE  0x40190000UL
#define IMXRT_LPUART5_BASE  0x40194000UL
#define IMXRT_LPUART6_BASE  0x40198000UL
#define IMXRT_LPUART7_BASE  0x4019C000UL
#define IMXRT_LPUART8_BASE  0x401A0000UL
#define IMXRT_ENET_BASE     0x402D8000UL
#define IMXRT_USB1_BASE     0x402E0000UL
#define IMXRT_OCOTP_MAC0    0x401F4620UL
#define IMXRT_OCOTP_MAC1    0x401F4630UL

/* Logical serial port roles on TinyTMUX */
enum TinyTmuxPortRole {
    TMUX_ROLE_UNUSED = 0,
    TMUX_ROLE_PPP_RADIO,    /* RFC 1661/1662/1332 PPP link (e.g. RadioFoundry firmware radio) */
    TMUX_ROLE_KISS_TNC,     /* KISS serial TNC */
    TMUX_ROLE_CONSOLE       /* Local VT100/ANSI operator console */
};

struct TinyTmuxUartMap {
    oyk_u8 logical_port;    /* 0..7 -> ser0..ser7 */
    oyk_u8 teensy_serial;   /* Arduino/Teensy SerialN number (1..8) */
    oyk_u8 lpuart_index;    /* IMXRT1060 LPUART1..LPUART8 */
    oyk_u32 reg_base;       /* IMXRT1060 peripheral base address */
    oyk_u8 pin_rx;
    oyk_u8 pin_tx;
    oyk_u8 pin_cts;         /* XBAR-mapped CTS pin */
    oyk_u8 pin_rts;
    const char* name;       /* "ser0".."ser7" */
};

struct TinyTmuxUartConfig {
    oyk_u32 baudrate;
    oyk_u8 cts_rts;         /* 1 = hardware CTS/RTS flow control */
    TinyTmuxPortRole role;
};

/* Returns the immutable TinyTMUX hardware UART pinout table (size TINYTMUX_UART_COUNT). */
const TinyTmuxUartMap* tinytmux_uart_table();
const TinyTmuxUartMap* tinytmux_uart_info(int logical_port);

/* Decodes a 6-byte Ethernet MAC address from NXP IMXRT1060 OCOTP MAC0/MAC1 fuse words. */
void tinytmux_decode_ocotp_mac(oyk_u32 ocotp_mac0, oyk_u32 ocotp_mac1, oyk_u8 mac_out[6]);

/* Board HAL interface (implemented for IMXRT1060 hardware; mockable on host tests). */
void tinytmux_board_init();
void tinytmux_board_read_mac(oyk_u8 mac_out[6]);
void tinytmux_board_set_link_led(int speed_100mbps);
oyk_u32 tinytmux_board_millis();

int tinytmux_uart_init(int logical_port, const TinyTmuxUartConfig* cfg);
size_t tinytmux_uart_write(int logical_port, const oyk_u8* data, size_t len);
size_t tinytmux_uart_read(int logical_port, oyk_u8* out, size_t max_len);

#endif /* OYK_BOARD_TINYTMUX_HPP */
