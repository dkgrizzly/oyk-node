/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "pico_picocalc_lcd.hpp"

#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#define PICOCALC_GRAM_HEIGHT 480U

static void lcd_write_cmd(oyk_u8 cmd) {
    gpio_put(OYK_PICOCALC_LCD_DC_PIN, 0);
    gpio_put(OYK_PICOCALC_LCD_CS_PIN, 0);
    spi_write_blocking(spi1, &cmd, 1);
    gpio_put(OYK_PICOCALC_LCD_CS_PIN, 1);
}

static void lcd_write_data(const oyk_u8* data, size_t len) {
    if (!data || len == 0) return;
    gpio_put(OYK_PICOCALC_LCD_DC_PIN, 1);
    gpio_put(OYK_PICOCALC_LCD_CS_PIN, 0);
    spi_write_blocking(spi1, data, len);
    gpio_put(OYK_PICOCALC_LCD_CS_PIN, 1);
}

static void lcd_write_u8(oyk_u8 v) {
    lcd_write_data(&v, 1);
}

static void lcd_set_window(oyk_u16 x0, oyk_u16 y0, oyk_u16 x1, oyk_u16 y1) {
    oyk_u8 xbuf[4];
    oyk_u8 ybuf[4];
    xbuf[0] = static_cast<oyk_u8>(x0 >> 8);
    xbuf[1] = static_cast<oyk_u8>(x0 & 0xFFU);
    xbuf[2] = static_cast<oyk_u8>(x1 >> 8);
    xbuf[3] = static_cast<oyk_u8>(x1 & 0xFFU);
    ybuf[0] = static_cast<oyk_u8>(y0 >> 8);
    ybuf[1] = static_cast<oyk_u8>(y0 & 0xFFU);
    ybuf[2] = static_cast<oyk_u8>(y1 >> 8);
    ybuf[3] = static_cast<oyk_u8>(y1 & 0xFFU);
    lcd_write_cmd(0x2AU);
    lcd_write_data(xbuf, 4);
    lcd_write_cmd(0x2BU);
    lcd_write_data(ybuf, 4);
    lcd_write_cmd(0x2CU);
}

static void lcd_set_vscroll_start(oyk_u16 line) {
    oyk_u8 buf[2];
    buf[0] = static_cast<oyk_u8>(line >> 8);
    buf[1] = static_cast<oyk_u8>(line & 0xFFU);
    lcd_write_cmd(0x37U); /* VSCRSADD */
    lcd_write_data(buf, 2);
}

PicoCalcLcd::PicoCalcLcd()
    : scroll_y_(0U), pixel_writes_(0UL), hw_scrolls_(0UL) {}

int PicoCalcLcd::init() {
    scroll_y_ = 0U;
    pixel_writes_ = 0UL;
    hw_scrolls_ = 0UL;

    spi_init(spi1, 40000000U);
    gpio_set_function(OYK_PICOCALC_LCD_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(OYK_PICOCALC_LCD_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(OYK_PICOCALC_LCD_MISO_PIN, GPIO_FUNC_SPI);

    gpio_init(OYK_PICOCALC_LCD_CS_PIN);
    gpio_set_dir(OYK_PICOCALC_LCD_CS_PIN, GPIO_OUT);
    gpio_put(OYK_PICOCALC_LCD_CS_PIN, 1);

    gpio_init(OYK_PICOCALC_LCD_DC_PIN);
    gpio_set_dir(OYK_PICOCALC_LCD_DC_PIN, GPIO_OUT);
    gpio_put(OYK_PICOCALC_LCD_DC_PIN, 1);

    gpio_init(OYK_PICOCALC_LCD_RST_PIN);
    gpio_set_dir(OYK_PICOCALC_LCD_RST_PIN, GPIO_OUT);
    gpio_put(OYK_PICOCALC_LCD_RST_PIN, 0);
    sleep_ms(20);
    gpio_put(OYK_PICOCALC_LCD_RST_PIN, 1);
    sleep_ms(120);

    lcd_write_cmd(0x11U); /* SLPOUT */
    sleep_ms(120);
    lcd_write_cmd(0x3AU); /* COLMOD: 16-bit RGB565 */
    lcd_write_u8(0x55U);
    lcd_write_cmd(0x36U); /* MADCTL */
    lcd_write_u8(0x48U);
    lcd_write_cmd(0x21U); /* INVON (IPS panel) */

    /* Configure 320px vertical scroll area inside 320px panel window */
    {
        oyk_u8 vs[6] = { 0x00U, 0x00U, 0x01U, 0x40U, 0x00U, 0x00U }; /* TFA=0, VSA=320, BFA=0 */
        lcd_write_cmd(0x33U); /* VSCRDEF */
        lcd_write_data(vs, 6);
    }
    lcd_set_vscroll_start(0U);
    lcd_write_cmd(0x29U); /* DISPON */
    clear(0x000000UL);
    return 1;
}

void PicoCalcLcd::fill_rect_cb(void* ctx, uint16_t px, uint16_t py, uint16_t pw, uint16_t ph, uint32_t bg_rgb24) {
    PicoCalcLcd* self = static_cast<PicoCalcLcd*>(ctx);
    if (!self || pw == 0U || ph == 0U || px >= OYK_PICOCALC_LCD_WIDTH || py >= OYK_PICOCALC_LCD_HEIGHT) return;
    if (px + pw > OYK_PICOCALC_LCD_WIDTH) pw = static_cast<uint16_t>(OYK_PICOCALC_LCD_WIDTH - px);
    if (py + ph > OYK_PICOCALC_LCD_HEIGHT) ph = static_cast<uint16_t>(OYK_PICOCALC_LCD_HEIGHT - py);

    const oyk_u16 c565 = oyk_vt_rgb24_to_565(bg_rgb24);
    const oyk_u8 hi = static_cast<oyk_u8>(c565 >> 8);
    const oyk_u8 lo = static_cast<oyk_u8>(c565 & 0xFFU);
    oyk_u8 row_buf[128]; /* 64 pixels/chunk keeps stack <= 256 B */

    for (uint16_t dy = 0; dy < ph; ++dy) {
        oyk_u16 phys_y = static_cast<oyk_u16>((self->scroll_y_ + py + dy) % OYK_PICOCALC_LCD_HEIGHT);
        lcd_set_window(px, phys_y, static_cast<uint16_t>(px + pw - 1U), phys_y);
        uint16_t rem = pw;
        while (rem > 0U) {
            uint16_t chunk = (rem > 64U) ? 64U : rem;
            for (uint16_t i = 0; i < chunk; ++i) {
                row_buf[i * 2U + 0U] = hi;
                row_buf[i * 2U + 1U] = lo;
            }
            lcd_write_data(row_buf, static_cast<size_t>(chunk) * 2U);
            rem = static_cast<uint16_t>(rem - chunk);
        }
    }
    self->pixel_writes_ += static_cast<oyk_u32>(pw) * static_cast<oyk_u32>(ph);
}

void PicoCalcLcd::blit_glyph_cb(void* ctx, uint16_t px, uint16_t py, uint16_t /*glyph*/,
                                const uint8_t* rows, uint8_t w, uint8_t h,
                                uint32_t fg_rgb24, uint32_t bg_rgb24, uint8_t attr) {
    PicoCalcLcd* self = static_cast<PicoCalcLcd*>(ctx);
    if (!self || !rows || w == 0U || h == 0U || px + w > OYK_PICOCALC_LCD_WIDTH || py + h > OYK_PICOCALC_LCD_HEIGHT) {
        return;
    }
    const oyk_u16 fg565 = oyk_vt_rgb24_to_565(fg_rgb24);
    const oyk_u16 bg565 = oyk_vt_rgb24_to_565(bg_rgb24);
    oyk_u8 span[16 * 8 * 2];
    for (uint8_t y = 0; y < h; ++y) {
        const uint8_t bits = rows[y];
        const int ul = ((attr & OYK_VT_ATTR_UNDERLINE) && (y == static_cast<uint8_t>(h - 1U)));
        for (uint8_t x = 0; x < w; ++x) {
            const oyk_u16 c = (ul || ((bits & (0x80U >> x)) != 0U)) ? fg565 : bg565;
            span[(y * w + x) * 2U + 0U] = static_cast<oyk_u8>(c >> 8);
            span[(y * w + x) * 2U + 1U] = static_cast<oyk_u8>(c & 0xFFU);
        }
    }
    oyk_u16 py0 = static_cast<oyk_u16>((self->scroll_y_ + py) % OYK_PICOCALC_LCD_HEIGHT);
    if (py0 + h <= OYK_PICOCALC_LCD_HEIGHT) {
        lcd_set_window(px, py0, static_cast<uint16_t>(px + w - 1U), static_cast<uint16_t>(py0 + h - 1U));
        lcd_write_data(span, static_cast<size_t>(w) * static_cast<size_t>(h) * 2U);
    } else {
        for (uint8_t y = 0; y < h; ++y) {
            oyk_u16 ry = static_cast<oyk_u16>((self->scroll_y_ + py + y) % OYK_PICOCALC_LCD_HEIGHT);
            lcd_set_window(px, ry, static_cast<uint16_t>(px + w - 1U), ry);
            lcd_write_data(&span[static_cast<size_t>(y) * w * 2U], static_cast<size_t>(w) * 2U);
        }
    }
    self->pixel_writes_ += static_cast<oyk_u32>(w) * static_cast<oyk_u32>(h);
}

int PicoCalcLcd::hw_scroll_cb(void* ctx, int16_t delta_rows, uint16_t cell_h) {
    PicoCalcLcd* self = static_cast<PicoCalcLcd*>(ctx);
    if (!self) return 0;
    int32_t next_y = (static_cast<int32_t>(self->scroll_y_) + static_cast<int32_t>(delta_rows) * cell_h) %
                     static_cast<int32_t>(OYK_PICOCALC_LCD_HEIGHT);
    if (next_y < 0) next_y += OYK_PICOCALC_LCD_HEIGHT;
    self->scroll_y_ = static_cast<oyk_u16>(next_y);
    lcd_set_vscroll_start(self->scroll_y_);
    self->hw_scrolls_++;
    return 1;
}

void PicoCalcLcd::clear(oyk_u32 rgb24) {
    scroll_y_ = 0U;
    lcd_set_vscroll_start(0U);
    fill_rect_cb(this, 0U, 0U, OYK_PICOCALC_LCD_WIDTH, OYK_PICOCALC_LCD_HEIGHT, rgb24);
}

void PicoCalcLcd::fill_renderer(OykVtRenderer* out_renderer) {
    if (!out_renderer) return;
    memset(out_renderer, 0, sizeof(*out_renderer));
    out_renderer->ctx = this;
    out_renderer->fill_rect = PicoCalcLcd::fill_rect_cb;
    out_renderer->blit_glyph = PicoCalcLcd::blit_glyph_cb;
    out_renderer->hw_scroll = PicoCalcLcd::hw_scroll_cb;
}

oyk_u32 PicoCalcLcd::pixel_writes() const { return pixel_writes_; }
oyk_u32 PicoCalcLcd::hw_scrolls() const { return hw_scrolls_; }
void PicoCalcLcd::reset_counters() { pixel_writes_ = 0UL; hw_scrolls_ = 0UL; }

#else /* Host test framebuffer backend */

PicoCalcLcd::PicoCalcLcd()
    : scroll_y_(0U), pixel_writes_(0UL), hw_scrolls_(0UL) {
    oyk_vt_fb_init(&fb_, pixels_, OYK_PICOCALC_LCD_WIDTH, OYK_PICOCALC_LCD_HEIGHT, 1);
}

int PicoCalcLcd::init() {
    scroll_y_ = 0U;
    pixel_writes_ = 0UL;
    hw_scrolls_ = 0UL;
    oyk_vt_fb_init(&fb_, pixels_, OYK_PICOCALC_LCD_WIDTH, OYK_PICOCALC_LCD_HEIGHT, 1);
    return 1;
}

void PicoCalcLcd::clear(oyk_u32 rgb24) {
    OykVtRenderer r;
    scroll_y_ = 0U;
    fb_.scroll_y = 0U;
    oyk_vt_fb_renderer(&fb_, &r);
    if (r.fill_rect) {
        r.fill_rect(r.ctx, 0U, 0U, OYK_PICOCALC_LCD_WIDTH, OYK_PICOCALC_LCD_HEIGHT, rgb24);
    }
    pixel_writes_ = fb_.pixel_writes;
    hw_scrolls_ = fb_.hw_scrolls;
}

void PicoCalcLcd::fill_renderer(OykVtRenderer* out_renderer) {
    oyk_vt_fb_renderer(&fb_, out_renderer);
}

oyk_u32 PicoCalcLcd::pixel_writes() const { return fb_.pixel_writes; }
oyk_u32 PicoCalcLcd::hw_scrolls() const { return fb_.hw_scrolls; }
void PicoCalcLcd::reset_counters() { fb_.pixel_writes = 0UL; fb_.hw_scrolls = 0UL; }

oyk_u16 PicoCalcLcd::sample_pixel(oyk_u16 x, oyk_u16 y) const {
    return oyk_vt_fb_get_pixel(&fb_, x, y);
}

int PicoCalcLcd::save_bmp(const char* path) const {
    return oyk_vt_fb_save_bmp(&fb_, path);
}

#endif
