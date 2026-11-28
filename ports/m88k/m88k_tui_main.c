/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "sysv_core.h"
#include "sysv_tui.h"
#include "sysv_net.h"

#include <curses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile int g_m88k_stop = 0;

static void on_sig(int sig) {
    (void)sig;
    g_m88k_stop = 1;
}

static int map_curses_style(int st, int has_clr) {
    if (!has_clr) {
        if (st == OYK_SYSV_ST_MENU_SEL || st == OYK_SYSV_ST_DLG_SEL ||
            st == OYK_SYSV_ST_HEADER || st == OYK_SYSV_ST_ALLOW ||
            st == OYK_SYSV_ST_DENY || st == OYK_SYSV_ST_LIST_HDR) {
            return A_REVERSE;
        }
        return A_NORMAL;
    }
    switch (st) {
        case OYK_SYSV_ST_HEADER:
        case OYK_SYSV_ST_MENU_SEL:
        case OYK_SYSV_ST_DLG_SEL:
            return (int)(COLOR_PAIR(1) | A_BOLD);
        case OYK_SYSV_ST_ALLOW:
        case OYK_SYSV_ST_OK:
            return (int)(COLOR_PAIR(2) | A_BOLD);
        case OYK_SYSV_ST_DENY:
        case OYK_SYSV_ST_CLOSE_BTN:
        case OYK_SYSV_ST_ERROR:
            return (int)(COLOR_PAIR(3) | A_BOLD);
        case OYK_SYSV_ST_LIST_HDR:
        case OYK_SYSV_ST_STATUS:
        case OYK_SYSV_ST_DLG_BAR:
            return (int)COLOR_PAIR(4);
        case OYK_SYSV_ST_WARN:
        case OYK_SYSV_ST_TITLE:
            return (int)(COLOR_PAIR(5) | A_BOLD);
        default:
            return (int)COLOR_PAIR(6);
    }
}

static void flush_canvas_to_curses(const OykSysvCanvas *cv, int has_clr) {
    int r;
    int c;
    int mr;
    int mc;
    getmaxyx(stdscr, mr, mc);
    for (r = 0; r < cv->rows && r < mr; ++r) {
        for (c = 0; c < cv->cols && c < mc; ++c) {
            attrset(map_curses_style((int)cv->styles[r][c], has_clr));
            mvaddch(r, c, (unsigned char)cv->chars[r][c]);
        }
    }
    attrset(A_NORMAL);
    refresh();
}

static int run_tui_self_test(OykSysvApp *app) {
    OykSysvCanvas *cv;
    int i;
    static const char * const screens[23] = {
        "main", "menu", "settings", "settings_bbs", "settings_chat",
        "settings_ifaces", "peers", "settings_rig", "settings_printer",
        "add_iface_1", "add_iface_2", "add_iface_3", "add_iface_4",
        "edit_iface", "edit_peer", "terminate", "ban", "exit",
        "theme", "terminal", "terminal_help", "files", "about"
    };
    cv = (OykSysvCanvas *)malloc(sizeof(OykSysvCanvas));
    if (!cv) return 1;
    for (i = 0; i < 23; ++i) {
        if (!oyk_sysv_tui_setup_screen(app, screens[i])) {
            free(cv);
            return 1;
        }
        oyk_sysv_tui_render(cv, app, 25, 80);
    }
    free(cv);
    printf("oyk-node-m88k TUI self-test: 23/23 screens OK\n");
    return 0;
}

int main(int argc, char **argv) {
    OykSysvApp *app;
    OykSysvCanvas *cv;
    OykSysvNet *net;
    int i;
    int headless;
    int unit_test;
    int has_clr;

    headless = 0;
    unit_test = 0;
    app = (OykSysvApp *)malloc(sizeof(OykSysvApp));
    if (!app) return 1;
    oyk_sysv_app_init(app, "Motorola 88K SVR4 (mc88100)", 1);

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) headless = 1;
        else if (strcmp(argv[i], "--unit-test") == 0) unit_test = 1;
        else if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) && i + 1 < argc) {
            oyk_sysv_config_load_file(argv[++i], &app->node.cfg);
        }
    }

    if (unit_test) {
        int rc;
        rc = run_tui_self_test(app);
        free(app);
        return rc;
    }

    net = (OykSysvNet *)malloc(sizeof(OykSysvNet));
    if (!net) { free(app); return 1; }
    oyk_sysv_net_init(net, &app->node.cfg);

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    if (headless) {
        sysv_u8 rx[512];
        printf("oyk-node-m88k running in headless mode (UDP port %u)\n",
               (unsigned int)net->bound_port);
        while (!g_m88k_stop) {
            (void)oyk_sysv_net_poll_udp(net, rx, sizeof(rx), 250);
            break;
        }
        oyk_sysv_net_shutdown(net);
        free(net);
        free(app);
        return 0;
    }

    cv = (OykSysvCanvas *)malloc(sizeof(OykSysvCanvas));
    if (!cv) { oyk_sysv_net_shutdown(net); free(net); free(app); return 1; }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    has_clr = has_colors() ? 1 : 0;
    if (has_clr) {
        start_color();
        init_pair(1, COLOR_WHITE, COLOR_BLUE);
        init_pair(2, COLOR_WHITE, COLOR_GREEN);
        init_pair(3, COLOR_WHITE, COLOR_RED);
        init_pair(4, COLOR_BLACK, COLOR_WHITE);
        init_pair(5, COLOR_YELLOW, COLOR_BLACK);
        init_pair(6, COLOR_WHITE, COLOR_BLACK);
    }

    while (app->running && !g_m88k_stop) {
        int mr;
        int mc;
        int ch;
        sysv_u8 rx[256];
        getmaxyx(stdscr, mr, mc);
        oyk_sysv_tui_render(cv, app, mr, mc);
        flush_canvas_to_curses(cv, has_clr);
        (void)oyk_sysv_net_poll_udp(net, rx, sizeof(rx), 50);
        ch = getch();
        if (ch == ERR) continue;
        if (ch == 27) {
            if (app->screen == OYK_SYSV_SCR_MAIN) break;
            app->screen = OYK_SYSV_SCR_MAIN;
        } else if (ch == KEY_F(10) || ch == 'm' || ch == 'M') {
            app->screen = (app->screen == OYK_SYSV_SCR_MENU) ? OYK_SYSV_SCR_MAIN : OYK_SYSV_SCR_MENU;
        } else if (ch == '\t' || ch == KEY_F(9)) {
            app->screen = (app->screen == OYK_SYSV_SCR_TERMINAL) ? OYK_SYSV_SCR_MAIN : OYK_SYSV_SCR_TERMINAL;
        } else if (ch == 's' || ch == 'S') {
            app->screen = OYK_SYSV_SCR_SETTINGS;
        } else if (ch == 't' || ch == 'T') {
            app->screen = OYK_SYSV_SCR_THEME;
        } else if (ch == 'f' || ch == 'F') {
            app->screen = OYK_SYSV_SCR_FILES;
        } else if (ch == 'q' || ch == 'Q' || ch == 'x' || ch == 'X') {
            oyk_sysv_app_exec_menu_action(app, OYK_SYSV_ACT_EXIT);
        } else if (app->screen == OYK_SYSV_SCR_SETTINGS && ch >= '1' && ch <= '7') {
            app->settings_tab = ch - '1';
        }
    }

    endwin();
    free(cv);
    oyk_sysv_net_shutdown(net);
    free(net);
    free(app);
    return 0;
}
