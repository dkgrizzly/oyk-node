/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "pico_picocalc_console.hpp"

#include <stdio.h>
#include <string.h>

PicoCalcConsole::PicoCalcConsole()
    : console_(NULL), zoomed_6x10_(0), pan_col_(0), last_bat_pct_(-1), last_ms_(0UL),
      reply_len_(0U), draining_replies_(0) {}

void PicoCalcConsole::init(PicoConsole* console) {
    console_ = console;
    zoomed_6x10_ = 0;
    pan_col_ = 0;
    last_ms_ = 0UL;
    reply_len_ = 0U;
    draining_replies_ = 0;

    lcd_.init();
    kbd_.init();
    audio_.init();
    last_bat_pct_ = kbd_.battery_percent();

    OykVtRenderer r;
    lcd_.fill_renderer(&r);
    OykVtCallbacks cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.ctx = this;
    cbs.on_reply = PicoCalcConsole::vt_reply_trampoline;
    cbs.on_bel = PicoCalcConsole::vt_bel_trampoline;

    oyk_vt100_init(&vt_, oyk_vt_font_4x8(), 80, 40, &r, &cbs);
    if (console_) {
        console_->set_battery_percent(last_bat_pct_);
    }
}

void PicoCalcConsole::connect_and_probe() {
    if (!console_) return;
    console_->on_usb_connect();
    drain_vt_replies();
    /* Explicitly push local terminal capability (VT340 or xterm-256) and 40x80 geometry */
#if OYK_VT_ENABLE_256COLOR
    static const oyk_u8 kInitReplies[] = "\033[?62;22c\033[>0;1;0c\033[40;80R";
#else
    static const oyk_u8 kInitReplies[] = "\033[?62;22c\033[40;80R";
#endif
    console_->feed_input(kInitReplies, sizeof(kInitReplies) - 1U);
    drain_vt_replies();
    oyk_vt100_flush(&vt_);
}

void PicoCalcConsole::vt_reply_trampoline(void* ctx, const char* data, size_t len) {
    PicoCalcConsole* self = static_cast<PicoCalcConsole*>(ctx);
    if (!self || !data || len == 0) return;
    size_t room = sizeof(self->reply_buf_) - self->reply_len_;
    size_t n = (len < room) ? len : room;
    if (n > 0U) {
        memcpy(self->reply_buf_ + self->reply_len_, data, n);
        self->reply_len_ += n;
    }
}

void PicoCalcConsole::drain_vt_replies() {
    if (draining_replies_ || !console_ || reply_len_ == 0U) return;
    draining_replies_ = 1;
    while (reply_len_ > 0U) {
        oyk_u8 tmp[64];
        size_t n = reply_len_;
        if (n > sizeof(tmp)) n = sizeof(tmp);
        memcpy(tmp, reply_buf_, n);
        reply_len_ = 0U;
        console_->feed_input(tmp, n);
    }
    draining_replies_ = 0;
}

void PicoCalcConsole::vt_bel_trampoline(void* ctx) {
    PicoCalcConsole* self = static_cast<PicoCalcConsole*>(ctx);
    if (self) {
        self->audio_.trigger_bel(self->last_ms_);
    }
}

size_t PicoCalcConsole::tx_trampoline(const char* data, size_t len, void* ctx) {
    PicoCalcConsole* self = static_cast<PicoCalcConsole*>(ctx);
    if (!self || !data || len == 0) return 0;
    oyk_vt100_write(&self->vt_, data, len);
    return len;
}

void PicoCalcConsole::flush() {
    oyk_vt100_flush(&vt_);
}

void PicoCalcConsole::toggle_font() {
    zoomed_6x10_ = !zoomed_6x10_;
    if (zoomed_6x10_) {
        pan_col_ = 0;
        oyk_vt100_set_font(&vt_, oyk_vt_font_6x10());
        oyk_vt100_resize(&vt_, 80, 32);
        oyk_vt100_set_viewport(&vt_, 53, 32, pan_col_);
        if (console_) {
            static const oyk_u8 kCpr32[] = "\033[32;80R";
            console_->feed_input(kCpr32, sizeof(kCpr32) - 1U);
        }
    } else {
        pan_col_ = 0;
        oyk_vt100_set_font(&vt_, oyk_vt_font_4x8());
        oyk_vt100_resize(&vt_, 80, 40);
        oyk_vt100_set_viewport(&vt_, 80, 40, 0);
        if (console_) {
            static const oyk_u8 kCpr40[] = "\033[40;80R";
            console_->feed_input(kCpr40, sizeof(kCpr40) - 1U);
        }
    }
    if (console_) console_->render();
    oyk_vt100_flush(&vt_);
}

void PicoCalcConsole::pan_horizontal(int delta_cols) {
    if (!zoomed_6x10_) return;
    int np = pan_col_ + delta_cols;
    if (np < 0) np = 0;
    if (np > 27) np = 27; /* 80 - 53 */
    if (np != pan_col_) {
        pan_col_ = np;
        oyk_vt100_set_viewport(&vt_, 53, 32, static_cast<int16_t>(pan_col_));
        oyk_vt100_flush(&vt_);
    }
}

void PicoCalcConsole::poll_tick(oyk_u32 now_ms) {
    last_ms_ = now_ms;
    audio_.tick(now_ms);

    oyk_u8 in_buf[64];
    int act = PICOCALC_ACT_NONE;
    size_t got = kbd_.poll_vt100(now_ms, in_buf, sizeof(in_buf), &act);
    if (act == PICOCALC_ACT_TOGGLE_FONT) {
        toggle_font();
    } else if (act == PICOCALC_ACT_PAN_LEFT) {
        pan_horizontal(-8);
    } else if (act == PICOCALC_ACT_PAN_RIGHT) {
        pan_horizontal(8);
    }
    if (got > 0 && console_) {
        console_->feed_input(in_buf, got);
    }
    drain_vt_replies();

    int bat = kbd_.battery_percent();
    if (bat != last_bat_pct_ && console_) {
        last_bat_pct_ = bat;
        console_->set_battery_percent(bat);
        if (console_->view_mode() == PICO_VIEW_CONSOLE) {
            console_->render();
        }
    }
    oyk_vt100_flush(&vt_);
}
