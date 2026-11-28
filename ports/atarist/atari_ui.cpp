/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "atari_ui.hpp"
#include "atari_capture.hpp"
#include "oyk_logo_data.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ATARI__
#include <gem.h>
#include <osbind.h>
#endif

static void at_copy(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0U) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1U;
    if (n > 0U) memcpy(dst, src, n);
    dst[n] = '\0';
}

struct AtHitZone {
    int x;
    int y;
    int w;
    int h;
    int action;
};

struct OykAtariUiState {
    OykPortableNode*    node;
    OykAtariNetState*   net;
    short               ap_id;
    short               vdi;
    short               win_handle;
    short               modal_win_handle;
    int                 modal_scr;
    short               desk_x;
    short               desk_y;
    short               desk_w;
    short               desk_h;
    short               wx;
    short               wy;
    short               ww;
    short               wh;
    int                 scr_w;
    int                 scr_h;
    int                 colors;
    OykAtariDisplayTier tier;
    int                 active_screen;
    int                 settings_tab;
    int                 sel_iface;
    int                 sel_peer;
    int                 sel_conn;
    int                 sel_ban;
    int                 running;
    int                 hit_count;
    AtHitZone           hits[32];
    OykConfig           draft_cfg;
    OykGuiAddIfaceWizard wiz;
    OykGuiThemeCtrl     theme_ctrl;
    OykThemeConfig      os_native_pal;
    OykThemeConfig      active_theme;
    int                 console_draw_count;
    int                 modal_draw_count;
    oyk_u32             last_console_bg;
    oyk_u32             last_modal_bg;
    int                 pen_bg;
    int                 pen_fg;
    int                 pen_panel;
    int                 pen_shadow;
    int                 pen_accent;
    int                 pen_accent_fg;
    int                 pen_status_bg;
    int                 pen_status_fg;
    int                 pen_menu_bg;
    int                 pen_menu_fg;
    int                 char_w;
    int                 char_h;
    int                 capture_mode;
    char                main_title[64];
    char                modal_title[64];
    int                 menu_installed;
};

#ifdef __ATARI__
static OBJECT s_at_menu_tree[24];
static char   s_at_menu_node_lbl[12][32];

static void at_init_obj(int idx, int next, int head, int tail,
                        unsigned short type, unsigned short flags,
                        unsigned short state, long spec,
                        int x, int y, int w, int h) {
    memset(&s_at_menu_tree[idx], 0, sizeof(OBJECT));
    s_at_menu_tree[idx].ob_next   = static_cast<short>(next);
    s_at_menu_tree[idx].ob_head   = static_cast<short>(head);
    s_at_menu_tree[idx].ob_tail   = static_cast<short>(tail);
    s_at_menu_tree[idx].ob_type   = type;
    s_at_menu_tree[idx].ob_flags  = flags;
    s_at_menu_tree[idx].ob_state  = state;
    s_at_menu_tree[idx].ob_spec.index = spec;
    s_at_menu_tree[idx].ob_x      = static_cast<short>(x);
    s_at_menu_tree[idx].ob_y      = static_cast<short>(y);
    s_at_menu_tree[idx].ob_width  = static_cast<short>(w);
    s_at_menu_tree[idx].ob_height = static_cast<short>(h);
}

static void at_install_menus(OykAtariUiState* ui) {
    if (!ui || ui->ap_id < 0 || ui->menu_installed) return;
    const int cw = (ui->char_w > 0) ? ui->char_w : 8;
    const int ch = (ui->char_h > 0) ? ui->char_h : 16;
    const int bh = ch + 3;
    const int rh = ch + 2;
    const int sw = (ui->scr_w > 0) ? ui->scr_w : 640;
    const int sh = (ui->scr_h > 0) ? ui->scr_h : 400;
    const int n_cnt = oyk_gui_node_menu_count();
    const int node_box_w = 29 * cw;
    const int node_items = (n_cnt <= 11) ? n_cnt : 11;

    /* GEM AES menu OBJECT tree: 0=root, 1=bar, 2=active, 3..5=titles, 6=menus, 7..8=Desk, 9..20=Node, 21..22=Help */
    at_init_obj(0, -1,  1,  6, G_IBOX,  OF_NONE, OS_NORMAL, 0L, 0, 0, sw, sh);
    at_init_obj(1,  6,  2,  2, G_BOX,   OF_NONE, OS_NORMAL, 0x00001100L, 0, 0, sw, bh);
    at_init_obj(2,  1,  3,  5, G_IBOX,  OF_NONE, OS_NORMAL, 0L, 2 * cw, 0, 22 * cw, bh);
    at_init_obj(3,  4, -1, -1, G_TITLE, OF_NONE, OS_NORMAL, reinterpret_cast<long>(" Desk "), 0, 0, 6 * cw, bh);
    at_init_obj(4,  5, -1, -1, G_TITLE, OF_NONE, OS_NORMAL, reinterpret_cast<long>(" Node "), 6 * cw, 0, 6 * cw, bh);
    at_init_obj(5,  2, -1, -1, G_TITLE, OF_NONE, OS_NORMAL, reinterpret_cast<long>(" Help "), 12 * cw, 0, 6 * cw, bh);
    at_init_obj(6,  0,  7, 21, G_IBOX,  OF_NONE, OS_NORMAL, 0L, 0, bh, sw, sh - bh);

    /* Desk dropdown */
    at_init_obj(7,  9,  8,  8, G_BOX,   OF_NONE, OS_NORMAL, 0x00FF1100L, 2 * cw, 0, 22 * cw, rh);
    at_init_obj(8,  7, -1, -1, G_STRING, OF_NONE, OS_NORMAL, reinterpret_cast<long>("  About OYK-Node...  "), 0, 0, 22 * cw, rh);

    /* Node dropdown (11 items at indices 10..20) */
    at_init_obj(9, 21, 10, 10 + node_items - 1, G_BOX, OF_NONE, OS_NORMAL, 0x00FF1100L, 8 * cw, 0, node_box_w, node_items * rh);
    for (int i = 0; i < node_items; ++i) {
        const OykGuiMenuItem* mi = oyk_gui_node_menu_item(i);
        if (mi && !mi->is_separator) {
            snprintf(s_at_menu_node_lbl[i], sizeof(s_at_menu_node_lbl[i]), "  %s", mi->label);
        } else {
            at_copy(s_at_menu_node_lbl[i], sizeof(s_at_menu_node_lbl[i]), "----------------------------");
        }
        int next_idx = (i + 1 < node_items) ? (10 + i + 1) : 9;
        at_init_obj(10 + i, next_idx, -1, -1, G_STRING, OF_NONE,
                    (mi && mi->is_separator) ? OS_DISABLED : OS_NORMAL,
                    reinterpret_cast<long>(s_at_menu_node_lbl[i]),
                    0, i * rh, node_box_w, rh);
    }

    /* Help dropdown */
    at_init_obj(21, 6, 22, 22, G_BOX, OF_NONE, OS_NORMAL, 0x00FF1100L, 14 * cw, 0, 22 * cw, rh);
    at_init_obj(22, 21, -1, -1, G_STRING, OF_LASTOB, OS_NORMAL, reinterpret_cast<long>("  About OYK-Node...  "), 0, 0, 22 * cw, rh);

    menu_bar(s_at_menu_tree, MENU_INSTALL);
    ui->menu_installed = 1;
}

static void at_remove_menus(OykAtariUiState* ui) {
    if (!ui || !ui->menu_installed || ui->ap_id < 0) return;
    menu_bar(s_at_menu_tree, MENU_REMOVE);
    ui->menu_installed = 0;
}
#endif

void oyk_atari_query_os_native_theme(short vdi_handle, int colors, OykThemeConfig* out_pal) {
    if (!out_pal) return;
    oyk_theme_init_builtin(out_pal, 1);
    at_copy(out_pal->name, sizeof(out_pal->name), "native");
    out_pal->is_dark_base = 0;

    oyk_u32 c_white = 0xFFFFFFUL;
    oyk_u32 c_black = 0x000000UL;
    oyk_u32 c_grey  = (colors <= 2) ? 0xFFFFFFUL : 0xCCCCCCUL;
    oyk_u32 c_blue  = (colors <= 2) ? 0x000000UL : 0x0055CCUL;

#ifdef __ATARI__
    if (vdi_handle > 0) {
        short rgb[3] = {1000, 1000, 1000};
        vq_color(vdi_handle, 0, 0, rgb);
        c_white = oyk_atari_vdi_to_rgb24(rgb[0], rgb[1], rgb[2]);
        vq_color(vdi_handle, 1, 0, rgb);
        c_black = oyk_atari_vdi_to_rgb24(rgb[0], rgb[1], rgb[2]);
        if (c_black == c_white) {
            c_black = (c_white == 0UL) ? 0xFFFFFFUL : 0x000000UL;
        }
        if (colors <= 2) {
            c_grey = c_white;
            c_blue = c_black;
        } else if (colors >= 16) {
            vq_color(vdi_handle, 8, 0, rgb);
            c_grey = oyk_atari_vdi_to_rgb24(rgb[0], rgb[1], rgb[2]);
            vq_color(vdi_handle, 4, 0, rgb);
            c_blue = oyk_atari_vdi_to_rgb24(rgb[0], rgb[1], rgb[2]);
        }
    }
#else
    (void)vdi_handle;
#endif

    out_pal->colors[OYK_TR_BG]           = c_white;
    out_pal->colors[OYK_TR_FG]           = c_black;
    out_pal->colors[OYK_TR_BORDER]       = c_black;
    out_pal->colors[OYK_TR_HEADER_BG]    = c_blue;
    out_pal->colors[OYK_TR_HEADER_FG]    = c_white;
    out_pal->colors[OYK_TR_STATUS_BG]    = c_blue;
    out_pal->colors[OYK_TR_STATUS_FG]    = c_white;
    out_pal->colors[OYK_TR_PANEL_BG]     = c_grey;
    out_pal->colors[OYK_TR_PANEL_FG]     = c_black;
    out_pal->colors[OYK_TR_SELECTION_BG] = c_blue;
    out_pal->colors[OYK_TR_SELECTION_FG] = c_white;
    out_pal->colors[OYK_TR_INPUT_BG]     = c_white;
    out_pal->colors[OYK_TR_INPUT_FG]     = c_black;
    out_pal->colors[OYK_TR_BUTTON_BG]    = c_grey;
    out_pal->colors[OYK_TR_BUTTON_FG]    = c_black;
    out_pal->colors[OYK_TR_ACCENT]       = c_blue;
}

static void at_apply_palette(OykAtariUiState* ui) {
    if (!ui) return;
    if (ui->tier == OYK_ATARI_MONO || ui->colors <= 2) {
        /* 1-bit ST High: pen 0=white, pen 1=black; invert cleanly for dark-base theme preview */
        if (ui->active_theme.is_dark_base) {
            ui->pen_bg        = 1;
            ui->pen_fg        = 0;
            ui->pen_panel     = 1;
            ui->pen_shadow    = 0;
            ui->pen_accent    = 0;
            ui->pen_accent_fg = 1;
            ui->pen_status_bg = 0;
            ui->pen_status_fg = 1;
            ui->pen_menu_bg   = 0;
            ui->pen_menu_fg   = 1;
        } else {
            ui->pen_bg        = 0;
            ui->pen_fg        = 1;
            ui->pen_panel     = 0;
            ui->pen_shadow    = 1;
            ui->pen_accent    = 1;
            ui->pen_accent_fg = 0;
            ui->pen_status_bg = 1;
            ui->pen_status_fg = 0;
            ui->pen_menu_bg   = 0;
            ui->pen_menu_fg   = 1;
        }
        return;
    }

    if (strcmp(ui->theme_ctrl.active_name, "native") == 0) {
        ui->pen_bg        = 0;
        ui->pen_fg        = 1;
        ui->pen_panel     = 8;
        ui->pen_shadow    = 9;
        ui->pen_accent    = 4;
        ui->pen_accent_fg = 0;
        ui->pen_status_bg = 4;
        ui->pen_status_fg = 0;
        ui->pen_menu_bg   = 0;
        ui->pen_menu_fg   = 1;
    } else {
        /* Dynamic VDI color registers 2..9 for full-screen live preview on 16-color VDI */
        ui->pen_bg        = 2;
        ui->pen_fg        = 3;
        ui->pen_panel     = 4;
        ui->pen_accent    = 5;
        ui->pen_accent_fg = 6;
        ui->pen_status_bg = 7;
        ui->pen_status_fg = 6;
        ui->pen_menu_bg   = 4;
        ui->pen_menu_fg   = 3;
        ui->pen_shadow    = 9;
#ifdef __ATARI__
        if (ui->vdi > 0) {
            short rgb[3];
            oyk_atari_rgb24_to_vdi(ui->active_theme.colors[OYK_TR_BG], &rgb[0], &rgb[1], &rgb[2]);
            vs_color(ui->vdi, 2, rgb);
            oyk_atari_rgb24_to_vdi(ui->active_theme.colors[OYK_TR_FG], &rgb[0], &rgb[1], &rgb[2]);
            vs_color(ui->vdi, 3, rgb);
            oyk_atari_rgb24_to_vdi(ui->active_theme.colors[OYK_TR_PANEL_BG], &rgb[0], &rgb[1], &rgb[2]);
            vs_color(ui->vdi, 4, rgb);
            oyk_atari_rgb24_to_vdi(ui->active_theme.colors[OYK_TR_ACCENT], &rgb[0], &rgb[1], &rgb[2]);
            vs_color(ui->vdi, 5, rgb);
            oyk_atari_rgb24_to_vdi(ui->active_theme.colors[OYK_TR_SELECTION_FG], &rgb[0], &rgb[1], &rgb[2]);
            vs_color(ui->vdi, 6, rgb);
            oyk_atari_rgb24_to_vdi(ui->active_theme.colors[OYK_TR_STATUS_BG], &rgb[0], &rgb[1], &rgb[2]);
            vs_color(ui->vdi, 7, rgb);
        }
#endif
    }
}

static void at_fill_rect(const OykAtariUiState* ui, int x, int y, int w, int h, int pen) {
#ifdef __ATARI__
    if (!ui || ui->vdi <= 0 || w <= 0 || h <= 0) return;
    short pxy[4];
    pxy[0] = static_cast<short>(x);
    pxy[1] = static_cast<short>(y);
    pxy[2] = static_cast<short>(x + w - 1);
    pxy[3] = static_cast<short>(y + h - 1);
    vsf_interior(ui->vdi, FIS_SOLID);
    vsf_color(ui->vdi, static_cast<short>(pen));
    v_bar(ui->vdi, pxy);
#else
    (void)ui; (void)x; (void)y; (void)w; (void)h; (void)pen;
#endif
}

static void at_frame_rect(const OykAtariUiState* ui, int x, int y, int w, int h, int pen) {
#ifdef __ATARI__
    if (!ui || ui->vdi <= 0 || w <= 1 || h <= 1) return;
    short pts[10];
    pts[0] = static_cast<short>(x);         pts[1] = static_cast<short>(y);
    pts[2] = static_cast<short>(x + w - 1); pts[3] = static_cast<short>(y);
    pts[4] = static_cast<short>(x + w - 1); pts[5] = static_cast<short>(y + h - 1);
    pts[6] = static_cast<short>(x);         pts[7] = static_cast<short>(y + h - 1);
    pts[8] = static_cast<short>(x);         pts[9] = static_cast<short>(y);
    vsl_color(ui->vdi, static_cast<short>(pen));
    v_pline(ui->vdi, 5, pts);
#else
    (void)ui; (void)x; (void)y; (void)w; (void)h; (void)pen;
#endif
}

static void at_hline(const OykAtariUiState* ui, int x1, int x2, int y, int pen) {
#ifdef __ATARI__
    if (!ui || ui->vdi <= 0) return;
    short pts[4];
    pts[0] = static_cast<short>(x1); pts[1] = static_cast<short>(y);
    pts[2] = static_cast<short>(x2); pts[3] = static_cast<short>(y);
    vsl_color(ui->vdi, static_cast<short>(pen));
    v_pline(ui->vdi, 2, pts);
#else
    (void)ui; (void)x1; (void)x2; (void)y; (void)pen;
#endif
}

static void at_draw_text(const OykAtariUiState* ui, int x, int y, const char* s,
                         int fg_pen, int max_chars) {
#ifdef __ATARI__
    if (!ui || ui->vdi <= 0 || !s || !s[0] || max_chars <= 0) return;
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
    short ho = 0, vo = 0;
    vswr_mode(ui->vdi, MD_TRANS);
    vst_alignment(ui->vdi, 0, 5, &ho, &vo);
    vst_color(ui->vdi, static_cast<short>(fg_pen));
    v_gtext(ui->vdi, static_cast<short>(x), static_cast<short>(y), buf);
    vswr_mode(ui->vdi, MD_REPLACE);
#else
    (void)ui; (void)x; (void)y; (void)s; (void)fg_pen; (void)max_chars;
#endif
}

static void at_reset_hits(OykAtariUiState* ui) {
    if (ui) ui->hit_count = 0;
}

static void at_add_hit(OykAtariUiState* ui, int x, int y, int w, int h, int action) {
    if (!ui || ui->hit_count >= 32 || w <= 0 || h <= 0) return;
    AtHitZone& z = ui->hits[ui->hit_count++];
    z.x = x;
    z.y = y;
    z.w = w;
    z.h = h;
    z.action = action;
}

static void at_draw_checkbox(const OykAtariUiState* ui, int x, int y, int checked,
                             const char* label, int label_pen, int max_chars) {
    int ch = (ui && ui->char_h > 0) ? ui->char_h : 16;
    int sz = oyk_gui_control_box_size(ch);
    int top_pad = (sz < ch) ? ((ch - sz) / 2) : 0;
    int box_y = y + top_pad;
    at_fill_rect(ui, x, box_y, sz, sz, ui->pen_bg);
    at_frame_rect(ui, x, box_y, sz, sz, ui->pen_fg);
#ifdef __ATARI__
    if (checked && ui && ui->vdi > 0) {
        short pts[6];
        pts[0] = static_cast<short>(x + 2);
        pts[1] = static_cast<short>(box_y + sz / 2);
        pts[2] = static_cast<short>(x + sz / 2 - 1);
        pts[3] = static_cast<short>(box_y + sz - 3);
        pts[4] = static_cast<short>(x + sz - 2);
        pts[5] = static_cast<short>(box_y + 2);
        vsl_color(ui->vdi, static_cast<short>(ui->pen_accent));
        v_pline(ui->vdi, 3, pts);
        if (sz >= 12) {
            pts[1] += 1;
            pts[3] += 1;
            pts[5] += 1;
            v_pline(ui->vdi, 3, pts);
        }
    }
#else
    (void)checked;
#endif
    if (label && label[0]) {
        int ty = y + oyk_gui_vertical_text_offset(sz, ch);
        at_draw_text(ui, x + sz + 6, ty, label, label_pen, max_chars);
    }
}

static void at_draw_radio(const OykAtariUiState* ui, int x, int y, int selected,
                          const char* label, int label_pen, int max_chars) {
    int ch = (ui && ui->char_h > 0) ? ui->char_h : 16;
    int sz = oyk_gui_control_box_size(ch);
    int box_y = y + ((sz < ch) ? ((ch - sz) / 2) : 0);
#ifdef __ATARI__
    if (ui && ui->vdi > 0) {
        short r = static_cast<short>(sz / 2);
        short cx = static_cast<short>(x + r);
        short cy = static_cast<short>(box_y + r);
        vsf_interior(ui->vdi, FIS_SOLID);
        vsf_color(ui->vdi, static_cast<short>(ui->pen_fg));
        v_circle(ui->vdi, cx, cy, r);
        vsf_color(ui->vdi, static_cast<short>(ui->pen_bg));
        v_circle(ui->vdi, cx, cy, static_cast<short>(r > 2 ? r - 1 : 1));
        if (selected) {
            vsf_color(ui->vdi, static_cast<short>(ui->pen_accent));
            v_circle(ui->vdi, cx, cy, static_cast<short>(r > 3 ? r - 3 : 2));
        }
    }
#else
    (void)selected;
#endif
    if (label && label[0]) {
        int ty = y + oyk_gui_vertical_text_offset(sz, ch);
        at_draw_text(ui, x + sz + 6, ty, label, label_pen, max_chars);
    }
}

static void at_draw_field_row(const OykAtariUiState* ui, int x, int y, const char* label,
                              const char* value, int label_w, int box_w) {
    int ch = (ui && ui->char_h > 0) ? ui->char_h : 16;
    int fh = (ch <= 10) ? oyk_gui_control_height(ch, 3) : oyk_gui_control_height(ch, 1);
    int toff = oyk_gui_vertical_text_offset(fh, ch);
    if (label && label[0]) {
        at_draw_text(ui, x, y + toff, label, ui->pen_fg, label_w / 8);
    }
    int bx = x + label_w;
    at_fill_rect(ui, bx, y, box_w, fh, ui->pen_bg);
    at_frame_rect(ui, bx, y, box_w, fh, ui->pen_fg);
    if (value && value[0]) {
        at_draw_text(ui, bx + 5, y + toff, value, ui->pen_fg, (box_w - 10) / 8);
    }
}

static void at_draw_box(const OykAtariUiState* ui, int x, int y, int w, int h, const char* title) {
    int ch = (ui && ui->char_h > 0) ? ui->char_h : 16;
    int hh = (ch <= 10) ? oyk_gui_control_height(ch, 3) : oyk_gui_control_height(ch, 1);
    int toff = oyk_gui_vertical_text_offset(hh, ch);
    at_fill_rect(ui, x, y, w, h, ui->pen_panel);
    at_frame_rect(ui, x, y, w, h, ui->pen_fg);
    at_fill_rect(ui, x + 1, y + 1, w - 2, hh, ui->pen_accent);
    at_hline(ui, x, x + w - 1, y + hh + 1, ui->pen_fg);
    if (title && title[0]) {
        at_draw_text(ui, x + 8, y + 1 + toff, title, ui->pen_accent_fg, (w - 16) / 8);
    }
}

static void at_draw_button(const OykAtariUiState* ui, int x, int y, int w, int h,
                           const char* label, int is_primary) {
    int ch = (ui && ui->char_h > 0) ? ui->char_h : 16;
    int eff_h = (ch <= 10 && h > 18) ? oyk_gui_control_height(ch, 3) : h;
    int y_off = (eff_h < h) ? ((h - eff_h) / 2) : 0;
    int toff = oyk_gui_vertical_text_offset(eff_h, ch);
    int bg = is_primary ? ui->pen_accent : ui->pen_panel;
    int fg = is_primary ? ui->pen_accent_fg : ui->pen_fg;
    at_fill_rect(ui, x, y + y_off, w, eff_h, bg);
    at_frame_rect(ui, x, y + y_off, w, eff_h, ui->pen_fg);
    if (label && label[0]) {
        at_draw_text(ui, x + 8, y + y_off + toff, label, fg, (w - 14) / 8);
    }
}

static void at_draw_listview_header(const OykAtariUiState* ui, int x, int y, int w,
                                    int ncols, const int* col_x, const char* const* cols,
                                    int bg, int fg) {
    int ch = (ui && ui->char_h > 0) ? ui->char_h : 16;
    int rh = oyk_gui_control_height(ch, (ch <= 10) ? 2 : 1);
    int toff = oyk_gui_vertical_text_offset(rh, ch);
    at_fill_rect(ui, x, y, w, rh, bg);
    for (int c = 0; c < ncols; ++c) {
        if (cols[c] && cols[c][0]) {
            at_draw_text(ui, col_x[c], y + toff, cols[c], fg, 24);
        }
    }
}

static void at_draw_listview_row(const OykAtariUiState* ui, int x, int y, int w, int h,
                                 int selected, int ncols, const int* col_x,
                                 const char* const* cells, const int* max_c,
                                 int norm_bg, int norm_fg) {
    int ch = (ui && ui->char_h > 0) ? ui->char_h : 16;
    int toff = oyk_gui_vertical_text_offset(h, ch);
    int bg = norm_bg;
    int fg = norm_fg;
    if (selected) {
        bg = (ui->colors <= 2) ? ui->pen_fg : ui->pen_accent;
        fg = (ui->colors <= 2) ? ui->pen_bg : ui->pen_accent_fg;
    }
    at_fill_rect(ui, x, y, w, h, bg);
    for (int c = 0; c < ncols; ++c) {
        if (cells[c] && cells[c][0]) {
            at_draw_text(ui, col_x[c], y + toff, cells[c], fg, max_c[c]);
        }
    }
}

static void at_draw_console_body(OykAtariUiState* ui) {
    OykPortableNode* node = ui->node;
    const OykConfig& cfg = node->config();
    int ch = (ui->char_h > 0) ? ui->char_h : 16;
    int row_step = (ch <= 10) ? 13 : 17;
    int hdr_h = (ch <= 10) ? oyk_gui_control_height(ch, 3) : oyk_gui_control_height(ch, 1);
    int ban_h = (ch <= 10) ? 18 : 22;
    int ban_off = oyk_gui_vertical_text_offset(ban_h, ch);
    int pill_h = ban_h - 4;
    int pill_off = oyk_gui_vertical_text_offset(pill_h, ch);
    int cx = (ui->ww > 0) ? ui->wx : 0;
    int cy = (ui->wh > 0) ? ui->wy : 0;
    int w  = (ui->ww > 0) ? ui->ww : ui->scr_w;
    int h  = (ui->wh > 0) ? ui->wh : ui->scr_h;
    int top = cy + 4;

    at_fill_rect(ui, cx, cy, w, h, ui->pen_bg);
    if (ui->win_handle < 0) {
        at_fill_rect(ui, cx, cy, w, 19, ui->pen_menu_bg);
        at_hline(ui, cx, cx + w - 1, cy + 19, ui->pen_fg);
        at_draw_text(ui, cx + 10, cy + oyk_gui_vertical_text_offset(19, ch),
                     "Desk  File  Edit  Node  View  Window  Help",
                     ui->pen_menu_fg, 46);
        {
            char rstat[32];
            sprintf(rstat, "%s", (ui->tier == OYK_ATARI_MONO) ? "ST-HIGH (Mono)" : "VDI-16 Color");
            at_draw_text(ui, cx + w - 124, cy + oyk_gui_vertical_text_offset(19, ch),
                         rstat, ui->pen_menu_fg, 14);
        }
        top = cy + 23;
    }

    /* Status banner with canonical header + right incoming badge */
    char cs[16], al[16], hdr[128];
    oyk_format_callsign(&cfg.callsign, cs, sizeof(cs));
    oyk_format_callsign(&cfg.alias, al, sizeof(al));
    sprintf(hdr, "OYK PACKET NODE: %s (%s) | Up: 0s | Conns: %d",
            cs, al, node->total_active_connections());
    const char* pill = oyk_gui_format_incoming_pill(node->accept_incoming(), 0);
    int pill_w = static_cast<int>(strlen(pill)) * 8 + 10;
    int bx = cx + 8;
    int bw = w - 16;
    at_fill_rect(ui, bx, top, bw, ban_h, ui->pen_status_bg);
    at_frame_rect(ui, bx, top, bw, ban_h, ui->pen_fg);
    at_draw_text(ui, bx + 6, top + ban_off, hdr, ui->pen_status_fg, (bw - pill_w - 20) / 8);
    at_fill_rect(ui, bx + bw - 4 - pill_w, top + 2, pill_w, pill_h, ui->pen_panel);
    at_frame_rect(ui, bx + bw - 4 - pill_w, top + 2, pill_w, pill_h, ui->pen_fg);
    at_draw_text(ui, bx + bw + 1 - pill_w, top + 2 + pill_off, pill, ui->pen_fg,
                 static_cast<int>(strlen(pill)));

    /* Side-by-side Interfaces & Last 5 Heard */
    int half = (bw - 6) / 2;
    int y0 = top + ban_h + 5;
    int top_h = (h >= 380) ? 104 : 84;
    at_draw_box(ui, bx, y0, half, top_h, "Interfaces");
    {
        int if_x[4] = { bx + 6, bx + 72, bx + 168, bx + 220 };
        const char* if_h[4] = { "Port", "Type", "State", "RX/TX" };
        int if_w[4] = { 8, 11, 5, 10 };
        int hy = y0 + hdr_h + 4;
        at_draw_listview_header(ui, bx + 2, hy, half - 4, 4, if_x, if_h, ui->pen_panel, ui->pen_fg);
        int ry0 = hy + hdr_h + 2;
        int max_top = (y0 + top_h - ry0) / row_step;
        if (max_top < 2) max_top = 2;
        for (int i = 0; i < cfg.iface_count && i < max_top; ++i) {
            const OykIfaceRuntime& rt = node->iface(i);
            char rxtx[20];
            sprintf(rxtx, "%lu/%lu",
                    static_cast<unsigned long>(rt.rx_frames),
                    static_cast<unsigned long>(rt.tx_frames));
            const char* cells[4] = { cfg.ifaces[i].id, cfg.ifaces[i].type, rt.running ? "UP" : "DN", rxtx };
            at_draw_listview_row(ui, bx + 2, ry0 + i * row_step, half - 4, row_step - 1,
                                 0, 4, if_x, cells, if_w, ui->pen_panel, ui->pen_fg);
        }
    }

    int rx = bx + half + 6;
    at_draw_box(ui, rx, y0, half, top_h, "Last 5 New Callsigns Heard");
    {
        int hd_x[4] = { rx + 6, rx + 30, rx + 112, rx + 180 };
        const char* hd_h[4] = { "#", "Callsign", "Port", "Heard" };
        int hd_w[4] = { 3, 10, 8, 10 };
        int hy = y0 + hdr_h + 4;
        at_draw_listview_header(ui, rx + 2, hy, half - 4, 4, hd_x, hd_h, ui->pen_panel, ui->pen_fg);
        int ry0 = hy + hdr_h + 2;
        int max_top = (y0 + top_h - ry0) / row_step;
        if (max_top < 2) max_top = 2;
        for (int i = 0; i < node->new_heard_count() && i < max_top; ++i) {
            const OykNewHeard& he = node->new_heard(i);
            char idx_s[16];
            sprintf(idx_s, "%d", i + 1);
            const char* cells[4] = { idx_s, he.callsign, he.iface_id, he.time_hms };
            at_draw_listview_row(ui, rx + 2, ry0 + i * row_step, half - 4, row_step - 1,
                                 0, 4, hd_x, cells, hd_w, ui->pen_panel, ui->pen_fg);
        }
    }

    /* Active Connections */
    int conn_y = y0 + top_h + 6;
    int ch_box = (h >= 380) ? 102 : 84;
    at_draw_box(ui, bx, conn_y, bw, ch_box, "Active Connections");
    {
        int cn_x[6] = { bx + 6, bx + 42, bx + 132, bx + 210, bx + 266, bx + 362 };
        const char* cn_h[6] = { "ID", "Callsign", "Port", "Dir", "State", "Since" };
        int cn_w[6] = { 4, 10, 9, 6, 11, 10 };
        int hy = conn_y + hdr_h + 4;
        at_draw_listview_header(ui, bx + 2, hy, bw - 4, 6, cn_x, cn_h, ui->pen_panel, ui->pen_fg);
        int ry0 = hy + hdr_h + 2;
        int max_conn = (conn_y + ch_box - ry0) / row_step;
        if (max_conn < 2) max_conn = 2;
        int shown_c = 0;
        for (int i = 0; i < node->max_sessions() && shown_c < max_conn; ++i) {
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
            at_draw_listview_row(ui, bx + 2, ry0 + shown_c * row_step, bw - 4, row_step - 1,
                                 0, 6, cn_x, cells, cn_w, ui->pen_panel, ui->pen_fg);
            ++shown_c;
        }
    }

    /* Node Activity Log */
    int ly = conn_y + ch_box + 6;
    int btn_h = oyk_gui_control_height(ch, 3);
    int bar_y = cy + h - btn_h - 4;
    int lh = bar_y - ly - 6;
    if (lh > 44) {
        at_draw_box(ui, bx, ly, bw, lh, "Node Activity Log");
        int ry0 = ly + hdr_h + 4;
        int max_r = (ly + lh - ry0 - 2) / row_step;
        int total = node->console_log_count();
        int start = (total > max_r) ? (total - max_r) : 0;
        for (int i = start; i < total; ++i) {
            const char* ln = node->console_log_line(i);
            if (ln) at_draw_text(ui, bx + 6, ry0 + (i - start) * row_step, ln, ui->pen_fg, (bw - 16) / 8);
        }
    }

    at_draw_button(ui, bx,       bar_y, 88, 22, "Terminal", 1); at_add_hit(ui, bx,       bar_y, 88, 22, OYK_SCR_TERMINAL);
    at_draw_button(ui, bx + 94,  bar_y, 68, 22, "Files",    0); at_add_hit(ui, bx + 94,  bar_y, 68, 22, OYK_SCR_FILES);
    at_draw_button(ui, bx + 168, bar_y, 88, 22, "Settings", 0); at_add_hit(ui, bx + 168, bar_y, 88, 22, OYK_SCR_SETTINGS);
    at_draw_text(ui, bx + 268, bar_y + oyk_gui_vertical_text_offset(btn_h, ch),
                 ui->net ? ui->net->caps.backend_summary : "ATARI GEM",
                 ui->pen_fg, (bw - 274) / 8);
}

static int at_swatch_pen_for_role(const OykAtariUiState* ui, int role_idx) {
    if (!ui) return 1;
    oyk_u32 c = ui->active_theme.colors[role_idx] & 0xFFFFFFUL;
    if (ui->colors <= 2) {
        unsigned int r = static_cast<unsigned int>((c >> 16) & 0xFFUL);
        unsigned int g = static_cast<unsigned int>((c >> 8) & 0xFFUL);
        unsigned int b = static_cast<unsigned int>(c & 0xFFUL);
        unsigned int lum = (r * 30U + g * 59U + b * 11U) / 100U;
        return (lum < 128U) ? 1 : 0;
    }
    if (c == (ui->active_theme.colors[OYK_TR_BG] & 0xFFFFFFUL)) return ui->pen_bg;
    if (c == (ui->active_theme.colors[OYK_TR_FG] & 0xFFFFFFUL)) return ui->pen_fg;
    if (c == (ui->active_theme.colors[OYK_TR_PANEL_BG] & 0xFFFFFFUL)) return ui->pen_panel;
    if (c == (ui->active_theme.colors[OYK_TR_ACCENT] & 0xFFFFFFUL)) return ui->pen_accent;
    if (c == (ui->active_theme.colors[OYK_TR_STATUS_BG] & 0xFFFFFFUL)) return ui->pen_status_bg;
    if (c == (ui->active_theme.colors[OYK_TR_STATUS_FG] & 0xFFFFFFUL)) return ui->pen_status_fg;
    return (c == 0xFFFFFFUL) ? 0 : ((c == 0x000000UL) ? 1 : (2 + (role_idx % 12)));
}

static void at_sync_modal_window(OykAtariUiState* ui) {
    if (!ui) return;
#ifdef __ATARI__
    if (ui->ap_id < 0) return;
    int scr = ui->active_screen;
    if (scr == OYK_SCR_MAIN || scr == OYK_SCR_MENU) {
        if (ui->modal_win_handle >= 0) {
            wind_close(ui->modal_win_handle);
            wind_delete(ui->modal_win_handle);
            ui->modal_win_handle = -1;
            ui->modal_scr = OYK_SCR_MAIN;
        }
        return;
    }
    if (ui->modal_win_handle >= 0 && ui->modal_scr == scr) {
        return;
    }
    if (ui->modal_win_handle >= 0) {
        wind_close(ui->modal_win_handle);
        wind_delete(ui->modal_win_handle);
        ui->modal_win_handle = -1;
        ui->modal_scr = OYK_SCR_MAIN;
    }
    short mw = static_cast<short>(ui->desk_w - 56);
    short mh = static_cast<short>(ui->desk_h - 40);
    if (mw < 480) mw = ui->desk_w - 16;
    if (mh < 260) mh = ui->desk_h - 16;
    short mx = static_cast<short>(ui->desk_x + (ui->desk_w - mw) / 2);
    short my = static_cast<short>(ui->desk_y + (ui->desk_h - mh) / 2);
    const char* raw_title = oyk_gui_screen_title(scr);
    if (scr == OYK_SCR_PEERS || scr == OYK_SCR_THEME) {
        raw_title = oyk_gui_screen_title(OYK_SCR_SETTINGS);
    }
    sprintf(ui->modal_title, " %s ", raw_title ? raw_title : "Dialog");
    ui->modal_win_handle = wind_create(NAME | CLOSER | MOVER,
                                       ui->desk_x, ui->desk_y, ui->desk_w, ui->desk_h);
    if (ui->modal_win_handle >= 0) {
        wind_set_str(ui->modal_win_handle, WF_NAME, ui->modal_title);
        wind_open(ui->modal_win_handle, mx, my, mw, mh);
        ui->modal_scr = scr;
    }
#endif
}

static void at_draw_modal_body(OykAtariUiState* ui) {
    int scr = ui->active_screen;
    int mx = 36, my = 30;
    int mw = ui->scr_w - 72;
    int mh = ui->scr_h - 68;
    const char* title = oyk_gui_screen_title(scr);
    if (scr == OYK_SCR_PEERS || scr == OYK_SCR_THEME) {
        title = oyk_gui_screen_title(OYK_SCR_SETTINGS);
    }

    int lx = mx + 12;
    int ty = my + 28;
    int bw = mw - 24;
    int by = my + mh - 30;

    if (ui->modal_win_handle >= 0) {
#ifdef __ATARI__
        short mwx = 0, mwy = 0, mww = 0, mwh = 0;
        wind_get(ui->modal_win_handle, WF_WORKXYWH, &mwx, &mwy, &mww, &mwh);
        if (mww > 0 && mwh > 0) {
            mx = mwx;
            my = mwy;
            mw = mww;
            mh = mwh;
        }
#endif
        at_fill_rect(ui, mx, my, mw, mh, ui->pen_panel);
        lx = mx + 10;
        ty = my + 6;
        bw = mw - 20;
        by = my + mh - 28;
    } else {
        at_fill_rect(ui, mx + 5, my + 4, mw, mh, ui->pen_shadow);
        at_fill_rect(ui, mx, my, mw, mh, ui->pen_panel);
        at_frame_rect(ui, mx, my, mw, mh, ui->pen_fg);
        at_fill_rect(ui, mx + 1, my + 1, mw - 2, 20, ui->pen_accent);
        at_hline(ui, mx, mx + mw - 1, my + 21, ui->pen_fg);
        at_draw_text(ui, mx + 10, my + 2, title, ui->pen_accent_fg, (mw - 48) / 8);
        at_fill_rect(ui, mx + mw - 32, my + 2, 29, 17, ui->pen_panel);
        at_frame_rect(ui, mx + mw - 32, my + 2, 29, 17, ui->pen_fg);
        at_draw_text(ui, mx + mw - 29, my + 2, "[X]", ui->pen_fg, 3);
        at_add_hit(ui, mx + mw - 32, my + 2, 29, 17, 999);
    }

    const OykConfig& cfg = ui->node->config();
    char cs[16], al[16], buf[128], nbuf[32];

    int ch = (ui->char_h > 0) ? ui->char_h : 16;
    int tab_h = oyk_gui_control_height(ch, (ch <= 10) ? 3 : 2);
    int tab_off = oyk_gui_vertical_text_offset(tab_h, ch);
    int row_h = (ch <= 10) ? 14 : 18;

    if (scr == OYK_SCR_MENU) {
        int mmx = (ui->ww > 0 ? ui->wx : 0) + 8;
        int mmy = (ui->wh > 0 ? ui->wy : 19) + 2;
        int mmw = 248;
        int mstep = (ch <= 10) ? 14 : 18;
        int mmh = 11 * mstep + 2 * 8 + 8;
        int my_pos = mmy + 4;
        at_fill_rect(ui, mmx + 4, mmy + 4, mmw, mmh, ui->pen_shadow);
        at_fill_rect(ui, mmx, mmy, mmw, mmh, ui->pen_panel);
        at_frame_rect(ui, mmx, mmy, mmw, mmh, ui->pen_fg);
        for (int i = 0; i < oyk_gui_node_menu_count(); ++i) {
            const OykGuiMenuItem* mi = oyk_gui_node_menu_item(i);
            if (!mi) continue;
            if (mi->is_separator) {
                at_hline(ui, mmx + 3, mmx + mmw - 4, my_pos + 3, ui->pen_fg);
                my_pos += 8;
            } else {
                int hl = (i == 0);
                char mrow[64];
                if (mi->shortcut_key) sprintf(mrow, "[%c] %s", mi->shortcut_key, mi->label);
                else sprintf(mrow, "    %s", mi->label);
                if (hl) {
                    int hbg = (ui->colors <= 2) ? ui->pen_fg : ui->pen_accent;
                    int hfg = (ui->colors <= 2) ? ui->pen_bg : ui->pen_accent_fg;
                    at_fill_rect(ui, mmx + 2, my_pos, mmw - 4, mstep, hbg);
                    at_draw_text(ui, mmx + 8, my_pos + oyk_gui_vertical_text_offset(mstep, ch),
                                 mrow, hfg, 32);
                } else {
                    at_draw_text(ui, mmx + 8, my_pos + oyk_gui_vertical_text_offset(mstep, ch),
                                 mrow, ui->pen_fg, 32);
                }
                my_pos += mstep;
            }
        }
        return;
    }

    if (scr == OYK_SCR_SETTINGS || scr == OYK_SCR_PEERS || scr == OYK_SCR_THEME) {
        int active_tab = (scr == OYK_SCR_PEERS) ? 4 : ((scr == OYK_SCR_THEME) ? 7 : ui->settings_tab);
        int tx = lx;
        int tw = bw / 8;
        for (int t = 0; t < oyk_gui_settings_tab_count(); ++t) {
            int sel = (t == active_tab);
            const char* tt = oyk_gui_settings_tab_title(t);
            if (strcmp(tt, "Interfaces") == 0) tt = "Ifaces";
            else if (strcmp(tt, "Printer") == 0) tt = "Prn";
            else if (strcmp(tt, "Theme") == 0 && tw < 68) tt = "Thm";
            sprintf(buf, "%d:%s", t + 1, tt);
            int tbg = sel ? ((ui->colors <= 2) ? ui->pen_fg : ui->pen_accent) : ui->pen_bg;
            int tfg = sel ? ((ui->colors <= 2) ? ui->pen_bg : ui->pen_accent_fg) : ui->pen_fg;
            at_fill_rect(ui, tx, ty, tw - 3, tab_h, tbg);
            at_frame_rect(ui, tx, ty, tw - 3, tab_h, ui->pen_fg);
            at_draw_text(ui, tx + 3, ty + tab_off, buf, tfg, (tw - 6) / 8);
            at_add_hit(ui, tx, ty, tw - 3, tab_h, 100 + t);
            tx += tw;
        }
        at_hline(ui, lx, mx + mw - 12, ty + tab_h + 4, ui->pen_fg);

        if (active_tab == 0) {
            oyk_format_callsign(&cfg.callsign, cs, sizeof(cs));
            oyk_format_callsign(&cfg.alias, al, sizeof(al));
            at_draw_field_row(ui, lx, ty + 32, "Callsign:", cs, 96, 130);
            at_draw_field_row(ui, lx + 248, ty + 32, "Alias:", al, 64, 130);
            at_draw_checkbox(ui, lx, ty + 60, cfg.accept_incoming,
                             "Accept Incoming Connections", ui->pen_fg, 36);
            sprintf(buf, "Display: %dx%d (%d colors)   Theme: %s",
                    ui->scr_w, ui->scr_h, ui->colors, ui->theme_ctrl.active_name);
            at_draw_text(ui, lx, ty + 88, buf, ui->pen_fg, bw / 8);
            sprintf(buf, "Hardware: %s", ui->net ? ui->net->caps.backend_summary : "RS-232");
            at_draw_text(ui, lx, ty + 110, buf, ui->pen_fg, bw / 8);
        } else if (active_tab == 1) {
            at_draw_checkbox(ui, lx, ty + 32, cfg.bbs_enabled,
                             "Enable Packet BBS Store & Forward", ui->pen_fg, 36);
            sprintf(nbuf, "-%u", static_cast<unsigned int>(cfg.bbs_ssid));
            at_draw_field_row(ui, lx + 340, ty + 30, "SSID:", nbuf, 52, 60);
            at_draw_checkbox(ui, lx, ty + 58, cfg.bbs_files_enabled,
                             "Enable BBS Shared Files Area", ui->pen_fg, 36);
            at_draw_field_row(ui, lx, ty + 84, "Mail Dir:", cfg.bbs_mail_path, 96, 360);
            at_draw_field_row(ui, lx, ty + 110, "Files Dir:", cfg.bbs_files_path, 96, 360);
        } else if (active_tab == 2) {
            at_draw_checkbox(ui, lx, ty + 32, cfg.chat_enabled,
                             "Enable Real-Time CHAT Conference Bridge", ui->pen_fg, 44);
            sprintf(nbuf, "-%u", static_cast<unsigned int>(cfg.chat_ssid));
            at_draw_field_row(ui, lx, ty + 58, "CHAT SSID:", nbuf, 96, 80);
            at_draw_field_row(ui, lx, ty + 84, "Default Topic:", cfg.chat_topic, 120, 320);
        } else if (active_tab == 3) {
            int ix[4] = { lx + 4, lx + 82, lx + 178, lx + 358 };
            const char* ih[4] = { "ID", "Driver", "Endpoint / Device", "Policy" };
            int iw[4] = { 10, 12, 22, 24 };
            at_draw_listview_header(ui, lx, ty + 30, bw, 4, ix, ih, ui->pen_bg, ui->pen_fg);
            for (int i = 0; i < cfg.iface_count && i < 6; ++i) {
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
                at_draw_listview_row(ui, lx, ty + 48 + i * row_h, bw, row_h - 1,
                                     (i == ui->sel_iface), 4, ix, cells, iw, ui->pen_panel, ui->pen_fg);
            }
            at_draw_button(ui, lx, by, 96, 22, "Add Iface", 0);
            at_add_hit(ui, lx, by, 96, 22, OYK_SCR_ADD_IFACE);
            at_draw_button(ui, lx + 104, by, 96, 22, "Edit Iface", 0);
            at_add_hit(ui, lx + 104, by, 96, 22, OYK_SCR_EDIT_IFACE);
        } else if (active_tab == 4) {
            int px[5] = { lx + 4, lx + 78, lx + 168, lx + 258, lx + 432 };
            const char* ph[5] = { "ID", "Callsign", "Interface", "Endpoint", "Policy" };
            int pw[5] = { 10, 10, 11, 22, 10 };
            at_draw_listview_header(ui, lx, ty + 30, bw, 5, px, ph, ui->pen_bg, ui->pen_fg);
            for (int p = 0; p < cfg.peer_count && p < 7; ++p) {
                char ep[48];
                sprintf(ep, "%s:%u", cfg.peers[p].host, static_cast<unsigned int>(cfg.peers[p].port));
                const char* cells[5] = {
                    cfg.peers[p].id, cfg.peers[p].callsign, cfg.peers[p].interface_id,
                    ep, cfg.peers[p].auto_learned ? "AUTO" : "STATIC"
                };
                at_draw_listview_row(ui, lx, ty + 48 + p * row_h, bw, row_h - 1,
                                     (p == ui->sel_peer), 5, px, cells, pw, ui->pen_panel, ui->pen_fg);
            }
            at_draw_button(ui, lx, by, 92, 22, "Edit Peer", 0);
            at_add_hit(ui, lx, by, 92, 22, OYK_SCR_EDIT_PEER);
        } else if (active_tab == 5) {
            at_draw_checkbox(ui, lx, ty + 32, cfg.rig_enabled,
                             "Enable Rig CAT Transceiver Control", ui->pen_fg, 40);
            at_draw_field_row(ui, lx, ty + 58, "Protocol:",
                              cfg.rig_protocol[0] ? cfg.rig_protocol : "rigctld", 96, 130);
            at_draw_field_row(ui, lx + 246, ty + 58, "Host:", cfg.rig_host, 56, 150);
            sprintf(nbuf, "%u", static_cast<unsigned int>(cfg.rig_port));
            at_draw_field_row(ui, lx, ty + 84, "CAT Port:", nbuf, 96, 90);
            sprintf(nbuf, "%d s", cfg.rig_poll_sec);
            at_draw_field_row(ui, lx + 246, ty + 84, "Poll:", nbuf, 56, 90);
        } else if (active_tab == 6) {
            at_draw_checkbox(ui, lx, ty + 32, cfg.printer_enabled,
                             "Enable Line Printer Packet Log", ui->pen_fg, 40);
            at_draw_field_row(ui, lx, ty + 58, "Device:",
                              cfg.printer_device[0] ? cfg.printer_device : "PRN:", 96, 150);
            at_draw_field_row(ui, lx + 266, ty + 58, "Proto:",
                              cfg.printer_protocol[0] ? cfg.printer_protocol : "epson", 64, 110);
            sprintf(nbuf, "%lu", static_cast<unsigned long>(cfg.printer_baud));
            at_draw_field_row(ui, lx, ty + 84, "Baud Rate:", nbuf, 96, 110);
            at_draw_checkbox(ui, lx + 246, ty + 85, cfg.printer_condensed,
                             "Condensed 132-col Print", ui->pen_fg, 24);
        } else if (active_tab == 7) {
            at_draw_button(ui, lx, ty + 28, 26, 20, "<", 0);
            at_add_hit(ui, lx, ty + 28, 26, 20, 401);
            at_draw_button(ui, lx + 30, ty + 28, 26, 20, ">", 0);
            at_add_hit(ui, lx + 30, ty + 28, 26, 20, 402);
            sprintf(buf, "Theme: %s  (Whole-Screen Live Preview, %d colors)",
                    ui->theme_ctrl.active_name, ui->colors);
            at_draw_text(ui, lx + 64, ty + 30, buf, ui->pen_fg, (bw - 68) / 8);
            at_draw_text(ui, lx, ty + 52, "Role             Key         Color    Preview",
                         ui->pen_fg, bw / 8);
            int max_rows = (ui->scr_h >= 460) ? 10 : 7;
            int step_y = (ui->scr_h >= 460) ? 18 : 16;
            for (int i = 0; i < max_rows; ++i) {
                int ry = ty + 70 + i * step_y;
                sprintf(buf, "%-16s %-11s #%06lX",
                        oyk_theme_role_label(i),
                        oyk_theme_role_key(i),
                        static_cast<unsigned long>(ui->active_theme.colors[i] & 0xFFFFFFUL));
                at_draw_text(ui, lx, ry, buf, ui->pen_fg, 46);
                int sw_pen = at_swatch_pen_for_role(ui, i);
                at_fill_rect(ui, lx + 350, ry + 2, 72, 12, sw_pen);
                at_frame_rect(ui, lx + 350, ry + 2, 72, 12, ui->pen_fg);
            }
        }
        at_draw_button(ui, mx + mw - 172, by, 76, 22, "Cancel", 0);
        at_add_hit(ui, mx + mw - 172, by, 76, 22, 999);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Save", 1);
        at_add_hit(ui, mx + mw - 88, by, 76, 22, 999);
    } else if (scr == OYK_SCR_ADD_IFACE) {
        int st = ui->wiz.step;
        sprintf(buf, "Step %d of 4: %s", st + 1, oyk_gui_wiz_step_prompt(&ui->wiz));
        at_draw_text(ui, lx, ty + 4, buf, ui->pen_fg, bw / 8);
        if (st == 0) {
            for (int i = 0; i < 5; ++i) {
                int ry = ty + 28 + i * (row_h + 2);
                at_draw_radio(ui, lx + 4, ry, i == ui->wiz.selected_driver_idx,
                              oyk_gui_wiz_driver_type(ui->wiz.driver_mask, i), ui->pen_fg, 16);
                at_draw_text(ui, lx + 146, ry,
                             oyk_gui_wiz_driver_desc(ui->wiz.driver_mask, i),
                             ui->pen_fg, 44);
                at_add_hit(ui, lx + 4, ry, 480, row_h, 200 + i);
            }
        } else if (st == 1) {
            at_draw_field_row(ui, lx, ty + 30, "Driver:", ui->wiz.draft.type, 112, 120);
            at_draw_field_row(ui, lx, ty + 54, "Interface ID:", ui->wiz.draft.id, 112, 160);
            at_draw_field_row(ui, lx, ty + 78, "Description:",
                              ui->wiz.draft.description[0] ? ui->wiz.draft.description : "Atari Interface",
                              112, 320);
        } else if (st == 2) {
            if (ui->wiz.draft.device[0] || strcmp(ui->wiz.draft.type, "kiss-serial") == 0) {
                at_draw_field_row(ui, lx, ty + 28, "Device:",
                                  ui->wiz.draft.device[0] ? ui->wiz.draft.device : "AUX:", 104, 140);
                sprintf(nbuf, "%lu", static_cast<unsigned long>(ui->wiz.draft.baud ? ui->wiz.draft.baud : 9600UL));
                at_draw_field_row(ui, lx, ty + 52, "Baud Rate:", nbuf, 104, 100);
            } else {
                at_draw_field_row(ui, lx, ty + 28, "Bind Host:", ui->wiz.draft.host, 104, 140);
                sprintf(nbuf, "%u", static_cast<unsigned int>(ui->wiz.draft.port));
                at_draw_field_row(ui, lx + 266, ty + 28, "Port:", nbuf, 48, 80);
                at_draw_field_row(ui, lx, ty + 52, "Netmask:", ui->wiz.draft.netmask, 104, 140);
            }
            at_draw_checkbox(ui, lx, ty + 78, !ui->wiz.draft.block_inbound, "Allow Inbound", ui->pen_fg, 16);
            at_draw_checkbox(ui, lx + 152, ty + 78, !ui->wiz.draft.block_outbound, "Allow Outbound", ui->pen_fg, 16);
            at_draw_checkbox(ui, lx + 312, ty + 78, ui->wiz.draft.allow_same_interface, "Same-Interface", ui->pen_fg, 16);
            at_draw_field_row(ui, lx, ty + 102, "Learned Policy:",
                              oyk_gui_format_learned_policy(ui->wiz.draft.learned_peer_policy), 120, 120);
        } else {
            char sum[320];
            oyk_gui_wiz_format_summary(&ui->wiz, sum, sizeof(sum), 0);
            int sy = ty + 28;
            const char* p = sum;
            while (*p && sy < by - 16) {
                const char* nl = strchr(p, '\n');
                char ln[88];
                size_t len = nl ? static_cast<size_t>(nl - p) : strlen(p);
                if (len >= sizeof(ln)) len = sizeof(ln) - 1U;
                memcpy(ln, p, len);
                ln[len] = '\0';
                at_draw_text(ui, lx, sy, ln, ui->pen_fg, bw / 8);
                sy += (ch <= 10) ? 13 : 17;
                p = nl ? (nl + 1) : "";
            }
        }
        at_draw_button(ui, lx, by, 76, 22, "< Back", 0);
        at_add_hit(ui, lx, by, 76, 22, 210);
        at_draw_button(ui, mx + mw - 174, by, 76, 22, "Cancel", 0);
        at_add_hit(ui, mx + mw - 174, by, 76, 22, 999);
        at_draw_button(ui, mx + mw - 90, by, 78, 22, (st >= 3) ? "Finish" : "Next >", 1);
        at_add_hit(ui, mx + mw - 90, by, 78, 22, 211);
    } else if (scr == OYK_SCR_EDIT_IFACE) {
        const OykIfaceConfig& ifc = (ui->sel_iface < cfg.iface_count) ? cfg.ifaces[ui->sel_iface] : cfg.ifaces[0];
        at_draw_field_row(ui, lx, ty + 10, "Interface ID:", ifc.id, 112, 120);
        at_draw_field_row(ui, lx + 250, ty + 10, "Driver:", ifc.type, 64, 120);
        at_draw_field_row(ui, lx, ty + 34, "Bind Host:", ifc.host, 112, 150);
        sprintf(nbuf, "%u", static_cast<unsigned int>(ifc.port));
        at_draw_field_row(ui, lx + 280, ty + 34, "Port:", nbuf, 48, 80);
        at_draw_field_row(ui, lx, ty + 58, "Netmask:", ifc.netmask, 112, 150);
        at_draw_checkbox(ui, lx, ty + 84, !ifc.block_inbound, "Allow Inbound", ui->pen_fg, 16);
        at_draw_checkbox(ui, lx + 152, ty + 84, !ifc.block_outbound, "Allow Outbound", ui->pen_fg, 16);
        at_draw_checkbox(ui, lx + 312, ty + 84, ifc.allow_same_interface, "Same-Interface", ui->pen_fg, 16);
        at_draw_field_row(ui, lx, ty + 108, "Learned Policy:",
                          oyk_gui_format_learned_policy(ifc.learned_peer_policy), 120, 120);
        at_add_hit(ui, lx + 120, ty + 108, 120, 18, 302);
        at_draw_button(ui, mx + mw - 172, by, 76, 22, "Cancel", 0);
        at_add_hit(ui, mx + mw - 172, by, 76, 22, 999);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Save", 1);
        at_add_hit(ui, mx + mw - 88, by, 76, 22, 999);
    } else if (scr == OYK_SCR_EDIT_PEER) {
        const OykPeerConfig& pr = cfg.peers[0];
        at_draw_field_row(ui, lx, ty + 12, "Peer ID:", pr.id, 96, 120);
        at_draw_field_row(ui, lx + 236, ty + 12, "Callsign:", pr.callsign, 80, 120);
        at_draw_field_row(ui, lx, ty + 38, "Interface:", pr.interface_id, 96, 120);
        sprintf(buf, "%s:%u", pr.host, static_cast<unsigned int>(pr.port));
        at_draw_field_row(ui, lx + 236, ty + 38, "Endpoint:", buf, 80, 170);
        at_draw_checkbox(ui, lx, ty + 70, pr.allow_inbound, "Allow Inbound", ui->pen_fg, 16);
        at_draw_checkbox(ui, lx + 152, ty + 70, pr.allow_outbound, "Allow Outbound", ui->pen_fg, 16);
        at_draw_checkbox(ui, lx + 312, ty + 70, pr.auto_learned, "Auto-Learned", ui->pen_fg, 16);
        at_draw_button(ui, mx + mw - 172, by, 76, 22, "Cancel", 0);
        at_add_hit(ui, mx + mw - 172, by, 76, 22, 999);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Save", 1);
        at_add_hit(ui, mx + mw - 88, by, 76, 22, 999);
    } else if (scr == OYK_SCR_TERMINATE) {
        int tx[4] = { lx + 4, lx + 56, lx + 166, lx + 266 };
        const char* th[4] = {
            oyk_gui_table_column(OYK_TBL_TERMINATE, 0),
            oyk_gui_table_column(OYK_TBL_TERMINATE, 1),
            oyk_gui_table_column(OYK_TBL_TERMINATE, 2),
            oyk_gui_table_column(OYK_TBL_TERMINATE, 3)
        };
        int tw[4] = { 5, 12, 11, 14 };
        at_draw_listview_header(ui, lx, ty + 8, bw, 4, tx, th, ui->pen_bg, ui->pen_fg);
        at_hline(ui, lx, mx + mw - 12, ty + 26, ui->pen_fg);
        int row_idx = 0;
        for (int i = 0; i < ui->node->max_sessions() && row_idx < 6; ++i) {
            const OykSession& s = ui->node->session_slot(i);
            if (s.state == OYK_SESS_FREE) continue;
            char id_s[12];
            oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
            sprintf(id_s, "%lu", static_cast<unsigned long>(s.id));
            const char* cells[4] = { id_s, cs, (s.iface_idx == 0) ? "axudp0" : "aux0", "CONNECTED" };
            at_draw_listview_row(ui, lx, ty + 30 + row_idx * row_h, bw, row_h - 1,
                                 (row_idx == ui->sel_conn), 4, tx, cells, tw, ui->pen_panel, ui->pen_fg);
            ++row_idx;
        }
        at_hline(ui, lx, mx + mw - 12, by - 8, ui->pen_fg);
        at_draw_button(ui, mx + mw - 196, by, 76, 22, "Cancel", 0);
        at_add_hit(ui, mx + mw - 196, by, 76, 22, 999);
        at_draw_button(ui, mx + mw - 114, by, 102, 22, "Terminate", 1);
    } else if (scr == OYK_SCR_BAN) {
        int bcol_x[5] = { lx + 4, lx + 84, lx + 210, lx + 298, lx + 356 };
        const char* bcol_h[5] = { "Callsign", "Banned At", "Lift At", "Flags", "Reason" };
        int bcol_w[5] = { 10, 16, 11, 7, 24 };
        at_draw_field_row(ui, lx, ty + 4, "Callsign:", "N0BAD", 80, 100);
        at_draw_field_row(ui, lx + 190, ty + 4, "Reason:", "Beacon flood", 64, 150);
        at_draw_field_row(ui, lx + 414, ty + 4, "Dur:", "7d", 40, 56);
        at_draw_checkbox(ui, lx, ty + 28, 1, "Hard Ban (Drop AX.25)", ui->pen_fg, 24);
        at_draw_checkbox(ui, lx + 230, ty + 28, 0, "Allow Appeal", ui->pen_fg, 16);
        at_draw_listview_header(ui, lx, ty + 52, bw, 5, bcol_x, bcol_h, ui->pen_bg, ui->pen_fg);
        for (int i = 0; i < ui->node->ban_count() && i < 4; ++i) {
            const OykBanEntry& be = ui->node->ban_entry(i);
            char fl[12];
            sprintf(fl, "0x%02lX", static_cast<unsigned long>(be.flags));
            const char* cells[5] = { be.key, be.banned_at, be.lift_at, fl, be.reason };
            at_draw_listview_row(ui, lx, ty + 70 + i * row_h, bw, row_h - 1,
                                 (i == ui->sel_ban), 5, bcol_x, cells, bcol_w, ui->pen_panel, ui->pen_fg);
        }
        at_draw_button(ui, mx + mw - 188, by, 76, 22, "Close", 0);
        at_add_hit(ui, mx + mw - 188, by, 76, 22, 999);
        at_draw_button(ui, mx + mw - 104, by, 92, 22, "Add Ban", 1);
    } else if (scr == OYK_SCR_EXIT_PEERS) {
        sprintf(buf, "%d auto-learned peer(s) found during session.",
                ui->node->auto_learned_peer_count());
        at_draw_text(ui, lx, ty + 18, buf, ui->pen_fg, bw / 8);
        at_draw_text(ui, lx, ty + 42, "Keep learned peers in node configuration before exit?",
                     ui->pen_fg, bw / 8);
        at_draw_button(ui, lx, by, 108, 22, "Save & Exit", 1);
        at_draw_button(ui, lx + 114, by, 78, 22, "Discard", 0);
        at_draw_button(ui, lx + 198, by, 118, 22, "Review Peers", 0);
        at_add_hit(ui, lx + 198, by, 118, 22, OYK_SCR_PEERS);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Cancel", 0);
        at_add_hit(ui, mx + mw - 88, by, 76, 22, 999);
    } else if (scr == OYK_SCR_TERMINAL) {
        int out_h = mh - 134;
        at_draw_text(ui, lx, ty + 4, "Session: KR4OYK-1 BBS | EOL: CRLF | Wrap: ON",
                     ui->pen_fg, bw / 8);
        at_fill_rect(ui, lx, ty + 22, bw, out_h, ui->pen_bg);
        at_frame_rect(ui, lx, ty + 22, bw, out_h, ui->pen_fg);
        at_draw_text(ui, lx + 8, ty + 30, "Welcome to KR4OYK-7 BBS on Atari ST GEM",
                     ui->pen_fg, (bw - 16) / 8);
        at_draw_text(ui, lx + 8, ty + 30 + row_h, "Type 'LIST' or 'B' to disconnect from BBS.",
                     ui->pen_fg, (bw - 16) / 8);
        at_draw_text(ui, lx + 8, ty + 30 + row_h * 2, "14:21:08  Msg #42  ALL  de W4ABC-7: Net tonight",
                     ui->pen_fg, (bw - 16) / 8);
        at_fill_rect(ui, lx, by - 50, bw, 18, ui->pen_status_bg);
        at_draw_text(ui, lx + 6, by - 50 + oyk_gui_vertical_text_offset(18, ui->char_h),
                     oyk_term_hotkey_bar_text(0, 0), ui->pen_status_fg, (bw - 12) / 8);
        at_draw_field_row(ui, lx, by - 26, "Input:", "LIST_", 56, bw - 140);
        at_draw_button(ui, lx + bw - 76, by - 28, 76, 22, "Send", 1);
        at_draw_button(ui, lx, by, 78, 22, "Help", 0);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Close", 0);
        at_add_hit(ui, mx + mw - 88, by, 76, 22, 999);
    } else if (scr == OYK_SCR_TERM_HELP) {
        at_draw_text(ui, lx, ty + 4, "Command prefix: Ctrl-A (press Ctrl-A, then hotkey)",
                     ui->pen_fg, bw / 8);
        at_fill_rect(ui, lx, ty + 24, bw, mh - 90, ui->pen_bg);
        at_frame_rect(ui, lx, ty + 24, bw, mh - 90, ui->pen_fg);
        for (int hi = 0; hi < oyk_term_help_count() && hi < 9; ++hi) {
            const OykTermHelpEntry* he = oyk_term_help_entry(hi);
            if (!he) continue;
            char row[96];
            sprintf(row, "%-9.9s %-36.36s %s", he->key_label, he->action, he->alt_key);
            at_draw_text(ui, lx + 8, ty + 32 + hi * row_h, row, ui->pen_fg, (bw - 16) / 8);
        }
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Close", 1);
        at_add_hit(ui, mx + mw - 88, by, 76, 22, 999);
    } else if (scr == OYK_SCR_FILES) {
        int pw = (bw - 12) / 2;
        int fx1[2] = { lx + 6, lx + pw - 62 };
        const char* fh[2] = { "Name", "Size" };
        int fw[2] = { 22, 7 };
        const char* fr1a[2] = { "W3TRM-1", "<DIR>" };
        const char* fr1b[2] = { "PACKETS.TXT", "2048 B" };
        int fx2[2] = { lx + pw + 18, lx + bw - 62 };
        const char* fr2a[2] = { "bulletins", "<DIR>" };
        const char* fr2b[2] = { "README.TXT", "1420 B" };
        at_draw_text(ui, lx, ty + 4, "Incoming: /", ui->pen_fg, pw / 8);
        at_draw_text(ui, lx + pw + 12, ty + 4, "Files: /", ui->pen_fg, pw / 8);
        at_fill_rect(ui, lx, ty + 24, pw, mh - 90, ui->pen_bg);
        at_frame_rect(ui, lx, ty + 24, pw, mh - 90, ui->pen_fg);
        at_fill_rect(ui, lx + pw + 12, ty + 24, pw, mh - 90, ui->pen_bg);
        at_frame_rect(ui, lx + pw + 12, ty + 24, pw, mh - 90, ui->pen_fg);
        at_draw_listview_header(ui, lx + 2, ty + 26, pw - 4, 2, fx1, fh, ui->pen_panel, ui->pen_fg);
        at_draw_listview_row(ui, lx + 2, ty + 44, pw - 4, row_h, 1, 2, fx1, fr1a, fw, ui->pen_bg, ui->pen_fg);
        at_draw_listview_row(ui, lx + 2, ty + 44 + row_h, pw - 4, row_h, 0, 2, fx1, fr1b, fw, ui->pen_bg, ui->pen_fg);
        at_draw_listview_header(ui, lx + pw + 14, ty + 26, pw - 4, 2, fx2, fh, ui->pen_panel, ui->pen_fg);
        at_draw_listview_row(ui, lx + pw + 14, ty + 44, pw - 4, row_h, 1, 2, fx2, fr2a, fw, ui->pen_bg, ui->pen_fg);
        at_draw_listview_row(ui, lx + pw + 14, ty + 44 + row_h, pw - 4, row_h, 0, 2, fx2, fr2b, fw, ui->pen_bg, ui->pen_fg);
        at_draw_button(ui, lx,       by, 68, 22, "Upload",  0);
        at_draw_button(ui, lx + 72,  by, 74, 22, "Approve", 1);
        at_draw_button(ui, lx + 150, by, 68, 22, "Rename",  0);
        at_draw_button(ui, lx + 222, by, 70, 22, "New Dir", 0);
        at_draw_button(ui, lx + 296, by, 66, 22, "Delete",  0);
        at_draw_button(ui, lx + 366, by, 54, 22, "View",    0);
        at_draw_button(ui, mx + mw - 80, by, 68, 22, "Close", 0);
        at_add_hit(ui, mx + mw - 80, by, 68, 22, 999);
    } else if (scr == OYK_SCR_ABOUT) {
        for (int py = 0; py < 32; ++py) {
            for (int px = 0; px < 32; ++px) {
                if (ui->colors <= 2) {
                    unsigned char b = kOykLogoLine32Bits[py * 4 + (px >> 3)];
                    if ((b & (0x80U >> (px & 7))) == 0U) continue;
                    at_fill_rect(ui, lx + px * 2, ty + 8 + py * 2, 2, 2, ui->pen_fg);
                } else {
                    unsigned char pval = kOykLogoSolid32Pal[py * 32 + px];
                    if (pval == 0U) continue;
                    int pen = (pval == 2U) ? ui->pen_bg
                            : ((pval == 3U) ? ui->pen_fg : ui->pen_accent);
                    at_fill_rect(ui, lx + px * 2, ty + 8 + py * 2, 2, 2, pen);
                }
            }
        }
        const int tx = lx + 74;
        const int tc = (bw > 74) ? ((bw - 74) / 8) : 16;
        at_draw_text(ui, tx, ty + 14, "OYK Amateur Radio Packet Node (GPL-3.0-or-later)",
                     ui->pen_fg, tc);
        at_draw_text(ui, tx, ty + 34, "Protocols: AX.25 / KISS / AXUDP / AXIP / BBS / YAPP",
                     ui->pen_fg, tc);
        at_draw_text(ui, tx, ty + 54, "Platform: Atari ST / TT GEM (VDI + AES + RS-232)",
                     ui->pen_fg, tc);
        at_draw_text(ui, tx, ty + 76, "Hotkeys: 1..8 Settings Tabs | Esc Close Modal",
                     ui->pen_fg, tc);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "OK", 1);
        at_add_hit(ui, mx + mw - 88, by, 76, 22, 999);
    } else {
        at_draw_text(ui, lx, ty + 18, "Hamlib rigctld CAT: Port 4532 / RS-232 AUX @ 9600",
                     ui->pen_fg, bw / 8);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Close", 1);
        at_add_hit(ui, mx + mw - 88, by, 76, 22, 999);
    }
}

static void at_redraw(OykAtariUiState* ui) {
    if (!ui || !ui->node) return;
    oyk_gui_theme_resolve(&ui->theme_ctrl, &ui->active_theme);
    at_apply_palette(ui);
    at_sync_modal_window(ui);
    ++ui->console_draw_count;
    ui->last_console_bg = ui->active_theme.colors[OYK_TR_BG];
    if (ui->active_screen != OYK_SCR_MAIN) {
        ++ui->modal_draw_count;
        ui->last_modal_bg = ui->active_theme.colors[OYK_TR_BG];
    }
#ifdef __ATARI__
    if (!ui->capture_mode) {
        if (ui->ap_id >= 0) graf_mouse(M_OFF, NULL);
        if (ui->vdi > 0) v_hide_c(ui->vdi);
    }
    if (ui->win_handle >= 0) {
        wind_update(BEG_UPDATE);
        short clip[4];
        short rx = 0, ry = 0, rw = 0, rh = 0;
        wind_get(ui->win_handle, WF_FIRSTXYWH, &rx, &ry, &rw, &rh);
        while (rw > 0 && rh > 0) {
            clip[0] = rx;
            clip[1] = ry;
            clip[2] = static_cast<short>(rx + rw - 1);
            clip[3] = static_cast<short>(ry + rh - 1);
            vs_clip(ui->vdi, 1, clip);
            at_reset_hits(ui);
            at_draw_console_body(ui);
            wind_get(ui->win_handle, WF_NEXTXYWH, &rx, &ry, &rw, &rh);
        }
        vs_clip(ui->vdi, 0, clip);
        if (ui->active_screen != OYK_SCR_MAIN) {
            if (ui->modal_win_handle >= 0) {
                int saved_hits = ui->hit_count;
                wind_get(ui->modal_win_handle, WF_FIRSTXYWH, &rx, &ry, &rw, &rh);
                while (rw > 0 && rh > 0) {
                    clip[0] = rx;
                    clip[1] = ry;
                    clip[2] = static_cast<short>(rx + rw - 1);
                    clip[3] = static_cast<short>(ry + rh - 1);
                    vs_clip(ui->vdi, 1, clip);
                    ui->hit_count = saved_hits;
                    at_draw_modal_body(ui);
                    wind_get(ui->modal_win_handle, WF_NEXTXYWH, &rx, &ry, &rw, &rh);
                }
                vs_clip(ui->vdi, 0, clip);
            } else {
                at_draw_modal_body(ui);
            }
        }
        wind_update(END_UPDATE);
    } else {
        at_reset_hits(ui);
        at_draw_console_body(ui);
        if (ui->active_screen != OYK_SCR_MAIN) {
            at_draw_modal_body(ui);
        }
    }
    if (!ui->capture_mode) {
        if (ui->vdi > 0) v_show_c(ui->vdi, 1);
        if (ui->ap_id >= 0) graf_mouse(M_ON, NULL);
    }
#else
    at_reset_hits(ui);
    at_draw_console_body(ui);
    if (ui->active_screen != OYK_SCR_MAIN) {
        at_draw_modal_body(ui);
    }
#endif
}

static void at_on_theme_preview(const OykThemeConfig* pal, const char* active_name, void* ctx) {
    (void)active_name;
    OykAtariUiState* ui = static_cast<OykAtariUiState*>(ctx);
    if (!ui) return;
    if (pal) ui->active_theme = *pal;
    at_redraw(ui);
}

static void at_seed_demo_node(OykAtariUiState* ui) {
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

    memset(&seed.ifaces[1], 0, sizeof(OykIfaceConfig));
    strcpy(seed.ifaces[1].id, "aux0");
    strcpy(seed.ifaces[1].type, "kiss-serial");
    strcpy(seed.ifaces[1].device, "AUX:");
    seed.ifaces[1].baud = 9600UL;

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
    strcpy(seed.peers[1].interface_id, "aux0");
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
    rx_f.control = 0x3FU;
    node->on_rx_frame(0, &rx_f, 0x7F000002UL, 10093U, 0U);

    oyk_u32 sid = 0;
    OykCallsign rcall;
    int isloc = 0;
    char err_b[96];
    err_b[0] = '\0';
    node->open_local_terminal(0, "BBS", &sid, &rcall, &isloc, err_b, sizeof(err_b));
    node->ban_callsign("N0BAD", 0x07UL, "Beacon flood", "7d", err_b, sizeof(err_b));
    node->log_message(OYK_LOG_INFO, "OYK-Node started on Atari ST GEM");
    node->log_message(OYK_LOG_INFO, ui->net ? ui->net->caps.backend_summary : "Atari ST");

    oyk_gui_wiz_init(&ui->wiz, &node->config(), OYK_DRV_ALL);
    oyk_gui_wiz_select_driver(&ui->wiz, &node->config(), 2);
}

struct AtGalleryStep {
    int         scr;
    int         tab;
    int         wiz_step;
    const char* bmp_path;
};

static const AtGalleryStep kAtGallery[23] = {
    { OYK_SCR_MAIN,       0, 0, "C:\\AT_MAIN.BMP" },
    { OYK_SCR_MENU,       0, 0, "C:\\AT_MENU.BMP" },
    { OYK_SCR_SETTINGS,   0, 0, "C:\\AT_SET1.BMP" },
    { OYK_SCR_SETTINGS,   1, 0, "C:\\AT_SET2.BMP" },
    { OYK_SCR_SETTINGS,   2, 0, "C:\\AT_SET3.BMP" },
    { OYK_SCR_SETTINGS,   3, 0, "C:\\AT_SET4.BMP" },
    { OYK_SCR_PEERS,      4, 0, "C:\\AT_PEER.BMP" },
    { OYK_SCR_SETTINGS,   5, 0, "C:\\AT_SET6.BMP" },
    { OYK_SCR_SETTINGS,   6, 0, "C:\\AT_SET7.BMP" },
    { OYK_SCR_ADD_IFACE,  0, 0, "C:\\AT_ADD1.BMP" },
    { OYK_SCR_ADD_IFACE,  0, 1, "C:\\AT_ADD2.BMP" },
    { OYK_SCR_ADD_IFACE,  0, 2, "C:\\AT_ADD3.BMP" },
    { OYK_SCR_ADD_IFACE,  0, 3, "C:\\AT_ADD4.BMP" },
    { OYK_SCR_EDIT_IFACE, 0, 0, "C:\\AT_EDTI.BMP" },
    { OYK_SCR_EDIT_PEER,  0, 0, "C:\\AT_EDTP.BMP" },
    { OYK_SCR_TERMINATE,  0, 0, "C:\\AT_TERM.BMP" },
    { OYK_SCR_BAN,        0, 0, "C:\\AT_BAN.BMP"  },
    { OYK_SCR_EXIT_PEERS, 0, 0, "C:\\AT_EXIT.BMP" },
    { OYK_SCR_SETTINGS,   7, 0, "C:\\AT_THME.BMP" },
    { OYK_SCR_TERMINAL,   0, 0, "C:\\AT_TML.BMP"  },
    { OYK_SCR_TERM_HELP,  0, 0, "C:\\AT_TMLH.BMP" },
    { OYK_SCR_FILES,      0, 0, "C:\\AT_FILE.BMP" },
    { OYK_SCR_ABOUT,      0, 0, "C:\\AT_ABT.BMP"  }
};

int oyk_atari_ui_run(OykPortableNode* node,
                     OykAtariNetState* net,
                     int ui_test_mode) {
    if (!node) return 1;
    OykAtariUiState* ui = new OykAtariUiState();
    if (!ui) return 1;
    memset(ui, 0, sizeof(*ui));
    ui->node = node;
    ui->net = net;
    ui->running = 1;
    ui->active_screen = OYK_SCR_MAIN;
    ui->ap_id = -1;
    ui->win_handle = -1;
    ui->modal_win_handle = -1;
    ui->modal_scr = OYK_SCR_MAIN;
    ui->scr_w = 640;
    ui->scr_h = 400;
    ui->colors = 2;
    ui->tier = OYK_ATARI_MONO;

#ifdef __ATARI__
    ui->ap_id = appl_init();
    short wchar = 8, hchar = 16, wbox = 16, hbox = 16;
    short work_in[11] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2 };
    short work_out[57];
    memset(work_out, 0, sizeof(work_out));
    ui->vdi = graf_handle(&wchar, &hchar, &wbox, &hbox);
    v_opnvwk(work_in, &ui->vdi, work_out);
    if (ui->vdi <= 0) {
        if (ui->ap_id >= 0) appl_exit();
        delete ui;
        return 1;
    }
    ui->scr_w = work_out[0] + 1;
    ui->scr_h = work_out[1] + 1;
    ui->colors = work_out[13];
    ui->tier = oyk_atari_classify_display(ui->scr_w, ui->scr_h, ui->colors);
    {
        short vatt[10];
        memset(vatt, 0, sizeof(vatt));
        vqt_attributes(ui->vdi, vatt);
        ui->char_w = (vatt[6] > 0) ? vatt[6] : wchar;
        ui->char_h = (vatt[7] > 0) ? vatt[7] : hchar;
    }
    ui->wx = 0;
    ui->wy = 0;
    ui->ww = static_cast<short>(ui->scr_w);
    ui->wh = static_cast<short>(ui->scr_h);
    if (ui->ap_id >= 0) {
        if (!ui_test_mode) at_install_menus(ui);
        wind_get(0, WF_WORKXYWH, &ui->desk_x, &ui->desk_y, &ui->desk_w, &ui->desk_h);
        if (ui->desk_w <= 0 || ui->desk_h <= 0) {
            ui->desk_x = 0;
            ui->desk_y = 19;
            ui->desk_w = static_cast<short>(ui->scr_w);
            ui->desk_h = static_cast<short>(ui->scr_h - 19);
        }
        ui->win_handle = wind_create(NAME | CLOSER | MOVER,
                                     ui->desk_x, ui->desk_y, ui->desk_w, ui->desk_h);
        if (ui->win_handle >= 0) {
            short ox = static_cast<short>(ui->desk_x + 4);
            short oy = static_cast<short>(ui->desk_y + 2);
            short ow = static_cast<short>(ui->desk_w - 8);
            short oh = static_cast<short>(ui->desk_h - 4);
            at_copy(ui->main_title, sizeof(ui->main_title), " OYK Amateur Radio Packet Node ");
            wind_set_str(ui->win_handle, WF_NAME, ui->main_title);
            wind_open(ui->win_handle, ox, oy, ow, oh);
            wind_get(ui->win_handle, WF_WORKXYWH, &ui->wx, &ui->wy, &ui->ww, &ui->wh);
        }
    }
    oyk_atari_query_os_native_theme(ui->vdi, ui->colors, &ui->os_native_pal);
#else
    oyk_atari_query_os_native_theme(0, 16, &ui->os_native_pal);
#endif

    ui->draft_cfg = node->config();
    oyk_gui_wiz_init(&ui->wiz, &node->config(), OYK_DRV_ALL);
    oyk_gui_theme_init(&ui->theme_ctrl, &ui->draft_cfg, 1, ui->os_native_pal.is_dark_base);
    oyk_gui_theme_set_os_native(&ui->theme_ctrl, &ui->os_native_pal);
    oyk_gui_theme_set_preview_cb(&ui->theme_ctrl, at_on_theme_preview, ui);
    ui->active_theme = ui->os_native_pal;

    if (ui_test_mode) {
        ui->capture_mode = 1;
#ifdef __ATARI__
        if (ui->ap_id >= 0) graf_mouse(M_OFF, NULL);
        if (ui->vdi > 0) v_hide_c(ui->vdi);
#endif
        at_seed_demo_node(ui);
        ui->draft_cfg = node->config();
        for (int i = 0; i < 23; ++i) {
            ui->active_screen = kAtGallery[i].scr;
            ui->settings_tab = kAtGallery[i].tab;
            ui->wiz.step = kAtGallery[i].wiz_step;
            at_redraw(ui);
#ifdef __ATARI__
            oyk_atari_capture_screen_bmp(kAtGallery[i].bmp_path, ui->vdi,
                                         ui->scr_w, ui->scr_h, ui->colors);
#endif
        }
        oyk_atari_finish_test();
#ifdef __ATARI__
        if (ui->modal_win_handle >= 0) {
            wind_close(ui->modal_win_handle);
            wind_delete(ui->modal_win_handle);
            ui->modal_win_handle = -1;
        }
        if (ui->win_handle >= 0) {
            wind_close(ui->win_handle);
            wind_delete(ui->win_handle);
            ui->win_handle = -1;
        }
        if (ui->vdi > 0) v_clsvwk(ui->vdi);
        if (ui->ap_id >= 0) appl_exit();
#endif
        delete ui;
        return 0;
    }

    at_redraw(ui);
#ifdef __ATARI__
    short msg[8];
    while (ui->running) {
        short key = 0, mx = 0, my = 0, mb = 0, ks = 0, br = 0;
        short ev = evnt_multi(MU_KEYBD | MU_BUTTON | MU_MESAG | MU_TIMER,
                              1, 1, 1,
                              0, 0, 0, 0, 0,
                              0, 0, 0, 0, 0,
                              msg, 250,
                              &mx, &my, &mb, &ks, &key, &br);
        if (ev & MU_MESAG) {
            if (msg[0] == WM_REDRAW || msg[0] == WM_TOPPED) {
                at_redraw(ui);
            } else if (msg[0] == WM_MOVED) {
                wind_set(msg[3], WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
                if (ui->win_handle >= 0) {
                    wind_get(ui->win_handle, WF_WORKXYWH, &ui->wx, &ui->wy, &ui->ww, &ui->wh);
                }
                at_redraw(ui);
            } else if (msg[0] == MN_SELECTED) {
                menu_tnormal(s_at_menu_tree, msg[3], UNHIGHLIGHT);
                if (ui->active_screen == OYK_SCR_MAIN) {
                    int mitem = msg[4];
                    int act_id = OYK_ACT_NONE;
                    if (mitem == 8 || mitem == 22) {
                        act_id = OYK_ACT_ABOUT;
                    } else if (mitem >= 10 && mitem <= 20) {
                        const OykGuiMenuItem* gi = oyk_gui_node_menu_item(mitem - 10);
                        if (gi) act_id = gi->action;
                    }
                    if (act_id == OYK_ACT_ALLOW_INCOMING) {
                        ui->node->set_accept_incoming(1);
                    } else if (act_id == OYK_ACT_DENY_INCOMING) {
                        ui->node->set_accept_incoming(0);
                    } else if (act_id == OYK_ACT_LOCAL_TERM) {
                        ui->active_screen = OYK_SCR_TERMINAL;
                    } else if (act_id == OYK_ACT_FILES) {
                        ui->active_screen = OYK_SCR_FILES;
                    } else if (act_id == OYK_ACT_RIG) {
                        ui->active_screen = OYK_SCR_RIG;
                    } else if (act_id == OYK_ACT_SETTINGS) {
                        ui->settings_tab = 0;
                        ui->active_screen = OYK_SCR_SETTINGS;
                    } else if (act_id == OYK_ACT_TERMINATE) {
                        ui->active_screen = OYK_SCR_TERMINATE;
                    } else if (act_id == OYK_ACT_BAN) {
                        ui->active_screen = OYK_SCR_BAN;
                    } else if (act_id == OYK_ACT_ABOUT) {
                        ui->active_screen = OYK_SCR_ABOUT;
                    } else if (act_id == OYK_ACT_EXIT) {
                        if (ui->node->auto_learned_peer_count() > 0) {
                            ui->active_screen = OYK_SCR_EXIT_PEERS;
                        } else {
                            ui->running = 0;
                        }
                    }
                    at_redraw(ui);
                }
            } else if (msg[0] == AP_TERM) {
                ui->running = 0;
            } else if (msg[0] == WM_CLOSED) {
                if (ui->modal_win_handle >= 0 && msg[3] == ui->modal_win_handle) {
                    oyk_gui_theme_revert(&ui->theme_ctrl);
                    ui->active_screen = OYK_SCR_MAIN;
                    at_redraw(ui);
                } else if (ui->active_screen == OYK_SCR_MAIN) {
                    ui->running = 0;
                }
            }
        }
        if (ev & MU_BUTTON) {
            int act = -1;
            for (int hi = ui->hit_count - 1; hi >= 0; --hi) {
                const AtHitZone& z = ui->hits[hi];
                if (mx >= z.x && mx < z.x + z.w && my >= z.y && my < z.y + z.h) {
                    act = z.action;
                    break;
                }
            }
            if (act == 999) {
                oyk_gui_theme_revert(&ui->theme_ctrl);
                ui->active_screen = OYK_SCR_MAIN;
                at_redraw(ui);
            } else if (act >= 100 && act < 108) {
                ui->active_screen = OYK_SCR_SETTINGS;
                ui->settings_tab = act - 100;
                at_redraw(ui);
            } else if (act >= 200 && act < 205) {
                oyk_gui_wiz_select_driver(&ui->wiz, &ui->node->config(), act - 200);
                at_redraw(ui);
            } else if (act == 210) {
                oyk_gui_wiz_back(&ui->wiz);
                at_redraw(ui);
            } else if (act == 211) {
                if (ui->wiz.step >= 3) {
                    ui->active_screen = OYK_SCR_SETTINGS;
                    ui->settings_tab = 3;
                } else {
                    oyk_gui_wiz_advance(&ui->wiz, &ui->node->config());
                }
                at_redraw(ui);
            } else if (act == 302) {
                OykConfig cpy = ui->node->config();
                if (cpy.iface_count > 0) {
                    int idx = (ui->sel_iface < cpy.iface_count) ? ui->sel_iface : 0;
                    cpy.ifaces[idx].learned_peer_policy = static_cast<oyk_u8>(
                        oyk_gui_cycle_learned_policy(cpy.ifaces[idx].learned_peer_policy, 1));
                    OykConfigApplyResult r;
                    ui->node->apply_config(&cpy, 0, &r);
                    at_redraw(ui);
                }
            } else if (act == 401) {
                oyk_gui_theme_cycle(&ui->theme_ctrl, -1);
            } else if (act == 402) {
                oyk_gui_theme_cycle(&ui->theme_ctrl, 1);
            } else if (act >= 0 && act < 100) {
                ui->active_screen = act;
                if (act == OYK_SCR_PEERS) ui->settings_tab = 4;
                at_redraw(ui);
            }
        }
        if (ev & MU_KEYBD) {
            int ch = key & 0xFF;
            if (ch == 27) {
                if (ui->active_screen != OYK_SCR_MAIN) {
                    oyk_gui_theme_revert(&ui->theme_ctrl);
                    ui->active_screen = OYK_SCR_MAIN;
                    at_redraw(ui);
                }
            } else if ((ch == 'q' || ch == 'Q') && ui->active_screen == OYK_SCR_MAIN) {
                ui->running = 0;
            } else if ((ui->active_screen == OYK_SCR_SETTINGS ||
                        ui->active_screen == OYK_SCR_PEERS ||
                        ui->active_screen == OYK_SCR_THEME) &&
                       ch >= '1' && ch <= '8') {
                ui->active_screen = OYK_SCR_SETTINGS;
                ui->settings_tab = ch - '1';
                at_redraw(ui);
            }
        }
        oyk_atari_net_poll(net);
    }
    at_remove_menus(ui);
    if (ui->modal_win_handle >= 0) {
        wind_close(ui->modal_win_handle);
        wind_delete(ui->modal_win_handle);
        ui->modal_win_handle = -1;
    }
    if (ui->win_handle >= 0) {
        wind_close(ui->win_handle);
        wind_delete(ui->win_handle);
        ui->win_handle = -1;
    }
    if (ui->vdi > 0) v_clsvwk(ui->vdi);
    if (ui->ap_id >= 0) appl_exit();
#endif
    delete ui;
    return 0;
}

void oyk_atari_ui_show_fatal(const char* msg) {
    fprintf(stderr, "OYK-Node Atari Fatal: %s\n", msg ? msg : "unknown error");
}

int oyk_atari_ui_run_theme_preview_self_test(OykPortableNode* node,
                                             OykAtariNetState* net,
                                             short vdi_handle,
                                             int colors,
                                             int* out_bg_changes,
                                             int* out_fg_changes) {
    if (!node) return 0;
    OykAtariUiState* ui = new OykAtariUiState();
    if (!ui) return 0;
    memset(ui, 0, sizeof(*ui));
    ui->node = node;
    ui->net = net;
    ui->ap_id = -1;
    ui->win_handle = -1;
    ui->modal_win_handle = -1;
    ui->modal_scr = OYK_SCR_MAIN;
    ui->vdi = vdi_handle;
    ui->scr_w = 640;
    ui->scr_h = (colors <= 2) ? 400 : 480;
    ui->colors = colors;
    ui->tier = oyk_atari_classify_display(ui->scr_w, ui->scr_h, colors);
    ui->active_screen = OYK_SCR_THEME;

    oyk_atari_query_os_native_theme(vdi_handle, colors, &ui->os_native_pal);
    ui->draft_cfg = node->config();
    oyk_gui_theme_init(&ui->theme_ctrl, &ui->draft_cfg, 1, ui->os_native_pal.is_dark_base);
    oyk_gui_theme_set_os_native(&ui->theme_ctrl, &ui->os_native_pal);
    oyk_gui_theme_set_preview_cb(&ui->theme_ctrl, at_on_theme_preview, ui);
    ui->active_theme = ui->os_native_pal;
    at_redraw(ui);

    int ok = 1, bg_changes = 0, fg_changes = 0;
    if (strcmp(ui->theme_ctrl.active_name, "native") != 0) ok = 0;

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
            oyk_gui_theme_set_role_rgb(&ui->theme_ctrl, OYK_TR_BG, 0x224466UL, terr, sizeof(terr));
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
        ui->last_console_bg != ui->os_native_pal.colors[OYK_TR_BG] ||
        ui->last_modal_bg != ui->os_native_pal.colors[OYK_TR_BG]) {
        ok = 0;
    }

    if (out_bg_changes) *out_bg_changes = bg_changes;
    if (out_fg_changes) *out_fg_changes = fg_changes;
    if (bg_changes < 4 || fg_changes < 4) ok = 0;
    delete ui;
    return ok;
}
