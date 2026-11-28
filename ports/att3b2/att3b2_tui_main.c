/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "sysv_core.h"
#include "sysv_tui.h"
#include "sysv_net.h"

#include <curses.h>
#include <signal.h>
#include <stdio.h>
#ifdef OYK_SYSV_SVR32
extern void *malloc(size_t);
extern void  free(void *);
extern int   atoi(const char *);
#else
#include <stdlib.h>
#endif
#include <string.h>

static volatile int g_3b2_stop = 0;

static void on_sig(int sig) {
    (void)sig;
    g_3b2_stop = 1;
}

static int map_3b2_style(int st) {
    if (st == OYK_SYSV_ST_MENU_SEL || st == OYK_SYSV_ST_DLG_SEL ||
        st == OYK_SYSV_ST_HEADER || st == OYK_SYSV_ST_ALLOW ||
        st == OYK_SYSV_ST_DENY || st == OYK_SYSV_ST_LIST_HDR) {
        return A_REVERSE;
    }
    if (st == OYK_SYSV_ST_TITLE || st == OYK_SYSV_ST_CLOSE_BTN) {
        return A_BOLD;
    }
    if (st == OYK_SYSV_ST_WARN) {
        return A_UNDERLINE;
    }
    return A_NORMAL;
}

static void flush_to_curses(const OykSysvCanvas *cv) {
    int r;
    int c;
    int mr;
    int mc;
    getmaxyx(stdscr, mr, mc);
    for (r = 0; r < cv->rows && r < mr; ++r) {
        for (c = 0; c < cv->cols && c < mc; ++c) {
            attrset(map_3b2_style((int)cv->styles[r][c]));
            mvaddch(r, c, (unsigned char)cv->chars[r][c]);
        }
    }
    attrset(A_NORMAL);
    refresh();
}

static int run_3b2_self_test(OykSysvApp *app, int ansi_color) {
    OykSysvCanvas *cv;
    char *ansi;
    int i;
    static const char * const screens[23] = {
        "main", "menu", "settings", "settings_bbs", "settings_chat",
        "settings_ifaces", "peers", "settings_rig", "settings_printer",
        "add_iface_1", "add_iface_2", "add_iface_3", "add_iface_4",
        "edit_iface", "edit_peer", "terminate", "ban", "exit",
        "theme", "terminal", "terminal_help", "files", "about"
    };
    cv = (OykSysvCanvas *)malloc(sizeof(OykSysvCanvas));
    ansi = (char *)malloc(16384U);
    if (!cv || !ansi) {
        if (cv) free(cv);
        if (ansi) free(ansi);
        return 1;
    }
    for (i = 0; i < 23; ++i) {
        if (!oyk_sysv_tui_setup_screen(app, screens[i])) {
            free(ansi);
            free(cv);
            return 1;
        }
        oyk_sysv_tui_render(cv, app, 25, 80);
    }
    oyk_sysv_tui_to_ansi(cv, ansi_color, ansi, 16384U);
    free(ansi);
    free(cv);
    printf("oyk-node-3b2 TUI self-test: 23/23 screens OK\n");
    return 0;
}

int main(int argc, char **argv) {
    OykSysvApp *app;
    OykSysvCanvas *cv;
    OykSysvNet *net;
    int i;
    int headless;
    int unit_test;
    int ansi_mode;
    int ansi_color;

    headless = 0;
    unit_test = 0;
    ansi_mode = 0;
    ansi_color = 0;
    app = (OykSysvApp *)malloc(sizeof(OykSysvApp));
    if (!app) return 1;
    oyk_sysv_app_init(app, "AT&T 3B2 SVR3.2 (WE32100)", 1);

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) headless = 1;
        else if (strcmp(argv[i], "--unit-test") == 0) unit_test = 1;
        else if (strcmp(argv[i], "--ansi") == 0) ansi_mode = 1;
        else if (strcmp(argv[i], "--ansi-color") == 0) { ansi_mode = 1; ansi_color = 1; }
        else if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) && i + 1 < argc) {
            oyk_sysv_config_load_file(argv[++i], &app->node.cfg);
        }
    }

    if (unit_test) {
        int rc;
        rc = run_3b2_self_test(app, ansi_color);
        free(app);
        return rc;
    }

    net = (OykSysvNet *)malloc(sizeof(OykSysvNet));
    if (!net) { free(app); return 1; }
    oyk_sysv_net_init(net, &app->node.cfg);

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    if (headless) {
        sysv_u8 rx[256];
        printf("oyk-node-3b2 running in headless mode (net=%d port=%u)\n",
               net->net_available, (unsigned int)net->bound_port);
        (void)oyk_sysv_net_poll_udp(net, rx, sizeof(rx), 100);
        oyk_sysv_net_shutdown(net);
        free(net);
        free(app);
        return 0;
    }

    cv = (OykSysvCanvas *)malloc(sizeof(OykSysvCanvas));
    if (!cv) { oyk_sysv_net_shutdown(net); free(net); free(app); return 1; }

    if (ansi_mode) {
        char *ansi;
        ansi = (char *)malloc(16384U);
        if (ansi) {
            oyk_sysv_tui_render(cv, app, 25, 80);
            oyk_sysv_tui_to_ansi(cv, ansi_color, ansi, 16384U);
            fputs(ansi, stdout);
            fflush(stdout);
            free(ansi);
        }
        free(cv);
        oyk_sysv_net_shutdown(net);
        free(net);
        free(app);
        return 0;
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

    while (app->running && !g_3b2_stop) {
        int mr;
        int mc;
        int ch;
        sysv_u8 rx[256];
        getmaxyx(stdscr, mr, mc);
        oyk_sysv_tui_render(cv, app, mr, mc);
        flush_to_curses(cv);
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
