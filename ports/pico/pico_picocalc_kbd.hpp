/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_PICO_PICOCALC_KBD_HPP
#define OYK_PICO_PICOCALC_KBD_HPP

#include "oyk_pico_config.h"
#include "oyk_portable.hpp"

/* ClockworkPi PicoCalc STM32F103 I2C register & keycode constants */
#define PICOCALC_REG_ID_TYPE  0x00U
#define PICOCALC_REG_ID_BKL   0x05U
#define PICOCALC_REG_ID_FIF   0x09U
#define PICOCALC_REG_ID_BK2   0x0AU
#define PICOCALC_REG_ID_BAT   0x0BU
#define PICOCALC_REG_WRITE    0x80U

#define PICOCALC_KEY_PRESSED  0x01U
#define PICOCALC_KEY_HELD     0x02U
#define PICOCALC_KEY_RELEASED 0x03U

#define PICOCALC_MOD_ALT      0xA1U
#define PICOCALC_MOD_LSHIFT   0xA2U
#define PICOCALC_MOD_RSHIFT   0xA3U
#define PICOCALC_MOD_CTRL     0xA5U

#define PICOCALC_KEY_F1       0x81U
#define PICOCALC_KEY_F2       0x82U
#define PICOCALC_KEY_F3       0x83U
#define PICOCALC_KEY_F4       0x84U
#define PICOCALC_KEY_F5       0x85U
#define PICOCALC_KEY_F6       0x86U
#define PICOCALC_KEY_F7       0x87U
#define PICOCALC_KEY_F8       0x88U
#define PICOCALC_KEY_F9       0x89U
#define PICOCALC_KEY_F10      0x8AU
#define PICOCALC_KEY_ESC      0xB1U
#define PICOCALC_KEY_LEFT     0xB4U
#define PICOCALC_KEY_UP       0xB5U
#define PICOCALC_KEY_DOWN     0xB6U
#define PICOCALC_KEY_RIGHT    0xB7U
#define PICOCALC_KEY_HOME     0xD2U
#define PICOCALC_KEY_DEL      0xD4U
#define PICOCALC_KEY_END      0xD5U
#define PICOCALC_KEY_PGUP     0xD6U
#define PICOCALC_KEY_PGDN     0xD7U

enum PicoCalcHotkeyAction {
    PICOCALC_ACT_NONE = 0,
    PICOCALC_ACT_TOGGLE_FONT,
    PICOCALC_ACT_PAN_LEFT,
    PICOCALC_ACT_PAN_RIGHT
};

class PicoCalcKbd {
public:
    PicoCalcKbd();

    int init();
    size_t poll_vt100(oyk_u32 now_ms, oyk_u8* out_vt, size_t max_len, int* out_action);

    int stm32_ready() const { return stm32_ready_; }
    int battery_percent() const { return battery_pct_; }
    oyk_u8 lcd_backlight() const { return lcd_bkl_; }
    oyk_u8 kbd_backlight() const { return kbd_bkl_; }
    void set_lcd_backlight(oyk_u8 level);
    void set_kbd_backlight(oyk_u8 level);

    /* Deterministic simulation injection for host tests */
    void sim_push_event(oyk_u8 state, oyk_u8 keycode);
    void sim_set_battery(int pct);

private:
    int handle_fifo_event(oyk_u8 state, oyk_u8 code, oyk_u8* out_vt, size_t max_len, int* out_action);

    int mod_shift_;
    int mod_ctrl_;
    int mod_alt_;
    int battery_pct_;
    oyk_u8 lcd_bkl_;
    oyk_u8 kbd_bkl_;
    int stm32_ready_;
    oyk_u32 last_bat_ms_;
    oyk_u32 last_stm32_retry_ms_;

#if !defined(PICO_ON_DEVICE) || !PICO_ON_DEVICE
    oyk_u8 sim_state_[32];
    oyk_u8 sim_code_[32];
    oyk_u8 sim_head_;
    oyk_u8 sim_tail_;
#endif
};

#endif /* OYK_PICO_PICOCALC_KBD_HPP */
