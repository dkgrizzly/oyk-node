/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/* Strict C++98 test suite for the portable MS-DOS / Win16 / Win32 / Win64 core.
 * Uses 8.3 FAT-safe relative paths on MS-DOS and /tmp paths on POSIX.
 * Define OYK_PORTABLE_TESTS_AS_LIBRARY to call oyk_run_portable_tests() from another main().
 */
#include "oyk_portable.hpp"
#include "oyk_yapp.hpp"
#include "oyk_rig.hpp"
#include "oyk_printer.hpp"
#include "oyk_tui.hpp"
#include "../ports/dos/dos_usb.hpp"
#include "../ports/dos/dos_video.hpp"
#include "../ports/dos/dos_mouse.hpp"
#if !defined(__DOS__) || defined(OYK_DOS_GFX_BUILD)
  #include "../ports/dos/dos_xms.hpp"
  #include "../ports/dos/dos_font8x16.hpp"
  #include "../ports/dos/dos_gfx.hpp"
#endif
#ifndef __DOS__
  #include "../ports/windows/win_service.hpp"
#endif

void oyk_run_portable_term_tests();

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
  #define OYK_T_YAPP_ROOT   "tinc"
  #define OYK_T_YAPP_SRC    "tysrc.txt"
  #define OYK_T_BBS_MAIL    "tmail"
  #define OYK_T_BBS_IDX     "tmail\\BBS.IDX"
  #define OYK_T_BBS_M1      "tmail\\M1.MSG"
  #define OYK_T_BBS_M2      "tmail\\M2.MSG"
  #define OYK_T_BBS_FILES   "tfiles"
  #define OYK_T_BBS_INC     "tinc2"
  #define OYK_T_BBS_INC_DIR "tinc2\\W1AW-0"
  #define OYK_T_BBS_INC_F   "tinc2\\W1AW-0\\up1.txt"
  #define OYK_T_BBS_DST_F   "tfiles\\up1.txt"
#else
  #define OYK_T_CFG_MIXED   "/tmp/oyk_port_mixed.cfg"
  #define OYK_T_CFG_SAVED   "/tmp/oyk_port_saved.cfg"
  #define OYK_T_CFG_CR      "/tmp/oyk_port_cr.cfg"
  #define OYK_T_PNODE_CFG   "/tmp/oyk_pnode.cfg"
  #define OYK_T_PNODE_BAN   "/tmp/oyk_pnode_ban.cfg"
  #define OYK_T_PNODE_LOGS  "/tmp/oyk_pnode_logs"
  #define OYK_T_LOG_NODE    "/tmp/oyk_pnode_logs/node.log"
  #define OYK_T_LOG_APPEALS "/tmp/oyk_pnode_logs/appeals.log"
  #define OYK_T_YAPP_ROOT   "/tmp/oyk_port_incoming"
  #define OYK_T_YAPP_SRC    "/tmp/oyk_port_yapp_src.txt"
  #define OYK_T_BBS_MAIL    "/tmp/oyk_port_mail"
  #define OYK_T_BBS_IDX     "/tmp/oyk_port_mail/BBS.IDX"
  #define OYK_T_BBS_M1      "/tmp/oyk_port_mail/M1.MSG"
  #define OYK_T_BBS_M2      "/tmp/oyk_port_mail/M2.MSG"
  #define OYK_T_BBS_FILES   "/tmp/oyk_port_files"
  #define OYK_T_BBS_INC     "/tmp/oyk_port_inc2"
  #define OYK_T_BBS_INC_DIR "/tmp/oyk_port_inc2/W1AW-0"
  #define OYK_T_BBS_INC_F   "/tmp/oyk_port_inc2/W1AW-0/up1.txt"
  #define OYK_T_BBS_DST_F   "/tmp/oyk_port_files/up1.txt"
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

    /* 2. CR-only config file read & write-back preserves CR and interface default ports */
    const char* cfg_cr = OYK_T_CFG_CR;
    write_raw_file(cfg_cr,
                   "[node]\rcallsign = W1MAC\r"
                   "[interface axudp0]\rtype = axudp\r"
                   "[interface telnet0]\rtype = telnet\r"
                   "[interface telnet1]\rtype = telnet\rbind_port = 8023\r");
    assert(oyk_config_load(cfg_cr, cfg, err, sizeof(err)));
    assert(cfg->line_ending == OYK_EOL_CR);
    assert(strcmp(cfg->callsign.base, "W1MAC") == 0 && cfg->callsign.ssid == 7);
    {
        OykCallsign c_def, c_zero, c_five;
        assert(oyk_parse_callsign_with_default_ssid("KR4OYK", OYK_DEFAULT_NODE_SSID, &c_def) == 1);
        assert(strcmp(c_def.base, "KR4OYK") == 0 && c_def.ssid == 7);
        assert(oyk_parse_callsign_with_default_ssid("KR4OYK-0", OYK_DEFAULT_NODE_SSID, &c_zero) == 1);
        assert(strcmp(c_zero.base, "KR4OYK") == 0 && c_zero.ssid == 0);
        assert(oyk_parse_callsign_with_default_ssid("KR4OYK-5", OYK_DEFAULT_NODE_SSID, &c_five) == 1);
        assert(strcmp(c_five.base, "KR4OYK") == 0 && c_five.ssid == 5);
    }
    assert(cfg->iface_count == 3);
    assert(cfg->ifaces[0].port == 10093);
    assert(cfg->ifaces[1].port == 8010);
    assert(cfg->ifaces[2].port == 8023);
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

    /* Graceful shutdown with an active normal session sends 73 sign-off + DISC, clears on UA */
    OykFrame sabm2;
    memset(&sabm2, 0, sizeof(sabm2));
    oyk_parse_callsign("K7XT-7", &sabm2.dst);
    oyk_parse_callsign("W1GOOD", &sabm2.src);
    sabm2.is_command = 1;
    sabm2.control = 0x3FU;
    txlog->count = 0;
    node->on_rx_frame(0, &sabm2, 0x7F000001UL, 10093, 0);
    assert(node->total_active_connections() == 1);

    txlog->count = 0;
    assert(node->begin_shutdown() == 1);
    assert(node->disconnecting_count() == 1);
    int saw_73 = 0;
    saw_disc = 0;
    for (int i = 0; i < txlog->count; ++i) {
        if (oyk_frame_type(&txlog->frames[i]) == OYK_FT_I) {
            char txt[260];
            size_t ilen = txlog->frames[i].info_len;
            if (ilen >= sizeof(txt)) ilen = sizeof(txt) - 1;
            memcpy(txt, txlog->frames[i].info, ilen);
            txt[ilen] = '\0';
            if (strstr(txt, "73 de K7XT-7") != NULL) saw_73 = 1;
        } else if (oyk_frame_type(&txlog->frames[i]) == OYK_FT_DISC) {
            saw_disc = 1;
        }
    }
    assert(saw_73 == 1 && saw_disc == 1);

    OykFrame ua_reply;
    memset(&ua_reply, 0, sizeof(ua_reply));
    ua_reply.dst = sabm2.dst;
    ua_reply.src = sabm2.src;
    ua_reply.is_response = 1;
    ua_reply.control = 0x73U;
    node->on_rx_frame(0, &ua_reply, 0x7F000001UL, 10093, 0);
    assert(node->disconnecting_count() == 0);
    node->finish_shutdown();
    delete node;
    delete txlog;
    delete cfg;
    remove(cfg_path);
    remove(ban_path);
    remove(OYK_T_LOG_NODE);
    remove(OYK_T_LOG_APPEALS);
    printf("[PASS] test_portable_ax25_kiss_and_bans\n");
}

struct PortableYappPipe {
    OykYappSession* peer;
    int overwrite_allow;
    int overwrite_calls;
};

static int yapp_pipe_send(const oyk_u8* data, size_t len, void* ctx) {
    PortableYappPipe* pipe = static_cast<PortableYappPipe*>(ctx);
    oyk_u8 plain[32];
    oyk_yapp_feed_bytes(pipe->peer, data, len, plain, sizeof(plain));
    return 1;
}

static int yapp_test_overwrite(const char* /*path*/, const char* /*fname*/, void* ctx) {
    PortableYappPipe* pipe = static_cast<PortableYappPipe*>(ctx);
    pipe->overwrite_calls++;
    return pipe->overwrite_allow;
}

static char g_pt_local_buf[512];
static size_t g_pt_local_len = 0;

static void pt_local_cb(oyk_u32 /*sid*/, int event_type,
                        const oyk_u8* data, size_t len,
                        const char* /*reason*/, void* /*ctx*/) {
    if (event_type == 2 && data && len > 0) {
        size_t avail = (g_pt_local_len < sizeof(g_pt_local_buf) - 1)
                           ? (sizeof(g_pt_local_buf) - 1 - g_pt_local_len)
                           : 0;
        if (len > avail) len = avail;
        if (len > 0) {
            memcpy(g_pt_local_buf + g_pt_local_len, data, len);
            g_pt_local_len += len;
            g_pt_local_buf[g_pt_local_len] = '\0';
        }
    }
}

static void test_portable_yapp_and_local_terminal() {
    /* 0. Ban lift spec parser (permanent, minutes, hours, days, dates) */
    char lift_out[20];
    assert(oyk_parse_lift_spec("", 1795540000L, lift_out, sizeof(lift_out)) == 1);
    assert(strcmp(lift_out, "permanent") == 0);
    assert(oyk_parse_lift_spec("permanent", 1795540000L, lift_out, sizeof(lift_out)) == 1);
    assert(strcmp(lift_out, "permanent") == 0);
    assert(oyk_parse_lift_spec("30m", 1795540000L, lift_out, sizeof(lift_out)) == 1);
    assert(strlen(lift_out) == 15 && lift_out[8] == '-');
    assert(oyk_parse_lift_spec("7 days", 1795540000L, lift_out, sizeof(lift_out)) == 1);
    assert(strlen(lift_out) == 15 && lift_out[8] == '-');
    assert(oyk_parse_lift_spec("2027-06-15", 1795540000L, lift_out, sizeof(lift_out)) == 1);
    assert(strcmp(lift_out, "20270615-235959") == 0);
    assert(oyk_parse_lift_spec("bad-spec", 1795540000L, lift_out, sizeof(lift_out)) == 0);

    /* 0b. Portable CHAT default SSID 12 + local loopback terminal */
    OykConfig* pcfg = new OykConfig;
    oyk_config_init_default(pcfg);
    strcpy(pcfg->log_path, OYK_T_PNODE_LOGS);
    strcpy(pcfg->banlist_path, OYK_T_PNODE_BAN);
    assert(pcfg->chat_enabled == 1 && pcfg->chat_ssid == 12);
    OykCallsign chat_c;
    oyk_config_resolved_chat_callsign(pcfg, &chat_c);
    assert(strcmp(chat_c.base, "N0CALL") == 0 && chat_c.ssid == 12);

    OykPortableNode* lnode = new OykPortableNode;
    MockTxLog* ltx = new MockTxLog;
    memset(ltx, 0, sizeof(*ltx));
    char lerr[64];
    assert(lnode->init(pcfg, mock_tx, ltx, lerr, sizeof(lerr)) == 1);
    g_pt_local_len = 0;
    g_pt_local_buf[0] = '\0';
    lnode->set_local_event_callback(pt_local_cb, NULL);

    oyk_u32 lsid = 0;
    OykCallsign lres;
    int is_local = 0;
    assert(lnode->open_local_terminal(-1, "CHAT", &lsid, &lres, &is_local, lerr, sizeof(lerr)) == 1);
    assert(is_local == 1 && lres.ssid == 12);
    assert(strstr(g_pt_local_buf, "N0CALL-12 Packet Chat") != NULL);

    /* Remote AX.25 station connects directly to N0CALL-12 CHAT */
    OykFrame csabm;
    memset(&csabm, 0, sizeof(csabm));
    csabm.dst = chat_c;
    oyk_parse_callsign("W2CHT", &csabm.src);
    csabm.is_command = 1;
    csabm.control = 0x3FU;
    lnode->on_rx_frame(0, &csabm, 0x7F000001UL, 10093, 0);
    assert(strstr(g_pt_local_buf, "W2CHT joined Lobby") != NULL);

    /* Local terminal sends chat line -> remote W2CHT receives broadcast I-frame */
    ltx->count = 0;
    const char* cmsg = "Hi W2CHT!\r";
    assert(lnode->send_local_bytes(lsid, reinterpret_cast<const oyk_u8*>(cmsg), strlen(cmsg)) == 1);
    assert(ltx->count >= 1 && oyk_frame_type(&ltx->frames[0]) == OYK_FT_I);
    lnode->close_local_terminal(lsid);
    lnode->shutdown();
    delete ltx;
    delete lnode;
    delete pcfg;

    /* 1. Filename sanitization & path traversal protection */
    assert(oyk_yapp_is_safe_filename("important-file.txt") == 1);
    assert(oyk_yapp_is_safe_filename("TEST123.BIN") == 1);
    assert(oyk_yapp_is_safe_filename("../evil.txt") == 0);
    assert(oyk_yapp_is_safe_filename("..\\evil.txt") == 0);
    assert(oyk_yapp_is_safe_filename("a/../b.txt") == 0);
    assert(oyk_yapp_is_safe_filename("sub/file.txt") == 0);
    assert(oyk_yapp_is_safe_filename("sub\\file.txt") == 0);
    assert(oyk_yapp_is_safe_filename("C:autoexec.bat") == 0);
    assert(oyk_yapp_is_safe_filename(".hidden") == 0);
    assert(oyk_yapp_is_safe_filename("LICENSE.") == 0);
    assert(oyk_yapp_is_safe_filename("CON") == 0);
    assert(oyk_yapp_is_safe_filename("COM1.TXT") == 0);

    /* 2. Peer spec parsing & incoming/<CALL-SSID>/<file> resolution */
    char p_call[16];
    oyk_u32 p_ip = 0;
    oyk_u16 p_port = 0;
    assert(oyk_parse_peer_spec("N0CALL-11@192.168.1.50:10093", p_call, sizeof(p_call),
                               &p_ip, &p_port, 10093) == 1);
    assert(strcmp(p_call, "N0CALL-11") == 0 && p_port == 10093 && p_ip != 0);

    OykCallsign remote;
    assert(oyk_parse_callsign("N0CALL-11", &remote) == 1);
    char out_path[180], out_dir[120];
    assert(oyk_yapp_resolve_incoming_path("incoming", &remote, "important-file.txt",
                                          out_path, sizeof(out_path),
                                          out_dir, sizeof(out_dir)) == 1);
    assert(strstr(out_path, "important-file.txt") != NULL);
    assert(oyk_yapp_resolve_incoming_path("incoming", &remote, "../escape.txt",
                                          out_path, sizeof(out_path),
                                          out_dir, sizeof(out_dir)) == 0);

    /* 3. End-to-end YAPP-C transfer + overwrite decline & approve + traversal rejection */
    write_raw_file(OYK_T_YAPP_SRC, "Hello YAPP-C packet radio world! 73 de N0CALL-11.\r\n");

    OykYappSession* tx_y = new OykYappSession;
    OykYappSession* rx_y = new OykYappSession;
    PortableYappPipe tx_pipe;
    PortableYappPipe rx_pipe;
    tx_pipe.peer = rx_y; tx_pipe.overwrite_allow = 0; tx_pipe.overwrite_calls = 0;
    rx_pipe.peer = tx_y; rx_pipe.overwrite_allow = 0; rx_pipe.overwrite_calls = 0;

    oyk_yapp_init(tx_y, OYK_T_YAPP_ROOT, yapp_pipe_send, &tx_pipe, NULL, NULL);
    oyk_yapp_init(rx_y, OYK_T_YAPP_ROOT, yapp_pipe_send, &rx_pipe, yapp_test_overwrite, &rx_pipe);
    oyk_yapp_set_remote(rx_y, &remote);

    /* Remove any stale test output first */
    const char* src_base = strrchr(OYK_T_YAPP_SRC, '/');
    src_base = src_base ? (src_base + 1) : OYK_T_YAPP_SRC;
    char saved_path[180], saved_dir[120];
    assert(oyk_yapp_resolve_incoming_path(OYK_T_YAPP_ROOT, &remote, src_base,
                                          saved_path, sizeof(saved_path),
                                          saved_dir, sizeof(saved_dir)) == 1);
    remove(saved_path);

    char err[64];
    assert(oyk_yapp_start_send(tx_y, OYK_T_YAPP_SRC, err, sizeof(err)) == 1);
    assert(tx_y->state == OYK_YAPP_COMPLETE);
    assert(rx_y->state == OYK_YAPP_COMPLETE);
    assert(rx_y->yappc == 1);
    assert(rx_pipe.overwrite_calls == 0);

    size_t got_len = 0;
    char* got = load_raw_file(saved_path, &got_len);
    assert(strcmp(got, "Hello YAPP-C packet radio world! 73 de N0CALL-11.\r\n") == 0);
    free(got);

    /* Second transfer with overwrite_allow=0 MUST NOT overwrite existing file */
    write_raw_file(OYK_T_YAPP_SRC, "MODIFIED CONTENT");
    oyk_yapp_reset(rx_y);
    assert(oyk_yapp_start_send(tx_y, OYK_T_YAPP_SRC, err, sizeof(err)) == 1);
    assert(rx_pipe.overwrite_calls == 1);
    assert(rx_y->state == OYK_YAPP_ABORTED);
    got = load_raw_file(saved_path, &got_len);
    assert(strcmp(got, "Hello YAPP-C packet radio world! 73 de N0CALL-11.\r\n") == 0);
    free(got);

    /* Third transfer with overwrite_allow=1 updates the file */
    rx_pipe.overwrite_allow = 1;
    oyk_yapp_reset(rx_y);
    assert(oyk_yapp_start_send(tx_y, OYK_T_YAPP_SRC, err, sizeof(err)) == 1);
    assert(rx_pipe.overwrite_calls == 2);
    assert(rx_y->state == OYK_YAPP_COMPLETE);
    got = load_raw_file(saved_path, &got_len);
    assert(strcmp(got, "MODIFIED CONTENT") == 0);
    free(got);

    /* Crafted YAPP header with path traversal MUST be rejected */
    oyk_yapp_reset(rx_y);
    const oyk_u8 init_pkt[2] = { OYK_YAPP_ENQ, 0x01U };
    oyk_u8 dummy[8];
    oyk_yapp_feed_bytes(rx_y, init_pkt, 2, dummy, sizeof(dummy));
    const char* bad_hdr = "../evil.txt\0" "5\0";
    oyk_u8 soh_pkt[20];
    soh_pkt[0] = OYK_YAPP_SOH;
    soh_pkt[1] = 14;
    memcpy(soh_pkt + 2, bad_hdr, 14);
    oyk_yapp_feed_bytes(rx_y, soh_pkt, 16, dummy, sizeof(dummy));
    assert(rx_y->state == OYK_YAPP_ERROR);

    remove(saved_path);
    remove(OYK_T_YAPP_SRC);
    delete tx_y;
    delete rx_y;
    printf("[PASS] test_portable_yapp_and_local_terminal\n");
}

struct PortableBbsCap {
    char buf[1024];
    size_t len;
};

static void test_bbs_local_cb(oyk_u32 /*sid*/, int ev, const oyk_u8* data, size_t len,
                              const char* /*reason*/, void* ctx) {
    PortableBbsCap* cap = static_cast<PortableBbsCap*>(ctx);
    if (ev == 2 && data && len > 0) {
        size_t avail = sizeof(cap->buf) - 1 - cap->len;
        if (len > avail) len = avail;
        memcpy(cap->buf + cap->len, data, len);
        cap->len += len;
        cap->buf[cap->len] = '\0';
    }
}

static void test_portable_bbs() {
    remove(OYK_T_BBS_IDX);
    remove(OYK_T_BBS_M1);
    remove(OYK_T_BBS_M2);

    OykConfig* pcfg = new OykConfig();
    oyk_config_init_default(pcfg);
    assert(pcfg->bbs_enabled == 1);
    assert(pcfg->bbs_ssid == 11);
    oyk_parse_callsign("K7OYK-7", &pcfg->callsign);
    strcpy(pcfg->log_path, OYK_T_PNODE_LOGS);
    strcpy(pcfg->banlist_path, OYK_T_PNODE_BAN);
    strcpy(pcfg->bbs_mail_path, OYK_T_BBS_MAIL);

    OykCallsign bbs_call;
    oyk_config_resolved_bbs_callsign(pcfg, &bbs_call);
    char bbs_s[16];
    oyk_format_callsign(&bbs_call, bbs_s, sizeof(bbs_s));
    assert(strcmp(bbs_s, "K7OYK-11") == 0);

    OykPortableNode* node = new OykPortableNode();
    char err[64];
    assert(node->init(pcfg, NULL, NULL, err, sizeof(err)) == 1);

    PortableBbsCap* cap = new PortableBbsCap();
    cap->len = 0;
    cap->buf[0] = '\0';
    node->set_local_event_callback(test_bbs_local_cb, cap);

    oyk_u32 sid = 0;
    int is_loc = 0;
    assert(node->open_local_terminal(-1, "K7OYK-11", &sid, NULL, &is_loc, err, sizeof(err)) == 1);
    assert(is_loc == 1);
    assert(strstr(cap->buf, "[OYK-0.1-HMR$]") != NULL);
    assert(strstr(cap->buf, "de K7OYK-11>") != NULL);

    /* Send bulletin #1 via SB ALL */
    cap->len = 0; cap->buf[0] = '\0';
    const char* s1 = "SB ALL\rTest Subj\rHello DOS BBS!\r/EX\r";
    assert(node->send_local_bytes(sid, reinterpret_cast<const oyk_u8*>(s1), strlen(s1)) == 1);
    assert(strstr(cap->buf, "Message #1 saved") != NULL);

    /* Read #1 */
    cap->len = 0; cap->buf[0] = '\0';
    const char* r1 = "R 1\r";
    assert(node->send_local_bytes(sid, reinterpret_cast<const oyk_u8*>(r1), strlen(r1)) == 1);
    assert(strstr(cap->buf, "Hello DOS BBS!") != NULL);

    node->close_local_terminal(sid);
    delete node;

    /* Reload fresh OykPortableNode and confirm BBS.IDX + M1.MSG persisted */
    OykPortableNode* node2 = new OykPortableNode();
    assert(node2->init(pcfg, NULL, NULL, err, sizeof(err)) == 1);
    cap->len = 0; cap->buf[0] = '\0';
    node2->set_local_event_callback(test_bbs_local_cb, cap);
    assert(node2->open_local_terminal(-1, "BBS", &sid, NULL, &is_loc, err, sizeof(err)) == 1);
    assert(strstr(cap->buf, "Active msgs: 1") != NULL);

    cap->len = 0; cap->buf[0] = '\0';
    const char* lb = "LB\r";
    assert(node2->send_local_bytes(sid, reinterpret_cast<const oyk_u8*>(lb), strlen(lb)) == 1);
    assert(strstr(cap->buf, "Test Subj") != NULL);

    /* Portable BBS Files Area: traversal rejection & sysop approval flow */
    {
        char abs_p[160], rel_p[64], perr[64];
        assert(oyk_bbs_resolve_safe_path(OYK_T_BBS_FILES, "", "../secret.txt", 0,
                                         abs_p, sizeof(abs_p), rel_p, sizeof(rel_p),
                                         perr, sizeof(perr)) == 0);
        assert(oyk_bbs_resolve_safe_path(OYK_T_BBS_FILES, "", "..\\secret.txt", 0,
                                         abs_p, sizeof(abs_p), rel_p, sizeof(rel_p),
                                         perr, sizeof(perr)) == 0);
        assert(oyk_bbs_resolve_safe_path(OYK_T_BBS_FILES, "", "C:win.ini", 0,
                                         abs_p, sizeof(abs_p), rel_p, sizeof(rel_p),
                                         perr, sizeof(perr)) == 0);
        assert(oyk_bbs_resolve_safe_path(OYK_T_BBS_FILES, "", "..", 1,
                                         abs_p, sizeof(abs_p), rel_p, sizeof(rel_p),
                                         perr, sizeof(perr)) == 0);

        remove(OYK_T_BBS_INC_F);
        remove(OYK_T_BBS_DST_F);

        /* Stage an incoming file via YAPP loopback into OYK_T_BBS_INC/W1AW-0/ */
        write_raw_file(OYK_T_YAPP_SRC, "Portable BBS approved file payload!\n");
        OykYappSession* tx_y = new OykYappSession();
        OykYappSession* rx_y = new OykYappSession();
        PortableYappPipe tx_pipe, rx_pipe;
        tx_pipe.peer = rx_y; tx_pipe.overwrite_allow = 1; tx_pipe.overwrite_calls = 0;
        rx_pipe.peer = tx_y; rx_pipe.overwrite_allow = 1; rx_pipe.overwrite_calls = 0;
        oyk_yapp_init(tx_y, OYK_T_BBS_INC, yapp_pipe_send, &tx_pipe, yapp_test_overwrite, &tx_pipe);
        oyk_yapp_init(rx_y, OYK_T_BBS_INC, yapp_pipe_send, &rx_pipe, yapp_test_overwrite, &rx_pipe);
        OykCallsign up_call;
        oyk_parse_callsign("W1AW-0", &up_call);
        oyk_yapp_set_remote(rx_y, &up_call);
        assert(oyk_yapp_start_send(tx_y, OYK_T_YAPP_SRC, perr, sizeof(perr)) == 1);
        assert(rx_y->state == OYK_YAPP_COMPLETE);
        delete tx_y;
        delete rx_y;
        remove(OYK_T_YAPP_SRC);

        OykPendingIncoming* plist = new OykPendingIncoming[8];
        int pcnt = oyk_bbs_list_incoming(OYK_T_BBS_INC, plist, 8);
        assert(pcnt >= 1);

        /* Reject traversal subdir on approval */
        char dst_out[160];
        int exists = 0;
        assert(oyk_bbs_approve_incoming_file(OYK_T_BBS_INC, OYK_T_BBS_FILES,
                                             plist[0].uploader, plist[0].filename,
                                             "../escape", 0, dst_out, sizeof(dst_out),
                                             &exists, perr, sizeof(perr)) == 0);

        /* Valid approval moves incoming file into OYK_T_BBS_FILES */
        assert(oyk_bbs_approve_incoming_file(OYK_T_BBS_INC, OYK_T_BBS_FILES,
                                             plist[0].uploader, plist[0].filename,
                                             "", 1, dst_out, sizeof(dst_out),
                                             &exists, perr, sizeof(perr)) == 1);
        assert(oyk_bbs_list_incoming(OYK_T_BBS_INC, plist, 8) == pcnt - 1);

        /* Verify approved file appears in oyk_bbs_list_approved & can be removed safely */
        OykApprovedFile* alist = new OykApprovedFile[8];
        int acnt = oyk_bbs_list_approved(OYK_T_BBS_FILES, alist, 8);
        assert(acnt >= 1);
        assert(oyk_bbs_delete_approved_file(OYK_T_BBS_FILES, "../escape.txt", perr, sizeof(perr)) == 0);
        assert(oyk_bbs_delete_approved_file(OYK_T_BBS_FILES, alist[0].rel_path, perr, sizeof(perr)) == 1);
        assert(oyk_bbs_list_approved(OYK_T_BBS_FILES, alist, 8) == acnt - 1);
        delete[] alist;
        delete[] plist;
    }

    /* Verify BBS traversal commands rejected in live portable BBS session */
    strcpy(pcfg->bbs_files_path, OYK_T_BBS_FILES);
    cap->len = 0; cap->buf[0] = '\0';
    const char* bad_type = "TYPE ../secret.txt\r";
    assert(node2->send_local_bytes(sid, reinterpret_cast<const oyk_u8*>(bad_type), strlen(bad_type)) == 1);
    assert(strstr(cap->buf, "Access denied") != NULL);

    cap->len = 0; cap->buf[0] = '\0';
    const char* loc_get = "GET readme.txt\r";
    assert(node2->send_local_bytes(sid, reinterpret_cast<const oyk_u8*>(loc_get), strlen(loc_get)) == 1);
    assert(strstr(cap->buf, "disallowed") != NULL);

    node2->close_local_terminal(sid);
    delete node2;
    delete cap;
    delete pcfg;

    remove(OYK_T_BBS_IDX);
    remove(OYK_T_BBS_M1);
    remove(OYK_T_BBS_M2);
    printf("[PASS] test_portable_bbs\n");
}

static void test_portable_settings_reconfig_and_readonly() {
    OykConfig* cfg = new OykConfig;
    OykPortableNode* node = new OykPortableNode;
    MockTxLog* tx = new MockTxLog;
    tx->count = 0;

    oyk_config_init_default(cfg);
    oyk_parse_callsign("K7OYK-7", &cfg->callsign);
    strcpy(cfg->welcome_text, "Portable Welcome 1");
    strcpy(cfg->info_text, "Portable Info 1");
    strcpy(cfg->source_path, OYK_T_PNODE_CFG);
    cfg->bbs_enabled = 1;
    cfg->bbs_files_enabled = 1;
    cfg->bbs_ssid = 11;
    cfg->chat_enabled = 1;
    cfg->chat_ssid = 12;

    char err[64];
    assert(oyk_config_save(cfg, OYK_T_PNODE_CFG) == 1);
    assert(node->init(cfg, mock_tx, tx, err, sizeof(err)) == 1);

    /* 1. Toggle BBS Files Area OFF/ON & edit welcome/info without restarting sessions */
    PortableBbsCap* cap = new PortableBbsCap;
    cap->len = 0; cap->buf[0] = '\0';
    node->set_local_event_callback(test_bbs_local_cb, cap);

    oyk_u32 bbs_sid = 0;
    int is_loc = 0;
    assert(node->open_local_terminal(-1, "BBS", &bbs_sid, NULL, &is_loc, err, sizeof(err)) == 1);

    OykConfig* upd = new OykConfig;
    *upd = node->config();
    upd->bbs_files_enabled = 0;
    strcpy(upd->welcome_text, "Portable Welcome 2");
    strcpy(upd->info_text, "Portable Info 2");
    OykConfigApplyResult res;
    assert(node->apply_config(upd, 1, &res) == 1);
    assert(res.applied == 1 && res.saved_to_disk == 1 && res.identity_restarted == 0 && res.signed_off_sessions == 0);

    cap->len = 0; cap->buf[0] = '\0';
    const char* fcmd = "FILES\r";
    assert(node->send_local_bytes(bbs_sid, reinterpret_cast<const oyk_u8*>(fcmd), strlen(fcmd)) == 1);
    assert(strstr(cap->buf, "Files Area is currently disabled") != NULL);

    /* 2. Connect remote CHAT session, then change callsign/SSID + disable CHAT */
    OykCallsign chat_c;
    oyk_config_resolved_chat_callsign(&node->config(), &chat_c);
    OykFrame csabm;
    memset(&csabm, 0, sizeof(csabm));
    csabm.dst = chat_c;
    oyk_parse_callsign("W2CHT", &csabm.src);
    csabm.is_command = 1;
    csabm.control = 0x3FU;
    node->on_rx_frame(0, &csabm, 0x7F000001UL, 10093, 0);

    tx->count = 0;
    cap->len = 0; cap->buf[0] = '\0';
    *upd = node->config();
    oyk_parse_callsign("W1AW-7", &upd->callsign);
    upd->bbs_ssid = 10;     /* BBS moves to W1AW-10 */
    upd->chat_enabled = 0;  /* CHAT disabled */
    upd->accept_incoming = 1;
    assert(node->apply_config(upd, 1, &res) == 1);
    assert(res.applied == 1 && res.saved_to_disk == 1 && res.identity_restarted == 1);
    assert(res.signed_off_sessions == 2);
    assert(node->accept_incoming() == 1);

    /* Local BBS session received W1AW-10 restart notice */
    assert(strstr(cap->buf, "restarting for configuration changes") != NULL);
    assert(strstr(cap->buf, "W1AW-10") != NULL);

    /* Remote CHAT session received TX I-frame with unavailability notice + DISC */
    int saw_chat_unavail = 0;
    for (int i = 0; i < tx->count; ++i) {
        if (oyk_frame_type(&tx->frames[i]) == OYK_FT_I) {
            char tmp[260];
            size_t n = tx->frames[i].info_len;
            if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
            memcpy(tmp, tx->frames[i].info, n);
            tmp[n] = '\0';
            if (strstr(tmp, "not be available") != NULL) saw_chat_unavail = 1;
        }
    }
    assert(saw_chat_unavail == 1);

    /* 3. Manual deny MUST stay disabled across config changes */
    node->set_accept_incoming(0);
    assert(node->accept_incoming() == 0);
    *upd = node->config();
    oyk_parse_callsign("W2AW-7", &upd->callsign);
    upd->accept_incoming = 1;
    assert(node->apply_config(upd, 1, &res) == 1);
    assert(res.applied == 1 && node->accept_incoming() == 0);
    node->set_accept_incoming(1);
    assert(node->accept_incoming() == 1);

    /* 4. Add & remove interface mapping */
    *upd = node->config();
    assert(upd->iface_count < OYK_MAX_INTERFACES);
    OykIfaceConfig* ni = &upd->ifaces[upd->iface_count++];
    memset(ni, 0, sizeof(*ni));
    strcpy(ni->id, "telnet0");
    strcpy(ni->type, "telnet");
    strcpy(ni->host, "0.0.0.0");
    ni->port = 8010;
    assert(node->apply_config(upd, 1, &res) == 1);
    assert(res.interfaces_changed == 1 && node->iface_count() == 2);
    upd->iface_count = 1;
    assert(node->apply_config(upd, 1, &res) == 1);
    assert(res.interfaces_changed == 1 && node->iface_count() == 1);

    /* 5. Unwritable config path applies in memory with warning */
    *upd = node->config();
    strcpy(upd->source_path, "/nonexistent_oyk_ro_dir/node.cfg");
    oyk_parse_callsign("K7MEM-7", &upd->callsign);
    assert(node->apply_config(upd, 1, &res) == 1);
    assert(res.applied == 1 && res.saved_to_disk == 0);
    assert(strstr(res.warning, "not writable") != NULL);
    char cur_call[16];
    oyk_format_callsign(&node->config().callsign, cur_call, sizeof(cur_call));
    assert(strcmp(cur_call, "K7MEM-7") == 0);

    node->shutdown();
    delete upd;
    delete cap;
    delete tx;
    delete node;
    delete cfg;
    remove(OYK_T_PNODE_CFG);
    printf("[PASS] test_portable_settings_reconfig_and_readonly\n");
}

struct TestMiniNetCap {
    oyk_u8 eth_tx[128];
    size_t eth_tx_len;
    oyk_u8 ppp_tx[256];
    size_t ppp_tx_len;
    oyk_u16 rx_sport;
    oyk_u16 rx_dport;
    char rx_payload[64];
    size_t rx_len;
};

static int test_mn_tx_eth(const oyk_u8* frame, size_t len, void* ctx) {
    TestMiniNetCap* c = static_cast<TestMiniNetCap*>(ctx);
    if (len > sizeof(c->eth_tx)) len = sizeof(c->eth_tx);
    memcpy(c->eth_tx, frame, len);
    c->eth_tx_len = len;
    return 1;
}

static int test_mn_tx_byte(oyk_u8 b, void* ctx) {
    TestMiniNetCap* c = static_cast<TestMiniNetCap*>(ctx);
    if (c->ppp_tx_len < sizeof(c->ppp_tx)) {
        c->ppp_tx[c->ppp_tx_len++] = b;
    }
    return 1;
}

static void test_mn_udp_rx(oyk_u32 /*src_ip*/, oyk_u16 sport, oyk_u16 dport,
                           const oyk_u8* pay, size_t len, void* ctx) {
    TestMiniNetCap* c = static_cast<TestMiniNetCap*>(ctx);
    c->rx_sport = sport;
    c->rx_dport = dport;
    size_t n = (len < sizeof(c->rx_payload) - 1) ? len : (sizeof(c->rx_payload) - 1);
    memcpy(c->rx_payload, pay, n);
    c->rx_payload[n] = '\0';
    c->rx_len = n;
}

static void test_portable_rig_and_dos_usb() {
    /* 1. Hamlib rigctl command builders & parsers */
    char buf[64];
    assert(oyk_rigctl_build_get_freq(buf, sizeof(buf)) == 2 && strcmp(buf, "f\n") == 0);
    assert(oyk_rigctl_build_set_freq(145050000UL, buf, sizeof(buf)) > 0 && strcmp(buf, "F 145050000\n") == 0);
    assert(oyk_rigctl_build_set_mode("PKTFM", 12500UL, buf, sizeof(buf)) > 0 && strcmp(buf, "M PKTFM 12500\n") == 0);
    assert(oyk_rigctl_build_set_ptt(1, buf, sizeof(buf)) == 4 && strcmp(buf, "T 1\n") == 0);
    assert(oyk_rigctl_build_set_vfo("VFOA", buf, sizeof(buf)) > 0 && strcmp(buf, "V VFOA\n") == 0);

    oyk_u32 hz = 0UL;
    int rprt = 99;
    assert(oyk_rigctl_parse_freq_reply("446000000\n", &hz, &rprt) == 1 && hz == 446000000UL);
    assert(oyk_rigctl_parse_freq_reply("RPRT -1\n", &hz, &rprt) == 0 && rprt == -1);
    char mode[16];
    oyk_u32 pb = 0UL;
    assert(oyk_rigctl_parse_mode_reply("FM\n12500\n", mode, sizeof(mode), &pb, &rprt) == 1);
    assert(strcmp(mode, "FM") == 0 && pb == 12500UL);
    int ptt = -1;
    assert(oyk_rigctl_parse_ptt_reply("1\n", &ptt, &rprt) == 1 && ptt == 1);

    /* 2. RadioFoundry / takRF UDP 4191 command builders & status parser */
    assert(oyk_rf_build_select(-1, -1, buf, sizeof(buf)) == 6 && strcmp(buf, "select") == 0);
    assert(oyk_rf_build_select(2, 5, buf, sizeof(buf)) > 0 && strcmp(buf, "select?zone=2&pos=5") == 0);
    assert(oyk_rf_build_stop_notify(buf, sizeof(buf)) == 15 && strcmp(buf, "select?notify=0") == 0);
    assert(oyk_rf_build_step_channel(-1, buf, sizeof(buf)) > 0 && strcmp(buf, "step?channel=-1") == 0);
    assert(oyk_rf_build_step_zone(1, buf, sizeof(buf)) > 0 && strcmp(buf, "step?zone=1") == 0);

    OykRigStatus st;
    oyk_rig_status_init(&st, OYK_RIG_PROTO_RADIOFOUNDRY);
    assert(oyk_rf_parse_status("zone=2 pos=5 name=\"Local DMR\" freq=446.500 cc=1 ts=2 tg=3100", &st) == 1);
    assert(st.zone == 2 && st.pos == 5 && strcmp(st.channel_name, "Local DMR") == 0);
    assert(st.freq_hz == 446500000UL && st.is_dmr == 1 && st.color_code == 1 && st.timeslot == 2 && st.talkgroup == 3100L);

    /* 3. [rig] config save & reload round-trip */
    OykConfig* c1 = new OykConfig;
    OykConfig* c2 = new OykConfig;
    oyk_config_init_default(c1);
    c1->rig_enabled = 1;
    strcpy(c1->rig_protocol, "radiofoundry");
    strcpy(c1->rig_host, "192.168.7.1");
    c1->rig_port = 4191;
    c1->rig_poll_sec = 40;
    c1->rig_notify = 0;
    assert(oyk_config_save(c1, "trigcfg.cfg") == 1);
    char err[64];
    assert(oyk_config_load("trigcfg.cfg", c2, err, sizeof(err)) == 1);
    assert(c2->rig_enabled == 1 && strcmp(c2->rig_protocol, "radiofoundry") == 0);
    assert(strcmp(c2->rig_host, "192.168.7.1") == 0 && c2->rig_port == 4191);
    assert(c2->rig_poll_sec == 40 && c2->rig_notify == 0);
    remove("trigcfg.cfg");
    delete c2;
    delete c1;

    /* 4. USB CDC-ACM setup builders & CDC-NCM 16-bit wrap/unwrap */
    oyk_u8 setup[8], lcode[7];
    assert(oyk_usb_cdc_build_set_line_coding(115200UL, 0, 0, 8, setup, lcode) == 7);
    assert(setup[1] == OYK_USB_CDC_SET_LINE_CODING && lcode[6] == 8U);
    assert(oyk_usb_cdc_build_set_ctrl_line_state(1, 1, 0, setup) == 8 && setup[2] == 0x03U);

    oyk_u8 fake_eth[60];
    memset(fake_eth, 0x5A, sizeof(fake_eth));
    oyk_u8 ntb[128], unwrapped[128];
    size_t ntb_len = oyk_usb_ncm16_wrap(fake_eth, sizeof(fake_eth), 7, ntb, sizeof(ntb));
    assert(ntb_len == 28 + sizeof(fake_eth));
    size_t unw_len = oyk_usb_ncm16_unwrap(ntb, ntb_len, unwrapped, sizeof(unwrapped));
    assert(unw_len == sizeof(fake_eth) && memcmp(fake_eth, unwrapped, sizeof(fake_eth)) == 0);

    /* 5. Standalone DOS mini-stack: Ethernet UDP & PPP UDP round-trip (no mTCP!) */
    OykDosMiniNet* net = new OykDosMiniNet;
    TestMiniNetCap* cap = new TestMiniNetCap;
    memset(cap, 0, sizeof(*cap));
    oyk_u32 ip_a = 0, ip_b = 0;
    assert(oyk_parse_ipv4("192.168.7.2", &ip_a) == 1);
    assert(oyk_parse_ipv4("192.168.7.1", &ip_b) == 1);
    oyk_dos_mininet_init(net, ip_a, ip_b);
    net->tx_eth = test_mn_tx_eth;
    net->tx_eth_ctx = cap;
    net->udp_cb = test_mn_udp_rx;
    net->udp_ctx = cap;

    const char* rf_q = "select?zone=1&pos=2";
    assert(oyk_dos_mininet_send_udp(net, ip_b, 41910U, 4191U,
                                    reinterpret_cast<const oyk_u8*>(rf_q), strlen(rf_q)) == 1);
    assert(cap->eth_tx_len >= 60);
    oyk_dos_mininet_feed_eth(net, cap->eth_tx, cap->eth_tx_len);
    assert(cap->rx_dport == 4191U && strcmp(cap->rx_payload, rf_q) == 0);

    /* PPP HDLC + IPv4/UDP loopback */
    memset(cap, 0, sizeof(*cap));
    net->tx_eth = NULL;
    net->tx_byte = test_mn_tx_byte;
    net->tx_byte_ctx = cap;
    assert(oyk_dos_mininet_send_udp(net, ip_b, 41910U, 4191U,
                                    reinterpret_cast<const oyk_u8*>("select"), 6) == 1);
    assert(cap->ppp_tx_len > 10);
    for (size_t i = 0; i < cap->ppp_tx_len; ++i) {
        oyk_dos_mininet_feed_ppp_byte(net, cap->ppp_tx[i]);
    }
    assert(cap->rx_dport == 4191U && strcmp(cap->rx_payload, "select") == 0);

    delete cap;
    delete net;

    /* 6. allow_flash_logging config round-trip & file log suppression when running on flash */
    OykConfig* fcfg = new OykConfig;
    oyk_config_init_default(fcfg);
    assert(fcfg->allow_flash_logging == 0);
    fcfg->allow_flash_logging = 1;
    assert(oyk_config_save(fcfg, "tflog.cfg") == 1);
    OykConfig* fcfg2 = new OykConfig;
    assert(oyk_config_load("tflog.cfg", fcfg2, err, sizeof(err)) == 1);
    assert(fcfg2->allow_flash_logging == 1);
    remove("tflog.cfg");
    OykPortableNode* fnode = new OykPortableNode;
    assert(fnode->init(fcfg, mock_tx, NULL, err, sizeof(err)) == 1);
    fnode->set_storage_is_flash(1);
    assert(fnode->file_logging_allowed() == 1);
    fcfg2->allow_flash_logging = 0;
    OykConfigApplyResult f_res;
    fnode->apply_config(fcfg2, 0, &f_res);
    assert(fnode->file_logging_allowed() == 0);
    fnode->set_storage_is_flash(0);
    assert(fnode->file_logging_allowed() == 1);
    fnode->shutdown();
    delete fnode;
    delete fcfg2;
    delete fcfg;

    printf("[PASS] test_portable_rig_and_dos_usb\n");
}

struct TestPrnCap {
    int count;
    oyk_u32 last_level;
    char last_line[160];
};

static void test_prn_cb(oyk_u32 level, const char* line, void* ctx) {
    TestPrnCap* c = static_cast<TestPrnCap*>(ctx);
    c->count++;
    c->last_level = level;
    strncpy(c->last_line, line, sizeof(c->last_line) - 1);
    c->last_line[sizeof(c->last_line) - 1] = '\0';
}

static void test_portable_printer() {
    assert(oyk_printer_parse_protocol("generic") == OYK_PRN_GENERIC);
    assert(oyk_printer_parse_protocol("MX-80") == OYK_PRN_MX80);
    assert(oyk_printer_parse_protocol("esc/p2") == OYK_PRN_ESCP2);
    assert(oyk_printer_parse_protocol("unknown") == OYK_PRN_NONE);

    unsigned char buf[256];
    size_t n = oyk_printer_build_init(OYK_PRN_MX80, 1, buf, sizeof(buf));
    assert(n == 3 && buf[0] == 0x1BU && buf[1] == '@' && buf[2] == 0x0FU);
    n = oyk_printer_build_init(OYK_PRN_ESCP2, 0, buf, sizeof(buf));
    assert(n == 7 && buf[0] == 0x1BU && buf[1] == '@' && buf[5] == 0x1BU && buf[6] == 'M');
    assert(oyk_printer_build_init(OYK_PRN_GENERIC, 0, buf, sizeof(buf)) == 0);

    n = oyk_printer_format_line(OYK_PRN_GENERIC, OYK_LOG_INFO, "Hello\x07World", buf, sizeof(buf));
    assert(n == 13 && memcmp(buf, "Hello?World\r\n", 13) == 0);

    n = oyk_printer_format_line(OYK_PRN_MX80, OYK_LOG_ERROR, "Fail", buf, sizeof(buf));
    assert(n == 10 && memcmp(buf, "\x1B" "EFail" "\x1B" "F\r\n", 10) == 0);

    n = oyk_printer_format_line(OYK_PRN_ESCP2, OYK_LOG_BAN, "Ban!", buf, sizeof(buf));
    assert(n == 16 && memcmp(buf, "\x1B" "E\x1B-\x01" "Ban!\x1B-\x00\x1B" "F\r\n", 16) == 0);

    /* Config round-trip & printer callback independence from allow_flash_logging */
    OykConfig* cfg = new OykConfig;
    oyk_config_init_default(cfg);
    cfg->printer_enabled = 1;
    strcpy(cfg->printer_device, "LPT1:");
    strcpy(cfg->printer_protocol, "mx80");
    cfg->printer_baud = 19200UL;
    cfg->printer_level = OYK_LOG_ERROR | OYK_LOG_INFO | OYK_LOG_APPEAL;
    cfg->printer_condensed = 1;
    cfg->allow_flash_logging = 0;
    assert(oyk_config_save(cfg, "tprncfg.cfg") == 1);

    OykConfig* cfg2 = new OykConfig;
    char err[128];
    assert(oyk_config_load("tprncfg.cfg", cfg2, err, sizeof(err)) == 1);
    remove("tprncfg.cfg");
    assert(cfg2->printer_enabled == 1);
    assert(strcmp(cfg2->printer_device, "LPT1:") == 0);
    assert(strcmp(cfg2->printer_protocol, "mx80") == 0);
    assert(cfg2->printer_baud == 19200UL);
    assert(cfg2->printer_level == (OYK_LOG_ERROR | OYK_LOG_INFO | OYK_LOG_APPEAL));
    assert(cfg2->printer_condensed == 1);

    OykPortableNode* node = new OykPortableNode;
    TestPrnCap cap;
    memset(&cap, 0, sizeof(cap));
    node->set_printer_callback(test_prn_cb, &cap);
    assert(node->init(cfg2, mock_tx, NULL, err, sizeof(err)) == 1);
    node->set_storage_is_flash(1);
    assert(node->file_logging_allowed() == 0);

    cap.count = 0;
    node->log_message(OYK_LOG_DEBUG, "Suppressed debug");
    assert(cap.count == 0);
    node->log_message(OYK_LOG_ERROR, "Printed error");
    assert(cap.count == 1 && cap.last_level == OYK_LOG_ERROR);
    assert(strstr(cap.last_line, "Printed error") != NULL);
    node->log_appeal("K7TMP-1", "Please unban me");
    assert(cap.count == 2 && cap.last_level == OYK_LOG_APPEAL);

    node->shutdown();
    delete node;
    delete cfg2;
    delete cfg;
    printf("[PASS] test_portable_printer\n");
}

static void test_dos_video_dirty_diff_and_attrs() {
    oyk_u16* sim = new oyk_u16[OYK_DOS_VID_CELLS];
    memset(sim, 0, sizeof(oyk_u16) * OYK_DOS_VID_CELLS);
    OykDosVideo* v = new OykDosVideo;

    /* Monochrome MDA mode uses reverse 0x70 without bit-7 blink */
    dos_vid_init_sim(v, 0, sim);
    assert(v->vseg == 0xB000U && v->is_color == 0);
    assert(dos_vid_list_hdr_attr(v) == 0x70U);
    dos_vid_shutdown(v);

    /* Color CGA/VGA mode uses dark-grey background (0x80) + bright white (0x0F) = 0x8F */
    dos_vid_init_sim(v, 1, sim);
    assert(v->vseg == 0xB800U && v->is_color == 1);
    assert(dos_vid_list_hdr_attr(v) == 0x8FU);

    /* First flush paints all 2000 cells; repeating without edits writes 0 cells */
    oyk_u32 w1 = dos_vid_flush(v);
    assert(w1 == static_cast<oyk_u32>(OYK_DOS_VID_CELLS));
    assert(dos_vid_flush(v) == 0UL);

    /* Single-line box drawing & divider glyphs + minimal dirty diff */
    dos_vid_box(v, 2, 10, 4, 10, 0x03U, 0x07U, 0);
    dos_vid_hdiv(v, 4, 10, 10, 0x03U);
    dos_vid_flush(v);
    assert((sim[2 * 80 + 10] & 0xFFU) == OYK_BOX_TL);
    assert((sim[2 * 80 + 19] & 0xFFU) == OYK_BOX_TR);
    assert((sim[3 * 80 + 10] & 0xFFU) == OYK_BOX_V);
    assert((sim[4 * 80 + 10] & 0xFFU) == OYK_BOX_LT);
    assert((sim[4 * 80 + 15] & 0xFFU) == OYK_BOX_H);
    assert((sim[4 * 80 + 19] & 0xFFU) == OYK_BOX_RT);
    assert((sim[5 * 80 + 10] & 0xFFU) == OYK_BOX_BL);
    assert((sim[5 * 80 + 19] & 0xFFU) == OYK_BOX_BR);

    /* Changing 3 cells in one row flushes strictly 3 cells */
    dos_vid_puts(v, 10, 5, 0x0FU, "ABC", 3);
    assert(dos_vid_flush_row(v, 10) == 3UL);
    assert(dos_vid_flush(v) == 0UL);

    /* Drop shadow preserves underlying chars in dark grey (0x08), clips edges, is idempotent */
    dos_vid_shadow(v, 2, 10, 4, 10);
    dos_vid_flush(v);
    assert((sim[3 * 80 + 20] & 0xFF00U) == 0x0800U);
    assert((sim[6 * 80 + 12] & 0xFF00U) == 0x0800U);
    assert((sim[6 * 80 + 21] & 0xFF00U) == 0x0800U);
    assert((sim[6 * 80 + 11] & 0xFF00U) != 0x0800U);
    dos_vid_shadow(v, 2, 10, 4, 10);
    assert(dos_vid_flush(v) == 0UL);
    dos_vid_shadow(v, 20, 75, 10, 10);
    dos_vid_flush(v);
    dos_vid_shutdown(v);

    /* Dynamic sized text mode (e.g., 80x43 EGA/VGA) */
    oyk_u16* sim43 = new oyk_u16[80 * 43];
    dos_vid_init_sim_sized(v, 1, 80, 43, sim43);
    assert(v->cols == 80 && v->rows == 43 && v->cells == 80 * 43);
    dos_vid_puts(v, 42, 0, 0x70U, "ROW42", 5);
    assert(dos_vid_flush_row(v, 42) == 80UL);
    assert((sim43[42 * 80] & 0xFFU) == 'R');
    dos_vid_shutdown(v);
    delete[] sim43;
    delete v;
    delete[] sim;
    printf("[PASS] test_dos_video_dirty_diff_and_attrs\n");
}

static void test_derive_default_iface_id() {
    OykConfig* cfg = new OykConfig;
    oyk_config_init_default(cfg);
    cfg->iface_count = 0;

    char id[24];
    oyk_derive_default_iface_id(cfg, "axudp", id, sizeof(id));
    assert(strcmp(id, "axudp0") == 0);
    strcpy(cfg->ifaces[cfg->iface_count++].id, "AXUDP0");
    oyk_derive_default_iface_id(cfg, "axudp", id, sizeof(id));
    assert(strcmp(id, "axudp1") == 0);

    oyk_derive_default_iface_id(cfg, "axip", id, sizeof(id));
    assert(strcmp(id, "axip0") == 0);
    oyk_derive_default_iface_id(cfg, "kiss-serial", id, sizeof(id));
    assert(strcmp(id, "kiss0") == 0);
    oyk_derive_default_iface_id(cfg, "kiss-tcp", id, sizeof(id));
    assert(strcmp(id, "kisstcp0") == 0);
    oyk_derive_default_iface_id(cfg, "telnet", id, sizeof(id));
    assert(strcmp(id, "telnet0") == 0);
    oyk_derive_default_iface_id(cfg, "ppp", id, sizeof(id));
    assert(strcmp(id, "ppp0") == 0);
    oyk_derive_default_iface_id(cfg, "usb-cdc-net", id, sizeof(id));
    assert(strcmp(id, "usbnet0") == 0);
    strcpy(cfg->ifaces[cfg->iface_count++].id, "usbnet0");
    oyk_derive_default_iface_id(cfg, "usb-cdc-net", id, sizeof(id));
    assert(strcmp(id, "usbnet1") == 0);
    oyk_derive_default_iface_id(cfg, "usb-cdc-serial", id, sizeof(id));
    assert(strcmp(id, "usbser0") == 0);

    delete cfg;
    printf("[PASS] test_derive_default_iface_id\n");
}

static void test_format_peer_spec() {
    OykPeerEntry pe;
    char out[64];

    memset(&pe, 0, sizeof(pe));
    assert(oyk_parse_peer_spec("N0CALL-5@192.168.1.42:10094", pe.callsign, sizeof(pe.callsign),
                               &pe.ip, &pe.port, 10093) == 1);
    oyk_format_peer_spec(&pe, out, sizeof(out));
    assert(strcmp(out, "N0CALL-5@192.168.1.42:10094") == 0);

    memset(&pe, 0, sizeof(pe));
    assert(oyk_parse_peer_spec("K1ABC@10.0.0.2", pe.callsign, sizeof(pe.callsign),
                               &pe.ip, &pe.port, 10093) == 1);
    oyk_format_peer_spec(&pe, out, sizeof(out));
    assert(strcmp(out, "K1ABC@10.0.0.2:10093") == 0);

    memset(&pe, 0, sizeof(pe));
    assert(oyk_parse_peer_spec("44.1.2.3:10095", pe.callsign, sizeof(pe.callsign),
                               &pe.ip, &pe.port, 10093) == 1);
    oyk_format_peer_spec(&pe, out, sizeof(out));
    assert(strcmp(out, "44.1.2.3:10095") == 0);

    memset(&pe, 0, sizeof(pe));
    assert(oyk_parse_peer_spec("172.16.0.9", pe.callsign, sizeof(pe.callsign),
                               &pe.ip, &pe.port, 10093) == 1);
    oyk_format_peer_spec(&pe, out, sizeof(out));
    assert(strcmp(out, "172.16.0.9:10093") == 0);

    printf("[PASS] test_format_peer_spec\n");
}

static void test_portable_peer_sections_and_auto_learn() {
    const char* path = "t_peers.cfg";
    FILE* fp = fopen(path, "wb");
    assert(fp != NULL);
    fprintf(fp,
            "[node]\n"
            "callsign=K7PORT-7\n"
            "welcome=Standard port\n"
            "new_user_welcome=Welcome new op!\n"
            "learned_peer_policy=inbound\n\n"
            "[interface axudp0]\n"
            "type=axudp\n"
            "bind_host=127.0.0.1\n"
            "bind_port=10093\n"
            "peer=K1LEG@10.1.2.3:10094\n\n"
            "[peer remote0]\n"
            "callsign=R3MOT-7\n"
            "description=Remote PoPT\n"
            "interface=axudp0\n"
            "host=12.34.56.78\n"
            "allow_inbound=false\n"
            "allow_outbound=true\n");
    fclose(fp);

    OykConfig* cfg = new OykConfig;
    char err[64];
    assert(oyk_config_load(path, cfg, err, sizeof(err)) == 1);
    assert(cfg->learned_peer_policy == OYK_LEARN_INBOUND);
    assert(strcmp(cfg->new_user_welcome, "Welcome new op!") == 0);
    assert(cfg->peer_count == 2);
    assert(strcmp(cfg->peers[0].id, "remote0") == 0);
    assert(strcmp(cfg->peers[0].callsign, "R3MOT-7") == 0);
    assert(cfg->peers[0].allow_inbound == 0);
    assert(cfg->peers[0].allow_outbound == 1);
    assert(strcmp(cfg->peers[1].callsign, "K1LEG") == 0);

    OykPortableNode* node = new OykPortableNode;
    assert(node->init(cfg, NULL, NULL, err, sizeof(err)) == 1);

    /* Inbound SABM from R3MOT-7 (allow_inbound=0) yields DM */
    OykFrame sabm_den;
    memset(&sabm_den, 0, sizeof(sabm_den));
    sabm_den.dst = cfg->callsign;
    assert(oyk_parse_callsign("R3MOT-7", &sabm_den.src));
    sabm_den.is_command = 1;
    sabm_den.control = 0x3FU;
    node->on_rx_frame(0, &sabm_den, 0x0C22384EUL, 10093, 0);

    /* Inbound SABM from N2NEW auto-learns and sends new_user_welcome */
    OykFrame sabm_new;
    memset(&sabm_new, 0, sizeof(sabm_new));
    sabm_new.dst = cfg->callsign;
    assert(oyk_parse_callsign("N2NEW", &sabm_new.src));
    sabm_new.is_command = 1;
    sabm_new.control = 0x3FU;
    node->on_rx_frame(0, &sabm_new, 0x7F000001UL, 10099, 0);
    assert(node->auto_learned_peer_count() == 1);

    assert(node->persist_auto_learned_peers(err, sizeof(err)) == 1);
    assert(node->auto_learned_peer_count() == 0);

    OykConfig* reloaded = new OykConfig;
    assert(oyk_config_load(path, reloaded, err, sizeof(err)) == 1);
    assert(reloaded->peer_count == 3);
    delete reloaded;

    /* Another auto-learn then discard */
    assert(oyk_parse_callsign("K9TMP", &sabm_new.src));
    node->on_rx_frame(0, &sabm_new, 0x7F000001UL, 10098, 0);
    assert(node->auto_learned_peer_count() == 1);
    node->discard_auto_learned_peers();
    assert(node->auto_learned_peer_count() == 0);

    delete node;
    delete cfg;
    remove(path);
    printf("[PASS] test_portable_peer_sections_and_auto_learn\n");
}

static void test_portable_iface_directions_netmask_and_dmr() {
    oyk_u32 did = 0;
    assert(oyk_is_all_digits("3112345") == 1);
    assert(oyk_is_all_digits("12.0.0.1") == 0);
    assert(oyk_parse_dmr_id("1", &did) == 1 && did == 1UL);
    assert(oyk_parse_dmr_id("3112345", &did) == 1 && did == 3112345UL);
    assert(oyk_parse_dmr_id("16777214", &did) == 1 && did == 16777214UL);
    assert(oyk_parse_dmr_id("0", &did) == 0);
    assert(oyk_parse_dmr_id("16777215", &did) == 0);

    char ipbuf[24];
    oyk_dmr_id_to_ipv4(3112345UL, 44U, ipbuf, sizeof(ipbuf));
    assert(strcmp(ipbuf, "44.47.125.153") == 0);

    const char* bad_path = "t_baddmr.cfg";
    {
        FILE* bfp = fopen(bad_path, "wb");
        assert(bfp != NULL);
        fprintf(bfp,
                "[node]\ncallsign = K7DMR-7\n"
                "[interface ax0]\ntype = axudp\nbind_host = 44.0.0.1\nnetmask = 255.0.0.0\n"
                "[peer p0]\ncallsign = W1BAD\ninterface = ax0\nhost = 16777215\n");
        fclose(bfp);
    }
    OykConfig* cfg = new OykConfig;
    char err[128];
    assert(oyk_config_load(bad_path, cfg, err, sizeof(err)) == 0);
    remove(bad_path);

    const char* path = "t_dmr.cfg";
    {
        FILE* gfp = fopen(path, "wb");
        assert(gfp != NULL);
        fprintf(gfp,
                "[node]\ncallsign = K7DMR-7\n"
                "[interface ax_dmr]\n"
                "type = axudp\n"
                "bind_host = 44.0.0.1\n"
                "bind_port = 10093\n"
                "netmask = 255.0.0.0\n"
                "allow_inbound = true\n"
                "allow_outbound = true\n"
                "[interface ax_rxonly]\n"
                "type = axudp\n"
                "bind_host = 127.0.0.1\n"
                "bind_port = 10094\n"
                "allow_inbound = true\n"
                "allow_outbound = false\n"
                "[interface tel0]\n"
                "type = telnet\n"
                "bind_host = 0.0.0.0\n"
                "bind_port = 8010\n"
                "allow_inbound = true\n"
                "allow_outbound = true\n"
                "[peer pdmr]\n"
                "callsign = W1DMR\n"
                "interface = ax_dmr\n"
                "host = 3112345\n"
                "allow_inbound = true\n"
                "allow_outbound = true\n"
                "[peer pbadnet]\n"
                "callsign = W1OUT\n"
                "interface = ax_dmr\n"
                "host = 10.0.0.5\n"
                "allow_inbound = true\n"
                "allow_outbound = true\n");
        fclose(gfp);
    }
    assert(oyk_config_load(path, cfg, err, sizeof(err)) == 1);
    assert(cfg->iface_count == 3);
    assert(cfg->ifaces[0].block_inbound == 0 && cfg->ifaces[0].block_outbound == 0);
    assert(cfg->ifaces[1].block_inbound == 0 && cfg->ifaces[1].block_outbound == 1);
    assert(cfg->ifaces[2].block_outbound == 1); /* telnet hard-locked off */
    assert(oyk_dmr_prefix_for_iface(&cfg->ifaces[0]) == 44U);

    oyk_u32 net_dmr_ip = 0, bad_net_ip = 0;
    assert(oyk_parse_ipv4("44.47.125.153", &net_dmr_ip) == 1);
    assert(oyk_parse_ipv4("10.0.0.5", &bad_net_ip) == 1);
    assert(cfg->peers[0].resolved_ip == net_dmr_ip);

    OykPortableNode* node = new OykPortableNode;
    assert(node->init(cfg, NULL, NULL, err, sizeof(err)) == 1);

    /* Inbound frame outside ax_dmr netmask (10.0.0.5) is dropped */
    OykFrame sabm;
    memset(&sabm, 0, sizeof(sabm));
    sabm.dst = cfg->callsign;
    assert(oyk_parse_callsign("W2TEST", &sabm.src));
    sabm.is_command = 1;
    sabm.control = 0x3FU;
    node->on_rx_frame(0, &sabm, bad_net_ip, 10093, 0);
    int act_cnt = 0;
    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        if (node->session_slot(i).state != OYK_SESS_FREE) ++act_cnt;
    }
    assert(act_cnt == 0);

    /* Inbound frame inside ax_dmr netmask (44.47.125.153) accepted */
    node->on_rx_frame(0, &sabm, net_dmr_ip, 10093, 0);
    act_cnt = 0;
    for (int i = 0; i < OYK_MAX_SESSIONS; ++i) {
        if (node->session_slot(i).state != OYK_SESS_FREE) ++act_cnt;
    }
    assert(act_cnt == 1);

    /* Outbound local terminal: blocked on ax_rxonly & tel0, blocked outside netmask, allowed on pdmr */
    oyk_u32 lsid = 0;
    assert(node->open_local_terminal(1, "W2ANY", &lsid, NULL, NULL, err, sizeof(err)) == 0);
    assert(node->open_local_terminal(2, "W2ANY", &lsid, NULL, NULL, err, sizeof(err)) == 0);
    assert(node->open_local_terminal(0, "W1OUT", &lsid, NULL, NULL, err, sizeof(err)) == 0);
    assert(node->open_local_terminal(0, "W1DMR", &lsid, NULL, NULL, err, sizeof(err)) == 1);
    node->terminate_session(lsid, err, sizeof(err));

    delete node;
    delete cfg;
    remove(path);
    printf("[PASS] test_portable_iface_directions_netmask_and_dmr\n");
}

static void tp_write_str(const char* path, const char* txt) {
    FILE* f = fopen(path, "wb");
    assert(f != NULL);
    fwrite(txt, 1, strlen(txt), f);
    fclose(f);
}

static void tp_read_str(const char* path, char* buf, size_t cap) {
    FILE* f = fopen(path, "rb");
    assert(f != NULL);
    size_t n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    fclose(f);
}

static void test_portable_config_include_storage_modes() {
    char* buf1 = new char[8192];
    char* buf2 = new char[8192];
    char* buf3 = new char[8192];
    OykConfig* cfg = new OykConfig();
    char err[96];
    assert(buf1 && buf2 && buf3 && cfg);

    /* 1. All inline */
    tp_write_str("ti_m1.cfg",
        "[node]\ncallsign = KR4OYK-7\n\n"
        "[interface axudp0]\ntype = axudp\nbind_port = 10093\n\n"
        "[peer p0]\ncallsign = W1AW-7\ninterface = axudp0\nhost = 127.0.0.1\n");
    assert(oyk_config_load("ti_m1.cfg", cfg, err, sizeof(err)));
    assert(cfg->interfaces_file[0] == '\0' && cfg->peers_file[0] == '\0');
    assert(cfg->iface_count == 1 && cfg->peer_count == 1);
    assert(oyk_config_save(cfg, "ti_m1.cfg"));
    tp_read_str("ti_m1.cfg", buf1, 8192);
    assert(strstr(buf1, "[interface axudp0]") != NULL);
    assert(strstr(buf1, "[peer p0]") != NULL);
    remove("ti_m1.cfg");

    /* 2. Separate peers only (consolidating mixed inline + external peers) */
    tp_write_str("ti_m2.cfg",
        "[node]\ncallsign = KR4OYK-7\npeers = tp_m2.cfg\n\n"
        "[interface axudp0]\ntype = axudp\nbind_port = 10093\n\n"
        "[peer pinl]\ncallsign = N0INL-1\ninterface = axudp0\nhost = 10.0.0.2\n");
    tp_write_str("tp_m2.cfg",
        "[peer pext]\ncallsign = N0EXT-2\ninterface = axudp0\nhost = 10.0.0.3\n");
    assert(oyk_config_load("ti_m2.cfg", cfg, err, sizeof(err)));
    assert(cfg->iface_count == 1 && cfg->peer_count == 2);
    assert(cfg->interfaces_file[0] == '\0' && strcmp(cfg->peers_file, "tp_m2.cfg") == 0);
    assert(oyk_config_save(cfg, "ti_m2.cfg"));
    tp_read_str("ti_m2.cfg", buf1, 8192);
    tp_read_str("tp_m2.cfg", buf2, 8192);
    assert(strstr(buf1, "[interface axudp0]") != NULL && strstr(buf1, "[peer ") == NULL);
    assert(strstr(buf2, "[peer pinl]") != NULL && strstr(buf2, "[peer pext]") != NULL);
    assert(strstr(buf2, "[interface ") == NULL);
    assert(oyk_config_load("ti_m2.cfg", cfg, err, sizeof(err)));
    assert(cfg->iface_count == 1 && cfg->peer_count == 2);
    remove("ti_m2.cfg");
    remove("tp_m2.cfg");

    /* 3. Separate interfaces only (consolidating mixed inline + external ifaces) */
    tp_write_str("ti_m3.cfg",
        "[node]\ncallsign = KR4OYK-7\ninterfaces = tf_m3.cfg\n\n"
        "[interface iinl]\ntype = axudp\nbind_port = 10093\n\n"
        "[peer p0]\ncallsign = W1AW-7\ninterface = iinl\nhost = 127.0.0.1\n");
    tp_write_str("tf_m3.cfg",
        "[interface iext]\ntype = telnet\nbind_port = 8010\n");
    assert(oyk_config_load("ti_m3.cfg", cfg, err, sizeof(err)));
    assert(cfg->iface_count == 2 && cfg->peer_count == 1);
    assert(strcmp(cfg->interfaces_file, "tf_m3.cfg") == 0 && cfg->peers_file[0] == '\0');
    assert(oyk_config_save(cfg, "ti_m3.cfg"));
    tp_read_str("ti_m3.cfg", buf1, 8192);
    tp_read_str("tf_m3.cfg", buf2, 8192);
    assert(strstr(buf1, "[peer p0]") != NULL && strstr(buf1, "[interface ") == NULL);
    assert(strstr(buf2, "[interface iinl]") != NULL && strstr(buf2, "[interface iext]") != NULL);
    assert(strstr(buf2, "[peer ") == NULL);
    assert(oyk_config_load("ti_m3.cfg", cfg, err, sizeof(err)));
    assert(cfg->iface_count == 2 && cfg->peer_count == 1);
    remove("ti_m3.cfg");
    remove("tf_m3.cfg");

    /* 4. Both separate (missing files load empty, created on save) */
    remove("tf_m4.cfg");
    remove("tp_m4.cfg");
    tp_write_str("ti_m4.cfg",
        "[node]\ncallsign = KR4OYK-7\ninterfaces = tf_m4.cfg\npeers = tp_m4.cfg\n");
    assert(oyk_config_load("ti_m4.cfg", cfg, err, sizeof(err)));
    assert(cfg->iface_count == 0 && cfg->peer_count == 0);
    cfg->iface_count = 1;
    memset(&cfg->ifaces[0], 0, sizeof(cfg->ifaces[0]));
    strcpy(cfg->ifaces[0].id, "axudp0");
    strcpy(cfg->ifaces[0].type, "axudp");
    strcpy(cfg->ifaces[0].host, "0.0.0.0");
    cfg->ifaces[0].port = 10093;
    cfg->peer_count = 1;
    memset(&cfg->peers[0], 0, sizeof(cfg->peers[0]));
    strcpy(cfg->peers[0].id, "rem0");
    strcpy(cfg->peers[0].callsign, "R3MOT-7");
    strcpy(cfg->peers[0].interface_id, "axudp0");
    strcpy(cfg->peers[0].host, "12.34.56.78");
    cfg->peers[0].allow_inbound = 1;
    cfg->peers[0].allow_outbound = 1;
    assert(oyk_config_save(cfg, "ti_m4.cfg"));
    tp_read_str("ti_m4.cfg", buf1, 8192);
    tp_read_str("tf_m4.cfg", buf2, 8192);
    tp_read_str("tp_m4.cfg", buf3, 8192);
    assert(strstr(buf1, "[interface ") == NULL && strstr(buf1, "[peer ") == NULL);
    assert(strstr(buf2, "[interface axudp0]") != NULL && strstr(buf2, "[peer ") == NULL);
    assert(strstr(buf3, "[peer rem0]") != NULL && strstr(buf3, "[interface ") == NULL);
    assert(oyk_config_load("ti_m4.cfg", cfg, err, sizeof(err)));
    assert(cfg->iface_count == 1 && cfg->peer_count == 1);
    remove("ti_m4.cfg");
    remove("tf_m4.cfg");
    remove("tp_m4.cfg");

    /* 5. Both combined into the same external file (and verify reload doesn't duplicate) */
    tp_write_str("ti_m5.cfg",
        "[node]\ncallsign = KR4OYK-7\ninterfaces = tc_m5.cfg\npeers = tc_m5.cfg\n\n"
        "[interface i0]\ntype = axudp\nbind_port = 10093\n"
        "[peer p0]\ncallsign = K1A-1\ninterface = i0\nhost = 127.0.0.1\n");
    tp_write_str("tc_m5.cfg",
        "[interface i1]\ntype = telnet\nbind_port = 8010\n"
        "[peer p1]\ncallsign = K1B-2\ninterface = i0\nhost = 127.0.0.2\n");
    assert(oyk_config_load("ti_m5.cfg", cfg, err, sizeof(err)));
    assert(cfg->iface_count == 2 && cfg->peer_count == 2);
    assert(oyk_config_save(cfg, "ti_m5.cfg"));
    tp_read_str("ti_m5.cfg", buf1, 8192);
    tp_read_str("tc_m5.cfg", buf2, 8192);
    assert(strstr(buf1, "[interface ") == NULL && strstr(buf1, "[peer ") == NULL);
    assert(strstr(buf2, "[interface i0]") != NULL && strstr(buf2, "[interface i1]") != NULL);
    assert(strstr(buf2, "[peer p0]") != NULL && strstr(buf2, "[peer p1]") != NULL);
    assert(oyk_config_load("ti_m5.cfg", cfg, err, sizeof(err)));
    assert(cfg->iface_count == 2 && cfg->peer_count == 2);
    remove("ti_m5.cfg");
    remove("tc_m5.cfg");

    delete cfg;
    delete[] buf3;
    delete[] buf2;
    delete[] buf1;
    printf("[PASS] test_portable_config_include_storage_modes\n");
}

static int txlog_contains(const MockTxLog* log, const char* needle) {
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

static void test_portable_same_interface_policy() {
    const char* p1 = "tsame1.cfg";
    const char* p2 = "tsame2.cfg";
    tp_write_str(p1,
                 "[node]\n"
                 "callsign = K7SAME-7\n\n"
                 "[interface ax0]\n"
                 "type = axudp\n"
                 "bind_host = 127.0.0.1\n"
                 "bind_port = 10093\n\n"
                 "[interface ax1]\n"
                 "type = axudp\n"
                 "bind_host = 127.0.0.1\n"
                 "bind_port = 10094\n"
                 "allow_same_interface = true\n\n"
                 "[interface tel0]\n"
                 "type = telnet\n"
                 "bind_host = 127.0.0.1\n"
                 "bind_port = 8010\n"
                 "allow_same_interface = true\n\n"
                 "[peer p0]\n"
                 "callsign = W2DST\n"
                 "interface = ax0\n"
                 "host = 127.0.0.1\n"
                 "port = 10099\n\n"
                 "[peer p1]\n"
                 "callsign = W2DST\n"
                 "interface = ax1\n"
                 "host = 127.0.0.1\n"
                 "port = 10100\n");

    OykConfig* cfg = new OykConfig();
    OykConfig* reloaded = new OykConfig();
    MockTxLog* txlog = new MockTxLog();
    OykPortableNode* node = new OykPortableNode();
    OykFrame* f = new OykFrame();
    char err[96];
    assert(cfg && reloaded && txlog && node && f);

    assert(oyk_config_load(p1, cfg, err, sizeof(err)) == 1);
    assert(cfg->iface_count == 3);
    assert(cfg->ifaces[0].allow_same_interface == 0);
    assert(cfg->ifaces[1].allow_same_interface == 1);
    assert(cfg->ifaces[2].allow_same_interface == 0);

    assert(oyk_config_save(cfg, p2) == 1);
    assert(oyk_config_load(p2, reloaded, err, sizeof(err)) == 1);
    assert(reloaded->iface_count == 3);
    assert(reloaded->ifaces[0].allow_same_interface == 0);
    assert(reloaded->ifaces[1].allow_same_interface == 1);
    assert(reloaded->ifaces[2].allow_same_interface == 0);

    memset(txlog, 0, sizeof(*txlog));
    assert(node->init(cfg, mock_tx, txlog, err, sizeof(err)) == 1);
    assert(node->can_forward_between_ifaces(0, 0) == 0);
    assert(node->can_forward_between_ifaces(0, 1) == 1);
    assert(node->can_forward_between_ifaces(1, 1) == 1);
    assert(node->can_forward_between_ifaces(-1, 0) == 1);

    /* Remote session on ax0: CONNECT ax0 blocked, CONNECT ax1 permitted */
    memset(f, 0, sizeof(*f));
    f->dst = cfg->callsign;
    assert(oyk_parse_callsign("W2SRC", &f->src) == 1);
    f->is_command = 1;
    f->control = 0x3FU;
    node->on_rx_frame(0, f, 0x7F000001UL, 10093, 0);

    memset(f, 0, sizeof(*f));
    f->dst = cfg->callsign;
    assert(oyk_parse_callsign("W2SRC", &f->src) == 1);
    f->is_command = 1;
    f->control = 0x00U;
    f->has_pid = 1;
    f->pid = 0xF0U;
    const char* same_cmd = "CONNECT ax0 W2DST\r";
    f->info_len = static_cast<oyk_u16>(strlen(same_cmd));
    memcpy(f->info, same_cmd, f->info_len);
    txlog->count = 0;
    node->on_rx_frame(0, f, 0x7F000001UL, 10093, 0);
    assert(txlog_contains(txlog, "same-interface forwarding") == 1);

    const char* cross_cmd = "CONNECT ax1 W2DST\r";
    f->control = 0x02U; /* N(S)=1 */
    f->info_len = static_cast<oyk_u16>(strlen(cross_cmd));
    memcpy(f->info, cross_cmd, f->info_len);
    txlog->count = 0;
    node->on_rx_frame(0, f, 0x7F000001UL, 10093, 0);
    assert(txlog_contains(txlog, "same-interface forwarding") == 0);
    assert(txlog_contains(txlog, "Connecting to W2DST via ax1") == 1);

    delete f;
    delete node;
    delete txlog;
    delete reloaded;
    delete cfg;
    remove(p1);
    remove(p2);
    printf("[PASS] test_portable_same_interface_policy\n");
}

static void test_portable_theme_config_and_cloning() {
    OykConfig* cfg = new OykConfig();
    OykConfig* reloaded = new OykConfig();
    oyk_config_init_default(cfg);

    assert(oyk_theme_is_builtin("dark") == 1);
    assert(oyk_theme_is_builtin("light") == 1);
    assert(oyk_theme_is_builtin("native") == 1);
    assert(oyk_theme_is_builtin("custom-dark") == 0);

    oyk_u32 parsed = 0;
    assert(oyk_theme_parse_hex("#38bdf8", &parsed) == 1);
    assert(parsed == 0x38BDF8UL);
    assert(oyk_theme_parse_hex("bad", &parsed) == 0);

    char name[24];
    oyk_theme_make_clone_name(cfg, 1, name, sizeof(name));
    assert(strcmp(name, "custom-dark") == 0);
    oyk_theme_init_builtin(&cfg->themes[cfg->theme_count++], 0);
    strcpy(cfg->themes[0].name, "custom-dark");
    cfg->themes[0].colors[OYK_TR_ACCENT] = 0xFF8800UL;
    oyk_theme_make_clone_name(cfg, 1, name, sizeof(name));
    assert(strcmp(name, "custom-dark-2") == 0);
    oyk_theme_make_clone_name(cfg, 0, name, sizeof(name));
    assert(strcmp(name, "custom-light") == 0);

    /* Inject built-in name into themes[] to verify oyk_config_save never writes built-ins */
    oyk_theme_init_builtin(&cfg->themes[cfg->theme_count++], 1);
    strcpy(cfg->theme_name, "custom-dark");
    strcpy(cfg->themes_file, "tth.cfg");

    const char* main_p = OYK_T_CFG_MIXED;
    assert(oyk_config_save(cfg, main_p) == 1);
    char th_p[64];
    oyk_config_resolve_themes(cfg, th_p, sizeof(th_p));

    char err[96];
    assert(oyk_config_load(main_p, reloaded, err, sizeof(err)) == 1);
    assert(strcmp(reloaded->theme_name, "custom-dark") == 0);
    assert(strcmp(reloaded->themes_file, "tth.cfg") == 0);
    assert(reloaded->theme_count == 1);
    assert(strcmp(reloaded->themes[0].name, "custom-dark") == 0);
    assert(reloaded->themes[0].colors[OYK_TR_ACCENT] == 0xFF8800UL);

    remove(main_p);
    remove(th_p);
    delete reloaded;
    delete cfg;
    printf("[PASS] test_portable_theme_config_and_cloning\n");
}

#if !defined(__DOS__) || defined(OYK_DOS_GFX_BUILD)
static void test_dos_gfx_renderer_and_smooth_scroll() {
    assert(oyk_dos_is_386_or_newer() == 1);
    assert(dos_gfx_parse_video_arg("auto") == OYK_GFX_MODE_NONE);
    assert(dos_gfx_parse_video_arg("vbe24") == OYK_GFX_MODE_VBE24);
    assert(dos_gfx_parse_video_arg("vbe16") == OYK_GFX_MODE_VBE16);
    assert(dos_gfx_parse_video_arg("vbe8") == OYK_GFX_MODE_VBE8);
    assert(dos_gfx_parse_video_arg("800x600") == OYK_GFX_MODE_VBE800);
    assert(dos_gfx_parse_video_arg("1024x768") == OYK_GFX_MODE_VBE1024);
    assert(dos_gfx_parse_video_arg("vga") == OYK_GFX_MODE_VGA16);
    assert(dos_gfx_parse_video_arg("ega") == OYK_GFX_MODE_EGA16);
    assert(dos_gfx_parse_video_arg("bogus") == -1);

    /* XMS / EMS / host sim extended memory odd-length roundtrip */
    OykDosExtMem xm;
    int xm_ok = xm.init(32768UL);
#ifndef __DOS__
    assert(xm_ok == 1);
#endif
    if (xm_ok) {
        assert(xm.available() == 1);
        const char pat[] = "OYK-XMS-UNALIGNED-TEST-PATTERN-386";
        char rbuf[48];
        memset(rbuf, 0, sizeof(rbuf));
        assert(xm.write_block(17UL, pat, static_cast<oyk_u16>(strlen(pat) + 1)) == 1);
        assert(xm.read_block(17UL, rbuf, static_cast<oyk_u16>(strlen(pat) + 1)) == 1);
        assert(strcmp(rbuf, pat) == 0);
        xm.shutdown();
        assert(xm.available() == 0);
    } else {
        assert(xm.available() == 0);
    }

    /* Font glyphs in 8x16 and EGA 8x14 (both copy helper and O(1) direct ptr) */
    OykDosFont fnt;
    oyk_dos_font_init(&fnt);
    oyk_u8 g16[16], g14[14];
    oyk_dos_font_get_glyph(&fnt, 16, 'K', g16);
    oyk_dos_font_get_glyph(&fnt, 14, OYK_BOX_TL, g14);
    assert(memcmp(g16, oyk_dos_font_glyph_ptr(&fnt, 16, 'K'), 16) == 0);
    assert(memcmp(g14, oyk_dos_font_glyph_ptr(&fnt, 14, OYK_BOX_TL), 14) == 0);
    int nonzero_16 = 0, nonzero_14 = 0;
    for (int i = 0; i < 16; ++i) if (g16[i] != 0) ++nonzero_16;
    for (int i = 0; i < 14; ++i) if (g14[i] != 0) ++nonzero_14;
    assert(nonzero_16 > 4 && nonzero_14 > 3);
    oyk_dos_font_shutdown(&fnt);

#ifndef __DOS__
    /* Graphical renderer sim: 640x480 fills 80x30 + palette cache skip + 800x600 100x37 + EGA */
    OykDosGfx* g = new OykDosGfx;
    oyk_u16* v1 = new oyk_u16[80 * 30];
    oyk_u16* v2 = new oyk_u16[80 * 30];
    OykDosVideo* vid = new OykDosVideo;
    assert(dos_gfx_init_sim(g, OYK_GFX_MODE_NONE) == 1);
    assert(g->mode == OYK_GFX_MODE_VBE8 && g->width == 640 && g->height == 480 && g->cols == 80 && g->rows == 30);
    OykThemeConfig tc;
    oyk_theme_init_builtin(&tc, 0);
    dos_gfx_set_theme(g, &tc, 0);
    oyk_u32 pw_before = g->hw_palette_writes;
    dos_gfx_set_theme(g, &tc, 0); /* Identical theme must skip redundant palette writes */
    assert(g->hw_palette_writes == pw_before);

    dos_vid_init_sim_sized(vid, 0, 80, 30, v1);
    dos_vid_clear(vid, 0x07U);
    dos_vid_puts(vid, 29, 1, 0x0EU, "BOTTOM_ROW_29", 13);
    dos_vid_box(vid, 4, 10, 6, 30, 0x59U, 0x58U, 1);
    dos_gfx_add_shadow(g, 4, 10, 6, 30);
    dos_vid_puts(vid, 6, 12, 0x58U, "Line A", 12);
    dos_vid_flush(vid);
    dos_gfx_present(g, v1, 1);

    /* Verify pixel alpha drop shadow right of box (col 40, row 5 => 50% darker than BG) */
    const oyk_u8* spx = g->sim_rgb + ((5 * 16 + 4) * 640 + (40 * 8 + 2)) * 3;
    oyk_u8 bg_r = static_cast<oyk_u8>((tc.colors[OYK_TR_BG] >> 16) & 0xFFUL);
    assert(spx[0] <= (bg_r / 2 + 1));

    memcpy(v2, v1, sizeof(oyk_u16) * (80 * 30));
    dos_vid_puts(vid, 6, 12, 0x58U, "Line B", 12);
    dos_vid_flush(vid);
    int steps = dos_gfx_smooth_scroll(g, v2, v1, 5, 11, 4, 28, 1);
    assert(steps >= 3);

    /* Verify live theme preview repaints BOTH background console & foreground modal on every switch */
    OykThemeConfig pal_light;
    oyk_theme_init_builtin(&pal_light, 1);
    OykThemeConfig pal_native;
    oyk_theme_init_dos_native(&pal_native);
    OykThemeConfig pal_custom = tc;
    pal_custom.colors[OYK_TR_BG] = 0x283448UL;
    pal_custom.colors[OYK_TR_PANEL_BG] = 0x384858UL;

    dos_gfx_present(g, v1, 0);
    const size_t bg_off = ((2 * 16 + 2) * 640 + (2 * 8 + 2)) * 3U;
    const size_t mdl_off = ((6 * 16 + 2) * 640 + (15 * 8 + 2)) * 3U;
    oyk_u8 bg_dark[3] = { g->sim_rgb[bg_off], g->sim_rgb[bg_off + 1], g->sim_rgb[bg_off + 2] };
    oyk_u8 mdl_dark[3] = { g->sim_rgb[mdl_off], g->sim_rgb[mdl_off + 1], g->sim_rgb[mdl_off + 2] };

    /* 1. dark -> light (with force_all = 0: palette_dirty must repaint entire screen) */
    dos_gfx_set_theme(g, &pal_light, 0);
    dos_gfx_present(g, v1, 0);
    assert(memcmp(&g->sim_rgb[bg_off], bg_dark, 3) != 0);
    assert(memcmp(&g->sim_rgb[mdl_off], mdl_dark, 3) != 0);
    oyk_u8 bg_light[3] = { g->sim_rgb[bg_off], g->sim_rgb[bg_off + 1], g->sim_rgb[bg_off + 2] };
    oyk_u8 mdl_light[3] = { g->sim_rgb[mdl_off], g->sim_rgb[mdl_off + 1], g->sim_rgb[mdl_off + 2] };

    /* 2. light -> native */
    dos_gfx_set_theme(g, &pal_native, 1);
    dos_gfx_present(g, v1, 0);
    assert(memcmp(&g->sim_rgb[bg_off], bg_light, 3) != 0);
    assert(memcmp(&g->sim_rgb[mdl_off], mdl_light, 3) != 0);

    /* 3. native -> custom */
    dos_gfx_set_theme(g, &pal_custom, 0);
    dos_gfx_present(g, v1, 0);
    assert(g->sim_rgb[bg_off] == 0x28U && g->sim_rgb[bg_off + 1] == 0x34U && g->sim_rgb[bg_off + 2] == 0x48U);
    assert(g->sim_rgb[mdl_off] == 0x38U && g->sim_rgb[mdl_off + 1] == 0x48U && g->sim_rgb[mdl_off + 2] == 0x58U);

    assert(dos_gfx_write_bmp(g, "/tmp/oyk_test_dos_gfx.bmp") == 1);
    remove("/tmp/oyk_test_dos_gfx.bmp");

    dos_vid_shutdown(vid);
    dos_gfx_shutdown(g);

    /* 800x600 fills 100x37 cells */
    assert(dos_gfx_init_sim(g, OYK_GFX_MODE_VBE800) == 1);
    assert(g->width == 800 && g->height == 600 && g->cols == 100 && g->rows == 37);
    dos_gfx_shutdown(g);

    /* EGA 640x350 8x14 fills 80x25 cells */
    assert(dos_gfx_init_sim(g, OYK_GFX_MODE_EGA16) == 1);
    assert(g->width == 640 && g->height == 350 && g->char_h == 14 && g->cols == 80 && g->rows == 25);
    dos_gfx_shutdown(g);

    delete vid;
    delete[] v2;
    delete[] v1;
    delete g;
#endif
    printf("[PASS] test_dos_gfx_renderer_and_smooth_scroll\n");
}
#endif

#ifndef __DOS__
static void dos_save_capture_prefixed(const char* prefix, const char* name,
                                      const OykDosVideo* v, const OykDosGfx* gfx) {
    const char* dir = getenv("OYK_UI_CAPTURE_DIR");
    if (!dir || !dir[0] || !v || !v->back || v->cells <= 0) return;
    char path[512];
    sprintf(path, "%s/%s_%s.dosbuf", dir, prefix, name);
    FILE* f = fopen(path, "wb");
    if (f) {
        size_t nbytes = static_cast<size_t>(v->cells) * 2U;
        unsigned char* raw = new unsigned char[nbytes];
        for (int i = 0; i < v->cells; ++i) {
            raw[i * 2]     = static_cast<unsigned char>(v->back[i] & 0xFFU);
            raw[i * 2 + 1] = static_cast<unsigned char>((v->back[i] >> 8) & 0xFFU);
        }
        fwrite(raw, 1, nbytes, f);
        delete[] raw;
        fclose(f);
    }
    if (gfx && gfx->active) {
        sprintf(path, "%s/%s_%s.bmp", dir, prefix, name);
        dos_gfx_write_bmp(gfx, path);
    }
}

struct DosMockPalette {
    oyk_u8 bg, hdr, mbar, msel, ok, lhdr, stat;
    oyk_u8 bdr, fill, sel, inp, bar;
};

static void dos_render_mock_main_pal(OykDosVideo* v, OykDosGfx* g, const DosMockPalette& p) {
    if (g) {
        dos_gfx_clear_shadows(g);
        dos_gfx_clear_cell_rgb(g);
    }
    OykTuiConsoleLayout lay;
    oyk_tui_compute_console_layout(v->rows, v->cols, &lay);
    const int ncols = lay.cols;

    dos_vid_clear(v, p.bg);
    dos_vid_hline(v, lay.menu_row, 0, ncols, p.mbar, ' ');
    dos_vid_puts(v, lay.menu_row, 1, p.msel, " Node ", 6);
    dos_vid_puts(v, lay.menu_row, ncols - 28, p.mbar, "Tab/F9:Term  F10/M:Menu ", 27);
    dos_vid_hline(v, lay.banner_row, 0, ncols, p.hdr, ' ');
    dos_vid_puts(v, lay.banner_row, 1, p.hdr, "OYK Node KR4OYK-7 (OYKNOD) | Up 00:05:12 | Conn: 1", ncols - 22);
    dos_vid_puts(v, lay.banner_row, ncols - 19, p.ok, " [INCOMING: ALLOW] ", 19);

    dos_vid_box(v, lay.iface_box.top, lay.iface_box.left, lay.iface_box.h, lay.iface_box.w, p.hdr, p.bg, 1);
    dos_vid_box(v, lay.heard_box.top, lay.heard_box.left, lay.heard_box.h, lay.heard_box.w, p.hdr, p.bg, 1);
    dos_vid_putc(v, lay.heard_box.top, lay.heard_box.left, p.hdr, OYK_BOX_TT);
    dos_vid_title(v, lay.iface_box.top, lay.iface_box.left + 2, 24, p.hdr, "Radio Interfaces");
    dos_vid_puts(v, lay.iface_box.top + 1, lay.iface_box.left + 1, p.lhdr,
                 "Port     Type     Conn Status          RX/TX", lay.iface_box.w - 2);
    dos_vid_puts(v, lay.iface_box.top + 2, lay.iface_box.left + 1, p.bg,
                 "axudp0   axudp       1 UP 0.0.0.0:10093 12/8", lay.iface_box.w - 2);
    dos_vid_puts(v, lay.iface_box.top + 3, lay.iface_box.left + 1, p.bg,
                 "kiss0    kiss-ser    0 UP ser0:9600      0/0", lay.iface_box.w - 2);

    dos_vid_title(v, lay.heard_box.top, lay.heard_box.left + 2, 26, p.hdr, "Last 5 New Callsigns Heard");
    dos_vid_puts(v, lay.heard_box.top + 1, lay.heard_box.left + 1, p.lhdr,
                 "#  Callsign  Port   Heard", lay.heard_box.w - 2);
    dos_vid_puts(v, lay.heard_box.top + 2, lay.heard_box.left + 1, p.bg,
                 "1  W3TRM-1   axudp0 20:40:01", lay.heard_box.w - 2);

    dos_vid_box(v, lay.conn_box.top, lay.conn_box.left, lay.conn_box.h, lay.conn_box.w, p.hdr, p.bg, 1);
    dos_vid_title(v, lay.conn_box.top, lay.conn_box.left + 2, 34, p.hdr, "Active Connections (1) | Banned: 0");
    dos_vid_puts(v, lay.conn_box.top + 1, lay.conn_box.left + 1, p.lhdr,
                 "ID   Callsign  Port     Dir      State      Role / Bridge", lay.conn_box.w - 2);
    dos_vid_puts(v, lay.conn_box.top + 2, lay.conn_box.left + 1, p.bg,
                 "#1   W3TRM-1   axudp0   INBOUND  CONNECTED  NODE SHELL", lay.conn_box.w - 2);

    dos_vid_box(v, lay.log_box.top, lay.log_box.left, lay.log_box.h, lay.log_box.w, p.hdr, p.bg, 1);
    dos_vid_title(v, lay.log_box.top, lay.log_box.left + 2, 20, p.hdr, "Node Activity Log");
    dos_vid_puts(v, lay.log_box.top + 1, lay.log_box.left + 2, p.bg,
                 "[20:40:01] [INFO] Interface axudp0 (axudp) started: UP 0.0.0.0:10093", lay.log_box.w - 4);
    dos_vid_puts(v, lay.log_box.top + 2, lay.log_box.left + 2, p.bg,
                 "[20:40:02] [INFO] Inbound connection #1 from W3TRM-1 on axudp0", lay.log_box.w - 4);

    dos_vid_hline(v, lay.status_row, 0, ncols, p.stat, ' ');
    dos_vid_puts(v, lay.status_row, 1, p.stat, "Ready. Press M/F10 for Node menu.", ncols - 2);
}

static void dos_modal_frame(OykDosVideo* v, OykDosGfx* g, const OykTuiRect& mr,
                            const char* title, oyk_u8 bdr, oyk_u8 fill) {
    dos_vid_box(v, mr.top, mr.left, mr.h, mr.w, bdr, fill, 1);
    dos_vid_shadow(v, mr.top, mr.left, mr.h, mr.w);
    if (g) dos_gfx_add_shadow(g, mr.top, mr.left, mr.h, mr.w);
    int cc = oyk_tui_close_btn_col(&mr);
    if (title && title[0] && cc > mr.left + 4) {
        dos_vid_title(v, mr.top, mr.left + 2, cc - (mr.left + 3), bdr, title);
    }
    if (cc >= 0) {
        dos_vid_puts(v, mr.top, cc, 0x4FU, "[X]", 3);
        if (g) dos_gfx_set_cell_rgb(g, mr.top, cc, 3, 0xFFFFFFUL, 0xCC2222UL);
    }
}

static void run_dos_mock_flow_suite(const char* prefix, OykDosGfx* g, const DosMockPalette& p) {
    const int rows = g ? g->rows : 25;
    const int cols = g ? g->cols : 80;
    oyk_u16* sim_vram = new oyk_u16[static_cast<size_t>(rows) * static_cast<size_t>(cols)];
    OykDosVideo* v = new OykDosVideo();
    dos_vid_init_sim_sized(v, g ? 0 : 1, cols, rows, sim_vram);

    const oyk_u8 bdr = p.bdr, fill = p.fill, sel = p.sel, inp = p.inp, bar = p.bar, lhdr = p.lhdr;
    OykTuiRect mr;

    /* 1. Main screen */
    dos_render_mock_main_pal(v, g, p);
    assert(dos_vid_flush(v) > 0);
    if (g) dos_gfx_present(g, sim_vram, 1);
    dos_save_capture_prefixed(prefix, "main", v, g);

    /* 2. Settings (Node tab) */
    dos_render_mock_main_pal(v, g, p);
    oyk_tui_center_modal(rows, cols, 21, 74, &mr);
    dos_modal_frame(v, g, mr, "Node Settings", bdr, fill);
    dos_vid_puts(v, mr.top + 1, mr.left + 1, bar, " [1:Node]  2:BBS  3:CHAT  4:Ifaces  5:Peers  6:Rig  7:Prn", mr.w - 2);
    dos_vid_hdiv(v, mr.top + 2, mr.left, mr.w, bdr);
    dos_vid_puts(v, mr.top + 3, mr.left + 3, fill, "Callsign       : ", 17); dos_vid_puts(v, mr.top + 3, mr.left + 20, sel, "KR4OYK-7", 32);
    dos_vid_puts(v, mr.top + 4, mr.left + 3, fill, "Alias          : ", 17); dos_vid_puts(v, mr.top + 4, mr.left + 20, inp, "OYKNOD", 32);
    dos_vid_puts(v, mr.top + 5, mr.left + 3, fill, "Learn Policy   : < accept >", 40);
    dos_vid_puts(v, mr.top + 6, mr.left + 3, fill, "Interfaces File: interfaces.cfg", 40);
    dos_vid_puts(v, mr.top + 7, mr.left + 3, fill, "Peers File     : peers.cfg", 40);
    dos_vid_puts(v, mr.top + 8, mr.left + 3, fill, "Themes File    : themes.cfg", 40);
    dos_vid_hdiv(v, mr.top + mr.h - 3, mr.left, mr.w, bdr);
    dos_vid_puts(v, mr.top + mr.h - 2, mr.left + 2, bar, "Tab/Lt/Rt/1-7:Tabs  Enter/Space:Edit  S/F2:Save & Apply  Esc:Cancel", mr.w - 4);
    assert(dos_vid_flush(v) > 0);
    if (g) dos_gfx_present(g, sim_vram, 1);
    dos_save_capture_prefixed(prefix, "settings", v, g);

    /* 3. Add Interface Wizard */
    dos_render_mock_main_pal(v, g, p);
    oyk_tui_center_modal(rows, cols, 19, 68, &mr);
    dos_modal_frame(v, g, mr, "Add Interface", bdr, fill);
    dos_vid_title(v, mr.top, oyk_tui_close_btn_col(&mr) - 15, 14, bdr, "[######..] 3/4");
    dos_vid_puts(v, mr.top + 1, mr.left + 2, bdr,  "Configure AXUDP bind host/DMR, port, netmask, and policy:", 62);
    dos_vid_puts(v, mr.top + 2, mr.left + 3, fill, "Interface    : axudp1 (axudp)", 50);
    dos_vid_puts(v, mr.top + 4, mr.left + 3, fill, "Host / DMR ID  : ", 17); dos_vid_puts(v, mr.top + 4, mr.left + 20, sel, "0.0.0.0", 30);
    dos_vid_puts(v, mr.top + 5, mr.left + 3, fill, "UDP Port       : ", 17); dos_vid_puts(v, mr.top + 5, mr.left + 20, inp, "10093", 30);
    dos_vid_puts(v, mr.top + 6, mr.left + 3, fill, "Netmask (opt)  : ", 17); dos_vid_puts(v, mr.top + 6, mr.left + 20, inp, "255.0.0.0", 30);
    dos_vid_puts(v, mr.top + 7, mr.left + 3, fill, "Peer (opt)     : ", 17); dos_vid_puts(v, mr.top + 7, mr.left + 20, inp, "", 30);
    dos_vid_puts(v, mr.top + 8, mr.left + 3, fill, "Allow Inbound  : [ON]", 30);
    dos_vid_puts(v, mr.top + 9, mr.left + 3, fill, "Allow Outbound : [ON]", 30);
    dos_vid_puts(v, mr.top + 10, mr.left + 3, fill, "Same-Iface Fwd : [OFF]", 30);
    dos_vid_puts(v, mr.top + 11, mr.left + 3, fill, "Learned Policy : < inherit >", 40);
    dos_vid_hdiv(v, mr.top + mr.h - 3, mr.left, mr.w, bdr);
    dos_vid_puts(v, mr.top + mr.h - 2, mr.left + 2, bar, "[ < Back ]  [ Next > ]  [ Cancel ]", 40);
    assert(dos_vid_flush(v) > 0);
    if (g) dos_gfx_present(g, sim_vram, 1);
    dos_save_capture_prefixed(prefix, "add_iface", v, g);

    /* 4. Edit Interface Modal */
    dos_render_mock_main_pal(v, g, p);
    oyk_tui_center_modal(rows, cols, 19, 68, &mr);
    dos_modal_frame(v, g, mr, "Edit Interface", bdr, fill);
    dos_vid_puts(v, mr.top + 1, mr.left + 2, bdr,  "Edit interface settings and select Save to update draft:", 62);
    dos_vid_puts(v, mr.top + 3, mr.left + 3, fill, "Driver (Lt/Rt) : ", 17); dos_vid_puts(v, mr.top + 3, mr.left + 20, sel, "< axudp >", 30);
    dos_vid_puts(v, mr.top + 4, mr.left + 3, fill, "Interface ID   : ", 17); dos_vid_puts(v, mr.top + 4, mr.left + 20, inp, "axudp0", 30);
    dos_vid_puts(v, mr.top + 5, mr.left + 3, fill, "Description    : ", 17); dos_vid_puts(v, mr.top + 5, mr.left + 20, inp, "Primary AXUDP Link", 30);
    dos_vid_puts(v, mr.top + 6, mr.left + 3, fill, "Host / DMR ID  : ", 17); dos_vid_puts(v, mr.top + 6, mr.left + 20, inp, "0.0.0.0", 30);
    dos_vid_puts(v, mr.top + 7, mr.left + 3, fill, "UDP Port       : ", 17); dos_vid_puts(v, mr.top + 7, mr.left + 20, inp, "10093", 30);
    dos_vid_puts(v, mr.top + 8, mr.left + 3, fill, "Netmask (opt)  : ", 17); dos_vid_puts(v, mr.top + 8, mr.left + 20, inp, "", 30);
    dos_vid_puts(v, mr.top + 9, mr.left + 3, fill, "Static Peer    : ", 17); dos_vid_puts(v, mr.top + 9, mr.left + 20, inp, "", 30);
    dos_vid_puts(v, mr.top + 10, mr.left + 3, fill, "Allow Inbound  : [ON]", 30);
    dos_vid_puts(v, mr.top + 11, mr.left + 3, fill, "Allow Outbound : [ON]", 30);
    dos_vid_puts(v, mr.top + 12, mr.left + 3, fill, "Same-Iface Fwd : [OFF]", 30);
    dos_vid_puts(v, mr.top + 13, mr.left + 3, fill, "Learned Policy : < inherit >", 40);
    dos_vid_hdiv(v, mr.top + mr.h - 3, mr.left, mr.w, bdr);
    dos_vid_puts(v, mr.top + mr.h - 2, mr.left + 2, bar, "[  Save  ]  [ Cancel ]", 30);
    assert(dos_vid_flush(v) > 0);
    if (g) dos_gfx_present(g, sim_vram, 1);
    dos_save_capture_prefixed(prefix, "edit_iface", v, g);

    /* 5. Settings (Peers Tab) */
    dos_render_mock_main_pal(v, g, p);
    oyk_tui_center_modal(rows, cols, 21, 74, &mr);
    dos_modal_frame(v, g, mr, "Node Settings", bdr, fill);
    dos_vid_puts(v, mr.top + 1, mr.left + 1, bar, " 1:Node  2:BBS  3:CHAT  4:Ifaces  [5:Peers]  6:Rig  7:Prn", mr.w - 2);
    dos_vid_hdiv(v, mr.top + 2, mr.left, mr.w, bdr);
    dos_vid_puts(v, mr.top + 3, mr.left + 1, lhdr, "ID        Callsign   Iface   Endpoint          In/Out", mr.w - 2);
    dos_vid_puts(v, mr.top + 4, mr.left + 1, sel,  "remote0   R3MOT-7    axudp0  12.34.56.78:10093 ON/ON ", mr.w - 2);
    dos_vid_hdiv(v, mr.top + mr.h - 3, mr.left, mr.w, bdr);
    dos_vid_puts(v, mr.top + mr.h - 2, mr.left + 2, bar, "A:Add  E/Enter:Edit  K:Del  S/F2:Save  Esc:Close", mr.w - 4);
    assert(dos_vid_flush(v) > 0);
    if (g) dos_gfx_present(g, sim_vram, 1);
    dos_save_capture_prefixed(prefix, "peers", v, g);

    /* 6. Edit Peer Modal */
    dos_render_mock_main_pal(v, g, p);
    oyk_tui_center_modal(rows, cols, 17, 60, &mr);
    dos_modal_frame(v, g, mr, "Edit Peer", bdr, fill);
    dos_vid_puts(v, mr.top + 2, mr.left + 2, fill, "Peer ID        : ", 17); dos_vid_puts(v, mr.top + 2, mr.left + 19, sel, "remote0", 28);
    dos_vid_puts(v, mr.top + 3, mr.left + 2, fill, "Callsign       : ", 17); dos_vid_puts(v, mr.top + 3, mr.left + 19, inp, "R3MOT-7", 28);
    dos_vid_puts(v, mr.top + 5, mr.left + 2, fill, "Interface      : < axudp0 >", 38);
    dos_vid_puts(v, mr.top + 6, mr.left + 2, fill, "Host / DMR ID  : ", 17); dos_vid_puts(v, mr.top + 6, mr.left + 19, inp, "12.34.56.78", 28);
    dos_vid_puts(v, mr.top + 8, mr.left + 2, fill, "Allow Inbound  : [ON]", 30);
    dos_vid_puts(v, mr.top + 9, mr.left + 2, fill, "Allow Outbound : [ON]", 30);
    dos_vid_hdiv(v, mr.top + mr.h - 3, mr.left, mr.w, bdr);
    dos_vid_puts(v, mr.top + mr.h - 2, mr.left + 2, bar, "[  Save  ]  [ Cancel ]", 30);
    assert(dos_vid_flush(v) > 0);
    if (g) dos_gfx_present(g, sim_vram, 1);
    dos_save_capture_prefixed(prefix, "edit_peer", v, g);

    /* 7. Terminate Active Connection Modal */
    dos_render_mock_main_pal(v, g, p);
    oyk_tui_center_modal(rows, cols, 16, 64, &mr);
    dos_modal_frame(v, g, mr, "Terminate Active Connection", bdr, fill);
    dos_vid_puts(v, mr.top + 2, mr.left + 2, lhdr, "ID    Callsign    Port       State", mr.w - 4);
    dos_vid_puts(v, mr.top + 3, mr.left + 2, sel,  "#1    W3TRM-1     axudp0     [CONNECTED]", mr.w - 4);
    dos_vid_hdiv(v, mr.top + mr.h - 3, mr.left, mr.w, bdr);
    dos_vid_puts(v, mr.top + mr.h - 2, mr.left + 2, bar, "Up/Down: select | Enter: terminate | Esc: cancel", mr.w - 4);
    assert(dos_vid_flush(v) > 0);
    if (g) dos_gfx_present(g, sim_vram, 1);
    dos_save_capture_prefixed(prefix, "terminate", v, g);

    /* 8. Ban / Unban Callsign Modal */
    dos_render_mock_main_pal(v, g, p);
    oyk_tui_center_modal(rows, cols, 21, 74, &mr);
    dos_modal_frame(v, g, mr, "Ban / Unban Callsign (banlist.cfg)", bdr, fill);
    dos_vid_puts(v, mr.top + 2, mr.left + 2, fill, "Callsign: ", 10); dos_vid_puts(v, mr.top + 2, mr.left + 12, sel, "B4NTST", 12);
    dos_vid_puts(v, mr.top + 3, mr.left + 2, fill, "Lift At : ", 10); dos_vid_puts(v, mr.top + 3, mr.left + 12, inp, "permanent", 16);
    dos_vid_puts(v, mr.top + 3, mr.left + 30, fill, "(permanent, 30m, 12h, 7d, YYYY-MM-DD)", 40);
    dos_vid_puts(v, mr.top + 4, mr.left + 2, fill, "Reason  : ", 10); dos_vid_puts(v, mr.top + 4, mr.left + 12, inp, "Banned by sysop.", 56);
    dos_vid_puts(v, mr.top + 5, mr.left + 2, fill, "Options : [ ] Hard Ban (H)   [X] Appeal Allowed (A)", 66);
    dos_vid_puts(v, mr.top + 7, mr.left + 2, fill, "Active Bans (1):", 30);
    dos_vid_puts(v, mr.top + 8, mr.left + 2, lhdr, "Callsign   Type  Lift At          Reason", mr.w - 4);
    dos_vid_puts(v, mr.top + 9, mr.left + 2, fill, "B4NTST     SOFT  permanent        Banned by sysop.", mr.w - 4);
    dos_vid_hdiv(v, mr.top + mr.h - 3, mr.left, mr.w, bdr);
    dos_vid_puts(v, mr.top + mr.h - 2, mr.left + 2, bar, "Enter: BAN | Tab: next field | Esc: close", mr.w - 4);
    assert(dos_vid_flush(v) > 0);
    if (g) dos_gfx_present(g, sim_vram, 1);
    dos_save_capture_prefixed(prefix, "ban", v, g);

    /* 9. Auto-Learned Peers 3-Way Exit Modal */
    dos_render_mock_main_pal(v, g, p);
    oyk_tui_center_modal(rows, cols, 11, 68, &mr);
    dos_modal_frame(v, g, mr, "Auto-Learned Peers", bdr, fill);
    dos_vid_puts(v, mr.top + 2, mr.left + 2, fill, "1 auto-learned peer(s) added during this session.", mr.w - 4);
    dos_vid_puts(v, mr.top + 3, mr.left + 2, fill, "Save to config, discard, or review in Peers?", mr.w - 4);
    dos_vid_hdiv(v, mr.top + mr.h - 3, mr.left, mr.w, bdr);
    dos_vid_puts(v, mr.top + mr.h - 2, mr.left + 2, sel, "[ Save & Exit ]", 15);
    dos_vid_puts(v, mr.top + mr.h - 2, mr.left + 19, bar, "[ Discard ]  [ Review Peers ]  [ Cancel ]", 42);
    assert(dos_vid_flush(v) > 0);
    if (g) dos_gfx_present(g, sim_vram, 1);
    dos_save_capture_prefixed(prefix, "exit", v, g);

    /* 10. Theme Settings Modal */
    dos_render_mock_main_pal(v, g, p);
    oyk_tui_center_modal(rows, cols, 21, 74, &mr);
    dos_modal_frame(v, g, mr, "Theme Settings", bdr, fill);
    dos_vid_puts(v, mr.top + 2, mr.left + 2, fill, "Theme  : < dark               > (Built-in (not saved))", mr.w - 4);
    dos_vid_puts(v, mr.top + 3, mr.left + 2, fill, "Editing built-in clones custom-dark or custom-light.", mr.w - 4);
    dos_vid_puts(v, mr.top + 5, mr.left + 2, lhdr, "Role              Key            Color     Preview", mr.w - 4);
    {
        OykThemeConfig dt;
        oyk_theme_init_builtin(&dt, 0);
        for (int r = 0; r < 10; ++r) {
            char hx[12], row_s[60];
            oyk_theme_format_hex(dt.colors[r], hx, sizeof(hx));
            sprintf(row_s, "%-16.16s  %-13.13s  %-8.8s",
                    oyk_theme_role_label(r), oyk_theme_role_key(r), hx);
            dos_vid_puts(v, mr.top + 6 + r, mr.left + 2, (r == 0) ? sel : fill, row_s, 44);
            oyk_u8 sw = static_cast<oyk_u8>((oyk_rgb_to_vga16(dt.colors[r]) << 4) | oyk_rgb_to_vga16(dt.colors[r]));
            dos_vid_puts(v, mr.top + 6 + r, mr.left + 47, sw, "    ", 4);
            if (g) dos_gfx_set_cell_rgb(g, mr.top + 6 + r, mr.left + 47, 4, dt.colors[r], dt.colors[r]);
        }
    }
    dos_vid_puts(v, mr.top + mr.h - 4, mr.left + 2, fill, "Live preview active (unsaved). Press S/F2 to save or Esc to revert.", mr.w - 4);
    dos_vid_hdiv(v, mr.top + mr.h - 3, mr.left, mr.w, bdr);
    dos_vid_puts(v, mr.top + mr.h - 2, mr.left + 2, bar, "Lt/Rt:Theme  Enter:Edit  X:Del  S/F2:Save & Apply  Esc:Cancel", mr.w - 4);
    assert(dos_vid_flush(v) > 0);
    if (g) dos_gfx_present(g, sim_vram, 1);
    dos_save_capture_prefixed(prefix, "theme", v, g);

    /* 11. Local Terminal (Ctrl+A B example session, full display with word-wrap & [EOL:CR]) */
    if (g) {
        dos_gfx_clear_shadows(g);
        dos_gfx_clear_cell_rgb(g);
    }
    OykTuiTermLayout tlay;
    oyk_tui_compute_term_layout(v->rows, v->cols, &tlay);
    dos_vid_clear(v, p.bg);
    dos_vid_hline(v, tlay.header_row, 0, tlay.cols, p.hdr, ' ');
    dos_vid_puts(v, tlay.header_row, 0, p.hdr,
                 " LOCAL TERMINAL | CONNECTED: KR4OYK-1 (LOCAL) [EOL:CR]", tlay.cols);
    dos_vid_box(v, tlay.body_top, 0, tlay.body_rows, tlay.cols, bdr, p.bg, 1);
    dos_vid_title(v, tlay.body_top, 2, tlay.cols - 4, p.hdr, "Terminal Output");
    int row_cur = tlay.body_top + 1;
    const char* sample_lines[8] = {
        "*** OYK Local Terminal (Ctrl+A Z:Help  Ctrl+A B:BBS  Ctrl+A T:CHAT)",
        "*** Connected locally to KR4OYK-1 (BBS)",
        "Welcome to KR4OYK-1 Packet BBS - automatic word wrapping handles long bulletin lines cleanly across narrow and wide screens.",
        "Type L to list messages, R # to read, B to bye.",
        "KR4OYK-1> L",
        "Msg#  Size  From     To       Subject",
        "1     248   W3TRM-1  ALL      Welcome to OYK Packet Node",
        "KR4OYK-1> "
    };
    for (int li = 0; li < 8; ++li) {
        const char* ln = sample_lines[li];
        oyk_u8 attr = p.bg;
        if (li == 1) attr = p.ok;
        else if (li == 5) attr = p.lhdr;
        OykTermWrapSpan spans[8];
        int sc = oyk_term_wrap_line(ln, strlen(ln), tlay.cols - 2, spans, 8);
        for (int si = 0; si < sc && row_cur < tlay.body_top + tlay.body_rows - 1; ++si) {
            char seg[160];
            size_t slen = spans[si].len;
            if (slen >= sizeof(seg)) slen = sizeof(seg) - 1;
            memcpy(seg, ln + spans[si].start, slen);
            seg[slen] = '\0';
            dos_vid_puts(v, row_cur++, 1, attr, seg, tlay.cols - 2);
        }
    }
    dos_vid_hline(v, tlay.hotkey_row, 0, tlay.cols, bar, ' ');
    dos_vid_puts(v, tlay.hotkey_row, 0, bar, " Ctrl+A Z:Help  F2:Conn  F3:YAPP  F4:Disc  Ctrl+A E:EOL  F9/Tab:Console", tlay.cols);
    dos_vid_hline(v, tlay.input_row, 0, tlay.cols, fill, ' ');
    dos_vid_puts(v, tlay.input_row, 0, fill, "> ", tlay.cols);
    dos_vid_putc(v, tlay.input_row, 2, p.sel, ' ');
    assert(dos_vid_flush(v) > 0);
    if (g) dos_gfx_present(g, sim_vram, 1);
    dos_save_capture_prefixed(prefix, "terminal", v, g);

    /* 12. Manage BBS Files Area Modal */
    dos_render_mock_main_pal(v, g, p);
    oyk_tui_center_modal(rows, cols, 17, 68, &mr);
    dos_modal_frame(v, g, mr, "Manage BBS Files Area", bdr, fill);
    dos_vid_puts(v, mr.top + 1, mr.left + 1, sel, "[1:Pending Incoming (2)]", 24);
    dos_vid_puts(v, mr.top + 1, mr.left + 27, bar, " 2:Approved Files (2) ", 22);
    dos_vid_puts(v, mr.top + 2, mr.left + 1, lhdr, "Uploader     Filename                      Size", mr.w - 2);
    dos_vid_puts(v, mr.top + 3, mr.left + 1, sel,  "W3TRM-1      fieldday.txt                  1420 B", mr.w - 2);
    dos_vid_puts(v, mr.top + 4, mr.left + 1, fill, "N0CALL-7     aprs.cfg                       612 B", mr.w - 2);
    dos_vid_hdiv(v, mr.top + mr.h - 3, mr.left, mr.w, bdr);
    dos_vid_puts(v, mr.top + mr.h - 2, mr.left + 2, bar, "A:Approve  D:Del  Tab:Switch  R:Refresh  Esc:Close", mr.w - 4);
    assert(dos_vid_flush(v) > 0);
    if (g) dos_gfx_present(g, sim_vram, 1);
    dos_save_capture_prefixed(prefix, "files", v, g);

    dos_vid_shutdown(v);
    delete v;
    delete[] sim_vram;
}

static void test_dos_ui_screen_flows() {
    DosMockPalette p;
    p.bg   = 0x17U; p.hdr  = 0x1FU; p.mbar = 0x70U; p.msel = 0x30U; p.ok = 0x1AU;
    p.lhdr = 0x8FU; p.stat = 0x70U; p.bdr  = 0x1FU; p.fill = 0x17U;
    p.sel  = 0x30U; p.inp  = 0x70U; p.bar  = 0x70U;
    run_dos_mock_flow_suite("dos", NULL, p);
    printf("[PASS] test_dos_ui_screen_flows\n");
}

static void test_dos_gfx_ui_screen_flows() {
    OykDosGfx* g = new OykDosGfx;
    assert(dos_gfx_init_sim(g, OYK_GFX_MODE_VBE8) == 1);
    assert(g->cols == 80 && g->rows == 30);
    OykThemeConfig dt;
    oyk_theme_init_builtin(&dt, 0);
    dos_gfx_set_theme(g, &dt, 0);

    DosMockPalette p;
    p.bg   = 0x07U; p.hdr  = 0x1FU; p.mbar = 0xDFU; p.msel = 0x3FU; p.ok = 0x2FU;
    p.lhdr = 0x5BU; p.stat = 0x6EU; p.bdr  = 0x59U; p.fill = 0x58U;
    p.sel  = 0x3FU; p.inp  = 0x07U; p.bar  = 0xDFU;
    run_dos_mock_flow_suite("dos_gfx", g, p);

    dos_gfx_shutdown(g);
    delete g;
    printf("[PASS] test_dos_gfx_ui_screen_flows\n");
}

static void test_win_service_cmdline_builder() {
    char buf[512];
    /* Windows NT default binPath (no config override) */
    assert(oyk_win_build_service_cmdline("C:\\OYK\\oykw64.exe", NULL, "--service", buf, sizeof(buf)) == 1);
    assert(strcmp(buf, "\"C:\\OYK\\oykw64.exe\" --service") == 0);

    /* Windows 9x RunServices command with empty config path */
    assert(oyk_win_build_service_cmdline("C:\\OYK\\oykw32.exe", "", "--headless", buf, sizeof(buf)) == 1);
    assert(strcmp(buf, "\"C:\\OYK\\oykw32.exe\" --headless") == 0);

    /* Paths containing spaces with --config carried into registry entry */
    assert(oyk_win_build_service_cmdline("C:\\Program Files\\OYK\\oykw32.exe",
                                         "C:\\Program Data\\OYK\\node.cfg",
                                         "--service",
                                         buf,
                                         sizeof(buf)) == 1);
    assert(strcmp(buf, "\"C:\\Program Files\\OYK\\oykw32.exe\" --service --config \"C:\\Program Data\\OYK\\node.cfg\"") == 0);

    /* Invalid / undersized arguments */
    assert(oyk_win_build_service_cmdline("", NULL, "--service", buf, sizeof(buf)) == 0);
    assert(oyk_win_build_service_cmdline("C:\\oykw32.exe", NULL, "", buf, sizeof(buf)) == 0);
    char tiny[12];
    assert(oyk_win_build_service_cmdline("C:\\OYK\\oykw64.exe", NULL, "--service", tiny, sizeof(tiny)) == 0);
    printf("[PASS] test_win_service_cmdline_builder\n");
}
#endif

int oyk_run_portable_tests() {
    test_portable_eol_and_config();
    test_portable_ax25_kiss_and_bans();
    test_portable_yapp_and_local_terminal();
    test_portable_bbs();
    test_portable_settings_reconfig_and_readonly();
    test_portable_rig_and_dos_usb();
    test_portable_printer();
    oyk_run_portable_term_tests();
    test_dos_video_dirty_diff_and_attrs();
    test_derive_default_iface_id();
    test_format_peer_spec();
    test_portable_peer_sections_and_auto_learn();
    test_portable_iface_directions_netmask_and_dmr();
    test_portable_config_include_storage_modes();
    test_portable_same_interface_policy();
    test_portable_theme_config_and_cloning();
#if !defined(__DOS__) || defined(OYK_DOS_GFX_BUILD)
    test_dos_gfx_renderer_and_smooth_scroll();
#endif
#ifndef __DOS__
    test_dos_ui_screen_flows();
    test_dos_gfx_ui_screen_flows();
    test_win_service_cmdline_builder();
#endif
    return 0;
}

#ifndef OYK_PORTABLE_TESTS_AS_LIBRARY
int main() {
    oyk_run_portable_tests();
    printf("All oyk_portable (C++98) tests passed!\n");
    return 0;
}
#endif
