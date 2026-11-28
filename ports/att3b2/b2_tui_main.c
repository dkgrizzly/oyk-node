/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "sysv_core.h"
#include "sysv_tui.h"
#include "sysv_net.h"

#include <curses.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#ifdef OYK_SYSV_SVR32
extern void *malloc(size_t);
extern void  free(void *);
extern int   atoi(const char *);
extern int   poll();
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

static int map_3b2_key(int ch) {
    int c2;
    int c3;
    if (ch == ERR) return OYK_SYSV_KEY_NONE;
    if (ch == KEY_UP) return OYK_SYSV_KEY_UP;
    if (ch == KEY_DOWN) return OYK_SYSV_KEY_DOWN;
    if (ch == KEY_LEFT) return OYK_SYSV_KEY_LEFT;
    if (ch == KEY_RIGHT) return OYK_SYSV_KEY_RIGHT;
    if (ch == KEY_HOME) return OYK_SYSV_KEY_HOME;
    if (ch == KEY_END) return OYK_SYSV_KEY_END;
    if (ch == KEY_PPAGE) return OYK_SYSV_KEY_PGUP;
    if (ch == KEY_NPAGE) return OYK_SYSV_KEY_PGDN;
    if (ch == KEY_F(9)) return OYK_SYSV_KEY_F9;
    if (ch == KEY_F(10)) return OYK_SYSV_KEY_F10;
    if (ch == KEY_BACKSPACE || ch == 8 || ch == 127) return OYK_SYSV_KEY_BS;
    if (ch == KEY_ENTER || ch == '\r' || ch == '\n') return OYK_SYSV_KEY_ENTER;
    if (ch == '\t') return OYK_SYSV_KEY_TAB;
    if (ch == 14) return OYK_SYSV_KEY_CTRL_N;
    if (ch == 1) return OYK_SYSV_KEY_CTRL_A;
    if (ch == 27) {
        c2 = getch();
        if (c2 == ERR) {
            (void)poll(NULL, 0UL, 25);
            c2 = getch();
        }
        if (c2 == ERR) return OYK_SYSV_KEY_ESC;
        if (c2 == '[' || c2 == 'O') {
            c3 = getch();
            if (c3 == ERR) {
                (void)poll(NULL, 0UL, 25);
                c3 = getch();
            }
            if (c3 == 'A') return OYK_SYSV_KEY_UP;
            if (c3 == 'B') return OYK_SYSV_KEY_DOWN;
            if (c3 == 'C') return OYK_SYSV_KEY_RIGHT;
            if (c3 == 'D') return OYK_SYSV_KEY_LEFT;
            if (c3 == 'H') return OYK_SYSV_KEY_HOME;
            if (c3 == 'F') return OYK_SYSV_KEY_END;
            if (c3 == '2') {
                int c4;
                int c5;
                c4 = getch();
                c5 = getch();
                if (c4 == '0' && c5 == '~') return OYK_SYSV_KEY_F9;
                if (c4 == '1' && c5 == '~') return OYK_SYSV_KEY_F10;
            }
            return OYK_SYSV_KEY_NONE;
        }
        return OYK_SYSV_KEY_ESC;
    }
    return ch;
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
    /* Verify clean startup & hotkey / modal preemption */
    oyk_sysv_app_init(app, "AT&T 3B2 SVR3.2 (WE32100)", 0);
    if (oyk_sysv_node_active_conn_count(&app->node) != 0 ||
        app->node.heard_count != 0 || app->term_connected != 0 ||
        oyk_sysv_tui_handle_key(app, 's') != 0 ||
        oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_CTRL_N) != 1 ||
        app->screen != OYK_SYSV_SCR_MENU) {
        free(ansi);
        free(cv);
        return 2;
    }
    oyk_sysv_tui_handle_key(app, 's');
    if (app->screen != OYK_SYSV_SCR_SETTINGS ||
        oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_CTRL_N) != 0 ||
        oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_F10) != 0 ||
        oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_F9) != 0 ||
        app->screen != OYK_SYSV_SCR_SETTINGS) {
        free(ansi);
        free(cv);
        return 3;
    }
    oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_LEFT);
    if (app->settings_tab != 7) {
        free(ansi);
        free(cv);
        return 4;
    }
    oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_ESC);
    if (app->screen != OYK_SYSV_SCR_MAIN) {
        free(ansi);
        free(cv);
        return 5;
    }
    free(ansi);
    free(cv);
    printf("oyk-node-3b2 TUI self-test: 23/23 screens + hotkey/modal OK\n");
    return 0;
}

int main(int argc, char **argv) {
    OykSysvApp *app;
    OykSysvCanvas *cv;
    OykSysvNet *net;
    const char *cfg_path;
    int i;
    int headless;
    int unit_test;
    int ansi_mode;
    int ansi_color;
    int needs_redraw;
    int last_mr;
    int last_mc;

    headless = 0;
    unit_test = 0;
    ansi_mode = 0;
    ansi_color = 0;
    cfg_path = NULL;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) headless = 1;
        else if (strcmp(argv[i], "--unit-test") == 0) unit_test = 1;
        else if (strcmp(argv[i], "--ansi") == 0) ansi_mode = 1;
        else if (strcmp(argv[i], "--ansi-color") == 0) { ansi_mode = 1; ansi_color = 1; }
        else if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) && i + 1 < argc) {
            cfg_path = argv[++i];
        }
    }

    app = (OykSysvApp *)malloc(sizeof(OykSysvApp));
    if (!app) return 1;
    oyk_sysv_app_init(app, "AT&T 3B2 SVR3.2 (WE32100)", unit_test ? 1 : 0);
    if (cfg_path) {
        (void)oyk_sysv_config_load_file(cfg_path, &app->node.cfg);
        oyk_sysv_filemgr_init_ex(&app->fm, app->node.cfg.incoming_path,
                                 app->node.cfg.bbs_files_path, unit_test ? 1 : 0);
    } else if (!unit_test) {
        if (oyk_sysv_config_load_file("node.cfg", &app->node.cfg)) {
            oyk_sysv_filemgr_init_ex(&app->fm, app->node.cfg.incoming_path,
                                     app->node.cfg.bbs_files_path, 0);
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

    needs_redraw = 1;
    last_mr = 0;
    last_mc = 0;
    while (app->running && !g_3b2_stop) {
        int mr;
        int mc;
        int ch;
        int mapped;
        sysv_u8 rx[256];
        getmaxyx(stdscr, mr, mc);
        if (mr != last_mr || mc != last_mc) {
            last_mr = mr;
            last_mc = mc;
            needs_redraw = 1;
        }
        if (needs_redraw) {
            oyk_sysv_tui_render(cv, app, mr, mc);
            flush_to_curses(cv);
            needs_redraw = 0;
        }
        if (oyk_sysv_net_poll_udp(net, rx, sizeof(rx), 50) > 0) {
            needs_redraw = 1;
        }
        ch = getch();
        if (ch != ERR) {
            mapped = map_3b2_key(ch);
            if (oyk_sysv_tui_handle_key(app, mapped) != 0) {
                needs_redraw = 1;
            }
        }
    }

    endwin();
    free(cv);
    oyk_sysv_net_shutdown(net);
    free(net);
    free(app);
    return 0;
}
