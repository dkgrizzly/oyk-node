/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * Native 68k Classic Macintosh Unit Test Runner (OYKTEST)
 *
 * Boots directly under Mini vMac System Software 6.0.8, exercises the shared
 * C++98 OYK Node core, GUI middleware (oyk_gui), Terminal engine (oyk_term),
 * Rig, Printer, YAPP, and Macintosh hardware detection (mac_net), writes
 * TESTLOG.TXT on the HFS volume, and powers off cleanly via ShutDwnPower().
 */

#include "oyk_portable.hpp"
#include "oyk_gui.hpp"
#include "oyk_term.hpp"
#include "oyk_rig.hpp"
#include "oyk_printer.hpp"
#include "oyk_yapp.hpp"
#include "mac_net.hpp"
#include "mac_ui.hpp"
#include "mac_capture.hpp"

#include <stdio.h>
#include <string.h>

#include <MacTypes.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Memory.h>

static int g_failures = 0;

#define MC_CHECK(cond, label) \
    do { \
        if (!(cond)) { \
            ++g_failures; \
            if (logf) fprintf(logf, "FAIL: %s (line %d)\n", (label), __LINE__); \
        } else { \
            if (logf) fprintf(logf, "PASS: %s\n", (label)); \
        } \
    } while (0)

int main() {
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    MaxApplZone();
    MoreMasters();
    oyk_mac_mount_all_drives(480UL);

    FILE* logf = fopen("OYKOUT:TESTLOG.TXT", "w");
    if (!logf) logf = fopen("TESTLOG.TXT", "w");

    /* 1. Verify Gestalt / Serial / MacTCP / QuickDraw runtime detection */
    OykMacNetCaps* caps = new OykMacNetCaps();
    MC_CHECK(caps != NULL, "alloc_caps");
    if (caps) {
        oyk_mac_detect_caps(caps);
        MC_CHECK(caps->has_serial == 1, "mac_serial_present");
        MC_CHECK(caps->sys_version_bcd >= 0x0600, "mac_sys_version_bcd");
        if (logf) {
            fprintf(logf, "INFO: System=0x%04X ColorQD=%d MacTCP=%d OT=%d Backend=%s\n",
                    caps->sys_version_bcd, caps->has_color_qd,
                    caps->has_mactcp, caps->has_open_transport,
                    caps->backend_summary);
        }
        delete caps;
    }

    /* 2. Verify OykPortableNode lifecycle + AX.25 callsign + config defaults */
    OykPortableNode* node = new OykPortableNode();
    OykConfig* cfg = new OykConfig();
    MC_CHECK(node != NULL && cfg != NULL, "alloc_portable_node");
    if (node && cfg) {
        oyk_config_init_default(cfg);
        char err[96];
        err[0] = '\0';
        MC_CHECK(node->init(cfg, NULL, NULL, err, sizeof(err)) == 1, "node_init");
        MC_CHECK(node->config().accept_incoming == 1, "default_accept_incoming");
        OykCallsign cs;
        MC_CHECK(oyk_parse_callsign("KR4OYK-7", &cs) == 1, "parse_callsign");

        /* 2b. Verify OS-sourced Classic Mac native theme & full-screen live preview */
        OykThemeConfig bw_pal;
        oyk_mac_query_os_native_theme(NULL, 0, &bw_pal);
        MC_CHECK(strcmp(bw_pal.name, "native") == 0, "mac_bw_native_name");
        MC_CHECK(bw_pal.is_dark_base == 0, "mac_bw_native_light_base");
        MC_CHECK(bw_pal.colors[OYK_TR_BG] == 0xFFFFFFUL && bw_pal.colors[OYK_TR_FG] == 0x000000UL,
                 "mac_bw_native_1bit_colors");

        OykThemeConfig clr_pal;
        oyk_mac_query_os_native_theme(NULL, 1, &clr_pal);
        MC_CHECK(strcmp(clr_pal.name, "native") == 0, "mac_color_native_name");
        MC_CHECK(clr_pal.is_dark_base == 0, "mac_color_native_light_base");

        OykMacNetState mnet;
        memset(&mnet, 0, sizeof(mnet));
        oyk_mac_detect_caps(&mnet.caps);
        int bg_changes = 0;
        int fg_changes = 0;
        int prev_ok = oyk_mac_ui_run_theme_preview_self_test(node, &mnet, &bg_changes, &fg_changes);
        MC_CHECK(prev_ok == 1, "mac_theme_full_screen_preview");
        if (logf) {
            fprintf(logf, "INFO: ThemePreview bg_changes=%d fg_changes=%d\n", bg_changes, fg_changes);
        }

        node->shutdown();
    }
    if (cfg) delete cfg;
    if (node) delete node;

    /* 3. Verify shared oyk_gui schemas, wizard, theme controller */
    MC_CHECK(oyk_gui_settings_tab_count() == 7, "gui_tabs_7");
    MC_CHECK(oyk_gui_node_menu_count() == 13, "gui_menu_13");
    MC_CHECK(strcmp(oyk_gui_table_column(OYK_TBL_TERMINATE, 0), "ID") == 0 &&
             strcmp(oyk_gui_table_column(OYK_TBL_TERMINATE, 1), "Callsign") == 0,
             "gui_terminate_cols");

    OykGuiAddIfaceWizard* wiz = new OykGuiAddIfaceWizard();
    MC_CHECK(wiz != NULL, "alloc_wizard");
    if (wiz) {
        oyk_gui_wiz_init(wiz, NULL, OYK_DRV_ALL);
        MC_CHECK(oyk_gui_wiz_driver_count(wiz->driver_mask) == 5, "gui_wiz_5_drivers");
        oyk_gui_wiz_select_driver(wiz, NULL, 2);
        MC_CHECK(strcmp(wiz->draft.type, "kiss-serial") == 0, "gui_wiz_kiss_serial");
        delete wiz;
    }

    /* 4. Verify oyk_term Ctrl+A hotkeys & word-wrap */
    MC_CHECK(oyk_term_decode_ctrl_a_cmd('?') == OYK_TCMD_HELP, "term_ctrl_a_help");
    OykTermWrapSpan spans[4];
    int sc = oyk_term_wrap_line("HELLO CLASSIC MACINTOSH WORLD", 29, 15, spans, 4);
    MC_CHECK(sc >= 2, "term_word_wrap");

    /* 5. Verify oyk_rig & oyk_printer */
    char rbuf[32];
    MC_CHECK(oyk_rigctl_build_set_freq(145050000UL, rbuf, sizeof(rbuf)) > 0, "rig_set_freq");
    unsigned char pbuf[64];
    MC_CHECK(oyk_printer_format_line(OYK_PRN_GENERIC, 0x04UL, "MAC TEST", pbuf, sizeof(pbuf)) > 0,
             "printer_format_line");

    if (logf) {
        fprintf(logf, "%s\n", (g_failures == 0) ? "ALL_TESTS_PASSED" : "TESTS_FAILED");
        fclose(logf);
    }

    oyk_mac_finish_test_and_shutdown(0);
    return (g_failures == 0) ? 0 : 1;
}
