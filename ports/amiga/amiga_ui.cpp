/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "amiga_ui.hpp"
#include "amiga_capture.hpp"

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
    int                scr_depth;
    int                scr_colors;
    int                active_screen;
    int                settings_tab;
    int                sel_peer;
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
#ifdef __AMIGA__
    struct Screen*     scr;
    struct Screen*     owned_scr;
    struct Window*     win;
    struct RastPort*   rp;
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

static void am_draw_console(OykAmigaUiState* ui) {
    OykPortableNode* node = ui->node;
    const OykConfig& cfg = node->config();
    int top = 14; /* Below Amiga window title/menu bar */
    int cw = ui->scr_w - 8;
    int ch = ui->scr_h - top - 4;
    if (cw < 400) cw = 632;
    if (ch < 180) ch = 238;

    ++ui->console_draw_count;
    ui->last_console_bg = ui->active_theme.colors[OYK_TR_BG];
    am_reset_hits(ui);

    /* Repaint entire window interior (full-screen background repaint on theme change) */
    am_fill_rect(ui, 0, 0, ui->scr_w, 13, ui->pen_shine);
    am_draw_text(ui, 8, 2, "Project  Edit  Node  View  Window  Help",
                 ui->pen_fg, ui->pen_shine, 46);
    {
        char rstat[32];
        sprintf(rstat, "WB KS%d %dclr", ui->exec_ver, ui->scr_colors);
        am_draw_text(ui, ui->scr_w - 120, 2, rstat, ui->pen_fg, ui->pen_shine, 14);
    }

    am_fill_rect(ui, 0, top, ui->scr_w, ui->scr_h - top, ui->pen_bg);

    /* Top Status Banner with canonical header + right incoming badge */
    char cs[16], al[16], hdr[128];
    oyk_format_callsign(&cfg.callsign, cs, sizeof(cs));
    oyk_format_callsign(&cfg.alias, al, sizeof(al));
    sprintf(hdr, "OYK PACKET NODE: %s (%s) | Up: 0s | Conns: %d",
            cs, al, node->total_active_connections());
    const char* pill = oyk_gui_format_incoming_pill(node->accept_incoming(), 0);
    int pill_w = static_cast<int>(strlen(pill)) * 8 + 8;
    am_fill_rect(ui, 6, top + 2, ui->scr_w - 12, 13, ui->pen_status_bg);
    am_bevel_box(ui, 6, top + 2, ui->scr_w - 12, 13, 0);
    am_draw_text(ui, 12, top + 4, hdr, ui->pen_status_fg, ui->pen_status_bg,
                 (ui->scr_w - pill_w - 28) / 8);
    am_fill_rect(ui, ui->scr_w - 10 - pill_w, top + 3, pill_w, 11, ui->pen_panel);
    am_bevel_box(ui, ui->scr_w - 10 - pill_w, top + 3, pill_w, 11, 0);
    am_draw_text(ui, ui->scr_w - 6 - pill_w, top + 4, pill, ui->pen_fg, ui->pen_panel,
                 static_cast<int>(strlen(pill)));

    int y0 = top + 18;
    int half = (ui->scr_w - 18) / 2;
    int top_h = (ui->scr_h >= 360) ? 88 : 54;
    am_draw_panel(ui, 6, y0, half, top_h, "Interfaces");
    am_draw_text(ui, 12, y0 + 14, "Port    Type       State RX/TX", ui->pen_fg, ui->pen_panel, (half - 12) / 8);
    int max_top_rows = (top_h - 24) / 10;
    for (int i = 0; i < cfg.iface_count && i < max_top_rows; ++i) {
        const OykIfaceRuntime& rt = node->iface(i);
        char row[80];
        sprintf(row, "%-7s %-10s %-4s  %lu/%lu",
                cfg.ifaces[i].id, cfg.ifaces[i].type,
                rt.running ? "UP" : "DN",
                static_cast<unsigned long>(rt.rx_frames),
                static_cast<unsigned long>(rt.tx_frames));
        am_draw_text(ui, 12, y0 + 24 + i * 10, row, ui->pen_fg, ui->pen_panel, (half - 12) / 8);
    }

    int rx = 12 + half;
    am_draw_panel(ui, rx, y0, half, top_h, "Last 5 New Callsigns Heard");
    am_draw_text(ui, rx + 6, y0 + 14, "#  Callsign  Port   Heard", ui->pen_fg, ui->pen_panel, (half - 12) / 8);
    for (int i = 0; i < node->new_heard_count() && i < max_top_rows; ++i) {
        const OykNewHeard& he = node->new_heard(i);
        char row[64];
        sprintf(row, "%-2d %-9s %-6s %s", i + 1, he.callsign, he.iface_id, he.time_hms);
        am_draw_text(ui, rx + 6, y0 + 24 + i * 10, row, ui->pen_fg, ui->pen_panel, (half - 12) / 8);
    }

    int cy = y0 + top_h + 4;
    int ch_box = (ui->scr_h >= 360) ? 86 : 52;
    am_draw_panel(ui, 6, cy, ui->scr_w - 12, ch_box, "Active Connections");
    am_draw_text(ui, 12, cy + 14, "ID   Callsign   Port    Dir   State      Since",
                 ui->pen_fg, ui->pen_panel, (ui->scr_w - 24) / 8);
    int max_conn_rows = (ch_box - 24) / 10;
    int shown_c = 0;
    for (int i = 0; i < node->max_sessions() && shown_c < max_conn_rows; ++i) {
        const OykSession& s = node->session_slot(i);
        if (s.state == OYK_SESS_FREE) continue;
        char scs[16], row[96];
        oyk_format_callsign(&s.remote_call, scs, sizeof(scs));
        const char* pname = (s.iface_idx >= 0 && s.iface_idx < cfg.iface_count)
                                ? cfg.ifaces[s.iface_idx].id : "local";
        sprintf(row, "%-4lu %-10s %-7s %-5s %-10s %s",
                static_cast<unsigned long>(s.id), scs, pname,
                s.is_local_term ? "LOCAL" : "IN",
                (s.state == OYK_SESS_CONNECTED) ? "CONNECTED" : "OPENING",
                s.since_hms);
        am_draw_text(ui, 12, cy + 24 + shown_c * 10, row, ui->pen_fg, ui->pen_panel, (ui->scr_w - 24) / 8);
        ++shown_c;
    }

    int ly = cy + ch_box + 4;
    int bar_y = ui->scr_h - 20;
    int lh = bar_y - ly - 4;
    if (lh > 28) {
        am_draw_panel(ui, 6, ly, ui->scr_w - 12, lh, "Node Activity Log");
        int max_r = (lh - 16) / 10;
        int total = node->console_log_count();
        int start = (total > max_r) ? (total - max_r) : 0;
        for (int i = start; i < total; ++i) {
            const char* ln = node->console_log_line(i);
            if (ln) am_draw_text(ui, 12, ly + 14 + (i - start) * 10, ln, ui->pen_fg, ui->pen_panel, (ui->scr_w - 24) / 8);
        }
    }

    am_draw_button(ui, 6,   bar_y, 78, 15, "Terminal", 1); am_add_hit(ui, 6,   bar_y, 78, 15, OYK_SCR_TERMINAL);
    am_draw_button(ui, 90,  bar_y, 64, 15, "Files",    0); am_add_hit(ui, 90,  bar_y, 64, 15, OYK_SCR_FILES);
    am_draw_button(ui, 160, bar_y, 78, 15, "Settings", 0); am_add_hit(ui, 160, bar_y, 78, 15, OYK_SCR_SETTINGS);
    am_draw_button(ui, 244, bar_y, 64, 15, "Theme",    0); am_add_hit(ui, 244, bar_y, 64, 15, OYK_SCR_THEME);
    am_draw_button(ui, 314, bar_y, 64, 15, "Peers",    0); am_add_hit(ui, 314, bar_y, 64, 15, OYK_SCR_PEERS);
    am_draw_text(ui, 390, bar_y + 3,
                 ui->is_private_screen ? "Screen:PRIVATE" : "Screen:Workbench (Priv:OFF)",
                 ui->pen_fg, ui->pen_bg, 30);
}

static void am_draw_modal(OykAmigaUiState* ui) {
    int scr = ui->active_screen;
    int mx = 36, my = 20;
    int mw = ui->scr_w - 72;
    int mh = ui->scr_h - 44;
    if (mw < 420) mw = 560;
    if (mh < 160) mh = 206;

    const char* title = oyk_gui_screen_title(scr);
    if (scr == OYK_SCR_PEERS) title = oyk_gui_screen_title(OYK_SCR_SETTINGS);

    ++ui->modal_draw_count;
    ui->last_modal_bg = ui->active_theme.colors[OYK_TR_BG];

    am_fill_rect(ui, mx + 4, my + 3, mw, mh, ui->pen_shadow);
    am_fill_rect(ui, mx, my, mw, mh, ui->pen_panel);
    am_bevel_box(ui, mx, my, mw, mh, 0);
    am_fill_rect(ui, mx + 2, my + 2, mw - 4, 13, ui->pen_accent);
    am_draw_text(ui, mx + 8, my + 4, title, ui->pen_accent_fg, ui->pen_accent, (mw - 42) / 8);
    am_fill_rect(ui, mx + mw - 30, my + 3, 26, 11, ui->pen_panel);
    am_bevel_box(ui, mx + mw - 30, my + 3, 26, 11, 0);
    am_draw_text(ui, mx + mw - 28, my + 4, "[X]", ui->pen_fg, ui->pen_panel, 3);
    am_add_hit(ui, mx + mw - 30, my + 3, 26, 11, 999);

    int lx = mx + 10;
    int ty = my + 19;
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
            am_draw_text(ui, lx, ty + 18, "ID       Driver      Endpoint / Device         Policy",
                         ui->pen_fg, ui->pen_panel, bw / 8);
            for (int i = 0; i < cfg.iface_count && i < 5; ++i) {
                if (cfg.ifaces[i].device[0]) {
                    sprintf(nbuf, "%s @ %lu", cfg.ifaces[i].device,
                            static_cast<unsigned long>(cfg.ifaces[i].baud));
                } else {
                    sprintf(nbuf, "%s:%u", cfg.ifaces[i].host,
                            static_cast<unsigned int>(cfg.ifaces[i].port));
                }
                sprintf(buf, "%s %-8s %-10s %-24s IN:%s OUT:%s",
                        (i == 0) ? ">" : " ",
                        cfg.ifaces[i].id, cfg.ifaces[i].type, nbuf,
                        cfg.ifaces[i].block_inbound ? "NO" : "YES",
                        cfg.ifaces[i].block_outbound ? "NO" : "YES");
                am_draw_text(ui, lx, ty + 30 + i * 11, buf, ui->pen_fg, ui->pen_panel, bw / 8);
            }
            am_draw_button(ui, lx, by, 96, 15, "Add Iface", 0);
            am_add_hit(ui, lx, by, 96, 15, OYK_SCR_ADD_IFACE);
            am_draw_button(ui, lx + 104, by, 96, 15, "Edit Iface", 0);
            am_add_hit(ui, lx + 104, by, 96, 15, OYK_SCR_EDIT_IFACE);
        } else if (active_tab == 4) {
            am_draw_text(ui, lx, ty + 18, "ID       Callsign  Interface Endpoint          Policy",
                         ui->pen_fg, ui->pen_panel, bw / 8);
            for (int p = 0; p < cfg.peer_count && p < 6; ++p) {
                sprintf(buf, "%s %-8s %-9s %-9s %s:%u %s",
                        (p == ui->sel_peer) ? ">" : " ",
                        cfg.peers[p].id, cfg.peers[p].callsign, cfg.peers[p].interface_id,
                        cfg.peers[p].host, static_cast<unsigned int>(cfg.peers[p].port),
                        cfg.peers[p].auto_learned ? "AUTO" : "STATIC");
                am_draw_text(ui, lx, ty + 30 + p * 11, buf, ui->pen_fg, ui->pen_panel, bw / 8);
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
        am_draw_text(ui, lx, ty + 2, "Step 1 of 4: Select Driver & Configure Endpoint",
                     ui->pen_fg, ui->pen_panel, bw / 8);
        for (int i = 0; i < 5; ++i) {
            int ry = ty + 16 + i * 13;
            am_draw_radio(ui, lx + 4, ry, i == ui->wiz.selected_driver_idx,
                          oyk_gui_wiz_driver_type(ui->wiz.driver_mask, i), ui->pen_fg, 16);
            am_add_hit(ui, lx + 4, ry, 160, 12, 200 + i);
        }
        am_draw_text(ui, lx + 186, ty + 16,
                     oyk_gui_wiz_driver_desc(ui->wiz.driver_mask, ui->wiz.selected_driver_idx),
                     ui->pen_fg, ui->pen_panel, 44);
        am_draw_field_row(ui, lx + 186, ty + 30, "Iface ID:", ui->wiz.draft.id, 84, 120);
        if (ui->wiz.draft.device[0] || strcmp(ui->wiz.draft.type, "kiss-serial") == 0) {
            am_draw_field_row(ui, lx + 186, ty + 45, "Device:",
                              ui->wiz.draft.device[0] ? ui->wiz.draft.device : "serial.device", 84, 150);
            sprintf(nbuf, "%lu", static_cast<unsigned long>(ui->wiz.draft.baud ? ui->wiz.draft.baud : 9600UL));
            am_draw_field_row(ui, lx + 186, ty + 60, "Baud:", nbuf, 84, 100);
        } else {
            am_draw_field_row(ui, lx + 186, ty + 45, "Bind Host:", ui->wiz.draft.host, 84, 140);
            sprintf(nbuf, "%u", static_cast<unsigned int>(ui->wiz.draft.port));
            am_draw_field_row(ui, lx + 186, ty + 60, "Port:", nbuf, 84, 90);
        }
        am_draw_checkbox(ui, lx + 186, ty + 76, !ui->wiz.draft.allow_same_interface,
                         "Isolate Same-Interface Bridges", ui->pen_fg, 34);
        am_draw_button(ui, lx, by, 72, 15, "< Back", 0);
        am_draw_button(ui, mx + mw - 166, by, 72, 15, "Cancel", 0);
        am_add_hit(ui, mx + mw - 166, by, 72, 15, 999);
        am_draw_button(ui, mx + mw - 86, by, 74, 15, "Next >", 1);
    } else if (scr == OYK_SCR_EDIT_IFACE) {
        const OykIfaceConfig& ifc = cfg.ifaces[0];
        am_draw_field_row(ui, lx, ty + 8, "Interface ID:", ifc.id, 112, 120);
        am_draw_field_row(ui, lx + 250, ty + 8, "Driver:", ifc.type, 64, 120);
        am_draw_field_row(ui, lx, ty + 24, "Bind Host:", ifc.host, 112, 150);
        sprintf(nbuf, "%u", static_cast<unsigned int>(ifc.port));
        am_draw_field_row(ui, lx + 280, ty + 24, "Port:", nbuf, 48, 80);
        am_draw_field_row(ui, lx, ty + 40, "Netmask:", ifc.netmask, 112, 150);
        am_draw_checkbox(ui, lx, ty + 58, !ifc.block_inbound, "Allow Inbound", ui->pen_fg, 16);
        am_draw_checkbox(ui, lx + 150, ty + 58, !ifc.block_outbound, "Allow Outbound", ui->pen_fg, 16);
        am_draw_checkbox(ui, lx + 310, ty + 58, ifc.allow_same_interface, "Same-Interface", ui->pen_fg, 16);
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
        sprintf(buf, "%-4s %-10s %-8s %-10s",
                oyk_gui_table_column(OYK_TBL_TERMINATE, 0),
                oyk_gui_table_column(OYK_TBL_TERMINATE, 1),
                oyk_gui_table_column(OYK_TBL_TERMINATE, 2),
                oyk_gui_table_column(OYK_TBL_TERMINATE, 3));
        am_draw_text(ui, lx, ty + 4, buf, ui->pen_fg, ui->pen_panel, bw / 8);
        int row_idx = 0;
        for (int i = 0; i < ui->node->max_sessions() && row_idx < 4; ++i) {
            const OykSession& s = ui->node->session_slot(i);
            if (s.state == OYK_SESS_FREE) continue;
            oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
            sprintf(buf, "%s %-3lu %-10s %-8s CONNECTED",
                    (row_idx == 0) ? ">" : " ",
                    static_cast<unsigned long>(s.id), cs,
                    (s.iface_idx == 0) ? "axudp0" : "serial0");
            am_draw_text(ui, lx, ty + 20 + row_idx * 12, buf, ui->pen_fg, ui->pen_panel, bw / 8);
            ++row_idx;
        }
        am_draw_button(ui, mx + mw - 188, by, 72, 15, "Cancel", 0);
        am_add_hit(ui, mx + mw - 188, by, 72, 15, 999);
        am_draw_button(ui, mx + mw - 108, by, 96, 15, "Terminate", 1);
    } else if (scr == OYK_SCR_BAN) {
        am_draw_field_row(ui, lx, ty + 2, "Callsign:", "N0BAD", 80, 100);
        am_draw_field_row(ui, lx + 190, ty + 2, "Reason:", "Beacon flood", 64, 150);
        am_draw_field_row(ui, lx + 414, ty + 2, "Dur:", "7d", 40, 56);
        am_draw_checkbox(ui, lx, ty + 18, 1, "Hard Ban (Drop AX.25)", ui->pen_fg, 24);
        am_draw_checkbox(ui, lx + 230, ty + 18, 0, "Allow Appeal", ui->pen_fg, 16);
        am_draw_text(ui, lx, ty + 35, "Callsign  Banned At        Lift At     Flags  Reason",
                     ui->pen_fg, ui->pen_panel, bw / 8);
        for (int i = 0; i < ui->node->ban_count() && i < 3; ++i) {
            const OykBanEntry& be = ui->node->ban_entry(i);
            sprintf(buf, "%-9s %-16s %-10s  0x%02lX   %s",
                    be.key, be.banned_at, be.lift_at,
                    static_cast<unsigned long>(be.flags), be.reason);
            am_draw_text(ui, lx, ty + 48 + i * 11, buf, ui->pen_fg, ui->pen_panel, bw / 8);
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
        am_draw_text(ui, lx, ty + 2, "KR4OYK-11 BBS | Ctrl+A H Help | S YAPP | W Wrap",
                     ui->pen_fg, ui->pen_panel, bw / 8);
        am_fill_rect(ui, lx, ty + 15, bw, mh - 82, ui->pen_bg);
        am_bevel_box(ui, lx, ty + 15, bw, mh - 82, 1);
        am_draw_text(ui, lx + 6, ty + 20, "Welcome to KR4OYK-7 BBS on Amiga Workbench",
                     ui->pen_fg, ui->pen_bg, (bw - 12) / 8);
        am_draw_text(ui, lx + 6, ty + 32, "EOL: Auto-Detected LF | Word-Wrap: ON (72 cols)",
                     ui->pen_fg, ui->pen_bg, (bw - 12) / 8);
        am_draw_text(ui, lx + 6, ty + 44, "Type 'LIST' or 'B' to disconnect from BBS.",
                     ui->pen_fg, ui->pen_bg, (bw - 12) / 8);
        am_draw_text(ui, lx + 6, ty + 56, "14:21:08  Msg #42  ALL  de W4ABC-7: Net tonight",
                     ui->pen_fg, ui->pen_bg, (bw - 12) / 8);
        am_draw_field_row(ui, lx, by - 20, "Input:", "LIST_", 56, bw - 134);
        am_draw_button(ui, lx + bw - 70, by - 21, 70, 14, "Send", 1);
        am_draw_button(ui, lx, by, 102, 15, "Ctrl+A Help", 0);
        am_draw_button(ui, mx + mw - 84, by, 72, 15, "Close", 0);
        am_add_hit(ui, mx + mw - 84, by, 72, 15, 999);
    } else if (scr == OYK_SCR_FILES) {
        int pw = (bw - 10) / 2;
        am_draw_text(ui, lx, ty + 2, "Pending Uploads", ui->pen_fg, ui->pen_panel, pw / 8);
        am_draw_text(ui, lx + pw + 10, ty + 2, "Approved BBS Files", ui->pen_fg, ui->pen_panel, pw / 8);
        am_fill_rect(ui, lx, ty + 15, pw, mh - 64, ui->pen_bg);
        am_bevel_box(ui, lx, ty + 15, pw, mh - 64, 1);
        am_fill_rect(ui, lx + pw + 10, ty + 15, pw, mh - 64, ui->pen_bg);
        am_bevel_box(ui, lx + pw + 10, ty + 15, pw, mh - 64, 1);
        am_draw_text(ui, lx + 6, ty + 19, "From     File         Size", ui->pen_fg, ui->pen_bg, (pw - 12) / 8);
        am_draw_text(ui, lx + 6, ty + 31, "W4ABC-7  PACKETS.TXT  2048", ui->pen_fg, ui->pen_bg, (pw - 12) / 8);
        am_draw_text(ui, lx + pw + 16, ty + 19, "Filename            Size", ui->pen_fg, ui->pen_bg, (pw - 12) / 8);
        am_draw_text(ui, lx + pw + 16, ty + 31, "README.TXT          1420", ui->pen_fg, ui->pen_bg, (pw - 12) / 8);
        am_draw_text(ui, lx + pw + 16, ty + 43, "NETLIST.CFG          618", ui->pen_fg, ui->pen_bg, (pw - 12) / 8);
        am_draw_button(ui, lx, by, 86, 15, "Upload...", 0);
        am_draw_button(ui, lx + 92, by, 86, 15, "Approve", 1);
        am_draw_button(ui, mx + mw - 84, by, 72, 15, "Close", 0);
        am_add_hit(ui, mx + mw - 84, by, 72, 15, 999);
    } else {
        am_draw_text(ui, lx, ty + 10, "Hamlib rigctld CAT: Port 4532 / serial.device @ 9600",
                     ui->pen_fg, ui->pen_panel, bw / 8);
        am_draw_button(ui, mx + mw - 84, by, 72, 15, "Close", 1);
        am_add_hit(ui, mx + mw - 84, by, 72, 15, 999);
    }
}

static void am_redraw(OykAmigaUiState* ui) {
    if (!ui || !ui->node) return;
    oyk_gui_theme_resolve(&ui->theme_ctrl, &ui->active_theme);
    am_apply_pens_and_palette(ui);
    am_draw_console(ui);
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

    struct NewWindow nw;
    memset(&nw, 0, sizeof(nw));
    nw.LeftEdge = 0;
    nw.TopEdge = 0;
    nw.Width = ui->scr ? ui->scr->Width : 640;
    nw.Height = ui->scr ? ui->scr->Height : 256;
    nw.DetailPen = 0;
    nw.BlockPen = 1;
    nw.IDCMPFlags = IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY | IDCMP_MOUSEBUTTONS | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK;
    nw.Flags = WFLG_BORDERLESS | WFLG_BACKDROP | WFLG_ACTIVATE | WFLG_SMART_REFRESH;
    nw.Screen = ui->scr;
    nw.Type = ui->scr ? (ui->owned_scr ? CUSTOMSCREEN : PUBLICSCREEN) : WBENCHSCREEN;
    if (!ui->scr && ui->exec_ver < 36) nw.Type = WBENCHSCREEN;

    ui->win = OpenWindow(&nw);
    if (!ui->win) return 0;
    if (!ui->scr) ui->scr = ui->win->WScreen;
    ui->rp = ui->win->RPort;
    ui->scr_w = ui->win->Width;
    ui->scr_h = ui->win->Height;
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
    const char* bmp_path;
};

static const AmGalleryStep kAmGallery[12] = {
    { OYK_SCR_MAIN,       "DH0:AM_MAIN.BMP" },
    { OYK_SCR_SETTINGS,   "DH0:AM_SETT.BMP" },
    { OYK_SCR_ADD_IFACE,  "DH0:AM_ADDI.BMP" },
    { OYK_SCR_EDIT_IFACE, "DH0:AM_EDTI.BMP" },
    { OYK_SCR_PEERS,      "DH0:AM_PEER.BMP" },
    { OYK_SCR_EDIT_PEER,  "DH0:AM_EDTP.BMP" },
    { OYK_SCR_TERMINATE,  "DH0:AM_TERM.BMP" },
    { OYK_SCR_BAN,        "DH0:AM_BAN.BMP" },
    { OYK_SCR_EXIT_PEERS, "DH0:AM_EXIT.BMP" },
    { OYK_SCR_THEME,      "DH0:AM_THME.BMP" },
    { OYK_SCR_TERMINAL,   "DH0:AM_TML.BMP" },
    { OYK_SCR_FILES,      "DH0:AM_FILE.BMP" }
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
        for (int i = 0; i < 12; ++i) {
            ui->active_screen = kAmGallery[i].scr;
            ui->settings_tab = (ui->active_screen == OYK_SCR_PEERS) ? 4 : 0;
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
        WaitPort(ui->win->UserPort);
        struct IntuiMessage* imsg;
        while ((imsg = reinterpret_cast<struct IntuiMessage*>(GetMsg(ui->win->UserPort))) != NULL) {
            ULONG cls = imsg->Class;
            UWORD code = imsg->Code;
            WORD mx_ev = imsg->MouseX;
            WORD my_ev = imsg->MouseY;
            ReplyMsg(reinterpret_cast<struct Message*>(imsg));
            if (cls == IDCMP_CLOSEWINDOW) {
                ui->running = 0;
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
                BeginRefresh(ui->win);
                am_redraw(ui);
                EndRefresh(ui->win, TRUE);
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
