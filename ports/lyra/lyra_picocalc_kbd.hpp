/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_LYRA_PICOCALC_KBD_HPP
#define OYK_LYRA_PICOCALC_KBD_HPP

#include "oyk_lyra_config.h"
#include "oyk_portable.hpp"

/* ClockworkPi PicoCalc STM32F103 I2C2 (0x1F) registers & keycodes */
#define LYRA_PICOCALC_REG_ID_TYPE  0x00U
#define LYRA_PICOCALC_REG_ID_BKL   0x05U
#define LYRA_PICOCALC_REG_ID_FIF   0x09U
#define LYRA_PICOCALC_REG_ID_BK2   0x0AU
#define LYRA_PICOCALC_REG_ID_BAT   0x0BU
#define LYRA_PICOCALC_REG_ID_OFF   0x0EU
#define LYRA_PICOCALC_REG_WRITE    0x80U

#define LYRA_PICOCALC_KEY_PRESSED  0x01U
#define LYRA_PICOCALC_KEY_HELD     0x02U
#define LYRA_PICOCALC_KEY_RELEASED 0x03U

#define LYRA_PICOCALC_MOD_ALT      0xA1U
#define LYRA_PICOCALC_MOD_LSHIFT   0xA2U
#define LYRA_PICOCALC_MOD_RSHIFT   0xA3U
#define LYRA_PICOCALC_MOD_CTRL     0xA5U

#define LYRA_PICOCALC_KEY_F1       0x81U
#define LYRA_PICOCALC_KEY_F2       0x82U
#define LYRA_PICOCALC_KEY_F3       0x83U
#define LYRA_PICOCALC_KEY_F4       0x84U
#define LYRA_PICOCALC_KEY_F5       0x85U
#define LYRA_PICOCALC_KEY_F6       0x86U
#define LYRA_PICOCALC_KEY_F7       0x87U
#define LYRA_PICOCALC_KEY_F8       0x88U
#define LYRA_PICOCALC_KEY_F9       0x89U
#define LYRA_PICOCALC_KEY_F10      0x8AU
#define LYRA_PICOCALC_KEY_ESC      0xB1U
#define LYRA_PICOCALC_KEY_LEFT     0xB4U
#define LYRA_PICOCALC_KEY_UP       0xB5U
#define LYRA_PICOCALC_KEY_DOWN     0xB6U
#define LYRA_PICOCALC_KEY_RIGHT    0xB7U
#define LYRA_PICOCALC_KEY_HOME     0xD2U
#define LYRA_PICOCALC_KEY_DEL      0xD4U
#define LYRA_PICOCALC_KEY_END      0xD5U
#define LYRA_PICOCALC_KEY_PGUP     0xD6U
#define LYRA_PICOCALC_KEY_PGDN     0xD7U

enum LyraPicoCalcHotkeyAction {
    LYRA_PICOCALC_ACT_NONE = 0,
    LYRA_PICOCALC_ACT_TOGGLE_FONT,
    LYRA_PICOCALC_ACT_PAN_LEFT,
    LYRA_PICOCALC_ACT_PAN_RIGHT
};

class LyraPicoCalcKbd {
public:
    LyraPicoCalcKbd();

    int init();
    size_t poll_vt100(oyk_u32 now_ms, oyk_u8* out_vt, size_t max_len, int* out_action);

    int battery_percent() const { return battery_pct_; }
    oyk_u8 lcd_backlight() const { return lcd_bkl_; }
    oyk_u8 kbd_backlight() const { return kbd_bkl_; }
    void set_lcd_backlight(oyk_u8 level);
    void set_kbd_backlight(oyk_u8 level);
    int request_poweroff(oyk_u8 delay_sec);
    int poweroff_requested() const { return poweroff_delay_; }

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
    int poweroff_delay_;
    oyk_u32 last_bat_ms_;

#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
    void* i2c_bus_;
#else
    oyk_u8 sim_state_[32];
    oyk_u8 sim_code_[32];
    oyk_u8 sim_head_;
    oyk_u8 sim_tail_;
#endif
};

#endif /* OYK_LYRA_PICOCALC_KBD_HPP */
