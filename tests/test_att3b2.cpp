/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sysv_core.h"
#include "sysv_tui.h"
#include "oyk_port_parity_tests.hpp"

static int g_fail = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
            ++g_fail; \
        } \
    } while (0)

static int file_contains(const char* rel_path, const char* needle) {
    char path[512];
    sprintf(path, "%s/%s", OYK_SOURCE_DIR, rel_path);
    FILE* fp = fopen(path, "rb");
    if (!fp) return 0;
    char* buf = new char[65536];
    size_t n = fread(buf, 1U, 65535U, fp);
    buf[n] = '\0';
    fclose(fp);
    int found = (strstr(buf, needle) != NULL) ? 1 : 0;
    delete[] buf;
    return found;
}

int main() {
    OykSysvApp* app = new OykSysvApp;
    OykSysvCanvas* cv = new OykSysvCanvas;
    oyk_sysv_app_init(app, "AT&T 3B2 SVR3.2 (WE32100)", 1);

    static const char* const screens[23] = {
        "main", "menu", "settings", "settings_bbs", "settings_chat",
        "settings_ifaces", "peers", "settings_rig", "settings_printer",
        "add_iface_1", "add_iface_2", "add_iface_3", "add_iface_4",
        "edit_iface", "edit_peer", "terminate", "ban", "exit",
        "theme", "terminal", "terminal_help", "files", "about"
    };
    for (int i = 0; i < 23; ++i) {
        CHECK(oyk_sysv_tui_setup_screen(app, screens[i]) == 1);
        oyk_sysv_tui_render(cv, app, 25, 80);
    }
    CHECK(oyk_sysv_canvas_find_text(cv, "AT&T 3B2 SVR3.2") == 1);
    CHECK(file_contains("ports/att3b2/Makefile", "OYK_SYSV_SVR32") == 1);
    CHECK(file_contains("ports/att3b2/Makefile", "-lnet -lnsl_s") == 1);
    CHECK(file_contains("ports/sysv/sysv_net.c", "<sys/in.h>") == 1);

    oyk_sysv_app_init(app, "AT&T 3B2 SVR3.2 (WE32100)", 0);
    CHECK(oyk_sysv_tui_handle_key(app, 's') == 0 && app->screen == OYK_SYSV_SCR_MAIN);
    CHECK(oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_CTRL_N) == 1 && app->screen == OYK_SYSV_SCR_MENU);
    CHECK(oyk_sysv_tui_handle_key(app, 's') == 1 && app->screen == OYK_SYSV_SCR_SETTINGS);
    CHECK(oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_CTRL_N) == 0 && app->screen == OYK_SYSV_SCR_SETTINGS);
    CHECK(oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_F10) == 0 && app->screen == OYK_SYSV_SCR_SETTINGS);
    CHECK(oyk_sysv_tui_handle_key(app, OYK_SYSV_KEY_ESC) == 1 && app->screen == OYK_SYSV_SCR_MAIN);

    OykPortableNode* pnode = new OykPortableNode();
    OykConfig* pcfg = new OykConfig();
    oyk_config_init_default(pcfg);
    char perr[64];
    CHECK(pnode->init(pcfg, NULL, NULL, perr, sizeof(perr)) == 1);
    oyk_run_shared_port_parity_tests(pnode, stdout, &g_fail);
    pnode->shutdown();
    delete pcfg;
    delete pnode;
    delete cv;
    delete app;

    printf("oyk_att3b2_tests: failures=%d\n", g_fail);
    return (g_fail == 0) ? 0 : 1;
}
