/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_PORT_PARITY_TESTS_HPP
#define OYK_PORT_PARITY_TESTS_HPP

/*
 * Shared strict C++98 cross-port unit-test parity suite.
 * Invoked by both emulator test runners (Classic Mac, Amiga, Atari ST) and
 * host CTest verification suites to ensure consistent core, protocol, GUI
 * schema, terminal hotkey/wrap, rig, and printer parity.
 *
 * Keeps stack frames <= 1,024 B (large structs allocated on heap).
 */

#include "oyk_portable.hpp"
#include "oyk_gui.hpp"
#include "oyk_term.hpp"
#include "oyk_rig.hpp"
#include "oyk_printer.hpp"
#include "oyk_yapp.hpp"

#include <stdio.h>
#include <string.h>

static inline void oyk_parity_record(FILE* logf, int* failures, int cond, const char* label) {
    if (!cond) {
        if (failures) ++(*failures);
        if (logf) fprintf(logf, "FAIL: %s\n", label);
    } else if (logf) {
        fprintf(logf, "PASS: %s\n", label);
    }
}

static inline int oyk_run_shared_port_parity_tests(OykPortableNode* node,
                                                   FILE* logf,
                                                   int* failures_out) {
    int fails = 0;

    /* 1. Callsign parsing + AX.25 frame codec + CRC-16 FCS */
    {
        OykCallsign dst, src;
        oyk_parity_record(logf, &fails,
                          oyk_parse_callsign("KR4OYK-7", &dst) == 1 &&
                          oyk_parse_callsign("W4ABC-1", &src) == 1,
                          "parity_parse_callsign");

        OykFrame* f = new OykFrame();
        OykFrame* df = new OykFrame();
        oyk_u8 raw[64];
        memset(f, 0, sizeof(*f));
        memset(df, 0, sizeof(*df));
        f->dst = dst;
        f->src = src;
        f->is_command = 1;
        f->control = 0x3FU; /* SABM */
        size_t enc_len = oyk_ax25_encode(f, raw, sizeof(raw));
        int dec_ok = (enc_len >= 15U && oyk_ax25_decode(raw, enc_len, df) == 1 &&
                      oyk_crc16_fcs(raw, enc_len) != 0U);
        oyk_parity_record(logf, &fails, dec_ok, "parity_ax25_crc_and_codec");
        delete f;
        delete df;
    }

    /* 2. KISS encoding & streaming decoder */
    {
        static const oyk_u8 payload[4] = { 0x82U, 0xC0U, 0xDBU, 0x41U };
        oyk_u8 kbuf[32];
        size_t klen = oyk_kiss_encode(0U, payload, sizeof(payload), kbuf, sizeof(kbuf));
        OykKissDecoder* dec = new OykKissDecoder();
        oyk_kiss_decoder_init(dec);
        oyk_u8 kport = 99U, kcmd = 99U;
        int got = 0;
        for (size_t i = 0; i < klen; ++i) {
            if (oyk_kiss_decoder_feed_byte(dec, kbuf[i], &kport, &kcmd) == 1) {
                got = 1;
            }
        }
        oyk_parity_record(logf, &fails,
                          got == 1 && dec->len == sizeof(payload) && kport == 0U && kcmd == 0U,
                          "parity_kiss_codec");
        delete dec;
    }

    /* 3. Terminal / config EOL normalization across CR, LF, CRLF */
    {
        OykTermEolParser eol;
        oyk_term_eol_init(&eol, OYK_TERM_EOL_CRLF, 1);
        int a1 = oyk_term_eol_feed_byte(&eol, '\r');
        int a2 = oyk_term_eol_feed_byte(&eol, '\n');
        int a3 = oyk_term_eol_feed_byte(&eol, 'A');
        oyk_parity_record(logf, &fails,
                          a1 == OYK_TERM_ACT_NEWLINE && a2 == OYK_TERM_ACT_IGNORE &&
                          a3 == OYK_TERM_ACT_CHAR &&
                          oyk_term_eol_effective_tx(&eol) == OYK_TERM_EOL_CRLF,
                          "parity_eol_crlf_norm");
    }

    /* 4. Node BBS + CHAT local session capability (when node provided) */
    if (node) {
        oyk_u32 sid = 0;
        OykCallsign rcall;
        int isloc = 0;
        char err[64];
        err[0] = '\0';
        int bbs_ok = node->open_local_terminal(0, "BBS", &sid, &rcall, &isloc, err, sizeof(err));
        if (sid != 0) node->close_local_terminal(sid);
        oyk_parity_record(logf, &fails, bbs_ok == 1 && sid != 0, "parity_bbs_local_session");
    }

    /* 5. Ban flags & appeal sanitization (<= 140 printable ASCII) */
    {
        oyk_u32 fl = 0;
        char long_in[180];
        memset(long_in, 'A', 170);
        long_in[0] = '\x07';
        long_in[1] = '\x1B';
        long_in[170] = '\0';
        char san[160];
        oyk_sanitize_appeal(long_in, san, sizeof(san));
        size_t slen = strlen(san);
        oyk_parity_record(logf, &fails,
                          oyk_parse_flags_u32("0b1001", &fl) == 1 && fl == 9UL &&
                          slen > 0U && slen <= 140U && san[0] == 'A',
                          "parity_ban_and_appeal_sanitize");
    }

    /* 6. YAPP filename path traversal guard */
    oyk_parity_record(logf, &fails,
                      oyk_yapp_is_safe_filename("../evil.txt") == 0 &&
                      oyk_yapp_is_safe_filename("a\\b.txt") == 0 &&
                      oyk_yapp_is_safe_filename("notes.txt") == 1,
                      "parity_yapp_traversal_guard");

    /* 7. Shared oyk_gui schemas, modal preemption, rich control metrics & 5-driver wizard */
    {
        int no_dups = 1;
        for (int mi = 0; mi < oyk_gui_node_menu_count(); ++mi) {
            const OykGuiMenuItem* m = oyk_gui_node_menu_item(mi);
            if (!m || m->action == OYK_ACT_PEERS || m->action == OYK_ACT_THEME) {
                no_dups = 0;
            }
        }
        int modal_preempt_ok = (oyk_gui_global_hotkey_allowed(OYK_SCR_MAIN, 0) == 1 &&
                                oyk_gui_global_hotkey_allowed(OYK_SCR_MAIN, 1) == 0 &&
                                oyk_gui_global_hotkey_allowed(OYK_SCR_SETTINGS, 0) == 0 &&
                                oyk_gui_global_hotkey_allowed(OYK_SCR_ADD_IFACE, 0) == 0 &&
                                oyk_gui_global_hotkey_allowed(OYK_SCR_EDIT_IFACE, 0) == 0 &&
                                oyk_gui_global_hotkey_allowed(OYK_SCR_EDIT_PEER, 0) == 0 &&
                                oyk_gui_global_hotkey_allowed(OYK_SCR_TERMINATE, 0) == 0 &&
                                oyk_gui_global_hotkey_allowed(OYK_SCR_BAN, 0) == 0 &&
                                oyk_gui_global_hotkey_allowed(OYK_SCR_EXIT_PEERS, 0) == 0 &&
                                oyk_gui_global_hotkey_allowed(OYK_SCR_FILES, 0) == 0 &&
                                oyk_gui_global_hotkey_allowed(OYK_SCR_RIG, 0) == 0 &&
                                oyk_gui_global_hotkey_allowed(OYK_SCR_ABOUT, 0) == 0 &&
                                oyk_gui_global_hotkey_allowed(OYK_SCR_TERM_HELP, 0) == 0) ? 1 : 0;
        oyk_parity_record(logf, &fails,
                          oyk_gui_settings_tab_count() == 8 &&
                          strcmp(oyk_gui_settings_tab_title(7), "Theme") == 0 &&
                          oyk_gui_node_menu_count() == 11 &&
                          no_dups == 1 &&
                          modal_preempt_ok == 1 &&
                          oyk_gui_control_box_size(12) >= 10 &&
                          oyk_gui_control_label_offset(12) > oyk_gui_control_box_size(12),
                          "parity_gui_schema_and_modal_preemption");
    }
    {
        OykGuiAddIfaceWizard* wiz = new OykGuiAddIfaceWizard();
        oyk_gui_wiz_init(wiz, NULL, OYK_DRV_ALL);
        int cnt = oyk_gui_wiz_driver_count(wiz->driver_mask);
        oyk_gui_wiz_select_driver(wiz, NULL, 2);
        oyk_parity_record(logf, &fails,
                          cnt == 5 && strcmp(wiz->draft.type, "kiss-serial") == 0 &&
                          oyk_gui_wiz_driver_desc(wiz->driver_mask, 0)[0] != '\0',
                          "parity_gui_wizard_5drivers");
        delete wiz;
    }

    /* 8. Local terminal Ctrl+A hotkeys & flush-left word-wrap */
    {
        OykTermWrapSpan spans[4];
        int sc = oyk_term_wrap_line("HELLO PACKET NODE CROSS PORT TEST", 33, 15, spans, 4);
        oyk_parity_record(logf, &fails,
                          oyk_term_decode_ctrl_a_cmd('?') == OYK_TCMD_HELP &&
                          oyk_term_help_count() >= 10 && sc >= 2 &&
                          strstr(oyk_term_hotkey_bar_text(1, 0), "Ctrl-A Z:Help") != NULL &&
                          strstr(oyk_term_hotkey_bar_text(1, 0), "Tab/F9") != NULL &&
                          strcmp(oyk_gui_screen_title(OYK_SCR_TERM_HELP), "Terminal Hotkey Help") == 0,
                          "parity_term_hotkeys_and_wrap");
    }

    /* 9. Jailed file manager subdir guard (blocks '..' at root) */
    {
        char absd[64], nrel[32], nerr[64];
        nrel[0] = 'x';
        oyk_parity_record(logf, &fails,
                          oyk_bbs_enter_subdir("files", "", "..", nrel, sizeof(nrel), nerr, sizeof(nerr)) == 0 &&
                          oyk_bbs_resolve_safe_path("files", "sub", "..", 1,
                                                    absd, sizeof(absd),
                                                    nrel, sizeof(nrel),
                                                    nerr, sizeof(nerr)) == 1 &&
                          nrel[0] == '\0',
                          "parity_buttons_and_file_jail");
    }

    /* 10. Rig CAT command builder & printer line formatter */
    {
        char rbuf[32];
        unsigned char pbuf[64];
        int rlen = oyk_rigctl_build_set_freq(145050000UL, rbuf, sizeof(rbuf));
        int plen = oyk_printer_format_line(OYK_PRN_GENERIC, 0x04UL, "PARITY", pbuf, sizeof(pbuf));
        oyk_parity_record(logf, &fails, rlen > 0 && plen > 0, "parity_rig_and_printer");
    }

    if (failures_out) *failures_out += fails;
    return (fails == 0) ? 1 : 0;
}

#endif /* OYK_PORT_PARITY_TESTS_HPP */
