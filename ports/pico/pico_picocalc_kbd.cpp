/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "pico_picocalc_kbd.hpp"

#include <stdio.h>
#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

static int stm32_write_reg(oyk_u8 reg, oyk_u8 val) {
    oyk_u8 buf[2];
    buf[0] = static_cast<oyk_u8>(reg | PICOCALC_REG_WRITE);
    buf[1] = val;
    return (i2c_write_timeout_us(i2c1, OYK_PICOCALC_KBD_I2C_ADDR, buf, 2, false, 20000U) == 2) ? 1 : 0;
}

static int stm32_read_reg2(oyk_u8 reg, oyk_u8 out[2]) {
    if (i2c_write_timeout_us(i2c1, OYK_PICOCALC_KBD_I2C_ADDR, &reg, 1, false, 20000U) != 1) return 0;
    sleep_us(200);
    return (i2c_read_timeout_us(i2c1, OYK_PICOCALC_KBD_I2C_ADDR, out, 2, false, 20000U) == 2) ? 1 : 0;
}
#endif

PicoCalcKbd::PicoCalcKbd()
    : mod_shift_(0), mod_ctrl_(0), mod_alt_(0),
      battery_pct_(100), lcd_bkl_(180U), kbd_bkl_(64U),
      stm32_ready_(0), last_bat_ms_(0UL), last_stm32_retry_ms_(0UL)
#if !defined(PICO_ON_DEVICE) || !PICO_ON_DEVICE
      , sim_head_(0U), sim_tail_(0U)
#endif
{}

int PicoCalcKbd::init() {
    mod_shift_ = 0;
    mod_ctrl_ = 0;
    mod_alt_ = 0;
    stm32_ready_ = 0;
    last_bat_ms_ = 0UL;
    last_stm32_retry_ms_ = 0UL;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    i2c_init(i2c1, 10000U);
    gpio_set_function(OYK_PICOCALC_KBD_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(OYK_PICOCALC_KBD_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(OYK_PICOCALC_KBD_SDA_PIN);
    gpio_pull_up(OYK_PICOCALC_KBD_SCL_PIN);
    sleep_ms(10);
    if (stm32_write_reg(PICOCALC_REG_ID_BKL, lcd_bkl_)) stm32_ready_ = 1;
    sleep_ms(2);
    if (stm32_write_reg(PICOCALC_REG_ID_BK2, kbd_bkl_)) stm32_ready_ = 1;
    oyk_u8 bat[2] = {0U, 100U};
    if (stm32_read_reg2(PICOCALC_REG_ID_BAT, bat)) {
        stm32_ready_ = 1;
        battery_pct_ = static_cast<int>(bat[1] & 0x7FU);
        if (battery_pct_ > 100) battery_pct_ = 100;
    }
#else
    stm32_ready_ = 1;
    sim_head_ = 0U;
    sim_tail_ = 0U;
    battery_pct_ = 88;
#endif
    return 1;
}

void PicoCalcKbd::set_lcd_backlight(oyk_u8 level) {
    lcd_bkl_ = level;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    stm32_write_reg(PICOCALC_REG_ID_BKL, lcd_bkl_);
#endif
}

void PicoCalcKbd::set_kbd_backlight(oyk_u8 level) {
    kbd_bkl_ = level;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    stm32_write_reg(PICOCALC_REG_ID_BK2, kbd_bkl_);
#endif
}

void PicoCalcKbd::sim_push_event(oyk_u8 state, oyk_u8 keycode) {
#if !defined(PICO_ON_DEVICE) || !PICO_ON_DEVICE
    oyk_u8 next = static_cast<oyk_u8>((sim_tail_ + 1U) & 31U);
    if (next == sim_head_) return;
    sim_state_[sim_tail_] = state;
    sim_code_[sim_tail_] = keycode;
    sim_tail_ = next;
#else
    (void)state;
    (void)keycode;
#endif
}

void PicoCalcKbd::sim_set_battery(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    battery_pct_ = pct;
}

int PicoCalcKbd::handle_fifo_event(oyk_u8 state, oyk_u8 code, oyk_u8* out_vt, size_t max_len, int* out_action) {
    if (code == PICOCALC_MOD_LSHIFT || code == PICOCALC_MOD_RSHIFT) {
        mod_shift_ = (state != PICOCALC_KEY_RELEASED) ? 1 : 0;
        return 0;
    }
    if (code == PICOCALC_MOD_CTRL) {
        mod_ctrl_ = (state != PICOCALC_KEY_RELEASED) ? 1 : 0;
        return 0;
    }
    if (code == PICOCALC_MOD_ALT) {
        mod_alt_ = (state != PICOCALC_KEY_RELEASED) ? 1 : 0;
        return 0;
    }
    if (state != PICOCALC_KEY_PRESSED && state != PICOCALC_KEY_HELD) {
        return 0;
    }

    if (mod_alt_ && code == PICOCALC_KEY_UP) {
        int nl = static_cast<int>(lcd_bkl_) + 25;
        set_lcd_backlight(static_cast<oyk_u8>(nl > 255 ? 255 : nl));
        return 0;
    }
    if (mod_alt_ && code == PICOCALC_KEY_DOWN) {
        int nl = static_cast<int>(lcd_bkl_) - 25;
        set_lcd_backlight(static_cast<oyk_u8>(nl < 25 ? 25 : nl));
        return 0;
    }
    if (mod_alt_ && code == PICOCALC_KEY_RIGHT) {
        int nl = static_cast<int>(kbd_bkl_) + 32;
        set_kbd_backlight(static_cast<oyk_u8>(nl > 255 ? 255 : nl));
        return 0;
    }
    if (mod_alt_ && code == PICOCALC_KEY_LEFT) {
        int nl = static_cast<int>(kbd_bkl_) - 32;
        set_kbd_backlight(static_cast<oyk_u8>(nl < 0 ? 0 : nl));
        return 0;
    }
    if (mod_alt_ && mod_ctrl_ && (code == 'f' || code == 'F')) {
        if (out_action) *out_action = PICOCALC_ACT_TOGGLE_FONT;
        return 0;
    }
    if (mod_alt_ && (code == 'h' || code == 'H')) {
        if (out_action) *out_action = PICOCALC_ACT_PAN_LEFT;
        return 0;
    }
    if (mod_alt_ && (code == 'l' || code == 'L')) {
        if (out_action) *out_action = PICOCALC_ACT_PAN_RIGHT;
        return 0;
    }

    const char* seq = NULL;
    char single = 0;
    switch (code) {
        case PICOCALC_KEY_UP:    seq = "\033[A"; break;
        case PICOCALC_KEY_DOWN:  seq = "\033[B"; break;
        case PICOCALC_KEY_RIGHT: seq = "\033[C"; break;
        case PICOCALC_KEY_LEFT:  seq = "\033[D"; break;
        case PICOCALC_KEY_HOME:  seq = "\033[H"; break;
        case PICOCALC_KEY_END:   seq = "\033[F"; break;
        case PICOCALC_KEY_DEL:   seq = "\033[3~"; break;
        case PICOCALC_KEY_PGUP:  seq = "\033[5~"; break;
        case PICOCALC_KEY_PGDN:  seq = "\033[6~"; break;
        case PICOCALC_KEY_ESC:   single = '\x1B'; break;
        case PICOCALC_KEY_F1:    seq = "\033OP"; break;
        case PICOCALC_KEY_F2:    seq = "\033[12~"; break;
        case PICOCALC_KEY_F9:    single = '\t'; break;
        case PICOCALC_KEY_F10:   seq = "\033[21~"; break;
        case '\n': case '\r':    single = '\r'; break;
        case '\t':               seq = mod_shift_ ? "\033[Z" : "\t"; break;
        case 0x08U: case 0x7FU:  single = 0x7F; break;
        default:
            if (code >= 32U && code < 127U) {
                char ch = static_cast<char>(code);
                if (mod_ctrl_) {
                    if (ch >= 'a' && ch <= 'z') single = static_cast<char>(ch - 'a' + 1);
                    else if (ch >= 'A' && ch <= 'Z') single = static_cast<char>(ch - 'A' + 1);
                } else {
                    single = ch;
                }
            }
            break;
    }

    if (seq) {
        size_t n = strlen(seq);
        if (n <= max_len) {
            memcpy(out_vt, seq, n);
            return static_cast<int>(n);
        }
        return 0;
    }
    if (single != 0 && max_len >= 1U) {
        out_vt[0] = static_cast<oyk_u8>(single);
        return 1;
    }
    return 0;
}

size_t PicoCalcKbd::poll_vt100(oyk_u32 now_ms, oyk_u8* out_vt, size_t max_len, int* out_action) {
    if (out_action) *out_action = PICOCALC_ACT_NONE;
    if (!out_vt || max_len == 0) return 0;

    size_t produced = 0;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    if (!stm32_ready_) {
        if ((now_ms - last_stm32_retry_ms_) >= 1000UL || last_stm32_retry_ms_ == 0UL) {
            last_stm32_retry_ms_ = now_ms;
            if (stm32_write_reg(PICOCALC_REG_ID_BKL, lcd_bkl_)) stm32_ready_ = 1;
            if (stm32_write_reg(PICOCALC_REG_ID_BK2, kbd_bkl_)) stm32_ready_ = 1;
        }
        return 0;
    }
    for (int i = 0; i < 16 && produced + 8 <= max_len; ++i) {
        oyk_u8 ev[2] = {0U, 0U};
        if (!stm32_read_reg2(PICOCALC_REG_ID_FIF, ev) || (ev[0] == 0U && ev[1] == 0U)) {
            break;
        }
        int act = PICOCALC_ACT_NONE;
        int n = handle_fifo_event(ev[0], ev[1], out_vt + produced, max_len - produced, &act);
        if (n > 0) produced += static_cast<size_t>(n);
        if (act != PICOCALC_ACT_NONE && out_action) {
            *out_action = act;
            break;
        }
    }
    if ((now_ms - last_bat_ms_) >= 5000UL || last_bat_ms_ == 0UL) {
        last_bat_ms_ = now_ms;
        oyk_u8 bat[2] = {0U, 0U};
        if (stm32_read_reg2(PICOCALC_REG_ID_BAT, bat)) {
            battery_pct_ = static_cast<int>(bat[1] & 0x7FU);
            if (battery_pct_ > 100) battery_pct_ = 100;
        }
    }
#else
    (void)now_ms;
    while (sim_head_ != sim_tail_ && produced + 8 <= max_len) {
        oyk_u8 st = sim_state_[sim_head_];
        oyk_u8 cd = sim_code_[sim_head_];
        sim_head_ = static_cast<oyk_u8>((sim_head_ + 1U) & 31U);
        int act = PICOCALC_ACT_NONE;
        int n = handle_fifo_event(st, cd, out_vt + produced, max_len - produced, &act);
        if (n > 0) produced += static_cast<size_t>(n);
        if (act != PICOCALC_ACT_NONE && out_action) {
            *out_action = act;
            break;
        }
    }
#endif
    return produced;
}
