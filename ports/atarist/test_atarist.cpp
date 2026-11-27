/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * Native Atari ST / TT m68k Unit Test Runner (OYKTEST.PRG)
 *
 * Boots under Hatari + EmuTOS, initializes GEM AES/VDI, verifies Atari HW/cookie
 * detection, VDI palette query, whole-screen live theme preview across mono & color,
 * oyk_portable, oyk_gui, oyk_term, oyk_rig, and oyk_printer, writing
 * C:\TESTLOG.TXT and C:\DONE.TXT.
 */

#include "oyk_portable.hpp"
#include "oyk_gui.hpp"
#include "oyk_term.hpp"
#include "oyk_rig.hpp"
#include "oyk_printer.hpp"
#include "atari_policy.hpp"
#include "atari_net.hpp"
#include "atari_ui.hpp"
#include "atari_capture.hpp"

#include <stdio.h>
#include <string.h>

#ifdef __ATARI__
#include <gem.h>
#endif

static int g_failures = 0;

#define AT_CHECK(cond, label) \
    do { \
        if (!(cond)) { \
            ++g_failures; \
            if (logf) fprintf(logf, "FAIL: %s (line %d)\n", (label), __LINE__); \
        } else { \
            if (logf) fprintf(logf, "PASS: %s\n", (label)); \
        } \
    } while (0)

int main() {
    short ap_id = -1;
    short vdi = 0;
    int scr_w = 640, scr_h = 400, colors = 2;

#ifdef __ATARI__
    ap_id = appl_init();
    short wchar = 8, hchar = 16, wbox = 16, hbox = 16;
    short work_in[11] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2};
    short work_out[57];
    memset(work_out, 0, sizeof(work_out));
    vdi = graf_handle(&wchar, &hchar, &wbox, &hbox);
    v_opnvwk(work_in, &vdi, work_out);
    if (vdi > 0) {
        scr_w = work_out[0] + 1;
        scr_h = work_out[1] + 1;
        colors = work_out[13];
    }
#endif

    FILE* logf = fopen("C:\\TESTLOG.TXT", "w");
    if (!logf) logf = fopen("TESTLOG.TXT", "w");

    /* 1. Hardware & Cookie detection */
    OykAtariNetCaps caps;
    oyk_atari_detect_caps(&caps);
    AT_CHECK(caps.has_rs232 == 1, "atari_rs232_present");
    AT_CHECK(vdi > 0, "atari_vdi_opened");
    if (logf) {
        fprintf(logf, "INFO: MCH=0x%08lX Mode=%dx%dx%d Backend=%s\n",
                static_cast<unsigned long>(caps.machine_cookie),
                scr_w, scr_h, colors, caps.backend_summary);
    }

    /* 2. Node lifecycle + OS-native theme + live whole-screen preview */
    OykPortableNode* node = new OykPortableNode();
    OykConfig* cfg = new OykConfig();
    AT_CHECK(node != NULL && cfg != NULL, "alloc_portable_node");
    if (node && cfg) {
        oyk_config_init_default(cfg);
        char err[96];
        err[0] = '\0';
        AT_CHECK(node->init(cfg, NULL, NULL, err, sizeof(err)) == 1, "node_init");
        AT_CHECK(node->accept_incoming() == 1, "default_accept_incoming");

        OykAtariNetState net;
        oyk_atari_net_init(&net, node);

        OykThemeConfig nat;
        oyk_atari_query_os_native_theme(vdi, colors, &nat);
        AT_CHECK(strcmp(nat.name, "native") == 0, "atari_native_theme_name");

        int bg_ch = 0, fg_ch = 0;
        AT_CHECK(oyk_atari_ui_run_theme_preview_self_test(node, &net, vdi, colors, &bg_ch, &fg_ch) == 1 &&
                 bg_ch >= 4 && fg_ch >= 4, "atari_theme_full_screen_preview");
        if (logf) {
            fprintf(logf, "INFO: ThemePreview bg_changes=%d fg_changes=%d\n", bg_ch, fg_ch);
        }

        oyk_atari_net_shutdown(&net);
        node->shutdown();
    }
    if (cfg) delete cfg;
    if (node) delete node;

    /* 3. Shared GUI / Term / Rig / Printer */
    AT_CHECK(oyk_gui_settings_tab_count() == 7, "gui_tabs_7");
    AT_CHECK(oyk_gui_node_menu_count() == 13, "gui_menu_13");
    AT_CHECK(oyk_term_decode_ctrl_a_cmd('?') == OYK_TCMD_HELP, "term_ctrl_a_help");
    OykTermWrapSpan spans[4];
    AT_CHECK(oyk_term_wrap_line("HELLO ATARI ST GEM DESKTOP", 25, 14, spans, 4) >= 2, "term_word_wrap");
    char rbuf[32];
    AT_CHECK(oyk_rigctl_build_set_freq(145050000UL, rbuf, sizeof(rbuf)) > 0, "rig_set_freq");
    unsigned char pbuf[64];
    AT_CHECK(oyk_printer_format_line(OYK_PRN_GENERIC, 0x04UL, "ATARI TEST", pbuf, sizeof(pbuf)) > 0,
             "printer_format");

    if (logf) {
        fprintf(logf, "%s\n", g_failures == 0 ? "ALL_TESTS_PASSED" : "TESTS_FAILED");
        fclose(logf);
    }
    oyk_atari_finish_test();
#ifdef __ATARI__
    if (vdi > 0) v_clsvwk(vdi);
    if (ap_id >= 0) appl_exit();
#endif
    return g_failures ? 1 : 0;
}
