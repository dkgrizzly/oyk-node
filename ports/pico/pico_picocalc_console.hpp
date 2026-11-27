/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_PICO_PICOCALC_CONSOLE_HPP
#define OYK_PICO_PICOCALC_CONSOLE_HPP

#include "oyk_pico_config.h"
#include "oyk_vt100.h"
#include "pico_console.hpp"
#include "pico_picocalc_lcd.hpp"
#include "pico_picocalc_kbd.hpp"
#include "pico_picocalc_audio.hpp"

class PicoCalcConsole {
public:
    PicoCalcConsole();

    void init(PicoConsole* console);
    void connect_and_probe();
    void poll_tick(oyk_u32 now_ms);
    void flush();

    void toggle_font();
    void pan_horizontal(int delta_cols);

    int zoomed_6x10() const { return zoomed_6x10_; }
    int pan_col() const { return pan_col_; }
    const OykVt100* vt() const { return &vt_; }
    OykVt100* vt() { return &vt_; }
    PicoCalcLcd* lcd() { return &lcd_; }
    const PicoCalcLcd* lcd() const { return &lcd_; }
    PicoCalcKbd* kbd() { return &kbd_; }
    const PicoCalcKbd* kbd() const { return &kbd_; }
    const PicoCalcAudio* audio() const { return &audio_; }

    static size_t tx_trampoline(const char* data, size_t len, void* ctx);

private:
    static void vt_reply_trampoline(void* ctx, const char* data, size_t len);
    static void vt_bel_trampoline(void* ctx);

    PicoConsole* console_;
    OykVt100 vt_;
    PicoCalcLcd lcd_;
    PicoCalcKbd kbd_;
    PicoCalcAudio audio_;
    int zoomed_6x10_;
    int pan_col_;
    int last_bat_pct_;
    oyk_u32 last_ms_;
};

#endif /* OYK_PICO_PICOCALC_CONSOLE_HPP */
