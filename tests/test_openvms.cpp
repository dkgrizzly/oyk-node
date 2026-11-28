// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk_port_parity_tests.hpp"
#include "oyk_portable.hpp"
#include "sysv_core.h"
#include "sysv_tui.h"
#include "vms_compat.h"

#include <cstdio>
#include <cstring>

int main() {
    int failures = 0;

    {
        OykPortableNode *pnode = new OykPortableNode();
        OykConfig *pcfg = new OykConfig();
        oyk_config_init_default(pcfg);
        char perr[64];
        if (pnode->init(pcfg, NULL, NULL, perr, sizeof(perr)) != 1) {
            std::printf("FAIL: OykPortableNode::init (%s)\n", perr);
            ++failures;
        } else {
            oyk_run_shared_port_parity_tests(pnode, stdout, &failures);
            pnode->shutdown();
        }
        delete pcfg;
        delete pnode;
    }

    {
        OykSysvApp app;
        OykSysvCanvas cv;
        oyk_sysv_app_init(&app, OYK_VMS_ARCH_NAME, 1);

        static const char *const kScreens[23] = {
            "main", "menu", "settings", "settings_bbs", "settings_chat",
            "settings_ifaces", "peers", "settings_rig", "settings_printer",
            "add_iface_1", "add_iface_2", "add_iface_3", "add_iface_4",
            "edit_iface", "edit_peer", "terminate", "ban", "exit",
            "theme", "terminal", "terminal_help", "files", "about"
        };
        for (int i = 0; i < 23; ++i) {
            if (!oyk_sysv_tui_setup_screen(&app, kScreens[i])) {
                std::printf("FAIL: OpenVMS screen setup %s\n", kScreens[i]);
                ++failures;
            }
            oyk_sysv_tui_render(&cv, &app, 24, 80);
            if (i == 22 && !oyk_sysv_canvas_find_text(&cv, "O Y K")) {
                std::printf("FAIL: OpenVMS about screen missing 15x10 logo\n");
                ++failures;
            }
        }
    }

    if (failures == 0) {
        std::printf("All OpenVMS parity tests PASSED.\n");
    }
    return failures == 0 ? 0 : 1;
}
