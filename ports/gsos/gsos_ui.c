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
#endif

typedef struct {
    short v1, h1, v2, h2;
} GsRect;

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
        ui->dp_handle = NewHandle(0x300UL, ui->uid, 0xC005, (Ptr)0L);
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

static void gs_draw_menubar(const OykGsosNode *node) {
    char stat[32];
    gs_fill_rect(0, 0, 640, 12, GS_CLR_PANEL);
    gs_hline(0, 639, 12, GS_CLR_FG);
    gs_draw_text(8, 1, "@  File  Edit  Node  View  Window  Help",
                 GS_CLR_FG, GS_CLR_PANEL, 48);
    sprintf(stat, "%s  GS/OS", node->cfg.accept_incoming ? "IN:ALLOW" : "IN:DENY");
    gs_draw_text(500, 1, stat, GS_CLR_ACCENT, GS_CLR_PANEL, 18);
}

static void gs_draw_console(OykGsosUiState *ui) {
    const OykGsosNode *node = ui->node;
    char cs[16], al[16], banner[128];
    int i, shown_c;

    ++ui->console_draw_count;
    ui->last_console_bg = ui->active_theme.colors[OYK_GSOS_TR_BG];

    gs_fill_rect(0, 13, 640, 187, GS_CLR_BG);
    gs_draw_menubar(node);

    /* Top Status Banner */
    oyk_gsos_format_callsign(&node->cfg.callsign, cs, sizeof(cs));
    oyk_gsos_format_callsign(&node->cfg.alias, al, sizeof(al));
    oyk_gsos_format_console_banner(cs, al, node->cfg.accept_incoming,
                                   oyk_gsos_node_active_conn_count(node),
                                   node->cfg.iface_count,
                                   banner, sizeof(banner));
    gs_fill_rect(6, 15, 628, 12, GS_CLR_ACCENT);
    gs_frame_rect(6, 15, 628, 12, GS_CLR_FG);
    gs_draw_text(12, 16, banner, GS_CLR_BG, GS_CLR_ACCENT, 84);

    /* Side-by-side Interfaces & Last 5 New Callsigns Heard */
    gs_draw_box(6, 29, 310, 52, "Interfaces");
    gs_draw_text(12, 41, "Port     Type     State  RX/TX", GS_CLR_ACCENT, GS_CLR_BG, 40);
    for (i = 0; i < node->cfg.iface_count && i < 3; ++i) {
        char row[64];
        sprintf(row, "%-8s %-8s %-5s  %lu/%lu",
                node->cfg.ifaces[i].id,
                node->cfg.ifaces[i].type,
                node->iface_rt[i].running ? "UP" : "DOWN",
                (unsigned long)node->iface_rt[i].rx_frames,
                (unsigned long)node->iface_rt[i].tx_frames);
        gs_draw_text(12, 51 + i * 9, row, GS_CLR_FG, GS_CLR_BG, 40);
    }

    gs_draw_box(322, 29, 312, 52, "Last 5 New Callsigns Heard");
    gs_draw_text(328, 41, "#  Callsign  Port     Heard", GS_CLR_ACCENT, GS_CLR_BG, 40);
    for (i = 0; i < node->heard_count && i < 3; ++i) {
        char row[64];
        sprintf(row, "%-2d %-9s %-8s %s", i + 1,
                node->heard[i].callsign, node->heard[i].iface_id, node->heard[i].time_hms);
        gs_draw_text(328, 51 + i * 9, row, GS_CLR_FG, GS_CLR_BG, 40);
    }

    /* Active Connections */
    gs_draw_box(6, 83, 628, 44, "Active Connections");
    gs_draw_text(12, 95, "ID   Callsign   Port     Dir    State      Since",
                 GS_CLR_ACCENT, GS_CLR_BG, 80);
    shown_c = 0;
    for (i = 0; i < OYK_GSOS_MAX_SESSIONS && shown_c < 2; ++i) {
        const OykGsosSession *s = &node->sessions[i];
        const char *pname;
        char row[80];
        if (s->state == 0) continue;
        pname = (s->iface_idx >= 0 && s->iface_idx < node->cfg.iface_count)
                    ? node->cfg.ifaces[s->iface_idx].id : "local";
        sprintf(row, "%-4lu %-10s %-8s %-6s %-10s %s",
                (unsigned long)s->id, s->callsign, pname,
                s->is_local_term ? "LOCAL" : "IN",
                (s->state == 2) ? "CONNECTED" : "OPENING",
                s->since_hms);
        gs_draw_text(12, 105 + shown_c * 9, row, GS_CLR_FG, GS_CLR_BG, 80);
        ++shown_c;
    }

    /* Node Activity Log */
    gs_draw_box(6, 129, 628, 50, "Node Activity Log");
    {
        int start = (node->log_count > 3) ? (node->log_count - 3) : 0;
        for (i = start; i < node->log_count; ++i) {
            gs_draw_text(12, 141 + (i - start) * 10, node->logs[i],
                         GS_CLR_FG, GS_CLR_BG, 82);
        }
    }

    /* Bottom Footer Quick Buttons */
    gs_draw_button(6,   183, 86, 13, "Terminal", 1);
    gs_draw_button(98,  183, 86, 13, "Files", 0);
    gs_draw_button(190, 183, 86, 13, "Settings", 0);
    gs_draw_button(282, 183, 86, 13, "Theme", 0);
    gs_draw_button(374, 183, 86, 13, "Peers", 0);
    gs_draw_text(470, 184, "SCC .A/.B | GS/OS", GS_CLR_FG, GS_CLR_BG, 24);
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

    /* Drop shadow + floating GS/OS modal */
    gs_fill_rect(mx + 4, my + 3, mw, mh, GS_CLR_FG);
    gs_fill_rect(mx, my, mw, mh, GS_CLR_BG);
    gs_frame_rect(mx, my, mw, mh, GS_CLR_FG);
    gs_fill_rect(mx + 1, my + 1, mw - 2, 12, GS_CLR_ACCENT);
    gs_hline(mx, mx + mw - 1, my + 13, GS_CLR_FG);
    gs_draw_text(mx + 10, my + 2, title, GS_CLR_BG, GS_CLR_ACCENT, 64);
    /* Apple IIgs close box + [X] marker */
    gs_fill_rect(mx + mw - 28, my + 2, 24, 10, GS_CLR_PANEL);
    gs_frame_rect(mx + mw - 28, my + 2, 24, 10, GS_CLR_FG);
    gs_draw_text(mx + mw - 26, my + 2, "[X]", GS_CLR_FG, GS_CLR_PANEL, 3);

    if (scr == OYK_GSOS_SCR_SETTINGS || scr == OYK_GSOS_SCR_PEERS) {
        int active_tab = (scr == OYK_GSOS_SCR_PEERS) ? 4 : ui->settings_tab;
        int tx = lx;
        int tw = 74;
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
            tx += tw;
        }
        gs_hline(lx, mx + mw - 12, ty + 13, GS_CLR_FG);

        if (active_tab == 4) {
            gs_draw_text(lx, ty + 17, "ID       Callsign  Interface Endpoint         Policy",
                         GS_CLR_ACCENT, GS_CLR_BG, 72);
            for (i = 0; i < cfg->peer_count && i < 6; ++i) {
                sprintf(buf, "%s %-8s %-9s %-9s %s:%u %s",
                        (i == ui->sel_peer) ? ">" : " ",
                        cfg->peers[i].id, cfg->peers[i].callsign,
                        cfg->peers[i].interface_id, cfg->peers[i].host,
                        (unsigned int)cfg->peers[i].port,
                        cfg->peers[i].auto_learned ? "AUTO" : "STATIC");
                gs_draw_text(lx, ty + 28 + i * 10, buf, GS_CLR_FG, GS_CLR_BG, 72);
            }
            gs_draw_button(lx, by, 86, 13, "Edit Peer", 0);
        } else {
            oyk_gsos_format_callsign(&cfg->callsign, cs, sizeof(cs));
            oyk_gsos_format_callsign(&cfg->alias, al, sizeof(al));
            sprintf(buf, "Callsign: %s    Alias: %s", cs, al);
            gs_draw_text(lx, ty + 19, buf, GS_CLR_FG, GS_CLR_BG, 70);
            sprintf(buf, "Accept Incoming: %s   Line Ending: %s",
                    cfg->accept_incoming ? "YES" : "NO",
                    (cfg->line_ending == OYK_GSOS_EOL_CRLF) ? "CRLF"
                    : (cfg->line_ending == OYK_GSOS_EOL_LF) ? "LF" : "CR");
            gs_draw_text(lx, ty + 31, buf, GS_CLR_FG, GS_CLR_BG, 70);
            sprintf(buf, "BBS (-%u): %s   CHAT (-%u): %s   Interfaces: %d",
                    (unsigned int)cfg->bbs_ssid, cfg->bbs_enabled ? "ON" : "OFF",
                    (unsigned int)cfg->chat_ssid, cfg->chat_enabled ? "ON" : "OFF",
                    cfg->iface_count);
            gs_draw_text(lx, ty + 43, buf, GS_CLR_FG, GS_CLR_BG, 70);
            sprintf(buf, "Printer: %s (%s @ %lu)   Theme: %s",
                    cfg->printer_device, cfg->printer_protocol,
                    (unsigned long)cfg->printer_baud, ui->theme_ctrl.active_name);
            gs_draw_text(lx, ty + 55, buf, GS_CLR_FG, GS_CLR_BG, 70);
            gs_draw_button(lx, by, 86, 13, "Save", 1);
        }
        gs_draw_button(mx + mw - 96, by, 82, 13, "Close", 0);
    } else if (scr == OYK_GSOS_SCR_ADD_IFACE) {
        gs_draw_text(lx, ty + 2, "Step 1 of 4: Select Driver & Review",
                     GS_CLR_ACCENT, GS_CLR_BG, 60);
        for (i = 0; i < oyk_gsos_wiz_driver_count(); ++i) {
            int sel = (i == ui->wiz.selected_driver_idx);
            sprintf(buf, "%s %s", sel ? "[*]" : "[ ]", oyk_gsos_wiz_driver_name(i));
            gs_draw_text(lx + 6, ty + 15 + i * 10, buf, GS_CLR_FG, GS_CLR_BG, 28);
        }
        sprintf(buf, "ID: %s  Type: %s  Host: %s:%u",
                ui->wiz.draft.id, ui->wiz.draft.type,
                ui->wiz.draft.host, (unsigned int)ui->wiz.draft.port);
        gs_draw_text(lx + 210, ty + 20, buf, GS_CLR_FG, GS_CLR_BG, 46);
        gs_draw_text(lx + 210, ty + 32, "Same-Iface: NO   Inbound/Outbound: YES",
                     GS_CLR_FG, GS_CLR_BG, 46);
        gs_draw_button(lx, by, 84, 13, "Create", 1);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Cancel", 0);
    } else if (scr == OYK_GSOS_SCR_EDIT_IFACE) {
        const OykGsosIfaceConfig *ifc = &cfg->ifaces[0];
        sprintf(buf, "Interface ID: %s    Type: %s", ifc->id, ifc->type);
        gs_draw_text(lx, ty + 6, buf, GS_CLR_FG, GS_CLR_BG, 68);
        sprintf(buf, "Bind Host: %s    Port: %u    Netmask: %s",
                ifc->host, (unsigned int)ifc->port, ifc->netmask);
        gs_draw_text(lx, ty + 20, buf, GS_CLR_FG, GS_CLR_BG, 68);
        gs_draw_text(lx, ty + 34, "Allow Inbound: [X]   Allow Outbound: [X]   Same-Iface: [ ]",
                     GS_CLR_FG, GS_CLR_BG, 68);
        gs_draw_button(lx, by, 84, 13, "Save", 1);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Cancel", 0);
    } else if (scr == OYK_GSOS_SCR_EDIT_PEER) {
        const OykGsosPeerConfig *pr = &cfg->peers[0];
        sprintf(buf, "Peer ID: %s    Callsign: %s", pr->id, pr->callsign);
        gs_draw_text(lx, ty + 6, buf, GS_CLR_FG, GS_CLR_BG, 68);
        sprintf(buf, "Interface: %s   Endpoint: %s:%u",
                pr->interface_id, pr->host, (unsigned int)pr->port);
        gs_draw_text(lx, ty + 20, buf, GS_CLR_FG, GS_CLR_BG, 68);
        gs_draw_text(lx, ty + 34, "Policy: STATIC   Inbound: YES   Outbound: YES",
                     GS_CLR_FG, GS_CLR_BG, 68);
        gs_draw_button(lx, by, 84, 13, "Save", 1);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Cancel", 0);
    } else if (scr == OYK_GSOS_SCR_TERMINATE) {
        sprintf(buf, "%-4s %-10s %-8s %-10s",
                oyk_gsos_table_column(OYK_GSOS_TBL_TERMINATE, 0),
                oyk_gsos_table_column(OYK_GSOS_TBL_TERMINATE, 1),
                oyk_gsos_table_column(OYK_GSOS_TBL_TERMINATE, 2),
                oyk_gsos_table_column(OYK_GSOS_TBL_TERMINATE, 3));
        gs_draw_text(lx, ty + 4, buf, GS_CLR_ACCENT, GS_CLR_BG, 68);
        gs_hline(lx, mx + mw - 12, ty + 15, GS_CLR_FG);
        for (i = 0; i < OYK_GSOS_MAX_SESSIONS; ++i) {
            const OykGsosSession *s = &ui->node->sessions[i];
            if (s->state == 0) continue;
            sprintf(buf, "%s %-3lu %-10s %-8s CONNECTED",
                    (i == 0) ? ">" : " ",
                    (unsigned long)s->id, s->callsign,
                    (s->iface_idx == 0) ? "axudp0" : "kiss0");
            gs_draw_text(lx, ty + 20 + i * 11, buf, GS_CLR_FG, GS_CLR_BG, 68);
        }
        gs_hline(lx, mx + mw - 12, by - 5, GS_CLR_FG);
        gs_draw_button(lx, by, 102, 13, "Terminate", 1);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Cancel", 0);
    } else if (scr == OYK_GSOS_SCR_BAN) {
        gs_draw_text(lx, ty + 4, "Callsign  Banned At        Lift At     Flags  Reason",
                     GS_CLR_ACCENT, GS_CLR_BG, 72);
        for (i = 0; i < ui->node->ban_count && i < 4; ++i) {
            const OykGsosBanEntry *be = &ui->node->bans[i];
            sprintf(buf, "%-9s %-16s %-10s  0x%02lX   %s",
                    be->key, be->banned_at, be->lift_at,
                    (unsigned long)be->flags, be->reason);
            gs_draw_text(lx, ty + 18 + i * 11, buf, GS_CLR_FG, GS_CLR_BG, 72);
        }
        gs_draw_button(lx, by, 92, 13, "Add Ban", 1);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Close", 0);
    } else if (scr == OYK_GSOS_SCR_EXIT_PEERS) {
        int auto_cnt = oyk_gsos_node_auto_peer_count(ui->node);
        sprintf(buf, "%d auto-learned peer(s) found during session.", auto_cnt);
        gs_draw_text(lx, ty + 10, buf, GS_CLR_FG, GS_CLR_BG, 68);
        gs_draw_text(lx, ty + 24, "Keep learned peers in node configuration before exit?",
                     GS_CLR_FG, GS_CLR_BG, 68);
        gs_draw_button(lx, by, 120, 13, "Save & Exit", 1);
        gs_draw_button(lx + 132, by, 120, 13, "Discard & Exit", 0);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Cancel", 0);
    } else if (scr == OYK_GSOS_SCR_THEME) {
        sprintf(buf, "Active Theme: < %s >   (Whole-Screen Live Preview)",
                ui->theme_ctrl.active_name);
        gs_draw_text(lx, ty + 2, buf, GS_CLR_ACCENT, GS_CLR_BG, 68);
        gs_draw_text(lx, ty + 13, "Role             Key         Color      Preview",
                     GS_CLR_ACCENT, GS_CLR_BG, 68);
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
        gs_draw_button(lx, by, 88, 13, "Next Theme", 0);
        gs_draw_button(lx + 96, by, 84, 13, "Apply", 1);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Cancel", 0);
    } else if (scr == OYK_GSOS_SCR_TERMINAL) {
        gs_draw_text(lx, ty + 2, "Connected to KR4OYK-11 BBS  (Ctrl+A ? = Hotkey Help)",
                     GS_CLR_ACCENT, GS_CLR_BG, 70);
        gs_fill_rect(lx, ty + 14, mw - 24, 96, GS_CLR_PANEL);
        gs_frame_rect(lx, ty + 14, mw - 24, 96, GS_CLR_FG);
        gs_draw_text(lx + 6, ty + 18, "Welcome to KR4OYK-7 BBS on Apple IIGS GS/OS",
                     GS_CLR_FG, GS_CLR_PANEL, 68);
        gs_draw_text(lx + 6, ty + 29, "Ctrl+A H Help | Ctrl+A S YAPP Send | Ctrl+A W Wrap",
                     GS_CLR_ACCENT, GS_CLR_PANEL, 68);
        gs_draw_text(lx + 6, ty + 40, "EOL: Auto-Detected CR | Word-Wrap: ON (72 cols)",
                     GS_CLR_FG, GS_CLR_PANEL, 68);
        gs_draw_text(lx + 6, ty + 52, "Type 'LIST' or 'B' to disconnect from BBS.",
                     GS_CLR_FG, GS_CLR_PANEL, 68);
        gs_draw_text(lx + 6, ty + 96, "> LIST_", GS_CLR_FG, GS_CLR_PANEL, 68);
        gs_draw_button(lx, by, 96, 13, "Ctrl+A Help", 0);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Close", 1);
    } else if (scr == OYK_GSOS_SCR_FILES) {
        gs_draw_text(lx, ty + 2, "Pending Uploads              Approved BBS Files",
                     GS_CLR_ACCENT, GS_CLR_BG, 68);
        gs_fill_rect(lx, ty + 14, 250, 96, GS_CLR_PANEL);
        gs_frame_rect(lx, ty + 14, 250, 96, GS_CLR_FG);
        gs_fill_rect(lx + 262, ty + 14, 266, 96, GS_CLR_PANEL);
        gs_frame_rect(lx + 262, ty + 14, 266, 96, GS_CLR_FG);
        gs_draw_text(lx + 6, ty + 20, "W4ABC-7  PACKETS.TXT  2048", GS_CLR_FG, GS_CLR_PANEL, 32);
        gs_draw_text(lx + 268, ty + 20, "README.TXT          1420", GS_CLR_FG, GS_CLR_PANEL, 34);
        gs_draw_text(lx + 268, ty + 31, "NETLIST.CFG          618", GS_CLR_FG, GS_CLR_PANEL, 34);
        gs_draw_button(lx, by, 84, 13, "Upload...", 0);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Close", 1);
    } else {
        gs_draw_text(lx, ty + 10, "Hamlib rigctld CAT: Port 4532 / SCC .A @ 9600 bps",
                     GS_CLR_FG, GS_CLR_BG, 68);
        gs_draw_button(mx + mw - 96, by, 82, 13, "Close", 1);
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
    const char *bmp_path;
} GsosGalleryStep;

static const GsosGalleryStep kGallerySteps[12] = {
    { OYK_GSOS_SCR_MAIN,       "/OYKOUT/GMAIN.BMP" },
    { OYK_GSOS_SCR_SETTINGS,   "/OYKOUT/GSETT.BMP" },
    { OYK_GSOS_SCR_ADD_IFACE,  "/OYKOUT/GADDI.BMP" },
    { OYK_GSOS_SCR_EDIT_IFACE, "/OYKOUT/GEDTI.BMP" },
    { OYK_GSOS_SCR_PEERS,      "/OYKOUT/GPEER.BMP" },
    { OYK_GSOS_SCR_EDIT_PEER,  "/OYKOUT/GEDTP.BMP" },
    { OYK_GSOS_SCR_TERMINATE,  "/OYKOUT/GTERM.BMP" },
    { OYK_GSOS_SCR_BAN,        "/OYKOUT/GBAN.BMP" },
    { OYK_GSOS_SCR_EXIT_PEERS, "/OYKOUT/GEXIT.BMP" },
    { OYK_GSOS_SCR_THEME,      "/OYKOUT/GTHME.BMP" },
    { OYK_GSOS_SCR_TERMINAL,   "/OYKOUT/GTML.BMP" },
    { OYK_GSOS_SCR_FILES,      "/OYKOUT/GFILE.BMP" }
};

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
        for (i = 0; i < 12; ++i) {
            ui->active_screen = kGallerySteps[i].scr;
            if (ui->active_screen == OYK_GSOS_SCR_PEERS) ui->settings_tab = 4;
            else ui->settings_tab = 0;
            gs_redraw(ui);
            oyk_gsos_capture_screen_bmp(kGallerySteps[i].bmp_path);
        }
        oyk_gsos_finish_test();
        return 0;
    }

    gs_redraw(ui);
    /* Interactive desktop loop */
    for (;;) {
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
