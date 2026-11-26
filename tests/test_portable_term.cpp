/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/* Strict C++98 tests for oyk_tui layout, oyk_term EOL/word-wrap, and DOS mouse simulation. */
#include "oyk_portable.hpp"
#include "oyk_tui.hpp"
#include "oyk_term.hpp"
#include "../ports/dos/dos_mouse.hpp"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct TermTxLog {
    int count;
    OykFrame frames[16];
};

static int term_mock_tx(int /*iface_idx*/, const OykFrame* frame,
                        oyk_u32 /*ip*/, oyk_u16 /*port*/, oyk_u16 /*handle*/, void* ctx) {
    TermTxLog* log = static_cast<TermTxLog*>(ctx);
    if (log->count < 16) {
        log->frames[log->count++] = *frame;
    }
    return 1;
}

static int term_txlog_contains(const TermTxLog* log, const char* needle) {
    for (int i = 0; i < log->count; ++i) {
        if (oyk_frame_type(&log->frames[i]) == OYK_FT_I && log->frames[i].info_len > 0) {
            char tmp[280];
            size_t n = log->frames[i].info_len;
            if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
            memcpy(tmp, log->frames[i].info, n);
            tmp[n] = '\0';
            if (strstr(tmp, needle) != NULL) return 1;
        }
    }
    return 0;
}

static void test_oyk_tui_layout_and_close_hit() {
    OykTuiConsoleLayout lay;
    const int sizes[5][2] = { {25, 80}, {30, 80}, {37, 100}, {48, 128}, {50, 132} };
    for (int i = 0; i < 5; ++i) {
        int r = sizes[i][0], c = sizes[i][1];
        oyk_tui_compute_console_layout(r, c, &lay);
        assert(lay.too_small == 0);
        assert(lay.status_row == r - 1);
        assert(lay.iface_box.top == 2 && lay.heard_box.top == 2);
        assert(lay.iface_box.left == 0);
        assert(lay.heard_box.left + lay.heard_box.w == c);
        assert(lay.conn_box.top == lay.iface_box.top + lay.iface_box.h);
        assert(lay.log_box.top == lay.conn_box.top + lay.conn_box.h);
        /* Full vertical display fill: bottom of Activity Log meets status row - 1 */
        assert(lay.log_box.top + lay.log_box.h == lay.status_row);
    }

    OykTuiTermLayout tlay;
    oyk_tui_compute_term_layout(30, 80, &tlay);
    assert(tlay.header_row == 0 && tlay.body_top == 1 && tlay.body_rows == 27 &&
           tlay.hotkey_row == 28 && tlay.input_row == 29);

    OykTuiRect modal;
    oyk_tui_center_modal(30, 80, 16, 62, &modal);
    int close_c = oyk_tui_close_btn_col(&modal);
    assert(close_c == modal.left + modal.w - 5);
    assert(oyk_tui_hit_close_btn(&modal, modal.top, close_c) == 1);
    assert(oyk_tui_hit_close_btn(&modal, modal.top, close_c + 2) == 1);
    assert(oyk_tui_hit_close_btn(&modal, modal.top + 1, close_c) == 0);
    assert(oyk_tui_hit_close_btn(&modal, modal.top, close_c - 1) == 0);

    OykThemeConfig nat;
    oyk_theme_init_dos_native(&nat);
    assert(nat.colors[OYK_TR_BG] == 0x0000AAUL);
    assert(nat.colors[OYK_TR_PANEL_BG] == 0x0000AAUL);
    assert(nat.colors[OYK_TR_STATUS_BG] == 0xAAAAAAUL);
    assert(nat.colors[OYK_TR_SELECTION_BG] == 0x00AAAAUL);
    printf("[PASS] test_oyk_tui_layout_and_close_hit\n");
}

static void test_oyk_term_eol_and_word_wrap() {
    /* 1. EOL state machine across CR, LF, CRLF, LFCR, CR+NUL, blank lines, and split packets */
    OykTermEolParser p;
    oyk_term_eol_init(&p, OYK_TERM_EOL_CR, 1);
    assert(oyk_term_eol_effective_tx(&p) == OYK_TERM_EOL_CR);
    assert(strcmp(oyk_term_eol_label(&p), "CR") == 0);

    /* LF stream with blank lines (\n\n) -> 2 blank newlines, auto-detect LF */
    assert(oyk_term_eol_feed_byte(&p, 'A') == OYK_TERM_ACT_CHAR);
    assert(oyk_term_eol_feed_byte(&p, '\n') == OYK_TERM_ACT_NEWLINE);
    assert(oyk_term_eol_effective_tx(&p) == OYK_TERM_EOL_LF);
    assert(oyk_term_eol_feed_byte(&p, '\n') == OYK_TERM_ACT_NEWLINE);
    assert(oyk_term_eol_feed_byte(&p, 'B') == OYK_TERM_ACT_CHAR);
    assert(strcmp(oyk_term_eol_label(&p), "LF") == 0);

    /* CRLF across split packet boundary (\r at end of pkt 1, \n at start of pkt 2) */
    oyk_term_eol_reset(&p, OYK_TERM_EOL_CR, 1);
    assert(oyk_term_eol_feed_byte(&p, 'H') == OYK_TERM_ACT_CHAR);
    assert(oyk_term_eol_feed_byte(&p, '\r') == OYK_TERM_ACT_NEWLINE);
    assert(oyk_term_eol_feed_byte(&p, '\n') == OYK_TERM_ACT_IGNORE);
    assert(oyk_term_eol_effective_tx(&p) == OYK_TERM_EOL_CRLF);
    assert(strcmp(oyk_term_eol_label(&p), "CRLF") == 0);
    /* Second consecutive CRLF (\r\n\r\n) must emit exactly ONE blank newline */
    assert(oyk_term_eol_feed_byte(&p, '\r') == OYK_TERM_ACT_NEWLINE);
    assert(oyk_term_eol_feed_byte(&p, '\n') == OYK_TERM_ACT_IGNORE);
    /* LFCR (\n\r) also collapses to a single newline */
    assert(oyk_term_eol_feed_byte(&p, '\n') == OYK_TERM_ACT_NEWLINE);
    assert(oyk_term_eol_feed_byte(&p, '\r') == OYK_TERM_ACT_IGNORE);
    /* CR + NUL (Telnet NVT) collapses cleanly */
    assert(oyk_term_eol_feed_byte(&p, '\r') == OYK_TERM_ACT_NEWLINE);
    assert(oyk_term_eol_feed_byte(&p, 0x00U) == OYK_TERM_ACT_IGNORE);
    assert(oyk_term_eol_feed_byte(&p, 'Z') == OYK_TERM_ACT_CHAR);

    /* 2. Manual /eol override and parse */
    int mode = 0;
    assert(oyk_term_eol_parse_mode("lf", &mode) == 1 && mode == OYK_TERM_EOL_LF);
    p.override_mode = mode;
    assert(strcmp(oyk_term_eol_label(&p), "LF*") == 0);
    size_t blen = 0;
    assert(memcmp(oyk_term_eol_tx_bytes(&p, &blen), "\n", 1) == 0 && blen == 1);
    assert(oyk_term_eol_parse_mode("auto", &mode) == 1 && mode == OYK_TERM_EOL_AUTO);
    p.override_mode = mode;
    assert(strcmp(oyk_term_eol_label(&p), "CR") == 0);
    assert(oyk_term_eol_parse_mode("CRLF", &mode) == 1 && mode == OYK_TERM_EOL_CRLF);
    assert(oyk_term_eol_parse_mode("bogus", &mode) == 0);

    /* 3. Format TX text normalizing mixed inputs */
    char fmt[64];
    size_t flen = oyk_term_format_tx_text("L1\rL2\nL3\r\n", OYK_TERM_EOL_CRLF, fmt, sizeof(fmt));
    assert(flen == 12 && strcmp(fmt, "L1\r\nL2\r\nL3\r\n") == 0);

    /* 4. Flush-left word wrap + hard-break + dynamic reflow */
    const char* para = "Hello packet radio operators across the HF and VHF bands!";
    OykTermWrapSpan spans[16];
    int sc = oyk_term_wrap_line(para, strlen(para), 20, spans, 16);
    assert(sc == 3);
    assert(spans[0].start == 0 && spans[0].len == 18); /* "Hello packet radio" */
    assert(spans[1].start == 19 && spans[1].len == 20); /* "operators across the" */
    assert(spans[2].start == 40 && spans[2].len == 17); /* "HF and VHF bands!" */
    assert(oyk_term_count_wrapped_rows(para, strlen(para), 40) == 2);

    const char* unbroken = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    sc = oyk_term_wrap_line(unbroken, strlen(unbroken), 10, spans, 16);
    assert(sc == 3 && spans[0].len == 10 && spans[1].len == 10 && spans[2].len == 6);
    assert(oyk_term_count_wrapped_rows("", 0, 20) == 1);

    /* 5. Horizontal input slice */
    size_t in_start = 99;
    int cur_col = -1;
    oyk_term_input_slice(5, 20, &in_start, &cur_col);
    assert(in_start == 0 && cur_col == 5);
    oyk_term_input_slice(25, 20, &in_start, &cur_col);
    assert(in_start == 6 && cur_col == 19);

    /* 6. PortableNode inbound AX.25 session adapts TX EOL to LF and CRLF */
    OykConfig* cfg = new OykConfig();
    oyk_config_init_default(cfg);
    assert(oyk_parse_callsign("K4E0L-7", &cfg->callsign) == 1);
    cfg->iface_count = 1;
    strcpy(cfg->ifaces[0].id, "ax0");
    strcpy(cfg->ifaces[0].type, "axudp");
    strcpy(cfg->ifaces[0].host, "127.0.0.1");
    cfg->ifaces[0].port = 10093;

    TermTxLog* txlog = new TermTxLog();
    memset(txlog, 0, sizeof(*txlog));
    OykPortableNode* node = new OykPortableNode();
    char err[96];
    assert(node->init(cfg, term_mock_tx, txlog, err, sizeof(err)) == 1);

    OykFrame* f = new OykFrame();
    memset(f, 0, sizeof(*f));
    f->dst = cfg->callsign;
    assert(oyk_parse_callsign("W1LF", &f->src) == 1);
    f->is_command = 1;
    f->control = 0x3FU;
    node->on_rx_frame(0, f, 0x7F000001UL, 10093, 0);

    /* W1LF sends command terminated with '\n' (LF) -> replies must use '\n' */
    txlog->count = 0;
    memset(f, 0, sizeof(*f));
    f->dst = cfg->callsign;
    assert(oyk_parse_callsign("W1LF", &f->src) == 1);
    f->is_command = 1;
    f->control = 0x00U;
    f->has_pid = 1;
    f->pid = 0xF0U;
    const char* lf_cmd = "INFO\n";
    f->info_len = static_cast<oyk_u16>(strlen(lf_cmd));
    memcpy(f->info, lf_cmd, f->info_len);
    node->on_rx_frame(0, f, 0x7F000001UL, 10093, 0);
    assert(term_txlog_contains(txlog, "\n") == 1);
    assert(term_txlog_contains(txlog, "\r") == 0);

    /* Switch peer W1LF to CRLF (\r\n) across 2 packets -> replies adapt to \r\n */
    txlog->count = 0;
    const char* p1 = "PORTS\r";
    f->control = 0x02U; /* N(S)=1 */
    f->info_len = static_cast<oyk_u16>(strlen(p1));
    memcpy(f->info, p1, f->info_len);
    node->on_rx_frame(0, f, 0x7F000001UL, 10093, 0);
    const char* p2 = "\nINFO\r\n";
    f->control = 0x04U; /* N(S)=2 */
    f->info_len = static_cast<oyk_u16>(strlen(p2));
    memcpy(f->info, p2, f->info_len);
    txlog->count = 0;
    node->on_rx_frame(0, f, 0x7F000001UL, 10093, 0);
    assert(term_txlog_contains(txlog, "\r\n") == 1);

    delete f;
    delete node;
    delete txlog;
    delete cfg;
    printf("[PASS] test_oyk_term_eol_and_word_wrap\n");
}

static void test_dos_mouse_sim_events() {
    OykDosMouse m;
    int hw = oyk_dos_mouse_init(&m, 0, 640, 200, 8, 8);
    (void)hw;

    OykDosMouseEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = OYK_MOUSE_DOWN;
    ev.row = 0; ev.col = 3;
    ev.button = OYK_MOUSE_BTN_LEFT;
    oyk_dos_mouse_push_sim(&m, &ev);

    OykDosMouseEvent wh;
    memset(&wh, 0, sizeof(wh));
    wh.kind = OYK_MOUSE_WHEEL;
    wh.row = 10; wh.col = 20;
    wh.wheel_delta = -1;
    oyk_dos_mouse_push_sim(&m, &wh);

    OykDosMouseEvent out;
    assert(oyk_dos_mouse_poll(&m, 100UL, &out) == 1);
    assert(out.kind == OYK_MOUSE_DOWN && out.row == 0 && out.col == 3 && out.button == OYK_MOUSE_BTN_LEFT);
    assert(oyk_dos_mouse_poll(&m, 120UL, &out) == 1);
    assert(out.kind == OYK_MOUSE_WHEEL && out.wheel_delta == -1);
    assert(oyk_dos_mouse_poll(&m, 130UL, &out) == 0);

    oyk_dos_mouse_shutdown(&m);
    printf("[PASS] test_dos_mouse_sim_events\n");
}

void oyk_run_portable_term_tests() {
    test_oyk_tui_layout_and_close_hit();
    test_oyk_term_eol_and_word_wrap();
    test_dos_mouse_sim_events();
}
