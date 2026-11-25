/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/* Strict C++98 test suite for the portable MS-DOS / Win16 / Win32 / Win64 core.
 * Uses 8.3 FAT-safe relative paths on MS-DOS and /tmp paths on POSIX.
 * Define OYK_PORTABLE_TESTS_AS_LIBRARY to call oyk_run_portable_tests() from another main().
 */
#include "oyk_portable.hpp"
#include "oyk_yapp.hpp"

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
        remove(dst_out);
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

int oyk_run_portable_tests() {
    test_portable_eol_and_config();
    test_portable_ax25_kiss_and_bans();
    test_portable_yapp_and_local_terminal();
    test_portable_bbs();
    return 0;
}

#ifndef OYK_PORTABLE_TESTS_AS_LIBRARY
int main() {
    oyk_run_portable_tests();
    printf("All oyk_portable (C++98) tests passed!\n");
    return 0;
}
#endif
