/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "amiga_ui.hpp"
#include "amiga_capture.hpp"
#include "oyk_logo_data.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __AMIGA__
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <libraries/gadtools.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
extern struct ExecBase* SysBase;
#endif

static void am_copy(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0U) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1U;
    if (n > 0U) memcpy(dst, src, n);
    dst[n] = '\0';
}

struct AmHitZone {
    int x;
    int y;
    int w;
    int h;
    int action;
};

struct OykAmigaUiState {
    OykPortableNode*   node;
    OykAmigaNetState*  net;
    int                exec_ver;
    int                has_gadtools;
    int                is_private_screen;
    int                locked_to_native;
    int                scr_w;
    int                scr_h;
    int                cli_x;
    int                cli_y;
    int                cli_w;
    int                cli_h;
    int                scr_depth;
    int                scr_colors;
    int                active_screen;
    int                modal_scr;
    int                settings_tab;
    int                sel_iface;
    int                sel_peer;
    int                sel_conn;
    int                sel_ban;
    int                running;
    int                hit_count;
    AmHitZone          hits[32];
    OykConfig          draft_cfg;
    OykGuiAddIfaceWizard wiz;
    OykGuiThemeCtrl    theme_ctrl;
    OykThemeConfig     os_native_pal;
    OykThemeConfig     active_theme;
    int                console_draw_count;
    int                modal_draw_count;
    oyk_u32            last_console_bg;
    oyk_u32            last_modal_bg;
    int                pen_bg;
    int                pen_fg;
    int                pen_shine;
    int                pen_shadow;
    int                pen_panel;
    int                pen_accent;
    int                pen_accent_fg;
    int                pen_status_bg;
    int                pen_status_fg;
    char               modal_title[64];
#ifdef __AMIGA__
    struct Screen*     scr;
    struct Screen*     owned_scr;
    struct Window*     win;
    struct RastPort*   rp;
    struct Window*     modal_win;
    struct RastPort*   modal_rp;
    APTR               vi;
    struct Menu*       menu_strip;
    struct Library*    gt_lib;
#endif
};

#ifdef __AMIGA__
static oyk_u32 am_query_pen_rgb(struct Screen* scr, int pen, oyk_u32 fallback) {
    if (!scr || !scr->ViewPort.ColorMap) return fallback;
    int ks = SysBase ? static_cast<int>(SysBase->LibNode.lib_Version) : 34;
    if (ks >= 39) {
        ULONG rgb32[3];
        GetRGB32(scr->ViewPort.ColorMap, static_cast<ULONG>(pen), 1UL, rgb32);
        oyk_u32 r = static_cast<oyk_u32>((rgb32[0] >> 24) & 0xFFUL);
        oyk_u32 g = static_cast<oyk_u32>((rgb32[1] >> 24) & 0xFFUL);
        oyk_u32 b = static_cast<oyk_u32>((rgb32[2] >> 24) & 0xFFUL);
        return (r << 16) | (g << 8) | b;
    }
    LONG rgb4 = GetRGB4(scr->ViewPort.ColorMap, pen);
    if (rgb4 < 0) return fallback;
    oyk_u32 r = static_cast<oyk_u32>(((rgb4 >> 8) & 0x0F) * 17U);
    oyk_u32 g = static_cast<oyk_u32>(((rgb4 >> 4) & 0x0F) * 17U);
    oyk_u32 b = static_cast<oyk_u32>((rgb4 & 0x0F) * 17U);
    return (r << 16) | (g << 8) | b;
}

static void am_set_pen_rgb(struct Screen* scr, int pen, oyk_u32 rgb) {
    if (!scr) return;
    int ks = SysBase ? static_cast<int>(SysBase->LibNode.lib_Version) : 34;
    unsigned int r8 = static_cast<unsigned int>((rgb >> 16) & 0xFFU);
    unsigned int g8 = static_cast<unsigned int>((rgb >> 8)  & 0xFFU);
    unsigned int b8 = static_cast<unsigned int>(rgb & 0xFFU);
    if (ks >= 39) {
        SetRGB32(&scr->ViewPort,
                 static_cast<ULONG>(pen),
                 static_cast<ULONG>(r8) * 0x01010101UL,
                 static_cast<ULONG>(g8) * 0x01010101UL,
                 static_cast<ULONG>(b8) * 0x01010101UL);
    } else {
        SetRGB4(&scr->ViewPort, pen, r8 >> 4, g8 >> 4, b8 >> 4);
    }
}
#endif

void oyk_amiga_query_os_native_theme(struct Screen* scr, OykThemeConfig* out_pal) {
    if (!out_pal) return;
    oyk_theme_init_builtin(out_pal, 1);
    am_copy(out_pal->name, sizeof(out_pal->name), "native");
    out_pal->is_dark_base = 0;

    oyk_u32 c_bg = 0xAAAAAAUL;     /* WB 2.x/3.x default gray pen 0 */
    oyk_u32 c_fg = 0x000000UL;     /* WB 2.x/3.x default black pen 1 */
    oyk_u32 c_shine = 0xFFFFFFUL;  /* WB 2.x/3.x default shine pen 2 */
    oyk_u32 c_fill = 0x6688BBUL;   /* WB 2.x/3.x default Amiga blue pen 3 */

#ifdef __AMIGA__
    if (scr) {
        int pen_bg = 0, pen_fg = 1, pen_sh = 2, pen_fl = 3, pen_ft = 1;
        int ks = SysBase ? static_cast<int>(SysBase->LibNode.lib_Version) : 34;
        if (ks >= 36) {
            struct DrawInfo* di = GetScreenDrawInfo(scr);
            if (di && di->dri_NumPens > FILLTEXTPEN) {
                pen_bg = di->dri_Pens[BACKGROUNDPEN];
                pen_fg = di->dri_Pens[TEXTPEN];
                pen_sh = di->dri_Pens[SHINEPEN];
                pen_fl = di->dri_Pens[FILLPEN];
                pen_ft = di->dri_Pens[FILLTEXTPEN];
            }
            if (di) FreeScreenDrawInfo(scr, di);
        }
        c_bg    = am_query_pen_rgb(scr, pen_bg, c_bg);
        c_fg    = am_query_pen_rgb(scr, pen_fg, c_fg);
        c_shine = am_query_pen_rgb(scr, pen_sh, c_shine);
        c_fill  = am_query_pen_rgb(scr, pen_fl, c_fill);
        out_pal->colors[OYK_TR_SELECTION_FG] = am_query_pen_rgb(scr, pen_ft, 0xFFFFFFUL);
    }
#endif

    out_pal->colors[OYK_TR_BG]           = c_bg;
    out_pal->colors[OYK_TR_FG]           = c_fg;
    out_pal->colors[OYK_TR_BORDER]       = c_fg;
    out_pal->colors[OYK_TR_HEADER_BG]    = c_fill;
    out_pal->colors[OYK_TR_HEADER_FG]    = c_shine;
    out_pal->colors[OYK_TR_STATUS_BG]    = c_fill;
    out_pal->colors[OYK_TR_STATUS_FG]    = c_shine;
    out_pal->colors[OYK_TR_BUTTON_BG]    = c_shine;
    out_pal->colors[OYK_TR_BUTTON_FG]    = c_fg;
    out_pal->colors[OYK_TR_SELECTION_BG] = c_fill;
    out_pal->colors[OYK_TR_PANEL_BG]     = c_bg;
    out_pal->colors[OYK_TR_PANEL_FG]     = c_fg;
    out_pal->colors[OYK_TR_INPUT_BG]     = c_shine;
    out_pal->colors[OYK_TR_INPUT_FG]     = c_fg;
    out_pal->colors[OYK_TR_ACCENT]       = c_fill;
}

static void am_apply_pens_and_palette(OykAmigaUiState* ui) {
    if (!ui) return;
    OykThemeConfig effective;
    int locked = 1;
    oyk_amiga_resolve_effective_theme(ui->scr_colors, &ui->os_native_pal,
                                      &ui->active_theme, &effective, &locked);
    ui->locked_to_native = locked;
    ui->active_theme = effective;

    if (locked) {
        /* Standard Amiga Intuition Workbench pens (KS 1.3 / 2.04 / 3.1 / AROS): never alter shared palette. */
        ui->pen_bg        = 0;
        ui->pen_fg        = 1;
        ui->pen_shine     = 2;
        ui->pen_shadow    = 1;
        ui->pen_panel     = 0;
        ui->pen_accent    = 3;
        ui->pen_accent_fg = 2;
        ui->pen_status_bg = 3;
        ui->pen_status_fg = 2;
#ifdef __AMIGA__
        if (ui->scr && ui->exec_ver >= 36) {
            struct DrawInfo* di = GetScreenDrawInfo(ui->scr);
            if (di && di->dri_NumPens > FILLTEXTPEN) {
                ui->pen_bg        = di->dri_Pens[BACKGROUNDPEN];
                ui->pen_fg        = di->dri_Pens[TEXTPEN];
                ui->pen_shine     = di->dri_Pens[SHINEPEN];
                ui->pen_shadow    = di->dri_Pens[SHADOWPEN];
                ui->pen_panel     = di->dri_Pens[BACKGROUNDPEN];
                ui->pen_accent    = di->dri_Pens[FILLPEN];
                ui->pen_accent_fg = di->dri_Pens[FILLTEXTPEN];
                ui->pen_status_bg = di->dri_Pens[FILLPEN];
                ui->pen_status_fg = di->dri_Pens[FILLTEXTPEN];
            }
            if (di) FreeScreenDrawInfo(ui->scr, di);
        }
#endif
    } else {
        /* >= 32 colors unlocked: map pens 4..26 to live OYK theme roles cleanly while preserving 0..3 backdrop/menus. */
        ui->pen_bg        = 4;
        ui->pen_fg        = 5;
        ui->pen_panel     = 6;
        ui->pen_accent    = 7;
        ui->pen_accent_fg = 8;
        ui->pen_status_bg = 9;
        ui->pen_status_fg = 10;
        ui->pen_shine     = 2;
        ui->pen_shadow    = 1;
#ifdef __AMIGA__
        if (ui->scr) {
            am_set_pen_rgb(ui->scr, 0, effective.colors[OYK_TR_PANEL_BG]);
            am_set_pen_rgb(ui->scr, 1, effective.colors[OYK_TR_PANEL_FG]);
            am_set_pen_rgb(ui->scr, 2, effective.colors[OYK_TR_SELECTION_FG]);
            am_set_pen_rgb(ui->scr, 3, effective.colors[OYK_TR_ACCENT]);
            am_set_pen_rgb(ui->scr, ui->pen_bg,        effective.colors[OYK_TR_BG]);
            am_set_pen_rgb(ui->scr, ui->pen_fg,        effective.colors[OYK_TR_FG]);
            am_set_pen_rgb(ui->scr, ui->pen_panel,     effective.colors[OYK_TR_PANEL_BG]);
            am_set_pen_rgb(ui->scr, ui->pen_accent,    effective.colors[OYK_TR_ACCENT]);
            am_set_pen_rgb(ui->scr, ui->pen_accent_fg, effective.colors[OYK_TR_SELECTION_FG]);
            am_set_pen_rgb(ui->scr, ui->pen_status_bg, effective.colors[OYK_TR_STATUS_BG]);
            am_set_pen_rgb(ui->scr, ui->pen_status_fg, effective.colors[OYK_TR_STATUS_FG]);
            for (int r = 0; r < OYK_THEME_COLOR_COUNT && (11 + r) < ui->scr_colors; ++r) {
                am_set_pen_rgb(ui->scr, 11 + r, effective.colors[r]);
            }
        }
#endif
    }
}

static void am_fill_rect(const OykAmigaUiState* ui, int x, int y, int w, int h, int pen) {
#ifdef __AMIGA__
    if (!ui || !ui->rp || w <= 0 || h <= 0) return;
    SetAPen(ui->rp, static_cast<ULONG>(pen));
    RectFill(ui->rp, x, y, x + w - 1, y + h - 1);
#else
    (void)ui; (void)x; (void)y; (void)w; (void)h; (void)pen;
#endif
}

static void am_bevel_box(const OykAmigaUiState* ui, int x, int y, int w, int h, int recessed) {
#ifdef __AMIGA__
    if (!ui || !ui->rp || w < 3 || h < 3) return;
    int tl = recessed ? ui->pen_shadow : ui->pen_shine;
    int br = recessed ? ui->pen_shine : ui->pen_shadow;
    SetAPen(ui->rp, static_cast<ULONG>(tl));
    Move(ui->rp, x, y + h - 1);
    Draw(ui->rp, x, y);
    Draw(ui->rp, x + w - 1, y);
    SetAPen(ui->rp, static_cast<ULONG>(br));
    Move(ui->rp, x + w - 1, y + 1);
    Draw(ui->rp, x + w - 1, y + h - 1);
    Draw(ui->rp, x + 1, y + h - 1);
#else
    (void)ui; (void)x; (void)y; (void)w; (void)h; (void)recessed;
#endif
}

static void am_draw_text(const OykAmigaUiState* ui, int x, int y, const char* s,
                         int fg_pen, int bg_pen, int max_chars) {
#ifdef __AMIGA__
    if (!ui || !ui->rp || !s || !s[0] || max_chars <= 0) return;
    char buf[120];
    size_t lim = static_cast<size_t>(max_chars);
    if (lim >= sizeof(buf)) lim = sizeof(buf) - 1U;
    size_t n = strlen(s);
    if (n > lim) {
        if (lim > 3U) {
            memcpy(buf, s, lim - 3U);
            memcpy(buf + lim - 3U, "...", 3);
            buf[lim] = '\0';
        } else {
            memcpy(buf, s, lim);
            buf[lim] = '\0';
        }
    } else {
        memcpy(buf, s, n + 1U);
    }
    SetDrMd(ui->rp, JAM2);
    SetAPen(ui->rp, static_cast<ULONG>(fg_pen));
    SetBPen(ui->rp, static_cast<ULONG>(bg_pen));
    Move(ui->rp, x, y + ui->rp->TxBaseline);
    Text(ui->rp, reinterpret_cast<CONST_STRPTR>(buf), static_cast<ULONG>(strlen(buf)));
#else
    (void)ui; (void)x; (void)y; (void)s; (void)fg_pen; (void)bg_pen; (void)max_chars;
#endif
}

static void am_reset_hits(OykAmigaUiState* ui) {
    if (ui) ui->hit_count = 0;
}

static void am_add_hit(OykAmigaUiState* ui, int x, int y, int w, int h, int action) {
    if (!ui || ui->hit_count >= 32 || w <= 0 || h <= 0) return;
    AmHitZone& z = ui->hits[ui->hit_count++];
    z.x = x;
    z.y = y;
    z.w = w;
    z.h = h;
    z.action = action;
}

static void am_draw_checkbox(const OykAmigaUiState* ui, int x, int y, int checked,
                             const char* label, int label_pen, int max_chars) {
    int sz = oyk_gui_control_box_size(8);
    if (sz < 9) sz = 9;
    am_fill_rect(ui, x, y, sz, sz, ui->pen_bg);
    am_bevel_box(ui, x, y, sz, sz, 1);
#ifdef __AMIGA__
    if (checked && ui && ui->rp) {
        SetAPen(ui->rp, static_cast<ULONG>(ui->pen_accent));
        Move(ui->rp, x + 2, y + sz / 2);
        Draw(ui->rp, x + sz / 2 - 1, y + sz - 3);
        Draw(ui->rp, x + sz - 2, y + 2);
        Move(ui->rp, x + 2, y + sz / 2 + 1);
        Draw(ui->rp, x + sz / 2 - 1, y + sz - 2);
        Draw(ui->rp, x + sz - 2, y + 3);
    }
#else
    (void)checked;
#endif
    if (label && label[0]) {
        am_draw_text(ui, x + sz + 5, y + 1, label, label_pen, ui->pen_panel, max_chars);
    }
}

static void am_draw_radio(const OykAmigaUiState* ui, int x, int y, int selected,
                          const char* label, int label_pen, int max_chars) {
    int sz = oyk_gui_control_box_size(8);
    if (sz < 9) sz = 9;
#ifdef __AMIGA__
    if (ui && ui->rp) {
        int r = sz / 2;
        int cx = x + r;
        int cy = y + r;
        am_fill_rect(ui, x + 1, y + 1, sz - 2, sz - 2, ui->pen_bg);
        SetAPen(ui->rp, static_cast<ULONG>(ui->pen_shadow));
        Move(ui->rp, cx - 2, y);
        Draw(ui->rp, cx + 2, y);
        Draw(ui->rp, x + sz - 1, cy - 2);
        Draw(ui->rp, x + sz - 1, cy + 2);
        SetAPen(ui->rp, static_cast<ULONG>(ui->pen_shine));
        Move(ui->rp, x + sz - 1, cy + 2);
        Draw(ui->rp, cx + 2, y + sz - 1);
        Draw(ui->rp, cx - 2, y + sz - 1);
        Draw(ui->rp, x, cy + 2);
        SetAPen(ui->rp, static_cast<ULONG>(ui->pen_shadow));
        Move(ui->rp, x, cy + 2);
        Draw(ui->rp, x, cy - 2);
        Draw(ui->rp, cx - 2, y);
        if (selected) {
            am_fill_rect(ui, cx - 2, cy - 2, 5, 5, ui->pen_accent);
        }
    }
#else
    (void)selected;
#endif
    if (label && label[0]) {
        am_draw_text(ui, x + sz + 5, y + 1, label, label_pen, ui->pen_panel, max_chars);
    }
}

static void am_draw_field_row(const OykAmigaUiState* ui, int x, int y, const char* label,
                              const char* value, int label_w, int box_w) {
    if (label && label[0]) {
        am_draw_text(ui, x, y + 2, label, ui->pen_fg, ui->pen_panel, label_w / 8);
    }
    int bx = x + label_w;
    am_fill_rect(ui, bx, y, box_w, 12, ui->pen_bg);
    am_bevel_box(ui, bx, y, box_w, 12, 1);
    if (value && value[0]) {
        am_draw_text(ui, bx + 4, y + 2, value, ui->pen_fg, ui->pen_bg, (box_w - 8) / 8);
    }
}

static int am_swatch_pen_for_role(const OykAmigaUiState* ui, int role_idx) {
    if (!ui->locked_to_native) return 11 + role_idx;
    oyk_u32 col = ui->active_theme.colors[role_idx];
    int r = static_cast<int>((col >> 16) & 0xFFU);
    int g = static_cast<int>((col >> 8) & 0xFFU);
    int b = static_cast<int>(col & 0xFFU);
    static const int kNativeRgb[4][3] = {
        { 170, 170, 170 }, /* pen_bg / panel (WB grey) */
        {   0,   0,   0 }, /* pen_fg / shadow (black) */
        { 255, 255, 255 }, /* pen_shine (white) */
        { 102, 136, 187 }  /* pen_accent (WB blue) */
    };
    const int pens[4] = { ui->pen_panel, ui->pen_fg, ui->pen_shine, ui->pen_accent };
    int best_idx = 0;
    long best_dist = 1000000L;
    for (int i = 0; i < 4; ++i) {
        long dr = r - kNativeRgb[i][0];
        long dg = g - kNativeRgb[i][1];
        long db = b - kNativeRgb[i][2];
        long d = dr * dr + dg * dg + db * db;
        if (d < best_dist) {
            best_dist = d;
            best_idx = i;
        }
    }
    return pens[best_idx];
}

static void am_draw_panel(const OykAmigaUiState* ui, int x, int y, int w, int h, const char* title) {
    am_fill_rect(ui, x, y, w, h, ui->pen_panel);
    am_bevel_box(ui, x, y, w, h, 1);
    am_fill_rect(ui, x + 1, y + 1, w - 2, 11, ui->pen_accent);
    if (title && title[0]) {
        am_draw_text(ui, x + 6, y + 2, title, ui->pen_accent_fg, ui->pen_accent, (w - 12) / 8);
    }
}

static void am_draw_button(const OykAmigaUiState* ui, int x, int y, int w, int h,
                           const char* label, int is_primary) {
    int bg = is_primary ? ui->pen_accent : ui->pen_panel;
    int fg = is_primary ? ui->pen_accent_fg : ui->pen_fg;
    am_fill_rect(ui, x, y, w, h, bg);
    am_bevel_box(ui, x, y, w, h, 0);
    if (label && label[0]) {
        am_draw_text(ui, x + 6, y + 3, label, fg, bg, (w - 10) / 8);
    }
}

static void am_draw_listview_header(const OykAmigaUiState* ui, int x, int y, int w,
                                    int ncols, const int* col_x, const char* const* cols,
                                    int bg, int fg) {
    am_fill_rect(ui, x, y, w, 10, bg);
    for (int c = 0; c < ncols; ++c) {
        if (cols[c] && cols[c][0]) {
            am_draw_text(ui, col_x[c], y + 1, cols[c], fg, bg, 24);
        }
    }
}

static void am_draw_listview_row(const OykAmigaUiState* ui, int x, int y, int w, int h,
                                 int selected, int ncols, const int* col_x,
                                 const char* const* cells, const int* max_c,
                                 int norm_bg, int norm_fg) {
    int bg = selected ? ui->pen_accent : norm_bg;
    int fg = selected ? ui->pen_accent_fg : norm_fg;
    am_fill_rect(ui, x, y, w, h, bg);
    for (int c = 0; c < ncols; ++c) {
        if (cells[c] && cells[c][0]) {
            am_draw_text(ui, col_x[c], y + 1, cells[c], fg, bg, max_c[c]);
        }
    }
}

static void am_draw_console(OykAmigaUiState* ui) {
    OykPortableNode* node = ui->node;
    const OykConfig& cfg = node->config();
    int cx = (ui->cli_w > 0) ? ui->cli_x : 0;
    int cy = (ui->cli_h > 0) ? ui->cli_y : 0;
    int cw = (ui->cli_w > 0) ? ui->cli_w : ui->scr_w;
    int ch = (ui->cli_h > 0) ? ui->cli_h : ui->scr_h;
    if (cw < 400) cw = 620;
    if (ch < 160) ch = 224;

    ++ui->console_draw_count;
    ui->last_console_bg = ui->active_theme.colors[OYK_TR_BG];
    am_reset_hits(ui);

    /* Repaint entire window client interior (full-screen background repaint on theme change) */
    am_fill_rect(ui, cx, cy, cw, ch, ui->pen_bg);

    /* Top Status Banner with canonical header + right incoming badge */
    char cs[16], al[16], hdr[128];
    oyk_format_callsign(&cfg.callsign, cs, sizeof(cs));
    oyk_format_callsign(&cfg.alias, al, sizeof(al));
    sprintf(hdr, "OYK PACKET NODE: %s (%s) | Up: 0s | Conns: %d",
            cs, al, node->total_active_connections());
    const char* pill = oyk_gui_format_incoming_pill(node->accept_incoming(), 0);
    int pill_w = static_cast<int>(strlen(pill)) * 8 + 8;
    int bx = cx + 6;
    int bw = cw - 12;
    int by_top = cy + 2;
    am_fill_rect(ui, bx, by_top, bw, 13, ui->pen_status_bg);
    am_bevel_box(ui, bx, by_top, bw, 13, 0);
    am_draw_text(ui, bx + 6, by_top + 2, hdr, ui->pen_status_fg, ui->pen_status_bg,
                 (bw - pill_w - 16) / 8);
    am_fill_rect(ui, bx + bw - 4 - pill_w, by_top + 1, pill_w, 11, ui->pen_panel);
    am_bevel_box(ui, bx + bw - 4 - pill_w, by_top + 1, pill_w, 11, 0);
    am_draw_text(ui, bx + bw - pill_w, by_top + 2, pill, ui->pen_fg, ui->pen_panel,
                 static_cast<int>(strlen(pill)));

    int y0 = by_top + 16;
    int half = (bw - 6) / 2;
    int top_h = (ch >= 320) ? 88 : 50;
    am_draw_panel(ui, bx, y0, half, top_h, "Interfaces");
    {
        int ix[4] = { bx + 6, bx + 72, bx + 164, bx + 208 };
        const char* ih[4] = { "Port", "Type", "State", "RX/TX" };
        int iw[4] = { 8, 11, 5, 10 };
        am_draw_listview_header(ui, bx + 2, y0 + 13, half - 4, 4, ix, ih, ui->pen_panel, ui->pen_fg);
        int max_top_rows = (top_h - 24) / 10;
        for (int i = 0; i < cfg.iface_count && i < max_top_rows; ++i) {
            const OykIfaceRuntime& rt = node->iface(i);
            char rxtx[20];
            sprintf(rxtx, "%lu/%lu",
                    static_cast<unsigned long>(rt.rx_frames),
                    static_cast<unsigned long>(rt.tx_frames));
            const char* cells[4] = { cfg.ifaces[i].id, cfg.ifaces[i].type, rt.running ? "UP" : "DN", rxtx };
            am_draw_listview_row(ui, bx + 2, y0 + 24 + i * 10, half - 4, 9, 0, 4, ix, cells, iw,
                                 ui->pen_panel, ui->pen_fg);
        }
    }

    int rx = bx + half + 6;
    am_draw_panel(ui, rx, y0, half, top_h, "Last 5 New Callsigns Heard");
    {
        int hx[4] = { rx + 6, rx + 28, rx + 110, rx + 168 };
        const char* hh[4] = { "#", "Callsign", "Port", "Heard" };
        int hw[4] = { 3, 10, 7, 10 };
        am_draw_listview_header(ui, rx + 2, y0 + 13, half - 4, 4, hx, hh, ui->pen_panel, ui->pen_fg);
        int max_top_rows = (top_h - 24) / 10;
        for (int i = 0; i < node->new_heard_count() && i < max_top_rows; ++i) {
            const OykNewHeard& he = node->new_heard(i);
            char idx_s[12];
            sprintf(idx_s, "%d", i + 1);
            const char* cells[4] = { idx_s, he.callsign, he.iface_id, he.time_hms };
            am_draw_listview_row(ui, rx + 2, y0 + 24 + i * 10, half - 4, 9, 0, 4, hx, cells, hw,
                                 ui->pen_panel, ui->pen_fg);
        }
    }

    int conn_y = y0 + top_h + 3;
    int ch_box = (ch >= 320) ? 86 : 48;
    am_draw_panel(ui, bx, conn_y, bw, ch_box, "Active Connections");
    {
        int cx_c[6] = { bx + 6, bx + 42, bx + 130, bx + 206, bx + 258, bx + 354 };
        const char* ch_c[6] = { "ID", "Callsign", "Port", "Dir", "State", "Since" };
        int cw_c[6] = { 4, 10, 8, 6, 10, 10 };
        am_draw_listview_header(ui, bx + 2, conn_y + 13, bw - 4, 6, cx_c, ch_c, ui->pen_panel, ui->pen_fg);
        int max_conn_rows = (ch_box - 24) / 10;
        int shown_c = 0;
        for (int i = 0; i < node->max_sessions() && shown_c < max_conn_rows; ++i) {
            const OykSession& s = node->session_slot(i);
            if (s.state == OYK_SESS_FREE) continue;
            char scs[16], id_s[12];
            oyk_format_callsign(&s.remote_call, scs, sizeof(scs));
            sprintf(id_s, "%lu", static_cast<unsigned long>(s.id));
            const char* pname = (s.iface_idx >= 0 && s.iface_idx < cfg.iface_count)
                                    ? cfg.ifaces[s.iface_idx].id : "local";
            const char* cells[6] = {
                id_s, scs, pname,
                s.is_local_term ? "LOCAL" : "IN",
                (s.state == OYK_SESS_CONNECTED) ? "CONNECTED" : "OPENING",
                s.since_hms
            };
            am_draw_listview_row(ui, bx + 2, conn_y + 24 + shown_c * 10, bw - 4, 9, 0,
                                 6, cx_c, cells, cw_c, ui->pen_panel, ui->pen_fg);
            ++shown_c;
        }
    }

    int ly = conn_y + ch_box + 3;
    int bar_y = cy + ch - 18;
    int lh = bar_y - ly - 3;
    if (lh > 28) {
        am_draw_panel(ui, bx, ly, bw, lh, "Node Activity Log");
        int max_r = (lh - 16) / 10;
        int total = node->console_log_count();
        int start = (total > max_r) ? (total - max_r) : 0;
        for (int i = start; i < total; ++i) {
            const char* ln = node->console_log_line(i);
            if (ln) am_draw_text(ui, bx + 6, ly + 14 + (i - start) * 10, ln, ui->pen_fg, ui->pen_panel, (bw - 12) / 8);
        }
    }

    am_draw_button(ui, bx,       bar_y, 78, 15, "Terminal", 1); am_add_hit(ui, bx,       bar_y, 78, 15, OYK_SCR_TERMINAL);
    am_draw_button(ui, bx + 84,  bar_y, 64, 15, "Files",    0); am_add_hit(ui, bx + 84,  bar_y, 64, 15, OYK_SCR_FILES);
    am_draw_button(ui, bx + 154, bar_y, 78, 15, "Settings", 0); am_add_hit(ui, bx + 154, bar_y, 78, 15, OYK_SCR_SETTINGS);
    am_draw_button(ui, bx + 238, bar_y, 64, 15, "Theme",    0); am_add_hit(ui, bx + 238, bar_y, 64, 15, OYK_SCR_THEME);
    am_draw_button(ui, bx + 308, bar_y, 64, 15, "Peers",    0); am_add_hit(ui, bx + 308, bar_y, 64, 15, OYK_SCR_PEERS);
    am_draw_text(ui, bx + 384, bar_y + 3,
                 ui->is_private_screen ? "Screen:PRIVATE" : "Screen:Workbench (Priv:OFF)",
                 ui->pen_fg, ui->pen_bg, (bw - 386) / 8);
}

static void am_sync_modal_window(OykAmigaUiState* ui) {
    if (!ui) return;
    int scr = ui->active_screen;
#ifdef __AMIGA__
    if (scr == OYK_SCR_MAIN || scr == OYK_SCR_MENU) {
        if (ui->modal_win) {
            CloseWindow(ui->modal_win);
            ui->modal_win = NULL;
            ui->modal_rp = NULL;
            ui->modal_scr = OYK_SCR_MAIN;
        }
        return;
    }
    if (ui->modal_win && ui->modal_scr == scr) {
        return;
    }
    if (ui->modal_win) {
        CloseWindow(ui->modal_win);
        ui->modal_win = NULL;
        ui->modal_rp = NULL;
        ui->modal_scr = OYK_SCR_MAIN;
    }
    const char* title = oyk_gui_screen_title(scr);
    if (scr == OYK_SCR_PEERS) title = oyk_gui_screen_title(OYK_SCR_SETTINGS);
    strncpy(ui->modal_title, title ? title : "Dialog", sizeof(ui->modal_title) - 1);
    ui->modal_title[sizeof(ui->modal_title) - 1] = '\0';

    int bar_h = (ui->scr && ui->scr->BarHeight > 0) ? (ui->scr->BarHeight + 2) : 12;
    int mw = (ui->scr_w > 590) ? 580 : (ui->scr_w - 20);
    int mh = (ui->scr_h >= 360) ? 280 : (ui->scr_h - bar_h - 10);
    int mx = (ui->scr_w - mw) / 2;
    int my = bar_h + 2 + (ui->scr_h - bar_h - 4 - mh) / 2;
    if (my < bar_h + 2) my = bar_h + 2;

    struct NewWindow mnw;
    memset(&mnw, 0, sizeof(mnw));
    mnw.LeftEdge = static_cast<WORD>(mx);
    mnw.TopEdge = static_cast<WORD>(my);
    mnw.Width = static_cast<WORD>(mw);
    mnw.Height = static_cast<WORD>(mh);
    mnw.DetailPen = 0;
    mnw.BlockPen = 1;
    mnw.IDCMPFlags = IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY | IDCMP_MOUSEBUTTONS | IDCMP_REFRESHWINDOW;
    mnw.Flags = WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET | WFLG_ACTIVATE | WFLG_SMART_REFRESH;
    mnw.Title = reinterpret_cast<STRPTR>(ui->modal_title);
    mnw.Screen = ui->scr;
    mnw.Type = ui->scr ? (ui->owned_scr ? CUSTOMSCREEN : PUBLICSCREEN) : WBENCHSCREEN;
    if (!ui->scr && ui->exec_ver < 36) mnw.Type = WBENCHSCREEN;

    ui->modal_win = OpenWindow(&mnw);
    if (ui->modal_win) {
        ui->modal_rp = ui->modal_win->RPort;
        ui->modal_scr = scr;
    }
#else
    (void)scr;
#endif
}

static void am_draw_modal(OykAmigaUiState* ui) {
    int scr = ui->active_screen;
    int mx = 36, my = 20;
    int mw = ui->scr_w - 72;
    int mh = ui->scr_h - 44;
    if (mw < 420) mw = 560;
    if (mh < 160) mh = 206;

#ifdef __AMIGA__
    struct RastPort* saved_rp = ui->rp;
    if (scr != OYK_SCR_MENU && ui->modal_win && ui->modal_rp) {
        ui->rp = ui->modal_rp;
        mx = ui->modal_win->BorderLeft;
        my = ui->modal_win->BorderTop;
        mw = ui->modal_win->Width - ui->modal_win->BorderLeft - ui->modal_win->BorderRight;
        mh = ui->modal_win->Height - ui->modal_win->BorderTop - ui->modal_win->BorderBottom;
    }
#endif

    ++ui->modal_draw_count;
    ui->last_modal_bg = ui->active_theme.colors[OYK_TR_BG];

    if (scr == OYK_SCR_MENU) {
        int mmx = (ui->cli_w > 0 ? ui->cli_x : 0) + 8;
        int mmy = (ui->cli_h > 0 ? ui->cli_y : 0) + 2;
        int mmw = 248, mmh = 154;
        int my_pos = mmy + 4;
        am_fill_rect(ui, mmx + 4, mmy + 3, mmw, mmh, ui->pen_shadow);
        am_fill_rect(ui, mmx, mmy, mmw, mmh, ui->pen_panel);
        am_bevel_box(ui, mmx, mmy, mmw, mmh, 0);
        for (int i = 0; i < oyk_gui_node_menu_count(); ++i) {
            const OykGuiMenuItem* mi = oyk_gui_node_menu_item(i);
            if (!mi) continue;
            if (mi->is_separator) {
                am_fill_rect(ui, mmx + 3, my_pos + 2, mmw - 6, 1, ui->pen_shadow);
                my_pos += 6;
            } else {
                int hl = (i == 0);
                char mrow[64];
                if (mi->shortcut_key) sprintf(mrow, "[%c] %s", mi->shortcut_key, mi->label);
                else sprintf(mrow, "    %s", mi->label);
                if (hl) {
                    am_fill_rect(ui, mmx + 2, my_pos, mmw - 4, 10, ui->pen_accent);
                    am_draw_text(ui, mmx + 8, my_pos + 1, mrow, ui->pen_accent_fg, ui->pen_accent, 32);
                } else {
                    am_draw_text(ui, mmx + 8, my_pos + 1, mrow, ui->pen_fg, ui->pen_panel, 32);
                }
                my_pos += 11;
            }
        }
        return;
    }

    am_fill_rect(ui, mx, my, mw, mh, ui->pen_panel);

    int lx = mx + 10;
    int ty = my + 6;
    int bw = mw - 20;
    int by = my + mh - 20;
    const OykConfig& cfg = ui->node->config();
    char cs[16], al[16], buf[128], nbuf[32];

    if (scr == OYK_SCR_SETTINGS || scr == OYK_SCR_PEERS) {
        int active_tab = (scr == OYK_SCR_PEERS) ? 4 : ui->settings_tab;
        int tx = lx;
        int tw = bw / 7;
        for (int t = 0; t < oyk_gui_settings_tab_count(); ++t) {
            int sel = (t == active_tab);
            const char* tt = oyk_gui_settings_tab_title(t);
            if (strcmp(tt, "Interfaces") == 0) tt = "Ifaces";
            else if (strcmp(tt, "Printer") == 0) tt = "Prn";
            sprintf(buf, "%d:%s", t + 1, tt);
            am_fill_rect(ui, tx, ty, tw - 2, 13, sel ? ui->pen_accent : ui->pen_panel);
            am_bevel_box(ui, tx, ty, tw - 2, 13, sel ? 1 : 0);
            am_draw_text(ui, tx + 3, ty + 2, buf,
                         sel ? ui->pen_accent_fg : ui->pen_fg,
                         sel ? ui->pen_accent : ui->pen_panel, (tw - 6) / 8);
            am_add_hit(ui, tx, ty, tw - 2, 13, 100 + t);
            tx += tw;
        }

        if (active_tab == 0) {
            oyk_format_callsign(&cfg.callsign, cs, sizeof(cs));
            oyk_format_callsign(&cfg.alias, al, sizeof(al));
            am_draw_field_row(ui, lx, ty + 20, "Callsign:", cs, 96, 130);
            am_draw_field_row(ui, lx + 250, ty + 20, "Alias:", al, 64, 130);
            am_draw_checkbox(ui, lx, ty + 38, cfg.accept_incoming,
                             "Accept Incoming Connections", ui->pen_fg, 32);
            am_add_hit(ui, lx, ty + 38, 260, 12, 302);
            am_draw_checkbox(ui, lx, ty + 53, cfg.amiga_private_screen,
                             "Own Private Screen (Default: OFF / Workbench)", ui->pen_fg, 48);
            am_add_hit(ui, lx, ty + 53, 400, 12, 301);
            sprintf(buf, "Display: %dx%dx%d (%d clr)  Theme: %s (%s)",
                    ui->scr_w, ui->scr_h, ui->scr_depth, ui->scr_colors,
                    ui->theme_ctrl.active_name,
                    ui->locked_to_native ? "Native Lock" : "Unlocked");
            am_draw_text(ui, lx, ty + 70, buf, ui->pen_fg, ui->pen_panel, bw / 8);
        } else if (active_tab == 1) {
            am_draw_checkbox(ui, lx, ty + 20, cfg.bbs_enabled,
                             "Enable Packet BBS Store & Forward", ui->pen_fg, 36);
            sprintf(nbuf, "-%u", static_cast<unsigned int>(cfg.bbs_ssid));
            am_draw_field_row(ui, lx + 340, ty + 20, "SSID:", nbuf, 52, 60);
            am_draw_checkbox(ui, lx, ty + 35, cfg.bbs_files_enabled,
                             "Enable BBS Shared Files Area", ui->pen_fg, 36);
            am_draw_field_row(ui, lx, ty + 52, "Mail Dir:", cfg.bbs_mail_path, 96, 360);
            am_draw_field_row(ui, lx, ty + 68, "Files Dir:", cfg.bbs_files_path, 96, 360);
        } else if (active_tab == 2) {
            am_draw_checkbox(ui, lx, ty + 20, cfg.chat_enabled,
                             "Enable Real-Time CHAT Conference Bridge", ui->pen_fg, 44);
            sprintf(nbuf, "-%u", static_cast<unsigned int>(cfg.chat_ssid));
            am_draw_field_row(ui, lx, ty + 38, "CHAT SSID:", nbuf, 96, 80);
            am_draw_field_row(ui, lx, ty + 54, "Default Topic:", cfg.chat_topic, 120, 320);
        } else if (active_tab == 3) {
            int ix[4] = { lx + 2, lx + 80, lx + 176, lx + 346 };
            const char* ih[4] = { "ID", "Driver", "Endpoint / Device", "Policy" };
            int iw[4] = { 10, 12, 22, 22 };
            am_draw_listview_header(ui, lx, ty + 18, bw, 4, ix, ih, ui->pen_bg, ui->pen_fg);
            for (int i = 0; i < cfg.iface_count && i < 5; ++i) {
                char pol[32];
                if (cfg.ifaces[i].device[0]) {
                    sprintf(nbuf, "%s @ %lu", cfg.ifaces[i].device,
                            static_cast<unsigned long>(cfg.ifaces[i].baud));
                } else {
                    sprintf(nbuf, "%s:%u", cfg.ifaces[i].host,
                            static_cast<unsigned int>(cfg.ifaces[i].port));
                }
                sprintf(pol, "IN:%s OUT:%s %s",
                        cfg.ifaces[i].block_inbound ? "N" : "Y",
                        cfg.ifaces[i].block_outbound ? "N" : "Y",
                        oyk_gui_format_learned_policy(cfg.ifaces[i].learned_peer_policy));
                const char* cells[4] = { cfg.ifaces[i].id, cfg.ifaces[i].type, nbuf, pol };
                am_draw_listview_row(ui, lx, ty + 30 + i * 11, bw, 10, (i == ui->sel_iface),
                                     4, ix, cells, iw, ui->pen_panel, ui->pen_fg);
            }
            am_draw_button(ui, lx, by, 96, 15, "Add Iface", 0);
            am_add_hit(ui, lx, by, 96, 15, OYK_SCR_ADD_IFACE);
            am_draw_button(ui, lx + 104, by, 96, 15, "Edit Iface", 0);
            am_add_hit(ui, lx + 104, by, 96, 15, OYK_SCR_EDIT_IFACE);
        } else if (active_tab == 4) {
            int px[5] = { lx + 2, lx + 78, lx + 164, lx + 252, lx + 422 };
            const char* ph[5] = { "ID", "Callsign", "Interface", "Endpoint", "Policy" };
            int pw[5] = { 10, 10, 11, 22, 10 };
            am_draw_listview_header(ui, lx, ty + 18, bw, 5, px, ph, ui->pen_bg, ui->pen_fg);
            for (int p = 0; p < cfg.peer_count && p < 6; ++p) {
                char ep[48];
                sprintf(ep, "%s:%u", cfg.peers[p].host, static_cast<unsigned int>(cfg.peers[p].port));
                const char* cells[5] = {
                    cfg.peers[p].id, cfg.peers[p].callsign, cfg.peers[p].interface_id,
                    ep, cfg.peers[p].auto_learned ? "AUTO" : "STATIC"
                };
                am_draw_listview_row(ui, lx, ty + 30 + p * 11, bw, 10, (p == ui->sel_peer),
                                     5, px, cells, pw, ui->pen_panel, ui->pen_fg);
            }
            am_draw_button(ui, lx, by, 86, 15, "Edit Peer", 0);
            am_add_hit(ui, lx, by, 86, 15, OYK_SCR_EDIT_PEER);
        } else if (active_tab == 5) {
            am_draw_checkbox(ui, lx, ty + 20, cfg.rig_enabled,
                             "Enable Rig CAT Transceiver Control", ui->pen_fg, 40);
            am_draw_field_row(ui, lx, ty + 38, "Protocol:",
                              cfg.rig_protocol[0] ? cfg.rig_protocol : "rigctld", 96, 130);
            am_draw_field_row(ui, lx + 246, ty + 38, "Host:", cfg.rig_host, 56, 150);
            sprintf(nbuf, "%u", static_cast<unsigned int>(cfg.rig_port));
            am_draw_field_row(ui, lx, ty + 54, "CAT Port:", nbuf, 96, 90);
            sprintf(nbuf, "%d s", cfg.rig_poll_sec);
            am_draw_field_row(ui, lx + 246, ty + 54, "Poll:", nbuf, 56, 90);
        } else {
            am_draw_checkbox(ui, lx, ty + 20, cfg.printer_enabled,
                             "Enable Line Printer Packet Log", ui->pen_fg, 40);
            am_draw_field_row(ui, lx, ty + 38, "Device:",
                              cfg.printer_device[0] ? cfg.printer_device : "PRT:", 96, 150);
            am_draw_field_row(ui, lx + 266, ty + 38, "Proto:",
                              cfg.printer_protocol[0] ? cfg.printer_protocol : "epson", 64, 110);
            sprintf(nbuf, "%lu", static_cast<unsigned long>(cfg.printer_baud));
            am_draw_field_row(ui, lx, ty + 54, "Baud Rate:", nbuf, 96, 110);
            am_draw_checkbox(ui, lx + 246, ty + 55, cfg.printer_condensed,
                             "Condensed 132-col Print", ui->pen_fg, 24);
        }
        am_draw_button(ui, mx + mw - 164, by, 72, 15, "Cancel", 0);
        am_add_hit(ui, mx + mw - 164, by, 72, 15, 999);
        am_draw_button(ui, mx + mw - 84, by, 72, 15, "Save", 1);
        am_add_hit(ui, mx + mw - 84, by, 72, 15, 999);
    } else if (scr == OYK_SCR_ADD_IFACE) {
        int st = ui->wiz.step;
        sprintf(buf, "Step %d of 4: %s", st + 1, oyk_gui_wiz_step_prompt(&ui->wiz));
        am_draw_text(ui, lx, ty + 2, buf, ui->pen_fg, ui->pen_panel, bw / 8);
        if (st == 0) {
            for (int i = 0; i < 5; ++i) {
                int ry = ty + 18 + i * 14;
                am_draw_radio(ui, lx + 4, ry, i == ui->wiz.selected_driver_idx,
                              oyk_gui_wiz_driver_type(ui->wiz.driver_mask, i), ui->pen_fg, 16);
                am_draw_text(ui, lx + 150, ry,
                             oyk_gui_wiz_driver_desc(ui->wiz.driver_mask, i),
                             ui->pen_fg, ui->pen_panel, 44);
                am_add_hit(ui, lx + 4, ry, 460, 12, 200 + i);
            }
        } else if (st == 1) {
            am_draw_field_row(ui, lx, ty + 20, "Driver:", ui->wiz.draft.type, 108, 120);
            am_draw_field_row(ui, lx, ty + 38, "Interface ID:", ui->wiz.draft.id, 108, 160);
            am_draw_field_row(ui, lx, ty + 56, "Description:",
                              ui->wiz.draft.description[0] ? ui->wiz.draft.description : "Amiga Interface",
                              108, 320);
        } else if (st == 2) {
            if (ui->wiz.draft.device[0] || strcmp(ui->wiz.draft.type, "kiss-serial") == 0) {
                am_draw_field_row(ui, lx, ty + 18, "Device:",
                                  ui->wiz.draft.device[0] ? ui->wiz.draft.device : "serial.device", 104, 150);
                sprintf(nbuf, "%lu", static_cast<unsigned long>(ui->wiz.draft.baud ? ui->wiz.draft.baud : 9600UL));
                am_draw_field_row(ui, lx, ty + 34, "Baud Rate:", nbuf, 104, 100);
            } else {
                am_draw_field_row(ui, lx, ty + 18, "Bind Host:", ui->wiz.draft.host, 104, 150);
                sprintf(nbuf, "%u", static_cast<unsigned int>(ui->wiz.draft.port));
                am_draw_field_row(ui, lx + 272, ty + 18, "Port:", nbuf, 48, 80);
                am_draw_field_row(ui, lx, ty + 34, "Netmask:", ui->wiz.draft.netmask, 104, 150);
            }
            am_draw_checkbox(ui, lx, ty + 52, !ui->wiz.draft.block_inbound, "Allow Inbound", ui->pen_fg, 16);
            am_draw_checkbox(ui, lx + 150, ty + 52, !ui->wiz.draft.block_outbound, "Allow Outbound", ui->pen_fg, 16);
            am_draw_checkbox(ui, lx + 310, ty + 52, ui->wiz.draft.allow_same_interface, "Same-Interface", ui->pen_fg, 16);
            am_draw_field_row(ui, lx, ty + 68, "Learned Policy:",
                              oyk_gui_format_learned_policy(ui->wiz.draft.learned_peer_policy), 120, 110);
        } else {
            char sum[320];
            oyk_gui_wiz_format_summary(&ui->wiz, sum, sizeof(sum), 0);
            int sy = ty + 18;
            const char* p = sum;
            while (*p && sy < by - 12) {
                const char* nl = strchr(p, '\n');
                char ln[88];
                size_t len = nl ? static_cast<size_t>(nl - p) : strlen(p);
                if (len >= sizeof(ln)) len = sizeof(ln) - 1U;
                memcpy(ln, p, len);
                ln[len] = '\0';
                am_draw_text(ui, lx, sy, ln, ui->pen_fg, ui->pen_panel, bw / 8);
                sy += 11;
                p = nl ? (nl + 1) : "";
            }
        }
        am_draw_button(ui, lx, by, 72, 15, "< Back", 0);
        am_add_hit(ui, lx, by, 72, 15, 210);
        am_draw_button(ui, mx + mw - 166, by, 72, 15, "Cancel", 0);
        am_add_hit(ui, mx + mw - 166, by, 72, 15, 999);
        am_draw_button(ui, mx + mw - 86, by, 74, 15, (st >= 3) ? "Finish" : "Next >", 1);
        am_add_hit(ui, mx + mw - 86, by, 74, 15, 211);
    } else if (scr == OYK_SCR_EDIT_IFACE) {
        const OykIfaceConfig& ifc = (ui->sel_iface < cfg.iface_count) ? cfg.ifaces[ui->sel_iface] : cfg.ifaces[0];
        am_draw_field_row(ui, lx, ty + 6, "Interface ID:", ifc.id, 112, 120);
        am_draw_field_row(ui, lx + 250, ty + 6, "Driver:", ifc.type, 64, 120);
        am_draw_field_row(ui, lx, ty + 22, "Bind Host:", ifc.host, 112, 150);
        sprintf(nbuf, "%u", static_cast<unsigned int>(ifc.port));
        am_draw_field_row(ui, lx + 280, ty + 22, "Port:", nbuf, 48, 80);
        am_draw_field_row(ui, lx, ty + 38, "Netmask:", ifc.netmask, 112, 150);
        am_draw_checkbox(ui, lx, ty + 55, !ifc.block_inbound, "Allow Inbound", ui->pen_fg, 16);
        am_draw_checkbox(ui, lx + 150, ty + 55, !ifc.block_outbound, "Allow Outbound", ui->pen_fg, 16);
        am_draw_checkbox(ui, lx + 310, ty + 55, ifc.allow_same_interface, "Same-Interface", ui->pen_fg, 16);
        am_draw_field_row(ui, lx, ty + 72, "Learned Policy:",
                          oyk_gui_format_learned_policy(ifc.learned_peer_policy), 120, 110);
        am_add_hit(ui, lx + 120, ty + 72, 110, 13, 302);
        am_draw_button(ui, mx + mw - 164, by, 72, 15, "Cancel", 0);
        am_add_hit(ui, mx + mw - 164, by, 72, 15, 999);
        am_draw_button(ui, mx + mw - 84, by, 72, 15, "Save", 1);
        am_add_hit(ui, mx + mw - 84, by, 72, 15, 999);
    } else if (scr == OYK_SCR_EDIT_PEER) {
        const OykPeerConfig& pr = cfg.peers[0];
        am_draw_field_row(ui, lx, ty + 8, "Peer ID:", pr.id, 96, 120);
        am_draw_field_row(ui, lx + 236, ty + 8, "Callsign:", pr.callsign, 80, 120);
        am_draw_field_row(ui, lx, ty + 24, "Interface:", pr.interface_id, 96, 120);
        sprintf(buf, "%s:%u", pr.host, static_cast<unsigned int>(pr.port));
        am_draw_field_row(ui, lx + 236, ty + 24, "Endpoint:", buf, 80, 170);
        am_draw_checkbox(ui, lx, ty + 44, pr.allow_inbound, "Allow Inbound", ui->pen_fg, 16);
        am_draw_checkbox(ui, lx + 150, ty + 44, pr.allow_outbound, "Allow Outbound", ui->pen_fg, 16);
        am_draw_checkbox(ui, lx + 310, ty + 44, pr.auto_learned, "Auto-Learned", ui->pen_fg, 16);
        am_draw_button(ui, mx + mw - 164, by, 72, 15, "Cancel", 0);
        am_add_hit(ui, mx + mw - 164, by, 72, 15, 999);
        am_draw_button(ui, mx + mw - 84, by, 72, 15, "Save", 1);
        am_add_hit(ui, mx + mw - 84, by, 72, 15, 999);
    } else if (scr == OYK_SCR_TERMINATE) {
        int tx[4] = { lx + 2, lx + 48, lx + 156, lx + 250 };
        const char* th[4] = {
            oyk_gui_table_column(OYK_TBL_TERMINATE, 0),
            oyk_gui_table_column(OYK_TBL_TERMINATE, 1),
            oyk_gui_table_column(OYK_TBL_TERMINATE, 2),
            oyk_gui_table_column(OYK_TBL_TERMINATE, 3)
        };
        int tw[4] = { 5, 12, 10, 14 };
        am_draw_listview_header(ui, lx, ty + 4, bw, 4, tx, th, ui->pen_bg, ui->pen_fg);
        int row_idx = 0;
        for (int i = 0; i < ui->node->max_sessions() && row_idx < 4; ++i) {
            const OykSession& s = ui->node->session_slot(i);
            if (s.state == OYK_SESS_FREE) continue;
            char id_s[12];
            oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
            sprintf(id_s, "%lu", static_cast<unsigned long>(s.id));
            const char* cells[4] = { id_s, cs, (s.iface_idx == 0) ? "axudp0" : "serial0", "CONNECTED" };
            am_draw_listview_row(ui, lx, ty + 18 + row_idx * 12, bw, 11, (row_idx == ui->sel_conn),
                                 4, tx, cells, tw, ui->pen_panel, ui->pen_fg);
            ++row_idx;
        }
        am_draw_button(ui, mx + mw - 188, by, 72, 15, "Cancel", 0);
        am_add_hit(ui, mx + mw - 188, by, 72, 15, 999);
        am_draw_button(ui, mx + mw - 108, by, 96, 15, "Terminate", 1);
    } else if (scr == OYK_SCR_BAN) {
        int bcol_x[5] = { lx + 2, lx + 80, lx + 204, lx + 290, lx + 344 };
        const char* bcol_h[5] = { "Callsign", "Banned At", "Lift At", "Flags", "Reason" };
        int bcol_w[5] = { 10, 16, 11, 7, 24 };
        am_draw_field_row(ui, lx, ty + 2, "Callsign:", "N0BAD", 80, 100);
        am_draw_field_row(ui, lx + 190, ty + 2, "Reason:", "Beacon flood", 64, 150);
        am_draw_field_row(ui, lx + 414, ty + 2, "Dur:", "7d", 40, 56);
        am_draw_checkbox(ui, lx, ty + 18, 1, "Hard Ban (Drop AX.25)", ui->pen_fg, 24);
        am_draw_checkbox(ui, lx + 230, ty + 18, 0, "Allow Appeal", ui->pen_fg, 16);
        am_draw_listview_header(ui, lx, ty + 35, bw, 5, bcol_x, bcol_h, ui->pen_bg, ui->pen_fg);
        for (int i = 0; i < ui->node->ban_count() && i < 3; ++i) {
            const OykBanEntry& be = ui->node->ban_entry(i);
            char fl[12];
            sprintf(fl, "0x%02lX", static_cast<unsigned long>(be.flags));
            const char* cells[5] = { be.key, be.banned_at, be.lift_at, fl, be.reason };
            am_draw_listview_row(ui, lx, ty + 48 + i * 11, bw, 10, (i == ui->sel_ban),
                                 5, bcol_x, cells, bcol_w, ui->pen_panel, ui->pen_fg);
        }
        am_draw_button(ui, mx + mw - 176, by, 72, 15, "Close", 0);
        am_add_hit(ui, mx + mw - 176, by, 72, 15, 999);
        am_draw_button(ui, mx + mw - 96, by, 84, 15, "Add Ban", 1);
    } else if (scr == OYK_SCR_EXIT_PEERS) {
        sprintf(buf, "%d auto-learned peer(s) found during session.",
                ui->node->auto_learned_peer_count());
        am_draw_text(ui, lx, ty + 10, buf, ui->pen_fg, ui->pen_panel, bw / 8);
        am_draw_text(ui, lx, ty + 26, "Keep learned peers in configuration before exit?",
                     ui->pen_fg, ui->pen_panel, bw / 8);
        am_draw_button(ui, lx, by, 106, 15, "Save & Exit", 1);
        am_draw_button(ui, lx + 112, by, 76, 15, "Discard", 0);
        am_draw_button(ui, lx + 194, by, 114, 15, "Review Peers", 0);
        am_add_hit(ui, lx + 194, by, 114, 15, OYK_SCR_PEERS);
        am_draw_button(ui, mx + mw - 84, by, 72, 15, "Cancel", 0);
        am_add_hit(ui, mx + mw - 84, by, 72, 15, 999);
    } else if (scr == OYK_SCR_THEME) {
        am_draw_button(ui, lx, ty, 24, 14, "<", 0);
        am_add_hit(ui, lx, ty, 24, 14, 401);
        am_draw_button(ui, lx + 28, ty, 24, 14, ">", 0);
        am_add_hit(ui, lx + 28, ty, 24, 14, 402);
        if (ui->locked_to_native) {
            sprintf(buf, "Theme: native (Locked: OS Native %d clr < 32)", ui->scr_colors);
        } else {
            sprintf(buf, "Theme: %s (Whole-Screen Live Preview, %d clr)",
                    ui->theme_ctrl.active_name, ui->scr_colors);
        }
        am_draw_text(ui, lx + 60, ty + 3, buf, ui->pen_fg, ui->pen_panel, (bw - 64) / 8);
        am_draw_text(ui, lx, ty + 18, "Role             Key         Color    Preview",
                     ui->pen_fg, ui->pen_panel, bw / 8);
        for (int i = 0; i < 8; ++i) {
            int ry = ty + 30 + i * 11;
            sprintf(buf, "%-16s %-11s #%06lX",
                    oyk_theme_role_label(i),
                    oyk_theme_role_key(i),
                    static_cast<unsigned long>(ui->active_theme.colors[i] & 0xFFFFFFUL));
            am_draw_text(ui, lx, ry, buf, ui->pen_fg, ui->pen_panel, 46);
            int sw_pen = am_swatch_pen_for_role(ui, i);
            am_fill_rect(ui, lx + 350, ry + 1, 68, 9, sw_pen);
            am_bevel_box(ui, lx + 350, ry + 1, 68, 9, 0);
        }
        am_draw_button(ui, mx + mw - 164, by, 72, 15, "Cancel", 0);
        am_add_hit(ui, mx + mw - 164, by, 72, 15, 999);
        am_draw_button(ui, mx + mw - 84, by, 72, 15, "Apply", 1);
        am_add_hit(ui, mx + mw - 84, by, 72, 15, 999);
    } else if (scr == OYK_SCR_TERMINAL) {
        am_draw_text(ui, lx, ty + 2, "Session: KR4OYK-1 BBS | EOL: LF | Wrap: ON",
                     ui->pen_fg, ui->pen_panel, bw / 8);
        am_fill_rect(ui, lx, ty + 14, bw, mh - 96, ui->pen_bg);
        am_bevel_box(ui, lx, ty + 14, bw, mh - 96, 1);
        am_draw_text(ui, lx + 6, ty + 19, "Welcome to KR4OYK-7 BBS on Amiga Workbench",
                     ui->pen_fg, ui->pen_bg, (bw - 12) / 8);
        am_draw_text(ui, lx + 6, ty + 31, "Type 'LIST' or 'B' to disconnect from BBS.",
                     ui->pen_fg, ui->pen_bg, (bw - 12) / 8);
        am_draw_text(ui, lx + 6, ty + 43, "14:21:08  Msg #42  ALL  de W4ABC-7: Net tonight",
                     ui->pen_fg, ui->pen_bg, (bw - 12) / 8);
        am_fill_rect(ui, lx, by - 36, bw, 13, ui->pen_status_bg);
        am_draw_text(ui, lx + 4, by - 33, oyk_term_hotkey_bar_text(0, 0),
                     ui->pen_status_fg, ui->pen_status_bg, (bw - 8) / 8);
        am_draw_field_row(ui, lx, by - 20, "Input:", "LIST_", 56, bw - 134);
        am_draw_button(ui, lx + bw - 70, by - 21, 70, 14, "Send", 1);
        am_draw_button(ui, lx, by, 72, 15, "Help", 0);
        am_draw_button(ui, mx + mw - 84, by, 72, 15, "Close", 0);
        am_add_hit(ui, mx + mw - 84, by, 72, 15, 999);
    } else if (scr == OYK_SCR_TERM_HELP) {
        am_draw_text(ui, lx, ty + 2, "Command prefix: Ctrl-A (press Ctrl-A, then hotkey)",
                     ui->pen_fg, ui->pen_panel, bw / 8);
        am_fill_rect(ui, lx, ty + 14, bw, mh - 62, ui->pen_bg);
        am_bevel_box(ui, lx, ty + 14, bw, mh - 62, 1);
        for (int hi = 0; hi < oyk_term_help_count() && hi < 8; ++hi) {
            const OykTermHelpEntry* he = oyk_term_help_entry(hi);
            if (!he) continue;
            char row[96];
            sprintf(row, "%-9.9s %-36.36s %s", he->key_label, he->action, he->alt_key);
            am_draw_text(ui, lx + 6, ty + 19 + hi * 11, row,
                         ui->pen_fg, ui->pen_bg, (bw - 12) / 8);
        }
        am_draw_button(ui, mx + mw - 84, by, 72, 15, "Close", 1);
        am_add_hit(ui, mx + mw - 84, by, 72, 15, 999);
    } else if (scr == OYK_SCR_FILES) {
        int pw = (bw - 10) / 2;
        int fx1[2] = { lx + 4, lx + pw - 58 };
        const char* fh[2] = { "Name", "Size" };
        int fw1[2] = { 21, 7 };
        const char* fr1a[2] = { "W3TRM-1", "<DIR>" };
        const char* fr1b[2] = { "PACKETS.TXT", "2048 B" };
        int fx2[2] = { lx + pw + 14, lx + bw - 58 };
        int fw2[2] = { 21, 7 };
        const char* fr2a[2] = { "bulletins", "<DIR>" };
        const char* fr2b[2] = { "README.TXT", "1420 B" };
        am_draw_text(ui, lx, ty + 2, "Incoming: /", ui->pen_fg, ui->pen_panel, pw / 8);
        am_draw_text(ui, lx + pw + 10, ty + 2, "Files: /", ui->pen_fg, ui->pen_panel, pw / 8);
        am_fill_rect(ui, lx, ty + 15, pw, mh - 64, ui->pen_bg);
        am_bevel_box(ui, lx, ty + 15, pw, mh - 64, 1);
        am_fill_rect(ui, lx + pw + 10, ty + 15, pw, mh - 64, ui->pen_bg);
        am_bevel_box(ui, lx + pw + 10, ty + 15, pw, mh - 64, 1);
        am_draw_listview_header(ui, lx + 2, ty + 17, pw - 4, 2, fx1, fh, ui->pen_panel, ui->pen_fg);
        am_draw_listview_row(ui, lx + 2, ty + 28, pw - 4, 10, 1, 2, fx1, fr1a, fw1, ui->pen_bg, ui->pen_fg);
        am_draw_listview_row(ui, lx + 2, ty + 39, pw - 4, 10, 0, 2, fx1, fr1b, fw1, ui->pen_bg, ui->pen_fg);
        am_draw_listview_header(ui, lx + pw + 12, ty + 17, pw - 4, 2, fx2, fh, ui->pen_panel, ui->pen_fg);
        am_draw_listview_row(ui, lx + pw + 12, ty + 28, pw - 4, 10, 1, 2, fx2, fr2a, fw2, ui->pen_bg, ui->pen_fg);
        am_draw_listview_row(ui, lx + pw + 12, ty + 39, pw - 4, 10, 0, 2, fx2, fr2b, fw2, ui->pen_bg, ui->pen_fg);
        am_draw_button(ui, lx,       by, 64, 15, "Upload",  0);
        am_draw_button(ui, lx + 68,  by, 68, 15, "Approve", 1);
        am_draw_button(ui, lx + 140, by, 62, 15, "Rename",  0);
        am_draw_button(ui, lx + 206, by, 64, 15, "New Dir", 0);
        am_draw_button(ui, lx + 274, by, 60, 15, "Delete",  0);
        am_draw_button(ui, lx + 338, by, 50, 15, "View",    0);
        am_draw_button(ui, mx + mw - 76, by, 64, 15, "Close", 0);
        am_add_hit(ui, mx + mw - 76, by, 64, 15, 999);
    } else if (scr == OYK_SCR_ABOUT) {
        const int px_h = (ui->scr_w >= 512 && ui->scr_h <= 300) ? 1 : 2;
        for (int py = 0; py < 32; ++py) {
            for (int px = 0; px < 32; ++px) {
                unsigned char pval = kOykLogoSolid32Pal[py * 32 + px];
                if (pval == 0U) continue;
                int pen = (pval == 2U) ? ui->pen_shine
                        : ((pval == 3U) ? ui->pen_fg : ui->pen_accent);
                am_fill_rect(ui, lx + px * 2, ty + 6 + py * px_h, 2, px_h, pen);
            }
        }
        const int tx = lx + 74;
        const int tc = (bw > 74) ? ((bw - 74) / 8) : 16;
        am_draw_text(ui, tx, ty + 8,  "OYK Amateur Radio Packet Node (GPL-3.0-or-later)",
                     ui->pen_fg, ui->pen_panel, tc);
        am_draw_text(ui, tx, ty + 22, "Protocols: AX.25 / KISS / AXUDP / AXIP / BBS / YAPP",
                     ui->pen_fg, ui->pen_panel, tc);
        am_draw_text(ui, tx, ty + 36, "Platform: Amiga Workbench (Intuition + serial.device)",
                     ui->pen_fg, ui->pen_panel, tc);
        am_draw_text(ui, tx, ty + 52, "Hotkeys: 1..7 Settings Tabs | Esc/Q Close Modal",
                     ui->pen_fg, ui->pen_panel, tc);
        am_draw_button(ui, mx + mw - 84, by, 72, 15, "OK", 1);
        am_add_hit(ui, mx + mw - 84, by, 72, 15, 999);
    } else {
        am_draw_text(ui, lx, ty + 10, "Hamlib rigctld CAT: Port 4532 / serial.device @ 9600",
                     ui->pen_fg, ui->pen_panel, bw / 8);
        am_draw_button(ui, mx + mw - 84, by, 72, 15, "Close", 1);
        am_add_hit(ui, mx + mw - 84, by, 72, 15, 999);
    }
#ifdef __AMIGA__
    ui->rp = saved_rp;
#endif
}

static void am_redraw(OykAmigaUiState* ui) {
    if (!ui || !ui->node) return;
    oyk_gui_theme_resolve(&ui->theme_ctrl, &ui->active_theme);
    am_apply_pens_and_palette(ui);
    am_draw_console(ui);
    am_sync_modal_window(ui);
    if (ui->active_screen != OYK_SCR_MAIN) {
        am_draw_modal(ui);
    }
}

static void am_on_theme_preview(const OykThemeConfig* pal, const char* active_name, void* ctx) {
    (void)active_name;
    OykAmigaUiState* ui = static_cast<OykAmigaUiState*>(ctx);
    if (!ui) return;
    if (ui->locked_to_native) {
        ui->active_theme = ui->os_native_pal;
    } else if (pal) {
        ui->active_theme = *pal;
    }
    am_redraw(ui);
}

#ifdef __AMIGA__
static int am_open_display(OykAmigaUiState* ui, int use_private, int force_depth) {
    ui->exec_ver = SysBase ? static_cast<int>(SysBase->LibNode.lib_Version) : 34;
    ui->has_gadtools = oyk_amiga_has_gadtools(ui->exec_ver);
    if (ui->has_gadtools) {
        ui->gt_lib = OpenLibrary(reinterpret_cast<CONST_STRPTR>("gadtools.library"), 36);
        if (!ui->gt_lib) ui->has_gadtools = 0;
        else GadToolsBase = ui->gt_lib;
    }

    int target_depth = (force_depth > 0) ? force_depth : 5;
    if (use_private || force_depth > 0) {
        ui->is_private_screen = 1;
        if (ui->exec_ver >= 36) {
            struct TagItem stags[7];
            stags[0].ti_Tag = SA_Width;     stags[0].ti_Data = 640;
            stags[1].ti_Tag = SA_Height;    stags[1].ti_Data = 256;
            stags[2].ti_Tag = SA_Depth;     stags[2].ti_Data = static_cast<ULONG>(target_depth);
            stags[3].ti_Tag = SA_Title;     stags[3].ti_Data = reinterpret_cast<ULONG>("OYK Amateur Radio Packet Node");
            stags[4].ti_Tag = SA_Type;      stags[4].ti_Data = CUSTOMSCREEN;
            stags[5].ti_Tag = SA_DisplayID; stags[5].ti_Data = HIRES_KEY;
            stags[6].ti_Tag = TAG_DONE;     stags[6].ti_Data = 0;
            ui->owned_scr = OpenScreenTagList(NULL, stags);
        }
        if (!ui->owned_scr) {
            struct NewScreen ns;
            memset(&ns, 0, sizeof(ns));
            ns.Width = 640;
            ns.Height = 256;
            ns.Depth = static_cast<WORD>(target_depth <= 4 ? target_depth : 4);
            ns.DetailPen = 0;
            ns.BlockPen = 1;
            ns.ViewModes = HIRES;
            ns.Type = CUSTOMSCREEN;
            ns.DefaultTitle = const_cast<STRPTR>("OYK Amateur Radio Packet Node");
            ui->owned_scr = OpenScreen(&ns);
        }
        ui->scr = ui->owned_scr;
    }

    if (!ui->scr) {
        ui->is_private_screen = 0;
        if (ui->exec_ver >= 36) {
            ui->scr = LockPubScreen(reinterpret_cast<CONST_STRPTR>("Workbench"));
        }
    }

    int scr_w = ui->scr ? ui->scr->Width : 640;
    int scr_h = ui->scr ? ui->scr->Height : 256;
    int bar_h = (ui->scr && ui->scr->BarHeight > 0) ? (ui->scr->BarHeight + 2) : 12;

    struct NewWindow nw;
    memset(&nw, 0, sizeof(nw));
    nw.LeftEdge = 4;
    nw.TopEdge = static_cast<WORD>(bar_h);
    nw.Width = static_cast<WORD>(scr_w - 8);
    nw.Height = static_cast<WORD>(scr_h - bar_h - 2);
    nw.DetailPen = 0;
    nw.BlockPen = 1;
    nw.IDCMPFlags = IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY | IDCMP_MOUSEBUTTONS | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK;
    nw.Flags = WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET | WFLG_ACTIVATE | WFLG_SMART_REFRESH;
    nw.Title = const_cast<STRPTR>("OYK Amateur Radio Packet Node");
    nw.Screen = ui->scr;
    nw.Type = ui->scr ? (ui->owned_scr ? CUSTOMSCREEN : PUBLICSCREEN) : WBENCHSCREEN;
    if (!ui->scr && ui->exec_ver < 36) nw.Type = WBENCHSCREEN;

    ui->win = OpenWindow(&nw);
    if (!ui->win) return 0;
    if (!ui->scr) ui->scr = ui->win->WScreen;
    ui->rp = ui->win->RPort;
    ui->scr_w = ui->scr ? ui->scr->Width : ui->win->Width;
    ui->scr_h = ui->scr ? ui->scr->Height : ui->win->Height;
    ui->cli_x = ui->win->BorderLeft;
    ui->cli_y = ui->win->BorderTop;
    ui->cli_w = ui->win->Width - ui->win->BorderLeft - ui->win->BorderRight;
    ui->cli_h = ui->win->Height - ui->win->BorderTop - ui->win->BorderBottom;
    if (ui->scr && ui->scr->RastPort.BitMap) {
        ui->scr_depth = (ui->exec_ver >= 39)
                            ? static_cast<int>(GetBitMapAttr(ui->scr->RastPort.BitMap, BMA_DEPTH))
                            : static_cast<int>(ui->scr->RastPort.BitMap->Depth);
    } else {
        ui->scr_depth = 2;
    }
    ui->scr_colors = oyk_amiga_colors_from_depth(ui->scr_depth);
    return 1;
}

static void am_close_display(OykAmigaUiState* ui) {
    if (ui->modal_win) {
        CloseWindow(ui->modal_win);
        ui->modal_win = NULL;
        ui->modal_rp = NULL;
        ui->modal_scr = OYK_SCR_MAIN;
    }
    if (ui->win) {
        CloseWindow(ui->win);
        ui->win = NULL;
    }
    if (ui->owned_scr) {
        CloseScreen(ui->owned_scr);
        ui->owned_scr = NULL;
        ui->scr = NULL;
    } else if (ui->scr) {
        if (ui->exec_ver >= 36) UnlockPubScreen(NULL, ui->scr);
        ui->scr = NULL;
    }
    if (ui->gt_lib) {
        CloseLibrary(ui->gt_lib);
        ui->gt_lib = NULL;
    }
}
#endif

static void am_seed_demo_node(OykAmigaUiState* ui) {
    OykPortableNode* node = ui->node;
    OykConfig seed = node->config();
    oyk_parse_callsign("KR4OYK-7", &seed.callsign);
    oyk_parse_callsign("OYKNOD", &seed.alias);
    seed.iface_count = 2;
    memset(&seed.ifaces[0], 0, sizeof(OykIfaceConfig));
    strcpy(seed.ifaces[0].id, "axudp0");
    strcpy(seed.ifaces[0].type, "axudp");
    strcpy(seed.ifaces[0].host, "127.0.0.1");
    seed.ifaces[0].port = 10093U;
    strcpy(seed.ifaces[0].netmask, "255.0.0.0");
    seed.ifaces[0].block_inbound = 0;
    seed.ifaces[0].block_outbound = 0;

    memset(&seed.ifaces[1], 0, sizeof(OykIfaceConfig));
    strcpy(seed.ifaces[1].id, "serial0");
    strcpy(seed.ifaces[1].type, "kiss-serial");
    strcpy(seed.ifaces[1].device, "serial.device");
    seed.ifaces[1].baud = 9600UL;
    seed.ifaces[1].block_inbound = 0;
    seed.ifaces[1].block_outbound = 0;

    seed.peer_count = 2;
    memset(&seed.peers[0], 0, sizeof(OykPeerConfig));
    strcpy(seed.peers[0].id, "remote0");
    strcpy(seed.peers[0].callsign, "R3MOT-7");
    strcpy(seed.peers[0].interface_id, "axudp0");
    strcpy(seed.peers[0].host, "12.34.56.78");
    seed.peers[0].port = 10093U;
    seed.peers[0].allow_inbound = 1;
    seed.peers[0].allow_outbound = 1;

    memset(&seed.peers[1], 0, sizeof(OykPeerConfig));
    strcpy(seed.peers[1].id, "auto1");
    strcpy(seed.peers[1].callsign, "W4ABC-7");
    strcpy(seed.peers[1].interface_id, "serial0");
    strcpy(seed.peers[1].host, "145.050");
    seed.peers[1].auto_learned = 1;

    OykConfigApplyResult res;
    node->apply_config(&seed, 0, &res);
    node->set_iface_status(0, 1, "UP");
    node->set_iface_status(1, 1, "UP");

    OykFrame rx_f;
    memset(&rx_f, 0, sizeof(rx_f));
    oyk_parse_callsign("KR4OYK-7", &rx_f.dst);
    oyk_parse_callsign("W4ABC-7", &rx_f.src);
    rx_f.control = 0x3FU; /* SABM+P */
    node->on_rx_frame(0, &rx_f, 0x7F000002UL, 10093U, 0U);

    oyk_u32 sid = 0;
    OykCallsign rcall;
    int isloc = 0;
    char err_b[96];
    err_b[0] = '\0';
    node->open_local_terminal(0, "BBS", &sid, &rcall, &isloc, err_b, sizeof(err_b));
    node->ban_callsign("N0BAD", 0x07UL, "Beacon flood", "7d", err_b, sizeof(err_b));
    node->log_message(OYK_LOG_INFO, "OYK-Node started on Amiga Workbench");
    node->log_message(OYK_LOG_INFO, ui->net ? ui->net->caps.backend_summary : "AmigaOS");

    oyk_gui_wiz_init(&ui->wiz, &node->config(), OYK_DRV_ALL);
    oyk_gui_wiz_select_driver(&ui->wiz, &node->config(), 2);
}

struct AmGalleryStep {
    int         scr;
    int         tab;
    int         wiz_step;
    const char* bmp_path;
};

static const AmGalleryStep kAmGallery[23] = {
    { OYK_SCR_MAIN,       0, 0, "DH0:AM_MAIN.BMP" },
    { OYK_SCR_MENU,       0, 0, "DH0:AM_MENU.BMP" },
    { OYK_SCR_SETTINGS,   0, 0, "DH0:AM_SET1.BMP" },
    { OYK_SCR_SETTINGS,   1, 0, "DH0:AM_SET2.BMP" },
    { OYK_SCR_SETTINGS,   2, 0, "DH0:AM_SET3.BMP" },
    { OYK_SCR_SETTINGS,   3, 0, "DH0:AM_SET4.BMP" },
    { OYK_SCR_PEERS,      4, 0, "DH0:AM_PEER.BMP" },
    { OYK_SCR_SETTINGS,   5, 0, "DH0:AM_SET6.BMP" },
    { OYK_SCR_SETTINGS,   6, 0, "DH0:AM_SET7.BMP" },
    { OYK_SCR_ADD_IFACE,  0, 0, "DH0:AM_ADD1.BMP" },
    { OYK_SCR_ADD_IFACE,  0, 1, "DH0:AM_ADD2.BMP" },
    { OYK_SCR_ADD_IFACE,  0, 2, "DH0:AM_ADD3.BMP" },
    { OYK_SCR_ADD_IFACE,  0, 3, "DH0:AM_ADD4.BMP" },
    { OYK_SCR_EDIT_IFACE, 0, 0, "DH0:AM_EDTI.BMP" },
    { OYK_SCR_EDIT_PEER,  0, 0, "DH0:AM_EDTP.BMP" },
    { OYK_SCR_TERMINATE,  0, 0, "DH0:AM_TERM.BMP" },
    { OYK_SCR_BAN,        0, 0, "DH0:AM_BAN.BMP"  },
    { OYK_SCR_EXIT_PEERS, 0, 0, "DH0:AM_EXIT.BMP" },
    { OYK_SCR_THEME,      0, 0, "DH0:AM_THME.BMP" },
    { OYK_SCR_TERMINAL,   0, 0, "DH0:AM_TML.BMP"  },
    { OYK_SCR_TERM_HELP,  0, 0, "DH0:AM_TMLH.BMP" },
    { OYK_SCR_FILES,      0, 0, "DH0:AM_FILE.BMP" },
    { OYK_SCR_ABOUT,      0, 0, "DH0:AM_ABT.BMP"  }
};

int oyk_amiga_ui_run(OykPortableNode* node,
                     OykAmigaNetState* net,
                     int use_private_screen,
                     int ui_test_mode) {
    if (!node) return 1;
    OykAmigaUiState* ui = new OykAmigaUiState();
    if (!ui) return 1;
    memset(ui, 0, sizeof(*ui));
    ui->node = node;
    ui->net = net;
    ui->running = 1;
    ui->active_screen = OYK_SCR_MAIN;
    ui->scr_w = 640;
    ui->scr_h = 256;
    ui->scr_depth = 2;
    ui->scr_colors = 4;

#ifdef __AMIGA__
    struct Library* ilib = OpenLibrary(reinterpret_cast<CONST_STRPTR>("intuition.library"), 33);
    struct Library* glib = OpenLibrary(reinterpret_cast<CONST_STRPTR>("graphics.library"), 33);
    if (!ilib || !glib) {
        if (glib) CloseLibrary(glib);
        if (ilib) CloseLibrary(ilib);
        delete ui;
        return 1;
    }
    IntuitionBase = reinterpret_cast<struct IntuitionBase*>(ilib);
    GfxBase = reinterpret_cast<struct GfxBase*>(glib);
    if (!am_open_display(ui, use_private_screen, 0)) {
        CloseLibrary(glib);
        CloseLibrary(ilib);
        delete ui;
        return 1;
    }
    oyk_amiga_query_os_native_theme(ui->scr, &ui->os_native_pal);
#else
    (void)use_private_screen;
    oyk_amiga_query_os_native_theme(NULL, &ui->os_native_pal);
#endif

    ui->draft_cfg = node->config();
    oyk_gui_wiz_init(&ui->wiz, NULL, OYK_DRV_ALL);
    oyk_gui_theme_init(&ui->theme_ctrl, &ui->draft_cfg, 1, ui->os_native_pal.is_dark_base);
    oyk_gui_theme_set_os_native(&ui->theme_ctrl, &ui->os_native_pal);
    oyk_gui_theme_set_preview_cb(&ui->theme_ctrl, am_on_theme_preview, ui);
    ui->active_theme = ui->os_native_pal;

    if (ui_test_mode) {
        am_seed_demo_node(ui);
        ui->draft_cfg = node->config();
        for (int i = 0; i < 23; ++i) {
            ui->active_screen = kAmGallery[i].scr;
            ui->settings_tab = kAmGallery[i].tab;
            ui->wiz.step = kAmGallery[i].wiz_step;
            am_redraw(ui);
#ifdef __AMIGA__
            oyk_amiga_capture_screen_bmp(kAmGallery[i].bmp_path, ui->scr);
#endif
        }
        oyk_amiga_finish_test();
#ifdef __AMIGA__
        am_close_display(ui);
        CloseLibrary(glib);
        CloseLibrary(ilib);
#endif
        delete ui;
        return 0;
    }

    am_redraw(ui);
#ifdef __AMIGA__
    while (ui->running) {
        ULONG sigmask = (1UL << ui->win->UserPort->mp_SigBit);
        if (ui->modal_win && ui->modal_win->UserPort) {
            sigmask |= (1UL << ui->modal_win->UserPort->mp_SigBit);
        }
        Wait(sigmask);
        struct MsgPort* ports[2];
        ports[0] = (ui->modal_win && ui->modal_win->UserPort) ? ui->modal_win->UserPort : NULL;
        ports[1] = ui->win->UserPort;
        for (int pi = 0; pi < 2; ++pi) {
            struct MsgPort* mp = ports[pi];
            if (!mp) continue;
            int from_modal = (pi == 0);
            struct IntuiMessage* imsg;
            while ((imsg = reinterpret_cast<struct IntuiMessage*>(GetMsg(mp))) != NULL) {
                ULONG cls = imsg->Class;
                UWORD code = imsg->Code;
                WORD mx_ev = imsg->MouseX;
                WORD my_ev = imsg->MouseY;
                ReplyMsg(reinterpret_cast<struct Message*>(imsg));
                if (cls == IDCMP_CLOSEWINDOW) {
                    if (from_modal && ui->active_screen != OYK_SCR_MAIN) {
                        ui->active_screen = OYK_SCR_MAIN;
                        am_redraw(ui);
                    } else {
                        ui->running = 0;
                    }
                } else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
                    int act = -1;
                    for (int hi = ui->hit_count - 1; hi >= 0; --hi) {
                        const AmHitZone& z = ui->hits[hi];
                        if (mx_ev >= z.x && mx_ev < z.x + z.w && my_ev >= z.y && my_ev < z.y + z.h) {
                            act = z.action;
                            break;
                        }
                    }
                    if (act == 999) {
                        ui->active_screen = OYK_SCR_MAIN;
                        am_redraw(ui);
                    } else if (act >= 100 && act < 107) {
                        ui->active_screen = OYK_SCR_SETTINGS;
                        ui->settings_tab = act - 100;
                        am_redraw(ui);
                    } else if (act >= 200 && act < 205) {
                        oyk_gui_wiz_select_driver(&ui->wiz, &ui->node->config(), act - 200);
                        am_redraw(ui);
                    } else if (act == 210) {
                        oyk_gui_wiz_back(&ui->wiz);
                        am_redraw(ui);
                    } else if (act == 211) {
                        if (ui->wiz.step >= 3) {
                            ui->active_screen = OYK_SCR_SETTINGS;
                            ui->settings_tab = 3;
                        } else {
                            oyk_gui_wiz_advance(&ui->wiz, &ui->node->config());
                        }
                        am_redraw(ui);
                    } else if (act == 302) {
                        OykConfig cpy = ui->node->config();
                        if (cpy.iface_count > 0) {
                            int idx = (ui->sel_iface < cpy.iface_count) ? ui->sel_iface : 0;
                            cpy.ifaces[idx].learned_peer_policy = static_cast<oyk_u8>(
                                oyk_gui_cycle_learned_policy(cpy.ifaces[idx].learned_peer_policy, 1));
                            OykConfigApplyResult r;
                            ui->node->apply_config(&cpy, 0, &r);
                            am_redraw(ui);
                        }
                    } else if (act == 401 && !ui->locked_to_native) {
                        oyk_gui_theme_cycle(&ui->theme_ctrl, -1);
                    } else if (act == 402 && !ui->locked_to_native) {
                        oyk_gui_theme_cycle(&ui->theme_ctrl, 1);
                    } else if (act >= 0 && act < 100) {
                        ui->active_screen = act;
                        if (act == OYK_SCR_PEERS) ui->settings_tab = 4;
                        am_redraw(ui);
                    }
                } else if (cls == IDCMP_VANILLAKEY) {
                    if (code == 'q' || code == 'Q' || code == 27) {
                        if (ui->active_screen != OYK_SCR_MAIN) {
                            ui->active_screen = OYK_SCR_MAIN;
                            am_redraw(ui);
                        } else {
                            ui->running = 0;
                        }
                    } else if ((ui->active_screen == OYK_SCR_SETTINGS || ui->active_screen == OYK_SCR_PEERS) &&
                               code >= '1' && code <= '7') {
                        ui->active_screen = OYK_SCR_SETTINGS;
                        ui->settings_tab = static_cast<int>(code - '1');
                        am_redraw(ui);
                    }
                } else if (cls == IDCMP_REFRESHWINDOW) {
                    struct Window* rw = (from_modal && ui->modal_win) ? ui->modal_win : ui->win;
                    BeginRefresh(rw);
                    am_redraw(ui);
                    EndRefresh(rw, TRUE);
                }
            }
        }
        oyk_amiga_net_poll(net);
    }
    am_close_display(ui);
    CloseLibrary(glib);
    CloseLibrary(ilib);
#endif
    delete ui;
    return 0;
}

void oyk_amiga_ui_show_fatal(const char* msg) {
    fprintf(stderr, "OYK-Node Fatal: %s\n", msg ? msg : "unknown error");
}

int oyk_amiga_ui_run_theme_preview_self_test(OykPortableNode* node,
                                             OykAmigaNetState* net,
                                             int screen_depth,
                                             int* out_locked_low,
                                             int* out_bg_changes,
                                             int* out_fg_changes) {
    if (!node) return 0;
    OykAmigaUiState* ui = new OykAmigaUiState();
    if (!ui) return 0;
    memset(ui, 0, sizeof(*ui));
    ui->node = node;
    ui->net = net;
    ui->active_screen = OYK_SCR_THEME;
    ui->scr_w = 640;
    ui->scr_h = 256;
    ui->scr_depth = screen_depth;
    ui->scr_colors = oyk_amiga_colors_from_depth(screen_depth);

#ifdef __AMIGA__
    struct Library* ilib = OpenLibrary(reinterpret_cast<CONST_STRPTR>("intuition.library"), 33);
    struct Library* glib = OpenLibrary(reinterpret_cast<CONST_STRPTR>("graphics.library"), 33);
    if (!ilib || !glib) {
        if (glib) CloseLibrary(glib);
        if (ilib) CloseLibrary(ilib);
        delete ui;
        return 0;
    }
    IntuitionBase = reinterpret_cast<struct IntuitionBase*>(ilib);
    GfxBase = reinterpret_cast<struct GfxBase*>(glib);
    if (!am_open_display(ui, (screen_depth >= 5) ? 1 : 0, (screen_depth >= 5) ? screen_depth : 0)) {
        CloseLibrary(glib);
        CloseLibrary(ilib);
        delete ui;
        return 0;
    }
    /* Ensure target depth reflected cleanly if private screen succeeded */
    if (screen_depth >= 5 && ui->scr_colors < 32) {
        ui->scr_depth = screen_depth;
        ui->scr_colors = oyk_amiga_colors_from_depth(screen_depth);
    }
    oyk_amiga_query_os_native_theme(ui->scr, &ui->os_native_pal);
#else
    oyk_amiga_query_os_native_theme(NULL, &ui->os_native_pal);
#endif

    ui->draft_cfg = node->config();
    oyk_gui_theme_init(&ui->theme_ctrl, &ui->draft_cfg, 1, ui->os_native_pal.is_dark_base);
    oyk_gui_theme_set_os_native(&ui->theme_ctrl, &ui->os_native_pal);
    oyk_gui_theme_set_preview_cb(&ui->theme_ctrl, am_on_theme_preview, ui);
    ui->active_theme = ui->os_native_pal;
    am_redraw(ui);

    int ok = 1;
    int bg_changes = 0, fg_changes = 0;
    if (oyk_amiga_policy_allows_custom_themes(ui->scr_colors) == 0) {
        /* Low color (<32 colors): must remain locked to OS native pens! */
        oyk_gui_theme_select(&ui->theme_ctrl, "dark");
        if (!ui->locked_to_native || ui->last_console_bg != ui->os_native_pal.colors[OYK_TR_BG]) {
            ok = 0;
        }
        if (out_locked_low) *out_locked_low = ui->locked_to_native;
    } else {
        /* >=32 colors: full live whole-screen preview across dark, light, custom, native! */
        if (out_locked_low) *out_locked_low = ui->locked_to_native;
        oyk_u32 prev_cbg = ui->last_console_bg;
        oyk_u32 prev_mbg = ui->last_modal_bg;
        int prev_cdraw = ui->console_draw_count;
        int prev_mdraw = ui->modal_draw_count;
        const char* seq[4] = { "dark", "light", NULL, "native" };
        char terr[64];
        for (int i = 0; i < 4; ++i) {
            if (seq[i]) {
                oyk_gui_theme_select(&ui->theme_ctrl, seq[i]);
            } else {
                oyk_gui_theme_set_role_rgb(&ui->theme_ctrl, OYK_TR_BG, 0x334455UL,
                                           terr, sizeof(terr));
            }
            if (ui->console_draw_count > prev_cdraw && ui->last_console_bg != prev_cbg) ++bg_changes;
            if (ui->modal_draw_count > prev_mdraw && ui->last_modal_bg != prev_mbg) ++fg_changes;
            prev_cbg = ui->last_console_bg;
            prev_mbg = ui->last_modal_bg;
            prev_cdraw = ui->console_draw_count;
            prev_mdraw = ui->modal_draw_count;
        }
        oyk_gui_theme_select(&ui->theme_ctrl, "light");
        oyk_gui_theme_revert(&ui->theme_ctrl);
        if (strcmp(ui->theme_ctrl.active_name, "native") != 0 ||
            ui->last_console_bg != ui->os_native_pal.colors[OYK_TR_BG]) {
            ok = 0;
        }
        if (bg_changes < 4 || fg_changes < 4) ok = 0;
    }

    if (out_bg_changes) *out_bg_changes = bg_changes;
    if (out_fg_changes) *out_fg_changes = fg_changes;

#ifdef __AMIGA__
    am_close_display(ui);
    CloseLibrary(glib);
    CloseLibrary(ilib);
#endif
    delete ui;
    return ok;
}
