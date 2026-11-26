/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_DOS_GFX_HPP
#define OYK_DOS_GFX_HPP

/*
 * 386+ Graphical Console Renderer for MS-DOS (oyknodeg.exe).
 * Supports VESA VBE (fast 8bpp programmable-DAC -> 16/15bpp -> 24/32bpp at 640x480
 * filling all 80x30 cells, plus optional 800x600 100x37 and 1024x768 128x48), VGA Mode 12h
 * (640x480x16 80x30 with 18-bit DAC), and EGA Mode 10h (640x350x16 80x25).
 * Optimizes throughput with 32-bit rep movsd scanlines, cached palette writes, zero-copy
 * pre-decoded font lookups, pre-resolved dirty runs, alpha pixel shadows, smooth scrolling,
 * and a save-under mouse pointer sprite.
 */

#include "../compat/oyk_portable.hpp"
#include "dos_font8x16.hpp"
#include "dos_video.hpp"
#include "dos_xms.hpp"

#define OYK_GFX_MODE_NONE    0
#define OYK_GFX_MODE_VBE24   1
#define OYK_GFX_MODE_VBE16   2
#define OYK_GFX_MODE_VBE8    3
#define OYK_GFX_MODE_VGA16   4
#define OYK_GFX_MODE_EGA16   5
#define OYK_GFX_MODE_VBE800  6
#define OYK_GFX_MODE_VBE1024 7

#define OYK_GFX_MAX_SHADOWS 12

struct OykDosGfxShadowRect {
    int top;
    int left;
    int h;
    int w;
};

struct OykDosGfx {
    int active;
    int sim_mode;
    int mode;
    int width;
    int height;
    int char_w;
    int char_h;
    int cols;
    int rows;
    int cells;
    int bpp;
    int win_seg;
    oyk_u16 bytes_per_scanline;
    oyk_u16 win_granularity_kb;
    oyk_u16 cur_bank;
    int orig_bios_mode;

    oyk_u32 palette16[16];
    oyk_u32 hw_palette16[16];
    int hw_palette_valid;
    oyk_u32 hw_palette_writes;
    int palette_dirty;
    int has_custom_theme;

    oyk_u16* prev_cells;
    oyk_u32* cell_fg_rgb;
    oyk_u32* cell_bg_rgb;
    oyk_u8*  cell_has_rgb;
    oyk_u8*  prev_shadow_mask;
    oyk_u8*  shadow_mask;

    OykDosGfxShadowRect shadows[OYK_GFX_MAX_SHADOWS];
    int shadow_count;

    int cursor_x;
    int cursor_y;
    int cursor_visible;
    int prev_cursor_x;
    int prev_cursor_y;
    int prev_cursor_visible;

    oyk_u8* scan_buf;
    OykDosFont font;
    OykDosExtMem extmem;
    oyk_u8* sim_rgb; /* Host 24-bit RGB24 framebuffer (width * height * 3) */
};

int dos_gfx_parse_video_arg(const char* arg);
const char* dos_gfx_mode_name(int mode);

/* Hardware init on MS-DOS (tries preferred mode -> fallback chain). */
int dos_gfx_init(OykDosGfx* g, int preferred_mode);

/* Host simulation init (no DOS BIOS calls; allocates width*height*3 RGB24 buffer). */
int dos_gfx_init_sim(OykDosGfx* g, int mode);

void dos_gfx_shutdown(OykDosGfx* g);

void dos_gfx_set_theme(OykDosGfx* g, const OykThemeConfig* tc, int is_native);
void dos_gfx_clear_cell_rgb(OykDosGfx* g);
void dos_gfx_set_cell_rgb(OykDosGfx* g, int row, int col, int len, oyk_u32 fg_rgb, oyk_u32 bg_rgb);

void dos_gfx_clear_shadows(OykDosGfx* g);
void dos_gfx_add_shadow(OykDosGfx* g, int top, int left, int h, int w);

void dos_gfx_set_cursor(OykDosGfx* g, int px_x, int px_y, int visible);

/* Render updated dynamic cells + pixel drop shadows + cursor to video / sim_rgb. */
void dos_gfx_present(OykDosGfx* g, const oyk_u16* vram, int force_all);

/* Smooth pixel-row scroll across interior cell rows [top_row .. top_row+rows-1]. */
int dos_gfx_smooth_scroll(OykDosGfx* g,
                          const oyk_u16* prev_vram,
                          const oyk_u16* new_vram,
                          int top_row, int left_col, int rows, int cols,
                          int scroll_up);

/* Write current framebuffer as a standard uncompressed 24-bit BMP file. */
int dos_gfx_write_bmp(const OykDosGfx* g, const char* path);

#endif /* OYK_DOS_GFX_HPP */
