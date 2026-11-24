/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/* Strict C++98 test suite for the portable MS-DOS / Win16 / Win32 / Win64 core.
 * Uses 8.3 FAT-safe relative paths on MS-DOS and /tmp paths on POSIX.
 * Define OYK_PORTABLE_TESTS_AS_LIBRARY to call oyk_run_portable_tests() from another main().
 */
#include "oyk_portable.hpp"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
  #define OYK_T_CFG_MIXED   "tmix.cfg"
  #define OYK_T_CFG_SAVED   "tsav.cfg"
  #define OYK_T_CFG_CR      "tcr.cfg"
  #define OYK_T_PNODE_CFG   "tpnode.cfg"
  #define OYK_T_PNODE_BAN   "tpban.cfg"
  #define OYK_T_PNODE_LOGS  "tlogs"
  #define OYK_T_LOG_NODE    "tlogs\\node.log"
  #define OYK_T_LOG_APPEALS "tlogs\\appeals.log"
#else
  #define OYK_T_CFG_MIXED   "/tmp/oyk_port_mixed.cfg"
  #define OYK_T_CFG_SAVED   "/tmp/oyk_port_saved.cfg"
  #define OYK_T_CFG_CR      "/tmp/oyk_port_cr.cfg"
  #define OYK_T_PNODE_CFG   "/tmp/oyk_pnode.cfg"
  #define OYK_T_PNODE_BAN   "/tmp/oyk_pnode_ban.cfg"
  #define OYK_T_PNODE_LOGS  "/tmp/oyk_pnode_logs"
  #define OYK_T_LOG_NODE    "/tmp/oyk_pnode_logs/node.log"
  #define OYK_T_LOG_APPEALS "/tmp/oyk_pnode_logs/appeals.log"
#endif

static char* load_raw_file(const char* path, size_t* len_out) {
    FILE* fp = fopen(path, "rb");
    assert(fp != NULL);
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* buf = static_cast<char*>(malloc(static_cast<size_t>(sz) + 1));
    assert(buf != NULL);
    size_t n = fread(buf, 1, static_cast<size_t>(sz), fp);
    buf[n] = '\0';
    fclose(fp);
    if (len_out) *len_out = n;
    return buf;
}

static void write_raw_file(const char* path, const char* data) {
    FILE* fp = fopen(path, "wb");
    assert(fp != NULL);
    fwrite(data, 1, strlen(data), fp);
    fclose(fp);
}

struct MockTxLog {
    int count;
    OykFrame frames[16];
};

static int mock_tx(int /*iface_idx*/, const OykFrame* frame,
                   oyk_u32 /*ip*/, oyk_u16 /*port*/, oyk_u16 /*handle*/, void* ctx) {
    MockTxLog* log = static_cast<MockTxLog*>(ctx);
    if (log->count < 16) {
        log->frames[log->count++] = *frame;
    }
    return 1;
}

static void test_portable_eol_and_config() {
    /* 1. Mixed line endings in node.cfg: FIRST line ending (CRLF) must take precedence! */
    const char* cfg_mixed = OYK_T_CFG_MIXED;
    write_raw_file(cfg_mixed,
                   "[node]\r\n"
                   "callsign = K7DOS-7\n"
                   "alias = DOSNOD\r"
                   "logpath = " OYK_T_PNODE_LOGS "\n"
                   "banlist = " OYK_T_PNODE_BAN "\r\n");

    OykConfig* cfg = new OykConfig;
    char err[96];
    assert(oyk_config_load(cfg_mixed, cfg, err, sizeof(err)));
    assert(cfg->has_detected_eol == 1);
    assert(cfg->line_ending == OYK_EOL_CRLF);
    assert(strcmp(cfg->callsign.base, "K7DOS") == 0 && cfg->callsign.ssid == 7);
    assert(strcmp(cfg->alias.base, "DOSNOD") == 0);

    /* Saving node.cfg back must use uniform CRLF (no lone LF or lone CR) */
    const char* cfg_saved = OYK_T_CFG_SAVED;
    assert(oyk_config_save(cfg, cfg_saved));
    size_t slen = 0;
    char* sraw = load_raw_file(cfg_saved, &slen);
    int found = 0, mixed = 0;
    assert(oyk_detect_first_eol(sraw, slen, OYK_EOL_LF, &found, &mixed) == OYK_EOL_CRLF);
    assert(found == 1 && mixed == 0);
    free(sraw);

    /* 2. CR-only config file read & write-back preserves CR */
    const char* cfg_cr = OYK_T_CFG_CR;
    write_raw_file(cfg_cr, "[node]\rcallsign = W1MAC\r");
    assert(oyk_config_load(cfg_cr, cfg, err, sizeof(err)));
    assert(cfg->line_ending == OYK_EOL_CR);
    assert(oyk_config_save(cfg, cfg_cr));
    sraw = load_raw_file(cfg_cr, &slen);
    assert(oyk_detect_first_eol(sraw, slen, OYK_EOL_LF, &found, &mixed) == OYK_EOL_CR);
    assert(found == 1 && mixed == 0);
    free(sraw);

    delete cfg;
    remove(cfg_mixed);
    remove(cfg_saved);
    remove(cfg_cr);
    printf("[PASS] test_portable_eol_and_config\n");
}

static void test_portable_ax25_kiss_and_bans() {
    /* CRC-16/X-25 test vector "123456789" == 0x906E */
    const oyk_u8 vec[9] = {'1','2','3','4','5','6','7','8','9'};
    assert(oyk_crc16_fcs(vec, 9) == 0x906EU);

    /* KISS round-trip with FEND/FESC inside payload */
    const oyk_u8 payload[4] = {0xC0, 0xDB, 0xDC, 0x41};
    oyk_u8 kbuf[32];
    size_t klen = oyk_kiss_encode(3, payload, 4, kbuf, sizeof(kbuf));
    assert(klen > 4);
    OykKissDecoder kdec;
    oyk_kiss_decoder_init(&kdec);
    int got = 0;
    oyk_u8 out_p = 0, out_c = 0;
    for (size_t i = 0; i < klen; ++i) {
        if (oyk_kiss_decoder_feed_byte(&kdec, kbuf[i], &out_p, &out_c)) {
            got = 1;
            assert(out_p == 3 && out_c == 0 && kdec.len == 4);
            assert(memcmp(kdec.buf, payload, 4) == 0);
        }
    }
    assert(got == 1);

    /* Restricted ban + appeal + CRLF inheritance from node.cfg */
    const char* cfg_path = OYK_T_PNODE_CFG;
    const char* ban_path = OYK_T_PNODE_BAN;
    remove(ban_path);
    write_raw_file(cfg_path,
                   "[node]\r\n"
                   "callsign = K7XT-7\r\n"
                   "logpath = " OYK_T_PNODE_LOGS "\r\n"
                   "banlist = " OYK_T_PNODE_BAN "\r\n");
    write_raw_file(ban_path,
                   "B4DCAL=20261102-130105,20271102-130105,0b1001,Banned for abusive language.\n"
                   "M1XED=20261102-130105,permanent,0x01,Mixed EOL test.\r\n");

    OykConfig* cfg = new OykConfig;
    char err[96];
    assert(oyk_config_load(cfg_path, cfg, err, sizeof(err)));
    assert(cfg->line_ending == OYK_EOL_CRLF);

    MockTxLog* txlog = new MockTxLog;
    memset(txlog, 0, sizeof(*txlog));
    OykPortableNode* node = new OykPortableNode;
    node->set_now_override(1795540000L); /* 2026-11-24 */
    assert(node->init(cfg, mock_tx, txlog, err, sizeof(err)));
    /* Mixed EOL in banlist.cfg was overridden by node.cfg's first EOL (CRLF)! */
    assert(node->banlist_line_ending() == OYK_EOL_CRLF);

    /* B4DCAL connects (unnotified hard ban + appeal allowed): receives UA + restricted notice */
    OykFrame sabm;
    memset(&sabm, 0, sizeof(sabm));
    oyk_parse_callsign("K7XT-7", &sabm.dst);
    oyk_parse_callsign("B4DCAL", &sabm.src);
    sabm.is_command = 1;
    sabm.control = 0x3FU;
    node->on_rx_frame(0, &sabm, 0x7F000001UL, 10093, 0);
    assert(txlog->count >= 2);
    assert(oyk_frame_type(&txlog->frames[0]) == OYK_FT_UA);
    assert(oyk_frame_type(&txlog->frames[1]) == OYK_FT_I);

    /* Submit appeal with control chars -> logged and disconnected */
    OykFrame ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.dst = sabm.dst;
    ifr.src = sabm.src;
    ifr.is_command = 1;
    ifr.control = 0x00U;
    ifr.has_pid = 1;
    ifr.pid = 0xF0U;
    const char* app = "\x07Sorry about that!\r";
    ifr.info_len = static_cast<oyk_u16>(strlen(app));
    memcpy(ifr.info, app, ifr.info_len);
    txlog->count = 0;
    node->on_rx_frame(0, &ifr, 0x7F000001UL, 10093, 0);

    int saw_disc = 0;
    for (int i = 0; i < txlog->count; ++i) {
        if (oyk_frame_type(&txlog->frames[i]) == OYK_FT_DISC) saw_disc = 1;
    }
    assert(saw_disc == 1);

    /* Verify saved banlist.cfg uses uniform CRLF */
    size_t blen = 0;
    char* braw = load_raw_file(ban_path, &blen);
    int found = 0, mixed = 0;
    assert(oyk_detect_first_eol(braw, blen, OYK_EOL_LF, &found, &mixed) == OYK_EOL_CRLF);
    assert(found == 1 && mixed == 0);
    free(braw);

    /* Second SABM from hard-banned B4DCAL -> immediate DM */
    txlog->count = 0;
    node->on_rx_frame(0, &sabm, 0x7F000001UL, 10093, 0);
    assert(txlog->count == 1 && oyk_frame_type(&txlog->frames[0]) == OYK_FT_DM);

    node->shutdown();
    delete node;
    delete txlog;
    delete cfg;
    remove(cfg_path);
    remove(ban_path);
    remove(OYK_T_LOG_NODE);
    remove(OYK_T_LOG_APPEALS);
    printf("[PASS] test_portable_ax25_kiss_and_bans\n");
}

int oyk_run_portable_tests() {
    test_portable_eol_and_config();
    test_portable_ax25_kiss_and_bans();
    return 0;
}

#ifndef OYK_PORTABLE_TESTS_AS_LIBRARY
int main() {
    oyk_run_portable_tests();
    printf("All oyk_portable (C++98) tests passed!\n");
    return 0;
}
#endif
