/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_PICO_PICOCALC_LCD_HPP
#define OYK_PICO_PICOCALC_LCD_HPP

#include "oyk_pico_config.h"
#include "oyk_portable.hpp"
#include "oyk_vt100.h"
#include "oyk_vt_fb.h"

class PicoCalcLcd {
public:
    PicoCalcLcd();

    int init();
    void fill_renderer(OykVtRenderer* out_renderer);
    void clear(oyk_u32 rgb24);

    oyk_u32 pixel_writes() const;
    oyk_u32 hw_scrolls() const;
    void reset_counters();
    oyk_u16 scroll_y() const { return scroll_y_; }

#if !defined(PICO_ON_DEVICE) || !PICO_ON_DEVICE
    oyk_u16 sample_pixel(oyk_u16 x, oyk_u16 y) const;
    int save_bmp(const char* path) const;
#endif

private:
    static void fill_rect_cb(void* ctx, uint16_t px, uint16_t py, uint16_t pw, uint16_t ph, uint32_t bg_rgb24);
    static void blit_glyph_cb(void* ctx, uint16_t px, uint16_t py, uint16_t glyph,
                              const uint8_t* rows, uint8_t w, uint8_t h,
                              uint32_t fg_rgb24, uint32_t bg_rgb24, uint8_t attr);
    static int  hw_scroll_cb(void* ctx, int16_t delta_rows, uint16_t cell_h);

    oyk_u16 scroll_y_;
    oyk_u32 pixel_writes_;
    oyk_u32 hw_scrolls_;
#if !defined(PICO_ON_DEVICE) || !PICO_ON_DEVICE
    OykVtFb fb_;
    oyk_u16 pixels_[OYK_PICOCALC_LCD_WIDTH * OYK_PICOCALC_LCD_HEIGHT];
#endif
};

#endif /* OYK_PICO_PICOCALC_LCD_HPP */
