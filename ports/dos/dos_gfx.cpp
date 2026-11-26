/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "dos_gfx.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
  #include <conio.h>
  #include <dos.h>
  #include <i86.h>
#endif

static const oyk_u32 k_vga16_default_rgb[16] = {
    0x000000UL, 0x0000AAUL, 0x00AA00UL, 0x00AAAAUL,
    0xAA0000UL, 0xAA00AAUL, 0xAA5500UL, 0xAAAAAAUL,
    0x555555UL, 0x5555FFUL, 0x55FF55UL, 0x55FFFFUL,
    0xFF5555UL, 0xFF55FFUL, 0xFFFF55UL, 0xFFFFFFUL
};

static const char* k_video_names[6] = {
    "none", "vbe24", "vbe16", "vbe8", "vga", "ega"
};

/* 10x16 software pointer arrow bitmap (2-bit: 0=transparent, 1=black outline, 2=white fill) */
static const oyk_u16 k_arrow_outline[14] = {
    0xC000U, 0xE000U, 0xF000U, 0xF800U, 0xFC00U, 0xFE00U, 0xFF00U,
    0xFF80U, 0xFC00U, 0xEE00U, 0xC600U, 0x0700U, 0x0300U, 0x0000U
};
static const oyk_u16 k_arrow_fill[14] = {
    0x0000U, 0x4000U, 0x6000U, 0x7000U, 0x7800U, 0x7C00U, 0x7E00U,
    0x7800U, 0x6800U, 0x4400U, 0x0400U, 0x0200U, 0x0000U, 0x0000U
};

int dos_gfx_parse_video_arg(const char* arg) {
    if (!arg || !arg[0] || strcmp(arg, "auto") == 0) return OYK_GFX_MODE_NONE;
    if (strcmp(arg, "vbe24") == 0 || strcmp(arg, "vbe32") == 0) return OYK_GFX_MODE_VBE24;
    if (strcmp(arg, "vbe16") == 0 || strcmp(arg, "vbe15") == 0) return OYK_GFX_MODE_VBE16;
    if (strcmp(arg, "vbe8") == 0) return OYK_GFX_MODE_VBE8;
    if (strcmp(arg, "vga") == 0 || strcmp(arg, "vga16") == 0) return OYK_GFX_MODE_VGA16;
    if (strcmp(arg, "ega") == 0 || strcmp(arg, "ega16") == 0) return OYK_GFX_MODE_EGA16;
    return -1;
}

const char* dos_gfx_mode_name(int mode) {
    if (mode >= 0 && mode <= 5) return k_video_names[mode];
    return "unknown";
}

static oyk_u32 scale_rgb_half(oyk_u32 rgb) {
    oyk_u32 r = ((rgb >> 16) & 0xFFUL) >> 1;
    oyk_u32 g = ((rgb >> 8) & 0xFFUL) >> 1;
    oyk_u32 b = (rgb & 0xFFUL) >> 1;
    return (r << 16) | (g << 8) | b;
}

static int color_dist_sq(oyk_u32 a, oyk_u32 b) {
    int dr = static_cast<int>((a >> 16) & 0xFFUL) - static_cast<int>((b >> 16) & 0xFFUL);
    int dg = static_cast<int>((a >> 8) & 0xFFUL) - static_cast<int>((b >> 8) & 0xFFUL);
    int db = static_cast<int>(a & 0xFFUL) - static_cast<int>(b & 0xFFUL);
    return (dr * dr) / 64 + (dg * dg) / 64 + (db * db) / 64;
}

static oyk_u8 nearest_pal16(const oyk_u32* pal16, oyk_u32 rgb) {
    int best = 0;
    int best_d = 0x7FFF;
    for (int i = 0; i < 16; ++i) {
        int d = color_dist_sq(pal16[i], rgb);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return static_cast<oyk_u8>(best);
}

#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
static oyk_u8 rgb_to_vbe8_index(const OykDosGfx* g, oyk_u32 rgb, int is_shadow) {
    if (is_shadow) rgb = scale_rgb_half(rgb);
    int r = static_cast<int>((rgb >> 16) & 0xFFUL);
    int gg = static_cast<int>((rgb >> 8) & 0xFFUL);
    int b = static_cast<int>(rgb & 0xFFUL);
    /* Entries 0..15 = active theme 16-color palette, 16..31 = 50% shadow copies,
     * 32..247 = 6x6x6 RGB cube */
    for (int i = 0; i < 16; ++i) {
        oyk_u32 pr = is_shadow ? scale_rgb_half(g->palette16[i]) : g->palette16[i];
        if (pr == rgb) return static_cast<oyk_u8>(is_shadow ? (16 + i) : i);
    }
    int ri = (r * 5 + 127) / 255;
    int gi = (gg * 5 + 127) / 255;
    int bi = (b * 5 + 127) / 255;
    return static_cast<oyk_u8>(32 + ri * 36 + gi * 6 + bi);
}
#endif

static void program_hardware_palette(OykDosGfx* g) {
#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
    if (g->sim_mode || !g->active) return;
    if (g->mode == OYK_GFX_MODE_VGA16 || g->mode == OYK_GFX_MODE_VBE8) {
        int i;
        outp(0x3C8, 0);
        for (i = 0; i < 16; ++i) {
            oyk_u32 c = g->palette16[i];
            outp(0x3C9, static_cast<int>(((c >> 16) & 0xFFUL) >> 2));
            outp(0x3C9, static_cast<int>(((c >> 8) & 0xFFUL) >> 2));
            outp(0x3C9, static_cast<int>((c & 0xFFUL) >> 2));
        }
        if (g->mode == OYK_GFX_MODE_VBE8) {
            for (i = 0; i < 16; ++i) {
                oyk_u32 c = scale_rgb_half(g->palette16[i]);
                outp(0x3C9, static_cast<int>(((c >> 16) & 0xFFUL) >> 2));
                outp(0x3C9, static_cast<int>(((c >> 8) & 0xFFUL) >> 2));
                outp(0x3C9, static_cast<int>((c & 0xFFUL) >> 2));
            }
            for (int r = 0; r < 6; ++r) {
                for (int gv = 0; gv < 6; ++gv) {
                    for (int b = 0; b < 6; ++b) {
                        outp(0x3C9, (r * 63 + 2) / 5);
                        outp(0x3C9, (gv * 63 + 2) / 5);
                        outp(0x3C9, (b * 63 + 2) / 5);
                    }
                }
            }
        }
    } else if (g->mode == OYK_GFX_MODE_EGA16) {
        union REGS r;
        struct SREGS sr;
        oyk_u8 ega_tbl[17];
        for (int i = 0; i < 16; ++i) {
            oyk_u32 c = g->palette16[i];
            int rv = static_cast<int>((c >> 16) & 0xFFUL);
            int gv = static_cast<int>((c >> 8) & 0xFFUL);
            int bv = static_cast<int>(c & 0xFFUL);
            /* EGA 6-bit DAC layout: bit5=Rlo,4=Glo,3=Blo,2=Rhi,1=Ghi,0=Bhi */
            int r2 = (rv * 3 + 127) / 255;
            int g2 = (gv * 3 + 127) / 255;
            int b2 = (bv * 3 + 127) / 255;
            oyk_u8 code = 0;
            if (r2 & 2) code |= 0x04U;
            if (r2 & 1) code |= 0x20U;
            if (g2 & 2) code |= 0x02U;
            if (g2 & 1) code |= 0x10U;
            if (b2 & 2) code |= 0x01U;
            if (b2 & 1) code |= 0x08U;
            ega_tbl[i] = code;
        }
        ega_tbl[16] = 0x00U;
        memset(&r, 0, sizeof(r));
        memset(&sr, 0, sizeof(sr));
        r.x.ax = 0x1002U;
        sr.es = FP_SEG(ega_tbl);
        r.x.dx = FP_OFF(ega_tbl);
        int86x(0x10, &r, &r, &sr);
    }
#else
    (void)g;
#endif
}

void dos_gfx_set_theme(OykDosGfx* g, const OykThemeConfig* tc, int is_native) {
    if (!g) return;
    if (is_native || !tc) {
        memcpy(g->palette16, k_vga16_default_rgb, sizeof(g->palette16));
        g->has_custom_theme = 0;
    } else {
        /* Map DOS VGA16 attribute slots used in resolve_dos_theme_attrs() to exact
         * 24-bit theme role colors so VBE, 18-bit VGA DAC, and EGA render true themes. */
        g->palette16[0]  = tc->colors[OYK_TR_BG];
        g->palette16[1]  = tc->colors[OYK_TR_HEADER_BG];
        g->palette16[2]  = 0x008000UL; /* ALLOW pill background */
        g->palette16[3]  = tc->colors[OYK_TR_SELECTION_BG];
        g->palette16[4]  = 0xAA0000UL; /* DENY pill background */
        g->palette16[5]  = tc->colors[OYK_TR_PANEL_BG];
        g->palette16[6]  = tc->colors[OYK_TR_STATUS_BG];
        g->palette16[7]  = tc->colors[OYK_TR_FG];
        g->palette16[8]  = tc->colors[OYK_TR_PANEL_FG];
        g->palette16[9]  = tc->colors[OYK_TR_BORDER];
        g->palette16[10] = tc->colors[OYK_TR_OK];
        g->palette16[11] = tc->colors[OYK_TR_ACCENT];
        g->palette16[12] = tc->colors[OYK_TR_ERROR];
        g->palette16[13] = tc->colors[OYK_TR_BUTTON_BG];
        g->palette16[14] = tc->colors[OYK_TR_STATUS_FG];
        g->palette16[15] = tc->colors[OYK_TR_HEADER_FG];
        g->has_custom_theme = 1;
    }
    program_hardware_palette(g);
}

void dos_gfx_clear_cell_rgb(OykDosGfx* g) {
    if (!g) return;
    memset(g->cell_has_rgb, 0, sizeof(g->cell_has_rgb));
}

void dos_gfx_set_cell_rgb(OykDosGfx* g, int row, int col, int len, oyk_u32 fg_rgb, oyk_u32 bg_rgb) {
    if (!g || row < 0 || row >= OYK_DOS_VID_ROWS || len <= 0) return;
    for (int i = 0; i < len; ++i) {
        int c = col + i;
        if (c < 0 || c >= OYK_DOS_VID_COLS) continue;
        int idx = row * OYK_DOS_VID_COLS + c;
        g->cell_fg_rgb[idx] = fg_rgb;
        g->cell_bg_rgb[idx] = bg_rgb;
        g->cell_has_rgb[idx] = 1U;
    }
}

void dos_gfx_clear_shadows(OykDosGfx* g) {
    if (!g) return;
    g->shadow_count = 0;
    memset(g->shadow_mask, 0, sizeof(g->shadow_mask));
}

void dos_gfx_add_shadow(OykDosGfx* g, int top, int left, int h, int w) {
    if (!g || h <= 0 || w <= 0) return;
    if (g->shadow_count < OYK_GFX_MAX_SHADOWS) {
        g->shadows[g->shadow_count].top = top;
        g->shadows[g->shadow_count].left = left;
        g->shadows[g->shadow_count].h = h;
        g->shadows[g->shadow_count].w = w;
        ++g->shadow_count;
    }
    /* Mark right strip and bottom strip cells in shadow_mask for pixel-alpha darkening */
    int sc = left + w;
    if (sc >= 0 && sc < OYK_DOS_VID_COLS) {
        for (int r = top + 1; r <= top + h; ++r) {
            if (r >= 0 && r < OYK_DOS_VID_ROWS) {
                g->shadow_mask[r * OYK_DOS_VID_COLS + sc] = 1U;
            }
        }
    }
    int sr = top + h;
    if (sr >= 0 && sr < OYK_DOS_VID_ROWS) {
        for (int c = left + 1; c <= left + w; ++c) {
            if (c >= 0 && c < OYK_DOS_VID_COLS) {
                g->shadow_mask[sr * OYK_DOS_VID_COLS + c] = 1U;
            }
        }
    }
}

void dos_gfx_set_cursor(OykDosGfx* g, int px_x, int px_y, int visible) {
    if (!g) return;
    if (px_x < 0) px_x = 0;
    if (px_y < 0) px_y = 0;
    if (px_x >= g->width) px_x = g->width - 1;
    if (px_y >= g->height) px_y = g->height - 1;
    g->cursor_x = px_x;
    g->cursor_y = px_y;
    g->cursor_visible = visible ? 1 : 0;
}

#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
static int vbe_set_bank(OykDosGfx* g, oyk_u16 bank) {
    if (g->cur_bank == bank) return 1;
    union REGS r;
    memset(&r, 0, sizeof(r));
    r.x.ax = 0x4F05U;
    r.x.bx = 0x0000U;
    r.x.dx = bank;
    int86(0x10, &r, &r);
    if (r.x.ax != 0x004FU) return 0;
    g->cur_bank = bank;
    return 1;
}

static void vbe_blit_scanline(OykDosGfx* g, int y, int x_off_bytes, const oyk_u8* src, oyk_u16 nbytes) {
    oyk_u32 byte_pos = static_cast<oyk_u32>(y) * static_cast<oyk_u32>(g->bytes_per_scanline) +
                       static_cast<oyk_u32>(x_off_bytes);
    oyk_u32 gran_bytes = static_cast<oyk_u32>(g->win_granularity_kb ? g->win_granularity_kb : 64U) * 1024UL;
    oyk_u16 rem = nbytes;
    const oyk_u8* sp = src;
    while (rem > 0U) {
        oyk_u16 bank = static_cast<oyk_u16>(byte_pos / gran_bytes);
        oyk_u32 in_bank = byte_pos % gran_bytes;
        /* Keep offsets within 64KB segment offset from start of bank */
        vbe_set_bank(g, bank);
        oyk_u32 seg_avail = 65536UL - in_bank;
        oyk_u16 chunk = (static_cast<oyk_u32>(rem) < seg_avail) ? rem : static_cast<oyk_u16>(seg_avail);
        oyk_u8 far* dst = static_cast<oyk_u8 far*>(MK_FP(g->win_seg, static_cast<oyk_u16>(in_bank)));
        for (oyk_u16 i = 0U; i < chunk; ++i) {
            dst[i] = sp[i];
        }
        sp += chunk;
        byte_pos += chunk;
        rem = static_cast<oyk_u16>(rem - chunk);
    }
}

static int try_init_vbe_mode(OykDosGfx* g, oyk_u16 vbe_mode, int expected_mode) {
    static oyk_u8 vbe_info[512];
    static oyk_u8 mode_info[256];
    union REGS r;
    struct SREGS sr;

    memset(vbe_info, 0, sizeof(vbe_info));
    memcpy(vbe_info, "VBE2", 4);
    memset(&r, 0, sizeof(r));
    memset(&sr, 0, sizeof(sr));
    r.x.ax = 0x4F00U;
    sr.es = FP_SEG(vbe_info);
    r.x.di = FP_OFF(vbe_info);
    int86x(0x10, &r, &r, &sr);
    if (r.x.ax != 0x004FU || memcmp(vbe_info, "VESA", 4) != 0) return 0;

    memset(mode_info, 0, sizeof(mode_info));
    memset(&r, 0, sizeof(r));
    memset(&sr, 0, sizeof(sr));
    r.x.ax = 0x4F01U;
    r.x.cx = vbe_mode;
    sr.es = FP_SEG(mode_info);
    r.x.di = FP_OFF(mode_info);
    int86x(0x10, &r, &r, &sr);
    if (r.x.ax != 0x004FU) return 0;

    oyk_u16 attr = static_cast<oyk_u16>(mode_info[0]) | (static_cast<oyk_u16>(mode_info[1]) << 8);
    if ((attr & 0x0001U) == 0U || (attr & 0x0010U) == 0U || (attr & 0x0040U) != 0U) {
        /* Requires supported graphics mode with 64KB real-mode banked VGA window */
        return 0;
    }
    oyk_u16 win_gran = static_cast<oyk_u16>(mode_info[4]) | (static_cast<oyk_u16>(mode_info[5]) << 8);
    oyk_u16 win_seg = static_cast<oyk_u16>(mode_info[8]) | (static_cast<oyk_u16>(mode_info[9]) << 8);
    oyk_u16 bps = static_cast<oyk_u16>(mode_info[16]) | (static_cast<oyk_u16>(mode_info[17]) << 8);
    oyk_u16 mw = static_cast<oyk_u16>(mode_info[18]) | (static_cast<oyk_u16>(mode_info[19]) << 8);
    oyk_u16 mh = static_cast<oyk_u16>(mode_info[20]) | (static_cast<oyk_u16>(mode_info[21]) << 8);
    oyk_u8 bpp = mode_info[25];
    if (mw < 640U || mh < 480U || win_seg == 0U) return 0;

    memset(&r, 0, sizeof(r));
    r.x.ax = 0x4F02U;
    r.x.bx = static_cast<oyk_u16>(vbe_mode & 0x0FFFU);
    int86(0x10, &r, &r);
    if (r.x.ax != 0x004FU) return 0;

    g->mode = expected_mode;
    g->width = 640;
    g->height = 480;
    g->char_w = 8;
    g->char_h = 16;
    g->bpp = static_cast<int>(bpp);
    g->win_seg = static_cast<int>(win_seg);
    g->bytes_per_scanline = bps ? bps : static_cast<oyk_u16>(640U * ((bpp + 7U) / 8U));
    g->win_granularity_kb = win_gran ? win_gran : 64U;
    g->cur_bank = 0xFFFFU;
    return 1;
}

static int has_vga_adapter() {
    union REGS r;
    memset(&r, 0, sizeof(r));
    r.x.ax = 0x1A00U;
    int86(0x10, &r, &r);
    return (r.h.al == 0x1AU && r.h.bl >= 0x07U) ? 1 : 0;
}

static int has_ega_adapter() {
    union REGS r;
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x12U;
    r.h.bl = 0x10U;
    int86(0x10, &r, &r);
    return (r.h.bl != 0x10U) ? 1 : 0;
}
#endif

static void gfx_reset_state(OykDosGfx* g) {
    g->active = 0;
    g->sim_mode = 0;
    g->mode = OYK_GFX_MODE_NONE;
    g->width = 0;
    g->height = 0;
    g->char_w = 8;
    g->char_h = 16;
    g->bpp = 0;
    g->win_seg = 0;
    g->bytes_per_scanline = 0;
    g->win_granularity_kb = 0;
    g->cur_bank = 0xFFFFU;
    g->orig_bios_mode = 0;
    g->has_custom_theme = 0;
    g->shadow_count = 0;
    g->cursor_x = 0;
    g->cursor_y = 0;
    g->cursor_visible = 0;
    g->prev_cursor_x = 0;
    g->prev_cursor_y = 0;
    g->prev_cursor_visible = 0;
    g->sim_rgb = NULL;
    memcpy(g->palette16, k_vga16_default_rgb, sizeof(g->palette16));
    memset(g->prev_cells, 0xFF, sizeof(g->prev_cells));
    memset(g->cell_has_rgb, 0, sizeof(g->cell_has_rgb));
    memset(g->prev_shadow_mask, 0, sizeof(g->prev_shadow_mask));
    memset(g->shadow_mask, 0, sizeof(g->shadow_mask));
    memset(&g->font, 0, sizeof(g->font));
    g->extmem.shutdown();
}

int dos_gfx_init(OykDosGfx* g, int preferred_mode) {
    if (!g) return 0;
    gfx_reset_state(g);
    oyk_dos_font_init(&g->font);

#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
    union REGS r;
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x0FU;
    int86(0x10, &r, &r);
    g->orig_bios_mode = static_cast<int>(r.h.al & 0x7FU);

    int max_lvl = (preferred_mode > 0) ? preferred_mode : OYK_GFX_MODE_VBE24;
    if (max_lvl <= OYK_GFX_MODE_VBE24) {
        if (try_init_vbe_mode(g, 0x112U, OYK_GFX_MODE_VBE24)) goto mode_ok;
    }
    if (max_lvl <= OYK_GFX_MODE_VBE16) {
        if (try_init_vbe_mode(g, 0x111U, OYK_GFX_MODE_VBE16)) goto mode_ok;
        if (try_init_vbe_mode(g, 0x110U, OYK_GFX_MODE_VBE16)) goto mode_ok;
    }
    if (max_lvl <= OYK_GFX_MODE_VBE8) {
        if (try_init_vbe_mode(g, 0x101U, OYK_GFX_MODE_VBE8)) goto mode_ok;
    }
    if (max_lvl <= OYK_GFX_MODE_VGA16 && has_vga_adapter()) {
        memset(&r, 0, sizeof(r));
        r.x.ax = 0x0012U;
        int86(0x10, &r, &r);
        g->mode = OYK_GFX_MODE_VGA16;
        g->width = 640;
        g->height = 480;
        g->char_w = 8;
        g->char_h = 16;
        g->bpp = 4;
        g->win_seg = 0xA000;
        g->bytes_per_scanline = 80;
        goto mode_ok;
    }
    if (has_ega_adapter() || has_vga_adapter()) {
        memset(&r, 0, sizeof(r));
        r.x.ax = 0x0010U;
        int86(0x10, &r, &r);
        g->mode = OYK_GFX_MODE_EGA16;
        g->width = 640;
        g->height = 350;
        g->char_w = 8;
        g->char_h = 14;
        g->bpp = 4;
        g->win_seg = 0xA000;
        g->bytes_per_scanline = 80;
        goto mode_ok;
    }
    oyk_dos_font_shutdown(&g->font);
    return 0;

mode_ok:
    g->active = 1;
    g->sim_mode = 0;
    /* Reserve 64KB in XMS/EMS for smooth-scroll offscreen scanlines & double buffering */
    g->extmem.init(65536UL);
    program_hardware_palette(g);
    return 1;
#else
    return dos_gfx_init_sim(g, preferred_mode > 0 ? preferred_mode : OYK_GFX_MODE_VBE24);
#endif
}

int dos_gfx_init_sim(OykDosGfx* g, int mode) {
    if (!g) return 0;
    gfx_reset_state(g);
    oyk_dos_font_init(&g->font);
    if (mode <= 0) mode = OYK_GFX_MODE_VBE24;
    g->mode = mode;
    g->sim_mode = 1;
    g->active = 1;
    g->width = 640;
    g->height = (mode == OYK_GFX_MODE_EGA16) ? 350 : 480;
    g->char_w = 8;
    g->char_h = (mode == OYK_GFX_MODE_EGA16) ? 14 : 16;
    g->bpp = (mode == OYK_GFX_MODE_VBE24) ? 24 : (mode == OYK_GFX_MODE_VBE16 ? 16 : (mode == OYK_GFX_MODE_VBE8 ? 8 : 4));
    g->bytes_per_scanline = static_cast<oyk_u16>(g->width * 3);
    g->sim_rgb = static_cast<oyk_u8*>(malloc(static_cast<size_t>(g->width) * static_cast<size_t>(g->height) * 3U));
    if (!g->sim_rgb) {
        oyk_dos_font_shutdown(&g->font);
        g->active = 0;
        return 0;
    }
    memset(g->sim_rgb, 0, static_cast<size_t>(g->width) * static_cast<size_t>(g->height) * 3U);
    g->extmem.init(65536UL);
    return 1;
}

void dos_gfx_shutdown(OykDosGfx* g) {
    if (!g) return;
#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
    if (g->active && !g->sim_mode) {
        union REGS r;
        memset(&r, 0, sizeof(r));
        r.h.ah = 0x00U;
        r.h.al = static_cast<oyk_u8>(g->orig_bios_mode ? g->orig_bios_mode : 0x03U);
        int86(0x10, &r, &r);
    }
#endif
    if (g->sim_rgb) {
        free(g->sim_rgb);
        g->sim_rgb = NULL;
    }
    g->extmem.shutdown();
    oyk_dos_font_shutdown(&g->font);
    g->active = 0;
}

static void resolve_cell_colors(const OykDosGfx* g, int idx, oyk_u16 cell,
                                oyk_u32* out_fg, oyk_u32* out_bg,
                                oyk_u8* out_fg_idx, oyk_u8* out_bg_idx) {
    oyk_u8 attr = static_cast<oyk_u8>((cell >> 8) & 0xFFU);
    oyk_u8 fgi = static_cast<oyk_u8>(attr & 0x0FU);
    oyk_u8 bgi = static_cast<oyk_u8>((attr >> 4) & 0x0FU);
    oyk_u32 fg = g->palette16[fgi];
    oyk_u32 bg = g->palette16[bgi];
    if (g->cell_has_rgb[idx]) {
        fg = g->cell_fg_rgb[idx];
        bg = g->cell_bg_rgb[idx];
        fgi = nearest_pal16(g->palette16, fg);
        bgi = nearest_pal16(g->palette16, bg);
    }
    if (g->shadow_mask[idx]) {
        fg = scale_rgb_half(fg);
        bg = scale_rgb_half(bg);
    }
    if (out_fg) *out_fg = fg;
    if (out_bg) *out_bg = bg;
    if (out_fg_idx) *out_fg_idx = g->shadow_mask[idx] ? static_cast<oyk_u8>(fgi ? 8U : 0U) : fgi;
    if (out_bg_idx) *out_bg_idx = static_cast<oyk_u8>(g->shadow_mask[idx] ? 0U : bgi);
}

static void render_cell_span_to_sim(OykDosGfx* g, int row, int col, oyk_u16 cell, int idx, int y_off_px, int max_y_clip) {
    oyk_u8 glyph[16];
    oyk_u8 ch = static_cast<oyk_u8>(cell & 0xFFU);
    oyk_dos_font_get_glyph(&g->font, g->char_h, ch, glyph);

    oyk_u32 fg, bg;
    oyk_u8 fgi, bgi;
    resolve_cell_colors(g, idx, cell, &fg, &bg, &fgi, &bgi);

    if (g->mode == OYK_GFX_MODE_VGA16 || g->mode == OYK_GFX_MODE_EGA16) {
        fg = g->palette16[fgi];
        bg = g->palette16[bgi];
    }

    oyk_u8 fg_r = static_cast<oyk_u8>((fg >> 16) & 0xFFU);
    oyk_u8 fg_g = static_cast<oyk_u8>((fg >> 8) & 0xFFU);
    oyk_u8 fg_b = static_cast<oyk_u8>(fg & 0xFFU);
    oyk_u8 bg_r = static_cast<oyk_u8>((bg >> 16) & 0xFFU);
    oyk_u8 bg_g = static_cast<oyk_u8>((bg >> 8) & 0xFFU);
    oyk_u8 bg_b = static_cast<oyk_u8>(bg & 0xFFU);

    int base_x = col * 8;
    int base_y = row * g->char_h + y_off_px;
    for (int gy = 0; gy < g->char_h; ++gy) {
        int py = base_y + gy;
        if (py < 0 || py >= max_y_clip || py >= g->height) continue;
        oyk_u8 bits = glyph[gy];
        oyk_u8* dst = g->sim_rgb + (static_cast<size_t>(py) * static_cast<size_t>(g->width) + static_cast<size_t>(base_x)) * 3U;
        for (int bx = 0; bx < 8; ++bx) {
            int on = (bits & (0x80U >> bx)) != 0U;
            dst[bx * 3 + 0] = on ? fg_r : bg_r;
            dst[bx * 3 + 1] = on ? fg_g : bg_g;
            dst[bx * 3 + 2] = on ? fg_b : bg_b;
        }
    }
}

static void overlay_cursor_sim(OykDosGfx* g) {
    if (!g->cursor_visible || !g->sim_rgb) return;
    for (int ry = 0; ry < 14; ++ry) {
        int py = g->cursor_y + ry;
        if (py < 0 || py >= g->height) continue;
        oyk_u16 om = k_arrow_outline[ry];
        oyk_u16 fm = k_arrow_fill[ry];
        for (int rx = 0; rx < 10; ++rx) {
            int px = g->cursor_x + rx;
            if (px < 0 || px >= g->width) continue;
            oyk_u16 bit = static_cast<oyk_u16>(0x8000U >> rx);
            if (om & bit) {
                oyk_u8 val = (fm & bit) ? 255U : 0U;
                oyk_u8* p = g->sim_rgb + (static_cast<size_t>(py) * static_cast<size_t>(g->width) + static_cast<size_t>(px)) * 3U;
                p[0] = val; p[1] = val; p[2] = val;
            }
        }
    }
}

#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
static void encode_scan_row(const OykDosGfx* g, const oyk_u16* vram, int row, int gy,
                            int start_col, int ncols) {
    int bpp = g->bpp;
    oyk_u8* dst = const_cast<oyk_u8*>(g->scan_buf);
    for (int i = 0; i < ncols; ++i) {
        int c = start_col + i;
        int idx = row * OYK_DOS_VID_COLS + c;
        oyk_u16 cell = vram[idx];
        oyk_u8 glyph[16];
        oyk_dos_font_get_glyph(&g->font, g->char_h, static_cast<oyk_u8>(cell & 0xFFU), glyph);
        oyk_u8 bits = glyph[gy];

        oyk_u32 fg, bg;
        oyk_u8 fgi, bgi;
        resolve_cell_colors(g, idx, cell, &fg, &bg, &fgi, &bgi);

        if (bpp == 24 || bpp == 32) {
            int step = (bpp == 32) ? 4 : 3;
            for (int bx = 0; bx < 8; ++bx) {
                oyk_u32 colv = (bits & (0x80U >> bx)) ? fg : bg;
                dst[0] = static_cast<oyk_u8>(colv & 0xFFU);
                dst[1] = static_cast<oyk_u8>((colv >> 8) & 0xFFU);
                dst[2] = static_cast<oyk_u8>((colv >> 16) & 0xFFU);
                if (step == 4) dst[3] = 0U;
                dst += step;
            }
        } else if (bpp == 16 || bpp == 15) {
            oyk_u16 fg16, bg16;
            if (bpp == 16) {
                fg16 = static_cast<oyk_u16>((((fg >> 19) & 0x1FU) << 11) | (((fg >> 10) & 0x3FU) << 5) | ((fg >> 3) & 0x1FU));
                bg16 = static_cast<oyk_u16>((((bg >> 19) & 0x1FU) << 11) | (((bg >> 10) & 0x3FU) << 5) | ((bg >> 3) & 0x1FU));
            } else {
                fg16 = static_cast<oyk_u16>((((fg >> 19) & 0x1FU) << 10) | (((fg >> 11) & 0x1FU) << 5) | ((fg >> 3) & 0x1FU));
                bg16 = static_cast<oyk_u16>((((bg >> 19) & 0x1FU) << 10) | (((bg >> 11) & 0x1FU) << 5) | ((bg >> 3) & 0x1FU));
            }
            for (int bx = 0; bx < 8; ++bx) {
                oyk_u16 p16 = (bits & (0x80U >> bx)) ? fg16 : bg16;
                dst[0] = static_cast<oyk_u8>(p16 & 0xFFU);
                dst[1] = static_cast<oyk_u8>((p16 >> 8) & 0xFFU);
                dst += 2;
            }
        } else {
            oyk_u8 fg8 = g->cell_has_rgb[idx] ? rgb_to_vbe8_index(g, g->cell_fg_rgb[idx], g->shadow_mask[idx])
                                              : static_cast<oyk_u8>(g->shadow_mask[idx] ? (16 + (cell >> 8 & 0x0F)) : (cell >> 8 & 0x0F));
            oyk_u8 bg8 = g->cell_has_rgb[idx] ? rgb_to_vbe8_index(g, g->cell_bg_rgb[idx], g->shadow_mask[idx])
                                              : static_cast<oyk_u8>(g->shadow_mask[idx] ? (16 + ((cell >> 12) & 0x0F)) : ((cell >> 12) & 0x0F));
            for (int bx = 0; bx < 8; ++bx) {
                *dst++ = (bits & (0x80U >> bx)) ? fg8 : bg8;
            }
        }
    }
}

static void overlay_cursor_on_scanline(const OykDosGfx* g, int y, int start_col, int ncols) {
    if (!g->cursor_visible) return;
    int ry = y - g->cursor_y;
    if (ry < 0 || ry >= 14) return;
    oyk_u16 om = k_arrow_outline[ry];
    oyk_u16 fm = k_arrow_fill[ry];
    int bpp = g->bpp;
    int step = (bpp == 32) ? 4 : (bpp == 24 ? 3 : ((bpp == 16 || bpp == 15) ? 2 : 1));
    int start_px = start_col * 8;
    int end_px = start_px + ncols * 8;
    for (int rx = 0; rx < 10; ++rx) {
        int px = g->cursor_x + rx;
        if (px < start_px || px >= end_px) continue;
        oyk_u16 bit = static_cast<oyk_u16>(0x8000U >> rx);
        if ((om & bit) == 0U) continue;
        int white = (fm & bit) != 0U;
        oyk_u8* p = const_cast<oyk_u8*>(g->scan_buf) + (px - start_px) * step;
        if (bpp == 24 || bpp == 32) {
            oyk_u8 v = white ? 255U : 0U;
            p[0] = v; p[1] = v; p[2] = v;
        } else if (bpp == 16) {
            oyk_u16 v = white ? 0xFFFFU : 0x0000U;
            p[0] = static_cast<oyk_u8>(v & 0xFFU);
            p[1] = static_cast<oyk_u8>(v >> 8);
        } else if (bpp == 15) {
            oyk_u16 v = white ? 0x7FFFU : 0x0000U;
            p[0] = static_cast<oyk_u8>(v & 0xFFU);
            p[1] = static_cast<oyk_u8>(v >> 8);
        } else {
            p[0] = white ? 15U : 0U;
        }
    }
}

static void planar_blit_row(OykDosGfx* g, const oyk_u16* vram, int row, const oyk_u8* col_dirty, int y_off_px) {
    /* Fast planar Mode 12h / 10h blit: 1 byte = 8 horizontal pixels per plane */
    oyk_u8 far* a000 = static_cast<oyk_u8 far*>(MK_FP(0xA000, 0));
    outpw(0x3CE, 0x0001U); /* Enable Set/Reset = 0 */
    outpw(0x3CE, 0x0003U); /* Data Rotate = 0 */
    outpw(0x3CE, 0x0005U); /* Write Mode 0 */
    outpw(0x3CE, 0xFF08U); /* Bit Mask = 0xFF */

    for (int col = 0; col < OYK_DOS_VID_COLS; ++col) {
        if (!col_dirty[col]) continue;
        int idx = row * OYK_DOS_VID_COLS + col;
        oyk_u16 cell = vram[idx];
        oyk_u8 glyph[16];
        oyk_dos_font_get_glyph(&g->font, g->char_h, static_cast<oyk_u8>(cell & 0xFFU), glyph);
        oyk_u8 fgi, bgi;
        resolve_cell_colors(g, idx, cell, NULL, NULL, &fgi, &bgi);

        for (int plane = 0; plane < 4; ++plane) {
            outpw(0x3C4, static_cast<oyk_u16>(0x0002U | ((1U << (plane + 8)))));
            oyk_u8 fb = static_cast<oyk_u8>((fgi >> plane) & 1U);
            oyk_u8 bb = static_cast<oyk_u8>((bgi >> plane) & 1U);
            for (int gy = 0; gy < g->char_h; ++gy) {
                int py = row * g->char_h + gy + y_off_px;
                if (py < 0 || py >= g->height) continue;
                oyk_u8 pat = static_cast<oyk_u8>((fb == bb) ? (fb ? 0xFFU : 0x00U)
                                                              : (fb ? glyph[gy] : static_cast<oyk_u8>(~glyph[gy])));
                a000[py * 80 + col] = pat;
            }
        }
    }
    outpw(0x3C4, 0x0F02U);
}

static void dos_wait_vretrace() {
    /* Wait briefly for VGA/EGA vertical retrace (port 0x3DA bit 3) without hanging */
    oyk_u32 guard = 0;
    while ((inp(0x3DA) & 0x08U) != 0U && ++guard < 60000UL) {}
    guard = 0;
    while ((inp(0x3DA) & 0x08U) == 0U && ++guard < 60000UL) {}
}
#endif

void dos_gfx_present(OykDosGfx* g, const oyk_u16* vram, int force_all) {
    if (!g || !g->active || !vram) return;

    int cur_moved = (g->cursor_visible != g->prev_cursor_visible ||
                     g->cursor_x != g->prev_cursor_x ||
                     g->cursor_y != g->prev_cursor_y);

    if (g->sim_mode) {
        for (int row = 0; row < OYK_DOS_VID_ROWS; ++row) {
            for (int col = 0; col < OYK_DOS_VID_COLS; ++col) {
                int idx = row * OYK_DOS_VID_COLS + col;
                int dirty = force_all || cur_moved ||
                            (vram[idx] != g->prev_cells[idx]) ||
                            (g->shadow_mask[idx] != g->prev_shadow_mask[idx]);
                if (dirty) {
                    render_cell_span_to_sim(g, row, col, vram[idx], idx, 0, g->height);
                }
            }
        }
        overlay_cursor_sim(g);
    } else {
#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
        int step_px = (g->bpp == 32) ? 4 : (g->bpp == 24 ? 3 : ((g->bpp == 16 || g->bpp == 15) ? 2 : 1));
        for (int row = 0; row < OYK_DOS_VID_ROWS; ++row) {
            oyk_u8 row_dirty_cols[OYK_DOS_VID_COLS];
            int any_dirty = 0;
            int min_c = OYK_DOS_VID_COLS;
            int max_c = -1;
            int row_y0 = row * g->char_h;
            int row_y1 = row_y0 + g->char_h - 1;
            int touches_old_cur = g->prev_cursor_visible && !(row_y1 < g->prev_cursor_y || row_y0 >= g->prev_cursor_y + 14);
            int touches_new_cur = g->cursor_visible && !(row_y1 < g->cursor_y || row_y0 >= g->cursor_y + 14);

            for (int col = 0; col < OYK_DOS_VID_COLS; ++col) {
                int idx = row * OYK_DOS_VID_COLS + col;
                int d = force_all ||
                        (vram[idx] != g->prev_cells[idx]) ||
                        (g->shadow_mask[idx] != g->prev_shadow_mask[idx]);
                if (!d && cur_moved && (touches_old_cur || touches_new_cur)) {
                    int px0 = col * 8, px1 = px0 + 7;
                    if ((touches_old_cur && !(px1 < g->prev_cursor_x || px0 >= g->prev_cursor_x + 10)) ||
                        (touches_new_cur && !(px1 < g->cursor_x || px0 >= g->cursor_x + 10))) {
                        d = 1;
                    }
                }
                row_dirty_cols[col] = static_cast<oyk_u8>(d ? 1U : 0U);
                if (d) {
                    any_dirty = 1;
                    if (col < min_c) min_c = col;
                    if (col > max_c) max_c = col;
                }
            }
            if (!any_dirty) continue;

            if (g->mode == OYK_GFX_MODE_VGA16 || g->mode == OYK_GFX_MODE_EGA16) {
                planar_blit_row(g, vram, row, row_dirty_cols, 0);
            } else {
                int ncols = max_c - min_c + 1;
                oyk_u16 span_bytes = static_cast<oyk_u16>(ncols * 8 * step_px);
                int x_off = min_c * 8 * step_px;
                for (int gy = 0; gy < g->char_h; ++gy) {
                    int py = row * g->char_h + gy;
                    encode_scan_row(g, vram, row, gy, min_c, ncols);
                    overlay_cursor_on_scanline(g, py, min_c, ncols);
                    vbe_blit_scanline(g, py, x_off, g->scan_buf, span_bytes);
                }
            }
        }
#endif
    }

    memcpy(g->prev_cells, vram, sizeof(g->prev_cells));
    memcpy(g->prev_shadow_mask, g->shadow_mask, sizeof(g->prev_shadow_mask));
    g->prev_cursor_x = g->cursor_x;
    g->prev_cursor_y = g->cursor_y;
    g->prev_cursor_visible = g->cursor_visible;
}

int dos_gfx_smooth_scroll(OykDosGfx* g,
                          const oyk_u16* prev_vram,
                          const oyk_u16* new_vram,
                          int top_row, int left_col, int rows, int cols,
                          int scroll_up) {
    if (!g || !g->active || !new_vram || rows <= 1 || cols <= 0) return 0;
    if (!prev_vram) {
        dos_gfx_present(g, new_vram, 1);
        return 0;
    }

    int step_px = (g->char_h >= 16) ? 4 : 3;
    int steps = 0;
    int max_px = g->char_h;

    for (int shift = step_px; shift < max_px; shift += step_px) {
#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
        if (!g->sim_mode && kbhit()) {
            /* Never queue lag ahead of keyboard typing: snap immediately */
            break;
        }
        if (!g->sim_mode) dos_wait_vretrace();
#endif
        int dy = scroll_up ? -shift : shift;
        int min_y = top_row * g->char_h;
        int max_y = (top_row + rows) * g->char_h;
        if (g->sim_mode) {
            for (int r = top_row; r < top_row + rows; ++r) {
                for (int c = left_col; c < left_col + cols; ++c) {
                    int idx = r * OYK_DOS_VID_COLS + c;
                    render_cell_span_to_sim(g, r, c, prev_vram[idx], idx, dy, max_y);
                    (void)min_y;
                }
            }
            if (scroll_up) {
                for (int c = left_col; c < left_col + cols; ++c) {
                    int idx = (top_row + rows - 1) * OYK_DOS_VID_COLS + c;
                    render_cell_span_to_sim(g, top_row + rows, c, new_vram[idx], idx, dy, max_y);
                }
            } else {
                for (int c = left_col; c < left_col + cols; ++c) {
                    int idx = top_row * OYK_DOS_VID_COLS + c;
                    render_cell_span_to_sim(g, top_row - 1, c, new_vram[idx], idx, dy, max_y);
                }
            }
        } else {
#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
            if (g->mode == OYK_GFX_MODE_VBE24 || g->mode == OYK_GFX_MODE_VBE16 || g->mode == OYK_GFX_MODE_VBE8) {
                int bpx = (g->bpp == 32) ? 4 : (g->bpp == 24 ? 3 : ((g->bpp == 16 || g->bpp == 15) ? 2 : 1));
                oyk_u16 span_bytes = static_cast<oyk_u16>(cols * 8 * bpx);
                int x_off = left_col * 8 * bpx;
                for (int y = min_y; y < max_y; ++y) {
                    int src_y = y + (scroll_up ? shift : -shift);
                    const oyk_u16* src_vram = prev_vram;
                    if (src_y < min_y) {
                        src_vram = new_vram;
                        src_y += max_y - min_y; /* Pull entering top row */
                    } else if (src_y >= max_y) {
                        src_vram = new_vram;
                        src_y -= max_y - min_y; /* Pull entering bottom row */
                    }
                    int srow = src_y / g->char_h;
                    int sgy = src_y % g->char_h;
                    encode_scan_row(g, src_vram, srow, sgy, left_col, cols);
                    vbe_blit_scanline(g, y, x_off, g->scan_buf, span_bytes);
                }
            } else {
                /* Planar EGA/VGA skips mid-step pixel shift cleanly and presents final row */
                break;
            }
#endif
        }
        ++steps;
    }

    dos_gfx_present(g, new_vram, 1);
    return steps;
}

static void write_le16(oyk_u8* p, oyk_u16 v) {
    p[0] = static_cast<oyk_u8>(v & 0xFFU);
    p[1] = static_cast<oyk_u8>((v >> 8) & 0xFFU);
}

static void write_le32(oyk_u8* p, oyk_u32 v) {
    p[0] = static_cast<oyk_u8>(v & 0xFFUL);
    p[1] = static_cast<oyk_u8>((v >> 8) & 0xFFUL);
    p[2] = static_cast<oyk_u8>((v >> 16) & 0xFFUL);
    p[3] = static_cast<oyk_u8>((v >> 24) & 0xFFUL);
}

int dos_gfx_write_bmp(const OykDosGfx* g, const char* path) {
    if (!g || !g->sim_rgb || !path || !path[0]) return 0;
    FILE* fp = fopen(path, "wb");
    if (!fp) return 0;

    oyk_u32 row_bytes = static_cast<oyk_u32>(g->width) * 3UL;
    oyk_u32 pad_bytes = (4UL - (row_bytes & 3UL)) & 3UL;
    oyk_u32 stride = row_bytes + pad_bytes;
    oyk_u32 img_bytes = stride * static_cast<oyk_u32>(g->height);
    oyk_u32 total_bytes = 54UL + img_bytes;

    oyk_u8 hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    write_le32(hdr + 2, total_bytes);
    write_le32(hdr + 10, 54UL);
    write_le32(hdr + 14, 40UL);
    write_le32(hdr + 18, static_cast<oyk_u32>(g->width));
    write_le32(hdr + 22, static_cast<oyk_u32>(g->height));
    write_le16(hdr + 26, 1U);
    write_le16(hdr + 28, 24U);
    write_le32(hdr + 34, img_bytes);
    fwrite(hdr, 1, sizeof(hdr), fp);

    oyk_u8 row_out[640 * 3 + 4];
    for (int y = g->height - 1; y >= 0; --y) {
        const oyk_u8* src = g->sim_rgb + static_cast<size_t>(y) * static_cast<size_t>(g->width) * 3U;
        for (int x = 0; x < g->width; ++x) {
            row_out[x * 3 + 0] = src[x * 3 + 2]; /* B */
            row_out[x * 3 + 1] = src[x * 3 + 1]; /* G */
            row_out[x * 3 + 2] = src[x * 3 + 0]; /* R */
        }
        for (oyk_u32 p = 0; p < pad_bytes; ++p) row_out[row_bytes + p] = 0U;
        fwrite(row_out, 1, static_cast<size_t>(stride), fp);
    }
    fclose(fp);
    return 1;
}
