/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * Native 65C816 GS/OS Unit Test Runner (OYKTEST.SYS16)
 *
 * Boots under GSplus + GS/OS, verifies gsos_core, gsos_net, QuickDraw II
 * native OS theme & whole-screen preview, config EOLs, Ctrl+A hotkeys, and
 * Rig/Printer helpers, then writes /OYKOUT/TESTLOG.TXT + /OYKOUT/DONE.TXT.
 */

#include "gsos_core.h"
#include "gsos_net.h"
#include "gsos_ui.h"
#include "gsos_capture.h"

#include <stdio.h>
#include <string.h>

static char            g_log_buf[4096];
static size_t          g_log_len = 0U;
static int             g_failures = 0;
static OykGsosNode     g_test_node;
static OykGsosNetState g_test_net;
static OykGsosConfig   g_test_cfg;

static void gs_log_line(const char *line) {
    size_t n = strlen(line);
    if (g_log_len + n + 2U < sizeof(g_log_buf)) {
        memcpy(g_log_buf + g_log_len, line, n);
        g_log_len += n;
        g_log_buf[g_log_len++] = '\n';
        g_log_buf[g_log_len] = '\0';
    }
}

#define GS_CHECK(cond, label) \
    do { \
        char _lbuf[96]; \
        if (!(cond)) { \
            ++g_failures; \
            sprintf(_lbuf, "FAIL: %s (line %d)", (label), __LINE__); \
        } else { \
            sprintf(_lbuf, "PASS: %s", (label)); \
        } \
        gs_log_line(_lbuf); \
    } while (0)

int main(void) {
    OykGsosCallsign cs, cs_bad;
    OykGsosThemeConfig nat;
    OykGsosThemeCtrl tctrl;
    OykGsosAddIfaceWiz wiz;
    OykGsosTermSpan spans[4];
    OykGsosTermEolParser eolp;
    char out_eol[64], rig_buf[32], info_buf[128], hdr_buf[128], cfg_ser[512];
    unsigned char prn_buf[64];
    int bg_ch = 0, fg_ch = 0, sc, en, di;
    size_t ser_n;
    const char *cr_sample;

    /* 1. Network / Serial runtime detection */
    oyk_gsos_detect_caps(&g_test_net.caps);
    GS_CHECK(g_test_net.caps.has_scc_serial == 1, "gsos_scc_serial_present");
    GS_CHECK(g_test_net.caps.has_shr_640 == 1, "gsos_shr_640_present");
    sprintf(info_buf, "INFO: GSOS=0x%04X Marinetti=%d Backend=%s",
            (unsigned int)g_test_net.caps.gsos_version,
            g_test_net.caps.has_marinetti,
            g_test_net.caps.backend_summary);
    gs_log_line(info_buf);

    /* 2. Node lifecycle & config defaults */
    oyk_gsos_config_init_default(&g_test_cfg);
    oyk_gsos_node_init(&g_test_node, &g_test_cfg);
    GS_CHECK(g_test_node.cfg.accept_incoming == 1, "default_accept_incoming");
    GS_CHECK(oyk_gsos_parse_callsign("KR4OYK-7", &cs) == 1 && cs.ssid == 7, "parse_callsign");
    GS_CHECK(oyk_gsos_parse_callsign("TOO_LONG-99", &cs_bad) == 0 &&
             oyk_gsos_parse_callsign("", &cs_bad) == 0, "parity_parse_callsign_invalid");

    cr_sample = "[node]\rcallsign = W4ABC-7\raccept_incoming = false\rtheme = dark\r";
    GS_CHECK(oyk_gsos_config_parse(cr_sample, strlen(cr_sample), &g_test_cfg) == 1 &&
             g_test_cfg.line_ending == OYK_GSOS_EOL_CR && g_test_cfg.accept_incoming == 0,
             "parity_config_eol_parse");
    ser_n = oyk_gsos_config_serialize(&g_test_cfg, cfg_ser, sizeof(cfg_ser));
    GS_CHECK(ser_n > 0U && strchr(cfg_ser, '\r') != NULL && strchr(cfg_ser, '\n') == NULL,
             "parity_config_eol_preserve");

    oyk_gsos_node_seed_demo(&g_test_node);
    GS_CHECK(oyk_gsos_node_auto_peer_count(&g_test_node) == 1, "parity_auto_peer_count");
    GS_CHECK(oyk_gsos_node_ban_callsign(&g_test_node, "K1XYZ", "Test ban") == 1,
             "parity_ban_callsign");
    GS_CHECK(oyk_gsos_node_terminate_session(&g_test_node, 1UL) == 1,
             "parity_terminate_session");
    oyk_gsos_config_init_default(&g_test_cfg);
    oyk_gsos_node_init(&g_test_node, &g_test_cfg);

    /* 3. OS-sourced native theme & whole-screen live preview */
    oyk_gsos_query_os_native_theme(&nat);
    GS_CHECK(strcmp(nat.name, "native") == 0, "gsos_native_theme_name");
    GS_CHECK(nat.is_dark_base == 0, "gsos_native_light_base");
    GS_CHECK(oyk_gsos_ui_run_theme_preview_self_test(&g_test_node, &g_test_net,
                                                     &bg_ch, &fg_ch) == 1,
             "gsos_theme_full_screen_preview");
    sprintf(info_buf, "INFO: ThemePreview bg_changes=%d fg_changes=%d", bg_ch, fg_ch);
    gs_log_line(info_buf);

    /* 4. Canonical UI schema parity */
    GS_CHECK(oyk_gsos_settings_tab_count() == 7, "gui_tabs_7");
    GS_CHECK(oyk_gsos_node_menu_count() == 13, "gui_menu_13");
    GS_CHECK(strcmp(oyk_gsos_table_column(OYK_GSOS_TBL_TERMINATE, 0), "ID") == 0 &&
             strcmp(oyk_gsos_table_column(OYK_GSOS_TBL_TERMINATE, 1), "Callsign") == 0,
             "gui_terminate_cols");
    oyk_gsos_wiz_init(&wiz, &g_test_cfg);
    GS_CHECK(oyk_gsos_wiz_driver_count() == 5, "gui_wiz_5_drivers");
    for (di = 0; di < 5; ++di) {
        oyk_gsos_wiz_select_driver(&wiz, &g_test_cfg, di);
    }
    GS_CHECK(strcmp(oyk_gsos_wiz_driver_name(2), "kiss-serial") == 0 &&
             oyk_gsos_wiz_driver_desc(2)[0] != '\0', "parity_wiz_5_drivers_and_descs");
    oyk_gsos_format_console_header("KR4OYK-7", "OYKNOD", 1, hdr_buf, sizeof(hdr_buf));
    GS_CHECK(strstr(hdr_buf, "OYK PACKET NODE:") != NULL &&
             strcmp(oyk_gsos_format_incoming_pill(1), "[INCOMING: ALLOW]") == 0 &&
             strcmp(oyk_gsos_format_incoming_pill(0), "[INCOMING: DENY]") == 0,
             "parity_console_header_and_pill");
    oyk_gsos_theme_init(&tctrl, &g_test_cfg, 1, 0);
    oyk_gsos_theme_cycle(&tctrl, 1);
    GS_CHECK(strcmp(tctrl.active_name, "native") != 0, "parity_theme_cycle");
    oyk_gsos_theme_revert(&tctrl);
    GS_CHECK(strcmp(tctrl.active_name, "native") == 0, "parity_theme_revert");
    oyk_gsos_wiz_select_driver(&wiz, &g_test_cfg, 2);
    GS_CHECK(strcmp(wiz.draft.type, "kiss-serial") == 0, "gui_wiz_kiss_serial");
    GS_CHECK(oyk_gsos_wiz_advance(&wiz, &g_test_cfg) == 1 && wiz.step == 1 &&
             oyk_gsos_wiz_advance(&wiz, &g_test_cfg) == 1 && wiz.step == 2 &&
             oyk_gsos_wiz_advance(&wiz, &g_test_cfg) == 1 && wiz.step == 3,
             "gui_wiz_4_steps");
    GS_CHECK(strcmp(oyk_gsos_format_learned_policy(0), "inherit") == 0 &&
             oyk_gsos_cycle_learned_policy(0, 1) == 1, "gui_learned_policy_cycle");

    /* 5. Config EOL detection + Ctrl+A hotkeys + word-wrap */
    GS_CHECK(oyk_gsos_detect_text_eol("a\rb\r", 4U) == OYK_GSOS_EOL_CR, "eol_detect_cr");
    GS_CHECK(oyk_gsos_detect_text_eol("a\r\nb\r\n", 6U) == OYK_GSOS_EOL_CRLF, "eol_detect_crlf");
    memset(&eolp, 0, sizeof(eolp));
    en = oyk_gsos_term_eol_feed(&eolp, "HI\r\n", 4, out_eol, sizeof(out_eol));
    GS_CHECK(en == 3 && eolp.detected == OYK_GSOS_EOL_CRLF, "term_eol_crlf");
    GS_CHECK(oyk_gsos_term_decode_ctrl_a('?') == OYK_GSOS_TCMD_HELP, "term_ctrl_a_help");
    sc = oyk_gsos_term_wrap_line("HELLO APPLE IIGS GSOS WORLD", 27, 15, spans, 4);
    GS_CHECK(sc >= 2, "term_word_wrap");

    /* 6. Rig & Printer */
    GS_CHECK(oyk_gsos_rigctl_build_set_freq(145050000UL, rig_buf, sizeof(rig_buf)) > 0,
             "rig_set_freq");
    GS_CHECK(oyk_gsos_printer_format_line("generic", "GSOS TEST", prn_buf, sizeof(prn_buf)) > 0,
             "printer_format");

    gs_log_line(g_failures == 0 ? "ALL_TESTS_PASSED" : "TESTS_FAILED");
    oyk_gsos_write_text_file("/OYKOUT/TESTLOG.TXT", g_log_buf, (unsigned long)g_log_len);
    oyk_gsos_finish_test();
    for (;;) {}
    return g_failures ? 1 : 0;
}
