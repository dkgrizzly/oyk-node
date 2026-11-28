/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "sysv_core.h"
#include "sysv_tui.h"
#include "sysv_net.h"

#include <stdio.h>
#ifdef OYK_SYSV_SVR32
extern void *malloc(size_t);
extern void  free(void *);
#else
#include <stdlib.h>
#endif
#include <string.h>

static int g_3b2_fail = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "3B2 TEST FAIL line %d: %s\n", __LINE__, #cond); \
            ++g_3b2_fail; \
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

    /* WE32100 big-endian byte-order verification */
    oyk_sysv_be32_write(be_buf, 0x0BADC0DEUL);
    CHECK(be_buf[0] == 0x0BU && be_buf[3] == 0xDEU);
    CHECK(oyk_sysv_be32_read(be_buf) == 0x0BADC0DEUL);

    memset(&f, 0, sizeof(f));
    CHECK(oyk_sysv_parse_callsign("KR4OYK-7", &f.dst) == 1);
    CHECK(oyk_sysv_parse_callsign("N4XYZ-2", &f.src) == 1);
    f.is_command = 1;
    f.control = 0x3FU;
    elen = oyk_sysv_ax25_encode(&f, raw, sizeof(raw));
    CHECK(elen >= 16U && oyk_sysv_ax25_decode(raw, elen, &df) == 1);

    app = (OykSysvApp *)malloc(sizeof(OykSysvApp));
    cv = (OykSysvCanvas *)malloc(sizeof(OykSysvCanvas));
    net = (OykSysvNet *)malloc(sizeof(OykSysvNet));
    CHECK(app != NULL && cv != NULL && net != NULL);

    if (app && cv && net) {
        oyk_sysv_app_init(app, "AT&T 3B2 SVR3.2 (WE32100)", 1);
        oyk_sysv_net_init(net, &app->node.cfg);

        for (i = 0; i < 23; ++i) {
            CHECK(oyk_sysv_tui_setup_screen(app, screens[i]) == 1);
            oyk_sysv_tui_render(cv, app, 25, 80);
        }
        CHECK(oyk_sysv_canvas_find_text(cv, "AT&T 3B2 SVR3.2") == 1);
        CHECK(oyk_sysv_filemgr_up(&app->fm, 0) == 0);
        CHECK(oyk_sysv_filemgr_enter_dir(&app->fm, 0, "W3TRM-1") == 1);
        CHECK(strcmp(app->fm.inc.entries[0].name, "..") == 0);
        CHECK(oyk_sysv_filemgr_up(&app->fm, 0) == 1);
        oyk_sysv_net_shutdown(net);
    }

    if (net) free(net);
    if (cv) free(cv);
    if (app) free(app);

    printf("oyk-3b2-test: failures=%d\n", g_3b2_fail);
    return (g_3b2_fail == 0) ? 0 : 1;
}
