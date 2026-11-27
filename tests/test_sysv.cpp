/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sysv_core.h"
#include "sysv_tui.h"
#include "sysv_net.h"
#include "sysv_motif_ui.h"
#include "oyk_port_parity_tests.hpp"

static int g_fail = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
            ++g_fail; \
        } \
    } while (0)

static void test_endian_and_protocol() {
    sysv_u8 buf[4];
    oyk_sysv_be16_write(buf, 0x1234U);
    CHECK(buf[0] == 0x12U && buf[1] == 0x34U);
    CHECK(oyk_sysv_be16_read(buf) == 0x1234U);
    oyk_sysv_be32_write(buf, 0x12345678UL);
    CHECK(buf[0] == 0x12U && buf[3] == 0x78U);
    CHECK(oyk_sysv_be32_read(buf) == 0x12345678UL);

    OykSysvFrame f, df;
    memset(&f, 0, sizeof(f));
    CHECK(oyk_sysv_parse_callsign("KR4OYK-7", &f.dst) == 1);
    CHECK(oyk_sysv_parse_callsign("W4ABC-1", &f.src) == 1);
    f.is_command = 1;
    f.control = 0x3FU;
    sysv_u8 raw[64];
    size_t elen = oyk_sysv_ax25_encode(&f, raw, sizeof(raw));
    CHECK(elen >= 16U);
    CHECK(oyk_sysv_ax25_decode(raw, elen, &df) == 1);
    CHECK(oyk_sysv_callsign_eq(&df.dst, &f.dst) == 1);
    CHECK(oyk_sysv_callsign_eq(&df.src, &f.src) == 1);

    static const sysv_u8 payload[4] = { 0x01U, 0xC0U, 0xDBU, 0x42U };
    sysv_u8 kbuf[32];
    size_t klen = oyk_sysv_kiss_encode(0U, payload, sizeof(payload), kbuf, sizeof(kbuf));
    OykSysvKissDecoder dec;
    oyk_sysv_kiss_init(&dec);
    sysv_u8 kp = 99U, kc = 99U;
    int got = 0;
    for (size_t i = 0; i < klen; ++i) {
        if (oyk_sysv_kiss_feed(&dec, kbuf[i], &kp, &kc) == 1) got = 1;
    }
    CHECK(got == 1 && dec.len == sizeof(payload) && kp == 0U && kc == 0U);
}

static void test_config_and_file_jail() {
    OykSysvConfig cfg;
    const char* crlf = "[node]\r\ncallsign = W3TRM-7\r\naccept_incoming = false\r\ntheme = dark\r\n";
    CHECK(oyk_sysv_config_parse(crlf, strlen(crlf), &cfg) == 1);
    CHECK(cfg.line_ending == OYK_SYSV_EOL_CRLF);
    CHECK(cfg.accept_incoming == 0);
    CHECK(strcmp(cfg.active_theme, "dark") == 0);
    char* out = new char[4096];
    size_t slen = oyk_sysv_config_serialize(&cfg, out, 4096U);
    CHECK(slen > 0U && strstr(out, "\r\n") != NULL);
    delete[] out;

    const char* cro = "[node]\rcallsign = N4XYZ\r";
    CHECK(oyk_sysv_config_parse(cro, strlen(cro), &cfg) == 1);
    CHECK(cfg.line_ending == OYK_SYSV_EOL_CR);

    OykSysvFileMgr fm;
    oyk_sysv_filemgr_init(&fm, "/var/oyk/incoming", "/var/oyk/files");
    CHECK(fm.inc.count >= 2 && strcmp(fm.inc.entries[0].name, "..") != 0);
    CHECK(oyk_sysv_filemgr_up(&fm, 0) == 0);
    CHECK(oyk_sysv_filemgr_enter_dir(&fm, 0, "../escape") == 0);
    CHECK(oyk_sysv_filemgr_enter_dir(&fm, 0, "W3TRM-1") == 1);
    CHECK(strcmp(fm.inc.entries[0].name, "..") == 0);
    CHECK(oyk_sysv_filemgr_up(&fm, 0) == 1 && strcmp(fm.inc.entries[0].name, "..") != 0);
}

static void test_23_tui_screens() {
    static const char* const screens[23] = {
        "main", "menu", "settings", "settings_bbs", "settings_chat",
        "settings_ifaces", "peers", "settings_rig", "settings_printer",
        "add_iface_1", "add_iface_2", "add_iface_3", "add_iface_4",
        "edit_iface", "edit_peer", "terminate", "ban", "exit",
        "theme", "terminal", "terminal_help", "files", "about"
    };
    OykSysvApp* app = new OykSysvApp;
    OykSysvCanvas* cv = new OykSysvCanvas;
    char* ansi = new char[32768];
    oyk_sysv_app_init(app, "Motorola 88K SVR4", 1);

    for (int i = 0; i < 23; ++i) {
        CHECK(oyk_sysv_tui_setup_screen(app, screens[i]) == 1);
        oyk_sysv_tui_render(cv, app, 25, 80);
        if (strcmp(screens[i], "main") == 0) {
            CHECK(oyk_sysv_canvas_find_text(cv, "[INCOMING: ALLOW]") == 1);
            CHECK(oyk_sysv_canvas_find_text(cv, "Interfaces") == 1);
            CHECK(oyk_sysv_canvas_find_text(cv, "Last 5 New Callsigns Heard") == 1);
            CHECK(oyk_sysv_canvas_find_text(cv, "Active Connections") == 1);
            CHECK(oyk_sysv_canvas_find_text(cv, "Node Activity Log") == 1);
        } else if (strcmp(screens[i], "menu") == 0) {
            CHECK(oyk_sysv_canvas_find_text(cv, "Allow Incoming") == 1);
            CHECK(oyk_sysv_canvas_find_text(cv, "[X]") == 0); /* dropdown has NO [X] */
        } else if (strncmp(screens[i], "settings", 8) == 0 || strcmp(screens[i], "peers") == 0) {
            CHECK(oyk_sysv_canvas_find_text(cv, "Node Settings") == 1);
            CHECK(oyk_sysv_canvas_find_text(cv, "1:Node") == 1);
            CHECK(oyk_sysv_canvas_find_text(cv, "7:Prn") == 1);
        } else if (strcmp(screens[i], "terminate") == 0) {
            CHECK(oyk_sysv_canvas_find_text(cv, "Terminate Active Connection") == 1);
            CHECK(oyk_sysv_canvas_find_text(cv, "ID    Callsign    Port       State") == 1);
        } else if (strcmp(screens[i], "exit") == 0) {
            CHECK(oyk_sysv_canvas_find_text(cv, "Save & Exit") == 1);
            CHECK(oyk_sysv_canvas_find_text(cv, "Review Peers") == 1);
        } else if (strcmp(screens[i], "terminal") == 0) {
            CHECK(oyk_sysv_canvas_find_text(cv, "Ctrl-A Z:Help") == 1);
            CHECK(oyk_sysv_canvas_find_text(cv, "Tab/F9:Console") == 1);
        } else if (strcmp(screens[i], "terminal_help") == 0) {
            CHECK(oyk_sysv_canvas_find_text(cv, "Terminal Hotkey Help") == 1);
            CHECK(oyk_sysv_canvas_find_text(cv, "[X]") == 1);
        } else if (strcmp(screens[i], "files") == 0) {
            CHECK(oyk_sysv_canvas_find_text(cv, "Incoming: /") == 1);
            CHECK(oyk_sysv_canvas_find_text(cv, "Files: /") == 1);
        } else if (strcmp(screens[i], "about") == 0) {
            CHECK(oyk_sysv_canvas_find_text(cv, "About OYK Amateur Radio Packet Node") == 1);
            CHECK(oyk_sysv_canvas_find_text(cv, "Motorola 88K SVR4") == 1);
        }
    }
    oyk_sysv_tui_to_ansi(cv, 1, ansi, 32768U);
    CHECK(strstr(ansi, "\033[") != NULL);

    OykSysvNet* net = new OykSysvNet;
    oyk_sysv_net_init(net, &app->node.cfg);
    oyk_sysv_net_shutdown(net);
    delete net;
    delete[] ansi;
    delete cv;
    delete app;
}

int main() {
    test_endian_and_protocol();
    test_config_and_file_jail();
    test_23_tui_screens();

    OykPortableNode* pnode = new OykPortableNode();
    OykConfig* pcfg = new OykConfig();
    oyk_config_init_default(pcfg);
    char perr[64];
    CHECK(pnode->init(pcfg, NULL, NULL, perr, sizeof(perr)) == 1);
    oyk_run_shared_port_parity_tests(pnode, stdout, &g_fail);
    pnode->shutdown();
    delete pcfg;
    delete pnode;

    printf("oyk_sysv_tests: failures=%d\n", g_fail);
    return (g_fail == 0) ? 0 : 1;
}
