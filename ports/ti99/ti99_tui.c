/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */
#include "ti99_tui.h"

#include <stdio.h>
#include <string.h>

static const char *const kTi99LogoAscii[10] = {
    ".-------------.",
    "| .---------. |",
    "| | \\    _. | |",
    "| |  \\   /| | |",
    "| |   `-'   | |",
    "| | o.   .o | |",
    "| '---------' |",
    "|    O Y K    |",
    "|   N O D E   |",
    "'-------------'"
};

static void draw_box(Ti99VdpState *vdp, int r, int c, int h, int w,
                     const char *title, uint8_t attr, int with_close)
{
    int i;
    int j;
    if (h < 3 || w < 6) {
        return;
    }
    for (i = 0; i < h; ++i) {
        for (j = 0; j < w; ++j) {
            char ch = ' ';
            if (i == 0 || i == h - 1) {
                ch = (j == 0 || j == w - 1) ? '+' : '-';
            } else if (j == 0 || j == w - 1) {
                ch = '|';
            }
            ti99_vdp_putc(vdp, r + i, c + j, ch, attr);
        }
    }
    if (title && title[0] && w > 6) {
        char buf[64];
        snprintf(buf, sizeof(buf), " %s ", title);
        ti99_vdp_puts(vdp, r, c + 2, buf, attr);
    }
    if (with_close && w >= 8) {
        ti99_vdp_puts(vdp, r, c + w - 5, "[X]", attr);
    }
}

static void draw_hsep(Ti99VdpState *vdp, int r, int c, int w, uint8_t attr)
{
    int j;
    ti99_vdp_putc(vdp, r, c, '+', attr);
    for (j = 1; j < w - 1; ++j) {
        ti99_vdp_putc(vdp, r, c + j, '-', attr);
    }
    ti99_vdp_putc(vdp, r, c + w - 1, '+', attr);
}

static void render_main_console(Ti99VdpState *vdp, const Ti99App *app)
{
    char line[81];
    int cols = (int)vdp->cols;
    int wide = (cols >= 80);
    int i;

    ti99_vdp_clear(vdp, 0xF4U);
    if (wide) {
        snprintf(line, sizeof(line), " Node   Tab/F9:Term   F10/M:Menu   TI-99/4A [%s]",
                 vdp->is_f18a ? "F18A 80c" : "9918 40c");
    } else {
        snprintf(line, sizeof(line), " Node  Tab:Term  M:Menu");
    }
    ti99_vdp_puts(vdp, 0, 0, line, 0x1FU);

    snprintf(line, sizeof(line), "%s (%s) | %s",
             app->node.cfg.callsign, app->node.cfg.alias,
             wide ? (app->node.cfg.allow_incoming ? "[INCOMING: ALLOW]" : "[INCOMING: DENY]")
                  : (app->node.cfg.allow_incoming ? "[IN:ALLOW]" : "[IN:DENY]"));
    ti99_vdp_puts(vdp, 1, 1, line, 0xF1U);

    if (wide) {
        draw_box(vdp, 2, 0, 7, 40, "Interfaces", 0xF4U, 0);
        for (i = 0; i < (int)app->node.cfg.iface_count && i < 4; ++i) {
            const Ti99IfaceConfig *ifc = &app->node.cfg.ifaces[i];
            snprintf(line, sizeof(line), "%-7s %-11s %-12s %s",
                     ifc->name, ifc->driver, ifc->endpoint, ifc->enabled ? "UP" : "DN");
            ti99_vdp_puts(vdp, 3 + i, 2, line, 0xF4U);
        }
        draw_box(vdp, 2, 40, 7, 40, "Last 5 New Callsigns Heard", 0xF4U, 0);
        for (i = 0; i < (int)app->node.mheard_count && i < 5; ++i) {
            snprintf(line, sizeof(line), "%d. %s", i + 1, app->node.mheard[i]);
            ti99_vdp_puts(vdp, 3 + i, 42, line, 0xF4U);
        }
        draw_box(vdp, 9, 0, 6, 80, "Active Connections", 0xF4U, 0);
        for (i = 0; i < (int)app->node.session_count && i < 4; ++i) {
            const Ti99Session *ss = &app->node.sessions[i];
            snprintf(line, sizeof(line), "%u  %-10s %-8s %s",
                     (unsigned)ss->id, ss->callsign, ss->port_name, ss->state);
            ti99_vdp_puts(vdp, 10 + i, 2, line, 0xF4U);
        }
        draw_box(vdp, 15, 0, 7, 80, "Node Activity Log", 0xF4U, 0);
        for (i = 0; i < (int)app->node.log_count && i < 5; ++i) {
            ti99_vdp_puts(vdp, 16 + i, 2, app->node.logs[i], 0xF4U);
        }
    } else {
        draw_box(vdp, 2, 0, 6, 40, "Interfaces", 0xF4U, 0);
        for (i = 0; i < (int)app->node.cfg.iface_count && i < 3; ++i) {
            snprintf(line, sizeof(line), "%-7s %-11s %s",
                     app->node.cfg.ifaces[i].name,
                     app->node.cfg.ifaces[i].driver,
                     app->node.cfg.ifaces[i].enabled ? "UP" : "DN");
            ti99_vdp_puts(vdp, 3 + i, 2, line, 0xF4U);
        }
        draw_box(vdp, 8, 0, 6, 40, "Last 5 New Callsigns Heard", 0xF4U, 0);
        for (i = 0; i < (int)app->node.mheard_count && i < 4; ++i) {
            ti99_vdp_puts(vdp, 9 + i, 2, app->node.mheard[i], 0xF4U);
        }
        draw_box(vdp, 14, 0, 5, 40, "Active Connections", 0xF4U, 0);
        for (i = 0; i < (int)app->node.session_count && i < 3; ++i) {
            snprintf(line, sizeof(line), "%u %-10s %s",
                     (unsigned)app->node.sessions[i].id,
                     app->node.sessions[i].callsign,
                     app->node.sessions[i].state);
            ti99_vdp_puts(vdp, 15 + i, 2, line, 0xF4U);
        }
        draw_box(vdp, 19, 0, 4, 40, "Node Activity Log", 0xF4U, 0);
        if (app->node.log_count > 0) {
            ti99_vdp_puts(vdp, 20, 2, app->node.logs[app->node.log_count - 1], 0xF4U);
        }
    }
    ti99_vdp_puts(vdp, (int)vdp->rows - 1, 1,
                  wide ? "SAMS:1MB  TIPI:OK  RS232:OK  Q:Exit" : "SAMS:1M TIPI:OK Q:Exit",
                  0x1FU);
}

static void render_local_terminal(Ti99VdpState *vdp, const Ti99App *app)
{
    char bar[81];
    int cols = (int)vdp->cols;
    int rows = (int)vdp->rows;
    int i;
    const char *eol_s = (app->term_eol == TI99_EOL_CRLF) ? "CRLF"
                      : ((app->term_eol == TI99_EOL_CR) ? "CR" : "LF");

    ti99_vdp_clear(vdp, 0xF1U);
    for (i = 0; i < (int)app->term_count && i < rows - 2; ++i) {
        ti99_vdp_puts(vdp, i, 0, app->term_lines[i], 0xF1U);
    }
    snprintf(bar, sizeof(bar),
             cols >= 80
                 ? "Ctrl-A Z:Help | Tab/F9:Console | [EOL:%s] [Echo:%s] Connected"
                 : "Ctrl-A Z:Help [EOL:%s] [Echo:%s]",
             eol_s, app->term_echo ? "ON" : "OFF");
    ti99_vdp_puts(vdp, rows - 2, 0, bar, 0x1FU);
    snprintf(bar, sizeof(bar), "> %s", app->term_input);
    ti99_vdp_puts(vdp, rows - 1, 0, bar, 0xF1U);
}

static void render_modal(Ti99VdpState *vdp, const Ti99App *app)
{
    int cols = (int)vdp->cols;
    int wide = (cols >= 80);
    int mr = 3;
    int mc = wide ? 6 : 1;
    int mh = 18;
    int mw = wide ? 68 : 38;
    char buf[81];
    int i;

    if (app->screen == TI99_SCR_MENU) {
        draw_box(vdp, 1, 1, 12, wide ? 30 : 28, NULL, 0xF1U, 0);
        ti99_vdp_puts(vdp, 2, 3, "[S] Node Settings", 0xF1U);
        ti99_vdp_puts(vdp, 3, 3, "[A] Add Interface", 0xF1U);
        ti99_vdp_puts(vdp, 4, 3, "[B] Ban Callsign", 0xF1U);
        ti99_vdp_puts(vdp, 5, 3, "[T] Local Terminal", 0xF1U);
        ti99_vdp_puts(vdp, 6, 3, "[K] Terminate Conn", 0xF1U);
        ti99_vdp_puts(vdp, 7, 3, "[F] File Manager", 0xF1U);
        ti99_vdp_puts(vdp, 8, 3, "[C] Theme Settings", 0xF1U);
        ti99_vdp_puts(vdp, 9, 3, "[?] About OYK Node", 0xF1U);
        ti99_vdp_puts(vdp, 10, 3, "[Q] Exit Node", 0xF1U);
        return;
    }

    draw_box(vdp, mr, mc, mh, mw, ti99_app_screen_title(app->screen), 0xF1U, 1);

    switch (app->screen) {
    case TI99_SCR_SETTINGS:
    case TI99_SCR_SETTINGS_BBS:
    case TI99_SCR_SETTINGS_CHAT:
    case TI99_SCR_SETTINGS_IFACES:
    case TI99_SCR_PEERS:
    case TI99_SCR_SETTINGS_RIG:
    case TI99_SCR_SETTINGS_PRINTER:
        ti99_vdp_puts(vdp, mr + 1, mc + 2,
                      wide ? "1:Node 2:BBS 3:CHAT 4:Ifaces 5:Peers 6:Rig 7:Prn"
                           : "1:Node 2:BBS 3:CHAT 4:If 5:Pr 6:Rg 7:Pn",
                      0xF1U);
        draw_hsep(vdp, mr + 2, mc, mw, 0xF1U);
        if (app->screen == TI99_SCR_SETTINGS) {
            snprintf(buf, sizeof(buf), "Callsign: %s  Alias: %s",
                     app->node.cfg.callsign, app->node.cfg.alias);
            ti99_vdp_puts(vdp, mr + 4, mc + 2, buf, 0xF1U);
            snprintf(buf, sizeof(buf), "T1: %us   N2: %u   SAMS: 1MB",
                     (unsigned)app->node.cfg.t1_sec, (unsigned)app->node.cfg.n2_retries);
            ti99_vdp_puts(vdp, mr + 6, mc + 2, buf, 0xF1U);
        } else if (app->screen == TI99_SCR_SETTINGS_BBS) {
            snprintf(buf, sizeof(buf), "BBS Callsign: %s  Enabled: %s",
                     app->node.cfg.bbs_callsign, app->node.cfg.bbs_enabled ? "YES" : "NO");
            ti99_vdp_puts(vdp, mr + 4, mc + 2, buf, 0xF1U);
        } else if (app->screen == TI99_SCR_SETTINGS_CHAT) {
            snprintf(buf, sizeof(buf), "CHAT Callsign: %s  Enabled: %s",
                     app->node.cfg.chat_callsign, app->node.cfg.chat_enabled ? "YES" : "NO");
            ti99_vdp_puts(vdp, mr + 4, mc + 2, buf, 0xF1U);
        } else if (app->screen == TI99_SCR_SETTINGS_IFACES) {
            for (i = 0; i < (int)app->node.cfg.iface_count && i < 6; ++i) {
                snprintf(buf, sizeof(buf), "%-7s %-11s %s",
                         app->node.cfg.ifaces[i].name,
                         app->node.cfg.ifaces[i].driver,
                         app->node.cfg.ifaces[i].endpoint);
                ti99_vdp_puts(vdp, mr + 4 + i, mc + 2, buf, 0xF1U);
            }
        } else if (app->screen == TI99_SCR_PEERS) {
            ti99_vdp_puts(vdp, mr + 4, mc + 2, "Auto-Learned & Static Peers", 0xF1U);
        } else if (app->screen == TI99_SCR_SETTINGS_RIG) {
            snprintf(buf, sizeof(buf), "rigctld Host: %s:%u",
                     app->node.cfg.rig_host, (unsigned)app->node.cfg.rig_port);
            ti99_vdp_puts(vdp, mr + 4, mc + 2, buf, 0xF1U);
        } else {
            snprintf(buf, sizeof(buf), "Printer: %s (%s)",
                     app->node.cfg.printer_driver, app->node.cfg.printer_dev);
            ti99_vdp_puts(vdp, mr + 4, mc + 2, buf, 0xF1U);
        }
        draw_hsep(vdp, mr + mh - 3, mc, mw, 0xF1U);
        ti99_vdp_puts(vdp, mr + mh - 2, mc + 2, "[ Save ]  [ Cancel ]", 0xF1U);
        break;

    case TI99_SCR_ADD_IFACE_1:
        ti99_vdp_puts(vdp, mr + 2, mc + 2, "Step 1: Driver", 0xF1U);
        ti99_vdp_puts(vdp, mr + 4, mc + 2, "(*) axudp  (TIPI UDP)", 0xF1U);
        ti99_vdp_puts(vdp, mr + 5, mc + 2, "( ) kiss-serial (RS232)", 0xF1U);
        ti99_vdp_puts(vdp, mr + 6, mc + 2, "( ) kiss-tcp    (TIPI TCP)", 0xF1U);
        ti99_vdp_puts(vdp, mr + mh - 2, mc + 2, "[ Next ]  [ Cancel ]", 0xF1U);
        break;

    case TI99_SCR_ADD_IFACE_2:
        ti99_vdp_puts(vdp, mr + 2, mc + 2, "Step 2: Identity", 0xF1U);
        ti99_vdp_puts(vdp, mr + 4, mc + 2, "Name: tipi0   Description: TIPI", 0xF1U);
        ti99_vdp_puts(vdp, mr + mh - 2, mc + 2, "[ Back ]  [ Next ]  [ Cancel ]", 0xF1U);
        break;

    case TI99_SCR_ADD_IFACE_3:
        ti99_vdp_puts(vdp, mr + 2, mc + 2, "Step 3: Endpoint & Policy", 0xF1U);
        ti99_vdp_puts(vdp, mr + 4, mc + 2, "Allow Same-Interface", 0xF1U);
        ti99_vdp_puts(vdp, mr + 5, mc + 2, "Learned Peer Policy: AUTO", 0xF1U);
        ti99_vdp_puts(vdp, mr + mh - 2, mc + 2, "[ Back ]  [ Next ]  [ Cancel ]", 0xF1U);
        break;

    case TI99_SCR_ADD_IFACE_4:
        ti99_vdp_puts(vdp, mr + 2, mc + 2, "Step 4: Review Summary", 0xF1U);
        ti99_vdp_puts(vdp, mr + 4, mc + 2, "Driver: axudp  Port: 10093", 0xF1U);
        ti99_vdp_puts(vdp, mr + mh - 2, mc + 2, "[ Back ]  [ Finish ]  [ Cancel ]", 0xF1U);
        break;

    case TI99_SCR_EDIT_IFACE:
        ti99_vdp_puts(vdp, mr + 2, mc + 2, "Interface: tipi0 (axudp)", 0xF1U);
        ti99_vdp_puts(vdp, mr + 4, mc + 2, "Allow Same-Interface", 0xF1U);
        ti99_vdp_puts(vdp, mr + 5, mc + 2, "Learned Peer Policy: AUTO", 0xF1U);
        ti99_vdp_puts(vdp, mr + mh - 2, mc + 2, "[ Save ]  [ Cancel ]", 0xF1U);
        break;

    case TI99_SCR_EDIT_PEER:
        ti99_vdp_puts(vdp, mr + 2, mc + 2, "Callsign: W1AW-4  Host: PI.UDP", 0xF1U);
        ti99_vdp_puts(vdp, mr + 4, mc + 2, "Pinned Peer: NO", 0xF1U);
        ti99_vdp_puts(vdp, mr + mh - 2, mc + 2, "[ Save ]  [ Cancel ]", 0xF1U);
        break;

    case TI99_SCR_TERMINATE:
        ti99_vdp_puts(vdp, mr + 2, mc + 2, "ID    Callsign    Port       State", 0xF1U);
        for (i = 0; i < (int)app->node.session_count && i < 6; ++i) {
            snprintf(buf, sizeof(buf), "%-5u %-11s %-10s %s",
                     (unsigned)app->node.sessions[i].id,
                     app->node.sessions[i].callsign,
                     app->node.sessions[i].port_name,
                     app->node.sessions[i].state);
            ti99_vdp_puts(vdp, mr + 3 + i, mc + 2, buf, 0xF1U);
        }
        draw_hsep(vdp, mr + mh - 3, mc, mw, 0xF1U);
        ti99_vdp_puts(vdp, mr + mh - 2, mc + 2, "[ Disconnect ]  [ Cancel ]", 0xF1U);
        break;

    case TI99_SCR_BAN:
        ti99_vdp_puts(vdp, mr + 2, mc + 2, "Callsign: __________  Type: Soft/Hard", 0xF1U);
        ti99_vdp_puts(vdp, mr + mh - 2, mc + 2, "[ Ban ]  [ Unban ]  [ Close ]", 0xF1U);
        break;

    case TI99_SCR_EXIT:
        ti99_vdp_puts(vdp, mr + 3, mc + 2, "Persist auto-learned peers?", 0xF1U);
        ti99_vdp_puts(vdp, mr + mh - 2, mc + 2,
                      wide ? "[ Save & Exit ] [ Discard ] [ Review Peers ] [ Cancel ]"
                           : "[Save&Exit] [Discard] [Review]",
                      0xF1U);
        break;

    case TI99_SCR_THEME:
        snprintf(buf, sizeof(buf), "Theme: < %s >", app->node.cfg.theme_name);
        ti99_vdp_puts(vdp, mr + 2, mc + 2, buf, 0xF1U);
        ti99_vdp_puts(vdp, mr + 4, mc + 2, "Role / Key / Color / Preview", 0xF1U);
        ti99_vdp_puts(vdp, mr + 5, mc + 2, "Desktop / Bg / VDP Blue / [####]", 0xF1U);
        draw_hsep(vdp, mr + mh - 3, mc, mw, 0xF1U);
        ti99_vdp_puts(vdp, mr + mh - 2, mc + 2, "[ Apply ]  [ Reset ]  [ Close ]", 0xF1U);
        break;

    case TI99_SCR_TERMINAL_HELP:
        ti99_vdp_puts(vdp, mr + 2, mc + 2, "Ctrl-A Z : Help", 0xF1U);
        ti99_vdp_puts(vdp, mr + 3, mc + 2, "Ctrl-A X / Tab / F9 : Console", 0xF1U);
        ti99_vdp_puts(vdp, mr + 4, mc + 2, "Ctrl-A E : Toggle Local Echo", 0xF1U);
        ti99_vdp_puts(vdp, mr + mh - 2, mc + 2, "[ Close ]", 0xF1U);
        break;

    case TI99_SCR_FILES:
    {
        Ti99FileEntry inc[8];
        Ti99FileEntry fil[8];
        uint8_t ni = ti99_app_list_files(app, 0, inc, 8);
        uint8_t nf = ti99_app_list_files(app, 1, fil, 8);
        ti99_vdp_puts(vdp, mr + 2, mc + 2, "Incoming:", 0xF1U);
        ti99_vdp_puts(vdp, mr + 2, mc + mw / 2, "Files:", 0xF1U);
        for (i = 0; i < (int)ni && i < 8; ++i) {
            ti99_vdp_puts(vdp, mr + 3 + i, mc + 2, inc[i].name, 0xF1U);
        }
        for (i = 0; i < (int)nf && i < 8; ++i) {
            ti99_vdp_puts(vdp, mr + 3 + i, mc + mw / 2, fil[i].name, 0xF1U);
        }
        ti99_vdp_puts(vdp, mr + mh - 2, mc + 2, "[ Approve ]  [ Close ]", 0xF1U);
        break;
    }

    case TI99_SCR_ABOUT:
        for (i = 0; i < 10; ++i) {
            ti99_vdp_puts(vdp, mr + 2 + i, mc + 2, kTi99LogoAscii[i], 0xF1U);
        }
        if (wide) {
            ti99_vdp_puts(vdp, mr + 3, mc + 19, "OYK Packet Node (TI-99/4A)", 0xF1U);
            ti99_vdp_puts(vdp, mr + 5, mc + 19, "TMS9900 + SAMS 1MB + TIPI + F18A", 0xF1U);
            ti99_vdp_puts(vdp, mr + 7, mc + 19, "GPL-3.0-or-later (C) KR4OYK", 0xF1U);
        } else {
            ti99_vdp_puts(vdp, mr + 13, mc + 2, "OYK Packet Node TI-99/4A", 0xF1U);
        }
        ti99_vdp_puts(vdp, mr + mh - 2, mc + 2, "[ Close ]", 0xF1U);
        break;

    default:
        break;
    }
}

void ti99_tui_render(Ti99VdpState *vdp, const Ti99App *app)
{
    if (!vdp || !app) {
        return;
    }
    if (app->screen == TI99_SCR_TERMINAL || app->screen == TI99_SCR_TERMINAL_HELP) {
        render_local_terminal(vdp, app);
        if (app->screen == TI99_SCR_TERMINAL_HELP) {
            render_modal(vdp, app);
        }
        return;
    }
    render_main_console(vdp, app);
    if (app->screen != TI99_SCR_MAIN) {
        render_modal(vdp, app);
    }
}
