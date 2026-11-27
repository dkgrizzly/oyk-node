/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "lyra_picocalc_lcd.hpp"

#include <string.h>

#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD

#include <rtthread.h>
#include <rtdevice.h>

static void lyra_lcd_write_cmd(struct rt_spi_device* spi, oyk_u8 cmd) {
    rt_pin_write(OYK_LYRA_LCD_DC_PIN, PIN_LOW);
    if (spi) rt_spi_send(spi, &cmd, 1U);
}

static void lyra_lcd_write_data(struct rt_spi_device* spi, const oyk_u8* data, size_t len) {
    if (!spi || !data || len == 0U) return;
    rt_pin_write(OYK_LYRA_LCD_DC_PIN, PIN_HIGH);
    rt_spi_send(spi, data, len);
}

static void lyra_lcd_write_u8(struct rt_spi_device* spi, oyk_u8 v) {
    lyra_lcd_write_data(spi, &v, 1U);
}

static void lyra_lcd_set_window(struct rt_spi_device* spi, oyk_u16 x0, oyk_u16 y0, oyk_u16 x1, oyk_u16 y1) {
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
    lyra_lcd_write_cmd(spi, 0x2AU);
    lyra_lcd_write_data(spi, xbuf, 4U);
    lyra_lcd_write_cmd(spi, 0x2BU);
    lyra_lcd_write_data(spi, ybuf, 4U);
    lyra_lcd_write_cmd(spi, 0x2CU);
}

static void lyra_lcd_set_vscroll(struct rt_spi_device* spi, oyk_u16 line) {
    oyk_u8 buf[2];
    buf[0] = static_cast<oyk_u8>(line >> 8);
    buf[1] = static_cast<oyk_u8>(line & 0xFFU);
    lyra_lcd_write_cmd(spi, 0x37U); /* VSCRSADD */
    lyra_lcd_write_data(spi, buf, 2U);
}

LyraPicoCalcLcd::LyraPicoCalcLcd()
    : spi_dev_(NULL), scroll_y_(0U), pixel_writes_(0UL), hw_scrolls_(0UL) {}

int LyraPicoCalcLcd::init() {
    scroll_y_ = 0U;
    pixel_writes_ = 0UL;
    hw_scrolls_ = 0UL;

    rt_pin_mode(OYK_LYRA_LCD_DC_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(OYK_LYRA_LCD_RST_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(OYK_LYRA_LCD_DC_PIN, PIN_HIGH);
    rt_pin_write(OYK_LYRA_LCD_RST_PIN, PIN_LOW);
    rt_thread_mdelay(20);
    rt_pin_write(OYK_LYRA_LCD_RST_PIN, PIN_HIGH);
    rt_thread_mdelay(120);

    struct rt_spi_device* spi = reinterpret_cast<struct rt_spi_device*>(rt_device_find(OYK_LYRA_LCD_SPI_DEV));
    spi_dev_ = spi;
    if (spi) {
        struct rt_spi_configuration scfg;
        memset(&scfg, 0, sizeof(scfg));
        scfg.data_width = 8U;
        scfg.mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
        scfg.max_hz = 80000000UL;
        rt_spi_configure(spi, &scfg);
    }

    lyra_lcd_write_cmd(spi, 0x11U); /* SLPOUT */
    rt_thread_mdelay(120);
    lyra_lcd_write_cmd(spi, 0x3AU); /* COLMOD: 16-bit RGB565 */
    lyra_lcd_write_u8(spi, 0x55U);
    lyra_lcd_write_cmd(spi, 0x36U); /* MADCTL */
    lyra_lcd_write_u8(spi, 0x48U);
    lyra_lcd_write_cmd(spi, 0x21U); /* INVON (IPS) */

    {
        oyk_u8 vs[6] = { 0x00U, 0x00U, 0x01U, 0x40U, 0x00U, 0x00U };
        lyra_lcd_write_cmd(spi, 0x33U); /* VSCRDEF */
        lyra_lcd_write_data(spi, vs, 6U);
    }
    lyra_lcd_set_vscroll(spi, 0U);
    lyra_lcd_write_cmd(spi, 0x29U); /* DISPON */
    clear(0x000000UL);
    return spi ? 1 : 0;
}

void LyraPicoCalcLcd::fill_rect_cb(void* ctx, uint16_t px, uint16_t py, uint16_t pw, uint16_t ph, uint32_t bg_rgb24) {
    LyraPicoCalcLcd* self = static_cast<LyraPicoCalcLcd*>(ctx);
    if (!self || pw == 0U || ph == 0U || px >= OYK_LYRA_LCD_WIDTH || py >= OYK_LYRA_LCD_HEIGHT) return;
    if (px + pw > OYK_LYRA_LCD_WIDTH) pw = static_cast<uint16_t>(OYK_LYRA_LCD_WIDTH - px);
    if (py + ph > OYK_LYRA_LCD_HEIGHT) ph = static_cast<uint16_t>(OYK_LYRA_LCD_HEIGHT - py);

    struct rt_spi_device* spi = reinterpret_cast<struct rt_spi_device*>(self->spi_dev_);
    const oyk_u16 c565 = oyk_vt_rgb24_to_565(bg_rgb24);
    const oyk_u8 hi = static_cast<oyk_u8>(c565 >> 8);
    const oyk_u8 lo = static_cast<oyk_u8>(c565 & 0xFFU);
    oyk_u8 row_buf[128];

    for (uint16_t dy = 0; dy < ph; ++dy) {
        oyk_u16 phys_y = static_cast<oyk_u16>((self->scroll_y_ + py + dy) % OYK_LYRA_LCD_HEIGHT);
        lyra_lcd_set_window(spi, px, phys_y, static_cast<uint16_t>(px + pw - 1U), phys_y);
        uint16_t rem = pw;
        while (rem > 0U) {
            uint16_t chunk = (rem > 64U) ? 64U : rem;
            for (uint16_t i = 0; i < chunk; ++i) {
                row_buf[i * 2U + 0U] = hi;
                row_buf[i * 2U + 1U] = lo;
            }
            lyra_lcd_write_data(spi, row_buf, static_cast<size_t>(chunk) * 2U);
            rem = static_cast<uint16_t>(rem - chunk);
        }
    }
    self->pixel_writes_ += static_cast<oyk_u32>(pw) * static_cast<oyk_u32>(ph);
}

void LyraPicoCalcLcd::blit_glyph_cb(void* ctx, uint16_t px, uint16_t py, uint16_t /*glyph*/,
                                    const uint8_t* rows, uint8_t w, uint8_t h,
                                    uint32_t fg_rgb24, uint32_t bg_rgb24, uint8_t attr) {
    LyraPicoCalcLcd* self = static_cast<LyraPicoCalcLcd*>(ctx);
    if (!self || !rows || w == 0U || h == 0U || px + w > OYK_LYRA_LCD_WIDTH || py + h > OYK_LYRA_LCD_HEIGHT) {
        return;
    }
    struct rt_spi_device* spi = reinterpret_cast<struct rt_spi_device*>(self->spi_dev_);
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
    oyk_u16 py0 = static_cast<oyk_u16>((self->scroll_y_ + py) % OYK_LYRA_LCD_HEIGHT);
    if (py0 + h <= OYK_LYRA_LCD_HEIGHT) {
        lyra_lcd_set_window(spi, px, py0, static_cast<uint16_t>(px + w - 1U), static_cast<uint16_t>(py0 + h - 1U));
        lyra_lcd_write_data(spi, span, static_cast<size_t>(w) * static_cast<size_t>(h) * 2U);
    } else {
        for (uint8_t y = 0; y < h; ++y) {
            oyk_u16 ry = static_cast<oyk_u16>((self->scroll_y_ + py + y) % OYK_LYRA_LCD_HEIGHT);
            lyra_lcd_set_window(spi, px, ry, static_cast<uint16_t>(px + w - 1U), ry);
            lyra_lcd_write_data(spi, &span[static_cast<size_t>(y) * w * 2U], static_cast<size_t>(w) * 2U);
        }
    }
    self->pixel_writes_ += static_cast<oyk_u32>(w) * static_cast<oyk_u32>(h);
}

int LyraPicoCalcLcd::hw_scroll_cb(void* ctx, int16_t delta_rows, uint16_t cell_h) {
    LyraPicoCalcLcd* self = static_cast<LyraPicoCalcLcd*>(ctx);
    if (!self) return 0;
    int32_t next_y = (static_cast<int32_t>(self->scroll_y_) + static_cast<int32_t>(delta_rows) * cell_h) %
                     static_cast<int32_t>(OYK_LYRA_LCD_HEIGHT);
    if (next_y < 0) next_y += OYK_LYRA_LCD_HEIGHT;
    self->scroll_y_ = static_cast<oyk_u16>(next_y);
    lyra_lcd_set_vscroll(reinterpret_cast<struct rt_spi_device*>(self->spi_dev_), self->scroll_y_);
    self->hw_scrolls_++;
    return 1;
}

void LyraPicoCalcLcd::clear(oyk_u32 rgb24) {
    scroll_y_ = 0U;
    lyra_lcd_set_vscroll(reinterpret_cast<struct rt_spi_device*>(spi_dev_), 0U);
    fill_rect_cb(this, 0U, 0U, OYK_LYRA_LCD_WIDTH, OYK_LYRA_LCD_HEIGHT, rgb24);
}

void LyraPicoCalcLcd::fill_renderer(OykVtRenderer* out_renderer) {
    if (!out_renderer) return;
    memset(out_renderer, 0, sizeof(*out_renderer));
    out_renderer->ctx = this;
    out_renderer->fill_rect = LyraPicoCalcLcd::fill_rect_cb;
    out_renderer->blit_glyph = LyraPicoCalcLcd::blit_glyph_cb;
    out_renderer->hw_scroll = LyraPicoCalcLcd::hw_scroll_cb;
}

oyk_u32 LyraPicoCalcLcd::pixel_writes() const { return pixel_writes_; }
oyk_u32 LyraPicoCalcLcd::hw_scrolls() const { return hw_scrolls_; }
void LyraPicoCalcLcd::reset_counters() { pixel_writes_ = 0UL; hw_scrolls_ = 0UL; }

#else

LyraPicoCalcLcd::LyraPicoCalcLcd()
    : scroll_y_(0U), pixel_writes_(0UL), hw_scrolls_(0UL) {
    oyk_vt_fb_init(&fb_, pixels_, OYK_LYRA_LCD_WIDTH, OYK_LYRA_LCD_HEIGHT, 1);
}

int LyraPicoCalcLcd::init() {
    scroll_y_ = 0U;
    pixel_writes_ = 0UL;
    hw_scrolls_ = 0UL;
    oyk_vt_fb_init(&fb_, pixels_, OYK_LYRA_LCD_WIDTH, OYK_LYRA_LCD_HEIGHT, 1);
    return 1;
}

void LyraPicoCalcLcd::clear(oyk_u32 rgb24) {
    OykVtRenderer r;
    scroll_y_ = 0U;
    fb_.scroll_y = 0U;
    oyk_vt_fb_renderer(&fb_, &r);
    if (r.fill_rect) {
        r.fill_rect(r.ctx, 0U, 0U, OYK_LYRA_LCD_WIDTH, OYK_LYRA_LCD_HEIGHT, rgb24);
    }
    pixel_writes_ = fb_.pixel_writes;
    hw_scrolls_ = fb_.hw_scrolls;
}

void LyraPicoCalcLcd::fill_renderer(OykVtRenderer* out_renderer) {
    oyk_vt_fb_renderer(&fb_, out_renderer);
}

oyk_u32 LyraPicoCalcLcd::pixel_writes() const { return fb_.pixel_writes; }
oyk_u32 LyraPicoCalcLcd::hw_scrolls() const { return fb_.hw_scrolls; }
void LyraPicoCalcLcd::reset_counters() { fb_.pixel_writes = 0UL; fb_.hw_scrolls = 0UL; }

oyk_u16 LyraPicoCalcLcd::sample_pixel(oyk_u16 x, oyk_u16 y) const {
    return oyk_vt_fb_get_pixel(&fb_, x, y);
}

int LyraPicoCalcLcd::save_bmp(const char* path) const {
    return oyk_vt_fb_save_bmp(&fb_, path);
}

#endif
