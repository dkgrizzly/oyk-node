/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * Native Amiga m68k Unit Test Runner (OYKTest)
 *
 * Boots under FS-UAE + AROS m68k, verifies OykPortableNode, Kickstart tier
 * detection, Private Screen default OFF, <32-color OS-native theme lock,
 * >=32-color (depth=5/6) whole-screen live preview, oyk_gui, oyk_term, Rig,
 * and Printer helpers, writing DH0:TESTLOG.TXT + DH0:DONE.TXT.
 */

#include "oyk_portable.hpp"
#include "oyk_gui.hpp"
#include "oyk_term.hpp"
#include "oyk_port_parity_tests.hpp"
#include "oyk_rig.hpp"
#include "oyk_printer.hpp"
#include "amiga_policy.hpp"
#include "amiga_net.hpp"
#include "amiga_ui.hpp"
#include "amiga_capture.hpp"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define AM_CHECK(cond, label) \
    do { \
        if (!(cond)) { \
            ++g_failures; \
            if (logf) fprintf(logf, "FAIL: %s (line %d)\n", (label), __LINE__); \
        } else { \
            if (logf) fprintf(logf, "PASS: %s\n", (label)); \
        } \
    } while (0)

int main() {
    FILE* logf = fopen("DH0:TESTLOG.TXT", "w");
    if (!logf) logf = fopen("TESTLOG.TXT", "w");

    /* 1. Hardware & Kickstart detection */
    OykAmigaNetCaps caps;
    oyk_amiga_detect_caps(&caps);
    AM_CHECK(caps.has_serial_device == 1, "amiga_serial_device");
    AM_CHECK(caps.exec_version >= 33, "amiga_exec_version");
    AM_CHECK(oyk_amiga_classify_kickstart(34) == OYK_AMIGA_KS_13 &&
             oyk_amiga_has_gadtools(34) == 0, "amiga_ks13_fallback_tier");
    AM_CHECK(oyk_amiga_classify_kickstart(37) == OYK_AMIGA_KS_204 &&
             oyk_amiga_has_gadtools(37) == 1 &&
             oyk_amiga_has_setrgb32(37) == 0, "amiga_ks204_tier");
    AM_CHECK(oyk_amiga_classify_kickstart(40) == OYK_AMIGA_KS_31 &&
             oyk_amiga_has_setrgb32(40) == 1, "amiga_ks31_tier");
    if (logf) {
        fprintf(logf, "INFO: Exec=%d WB=%dx%dx%d (%d colors) Backend=%s\n",
                caps.exec_version, caps.wb_width, caps.wb_height,
                caps.wb_depth, caps.wb_colors, caps.backend_summary);
    }

    /* 2. OykPortableNode lifecycle + Private Screen default OFF */
    OykPortableNode* node = new OykPortableNode();
    OykConfig* cfg = new OykConfig();
    AM_CHECK(node != NULL && cfg != NULL, "alloc_portable_node");
    if (node && cfg) {
        oyk_config_init_default(cfg);
        AM_CHECK(cfg->amiga_private_screen == 0, "amiga_private_screen_default_off");
        AM_CHECK(oyk_amiga_should_use_private_screen(cfg, -1, -1) == 0, "amiga_workbench_default");

        char err[96];
        err[0] = '\0';
        AM_CHECK(node->init(cfg, NULL, NULL, err, sizeof(err)) == 1, "node_init");
        AM_CHECK(node->accept_incoming() == 1, "default_accept_incoming");

        OykAmigaNetState net;
        oyk_amiga_net_init(&net, node);

        /* 3a. Low-color Workbench (<32 colors) MUST lock to OS native pens */
        int locked_low = 0;
        AM_CHECK(oyk_amiga_ui_run_theme_preview_self_test(node, &net, 2, &locked_low, NULL, NULL) == 1 &&
                 locked_low == 1, "amiga_low_color_native_lock");

        /* 3b. 32/64+ color Private Screen (depth=5) unlocks custom OYK themes & whole-screen preview */
        int locked_hi = 1, bg_ch = 0, fg_ch = 0;
        AM_CHECK(oyk_amiga_ui_run_theme_preview_self_test(node, &net, 5, &locked_hi, &bg_ch, &fg_ch) == 1 &&
                 locked_hi == 0 && bg_ch >= 4 && fg_ch >= 4, "amiga_32color_theme_preview");
        if (logf) {
            fprintf(logf, "INFO: ThemePreview32 locked=%d bg_changes=%d fg_changes=%d\n",
                    locked_hi, bg_ch, fg_ch);
        }

        oyk_run_shared_port_parity_tests(node, logf, &g_failures);
    oyk_amiga_net_shutdown(&net);
        node->shutdown();
    }
    if (cfg) delete cfg;
    if (node) delete node;

    /* 4. Shared GUI / Term / Rig / Printer */
    AM_CHECK(oyk_gui_settings_tab_count() == 7, "gui_tabs_7");
    AM_CHECK(oyk_gui_node_menu_count() == 13, "gui_menu_13");
    AM_CHECK(oyk_term_decode_ctrl_a_cmd('?') == OYK_TCMD_HELP, "term_ctrl_a_help");
    OykTermWrapSpan spans[4];
    AM_CHECK(oyk_term_wrap_line("HELLO AMIGA WORKBENCH WORLD", 27, 15, spans, 4) >= 2, "term_word_wrap");
    char rbuf[32];
    AM_CHECK(oyk_rigctl_build_set_freq(145050000UL, rbuf, sizeof(rbuf)) > 0, "rig_set_freq");
    unsigned char pbuf[64];
    AM_CHECK(oyk_printer_format_line(OYK_PRN_GENERIC, 0x04UL, "AMIGA TEST", pbuf, sizeof(pbuf)) > 0,
             "printer_format");

    if (logf) {
        fprintf(logf, "%s\n", g_failures == 0 ? "ALL_TESTS_PASSED" : "TESTS_FAILED");
        fclose(logf);
    }
    oyk_amiga_finish_test();
    return g_failures ? 1 : 0;
}
