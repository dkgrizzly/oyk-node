/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * Strict C++98 host test suite for the Freescale/NXP IMXRT1060 (Teensy 4.1 / TinyTMUX) port:
 *   1. TinyTMUX 8-UART pinout & IMXRT1060 OCOTP MAC decoding
 *   2. RFC 1662 Async HDLC framing, ACCM byte-stuffing, and FCS-16 validation
 *   3. RFC 1661 LCP + RFC 1332 IPCP negotiation (Teensy 4.1 <-> RadioFoundry radio)
 *   4. Simultaneous AXUDP + AXIP (IP proto 93) over PPP AND over 10/100 Ethernet
 *   5. ANSI/VT100 Operator Console rendering & switchable Local Terminal over USB-CDC
 */

#include "../ports/compat/oyk_portable.hpp"
#include "../ports/tinytmux/board_tinytmux.hpp"
#include "../ports/tinytmux/lwip_net_bridge.hpp"
#include "../ports/tinytmux/oyk_ppp.hpp"
#include "../ports/tinytmux/tinytmux_console.hpp"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_board_pinout_and_mac() {
    const TinyTmuxUartMap* tbl = tinytmux_uart_table();
    assert(tbl != NULL);

    /* Verify exact TinyTMUX pinout from ~/TinyTMUX/TinyTMUX.ino */
    assert(tbl[0].teensy_serial == 1 && tbl[0].lpuart_index == 6 && tbl[0].pin_rx == 0  && tbl[0].pin_tx == 1  && tbl[0].pin_cts == 33 && tbl[0].pin_rts == 6);
    assert(tbl[1].teensy_serial == 2 && tbl[1].lpuart_index == 4 && tbl[1].pin_rx == 7  && tbl[1].pin_tx == 8  && tbl[1].pin_cts == 2  && tbl[1].pin_rts == 9);
    assert(tbl[2].teensy_serial == 6 && tbl[2].lpuart_index == 1 && tbl[2].pin_rx == 25 && tbl[2].pin_tx == 24 && tbl[2].pin_cts == 3  && tbl[2].pin_rts == 26);
    assert(tbl[3].teensy_serial == 7 && tbl[3].lpuart_index == 7 && tbl[3].pin_rx == 28 && tbl[3].pin_tx == 29 && tbl[3].pin_cts == 4  && tbl[3].pin_rts == 27);
    assert(tbl[4].teensy_serial == 8 && tbl[4].lpuart_index == 5 && tbl[4].pin_rx == 34 && tbl[4].pin_tx == 35 && tbl[4].pin_cts == 5  && tbl[4].pin_rts == 36);
    assert(tbl[5].teensy_serial == 3 && tbl[5].lpuart_index == 2 && tbl[5].pin_rx == 15 && tbl[5].pin_tx == 14 && tbl[5].pin_cts == 30 && tbl[5].pin_rts == 41);
    assert(tbl[6].teensy_serial == 4 && tbl[6].lpuart_index == 3 && tbl[6].pin_rx == 16 && tbl[6].pin_tx == 17 && tbl[6].pin_cts == 31 && tbl[6].pin_rts == 40);
    assert(tbl[7].teensy_serial == 5 && tbl[7].lpuart_index == 8 && tbl[7].pin_rx == 21 && tbl[7].pin_tx == 20 && tbl[7].pin_cts == 32 && tbl[7].pin_rts == 22);
    assert(tinytmux_uart_info(-1) == NULL);
    assert(tinytmux_uart_info(8) == NULL);

    /* OCOTP MAC decoding */
    oyk_u8 mac[6];
    tinytmux_decode_ocotp_mac(0x11223344UL, 0x000004E9UL, mac);
    assert(mac[0] == 0x04U && mac[1] == 0xE9U &&
           mac[2] == 0x11U && mac[3] == 0x22U &&
           mac[4] == 0x33U && mac[5] == 0x44U);

    /* Load TinyTMUX example config */
    OykConfig* cfg = new OykConfig();
    char err[96];
    const char* cfg_path = "ports/tinytmux/node_tinytmux.cfg";
    FILE* probe = fopen(cfg_path, "rb");
    if (!probe) cfg_path = "../ports/tinytmux/node_tinytmux.cfg";
    else fclose(probe);
    assert(oyk_config_load(cfg_path, cfg, err, sizeof(err)) == 1);
    assert(cfg->bbs_enabled == 1 && cfg->bbs_ssid == 11);
    assert(cfg->chat_enabled == 1 && cfg->chat_ssid == 12);
    assert(cfg->iface_count == 4);
    assert(strcmp(cfg->ifaces[2].type, "ppp") == 0);
    assert(strcmp(cfg->ifaces[2].device, "ser0") == 0);
    delete cfg;

    printf("[PASS] test_board_pinout_and_mac\n");
}

struct SerialWireBuf {
    oyk_u8 data[4096];
    size_t len;
};

static size_t wire_write_cb(const oyk_u8* data, size_t len, void* ctx) {
    SerialWireBuf* w = static_cast<SerialWireBuf*>(ctx);
    if (!w || !data) return 0;
    size_t avail = sizeof(w->data) - w->len;
    if (len > avail) len = avail;
    memcpy(w->data + w->len, data, len);
    w->len += len;
    return len;
}

struct CapturedAx25 {
    int count;
    int last_is_axip;
    oyk_u32 last_ip;
    oyk_u16 last_port;
    OykFrame last_frame;
    char text[512];
    size_t text_len;
};

static void radio_ax25_rx_cb(int is_axip, const OykFrame* f,
                             oyk_u32 src_ip, oyk_u16 src_port, void* ctx) {
    CapturedAx25* c = static_cast<CapturedAx25*>(ctx);
    if (!c || !f) return;
    c->count++;
    c->last_is_axip = is_axip;
    c->last_ip = src_ip;
    c->last_port = src_port;
    c->last_frame = *f;
    if (oyk_frame_type(f) == OYK_FT_I && f->info_len > 0) {
        size_t avail = sizeof(c->text) - 1 - c->text_len;
        size_t n = (f->info_len < avail) ? f->info_len : avail;
        memcpy(c->text + c->text_len, f->info, n);
        c->text_len += n;
        c->text[c->text_len] = '\0';
    }
}

static int enet_tx_capture_cb(int is_axip, oyk_u32 dst_ip, oyk_u16 dst_port,
                              const oyk_u8* payload_with_fcs, size_t len, void* ctx) {
    CapturedAx25* c = static_cast<CapturedAx25*>(ctx);
    if (!c || !payload_with_fcs || len < 17) return 0;
    assert(oyk_verify_fcs(payload_with_fcs, len) == 1);
    OykFrame f;
    assert(oyk_ax25_decode(payload_with_fcs, len - 2, &f) == 1);
    radio_ax25_rx_cb(is_axip, &f, dst_ip, dst_port, ctx);
    return 1;
}

static void pump_ppp_pair(TinyTmuxNetBridge* bridge, int ppp_slot,
                          OykPppLink* radio_ppp,
                          SerialWireBuf* node_to_radio,
                          SerialWireBuf* radio_to_node,
                          oyk_u32 now_ms) {
    for (int round = 0; round < 12; ++round) {
        int moved = 0;
        if (node_to_radio->len > 0) {
            oyk_u8 tmp[4096];
            size_t n = node_to_radio->len;
            memcpy(tmp, node_to_radio->data, n);
            node_to_radio->len = 0;
            radio_ppp->feed_serial_bytes(tmp, n, now_ms);
            moved = 1;
        }
        if (radio_to_node->len > 0) {
            oyk_u8 tmp[4096];
            size_t n = radio_to_node->len;
            memcpy(tmp, radio_to_node->data, n);
            radio_to_node->len = 0;
            bridge->feed_ppp_serial(ppp_slot, tmp, n, now_ms);
            moved = 1;
        }
        if (!moved) break;
    }
}

static void test_ppp_and_dual_ethernet_axudp_axip() {
    /* 1. Verify HDLC framing + ACCM escaping + corrupt FCS detection */
    {
        oyk_u8 payload[4] = { 0x7EU, 0x7DU, 0x01U, 0x41U };
        oyk_u8 enc[64];
        size_t n = oyk_ppp_hdlc_encode(OYK_PPP_PROTO_IP, payload, 4, 0xFFFFFFFFUL, enc, sizeof(enc));
        assert(n > 10 && enc[0] == 0x7EU && enc[n - 1] == 0x7EU);

        /* Corrupt a byte inside the frame -> must increment rx_fcs_errors */
        OykPppLink probe;
        probe.init(0, 0, 10093, 0x12345678UL, NULL, NULL);
        enc[n / 2] ^= 0xFFU;
        probe.feed_serial_bytes(enc, n, 100);
        assert(probe.rx_fcs_errors() == 1);
    }

    /* 2. Build OykPortableNode with BOTH Ethernet (AXUDP + AXIP) AND RadioFoundry PPP */
    OykConfig* cfg = new OykConfig();
    oyk_config_init_default(cfg);
    oyk_parse_callsign("K7OYK-7", &cfg->callsign);
    strcpy(cfg->log_path, "/tmp/oyk_tmux_logs");
    strcpy(cfg->banlist_path, "/tmp/oyk_tmux_ban.cfg");
    strcpy(cfg->bbs_mail_path, "/tmp/oyk_tmux_mail");
    strcpy(cfg->bbs_files_path, "/tmp/oyk_tmux_files");
    remove("/tmp/oyk_tmux_ban.cfg");

    cfg->iface_count = 3;
    /* 0: Ethernet AXUDP */
    memset(&cfg->ifaces[0], 0, sizeof(cfg->ifaces[0]));
    strcpy(cfg->ifaces[0].id, "enet0");
    strcpy(cfg->ifaces[0].type, "axudp");
    strcpy(cfg->ifaces[0].host, "192.168.1.10");
    cfg->ifaces[0].port = 10093;
    /* 1: Ethernet AXIP (proto 93) */
    memset(&cfg->ifaces[1], 0, sizeof(cfg->ifaces[1]));
    strcpy(cfg->ifaces[1].id, "axip0");
    strcpy(cfg->ifaces[1].type, "axip");
    strcpy(cfg->ifaces[1].host, "192.168.1.10");
    /* 2: RadioFoundry PPP carrying BOTH AXUDP & AXIP */
    memset(&cfg->ifaces[2], 0, sizeof(cfg->ifaces[2]));
    strcpy(cfg->ifaces[2].id, "ppp0");
    strcpy(cfg->ifaces[2].type, "ppp");
    strcpy(cfg->ifaces[2].device, "ser0");
    strcpy(cfg->ifaces[2].host, "10.44.0.1");
    cfg->ifaces[2].port = 10093;

    OykPortableNode* node = new OykPortableNode();
    TinyTmuxNetBridge* bridge = new TinyTmuxNetBridge();
    char err[96];
    assert(node->init(cfg, TinyTmuxNetBridge::node_tx_callback, bridge, err, sizeof(err)) == 1);
    assert(bridge->init(node, 1000) == 1);

    /* Bring up 10/100 Ethernet link at 100 Mbps */
    oyk_u32 enet_ip = 0, enet_peer = 0;
    assert(oyk_parse_ipv4("192.168.1.10", &enet_ip) == 1);
    assert(oyk_parse_ipv4("192.168.1.50", &enet_peer) == 1);
    CapturedAx25 enet_cap;
    memset(&enet_cap, 0, sizeof(enet_cap));
    bridge->set_ethernet_tx_hook(enet_tx_capture_cb, &enet_cap);
    bridge->set_ethernet_link_state(1, 1, enet_ip);
    assert(bridge->ethernet_link_up() == 1);

    /* Wire PPP slot 0 to a simulated RadioFoundry peer (requesting IP 0.0.0.0 -> assigned 10.44.0.2!) */
    SerialWireBuf node_to_radio, radio_to_node;
    node_to_radio.len = 0;
    radio_to_node.len = 0;

    oyk_u32 ppp_local = 0, ppp_radio_assigned = 0;
    assert(oyk_parse_ipv4("10.44.0.1", &ppp_local) == 1);
    assert(oyk_parse_ipv4("10.44.0.2", &ppp_radio_assigned) == 1);

    bridge->configure_ppp_slot(0, -1, 2, 2, ppp_local, ppp_radio_assigned, 10093,
                               wire_write_cb, &node_to_radio, 1000);

    OykPppLink radio_ppp;
    CapturedAx25 radio_cap;
    memset(&radio_cap, 0, sizeof(radio_cap));
    radio_ppp.init(0, 0, 10093, 0xCAFEBABEUL, wire_write_cb, &radio_to_node);
    radio_ppp.set_ax25_rx_callback(radio_ax25_rx_cb, &radio_cap);
    radio_ppp.open(1000);

    /* Negotiate full LCP + IPCP handshake over virtual serial wire */
    pump_ppp_pair(bridge, 0, &radio_ppp, &node_to_radio, &radio_to_node, 1100);
    assert(bridge->ppp_link(0)->is_up() == 1);
    assert(radio_ppp.is_up() == 1);
    assert(radio_ppp.local_ip() == ppp_radio_assigned);
    assert(bridge->ppp_link(0)->remote_ip() == ppp_radio_assigned);

    /* A. Send AXUDP SABM over PPP from RF1RAD -> K7OYK-7 (Node Shell) */
    OykFrame sabm;
    memset(&sabm, 0, sizeof(sabm));
    oyk_parse_callsign("K7OYK-7", &sabm.dst);
    oyk_parse_callsign("RF1RAD", &sabm.src);
    sabm.is_command = 1;
    sabm.control = 0x3FU;
    assert(radio_ppp.send_axudp(&sabm, ppp_local, 10093) == 1);
    pump_ppp_pair(bridge, 0, &radio_ppp, &node_to_radio, &radio_to_node, 1200);
    assert(radio_cap.count >= 2);
    assert(radio_cap.last_is_axip == 0); /* Replied via AXUDP over PPP */
    assert(strstr(radio_cap.text, "Connected to K7OYK-7") != NULL);

    /* B. Send AXIP (IP Protocol 93) SABM over the SAME PPP link from RF2IP -> K7OYK-11 (BBS!) */
    memset(&radio_cap, 0, sizeof(radio_cap));
    OykFrame axip_sabm;
    memset(&axip_sabm, 0, sizeof(axip_sabm));
    oyk_parse_callsign("K7OYK-11", &axip_sabm.dst);
    oyk_parse_callsign("RF2IP", &axip_sabm.src);
    axip_sabm.is_command = 1;
    axip_sabm.control = 0x3FU;
    assert(radio_ppp.send_axip(&axip_sabm, ppp_local) == 1);
    pump_ppp_pair(bridge, 0, &radio_ppp, &node_to_radio, &radio_to_node, 1300);
    assert(radio_cap.count >= 2);
    assert(radio_cap.last_is_axip == 1); /* Replied via AXIP proto 93 over PPP */
    assert(strstr(radio_cap.text, "[OYK-0.1-HMR$]") != NULL);

    /* C. Simultaneously connect over 10/100 Ethernet AXUDP (W1ETH -> K7OYK-7) */
    OykFrame eth_sabm;
    memset(&eth_sabm, 0, sizeof(eth_sabm));
    oyk_parse_callsign("K7OYK-7", &eth_sabm.dst);
    oyk_parse_callsign("W1ETH", &eth_sabm.src);
    eth_sabm.is_command = 1;
    eth_sabm.control = 0x3FU;
    oyk_u8 eth_raw[64];
    size_t en = oyk_ax25_encode(&eth_sabm, eth_raw, sizeof(eth_raw) - 2);
    oyk_u16 efcs = oyk_crc16_fcs(eth_raw, en);
    eth_raw[en++] = static_cast<oyk_u8>(efcs & 0xFFU);
    eth_raw[en++] = static_cast<oyk_u8>((efcs >> 8) & 0xFFU);
    bridge->on_ethernet_rx(0, enet_peer, 10093, eth_raw, en);
    assert(enet_cap.count >= 2);
    assert(enet_cap.last_is_axip == 0);
    assert(strstr(enet_cap.text, "Connected to K7OYK-7") != NULL);

    /* D. Simultaneously connect over 10/100 Ethernet AXIP (W2EIP -> K7OYK-12 CHAT!) */
    memset(&enet_cap, 0, sizeof(enet_cap));
    oyk_parse_callsign("K7OYK-12", &eth_sabm.dst);
    oyk_parse_callsign("W2EIP", &eth_sabm.src);
    en = oyk_ax25_encode(&eth_sabm, eth_raw, sizeof(eth_raw) - 2);
    efcs = oyk_crc16_fcs(eth_raw, en);
    eth_raw[en++] = static_cast<oyk_u8>(efcs & 0xFFU);
    eth_raw[en++] = static_cast<oyk_u8>((efcs >> 8) & 0xFFU);
    bridge->on_ethernet_rx(1, enet_peer, 0, eth_raw, en);
    assert(enet_cap.count >= 2);
    assert(enet_cap.last_is_axip == 1);
    assert(strstr(enet_cap.text, "Packet Chat") != NULL);

    /* All 4 sessions (PPP AXUDP, PPP AXIP, Ethernet AXUDP, Ethernet AXIP) are active concurrently! */
    assert(node->total_active_connections() == 4);

    delete bridge;
    delete node;
    delete cfg;
    printf("[PASS] test_ppp_and_dual_ethernet_axudp_axip\n");
}

struct ConsoleCap {
    char buf[4096];
    size_t len;
};

static size_t console_tx_cap(const char* data, size_t len, void* ctx) {
    ConsoleCap* c = static_cast<ConsoleCap*>(ctx);
    size_t avail = sizeof(c->buf) - 1 - c->len;
    if (len > avail) len = avail;
    memcpy(c->buf + c->len, data, len);
    c->len += len;
    c->buf[c->len] = '\0';
    return len;
}

static TinyTmuxConsole* g_test_con = NULL;
static void test_con_local_cb(oyk_u32 sid, int ev, const oyk_u8* data, size_t len,
                              const char* reason, void* /*ctx*/) {
    if (g_test_con) g_test_con->on_local_term_event(sid, ev, data, len, reason);
}

static void test_vt100_operator_console() {
    OykConfig* cfg = new OykConfig();
    oyk_config_init_default(cfg);
    oyk_parse_callsign("KR4OYK", &cfg->callsign);
    oyk_parse_callsign_with_default_ssid("KR4OYK", OYK_DEFAULT_NODE_SSID, &cfg->callsign);
    strcpy(cfg->log_path, "/tmp/oyk_tmux_logs");
    strcpy(cfg->banlist_path, "/tmp/oyk_tmux_ban.cfg");

    OykPortableNode* node = new OykPortableNode();
    char err[64];
    assert(node->init(cfg, NULL, NULL, err, sizeof(err)) == 1);

    TinyTmuxConsole con;
    g_test_con = &con;
    node->set_local_event_callback(test_con_local_cb, NULL);

    ConsoleCap cap;
    cap.len = 0;
    cap.buf[0] = '\0';
    con.init(node, console_tx_cap, &cap);
    con.render();
    assert(strstr(cap.buf, "KR4OYK-7") != NULL);
    assert(strstr(cap.buf, "INCOMING: ALLOW") != NULL);

    /* Switch to Local Terminal via Tab, connect to /bbs, switch back via Esc keeping session active */
    cap.len = 0; cap.buf[0] = '\0';
    const char* seq = "\t/bbs\r\x1b";
    con.feed_input(reinterpret_cast<const oyk_u8*>(seq), strlen(seq));
    assert(con.view_mode() == TMUX_VIEW_CONSOLE);
    assert(con.has_active_terminal() == 1);
    assert(strstr(cap.buf, "Tab:Term(ACTIVE)") != NULL);

    g_test_con = NULL;
    delete node;
    delete cfg;
    printf("[PASS] test_vt100_operator_console\n");
}

int main() {
    test_board_pinout_and_mac();
    test_ppp_and_dual_ethernet_axudp_axip();
    test_vt100_operator_console();
    printf("All TinyTMUX (IMXRT1060 / Teensy 4.1) host tests passed!\n");
    return 0;
}
