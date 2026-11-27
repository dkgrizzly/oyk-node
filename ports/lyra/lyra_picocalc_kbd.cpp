/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "lyra_picocalc_kbd.hpp"

#include <string.h>

#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
#include <rtthread.h>
#include <rtdevice.h>

static int lyra_stm32_write_reg(void* bus_ptr, oyk_u8 reg, oyk_u8 val) {
    struct rt_i2c_bus_device* bus = reinterpret_cast<struct rt_i2c_bus_device*>(bus_ptr);
    if (!bus) return 0;
    oyk_u8 buf[2];
    buf[0] = static_cast<oyk_u8>(reg | LYRA_PICOCALC_REG_WRITE);
    buf[1] = val;
    struct rt_i2c_msg msg;
    msg.addr = OYK_LYRA_KBD_I2C_ADDR;
    msg.flags = RT_I2C_WR;
    msg.len = 2U;
    msg.buf = buf;
    return (rt_i2c_transfer(bus, &msg, 1) == 1) ? 1 : 0;
}

static int lyra_stm32_read_reg2(void* bus_ptr, oyk_u8 reg, oyk_u8 out[2]) {
    struct rt_i2c_bus_device* bus = reinterpret_cast<struct rt_i2c_bus_device*>(bus_ptr);
    if (!bus) return 0;
    struct rt_i2c_msg msgs[2];
    msgs[0].addr = OYK_LYRA_KBD_I2C_ADDR;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].len = 1U;
    msgs[0].buf = &reg;
    msgs[1].addr = OYK_LYRA_KBD_I2C_ADDR;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].len = 2U;
    msgs[1].buf = out;
    return (rt_i2c_transfer(bus, msgs, 2) == 2) ? 1 : 0;
}
#endif

LyraPicoCalcKbd::LyraPicoCalcKbd()
    : mod_shift_(0), mod_ctrl_(0), mod_alt_(0),
      battery_pct_(100), lcd_bkl_(180U), kbd_bkl_(64U),
      poweroff_delay_(-1), last_bat_ms_(0UL)
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
      , i2c_bus_(NULL)
#else
      , sim_head_(0U), sim_tail_(0U)
#endif
{}

int LyraPicoCalcKbd::init() {
    mod_shift_ = 0;
    mod_ctrl_ = 0;
    mod_alt_ = 0;
    poweroff_delay_ = -1;
    last_bat_ms_ = 0UL;
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
    i2c_bus_ = rt_device_find(OYK_LYRA_KBD_I2C_DEV);
    if (!i2c_bus_) return 0;
    lyra_stm32_write_reg(i2c_bus_, LYRA_PICOCALC_REG_ID_BKL, lcd_bkl_);
    lyra_stm32_write_reg(i2c_bus_, LYRA_PICOCALC_REG_ID_BK2, kbd_bkl_);
    oyk_u8 bat[2] = {0U, 100U};
    if (lyra_stm32_read_reg2(i2c_bus_, LYRA_PICOCALC_REG_ID_BAT, bat)) {
        battery_pct_ = static_cast<int>(bat[1] & 0x7FU);
        if (battery_pct_ > 100) battery_pct_ = 100;
    }
    return 1;
#else
    sim_head_ = 0U;
    sim_tail_ = 0U;
    battery_pct_ = 88;
    return 1;
#endif
}

void LyraPicoCalcKbd::set_lcd_backlight(oyk_u8 level) {
    lcd_bkl_ = level;
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
    lyra_stm32_write_reg(i2c_bus_, LYRA_PICOCALC_REG_ID_BKL, lcd_bkl_);
#endif
}

void LyraPicoCalcKbd::set_kbd_backlight(oyk_u8 level) {
    kbd_bkl_ = level;
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
    lyra_stm32_write_reg(i2c_bus_, LYRA_PICOCALC_REG_ID_BK2, kbd_bkl_);
#endif
}

int LyraPicoCalcKbd::request_poweroff(oyk_u8 delay_sec) {
    oyk_u8 clamped = (delay_sec > 63U) ? 63U : delay_sec;
    poweroff_delay_ = static_cast<int>(clamped);
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
    return lyra_stm32_write_reg(i2c_bus_, LYRA_PICOCALC_REG_ID_OFF, clamped);
#else
    return 1;
#endif
}

void LyraPicoCalcKbd::sim_push_event(oyk_u8 state, oyk_u8 keycode) {
#if !defined(OYK_LYRA_RTTHREAD) || !OYK_LYRA_RTTHREAD
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

void LyraPicoCalcKbd::sim_set_battery(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    battery_pct_ = pct;
}

int LyraPicoCalcKbd::handle_fifo_event(oyk_u8 state, oyk_u8 code, oyk_u8* out_vt, size_t max_len, int* out_action) {
    if (code == LYRA_PICOCALC_MOD_LSHIFT || code == LYRA_PICOCALC_MOD_RSHIFT) {
        mod_shift_ = (state != LYRA_PICOCALC_KEY_RELEASED) ? 1 : 0;
        return 0;
    }
    if (code == LYRA_PICOCALC_MOD_CTRL) {
        mod_ctrl_ = (state != LYRA_PICOCALC_KEY_RELEASED) ? 1 : 0;
        return 0;
    }
    if (code == LYRA_PICOCALC_MOD_ALT) {
        mod_alt_ = (state != LYRA_PICOCALC_KEY_RELEASED) ? 1 : 0;
        return 0;
    }
    if (state != LYRA_PICOCALC_KEY_PRESSED && state != LYRA_PICOCALC_KEY_HELD) {
        return 0;
    }

    if (mod_alt_ && code == LYRA_PICOCALC_KEY_UP) {
        int nl = static_cast<int>(lcd_bkl_) + 25;
        set_lcd_backlight(static_cast<oyk_u8>(nl > 255 ? 255 : nl));
        return 0;
    }
    if (mod_alt_ && code == LYRA_PICOCALC_KEY_DOWN) {
        int nl = static_cast<int>(lcd_bkl_) - 25;
        set_lcd_backlight(static_cast<oyk_u8>(nl < 25 ? 25 : nl));
        return 0;
    }
    if (mod_alt_ && code == LYRA_PICOCALC_KEY_RIGHT) {
        if (mod_ctrl_) {
            if (out_action) *out_action = LYRA_PICOCALC_ACT_PAN_RIGHT;
        } else {
            int nl = static_cast<int>(kbd_bkl_) + 32;
            set_kbd_backlight(static_cast<oyk_u8>(nl > 255 ? 255 : nl));
        }
        return 0;
    }
    if (mod_alt_ && code == LYRA_PICOCALC_KEY_LEFT) {
        if (mod_ctrl_) {
            if (out_action) *out_action = LYRA_PICOCALC_ACT_PAN_LEFT;
        } else {
            int nl = static_cast<int>(kbd_bkl_) - 32;
            set_kbd_backlight(static_cast<oyk_u8>(nl < 0 ? 0 : nl));
        }
        return 0;
    }
    if (mod_alt_ && mod_ctrl_ && (code == 'f' || code == 'F')) {
        if (out_action) *out_action = LYRA_PICOCALC_ACT_TOGGLE_FONT;
        return 0;
    }
    if (mod_alt_ && (code == 'h' || code == 'H')) {
        if (out_action) *out_action = LYRA_PICOCALC_ACT_PAN_LEFT;
        return 0;
    }
    if (mod_alt_ && (code == 'l' || code == 'L')) {
        if (out_action) *out_action = LYRA_PICOCALC_ACT_PAN_RIGHT;
        return 0;
    }

    const char* seq = NULL;
    char single = 0;
    switch (code) {
        case LYRA_PICOCALC_KEY_UP:    seq = "\033[A"; break;
        case LYRA_PICOCALC_KEY_DOWN:  seq = "\033[B"; break;
        case LYRA_PICOCALC_KEY_RIGHT: seq = "\033[C"; break;
        case LYRA_PICOCALC_KEY_LEFT:  seq = "\033[D"; break;
        case LYRA_PICOCALC_KEY_HOME:  seq = "\033[H"; break;
        case LYRA_PICOCALC_KEY_END:   seq = "\033[F"; break;
        case LYRA_PICOCALC_KEY_DEL:   seq = "\033[3~"; break;
        case LYRA_PICOCALC_KEY_PGUP:  seq = "\033[5~"; break;
        case LYRA_PICOCALC_KEY_PGDN:  seq = "\033[6~"; break;
        case LYRA_PICOCALC_KEY_ESC:   single = '\x1B'; break;
        case LYRA_PICOCALC_KEY_F1:    seq = "\033OP"; break;
        case LYRA_PICOCALC_KEY_F2:    seq = "\033[12~"; break;
        case LYRA_PICOCALC_KEY_F9:    single = '\t'; break;
        case LYRA_PICOCALC_KEY_F10:   seq = "\033[21~"; break;
        case '\n': case '\r':         single = '\r'; break;
        case '\t':                    seq = mod_shift_ ? "\033[Z" : "\t"; break;
        case 0x08U: case 0x7FU:       single = 0x7F; break;
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

size_t LyraPicoCalcKbd::poll_vt100(oyk_u32 now_ms, oyk_u8* out_vt, size_t max_len, int* out_action) {
    if (out_action) *out_action = LYRA_PICOCALC_ACT_NONE;
    if (!out_vt || max_len == 0) return 0;

    size_t produced = 0;
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
    for (int i = 0; i < 16 && produced + 8 <= max_len; ++i) {
        oyk_u8 ev[2] = {0U, 0U};
        if (!lyra_stm32_read_reg2(i2c_bus_, LYRA_PICOCALC_REG_ID_FIF, ev) || (ev[0] == 0U && ev[1] == 0U)) {
            break;
        }
        int act = LYRA_PICOCALC_ACT_NONE;
        int n = handle_fifo_event(ev[0], ev[1], out_vt + produced, max_len - produced, &act);
        if (n > 0) produced += static_cast<size_t>(n);
        if (act != LYRA_PICOCALC_ACT_NONE && out_action) {
            *out_action = act;
            break;
        }
    }
    if ((now_ms - last_bat_ms_) >= 5000UL || last_bat_ms_ == 0UL) {
        last_bat_ms_ = now_ms;
        oyk_u8 bat[2] = {0U, 0U};
        if (lyra_stm32_read_reg2(i2c_bus_, LYRA_PICOCALC_REG_ID_BAT, bat)) {
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
        int act = LYRA_PICOCALC_ACT_NONE;
        int n = handle_fifo_event(st, cd, out_vt + produced, max_len - produced, &act);
        if (n > 0) produced += static_cast<size_t>(n);
        if (act != LYRA_PICOCALC_ACT_NONE && out_action) {
            *out_action = act;
            break;
        }
    }
#endif
    return produced;
}
