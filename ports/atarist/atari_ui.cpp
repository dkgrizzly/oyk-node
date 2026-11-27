/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "atari_ui.hpp"
#include "atari_capture.hpp"

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
    int                 scr_w;
    int                 scr_h;
    int                 colors;
    OykAtariDisplayTier tier;
    int                 active_screen;
    int                 settings_tab;
    int                 sel_peer;
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
};

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
    int sz = oyk_gui_control_box_size(ui ? ui->char_h : 16);
    if (sz < 11) sz = 11;
    at_fill_rect(ui, x, y, sz, sz, ui->pen_bg);
    at_frame_rect(ui, x, y, sz, sz, ui->pen_fg);
#ifdef __ATARI__
    if (checked && ui && ui->vdi > 0) {
        short pts[6];
        pts[0] = static_cast<short>(x + 2);
        pts[1] = static_cast<short>(y + sz / 2);
        pts[2] = static_cast<short>(x + sz / 2 - 1);
        pts[3] = static_cast<short>(y + sz - 3);
        pts[4] = static_cast<short>(x + sz - 2);
        pts[5] = static_cast<short>(y + 2);
        vsl_color(ui->vdi, static_cast<short>(ui->pen_accent));
        v_pline(ui->vdi, 3, pts);
        pts[1] += 1;
        pts[3] += 1;
        pts[5] += 1;
        v_pline(ui->vdi, 3, pts);
    }
#else
    (void)checked;
#endif
    if (label && label[0]) {
        at_draw_text(ui, x + sz + 6, y, label, label_pen, max_chars);
    }
}

static void at_draw_radio(const OykAtariUiState* ui, int x, int y, int selected,
                          const char* label, int label_pen, int max_chars) {
    int sz = oyk_gui_control_box_size(ui ? ui->char_h : 16);
    if (sz < 11) sz = 11;
#ifdef __ATARI__
    if (ui && ui->vdi > 0) {
        short r = static_cast<short>(sz / 2);
        short cx = static_cast<short>(x + r);
        short cy = static_cast<short>(y + r + 1);
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
        at_draw_text(ui, x + sz + 6, y, label, label_pen, max_chars);
    }
}

static void at_draw_field_row(const OykAtariUiState* ui, int x, int y, const char* label,
                              const char* value, int label_w, int box_w) {
    if (label && label[0]) {
        at_draw_text(ui, x, y + 1, label, ui->pen_fg, label_w / 8);
    }
    int bx = x + label_w;
    at_fill_rect(ui, bx, y, box_w, 18, ui->pen_bg);
    at_frame_rect(ui, bx, y, box_w, 18, ui->pen_fg);
    if (value && value[0]) {
        at_draw_text(ui, bx + 5, y + 1, value, ui->pen_fg, (box_w - 10) / 8);
    }
}

static void at_draw_box(const OykAtariUiState* ui, int x, int y, int w, int h, const char* title) {
    at_fill_rect(ui, x, y, w, h, ui->pen_panel);
    at_frame_rect(ui, x, y, w, h, ui->pen_fg);
    at_fill_rect(ui, x + 1, y + 1, w - 2, 18, ui->pen_accent);
    at_hline(ui, x, x + w - 1, y + 19, ui->pen_fg);
    if (title && title[0]) {
        at_draw_text(ui, x + 8, y + 2, title, ui->pen_accent_fg, (w - 16) / 8);
    }
}

static void at_draw_button(const OykAtariUiState* ui, int x, int y, int w, int h,
                           const char* label, int is_primary) {
    int bg = is_primary ? ui->pen_accent : ui->pen_panel;
    int fg = is_primary ? ui->pen_accent_fg : ui->pen_fg;
    at_fill_rect(ui, x, y, w, h, bg);
    at_frame_rect(ui, x, y, w, h, ui->pen_fg);
    if (label && label[0]) {
        at_draw_text(ui, x + 8, y + 3, label, fg, (w - 14) / 8);
    }
}

static void at_draw_console(OykAtariUiState* ui) {
    OykPortableNode* node = ui->node;
    const OykConfig& cfg = node->config();
    int w = ui->scr_w;
    int h = ui->scr_h;

    ++ui->console_draw_count;
    ui->last_console_bg = ui->active_theme.colors[OYK_TR_BG];
    at_reset_hits(ui);

    /* Repaint entire GEM screen workspace + top menu bar */
    at_fill_rect(ui, 0, 0, w, h, ui->pen_bg);
    at_fill_rect(ui, 0, 0, w, 19, ui->pen_menu_bg);
    at_hline(ui, 0, w - 1, 19, ui->pen_fg);
    at_draw_text(ui, 10, 2, "Desk  File  Edit  Node  View  Window  Help",
                 ui->pen_menu_fg, 46);
    {
        char rstat[32];
        sprintf(rstat, "%s", (ui->tier == OYK_ATARI_MONO) ? "ST-HIGH (Mono)" : "VDI-16 Color");
        at_draw_text(ui, w - 124, 2, rstat, ui->pen_menu_fg, 14);
    }

    /* Status banner with canonical header + right incoming badge */
    char cs[16], al[16], hdr[128];
    oyk_format_callsign(&cfg.callsign, cs, sizeof(cs));
    oyk_format_callsign(&cfg.alias, al, sizeof(al));
    sprintf(hdr, "OYK PACKET NODE: %s (%s) | Up: 0s | Conns: %d",
            cs, al, node->total_active_connections());
    const char* pill = oyk_gui_format_incoming_pill(node->accept_incoming(), 0);
    int pill_w = static_cast<int>(strlen(pill)) * 8 + 10;
    at_fill_rect(ui, 8, 23, w - 16, 22, ui->pen_status_bg);
    at_frame_rect(ui, 8, 23, w - 16, 22, ui->pen_fg);
    at_draw_text(ui, 14, 26, hdr, ui->pen_status_fg, (w - pill_w - 36) / 8);
    at_fill_rect(ui, w - 12 - pill_w, 25, pill_w, 18, ui->pen_panel);
    at_frame_rect(ui, w - 12 - pill_w, 25, pill_w, 18, ui->pen_fg);
    at_draw_text(ui, w - 7 - pill_w, 26, pill, ui->pen_fg, static_cast<int>(strlen(pill)));

    /* Side-by-side Interfaces & Last 5 Heard */
    int half = (w - 22) / 2;
    int y0 = 49;
    int top_h = (h >= 440) ? 116 : 92;
    at_draw_box(ui, 8, y0, half, top_h, "Interfaces");
    at_draw_text(ui, 14, y0 + 22, "Port    Type        State RX/TX", ui->pen_fg, (half - 16) / 8);
    for (int i = 0; i < cfg.iface_count && i < 3; ++i) {
        const OykIfaceRuntime& rt = node->iface(i);
        char row[80];
        sprintf(row, "%-7s %-11s %-4s  %lu/%lu",
                cfg.ifaces[i].id, cfg.ifaces[i].type,
                rt.running ? "UP" : "DN",
                static_cast<unsigned long>(rt.rx_frames),
                static_cast<unsigned long>(rt.tx_frames));
        at_draw_text(ui, 14, y0 + 40 + i * 17, row, ui->pen_fg, (half - 16) / 8);
    }

    int rx = 14 + half;
    at_draw_box(ui, rx, y0, half, top_h, "Last 5 New Callsigns Heard");
    at_draw_text(ui, rx + 6, y0 + 22, "#  Callsign  Port    Heard", ui->pen_fg, (half - 16) / 8);
    for (int i = 0; i < node->new_heard_count() && i < 3; ++i) {
        const OykNewHeard& he = node->new_heard(i);
        char row[64];
        sprintf(row, "%-2d %-9s %-7s %s", i + 1, he.callsign, he.iface_id, he.time_hms);
        at_draw_text(ui, rx + 6, y0 + 40 + i * 17, row, ui->pen_fg, (half - 16) / 8);
    }

    /* Active Connections */
    int cy = y0 + top_h + 6;
    int ch_box = (h >= 440) ? 114 : 92;
    at_draw_box(ui, 8, cy, w - 16, ch_box, "Active Connections");
    at_draw_text(ui, 14, cy + 22, "ID   Callsign   Port     Dir    State      Since",
                 ui->pen_fg, (w - 32) / 8);
    int shown_c = 0;
    for (int i = 0; i < node->max_sessions() && shown_c < 3; ++i) {
        const OykSession& s = node->session_slot(i);
        if (s.state == OYK_SESS_FREE) continue;
        char scs[16], row[96];
        oyk_format_callsign(&s.remote_call, scs, sizeof(scs));
        const char* pname = (s.iface_idx >= 0 && s.iface_idx < cfg.iface_count)
                                ? cfg.ifaces[s.iface_idx].id : "local";
        sprintf(row, "%-4lu %-10s %-8s %-6s %-10s %s",
                static_cast<unsigned long>(s.id), scs, pname,
                s.is_local_term ? "LOCAL" : "IN",
                (s.state == OYK_SESS_CONNECTED) ? "CONNECTED" : "OPENING",
                s.since_hms);
        at_draw_text(ui, 14, cy + 40 + shown_c * 17, row, ui->pen_fg, (w - 32) / 8);
        ++shown_c;
    }

    /* Node Activity Log */
    int ly = cy + ch_box + 6;
    int bar_y = h - 28;
    int lh = bar_y - ly - 6;
    if (lh > 46) {
        at_draw_box(ui, 8, ly, w - 16, lh, "Node Activity Log");
        int max_r = (lh - 26) / 17;
        int total = node->console_log_count();
        int start = (total > max_r) ? (total - max_r) : 0;
        for (int i = start; i < total; ++i) {
            const char* ln = node->console_log_line(i);
            if (ln) at_draw_text(ui, 14, ly + 22 + (i - start) * 17, ln, ui->pen_fg, (w - 32) / 8);
        }
    }

    at_draw_button(ui, 8,   bar_y, 88, 22, "Terminal", 1); at_add_hit(ui, 8,   bar_y, 88, 22, OYK_SCR_TERMINAL);
    at_draw_button(ui, 102, bar_y, 68, 22, "Files",    0); at_add_hit(ui, 102, bar_y, 68, 22, OYK_SCR_FILES);
    at_draw_button(ui, 176, bar_y, 88, 22, "Settings", 0); at_add_hit(ui, 176, bar_y, 88, 22, OYK_SCR_SETTINGS);
    at_draw_button(ui, 270, bar_y, 68, 22, "Theme",    0); at_add_hit(ui, 270, bar_y, 68, 22, OYK_SCR_THEME);
    at_draw_button(ui, 344, bar_y, 68, 22, "Peers",    0); at_add_hit(ui, 344, bar_y, 68, 22, OYK_SCR_PEERS);
    at_draw_text(ui, 424, bar_y + 3,
                 ui->net ? ui->net->caps.backend_summary : "ATARI GEM",
                 ui->pen_fg, 25);
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

static void at_draw_modal(OykAtariUiState* ui) {
    int scr = ui->active_screen;
    int mx = 36, my = 30;
    int mw = ui->scr_w - 72;
    int mh = ui->scr_h - 68;
    const char* title = oyk_gui_screen_title(scr);
    if (scr == OYK_SCR_PEERS) title = oyk_gui_screen_title(OYK_SCR_SETTINGS);

    ++ui->modal_draw_count;
    ui->last_modal_bg = ui->active_theme.colors[OYK_TR_BG];

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

    int lx = mx + 12;
    int ty = my + 28;
    int bw = mw - 24;
    int by = my + mh - 30;
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
            at_fill_rect(ui, tx, ty, tw - 3, 20, sel ? ui->pen_accent : ui->pen_bg);
            at_frame_rect(ui, tx, ty, tw - 3, 20, ui->pen_fg);
            at_draw_text(ui, tx + 4, ty + 2, buf,
                         sel ? ui->pen_accent_fg : ui->pen_fg, (tw - 8) / 8);
            at_add_hit(ui, tx, ty, tw - 3, 20, 100 + t);
            tx += tw;
        }
        at_hline(ui, lx, mx + mw - 12, ty + 24, ui->pen_fg);

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
            at_draw_text(ui, lx, ty + 30, "ID       Driver      Endpoint / Device         Policy",
                         ui->pen_fg, bw / 8);
            for (int i = 0; i < cfg.iface_count && i < 6; ++i) {
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
                at_draw_text(ui, lx, ty + 50 + i * 18, buf, ui->pen_fg, bw / 8);
            }
            at_draw_button(ui, lx, by, 96, 22, "Add Iface", 0);
            at_add_hit(ui, lx, by, 96, 22, OYK_SCR_ADD_IFACE);
            at_draw_button(ui, lx + 104, by, 96, 22, "Edit Iface", 0);
            at_add_hit(ui, lx + 104, by, 96, 22, OYK_SCR_EDIT_IFACE);
        } else if (active_tab == 4) {
            at_draw_text(ui, lx, ty + 30, "ID       Callsign  Interface Endpoint          Policy",
                         ui->pen_fg, bw / 8);
            for (int p = 0; p < cfg.peer_count && p < 7; ++p) {
                sprintf(buf, "%s %-8s %-9s %-9s %s:%u %s",
                        (p == ui->sel_peer) ? ">" : " ",
                        cfg.peers[p].id, cfg.peers[p].callsign, cfg.peers[p].interface_id,
                        cfg.peers[p].host, static_cast<unsigned int>(cfg.peers[p].port),
                        cfg.peers[p].auto_learned ? "AUTO" : "STATIC");
                at_draw_text(ui, lx, ty + 50 + p * 18, buf, ui->pen_fg, bw / 8);
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
        } else {
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
        }
        at_draw_button(ui, mx + mw - 172, by, 76, 22, "Cancel", 0);
        at_add_hit(ui, mx + mw - 172, by, 76, 22, 999);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Save", 1);
        at_add_hit(ui, mx + mw - 88, by, 76, 22, 999);
    } else if (scr == OYK_SCR_ADD_IFACE) {
        at_draw_text(ui, lx, ty + 4, "Step 1 of 4: Select Driver & Configure Endpoint",
                     ui->pen_fg, bw / 8);
        for (int i = 0; i < 5; ++i) {
            int ry = ty + 28 + i * 20;
            at_draw_radio(ui, lx + 4, ry, i == ui->wiz.selected_driver_idx,
                          oyk_gui_wiz_driver_type(ui->wiz.driver_mask, i), ui->pen_fg, 16);
            at_add_hit(ui, lx + 4, ry, 164, 18, 200 + i);
        }
        at_draw_text(ui, lx + 186, ty + 28,
                     oyk_gui_wiz_driver_desc(ui->wiz.driver_mask, ui->wiz.selected_driver_idx),
                     ui->pen_fg, 44);
        at_draw_field_row(ui, lx + 186, ty + 52, "Iface ID:", ui->wiz.draft.id, 84, 120);
        if (ui->wiz.draft.device[0] || strcmp(ui->wiz.draft.type, "kiss-serial") == 0) {
            at_draw_field_row(ui, lx + 186, ty + 76, "Device:",
                              ui->wiz.draft.device[0] ? ui->wiz.draft.device : "AUX:", 84, 140);
            sprintf(nbuf, "%lu", static_cast<unsigned long>(ui->wiz.draft.baud ? ui->wiz.draft.baud : 9600UL));
            at_draw_field_row(ui, lx + 186, ty + 100, "Baud:", nbuf, 84, 100);
        } else {
            at_draw_field_row(ui, lx + 186, ty + 76, "Bind Host:", ui->wiz.draft.host, 84, 140);
            sprintf(nbuf, "%u", static_cast<unsigned int>(ui->wiz.draft.port));
            at_draw_field_row(ui, lx + 186, ty + 100, "Port:", nbuf, 84, 90);
        }
        at_draw_checkbox(ui, lx + 186, ty + 126, !ui->wiz.draft.allow_same_interface,
                         "Isolate Same-Interface Bridges", ui->pen_fg, 34);
        at_draw_button(ui, lx, by, 76, 22, "< Back", 0);
        at_draw_button(ui, mx + mw - 174, by, 76, 22, "Cancel", 0);
        at_add_hit(ui, mx + mw - 174, by, 76, 22, 999);
        at_draw_button(ui, mx + mw - 90, by, 78, 22, "Next >", 1);
    } else if (scr == OYK_SCR_EDIT_IFACE) {
        const OykIfaceConfig& ifc = cfg.ifaces[0];
        at_draw_field_row(ui, lx, ty + 12, "Interface ID:", ifc.id, 112, 120);
        at_draw_field_row(ui, lx + 250, ty + 12, "Driver:", ifc.type, 64, 120);
        at_draw_field_row(ui, lx, ty + 38, "Bind Host:", ifc.host, 112, 150);
        sprintf(nbuf, "%u", static_cast<unsigned int>(ifc.port));
        at_draw_field_row(ui, lx + 280, ty + 38, "Port:", nbuf, 48, 80);
        at_draw_field_row(ui, lx, ty + 64, "Netmask:", ifc.netmask, 112, 150);
        at_draw_checkbox(ui, lx, ty + 94, !ifc.block_inbound, "Allow Inbound", ui->pen_fg, 16);
        at_draw_checkbox(ui, lx + 152, ty + 94, !ifc.block_outbound, "Allow Outbound", ui->pen_fg, 16);
        at_draw_checkbox(ui, lx + 312, ty + 94, ifc.allow_same_interface, "Same-Interface", ui->pen_fg, 16);
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
        sprintf(buf, "%-4s %-10s %-8s %-10s",
                oyk_gui_table_column(OYK_TBL_TERMINATE, 0),
                oyk_gui_table_column(OYK_TBL_TERMINATE, 1),
                oyk_gui_table_column(OYK_TBL_TERMINATE, 2),
                oyk_gui_table_column(OYK_TBL_TERMINATE, 3));
        at_draw_text(ui, lx, ty + 8, buf, ui->pen_fg, bw / 8);
        at_hline(ui, lx, mx + mw - 12, ty + 28, ui->pen_fg);
        int row_idx = 0;
        for (int i = 0; i < ui->node->max_sessions() && row_idx < 6; ++i) {
            const OykSession& s = ui->node->session_slot(i);
            if (s.state == OYK_SESS_FREE) continue;
            oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
            sprintf(buf, "%s %-3lu %-10s %-8s CONNECTED",
                    (row_idx == 0) ? ">" : " ",
                    static_cast<unsigned long>(s.id), cs,
                    (s.iface_idx == 0) ? "axudp0" : "aux0");
            at_draw_text(ui, lx, ty + 34 + row_idx * 18, buf, ui->pen_fg, bw / 8);
            ++row_idx;
        }
        at_hline(ui, lx, mx + mw - 12, by - 8, ui->pen_fg);
        at_draw_button(ui, mx + mw - 196, by, 76, 22, "Cancel", 0);
        at_add_hit(ui, mx + mw - 196, by, 76, 22, 999);
        at_draw_button(ui, mx + mw - 114, by, 102, 22, "Terminate", 1);
    } else if (scr == OYK_SCR_BAN) {
        at_draw_field_row(ui, lx, ty + 4, "Callsign:", "N0BAD", 80, 100);
        at_draw_field_row(ui, lx + 190, ty + 4, "Reason:", "Beacon flood", 64, 150);
        at_draw_field_row(ui, lx + 414, ty + 4, "Dur:", "7d", 40, 56);
        at_draw_checkbox(ui, lx, ty + 28, 1, "Hard Ban (Drop AX.25)", ui->pen_fg, 24);
        at_draw_checkbox(ui, lx + 230, ty + 28, 0, "Allow Appeal", ui->pen_fg, 16);
        at_draw_text(ui, lx, ty + 52, "Callsign  Banned At        Lift At     Flags  Reason",
                     ui->pen_fg, bw / 8);
        for (int i = 0; i < ui->node->ban_count() && i < 4; ++i) {
            const OykBanEntry& be = ui->node->ban_entry(i);
            sprintf(buf, "%-9s %-16s %-10s  0x%02lX   %s",
                    be.key, be.banned_at, be.lift_at,
                    static_cast<unsigned long>(be.flags), be.reason);
            at_draw_text(ui, lx, ty + 72 + i * 18, buf, ui->pen_fg, bw / 8);
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
    } else if (scr == OYK_SCR_THEME) {
        at_draw_button(ui, lx, ty + 2, 26, 20, "<", 0);
        at_add_hit(ui, lx, ty + 2, 26, 20, 401);
        at_draw_button(ui, lx + 30, ty + 2, 26, 20, ">", 0);
        at_add_hit(ui, lx + 30, ty + 2, 26, 20, 402);
        sprintf(buf, "Theme: %s  (Whole-Screen Live Preview, %d colors)",
                ui->theme_ctrl.active_name, ui->colors);
        at_draw_text(ui, lx + 64, ty + 4, buf, ui->pen_fg, (bw - 68) / 8);
        at_draw_text(ui, lx, ty + 28, "Role             Key         Color    Preview",
                     ui->pen_fg, bw / 8);
        int max_rows = (ui->scr_h >= 460) ? 10 : 8;
        for (int i = 0; i < max_rows; ++i) {
            int ry = ty + 48 + i * 19;
            sprintf(buf, "%-16s %-11s #%06lX",
                    oyk_theme_role_label(i),
                    oyk_theme_role_key(i),
                    static_cast<unsigned long>(ui->active_theme.colors[i] & 0xFFFFFFUL));
            at_draw_text(ui, lx, ry, buf, ui->pen_fg, 46);
            int sw_pen = at_swatch_pen_for_role(ui, i);
            at_fill_rect(ui, lx + 350, ry + 2, 72, 13, sw_pen);
            at_frame_rect(ui, lx + 350, ry + 2, 72, 13, ui->pen_fg);
        }
        at_draw_button(ui, mx + mw - 172, by, 76, 22, "Cancel", 0);
        at_add_hit(ui, mx + mw - 172, by, 76, 22, 999);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Apply", 1);
        at_add_hit(ui, mx + mw - 88, by, 76, 22, 999);
    } else if (scr == OYK_SCR_TERMINAL) {
        at_draw_text(ui, lx, ty + 4, "KR4OYK-11 BBS | Ctrl+A H Help | S YAPP | W Wrap",
                     ui->pen_fg, bw / 8);
        at_fill_rect(ui, lx, ty + 24, bw, mh - 116, ui->pen_bg);
        at_frame_rect(ui, lx, ty + 24, bw, mh - 116, ui->pen_fg);
        at_draw_text(ui, lx + 8, ty + 32, "Welcome to KR4OYK-7 BBS on Atari ST GEM",
                     ui->pen_fg, (bw - 16) / 8);
        at_draw_text(ui, lx + 8, ty + 52, "EOL: Auto-Detected CRLF | Word-Wrap: ON (72 cols)",
                     ui->pen_fg, (bw - 16) / 8);
        at_draw_text(ui, lx + 8, ty + 72, "Type 'LIST' or 'B' to disconnect from BBS.",
                     ui->pen_fg, (bw - 16) / 8);
        at_draw_text(ui, lx + 8, ty + 92, "14:21:08  Msg #42  ALL  de W4ABC-7: Net tonight",
                     ui->pen_fg, (bw - 16) / 8);
        at_draw_field_row(ui, lx, by - 26, "Input:", "LIST_", 56, bw - 140);
        at_draw_button(ui, lx + bw - 76, by - 28, 76, 22, "Send", 1);
        at_draw_button(ui, lx, by, 112, 22, "Ctrl+A Help", 0);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Close", 0);
        at_add_hit(ui, mx + mw - 88, by, 76, 22, 999);
    } else if (scr == OYK_SCR_FILES) {
        int pw = (bw - 12) / 2;
        at_draw_text(ui, lx, ty + 4, "Pending Uploads", ui->pen_fg, pw / 8);
        at_draw_text(ui, lx + pw + 12, ty + 4, "Approved BBS Files", ui->pen_fg, pw / 8);
        at_fill_rect(ui, lx, ty + 24, pw, mh - 90, ui->pen_bg);
        at_frame_rect(ui, lx, ty + 24, pw, mh - 90, ui->pen_fg);
        at_fill_rect(ui, lx + pw + 12, ty + 24, pw, mh - 90, ui->pen_bg);
        at_frame_rect(ui, lx + pw + 12, ty + 24, pw, mh - 90, ui->pen_fg);
        at_draw_text(ui, lx + 8, ty + 30, "From     File         Size", ui->pen_fg, (pw - 16) / 8);
        at_draw_text(ui, lx + 8, ty + 50, "W4ABC-7  PACKETS.TXT  2048", ui->pen_fg, (pw - 16) / 8);
        at_draw_text(ui, lx + pw + 20, ty + 30, "Filename            Size", ui->pen_fg, (pw - 16) / 8);
        at_draw_text(ui, lx + pw + 20, ty + 50, "README.TXT          1420", ui->pen_fg, (pw - 16) / 8);
        at_draw_text(ui, lx + pw + 20, ty + 70, "NETLIST.CFG          618", ui->pen_fg, (pw - 16) / 8);
        at_draw_button(ui, lx, by, 92, 22, "Upload...", 0);
        at_draw_button(ui, lx + 98, by, 92, 22, "Approve", 1);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Close", 0);
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
#ifdef __ATARI__
    if (!ui->capture_mode) {
        if (ui->ap_id >= 0) graf_mouse(M_OFF, NULL);
        if (ui->vdi > 0) v_hide_c(ui->vdi);
    }
#endif
    at_draw_console(ui);
    if (ui->active_screen != OYK_SCR_MAIN) {
        at_draw_modal(ui);
    }
#ifdef __ATARI__
    if (!ui->capture_mode) {
        if (ui->vdi > 0) v_show_c(ui->vdi, 1);
        if (ui->ap_id >= 0) graf_mouse(M_ON, NULL);
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
    const char* bmp_path;
};

static const AtGalleryStep kAtGallery[12] = {
    { OYK_SCR_MAIN,       "C:\\AT_MAIN.BMP" },
    { OYK_SCR_SETTINGS,   "C:\\AT_SETT.BMP" },
    { OYK_SCR_ADD_IFACE,  "C:\\AT_ADDI.BMP" },
    { OYK_SCR_EDIT_IFACE, "C:\\AT_EDTI.BMP" },
    { OYK_SCR_PEERS,      "C:\\AT_PEER.BMP" },
    { OYK_SCR_EDIT_PEER,  "C:\\AT_EDTP.BMP" },
    { OYK_SCR_TERMINATE,  "C:\\AT_TERM.BMP" },
    { OYK_SCR_BAN,        "C:\\AT_BAN.BMP" },
    { OYK_SCR_EXIT_PEERS, "C:\\AT_EXIT.BMP" },
    { OYK_SCR_THEME,      "C:\\AT_THME.BMP" },
    { OYK_SCR_TERMINAL,   "C:\\AT_TML.BMP" },
    { OYK_SCR_FILES,      "C:\\AT_FILE.BMP" }
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
    ui->char_w = wchar;
    ui->char_h = hchar;
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
        for (int i = 0; i < 12; ++i) {
            ui->active_screen = kAtGallery[i].scr;
            ui->settings_tab = (ui->active_screen == OYK_SCR_PEERS) ? 4 : 0;
            at_redraw(ui);
#ifdef __ATARI__
            oyk_atari_capture_screen_bmp(kAtGallery[i].bmp_path, ui->vdi,
                                         ui->scr_w, ui->scr_h, ui->colors);
#endif
        }
        oyk_atari_finish_test();
#ifdef __ATARI__
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
            } else if (msg[0] == AP_TERM || msg[0] == WM_CLOSED) {
                ui->running = 0;
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
                ui->active_screen = OYK_SCR_MAIN;
                at_redraw(ui);
            } else if (act >= 100 && act < 107) {
                ui->active_screen = OYK_SCR_SETTINGS;
                ui->settings_tab = act - 100;
                at_redraw(ui);
            } else if (act >= 200 && act < 205) {
                oyk_gui_wiz_select_driver(&ui->wiz, &ui->node->config(), act - 200);
                at_redraw(ui);
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
            if (ch == 'q' || ch == 'Q' || ch == 27) {
                if (ui->active_screen != OYK_SCR_MAIN) {
                    ui->active_screen = OYK_SCR_MAIN;
                    at_redraw(ui);
                } else {
                    ui->running = 0;
                }
            } else if ((ui->active_screen == OYK_SCR_SETTINGS || ui->active_screen == OYK_SCR_PEERS) &&
                       ch >= '1' && ch <= '7') {
                ui->active_screen = OYK_SCR_SETTINGS;
                ui->settings_tab = ch - '1';
                at_redraw(ui);
            }
        }
        oyk_atari_net_poll(net);
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
