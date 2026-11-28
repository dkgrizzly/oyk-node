/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "sysv_core.h"
#include "sysv_tui.h"
#include "sysv_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_m88k_fail = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "M88K TEST FAIL line %d: %s\n", __LINE__, #cond); \
            ++g_m88k_fail; \
        } \
    } while (0)

int main(void) {
    OykSysvApp *app;
    OykSysvCanvas *cv;
    OykSysvNet *net;
    OykSysvFrame f, df;
    sysv_u8 raw[64];
    size_t elen;
    sysv_u8 be_buf[4];
    int i;
    static const char * const screens[23] = {
        "main", "menu", "settings", "settings_bbs", "settings_chat",
        "settings_ifaces", "peers", "settings_rig", "settings_printer",
        "add_iface_1", "add_iface_2", "add_iface_3", "add_iface_4",
        "edit_iface", "edit_peer", "terminate", "ban", "exit",
        "theme", "terminal", "terminal_help", "files", "about"
    };

    /* Verify mc88100 big-endian & byte-order helpers */
    oyk_sysv_be16_write(be_buf, 0xCAFEU);
    CHECK(be_buf[0] == 0xCAU && be_buf[1] == 0xFEU);
    CHECK(oyk_sysv_be16_read(be_buf) == 0xCAFEU);

    /* AX.25 codec + CRC-16 */
    memset(&f, 0, sizeof(f));
    CHECK(oyk_sysv_parse_callsign("KR4OYK-7", &f.dst) == 1);
    CHECK(oyk_sysv_parse_callsign("W4ABC-1", &f.src) == 1);
    f.is_command = 1;
    f.control = 0x3FU;
    elen = oyk_sysv_ax25_encode(&f, raw, sizeof(raw));
    CHECK(elen >= 16U && oyk_sysv_ax25_decode(raw, elen, &df) == 1);

    app = (OykSysvApp *)malloc(sizeof(OykSysvApp));
    cv = (OykSysvCanvas *)malloc(sizeof(OykSysvCanvas));
    net = (OykSysvNet *)malloc(sizeof(OykSysvNet));
    CHECK(app != NULL && cv != NULL && net != NULL);

    if (app && cv && net) {
        oyk_sysv_app_init(app, "Motorola 88K SVR4 (mc88100)", 0);
        CHECK(oyk_sysv_node_active_conn_count(&app->node) == 0);
        CHECK(app->node.heard_count == 0);
        CHECK(app->node.ban_count == 0);
        CHECK(app->term_connected == 0);
        CHECK(oyk_sysv_tui_handle_key(app, 's') == 0 && app->screen == OYK_SYSV_SCR_MAIN);
        CHECK(oyk_sysv_tui_handle_key(app, 'm') == 0 && app->screen == OYK_SYSV_SCR_MAIN);
        CHECK(oyk_sysv_tui_handle_key(app, 'q') == 0 && app->running == 1);
        CHECK(oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_CTRL_N) == 1 && app->screen == OYK_SYSV_SCR_MENU);
        CHECK(oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_DOWN) == 1);
        CHECK(oyk_sysv_tui_handle_key(app, 's') == 1 && app->screen == OYK_SYSV_SCR_SETTINGS);
        CHECK(oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_CTRL_N) == 0 && app->screen == OYK_SYSV_SCR_SETTINGS);
        CHECK(oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_F10) == 0 && app->screen == OYK_SYSV_SCR_SETTINGS);
        CHECK(oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_F9) == 0 && app->screen == OYK_SYSV_SCR_SETTINGS);
        CHECK(oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_LEFT) == 1 && app->settings_tab == 7);
        CHECK(oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_ESC) == 1 && app->screen == OYK_SYSV_SCR_MAIN);

        oyk_sysv_app_init(app, "Motorola 88K SVR4 (mc88100)", 1);
        oyk_sysv_net_init(net, &app->node.cfg);

        for (i = 0; i < 23; ++i) {
            CHECK(oyk_sysv_tui_setup_screen(app, screens[i]) == 1);
            oyk_sysv_tui_render(cv, app, 25, 80);
        }
        CHECK(oyk_sysv_canvas_find_text(cv, "Motorola 88K SVR4") == 1);
        CHECK(oyk_sysv_filemgr_up(&app->fm, 0) == 0);
        CHECK(oyk_sysv_filemgr_enter_dir(&app->fm, 0, "W3TRM-1") == 1);
        CHECK(strcmp(app->fm.inc.entries[0].name, "..") == 0);
        CHECK(oyk_sysv_filemgr_up(&app->fm, 0) == 1);
        oyk_sysv_net_shutdown(net);
    }

    if (net) free(net);
    if (cv) free(cv);
    if (app) free(app);

    printf("oyk-m88k-test: failures=%d\n", g_m88k_fail);
    return (g_m88k_fail == 0) ? 0 : 1;
}
