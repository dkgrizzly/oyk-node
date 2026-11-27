/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "gsos_ui.h"
#include "gsos_capture.h"

#include <stdio.h>
#include <string.h>

#ifdef __VBCC__
#include <OS/Types.h>
#include <OS/Locator.h>
#include <OS/Memory.h>
#include <OS/MiscTool.h>
#include <OS/QuickDraw.h>
#include <OS/Event.h>
#endif

typedef struct {
    short v1, h1, v2, h2;
} GsRect;

typedef struct {
    int x, y, w, h, action;
} GsHitZone;

typedef struct {
    OykGsosNode       *node;
    OykGsosNetState   *net;
    int                active_screen;
    int                settings_tab;
    int                sel_iface;
    int                sel_peer;
    int                sel_conn;
    int                sel_ban;
    int                sel_theme_role;
    int                running;
    int                hit_count;
    GsHitZone          hits[28];
    OykGsosConfig      draft_cfg;
    OykGsosAddIfaceWiz wiz;
    OykGsosThemeCtrl   theme_ctrl;
    OykGsosThemeConfig active_theme;
    int                console_draw_count;
    int                modal_draw_count;
    gsos_u32           last_console_bg;
    gsos_u32           last_modal_bg;
    int                qd_ready;
#ifdef __VBCC__
    Word               uid;
    Handle             dp_handle;
#endif
} OykGsosUiState;

static OykGsosUiState g_ui_state;

/* In Apple IIGS 640-mode QuickDraw II, 4 pure non-dithered QD colors are 0, 5, 10, 15. */
#define GS_CLR_BG      0
#define GS_CLR_PANEL   5
#define GS_CLR_ACCENT  10
#define GS_CLR_FG      15

#ifdef __VBCC__
static unsigned short gs_rgb24_to_0rgb(gsos_u32 rgb) {
    unsigned int r4 = (unsigned int)((rgb >> 20) & 0x0FUL);
    unsigned int g4 = (unsigned int)((rgb >> 12) & 0x0FUL);
    unsigned int b4 = (unsigned int)((rgb >> 4)  & 0x0FUL);
    return (unsigned short)((r4 << 8) | (g4 << 4) | b4);
}
#endif

static void gs_apply_theme_palette(const OykGsosThemeConfig *pal, int active_screen) {
#ifdef __VBCC__
    volatile unsigned char *pals = (volatile unsigned char *)0xE19E00UL;
    volatile unsigned char *scbs = (volatile unsigned char *)0xE19D00UL;
    unsigned short c[4];
    int y, t, i;
    if (!pal) return;
    c[0] = gs_rgb24_to_0rgb(pal->colors[OYK_GSOS_TR_BG]);
    c[1] = gs_rgb24_to_0rgb(pal->colors[OYK_GSOS_TR_PANEL_BG]);
    c[2] = gs_rgb24_to_0rgb(pal->colors[OYK_GSOS_TR_ACCENT]);
    c[3] = gs_rgb24_to_0rgb(pal->colors[OYK_GSOS_TR_FG]);
    /* Write uniform quad into Color Table 0 and copy base into 1..15. */
    for (t = 0; t < 16; ++t) {
        volatile unsigned char *tp = pals + (unsigned long)t * 32UL;
        for (i = 0; i < 16; ++i) {
            unsigned short w = c[i & 3];
            /* If on Theme Settings screen, palette t (t>=2) carries role swatch color in slot 2! */
            if (active_screen == OYK_GSOS_SCR_THEME && t >= 2 && (i & 3) == 2) {
                int role = (t - 2);
                if (role < OYK_GSOS_THEME_COLOR_COUNT) {
                    w = gs_rgb24_to_0rgb(pal->colors[role]);
                }
            }
            tp[i * 2]     = (unsigned char)(w & 0xFFU);
            tp[i * 2 + 1] = (unsigned char)((w >> 8) & 0xFFU);
        }
    }
    for (y = 0; y < 200; ++y) {
        scbs[y] = 0x80;
    }
    if (active_screen == OYK_GSOS_SCR_THEME) {
        /* Assign palette 2..10 to the Theme role swatch rows (y=58..154). */
        for (i = 0; i < 9; ++i) {
            int sy0 = 58 + i * 11;
            int sy;
            for (sy = sy0; sy < sy0 + 9 && sy < 199; ++sy) {
                scbs[sy] = (unsigned char)(0x80 | (2 + i));
            }
        }
    }
#else
    (void)pal;
    (void)active_screen;
#endif
}

static int gs_qd_init(OykGsosUiState *ui) {
    if (!ui) return 0;
    if (ui->qd_ready) return 1;
#ifdef __VBCC__
    {
        Word dp = 0;
        TLStartUp();
        ui->uid = MMStartUp();
        if (!ui->uid) ui->uid = 0x1000;
        MTStartUp();
        ui->dp_handle = NewHandle(0x400UL, ui->uid, 0xC005, (Ptr)0L);
        if (ui->dp_handle) {
            dp = (Word)(*(LongWord *)ui->dp_handle);
        }
        QDStartUp(ui->uid, 640, 0x80, dp);
        GrafOn();
        InitCursor();
        HideCursor();
        SetMasterSCB(0x80);
        SetAllSCBs(0x80);
    }
#endif
    ui->qd_ready = 1;
    return 1;
}

static void gs_fill_rect(int x, int y, int w, int h, int pat_idx) {
#ifdef __VBCC__
    GsRect r;
    if (w <= 0 || h <= 0) return;
    r.v1 = (short)y;
    r.h1 = (short)x;
    r.v2 = (short)(y + h);
    r.h2 = (short)(x + w);
    SetSolidPenPat((Word)pat_idx);
    PaintRect(&r);
#else
    (void)x; (void)y; (void)w; (void)h; (void)pat_idx;
#endif
}

static void gs_frame_rect(int x, int y, int w, int h, int pat_idx) {
#ifdef __VBCC__
    GsRect r;
    if (w <= 0 || h <= 0) return;
    r.v1 = (short)y;
    r.h1 = (short)x;
    r.v2 = (short)(y + h);
    r.h2 = (short)(x + w);
    SetSolidPenPat((Word)pat_idx);
    FrameRect(&r);
#else
    (void)x; (void)y; (void)w; (void)h; (void)pat_idx;
#endif
}

static void gs_hline(int x1, int x2, int y, int pat_idx) {
#ifdef __VBCC__
    SetSolidPenPat((Word)pat_idx);
    MoveTo((Word)y, (Word)x1);
    LineTo((Word)y, (Word)x2);
#else
    (void)x1; (void)x2; (void)y; (void)pat_idx;
#endif
}

static void gs_draw_text(int x, int y, const char *s, int fg_idx, int bg_idx, int max_chars) {
#ifdef __VBCC__
    char buf[96];
    size_t n, lim;
    if (!s || !s[0] || max_chars <= 0) return;
    lim = (size_t)max_chars;
    if (lim >= sizeof(buf)) lim = sizeof(buf) - 1U;
    n = strlen(s);
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
    SetTextMode(0);
    SetForeColor((Word)fg_idx);
    SetBackColor((Word)bg_idx);
    MoveTo((Word)(y + 8), (Word)x);
    DrawCString(buf);
#else
    (void)x; (void)y; (void)s; (void)fg_idx; (void)bg_idx; (void)max_chars;
#endif
}

static void gs_reset_hits(OykGsosUiState *ui) {
    if (ui) ui->hit_count = 0;
}

static void gs_add_hit(OykGsosUiState *ui, int x, int y, int w, int h, int action) {
    GsHitZone *z;
    if (!ui || ui->hit_count >= 28 || w <= 0 || h <= 0) return;
    z = &ui->hits[ui->hit_count++];
    z->x = x;
    z->y = y;
    z->w = w;
    z->h = h;
    z->action = action;
}

static void gs_draw_checkbox(int x, int y, int checked, const char *label, int max_chars) {
    gs_fill_rect(x, y, 12, 8, GS_CLR_BG);
    gs_frame_rect(x, y, 12, 8, GS_CLR_FG);
    if (checked) {
        gs_fill_rect(x + 2, y + 4, 2, 2, GS_CLR_ACCENT);
        gs_fill_rect(x + 4, y + 5, 2, 2, GS_CLR_ACCENT);
        gs_fill_rect(x + 6, y + 3, 2, 2, GS_CLR_ACCENT);
        gs_fill_rect(x + 8, y + 2, 2, 2, GS_CLR_ACCENT);
    }
    if (label && label[0]) {
        gs_draw_text(x + 17, y, label, GS_CLR_FG, GS_CLR_BG, max_chars);
    }
}

static void gs_draw_radio(int x, int y, int selected, const char *label, int max_chars) {
    gs_fill_rect(x + 2, y + 1, 8, 6, GS_CLR_BG);
    gs_hline(x + 3, x + 8, y, GS_CLR_FG);
    gs_hline(x + 3, x + 8, y + 7, GS_CLR_FG);
    gs_fill_rect(x + 1, y + 1, 2, 1, GS_CLR_FG);
    gs_fill_rect(x + 9, y + 1, 2, 1, GS_CLR_FG);
    gs_fill_rect(x, y + 2, 2, 4, GS_CLR_FG);
    gs_fill_rect(x + 10, y + 2, 2, 4, GS_CLR_FG);
    gs_fill_rect(x + 1, y + 6, 2, 1, GS_CLR_FG);
    gs_fill_rect(x + 9, y + 6, 2, 1, GS_CLR_FG);
    if (selected) {
        gs_fill_rect(x + 4, y + 2, 4, 4, GS_CLR_ACCENT);
    }
    if (label && label[0]) {
        gs_draw_text(x + 17, y, label, GS_CLR_FG, GS_CLR_BG, max_chars);
    }
}

static void gs_draw_field_row(int x, int y, const char *label, const char *value,
                              int label_w, int box_w) {
    int bx = x + label_w;
    if (label && label[0]) {
        gs_draw_text(x, y + 1, label, GS_CLR_FG, GS_CLR_BG, label_w / 7);
    }
    gs_fill_rect(bx, y, box_w, 11, GS_CLR_BG);
    gs_frame_rect(bx, y, box_w, 11, GS_CLR_FG);
    if (value && value[0]) {
        gs_draw_text(bx + 4, y + 1, value, GS_CLR_FG, GS_CLR_BG, (box_w - 8) / 7);
    }
}

static void gs_draw_box(int x, int y, int w, int h, const char *title) {
    gs_fill_rect(x, y, w, h, GS_CLR_BG);
    gs_frame_rect(x, y, w, h, GS_CLR_FG);
    gs_fill_rect(x + 1, y + 1, w - 2, 10, GS_CLR_PANEL);
    gs_hline(x, x + w - 1, y + 11, GS_CLR_FG);
    if (title && title[0]) {
        gs_draw_text(x + 6, y + 1, title, GS_CLR_FG, GS_CLR_PANEL, (w - 12) / 7);
    }
}

static void gs_draw_button(int x, int y, int w, int h, const char *label, int is_primary) {
    int bg = is_primary ? GS_CLR_ACCENT : GS_CLR_PANEL;
    int fg = is_primary ? GS_CLR_BG : GS_CLR_FG;
    gs_fill_rect(x, y, w, h, bg);
    gs_frame_rect(x, y, w, h, GS_CLR_FG);
    if (label) {
        gs_draw_text(x + 6, y + 1, label, fg, bg, (w - 10) / 7);
    }
}

static void gs_draw_listview_header(int x, int y, int w, int ncols,
                                    const int *col_x, const char * const *cols,
                                    int bg, int fg) {
    int c;
    gs_fill_rect(x, y, w, 10, bg);
    for (c = 0; c < ncols; ++c) {
        if (cols[c] && cols[c][0]) {
            gs_draw_text(col_x[c], y, cols[c], fg, bg, 24);
        }
    }
}

static void gs_draw_listview_row(int x, int y, int w, int h, int selected,
                                 int ncols, const int *col_x,
                                 const char * const *cells, const int *max_c,
                                 int norm_bg, int norm_fg) {
    int c;
    int bg = selected ? GS_CLR_ACCENT : norm_bg;
    int fg = selected ? GS_CLR_BG : norm_fg;
    gs_fill_rect(x, y, w, h, bg);
    for (c = 0; c < ncols; ++c) {
        if (cells[c] && cells[c][0]) {
            gs_draw_text(col_x[c], y, cells[c], fg, bg, max_c[c]);
        }
    }
}

static void gs_draw_menubar(const OykGsosNode *node) {
    (void)node;
    gs_fill_rect(0, 0, 640, 12, GS_CLR_PANEL);
    gs_hline(0, 639, 12, GS_CLR_FG);
    gs_draw_text(8, 1, "Apple  File  Edit  Node  View  Window  Help",
                 GS_CLR_FG, GS_CLR_PANEL, 48);
    gs_draw_text(508, 1, "Apple IIGS GS/OS", GS_CLR_FG, GS_CLR_PANEL, 18);
}

static void gs_draw_console(OykGsosUiState *ui) {
    const OykGsosNode *node = ui->node;
    char cs[16], al[16], hdr[128];
    const char *pill;
    int pill_w;
    int i, shown_c;

    ++ui->console_draw_count;
    ui->last_console_bg = ui->active_theme.colors[OYK_GSOS_TR_BG];
    gs_reset_hits(ui);

    gs_fill_rect(0, 13, 640, 187, GS_CLR_PANEL);
    gs_draw_menubar(node);

    /* Framed GS/OS Platinum Desktop Window */
    gs_fill_rect(6, 16, 632, 184, GS_CLR_FG);
    gs_fill_rect(4, 14, 632, 184, GS_CLR_BG);
    gs_frame_rect(4, 14, 632, 184, GS_CLR_FG);
    gs_fill_rect(5, 15, 630, 11, GS_CLR_ACCENT);
    gs_hline(4, 635, 26, GS_CLR_FG);
    gs_fill_rect(9, 17, 9, 7, GS_CLR_BG);
    gs_frame_rect(9, 17, 9, 7, GS_CLR_FG);
    gs_draw_text(24, 16, "OYK Amateur Radio Packet Node", GS_CLR_BG, GS_CLR_ACCENT, 40);

    /* Top Status Banner with canonical header + right incoming badge */
    oyk_gsos_format_callsign(&node->cfg.callsign, cs, sizeof(cs));
    oyk_gsos_format_callsign(&node->cfg.alias, al, sizeof(al));
    oyk_gsos_format_console_header(cs, al, oyk_gsos_node_active_conn_count(node),
                                   hdr, sizeof(hdr));
    pill = oyk_gsos_format_incoming_pill(node->cfg.accept_incoming);
    pill_w = (int)strlen(pill) * 7 + 10;
    gs_fill_rect(10, 28, 620, 11, GS_CLR_ACCENT);
    gs_frame_rect(10, 28, 620, 11, GS_CLR_FG);
    gs_draw_text(16, 29, hdr, GS_CLR_BG, GS_CLR_ACCENT, (612 - pill_w) / 7);
    gs_fill_rect(626 - pill_w, 29, pill_w, 9, GS_CLR_PANEL);
    gs_frame_rect(626 - pill_w, 29, pill_w, 9, GS_CLR_FG);
    gs_draw_text(630 - pill_w, 29, pill, GS_CLR_FG, GS_CLR_PANEL, (int)strlen(pill));

    /* Side-by-side Interfaces & Last 5 New Callsigns Heard */
    {
        int if_x[4];
        const char *if_h[4];
        int if_w[4];
        if_x[0] = 16; if_x[1] = 80; if_x[2] = 166; if_x[3] = 216;
        if_h[0] = "Port"; if_h[1] = "Type"; if_h[2] = "State"; if_h[3] = "RX/TX";
        if_w[0] = 9; if_w[1] = 11; if_w[2] = 6; if_w[3] = 12;
        gs_draw_box(10, 41, 307, 47, "Interfaces");
        gs_draw_listview_header(12, 53, 303, 4, if_x, if_h, GS_CLR_BG, GS_CLR_FG);
        for (i = 0; i < node->cfg.iface_count && i < 2; ++i) {
            char rxtx[20];
            const char *cells[4];
            sprintf(rxtx, "%lu/%lu",
                    (unsigned long)node->iface_rt[i].rx_frames,
                    (unsigned long)node->iface_rt[i].tx_frames);
            cells[0] = node->cfg.ifaces[i].id;
            cells[1] = node->cfg.ifaces[i].type;
            cells[2] = node->iface_rt[i].running ? "UP" : "DOWN";
            cells[3] = rxtx;
            gs_draw_listview_row(12, 63 + i * 10, 303, 9, 0, 4, if_x, cells, if_w,
                                 GS_CLR_BG, GS_CLR_FG);
        }
    }

    {
        int hd_x[4];
        const char *hd_h[4];
        int hd_w[4];
        hd_x[0] = 329; hd_x[1] = 354; hd_x[2] = 436; hd_x[3] = 502;
        hd_h[0] = "#"; hd_h[1] = "Callsign"; hd_h[2] = "Port"; hd_h[3] = "Heard";
        hd_w[0] = 3; hd_w[1] = 10; hd_w[2] = 8; hd_w[3] = 10;
        gs_draw_box(323, 41, 307, 47, "Last 5 New Callsigns Heard");
        gs_draw_listview_header(325, 53, 303, 4, hd_x, hd_h, GS_CLR_BG, GS_CLR_FG);
        for (i = 0; i < node->heard_count && i < 2; ++i) {
            char idx_s[8];
            const char *cells[4];
            sprintf(idx_s, "%d", i + 1);
            cells[0] = idx_s;
            cells[1] = node->heard[i].callsign;
            cells[2] = node->heard[i].iface_id;
            cells[3] = node->heard[i].time_hms;
            gs_draw_listview_row(325, 63 + i * 10, 303, 9, 0, 4, hd_x, cells, hd_w,
                                 GS_CLR_BG, GS_CLR_FG);
        }
    }

    /* Active Connections */
    {
        int cn_x[6];
        const char *cn_h[6];
        int cn_w[6];
        cn_x[0] = 16; cn_x[1] = 52; cn_x[2] = 140;
        cn_x[3] = 214; cn_x[4] = 268; cn_x[5] = 358;
        cn_h[0] = "ID"; cn_h[1] = "Callsign"; cn_h[2] = "Port";
        cn_h[3] = "Dir"; cn_h[4] = "State"; cn_h[5] = "Since";
        cn_w[0] = 4; cn_w[1] = 10; cn_w[2] = 9; cn_w[3] = 6; cn_w[4] = 10; cn_w[5] = 10;
        gs_draw_box(10, 90, 620, 41, "Active Connections");
        gs_draw_listview_header(12, 102, 616, 6, cn_x, cn_h, GS_CLR_BG, GS_CLR_FG);
        shown_c = 0;
        for (i = 0; i < OYK_GSOS_MAX_SESSIONS && shown_c < 2; ++i) {
            const OykGsosSession *s = &node->sessions[i];
            const char *pname;
            char id_s[12];
            const char *cells[6];
            if (s->state == 0) continue;
            pname = (s->iface_idx >= 0 && s->iface_idx < node->cfg.iface_count)
                        ? node->cfg.ifaces[s->iface_idx].id : "local";
            sprintf(id_s, "%lu", (unsigned long)s->id);
            cells[0] = id_s;
            cells[1] = s->callsign;
            cells[2] = pname;
            cells[3] = s->is_local_term ? "LOCAL" : "IN";
            cells[4] = (s->state == 2) ? "CONNECTED" : "OPENING";
            cells[5] = s->since_hms;
            gs_draw_listview_row(12, 112 + shown_c * 10, 616, 9, 0, 6, cn_x, cells, cn_w,
                                 GS_CLR_BG, GS_CLR_FG);
            ++shown_c;
        }
    }

    /* Node Activity Log */
    gs_draw_box(10, 133, 620, 44, "Node Activity Log");
    {
        int start = (node->log_count > 3) ? (node->log_count - 3) : 0;
        for (i = start; i < node->log_count; ++i) {
            gs_draw_text(16, 146 + (i - start) * 9, node->logs[i],
                         GS_CLR_FG, GS_CLR_BG, 80);
        }
    }

    /* Bottom Footer Quick Buttons inside window client area */
    gs_draw_button(10,  180, 84, 13, "Terminal", 1); gs_add_hit(ui, 10,  180, 84, 13, OYK_GSOS_SCR_TERMINAL);
    gs_draw_button(100, 180, 84, 13, "Files",    0); gs_add_hit(ui, 100, 180, 84, 13, OYK_GSOS_SCR_FILES);
    gs_draw_button(190, 180, 84, 13, "Settings", 0); gs_add_hit(ui, 190, 180, 84, 13, OYK_GSOS_SCR_SETTINGS);
    gs_draw_button(280, 180, 84, 13, "Theme",    0); gs_add_hit(ui, 280, 180, 84, 13, OYK_GSOS_SCR_THEME);
    gs_draw_button(370, 180, 84, 13, "Peers",    0); gs_add_hit(ui, 370, 180, 84, 13, OYK_GSOS_SCR_PEERS);
    gs_draw_text(464, 182, "SCC .A/.B | GS/OS", GS_CLR_FG, GS_CLR_BG, 22);
}

static void gs_draw_modal(OykGsosUiState *ui) {
    int scr = ui->active_screen;
    int mx = 44, my = 20, mw = 552, mh = 160;
    const char *title = oyk_gsos_screen_title(scr);
    int lx = mx + 12;
    int ty = my + 16;
    int by = my + mh - 18;
    const OykGsosConfig *cfg = &ui->node->cfg;
    char cs[16], al[16], buf[128];
    int i;

    if (scr == OYK_GSOS_SCR_PEERS) title = oyk_gsos_screen_title(OYK_GSOS_SCR_SETTINGS);

    ++ui->modal_draw_count;
    ui->last_modal_bg = ui->active_theme.colors[OYK_GSOS_TR_BG];

    if (scr == OYK_GSOS_SCR_MENU) {
        int mmx = 8, mmy = 13, mmw = 248, mmh = 148;
        int my_pos = mmy + 4;
        gs_fill_rect(mmx + 4, mmy + 3, mmw, mmh, GS_CLR_FG);
        gs_fill_rect(mmx, mmy, mmw, mmh, GS_CLR_BG);
        gs_frame_rect(mmx, mmy, mmw, mmh, GS_CLR_FG);
        for (i = 0; i < oyk_gsos_node_menu_count(); ++i) {
            const OykGsosMenuItem *mi = oyk_gsos_node_menu_item(i);
            if (!mi) continue;
            if (mi->is_separator) {
                gs_hline(mmx + 2, mmx + mmw - 3, my_pos + 3, GS_CLR_FG);
                my_pos += 6;
            } else {
                int hl = (i == 0);
                char mrow[64];
                if (mi->shortcut_key) {
                    sprintf(mrow, "[%c] %s", mi->shortcut_key, mi->label);
                } else {
                    sprintf(mrow, "    %s", mi->label);
                }
                if (hl) {
                    gs_fill_rect(mmx + 2, my_pos, mmw - 4, 10, GS_CLR_ACCENT);
                    gs_draw_text(mmx + 8, my_pos, mrow, GS_CLR_BG, GS_CLR_ACCENT, 32);
                } else {
                    gs_draw_text(mmx + 8, my_pos, mrow, GS_CLR_FG, GS_CLR_BG, 32);
                }
                my_pos += 11;
            }
        }
        gs_add_hit(ui, 0, 0, 640, 12, 999);
        return;
    }

    /* Drop shadow + floating GS/OS Platinum modal with top-left close square */
    gs_fill_rect(mx + 4, my + 3, mw, mh, GS_CLR_FG);
    gs_fill_rect(mx, my, mw, mh, GS_CLR_BG);
    gs_frame_rect(mx, my, mw, mh, GS_CLR_FG);
    gs_fill_rect(mx + 1, my + 1, mw - 2, 12, GS_CLR_ACCENT);
    gs_hline(mx, mx + mw - 1, my + 13, GS_CLR_FG);
    gs_fill_rect(mx + 5, my + 3, 9, 7, GS_CLR_BG);
    gs_frame_rect(mx + 5, my + 3, 9, 7, GS_CLR_FG);
    gs_add_hit(ui, mx + 4, my + 2, 12, 10, 999);
    gs_draw_text(mx + 20, my + 2, title, GS_CLR_BG, GS_CLR_ACCENT, 64);

    if (scr == OYK_GSOS_SCR_SETTINGS || scr == OYK_GSOS_SCR_PEERS) {
        int active_tab = (scr == OYK_GSOS_SCR_PEERS) ? 4 : ui->settings_tab;
        int tx = lx;
        int tw = 74;
        char nbuf[32];
        for (i = 0; i < oyk_gsos_settings_tab_count(); ++i) {
            const char *tt = oyk_gsos_settings_tab_title(i);
            char lbl[16];
            int sel = (i == active_tab);
            if (strcmp(tt, "Interfaces") == 0) tt = "Ifaces";
            else if (strcmp(tt, "Printer") == 0) tt = "Prn";
            sprintf(lbl, "%d:%s", i + 1, tt);
            gs_fill_rect(tx, ty, tw - 2, 11, sel ? GS_CLR_ACCENT : GS_CLR_PANEL);
            gs_frame_rect(tx, ty, tw - 2, 11, GS_CLR_FG);
            gs_draw_text(tx + 3, ty + 1, lbl, sel ? GS_CLR_BG : GS_CLR_FG,
                         sel ? GS_CLR_ACCENT : GS_CLR_PANEL, 9);
            gs_add_hit(ui, tx, ty, tw - 2, 11, 100 + i);
            tx += tw;
        }
        gs_hline(lx, mx + mw - 12, ty + 13, GS_CLR_FG);

        if (active_tab == 0) {
            oyk_gsos_format_callsign(&cfg->callsign, cs, sizeof(cs));
            oyk_gsos_format_callsign(&cfg->alias, al, sizeof(al));
            gs_draw_field_row(lx, ty + 17, "Callsign:", cs, 84, 130);
            gs_draw_field_row(lx + 236, ty + 17, "Alias:", al, 56, 130);
            gs_draw_checkbox(lx, ty + 33, cfg->accept_incoming,
                             "Accept Incoming Connections", 36);
            gs_add_hit(ui, lx, ty + 33, 240, 10, 301);
            sprintf(buf, "Default EOL: %s   Interfaces: %d   Peers: %d",
                    (cfg->line_ending == OYK_GSOS_EOL_CRLF) ? "CRLF"
                    : (cfg->line_ending == OYK_GSOS_EOL_LF) ? "LF" : "CR",
                    cfg->iface_count, cfg->peer_count);
            gs_draw_text(lx, ty + 48, buf, GS_CLR_FG, GS_CLR_BG, 70);
            sprintf(buf, "Theme: %s   Display: 640x200 SHR QuickDraw II",
                    ui->theme_ctrl.active_name);
            gs_draw_text(lx, ty + 62, buf, GS_CLR_FG, GS_CLR_BG, 70);
        } else if (active_tab == 1) {
            gs_draw_checkbox(lx, ty + 17, cfg->bbs_enabled,
                             "Enable Packet BBS Store & Forward", 38);
            sprintf(nbuf, "-%u", (unsigned int)cfg->bbs_ssid);
            gs_draw_field_row(lx + 320, ty + 16, "SSID:", nbuf, 48, 56);
            gs_draw_checkbox(lx, ty + 31, cfg->bbs_files_enabled,
                             "Enable BBS Shared Files Area", 38);
            gs_draw_field_row(lx, ty + 46, "Mail Path:", cfg->bbs_mail_path, 92, 340);
            gs_draw_field_row(lx, ty + 60, "Files Path:", cfg->bbs_files_path, 92, 340);
        } else if (active_tab == 2) {
            gs_draw_checkbox(lx, ty + 17, cfg->chat_enabled,
                             "Enable Real-Time CHAT Conference", 40);
            sprintf(nbuf, "-%u", (unsigned int)cfg->chat_ssid);
            gs_draw_field_row(lx, ty + 33, "CHAT SSID:", nbuf, 92, 72);
            gs_draw_field_row(lx, ty + 48, "Topic:", cfg->chat_topic, 92, 300);
        } else if (active_tab == 3) {
            int ix[4];
            const char *ih[4];
            int iw[4];
            ix[0] = lx + 2; ix[1] = lx + 82; ix[2] = lx + 178; ix[3] = lx + 354;
            ih[0] = "ID"; ih[1] = "Driver"; ih[2] = "Endpoint / Device"; ih[3] = "Policy";
            iw[0] = 10; iw[1] = 12; iw[2] = 22; iw[3] = 20;
            gs_draw_listview_header(lx, ty + 16, mw - 24, 4, ix, ih, GS_CLR_PANEL, GS_CLR_FG);
            for (i = 0; i < cfg->iface_count && i < 4; ++i) {
                char ep[40], pol[28];
                const char *cells[4];
                if (cfg->ifaces[i].device[0]) {
                    sprintf(ep, "%s @ %lu", cfg->ifaces[i].device,
                            (unsigned long)cfg->ifaces[i].baud);
                } else {
                    sprintf(ep, "%s:%u", cfg->ifaces[i].host,
                            (unsigned int)cfg->ifaces[i].port);
                }
                sprintf(pol, "IN:%s OUT:%s %s",
                        cfg->ifaces[i].allow_inbound ? "Y" : "N",
                        cfg->ifaces[i].allow_outbound ? "Y" : "N",
                        oyk_gsos_format_learned_policy(cfg->ifaces[i].learned_peer_policy));
                cells[0] = cfg->ifaces[i].id;
                cells[1] = cfg->ifaces[i].type;
                cells[2] = ep;
                cells[3] = pol;
                gs_draw_listview_row(lx, ty + 28 + i * 11, mw - 24, 10,
                                     (i == ui->sel_iface), 4, ix, cells, iw,
                                     GS_CLR_BG, GS_CLR_FG);
            }
            gs_draw_button(lx, by, 86, 13, "Add Iface", 0);
            gs_add_hit(ui, lx, by, 86, 13, OYK_GSOS_SCR_ADD_IFACE);
            gs_draw_button(lx + 92, by, 86, 13, "Edit Iface", 0);
            gs_add_hit(ui, lx + 92, by, 86, 13, OYK_GSOS_SCR_EDIT_IFACE);
        } else if (active_tab == 4) {
            int px[5];
            const char *ph[5];
            int pw[5];
            px[0] = lx + 2; px[1] = lx + 78; px[2] = lx + 164;
            px[3] = lx + 252; px[4] = lx + 420;
            ph[0] = "ID"; ph[1] = "Callsign"; ph[2] = "Interface";
            ph[3] = "Endpoint"; ph[4] = "Policy";
            pw[0] = 10; pw[1] = 10; pw[2] = 11; pw[3] = 22; pw[4] = 10;
            gs_draw_listview_header(lx, ty + 16, mw - 24, 5, px, ph, GS_CLR_PANEL, GS_CLR_FG);
            for (i = 0; i < cfg->peer_count && i < 6; ++i) {
                char ep[48];
                const char *cells[5];
                sprintf(ep, "%s:%u", cfg->peers[i].host, (unsigned int)cfg->peers[i].port);
                cells[0] = cfg->peers[i].id;
                cells[1] = cfg->peers[i].callsign;
                cells[2] = cfg->peers[i].interface_id;
                cells[3] = ep;
                cells[4] = cfg->peers[i].auto_learned ? "AUTO" : "STATIC";
                gs_draw_listview_row(lx, ty + 28 + i * 11, mw - 24, 10,
                                     (i == ui->sel_peer), 5, px, cells, pw,
                                     GS_CLR_BG, GS_CLR_FG);
            }
            gs_draw_button(lx, by, 86, 13, "Edit Peer", 0);
            gs_add_hit(ui, lx, by, 86, 13, OYK_GSOS_SCR_EDIT_PEER);
        } else if (active_tab == 5) {
            gs_draw_checkbox(lx, ty + 17, cfg->rig_enabled,
                             "Enable Rig CAT Control", 38);
            gs_draw_field_row(lx, ty + 32, "Protocol:", cfg->rig_protocol, 84, 120);
            gs_draw_field_row(lx + 226, ty + 32, "Host:", cfg->rig_host, 50, 140);
            sprintf(nbuf, "%u", (unsigned int)cfg->rig_port);
            gs_draw_field_row(lx, ty + 47, "Port:", nbuf, 84, 80);
            sprintf(nbuf, "%d s", cfg->rig_poll_sec);
            gs_draw_field_row(lx + 226, ty + 47, "Poll:", nbuf, 50, 70);
        } else {
            gs_draw_checkbox(lx, ty + 17, cfg->printer_enabled,
                             "Enable Line Printer Packet Log", 40);
            gs_draw_field_row(lx, ty + 32, "Device:", cfg->printer_device, 84, 120);
            gs_draw_field_row(lx + 226, ty + 32, "Proto:", cfg->printer_protocol, 56, 100);
            sprintf(nbuf, "%lu", (unsigned long)cfg->printer_baud);
            gs_draw_field_row(lx, ty + 47, "Baud:", nbuf, 84, 90);
            gs_draw_checkbox(lx + 226, ty + 48, cfg->printer_condensed,
                             "Condensed 132-col", 24);
        }
        gs_draw_button(mx + mw - 184, by, 82, 13, "Cancel", 0);
        gs_add_hit(ui, mx + mw - 184, by, 82, 13, 999);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Save", 1);
        gs_add_hit(ui, mx + mw - 96, by, 82, 13, 999);
    } else if (scr == OYK_GSOS_SCR_ADD_IFACE) {
        char nbuf[32];
        int st = ui->wiz.step;
        gs_draw_text(lx, ty + 2, oyk_gsos_wiz_step_prompt(&ui->wiz),
                     GS_CLR_FG, GS_CLR_BG, 68);
        if (st == 0) {
            for (i = 0; i < oyk_gsos_wiz_driver_count(); ++i) {
                int ry = ty + 18 + i * 13;
                gs_draw_radio(lx + 4, ry, i == ui->wiz.selected_driver_idx,
                              oyk_gsos_wiz_driver_name(i), 16);
                gs_draw_text(lx + 150, ry, oyk_gsos_wiz_driver_desc(i),
                             GS_CLR_FG, GS_CLR_BG, 46);
                gs_add_hit(ui, lx + 4, ry, 460, 11, 200 + i);
            }
        } else if (st == 1) {
            gs_draw_field_row(lx, ty + 18, "Driver:", ui->wiz.draft.type, 108, 120);
            gs_draw_field_row(lx, ty + 34, "Interface ID:", ui->wiz.draft.id, 108, 160);
            gs_draw_field_row(lx, ty + 50, "Description:",
                              ui->wiz.draft.description[0] ? ui->wiz.draft.description : "New Interface",
                              108, 300);
        } else if (st == 2) {
            if (strcmp(ui->wiz.draft.type, "kiss-serial") == 0) {
                gs_draw_field_row(lx, ty + 18, "Device:", ui->wiz.draft.device, 104, 140);
                sprintf(nbuf, "%lu", (unsigned long)ui->wiz.draft.baud);
                gs_draw_field_row(lx, ty + 33, "Baud Rate:", nbuf, 104, 100);
            } else {
                gs_draw_field_row(lx, ty + 18, "Bind Host:", ui->wiz.draft.host, 104, 150);
                sprintf(nbuf, "%u", (unsigned int)ui->wiz.draft.port);
                gs_draw_field_row(lx + 272, ty + 18, "Port:", nbuf, 48, 80);
                gs_draw_field_row(lx, ty + 33, "Netmask:", ui->wiz.draft.netmask, 104, 150);
            }
            gs_draw_checkbox(lx, ty + 50, ui->wiz.draft.allow_inbound, "Allow Inbound", 16);
            gs_draw_checkbox(lx + 150, ty + 50, ui->wiz.draft.allow_outbound, "Allow Outbound", 16);
            gs_draw_checkbox(lx + 306, ty + 50, ui->wiz.draft.allow_same_interface, "Same-Interface", 16);
            gs_draw_field_row(lx, ty + 66, "Learned Policy:",
                              oyk_gsos_format_learned_policy(ui->wiz.draft.learned_peer_policy),
                              112, 110);
        } else {
            char sum[320];
            const char *p;
            int sy = ty + 16;
            oyk_gsos_wiz_format_summary(&ui->wiz, sum, sizeof(sum));
            p = sum;
            while (*p && sy < by - 12) {
                const char *nl = strchr(p, '\n');
                char ln[88];
                size_t len = nl ? (size_t)(nl - p) : strlen(p);
                if (len >= sizeof(ln)) len = sizeof(ln) - 1U;
                memcpy(ln, p, len);
                ln[len] = '\0';
                gs_draw_text(lx, sy, ln, GS_CLR_FG, GS_CLR_BG, 72);
                sy += 11;
                p = nl ? (nl + 1) : "";
            }
        }
        gs_draw_button(lx, by, 76, 13, "< Back", 0);
        gs_add_hit(ui, lx, by, 76, 13, 210);
        gs_draw_button(mx + mw - 184, by, 82, 13, "Cancel", 0);
        gs_add_hit(ui, mx + mw - 184, by, 82, 13, 999);
        gs_draw_button(mx + mw - 96, by, 84, 13, (st >= 3) ? "Finish" : "Next >", 1);
        gs_add_hit(ui, mx + mw - 96, by, 84, 13, 211);
    } else if (scr == OYK_GSOS_SCR_EDIT_IFACE) {
        const OykGsosIfaceConfig *ifc = (ui->sel_iface < cfg->iface_count) ? &cfg->ifaces[ui->sel_iface] : &cfg->ifaces[0];
        char nbuf[24];
        gs_draw_field_row(lx, ty + 4, "Interface ID:", ifc->id, 104, 120);
        gs_draw_field_row(lx + 240, ty + 4, "Driver:", ifc->type, 64, 120);
        gs_draw_field_row(lx, ty + 18, "Bind Host:", ifc->host, 104, 140);
        sprintf(nbuf, "%u", (unsigned int)ifc->port);
        gs_draw_field_row(lx + 260, ty + 18, "Port:", nbuf, 48, 80);
        gs_draw_field_row(lx, ty + 32, "Netmask:", ifc->netmask, 104, 140);
        gs_draw_checkbox(lx, ty + 48, ifc->allow_inbound, "Allow Inbound", 16);
        gs_draw_checkbox(lx + 150, ty + 48, ifc->allow_outbound, "Allow Outbound", 16);
        gs_draw_checkbox(lx + 306, ty + 48, ifc->allow_same_interface, "Same-Interface", 16);
        gs_draw_field_row(lx, ty + 64, "Learned Policy:",
                          oyk_gsos_format_learned_policy(ifc->learned_peer_policy), 112, 110);
        gs_add_hit(ui, lx + 112, ty + 64, 110, 11, 302);
        gs_draw_button(mx + mw - 184, by, 82, 13, "Cancel", 0);
        gs_add_hit(ui, mx + mw - 184, by, 82, 13, 999);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Save", 1);
        gs_add_hit(ui, mx + mw - 96, by, 82, 13, 999);
    } else if (scr == OYK_GSOS_SCR_EDIT_PEER) {
        const OykGsosPeerConfig *pr = &cfg->peers[0];
        gs_draw_field_row(lx, ty + 6, "Peer ID:", pr->id, 90, 120);
        gs_draw_field_row(lx + 226, ty + 6, "Callsign:", pr->callsign, 76, 120);
        gs_draw_field_row(lx, ty + 21, "Interface:", pr->interface_id, 90, 120);
        sprintf(buf, "%s:%u", pr->host, (unsigned int)pr->port);
        gs_draw_field_row(lx + 226, ty + 21, "Endpoint:", buf, 76, 160);
        gs_draw_checkbox(lx, ty + 42, pr->allow_inbound, "Allow Inbound", 16);
        gs_draw_checkbox(lx + 150, ty + 42, pr->allow_outbound, "Allow Outbound", 16);
        gs_draw_checkbox(lx + 306, ty + 42, pr->auto_learned, "Auto-Learned", 16);
        gs_draw_button(mx + mw - 184, by, 82, 13, "Cancel", 0);
        gs_add_hit(ui, mx + mw - 184, by, 82, 13, 999);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Save", 1);
        gs_add_hit(ui, mx + mw - 96, by, 82, 13, 999);
    } else if (scr == OYK_GSOS_SCR_TERMINATE) {
        int tx[4];
        const char *th[4];
        int tw[4];
        int shown_t = 0;
        tx[0] = lx + 2; tx[1] = lx + 48; tx[2] = lx + 154; tx[3] = lx + 250;
        th[0] = oyk_gsos_table_column(OYK_GSOS_TBL_TERMINATE, 0);
        th[1] = oyk_gsos_table_column(OYK_GSOS_TBL_TERMINATE, 1);
        th[2] = oyk_gsos_table_column(OYK_GSOS_TBL_TERMINATE, 2);
        th[3] = oyk_gsos_table_column(OYK_GSOS_TBL_TERMINATE, 3);
        tw[0] = 5; tw[1] = 12; tw[2] = 11; tw[3] = 14;
        gs_draw_listview_header(lx, ty + 4, mw - 24, 4, tx, th, GS_CLR_PANEL, GS_CLR_FG);
        gs_hline(lx, mx + mw - 12, ty + 15, GS_CLR_FG);
        for (i = 0; i < OYK_GSOS_MAX_SESSIONS; ++i) {
            const OykGsosSession *s = &ui->node->sessions[i];
            char id_s[12];
            const char *cells[4];
            if (s->state == 0) continue;
            sprintf(id_s, "%lu", (unsigned long)s->id);
            cells[0] = id_s;
            cells[1] = s->callsign;
            cells[2] = (s->iface_idx == 0) ? "axudp0" : "kiss0";
            cells[3] = "CONNECTED";
            gs_draw_listview_row(lx, ty + 19 + shown_t * 11, mw - 24, 10,
                                 (shown_t == ui->sel_conn), 4, tx, cells, tw,
                                 GS_CLR_BG, GS_CLR_FG);
            ++shown_t;
        }
        gs_hline(lx, mx + mw - 12, by - 5, GS_CLR_FG);
        gs_draw_button(mx + mw - 196, by, 82, 13, "Cancel", 0);
        gs_add_hit(ui, mx + mw - 196, by, 82, 13, 999);
        gs_draw_button(mx + mw - 108, by, 94, 13, "Terminate", 1);
    } else if (scr == OYK_GSOS_SCR_BAN) {
        int bx_c[5];
        const char *bh[5];
        int bw_c[5];
        bx_c[0] = lx + 2; bx_c[1] = lx + 82; bx_c[2] = lx + 206;
        bx_c[3] = lx + 292; bx_c[4] = lx + 348;
        bh[0] = "Callsign"; bh[1] = "Banned At"; bh[2] = "Lift At";
        bh[3] = "Flags"; bh[4] = "Reason";
        bw_c[0] = 10; bw_c[1] = 16; bw_c[2] = 11; bw_c[3] = 7; bw_c[4] = 24;
        gs_draw_field_row(lx, ty + 2, "Callsign:", "N0BAD", 74, 90);
        gs_draw_field_row(lx + 174, ty + 2, "Reason:", "Beacon flood", 58, 140);
        gs_draw_field_row(lx + 384, ty + 2, "Dur:", "7d", 36, 52);
        gs_draw_checkbox(lx, ty + 16, 1, "Hard Ban (Drop AX.25)", 24);
        gs_draw_checkbox(lx + 210, ty + 16, 0, "Allow Appeal", 16);
        gs_draw_listview_header(lx, ty + 29, mw - 24, 5, bx_c, bh, GS_CLR_PANEL, GS_CLR_FG);
        for (i = 0; i < ui->node->ban_count && i < 3; ++i) {
            const OykGsosBanEntry *be = &ui->node->bans[i];
            char fl[12];
            const char *cells[5];
            sprintf(fl, "0x%02lX", (unsigned long)be->flags);
            cells[0] = be->key;
            cells[1] = be->banned_at;
            cells[2] = be->lift_at;
            cells[3] = fl;
            cells[4] = be->reason;
            gs_draw_listview_row(lx, ty + 41 + i * 11, mw - 24, 10,
                                 (i == ui->sel_ban), 5, bx_c, cells, bw_c,
                                 GS_CLR_BG, GS_CLR_FG);
        }
        gs_draw_button(mx + mw - 184, by, 82, 13, "Close", 0);
        gs_add_hit(ui, mx + mw - 184, by, 82, 13, 999);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Add Ban", 1);
    } else if (scr == OYK_GSOS_SCR_EXIT_PEERS) {
        int auto_cnt = oyk_gsos_node_auto_peer_count(ui->node);
        sprintf(buf, "%d auto-learned peer(s) found during session.", auto_cnt);
        gs_draw_text(lx, ty + 10, buf, GS_CLR_FG, GS_CLR_BG, 68);
        gs_draw_text(lx, ty + 24, "Keep learned peers in node configuration before exit?",
                     GS_CLR_FG, GS_CLR_BG, 68);
        gs_draw_button(lx, by, 102, 13, "Save & Exit", 1);
        gs_draw_button(lx + 108, by, 78, 13, "Discard", 0);
        gs_draw_button(lx + 192, by, 110, 13, "Review Peers", 0);
        gs_add_hit(ui, lx + 192, by, 110, 13, OYK_GSOS_SCR_PEERS);
        gs_draw_button(mx + mw - 90, by, 76, 13, "Cancel", 0);
        gs_add_hit(ui, mx + mw - 90, by, 76, 13, 999);
    } else if (scr == OYK_GSOS_SCR_THEME) {
        gs_draw_button(lx, ty, 24, 11, "<", 0);
        gs_add_hit(ui, lx, ty, 24, 11, 401);
        gs_draw_button(lx + 28, ty, 24, 11, ">", 0);
        gs_add_hit(ui, lx + 28, ty, 24, 11, 402);
        sprintf(buf, "Theme: %s   (Whole-Screen Live Preview)",
                ui->theme_ctrl.active_name);
        gs_draw_text(lx + 58, ty + 1, buf, GS_CLR_FG, GS_CLR_BG, 60);
        gs_draw_text(lx, ty + 12, "Role             Key         Color      Preview",
                     GS_CLR_FG, GS_CLR_BG, 68);
        for (i = 0; i < 9; ++i) {
            int row_y = 58 + i * 11;
            sprintf(buf, "%-16s %-11s #%06lX",
                    oyk_gsos_theme_role_name(i),
                    oyk_gsos_theme_role_key(i),
                    (unsigned long)(ui->active_theme.colors[i] & 0xFFFFFFUL));
            gs_draw_text(lx, row_y, buf, GS_CLR_FG, GS_CLR_BG, 48);
            gs_fill_rect(lx + 340, row_y + 1, 72, 8, GS_CLR_ACCENT);
            gs_frame_rect(lx + 340, row_y + 1, 72, 8, GS_CLR_FG);
        }
        gs_draw_button(mx + mw - 184, by, 82, 13, "Cancel", 0);
        gs_add_hit(ui, mx + mw - 184, by, 82, 13, 999);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Apply", 1);
        gs_add_hit(ui, mx + mw - 96, by, 82, 13, 999);
    } else if (scr == OYK_GSOS_SCR_TERMINAL) {
        gs_draw_text(lx, ty + 2, "KR4OYK-11 BBS | EOL: CR | Wrap: ON",
                     GS_CLR_FG, GS_CLR_BG, 70);
        gs_fill_rect(lx, ty + 13, mw - 24, 65, GS_CLR_PANEL);
        gs_frame_rect(lx, ty + 13, mw - 24, 65, GS_CLR_FG);
        gs_draw_text(lx + 6, ty + 17, "Welcome to KR4OYK-7 BBS on Apple IIGS GS/OS",
                     GS_CLR_FG, GS_CLR_PANEL, 68);
        gs_draw_text(lx + 6, ty + 28, "Type 'LIST' or 'B' to disconnect from BBS.",
                     GS_CLR_FG, GS_CLR_PANEL, 68);
        gs_draw_text(lx + 6, ty + 39, "14:21:08  Msg #42  ALL  de W4ABC-7: Net tonight",
                     GS_CLR_FG, GS_CLR_PANEL, 68);
        gs_fill_rect(lx, by - 30, mw - 24, 12, GS_CLR_FG);
        gs_draw_text(lx + 4, by - 28, oyk_gsos_term_hotkey_bar_text(0),
                     GS_CLR_BG, GS_CLR_FG, 72);
        gs_draw_field_row(lx, by - 16, "Input:", "LIST_", 52, mw - 156);
        gs_draw_button(lx + mw - 98, by - 16, 74, 12, "Send", 1);
        gs_draw_button(lx, by, 72, 13, "Help", 0);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Close", 0);
        gs_add_hit(ui, mx + mw - 96, by, 82, 13, 999);
    } else if (scr == OYK_GSOS_SCR_TERM_HELP) {
        int hi;
        gs_draw_text(lx, ty + 2, "Command prefix: Ctrl-A (press Ctrl-A, then hotkey)",
                     GS_CLR_FG, GS_CLR_BG, 68);
        gs_fill_rect(lx, ty + 13, mw - 24, 98, GS_CLR_PANEL);
        gs_frame_rect(lx, ty + 13, mw - 24, 98, GS_CLR_FG);
        for (hi = 0; hi < oyk_gsos_term_help_count() && hi < 8; ++hi) {
            char row[80];
            sprintf(row, "%-9.9s  %-34.34s  %-10.10s",
                    oyk_gsos_term_help_key(hi),
                    oyk_gsos_term_help_action(hi),
                    oyk_gsos_term_help_alt(hi));
            gs_draw_text(lx + 6, ty + 17 + hi * 11, row, GS_CLR_FG, GS_CLR_PANEL, 68);
        }
        gs_draw_button(mx + mw - 96, by, 82, 13, "Close", 1);
        gs_add_hit(ui, mx + mw - 96, by, 82, 13, 999);
    } else if (scr == OYK_GSOS_SCR_FILES) {
        OykGsosFileMgr fm;
        int fx1[2], fx2[2], fw1[2], fw2[2], ri;
        const char *fh[2], *fr[2];
        char tit1[40], tit2[40], sz1[8][12], sz2[8][12];
        oyk_gsos_filemgr_init(&fm, "/OYK/INCOMING", "/OYK/FILES");
        oyk_gsos_filemgr_pane_title(&fm.inc, "Incoming", tit1, sizeof(tit1));
        oyk_gsos_filemgr_pane_title(&fm.files, "Files", tit2, sizeof(tit2));
        fx1[0] = lx + 4; fx1[1] = lx + 188;
        fx2[0] = lx + 266; fx2[1] = lx + 464;
        fh[0] = "Name"; fh[1] = "Size";
        fw1[0] = 22; fw1[1] = 7;
        fw2[0] = 24; fw2[1] = 7;

        gs_draw_text(lx, ty + 2, tit1, GS_CLR_FG, GS_CLR_BG, 30);
        gs_draw_text(lx + 262, ty + 2, tit2, GS_CLR_FG, GS_CLR_BG, 30);
        gs_fill_rect(lx, ty + 14, 250, 96, GS_CLR_PANEL);
        gs_frame_rect(lx, ty + 14, 250, 96, GS_CLR_FG);
        gs_fill_rect(lx + 262, ty + 14, 266, 96, GS_CLR_PANEL);
        gs_frame_rect(lx + 262, ty + 14, 266, 96, GS_CLR_FG);
        gs_draw_listview_header(lx + 2, ty + 16, 246, 2, fx1, fh, GS_CLR_PANEL, GS_CLR_FG);
        for (ri = 0; ri < fm.inc.count && ri < 6; ++ri) {
            if (fm.inc.entries[ri].is_dir) strcpy(sz1[ri], "<DIR>");
            else sprintf(sz1[ri], "%lu", fm.inc.entries[ri].size);
            fr[0] = fm.inc.entries[ri].name; fr[1] = sz1[ri];
            gs_draw_listview_row(lx + 2, ty + 27 + ri * 11, 246, 10, ri == 0, 2, fx1, fr, fw1,
                                 GS_CLR_PANEL, GS_CLR_FG);
        }
        gs_draw_listview_header(lx + 264, ty + 16, 262, 2, fx2, fh, GS_CLR_PANEL, GS_CLR_FG);
        for (ri = 0; ri < fm.files.count && ri < 6; ++ri) {
            if (fm.files.entries[ri].is_dir) strcpy(sz2[ri], "<DIR>");
            else sprintf(sz2[ri], "%lu", fm.files.entries[ri].size);
            fr[0] = fm.files.entries[ri].name; fr[1] = sz2[ri];
            gs_draw_listview_row(lx + 264, ty + 27 + ri * 11, 262, 10, ri == 0, 2, fx2, fr, fw2,
                                 GS_CLR_PANEL, GS_CLR_FG);
        }
        gs_draw_button(lx, by, 72, 13, "Upload", 0);
        gs_draw_button(lx + 76, by, 76, 13, "Approve", 1);
        gs_draw_button(lx + 156, by, 68, 13, "Rename", 0);
        gs_draw_button(lx + 228, by, 72, 13, "New Dir", 0);
        gs_draw_button(lx + 304, by, 68, 13, "Delete", 0);
        gs_draw_button(lx + 376, by, 58, 13, "View", 0);
        gs_draw_button(mx + mw - 86, by, 74, 13, "Close", 0);
        gs_add_hit(ui, mx + mw - 86, by, 74, 13, 999);
    } else if (scr == OYK_GSOS_SCR_ABOUT) {
        gs_draw_text(lx, ty + 8, "OYK Amateur Radio Packet Node (GPL-3.0-or-later)",
                     GS_CLR_FG, GS_CLR_BG, 68);
        gs_draw_text(lx, ty + 22, "Protocols: AX.25 / KISS / AXUDP / AXIP / BBS / CHAT / YAPP / CAT",
                     GS_CLR_FG, GS_CLR_BG, 72);
        gs_draw_text(lx, ty + 36, "Platform: Apple IIGS GS/OS (65C816 QuickDraw II + SCC .A/.B)",
                     GS_CLR_FG, GS_CLR_BG, 72);
        gs_draw_text(lx, ty + 52, "Hotkeys: 1..7 Settings Tabs | Esc/Q Close Modal",
                     GS_CLR_FG, GS_CLR_BG, 72);
        gs_draw_button(mx + mw - 96, by, 82, 13, "OK", 1);
        gs_add_hit(ui, mx + mw - 96, by, 82, 13, 999);
    } else {
        gs_draw_text(lx, ty + 10, "Hamlib rigctld CAT: Port 4532 / SCC .A @ 9600 bps",
                     GS_CLR_FG, GS_CLR_BG, 68);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Close", 1);
        gs_add_hit(ui, mx + mw - 96, by, 82, 13, 999);
    }
}

static void gs_redraw(OykGsosUiState *ui) {
    if (!ui || !ui->node) return;
    oyk_gsos_theme_resolve(&ui->theme_ctrl, &ui->active_theme);
    gs_apply_theme_palette(&ui->active_theme, ui->active_screen);
    gs_draw_console(ui);
    if (ui->active_screen != OYK_GSOS_SCR_MAIN) {
        gs_draw_modal(ui);
    }
}

static void gs_on_theme_preview(const OykGsosThemeConfig *pal, const char *active_name, void *ctx) {
    OykGsosUiState *ui = (OykGsosUiState *)ctx;
    (void)active_name;
    if (!ui) return;
    if (pal) ui->active_theme = *pal;
    gs_redraw(ui);
}

void oyk_gsos_query_os_native_theme(OykGsosThemeConfig *out_pal) {
    oyk_gsos_theme_preset("native", out_pal);
}

typedef struct {
    int         scr;
    int         tab;
    int         wiz_step;
    const char *bmp_path;
} GsosGalleryStep;

static const GsosGalleryStep kGallerySteps[23] = {
    { OYK_GSOS_SCR_MAIN,       0, 0, "/OYKOUT/GMAIN.BMP" },
    { OYK_GSOS_SCR_MENU,       0, 0, "/OYKOUT/GMENU.BMP" },
    { OYK_GSOS_SCR_SETTINGS,   0, 0, "/OYKOUT/GSET1.BMP" },
    { OYK_GSOS_SCR_SETTINGS,   1, 0, "/OYKOUT/GSET2.BMP" },
    { OYK_GSOS_SCR_SETTINGS,   2, 0, "/OYKOUT/GSET3.BMP" },
    { OYK_GSOS_SCR_SETTINGS,   3, 0, "/OYKOUT/GSET4.BMP" },
    { OYK_GSOS_SCR_PEERS,      4, 0, "/OYKOUT/GPEER.BMP" },
    { OYK_GSOS_SCR_SETTINGS,   5, 0, "/OYKOUT/GSET6.BMP" },
    { OYK_GSOS_SCR_SETTINGS,   6, 0, "/OYKOUT/GSET7.BMP" },
    { OYK_GSOS_SCR_ADD_IFACE,  0, 0, "/OYKOUT/GADD1.BMP" },
    { OYK_GSOS_SCR_ADD_IFACE,  0, 1, "/OYKOUT/GADD2.BMP" },
    { OYK_GSOS_SCR_ADD_IFACE,  0, 2, "/OYKOUT/GADD3.BMP" },
    { OYK_GSOS_SCR_ADD_IFACE,  0, 3, "/OYKOUT/GADD4.BMP" },
    { OYK_GSOS_SCR_EDIT_IFACE, 0, 0, "/OYKOUT/GEDTI.BMP" },
    { OYK_GSOS_SCR_EDIT_PEER,  0, 0, "/OYKOUT/GEDTP.BMP" },
    { OYK_GSOS_SCR_TERMINATE,  0, 0, "/OYKOUT/GTERM.BMP" },
    { OYK_GSOS_SCR_BAN,        0, 0, "/OYKOUT/GBAN.BMP"  },
    { OYK_GSOS_SCR_EXIT_PEERS, 0, 0, "/OYKOUT/GEXIT.BMP" },
    { OYK_GSOS_SCR_THEME,      0, 0, "/OYKOUT/GTHME.BMP" },
    { OYK_GSOS_SCR_TERMINAL,   0, 0, "/OYKOUT/GTML.BMP"  },
    { OYK_GSOS_SCR_TERM_HELP,  0, 0, "/OYKOUT/GTMLH.BMP" },
    { OYK_GSOS_SCR_FILES,      0, 0, "/OYKOUT/GFILE.BMP" },
    { OYK_GSOS_SCR_ABOUT,      0, 0, "/OYKOUT/GABT.BMP"  }
};

#ifdef __VBCC__
static void gs_dispatch_hit(OykGsosUiState *ui, int mx, int my) {
    int hi, act = -1;
    if (!ui) return;
    for (hi = ui->hit_count - 1; hi >= 0; --hi) {
        const GsHitZone *z = &ui->hits[hi];
        if (mx >= z->x && mx < z->x + z->w && my >= z->y && my < z->y + z->h) {
            act = z->action;
            break;
        }
    }
    if (act == 999) {
        ui->active_screen = OYK_GSOS_SCR_MAIN;
        gs_redraw(ui);
    } else if (act >= 100 && act < 107) {
        ui->active_screen = OYK_GSOS_SCR_SETTINGS;
        ui->settings_tab = act - 100;
        gs_redraw(ui);
    } else if (act >= 200 && act < 205) {
        oyk_gsos_wiz_select_driver(&ui->wiz, &ui->node->cfg, act - 200);
        gs_redraw(ui);
    } else if (act == 210) {
        oyk_gsos_wiz_back(&ui->wiz);
        gs_redraw(ui);
    } else if (act == 211) {
        if (ui->wiz.step >= 3) {
            ui->active_screen = OYK_GSOS_SCR_SETTINGS;
            ui->settings_tab = 3;
        } else {
            oyk_gsos_wiz_advance(&ui->wiz, &ui->node->cfg);
        }
        gs_redraw(ui);
    } else if (act == 301) {
        ui->node->cfg.accept_incoming = ui->node->cfg.accept_incoming ? 0 : 1;
        gs_redraw(ui);
    } else if (act == 302) {
        if (ui->node->cfg.iface_count > 0) {
            int idx = (ui->sel_iface < ui->node->cfg.iface_count) ? ui->sel_iface : 0;
            ui->node->cfg.ifaces[idx].learned_peer_policy =
                oyk_gsos_cycle_learned_policy(ui->node->cfg.ifaces[idx].learned_peer_policy, 1);
            gs_redraw(ui);
        }
    } else if (act == 401) {
        oyk_gsos_theme_cycle(&ui->theme_ctrl, -1);
    } else if (act == 402) {
        oyk_gsos_theme_cycle(&ui->theme_ctrl, 1);
    } else if (act >= 0 && act < 100) {
        ui->active_screen = act;
        if (act == OYK_GSOS_SCR_PEERS) ui->settings_tab = 4;
        gs_redraw(ui);
    }
}
#endif

int oyk_gsos_ui_run(OykGsosNode *node, OykGsosNetState *net, int ui_test_mode) {
    OykGsosUiState *ui = &g_ui_state;
    OykGsosThemeConfig os_pal;
    int i;
    if (!node) return 1;
    memset(ui, 0, sizeof(*ui));
    ui->node = node;
    ui->net = net;
    ui->running = 1;
    ui->active_screen = OYK_GSOS_SCR_MAIN;
    ui->draft_cfg = node->cfg;
    oyk_gsos_wiz_init(&ui->wiz, &node->cfg);

    gs_qd_init(ui);
    oyk_gsos_query_os_native_theme(&os_pal);
    oyk_gsos_theme_init(&ui->theme_ctrl, &ui->draft_cfg, 1, os_pal.is_dark_base);
    oyk_gsos_theme_set_os_native(&ui->theme_ctrl, &os_pal);
    oyk_gsos_theme_set_preview_cb(&ui->theme_ctrl, gs_on_theme_preview, ui);

    if (ui_test_mode) {
        oyk_gsos_node_seed_demo(node);
        ui->draft_cfg = node->cfg;
        oyk_gsos_wiz_init(&ui->wiz, &node->cfg);
        for (i = 0; i < 23; ++i) {
            ui->active_screen = kGallerySteps[i].scr;
            ui->settings_tab = kGallerySteps[i].tab;
            ui->wiz.step = kGallerySteps[i].wiz_step;
            gs_redraw(ui);
            oyk_gsos_capture_screen_bmp(kGallerySteps[i].bmp_path);
        }
        oyk_gsos_finish_test();
        return 0;
    }

#ifdef __VBCC__
    if (ui->dp_handle && !EMStatus()) {
        Word dp = (Word)(*(LongWord *)ui->dp_handle);
        EMStartUp((Word)(dp + 0x300U), 20, 0, 640, 0, 200, ui->uid);
    }
#endif
    gs_redraw(ui);
    /* Interactive desktop loop */
    while (ui->running) {
#ifdef __VBCC__
        EventRecord ev;
        if (GetNextEvent((Word)(mDownMask | keyDownMask | autoKeyMask), &ev)) {
            if (ev.what == mouseDownEvt) {
                gs_dispatch_hit(ui, (int)ev.where.h, (int)ev.where.v);
            } else if (ev.what == keyDownEvt || ev.what == autoKeyEvt) {
                int ch = (int)(ev.message & 0xFFUL);
                if (ch == 'q' || ch == 'Q' || ch == 27) {
                    if (ui->active_screen != OYK_GSOS_SCR_MAIN) {
                        ui->active_screen = OYK_GSOS_SCR_MAIN;
                        gs_redraw(ui);
                    } else {
                        ui->running = 0;
                    }
                } else if ((ui->active_screen == OYK_GSOS_SCR_SETTINGS ||
                            ui->active_screen == OYK_GSOS_SCR_PEERS) &&
                           ch >= '1' && ch <= '7') {
                    ui->active_screen = OYK_GSOS_SCR_SETTINGS;
                    ui->settings_tab = ch - '1';
                    gs_redraw(ui);
                }
            }
        }
#endif
        oyk_gsos_net_poll(net);
    }
    return 0;
}

int oyk_gsos_ui_run_theme_preview_self_test(OykGsosNode *node,
                                            OykGsosNetState *net,
                                            int *out_bg_changes,
                                            int *out_fg_changes) {
    OykGsosUiState *ui = &g_ui_state;
    OykGsosThemeConfig os_pal;
    int ok = 1, bg_changes = 0, fg_changes = 0, i;
    int prev_cdraw, prev_mdraw;
    gsos_u32 prev_cbg, prev_mbg;
    const char *seq[4];
#ifdef __VBCC__
    const volatile unsigned char *pals = (const volatile unsigned char *)0xE19E00UL;
    unsigned char prev_c0_lo = 0, prev_c3_lo = 0;
#endif
    if (!node) return 0;
    memset(ui, 0, sizeof(*ui));
    ui->node = node;
    ui->net = net;
    ui->active_screen = OYK_GSOS_SCR_THEME;
    ui->draft_cfg = node->cfg;
    gs_qd_init(ui);

    oyk_gsos_query_os_native_theme(&os_pal);
    oyk_gsos_theme_init(&ui->theme_ctrl, &ui->draft_cfg, 1, os_pal.is_dark_base);
    oyk_gsos_theme_set_os_native(&ui->theme_ctrl, &os_pal);
    oyk_gsos_theme_set_preview_cb(&ui->theme_ctrl, gs_on_theme_preview, ui);
    gs_redraw(ui);

    if (strcmp(ui->theme_ctrl.active_name, "native") != 0) ok = 0;
    prev_cbg = ui->last_console_bg;
    prev_mbg = ui->last_modal_bg;
    prev_cdraw = ui->console_draw_count;
    prev_mdraw = ui->modal_draw_count;
#ifdef __VBCC__
    prev_c0_lo = pals[0];
    prev_c3_lo = pals[6];
#endif

    seq[0] = "dark";
    seq[1] = "light";
    seq[2] = NULL;
    seq[3] = "native";
    for (i = 0; i < 4; ++i) {
        if (seq[i]) {
            oyk_gsos_theme_select(&ui->theme_ctrl, seq[i]);
        } else {
            oyk_gsos_theme_set_role_rgb(&ui->theme_ctrl, OYK_GSOS_TR_BG, 0x334455UL);
        }
        if (ui->console_draw_count > prev_cdraw && ui->last_console_bg != prev_cbg) {
            ++bg_changes;
        }
        if (ui->modal_draw_count > prev_mdraw && ui->last_modal_bg != prev_mbg) {
            ++fg_changes;
        }
#ifdef __VBCC__
        if (i == 0 && pals[0] == prev_c0_lo && pals[6] == prev_c3_lo) {
            ok = 0;
        }
#endif
        prev_cbg = ui->last_console_bg;
        prev_mbg = ui->last_modal_bg;
        prev_cdraw = ui->console_draw_count;
        prev_mdraw = ui->modal_draw_count;
    }

    oyk_gsos_theme_select(&ui->theme_ctrl, "light");
    oyk_gsos_theme_revert(&ui->theme_ctrl);
    if (strcmp(ui->theme_ctrl.active_name, "native") != 0 ||
        ui->last_console_bg != os_pal.colors[OYK_GSOS_TR_BG] ||
        ui->last_modal_bg != os_pal.colors[OYK_GSOS_TR_BG]) {
        ok = 0;
    }

    if (out_bg_changes) *out_bg_changes = bg_changes;
    if (out_fg_changes) *out_fg_changes = fg_changes;
    if (bg_changes < 4 || fg_changes < 4) ok = 0;
    return ok;
}
