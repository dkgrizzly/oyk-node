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

    /* Repaint entire GEM screen workspace + top menu bar */
    at_fill_rect(ui, 0, 0, w, h, ui->pen_bg);
    at_fill_rect(ui, 0, 0, w, 19, ui->pen_menu_bg);
    at_hline(ui, 0, w - 1, 19, ui->pen_fg);
    at_draw_text(ui, 10, 2, "Desk  File  Edit  Node  View  Window  Help",
                 ui->pen_menu_fg, 46);
    {
        char rstat[32];
        sprintf(rstat, "%s  %s",
                node->accept_incoming() ? "IN:ALLOW" : "IN:DENY",
                (ui->tier == OYK_ATARI_MONO) ? "ST-HIGH" : "VDI-16");
        at_draw_text(ui, w - 148, 2, rstat, ui->pen_menu_fg, 18);
    }

    /* Status banner */
    char cs[16], al[16], banner[128];
    oyk_format_callsign(&cfg.callsign, cs, sizeof(cs));
    oyk_format_callsign(&cfg.alias, al, sizeof(al));
    oyk_gui_format_console_banner(cs, al, node->accept_incoming(),
                                  node->total_active_connections(), cfg.iface_count,
                                  banner, sizeof(banner));
    at_fill_rect(ui, 8, 24, w - 16, 20, ui->pen_status_bg);
    at_frame_rect(ui, 8, 24, w - 16, 20, ui->pen_fg);
    at_draw_text(ui, 14, 26, banner, ui->pen_status_fg, (w - 32) / 8);

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

    at_draw_button(ui, 8,   bar_y, 88, 22, "Terminal", 1);
    at_draw_button(ui, 102, bar_y, 68, 22, "Files", 0);
    at_draw_button(ui, 176, bar_y, 88, 22, "Settings", 0);
    at_draw_button(ui, 270, bar_y, 68, 22, "Theme", 0);
    at_draw_button(ui, 344, bar_y, 68, 22, "Peers", 0);
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

    int lx = mx + 12;
    int ty = my + 28;
    int bw = mw - 24;
    int by = my + mh - 30;
    const OykConfig& cfg = ui->node->config();
    char cs[16], al[16], buf[128];

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
            tx += tw;
        }
        at_hline(ui, lx, mx + mw - 12, ty + 24, ui->pen_fg);

        if (active_tab == 4) {
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
        } else {
            oyk_format_callsign(&cfg.callsign, cs, sizeof(cs));
            oyk_format_callsign(&cfg.alias, al, sizeof(al));
            sprintf(buf, "Callsign: %s   Alias: %s   Accept Incoming: %s",
                    cs, al, cfg.accept_incoming ? "YES" : "NO");
            at_draw_text(ui, lx, ty + 34, buf, ui->pen_fg, bw / 8);
            sprintf(buf, "BBS (-%u): %s   CHAT (-%u): %s   Interfaces: %d",
                    static_cast<unsigned int>(cfg.bbs_ssid), cfg.bbs_enabled ? "ON" : "OFF",
                    static_cast<unsigned int>(cfg.chat_ssid), cfg.chat_enabled ? "ON" : "OFF",
                    cfg.iface_count);
            at_draw_text(ui, lx, ty + 54, buf, ui->pen_fg, bw / 8);
            sprintf(buf, "Display: %dx%d (%d colors)   Theme: %s",
                    ui->scr_w, ui->scr_h, ui->colors, ui->theme_ctrl.active_name);
            at_draw_text(ui, lx, ty + 74, buf, ui->pen_fg, bw / 8);
            sprintf(buf, "Hardware: %s", ui->net ? ui->net->caps.backend_summary : "RS-232");
            at_draw_text(ui, lx, ty + 94, buf, ui->pen_fg, bw / 8);
            at_draw_button(ui, lx, by, 76, 22, "Save", 1);
        }
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Close", 0);
    } else if (scr == OYK_SCR_ADD_IFACE) {
        at_draw_text(ui, lx, ty + 4, "Step 1 of 4: Select Driver & Review", ui->pen_fg, bw / 8);
        for (int i = 0; i < 5; ++i) {
            sprintf(buf, "%s %s", (i == ui->wiz.selected_driver_idx) ? "[*]" : "[ ]",
                    oyk_gui_wiz_driver_type(ui->wiz.driver_mask, i));
            at_draw_text(ui, lx + 4, ty + 28 + i * 18, buf, ui->pen_fg, 28);
        }
        sprintf(buf, "ID: %s   Type: %s   Host: %s:%u",
                ui->wiz.draft.id, ui->wiz.draft.type,
                ui->wiz.draft.host, static_cast<unsigned int>(ui->wiz.draft.port));
        at_draw_text(ui, lx + 190, ty + 34, buf, ui->pen_fg, 44);
        at_draw_text(ui, lx + 190, ty + 54, "Same-Iface: NO   Inbound/Outbound: YES",
                     ui->pen_fg, 44);
        at_draw_button(ui, lx, by, 84, 22, "Create", 1);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Cancel", 0);
    } else if (scr == OYK_SCR_EDIT_IFACE) {
        const OykIfaceConfig& ifc = cfg.ifaces[0];
        sprintf(buf, "Interface ID: %s   Type: %s", ifc.id, ifc.type);
        at_draw_text(ui, lx, ty + 14, buf, ui->pen_fg, bw / 8);
        sprintf(buf, "Bind Host: %s   Port: %u   Netmask: %s",
                ifc.host, static_cast<unsigned int>(ifc.port), ifc.netmask);
        at_draw_text(ui, lx, ty + 36, buf, ui->pen_fg, bw / 8);
        at_draw_text(ui, lx, ty + 58, "Allow Inbound: [X]   Allow Outbound: [X]   Same-Iface: [ ]",
                     ui->pen_fg, bw / 8);
        at_draw_button(ui, lx, by, 76, 22, "Save", 1);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Cancel", 0);
    } else if (scr == OYK_SCR_EDIT_PEER) {
        const OykPeerConfig& pr = cfg.peers[0];
        sprintf(buf, "Peer ID: %s   Callsign: %s", pr.id, pr.callsign);
        at_draw_text(ui, lx, ty + 14, buf, ui->pen_fg, bw / 8);
        sprintf(buf, "Interface: %s   Endpoint: %s:%u",
                pr.interface_id, pr.host, static_cast<unsigned int>(pr.port));
        at_draw_text(ui, lx, ty + 36, buf, ui->pen_fg, bw / 8);
        at_draw_text(ui, lx, ty + 58, "Policy: STATIC   Inbound: YES   Outbound: YES",
                     ui->pen_fg, bw / 8);
        at_draw_button(ui, lx, by, 76, 22, "Save", 1);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Cancel", 0);
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
        at_draw_button(ui, lx, by, 102, 22, "Terminate", 1);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Cancel", 0);
    } else if (scr == OYK_SCR_BAN) {
        at_draw_text(ui, lx, ty + 8, "Callsign  Banned At        Lift At     Flags  Reason",
                     ui->pen_fg, bw / 8);
        for (int i = 0; i < ui->node->ban_count() && i < 6; ++i) {
            const OykBanEntry& be = ui->node->ban_entry(i);
            sprintf(buf, "%-9s %-16s %-10s  0x%02lX   %s",
                    be.key, be.banned_at, be.lift_at,
                    static_cast<unsigned long>(be.flags), be.reason);
            at_draw_text(ui, lx, ty + 32 + i * 18, buf, ui->pen_fg, bw / 8);
        }
        at_draw_button(ui, lx, by, 92, 22, "Add Ban", 1);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Close", 0);
    } else if (scr == OYK_SCR_EXIT_PEERS) {
        sprintf(buf, "%d auto-learned peer(s) found during session.",
                ui->node->auto_learned_peer_count());
        at_draw_text(ui, lx, ty + 18, buf, ui->pen_fg, bw / 8);
        at_draw_text(ui, lx, ty + 42, "Keep learned peers in node configuration before exit?",
                     ui->pen_fg, bw / 8);
        at_draw_button(ui, lx, by, 112, 22, "Save & Exit", 1);
        at_draw_button(ui, lx + 120, by, 134, 22, "Discard & Exit", 0);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Cancel", 0);
    } else if (scr == OYK_SCR_THEME) {
        sprintf(buf, "Theme: < %s >  (Whole-Screen Live Preview, %d colors)",
                ui->theme_ctrl.active_name, ui->colors);
        at_draw_text(ui, lx, ty + 4, buf, ui->pen_fg, bw / 8);
        at_draw_text(ui, lx, ty + 26, "Role             Key         Color    Preview",
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
        at_draw_button(ui, lx, by, 108, 22, "Next Theme", 0);
        at_draw_button(ui, lx + 116, by, 76, 22, "Apply", 1);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Cancel", 0);
    } else if (scr == OYK_SCR_TERMINAL) {
        at_draw_text(ui, lx, ty + 4, "Connected to KR4OYK-11 BBS  (Ctrl+A ? = Hotkey Help)",
                     ui->pen_fg, bw / 8);
        at_fill_rect(ui, lx, ty + 26, bw, mh - 92, ui->pen_bg);
        at_frame_rect(ui, lx, ty + 26, bw, mh - 92, ui->pen_fg);
        at_draw_text(ui, lx + 8, ty + 34, "Welcome to KR4OYK-7 BBS on Atari ST GEM",
                     ui->pen_fg, (bw - 16) / 8);
        at_draw_text(ui, lx + 8, ty + 54, "Ctrl+A H Help | Ctrl+A S YAPP Send | Ctrl+A W Wrap",
                     ui->pen_fg, (bw - 16) / 8);
        at_draw_text(ui, lx + 8, ty + 74, "EOL: Auto-Detected CRLF | Word-Wrap: ON (72 cols)",
                     ui->pen_fg, (bw - 16) / 8);
        at_draw_text(ui, lx + 8, ty + 94, "Type 'LIST' or 'B' to disconnect from BBS.",
                     ui->pen_fg, (bw - 16) / 8);
        at_draw_text(ui, lx + 8, ty + 160, "> LIST_", ui->pen_fg, (bw - 16) / 8);
        at_draw_button(ui, lx, by, 112, 22, "Ctrl+A Help", 0);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Close", 1);
    } else if (scr == OYK_SCR_FILES) {
        int pw = (bw - 12) / 2;
        at_draw_text(ui, lx, ty + 4, "Pending Uploads", ui->pen_fg, pw / 8);
        at_draw_text(ui, lx + pw + 12, ty + 4, "Approved BBS Files", ui->pen_fg, pw / 8);
        at_fill_rect(ui, lx, ty + 26, pw, mh - 92, ui->pen_bg);
        at_frame_rect(ui, lx, ty + 26, pw, mh - 92, ui->pen_fg);
        at_fill_rect(ui, lx + pw + 12, ty + 26, pw, mh - 92, ui->pen_bg);
        at_frame_rect(ui, lx + pw + 12, ty + 26, pw, mh - 92, ui->pen_fg);
        at_draw_text(ui, lx + 8, ty + 34, "W4ABC-7 PACKETS.TXT 2048", ui->pen_fg, (pw - 16) / 8);
        at_draw_text(ui, lx + pw + 20, ty + 34, "README.TXT         1420", ui->pen_fg, (pw - 16) / 8);
        at_draw_text(ui, lx + pw + 20, ty + 54, "NETLIST.CFG         618", ui->pen_fg, (pw - 16) / 8);
        at_draw_button(ui, lx, by, 92, 22, "Upload...", 0);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Close", 1);
    } else {
        at_draw_text(ui, lx, ty + 18, "Hamlib rigctld CAT: Port 4532 / RS-232 AUX @ 9600",
                     ui->pen_fg, bw / 8);
        at_draw_button(ui, mx + mw - 88, by, 76, 22, "Close", 1);
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
        short ev = evnt_multi(MU_KEYBD | MU_MESAG | MU_TIMER,
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
        if (ev & MU_KEYBD) {
            int ch = key & 0xFF;
            if (ch == 'q' || ch == 'Q' || ch == 27) {
                if (ui->active_screen != OYK_SCR_MAIN) {
                    ui->active_screen = OYK_SCR_MAIN;
                    at_redraw(ui);
                } else {
                    ui->running = 0;
                }
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
