/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "vms_compat.h"
#include "sysv_core.h"
#include "sysv_tui.h"
#include "sysv_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run_vms_console_selftest(void)
{
    OykSysvApp app;
    OykSysvCanvas cv;
    const char *screens[23] = {
        "main", "menu", "settings", "settings_bbs", "settings_chat",
        "settings_ifaces", "peers", "settings_rig", "settings_printer",
        "add_iface_1", "add_iface_2", "add_iface_3", "add_iface_4",
        "edit_iface", "edit_peer", "terminate", "ban", "exit",
        "theme", "terminal", "terminal_help", "files", "about"
    };
    int i;

    oyk_sysv_app_init(&app, OYK_VMS_ARCH_NAME, 1);
    for (i = 0; i < 23; ++i) {
        if (!oyk_sysv_tui_setup_screen(&app, screens[i])) {
            return 1;
        }
        oyk_sysv_tui_render(&cv, &app, 24, 80);
        if (i == 22 && !oyk_sysv_canvas_find_text(&cv, "O Y K")) {
            return 2;
        }
        if (i == 19 && !oyk_sysv_canvas_find_text(&cv, "Ctrl-A Z:Help")) {
            return 3;
        }
    }
    oyk_sysv_app_init(&app, OYK_VMS_ARCH_NAME, 0);
    if (oyk_sysv_node_active_conn_count(&app.node) != 0 ||
        app.term_connected != 0 ||
        oyk_sysv_tui_handle_key(&app, 's') != 0 ||
        oyk_sysv_tui_handle_key(&app, OYK_SYSV_KEY_CTRL_N) != 1 ||
        app.screen != OYK_SYSV_SCR_MENU) {
        return 4;
    }
    oyk_sysv_tui_handle_key(&app, 's');
    if (app.screen != OYK_SYSV_SCR_SETTINGS ||
        oyk_sysv_tui_handle_key(&app, OYK_SYSV_KEY_CTRL_N) != 0 ||
        oyk_sysv_tui_handle_key(&app, OYK_SYSV_KEY_F10) != 0 ||
        oyk_sysv_tui_handle_key(&app, OYK_SYSV_KEY_LEFT) != 1 ||
        app.settings_tab != 7) {
        return 5;
    }
    oyk_sysv_tui_handle_key(&app, OYK_SYSV_KEY_ESC);
    if (app.screen != OYK_SYSV_SCR_MAIN) {
        return 6;
    }
    return 0;
}

int main(int argc, char **argv)
{
    OykSysvApp app;
    OykSysvCanvas cv;
    OykSysvNet net;
    int i;
    int unit_test = 0;
    int headless = 0;
    int ansi_dump = 0;
    const char *cfg_path = NULL;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--unit-test") == 0) {
            unit_test = 1;
        } else if (strcmp(argv[i], "--headless") == 0) {
            headless = 1;
        } else if (strcmp(argv[i], "--ansi") == 0) {
            ansi_dump = 1;
        } else if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) &&
                   i + 1 < argc) {
            cfg_path = argv[++i];
        }
    }

    if (unit_test) {
        int rc = run_vms_console_selftest();
        if (rc == 0) {
            printf("OYK OpenVMS SMG$/VT100 console self-test PASSED (%s)\n",
                   OYK_VMS_ARCH_NAME);
        }
        return rc;
    }

    oyk_sysv_app_init(&app, OYK_VMS_ARCH_NAME, 0);
    if (cfg_path) {
        oyk_sysv_config_load_file(cfg_path, &app.node.cfg);
    } else {
        (void)oyk_sysv_config_load_file("node.cfg", &app.node.cfg);
    }
    oyk_sysv_net_init(&net, &app.node.cfg);

    if (headless) {
        char call_buf[16];
        oyk_sysv_format_callsign(&app.node.cfg.callsign, call_buf, sizeof(call_buf));
        printf("OYK OpenVMS headless daemon (%s) ready: %s\n",
               OYK_VMS_ARCH_NAME, call_buf);
        oyk_sysv_net_shutdown(&net);
        return 0;
    }

#if defined(__VMS)
    {
        unsigned long pb = 0;
        unsigned long vd = 0;
        unsigned long rows = 24;
        unsigned long cols = 80;
        if (smg$create_pasteboard(&pb, 0, &rows, &cols, 0, 0, 0) & 1) {
            smg$create_virtual_display(&rows, &cols, &vd, 0, 0, 0);
            smg$paste_virtual_display(&vd, &pb, 1, 1, 0);
        }
        oyk_sysv_tui_render(&cv, &app, (int)rows, (int)cols);
        {
            char *ansi = (char *)malloc(16384);
            if (ansi) {
                oyk_sysv_tui_to_ansi(&cv, 1, ansi, 16384);
                fputs(ansi, stdout);
                fflush(stdout);
                free(ansi);
            }
        }
        if (vd) {
            smg$delete_virtual_display(&vd);
        }
        if (pb) {
            smg$delete_pasteboard(&pb);
        }
    }
#else
    oyk_sysv_tui_render(&cv, &app, 24, 80);
    if (ansi_dump) {
        char *ansi = (char *)malloc(16384);
        if (ansi) {
            oyk_sysv_tui_to_ansi(&cv, 1, ansi, 16384);
            fputs(ansi, stdout);
            free(ansi);
        }
    } else {
        char call_buf[16];
        oyk_sysv_format_callsign(&app.node.cfg.callsign, call_buf, sizeof(call_buf));
        printf("OYK OpenVMS Console (%s): %s\n",
               OYK_VMS_ARCH_NAME, call_buf);
    }
#endif

    oyk_sysv_net_shutdown(&net);
    return 0;
}
