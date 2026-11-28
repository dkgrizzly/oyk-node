// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "os2_net.hpp"
#include "oyk_gui.hpp"
#include "oyk_port_parity_tests.hpp"
#include "oyk_portable.hpp"
#include "oyk_tui.hpp"

#include <cstdio>
#include <cstring>

int main() {
    int failures = 0;

    /* 1. Shared cross-port parity suite */
    {
        OykPortableNode* pnode = new OykPortableNode();
        OykConfig* pcfg = new OykConfig();
        oyk_config_init_default(pcfg);
        char perr[64];
        if (pnode->init(pcfg, NULL, NULL, perr, sizeof(perr)) != 1) {
            std::printf("FAIL: OS/2 OykPortableNode::init (%s)\n", perr);
            ++failures;
        } else {
            oyk_run_shared_port_parity_tests(pnode, stdout, &failures);
            pnode->shutdown();
        }
        delete pcfg;
        delete pnode;
    }

    /* 2. OS/2 SO32DLL dynamic mock + COM1..COM4 validation */
    {
        if (!oyk_os2_is_valid_com_port("COM1") || !oyk_os2_is_valid_com_port("com4") ||
            oyk_os2_is_valid_com_port("COM0") || oyk_os2_is_valid_com_port("/dev/ttyS0")) {
            std::printf("FAIL: OS/2 COM port validation\n");
            ++failures;
        }

        OykOs2NetState net;
        oyk_os2_net_init(&net);
        if (oyk_os2_net_open_udp(&net, 10093) != 0) {
            std::printf("FAIL: OS/2 UDP open\n");
            ++failures;
        }
        if (oyk_os2_net_open_serial(&net, "COM2", 9600) != 0) {
            std::printf("FAIL: OS/2 COM2 open\n");
            ++failures;
        }
        const oyk_u8 pkt[4] = { 'O', 'Y', 'K', '2' };
        oyk_u8 rx[8] = { 0 };
        if (oyk_os2_net_send_udp(&net, "127.0.0.1", 10093, pkt, sizeof(pkt)) != 4 ||
            oyk_os2_net_poll_udp(&net, rx, sizeof(rx)) != 4 ||
            std::memcmp(pkt, rx, sizeof(pkt)) != 0) {
            std::printf("FAIL: OS/2 UDP loopback\n");
            ++failures;
        }
        oyk_os2_net_shutdown(&net);
    }

    /* 3. Verify shared TUI/GUI 23-screen, 8-tab schema, and modal preemption */
    {
        if (oyk_gui_settings_tab_count() != 8 ||
            oyk_gui_node_menu_count() != 11 ||
            oyk_gui_is_modal_screen(OYK_SCR_SETTINGS) != 1 ||
            oyk_gui_global_hotkey_allowed(OYK_SCR_SETTINGS, 0) != 0 ||
            oyk_gui_global_hotkey_allowed(OYK_SCR_MAIN, 0) != 1 ||
            std::strcmp(oyk_gui_screen_title(OYK_SCR_EXIT_PEERS), "Auto-Learned Peers") != 0 ||
            std::strcmp(oyk_gui_screen_title(OYK_SCR_TERM_HELP), "Terminal Hotkey Help") != 0) {
            std::printf("FAIL: OS/2 shared GUI schema/preemption check\n");
            ++failures;
        }
    }

    if (failures == 0) {
        std::printf("All OS/2 2.0+ parity tests PASSED.\n");
    }
    return failures == 0 ? 0 : 1;
}
