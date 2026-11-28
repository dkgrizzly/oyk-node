/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "pico_picocalc_lcd.hpp"

#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#define PICOCALC_GRAM_HEIGHT 480U

static void lcd_cmd(oyk_u8 cmd) {
    gpio_put(OYK_PICOCALC_LCD_DC_PIN, 0);
    gpio_put(OYK_PICOCALC_LCD_CS_PIN, 0);
    spi_write_blocking(spi1, &cmd, 1);
    gpio_put(OYK_PICOCALC_LCD_CS_PIN, 1);
}

static void lcd_cmd_data(oyk_u8 cmd, const oyk_u8* data, size_t len) {
    gpio_put(OYK_PICOCALC_LCD_DC_PIN, 0);
    gpio_put(OYK_PICOCALC_LCD_CS_PIN, 0);
    spi_write_blocking(spi1, &cmd, 1);
    if (data && len > 0U) {
        gpio_put(OYK_PICOCALC_LCD_DC_PIN, 1);
        spi_write_blocking(spi1, data, len);
    }
    gpio_put(OYK_PICOCALC_LCD_CS_PIN, 1);
}

static void lcd_cmd_u8(oyk_u8 cmd, oyk_u8 val) {
    lcd_cmd_data(cmd, &val, 1U);
}

static void lcd_begin_ramwr(oyk_u16 x0, oyk_u16 y0, oyk_u16 x1, oyk_u16 y1) {
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
    lcd_cmd_data(0x2AU, xbuf, 4U);
    lcd_cmd_data(0x2BU, ybuf, 4U);
    oyk_u8 ramwr = 0x2CU;
    gpio_put(OYK_PICOCALC_LCD_DC_PIN, 0);
    gpio_put(OYK_PICOCALC_LCD_CS_PIN, 0);
    spi_write_blocking(spi1, &ramwr, 1U);
    gpio_put(OYK_PICOCALC_LCD_DC_PIN, 1);
}

static void lcd_end_ramwr() {
    gpio_put(OYK_PICOCALC_LCD_CS_PIN, 1);
}

static void lcd_set_vscroll_start(oyk_u16 line) {
    oyk_u8 buf[2];
    buf[0] = static_cast<oyk_u8>(line >> 8);
    buf[1] = static_cast<oyk_u8>(line & 0xFFU);
    lcd_cmd_data(0x37U, buf, 2U); /* VSCRSADD */
}

static void lcd_fill_band_rgb666(oyk_u16 x0, oyk_u16 y0, oyk_u16 w, oyk_u16 h,
                                 oyk_u8 r, oyk_u8 g, oyk_u8 b) {
    if (w == 0U || h == 0U) return;
    oyk_u8 chunk_buf[64U * 3U];
    for (uint16_t i = 0U; i < 64U; ++i) {
        chunk_buf[i * 3U + 0U] = r;
        chunk_buf[i * 3U + 1U] = g;
        chunk_buf[i * 3U + 2U] = b;
    }
    lcd_begin_ramwr(x0, y0, static_cast<oyk_u16>(x0 + w - 1U), static_cast<oyk_u16>(y0 + h - 1U));
    oyk_u32 rem = static_cast<oyk_u32>(w) * static_cast<oyk_u32>(h);
    while (rem > 0UL) {
        oyk_u32 n = (rem > 64UL) ? 64UL : rem;
        spi_write_blocking(spi1, chunk_buf, static_cast<size_t>(n) * 3U);
        rem -= n;
    }
    lcd_end_ramwr();
}

PicoCalcLcd::PicoCalcLcd()
    : scroll_y_(0U), pixel_writes_(0UL), hw_scrolls_(0UL) {}

int PicoCalcLcd::init() {
    scroll_y_ = 0U;
    pixel_writes_ = 0UL;
    hw_scrolls_ = 0UL;

    spi_init(spi1, 16000000U);
    spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
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
    gpio_put(OYK_PICOCALC_LCD_RST_PIN, 1);
    sleep_ms(10);
    gpio_put(OYK_PICOCALC_LCD_RST_PIN, 0);
    sleep_ms(20);
    gpio_put(OYK_PICOCALC_LCD_RST_PIN, 1);
    sleep_ms(120);

    lcd_cmd(0x01U); /* SWRESET */
    sleep_ms(120);
    lcd_cmd_u8(0xF0U, 0xC3U); /* Unlock Command Set 2 */
    lcd_cmd_u8(0xF0U, 0x96U);
    lcd_cmd_u8(0x36U, 0x48U); /* MADCTL: MX=1, BGR=1 */
    lcd_cmd_u8(0x3AU, 0x66U); /* COLMOD: 18-bit RGB666 over SPI */
    lcd_cmd_u8(0xB4U, 0x01U); /* 1-dot inversion */
    {
        static const oyk_u8 dfc[3] = { 0x02U, 0x02U, 0x3BU };
        lcd_cmd_data(0xB6U, dfc, sizeof(dfc)); /* MCU GRAM mode, 480 lines */
    }
    lcd_cmd_u8(0xB7U, 0xC6U); /* Entry Mode Set */
    {
        static const oyk_u8 p1[2] = { 0x80U, 0x45U };
        lcd_cmd_data(0xC0U, p1, sizeof(p1));
    }
    lcd_cmd_u8(0xC1U, 0x13U); /* Power Control 2 */
    lcd_cmd_u8(0xC2U, 0xA7U); /* Power Control 3 */
    lcd_cmd_u8(0xC5U, 0x0AU); /* VCOM Control */
    {
        static const oyk_u8 doca[8] = {
            0x40U, 0x8AU, 0x00U, 0x00U, 0x29U, 0x19U, 0xA5U, 0x33U
        };
        lcd_cmd_data(0xE8U, doca, sizeof(doca));
    }
    {
        static const oyk_u8 pgamma[14] = {
            0xD0U, 0x08U, 0x0FU, 0x06U, 0x06U, 0x33U, 0x30U,
            0x33U, 0x47U, 0x17U, 0x13U, 0x13U, 0x2BU, 0x31U
        };
        static const oyk_u8 ngamma[14] = {
            0xD0U, 0x0AU, 0x11U, 0x0BU, 0x09U, 0x07U, 0x2FU,
            0x33U, 0x47U, 0x38U, 0x15U, 0x16U, 0x2CU, 0x32U
        };
        lcd_cmd_data(0xE0U, pgamma, sizeof(pgamma));
        lcd_cmd_data(0xE1U, ngamma, sizeof(ngamma));
    }
    lcd_cmd_u8(0xF0U, 0x3CU); /* Lock Command Set 2 */
    lcd_cmd_u8(0xF0U, 0x69U);
    lcd_cmd(0x21U); /* INVON (IPS panel) */
    lcd_cmd(0x11U); /* SLPOUT */
    sleep_ms(120);

    /* TFA(0) + VSA(320) + BFA(160) = 480 (GRAM height) */
    {
        static const oyk_u8 vs[6] = { 0x00U, 0x00U, 0x01U, 0x40U, 0x00U, 0xA0U };
        lcd_cmd_data(0x33U, vs, sizeof(vs)); /* VSCRDEF */
    }
    lcd_set_vscroll_start(0U);
    lcd_fill_band_rgb666(0U, 0U, OYK_PICOCALC_LCD_WIDTH, PICOCALC_GRAM_HEIGHT, 0U, 0U, 0U);
    lcd_cmd(0x29U); /* DISPON */
    sleep_ms(50);
    return 1;
}

void PicoCalcLcd::fill_rect_cb(void* ctx, uint16_t px, uint16_t py, uint16_t pw, uint16_t ph, uint32_t bg_rgb24) {
    PicoCalcLcd* self = static_cast<PicoCalcLcd*>(ctx);
    if (!self || pw == 0U || ph == 0U || px >= OYK_PICOCALC_LCD_WIDTH || py >= OYK_PICOCALC_LCD_HEIGHT) return;
    if (px + pw > OYK_PICOCALC_LCD_WIDTH) pw = static_cast<uint16_t>(OYK_PICOCALC_LCD_WIDTH - px);
    if (py + ph > OYK_PICOCALC_LCD_HEIGHT) ph = static_cast<uint16_t>(OYK_PICOCALC_LCD_HEIGHT - py);

    const oyk_u8 r = static_cast<oyk_u8>((bg_rgb24 >> 16) & 0xFCU);
    const oyk_u8 g = static_cast<oyk_u8>((bg_rgb24 >> 8) & 0xFCU);
    const oyk_u8 b = static_cast<oyk_u8>(bg_rgb24 & 0xFCU);

    oyk_u16 py0 = static_cast<oyk_u16>((self->scroll_y_ + py) % OYK_PICOCALC_LCD_HEIGHT);
    if (py0 + ph <= OYK_PICOCALC_LCD_HEIGHT) {
        lcd_fill_band_rgb666(px, py0, pw, ph, r, g, b);
    } else {
        oyk_u16 h1 = static_cast<oyk_u16>(OYK_PICOCALC_LCD_HEIGHT - py0);
        oyk_u16 h2 = static_cast<oyk_u16>(ph - h1);
        lcd_fill_band_rgb666(px, py0, pw, h1, r, g, b);
        lcd_fill_band_rgb666(px, 0U, pw, h2, r, g, b);
    }
    self->pixel_writes_ += static_cast<oyk_u32>(pw) * static_cast<oyk_u32>(ph);
}

void PicoCalcLcd::blit_glyph_cb(void* ctx, uint16_t px, uint16_t py, uint16_t /*glyph*/,
                                const uint8_t* rows, uint8_t w, uint8_t h,
                                uint32_t fg_rgb24, uint32_t bg_rgb24, uint8_t attr) {
    PicoCalcLcd* self = static_cast<PicoCalcLcd*>(ctx);
    if (!self || !rows || w == 0U || h == 0U || w > 16U ||
        px + w > OYK_PICOCALC_LCD_WIDTH || py + h > OYK_PICOCALC_LCD_HEIGHT) {
        return;
    }
    const oyk_u8 fg_r = static_cast<oyk_u8>((fg_rgb24 >> 16) & 0xFCU);
    const oyk_u8 fg_g = static_cast<oyk_u8>((fg_rgb24 >> 8) & 0xFCU);
    const oyk_u8 fg_b = static_cast<oyk_u8>(fg_rgb24 & 0xFCU);
    const oyk_u8 bg_r = static_cast<oyk_u8>((bg_rgb24 >> 16) & 0xFCU);
    const oyk_u8 bg_g = static_cast<oyk_u8>((bg_rgb24 >> 8) & 0xFCU);
    const oyk_u8 bg_b = static_cast<oyk_u8>(bg_rgb24 & 0xFCU);
    oyk_u8 row_buf[16U * 3U];

    oyk_u16 py0 = static_cast<oyk_u16>((self->scroll_y_ + py) % OYK_PICOCALC_LCD_HEIGHT);
    if (py0 + h <= OYK_PICOCALC_LCD_HEIGHT) {
        lcd_begin_ramwr(px, py0, static_cast<oyk_u16>(px + w - 1U), static_cast<oyk_u16>(py0 + h - 1U));
        for (uint8_t y = 0; y < h; ++y) {
            const uint8_t bits = rows[y];
            const int ul = ((attr & OYK_VT_ATTR_UNDERLINE) && (y == static_cast<uint8_t>(h - 1U)));
            for (uint8_t x = 0; x < w; ++x) {
                const int on = (ul || ((bits & (0x80U >> x)) != 0U));
                row_buf[x * 3U + 0U] = on ? fg_r : bg_r;
                row_buf[x * 3U + 1U] = on ? fg_g : bg_g;
                row_buf[x * 3U + 2U] = on ? fg_b : bg_b;
            }
            spi_write_blocking(spi1, row_buf, static_cast<size_t>(w) * 3U);
        }
        lcd_end_ramwr();
    } else {
        for (uint8_t y = 0; y < h; ++y) {
            oyk_u16 ry = static_cast<oyk_u16>((self->scroll_y_ + py + y) % OYK_PICOCALC_LCD_HEIGHT);
            const uint8_t bits = rows[y];
            const int ul = ((attr & OYK_VT_ATTR_UNDERLINE) && (y == static_cast<uint8_t>(h - 1U)));
            for (uint8_t x = 0; x < w; ++x) {
                const int on = (ul || ((bits & (0x80U >> x)) != 0U));
                row_buf[x * 3U + 0U] = on ? fg_r : bg_r;
                row_buf[x * 3U + 1U] = on ? fg_g : bg_g;
                row_buf[x * 3U + 2U] = on ? fg_b : bg_b;
            }
            lcd_begin_ramwr(px, ry, static_cast<oyk_u16>(px + w - 1U), ry);
            spi_write_blocking(spi1, row_buf, static_cast<size_t>(w) * 3U);
            lcd_end_ramwr();
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
    const oyk_u8 r = static_cast<oyk_u8>((rgb24 >> 16) & 0xFCU);
    const oyk_u8 g = static_cast<oyk_u8>((rgb24 >> 8) & 0xFCU);
    const oyk_u8 b = static_cast<oyk_u8>(rgb24 & 0xFCU);
    lcd_fill_band_rgb666(0U, 0U, OYK_PICOCALC_LCD_WIDTH, PICOCALC_GRAM_HEIGHT, r, g, b);
    pixel_writes_ += static_cast<oyk_u32>(OYK_PICOCALC_LCD_WIDTH) * static_cast<oyk_u32>(PICOCALC_GRAM_HEIGHT);
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
