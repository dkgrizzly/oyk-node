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
#include "../ports/compat/oyk_rig.hpp"
#include "../ports/tinytmux/oyk_tinytmux/board_tinytmux.hpp"
#include "../ports/tinytmux/oyk_tinytmux/lwip_net_bridge.hpp"
#include "../ports/tinytmux/oyk_tinytmux/oyk_ppp.hpp"
#include "../ports/tinytmux/oyk_tinytmux/tinytmux_console.hpp"
#include "../ports/tinytmux/oyk_tinytmux/tinytmux_psram.hpp"
#include "../ports/tinytmux/oyk_tinytmux/tinytmux_fs.hpp"
#include "../ports/tinytmux/oyk_tinytmux/tinytmux_usb_host.hpp"

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

static oyk_u32 test_parse_ip(const char* text) {
    oyk_u32 ip = 0;
    if (text && text[0]) oyk_parse_ipv4(text, &ip);
    return ip;
}

static void test_psram_detection_and_self_test() {
    oyk_u32* sim_cs0 = new oyk_u32[1024];
    oyk_u32* sim_cs1 = new oyk_u32[1024];
    char tag[8];
    const TinyTmuxPsramInfo* info = NULL;

    /* 0 PSRAM chips installed -> boots cleanly on internal SRAM */
    tinytmux_psram_set_sim_chips(NULL, 0, 0, 0, NULL, 0, 0, 0);
    tinytmux_psram_set_sim_fault_mask(0, 0);
    tinytmux_psram_set_sim_fault_mask(1, 0);
    info = tinytmux_psram_init_and_test();
    assert(info->chip_count == 0);
    assert(info->usable_chip_count == 0);
    assert(info->usable_bytes == 0UL);
    tinytmux_psram_format_short(info, tag, sizeof(tag));
    assert(strcmp(tag, "NO") == 0);

    /* 1 PSRAM chip (8 MB) */
    tinytmux_psram_set_sim_chips(sim_cs0, 1024, 0x0DU, 0x5DU, NULL, 0, 0, 0);
    info = tinytmux_psram_init_and_test();
    assert(info->chip_count == 1);
    assert(info->usable_chip_count == 1);
    assert(info->usable_bytes == TINYTMUX_PSRAM_CHIP_BYTES);
    assert(info->chips[0].detected == 1 && info->chips[0].passed == 1);
    assert(info->chips[0].base_addr == TINYTMUX_PSRAM_BASE);
    assert(info->chips[1].detected == 0);
    tinytmux_psram_format_short(info, tag, sizeof(tag));
    assert(strcmp(tag, "8M") == 0);

    /* 2 PSRAM chips (16 MB total) */
    tinytmux_psram_set_sim_chips(sim_cs0, 1024, 0x0DU, 0x5DU, sim_cs1, 1024, 0x0DU, 0x5DU);
    info = tinytmux_psram_init_and_test();
    assert(info->chip_count == 2);
    assert(info->usable_chip_count == 2);
    assert(info->usable_bytes == 2UL * TINYTMUX_PSRAM_CHIP_BYTES);
    assert(info->chips[1].base_addr == TINYTMUX_PSRAM_BASE + TINYTMUX_PSRAM_CHIP_BYTES);
    tinytmux_psram_format_short(info, tag, sizeof(tag));
    assert(strcmp(tag, "16M") == 0);

    /* Degraded mode: 2 chips soldered, chip 1 fails -> disable chip 1, keep 8 MB healthy */
    tinytmux_psram_set_sim_fault_mask(1, 0x00010000UL);
    info = tinytmux_psram_retest();
    assert(info->chip_count == 2);
    assert(info->usable_chip_count == 1);
    assert(info->usable_bytes == TINYTMUX_PSRAM_CHIP_BYTES);
    assert(info->chips[0].passed == 1);
    assert(info->chips[1].passed == 0);
    tinytmux_psram_format_short(info, tag, sizeof(tag));
    assert(strcmp(tag, "8M!") == 0);

    /* Both chips fail -> fail gracefully, 0 MB EXTMEM usable */
    tinytmux_psram_set_sim_fault_mask(0, 0x00000001UL);
    info = tinytmux_psram_retest();
    assert(info->usable_chip_count == 0);
    assert(info->usable_bytes == 0UL);
    tinytmux_psram_format_short(info, tag, sizeof(tag));
    assert(strcmp(tag, "ERR") == 0);

    /* Restore healthy 2-chip 16 MB state and verify full cooperative March C- pass, fault footprint, and cancel */
    tinytmux_psram_set_sim_fault_mask(0, 0);
    tinytmux_psram_set_sim_fault_mask(1, 0);
    info = tinytmux_psram_retest();
    assert(info->usable_chip_count == 2);

    /* Full March C- across both chips in small chunks */
    assert(tinytmux_psram_marchc_start(1000UL) == 1);
    assert(tinytmux_psram_marchc_status()->state == TMUX_MARCHC_RUNNING);
    for (int step = 0; step < 64 && tinytmux_psram_marchc_status()->state == TMUX_MARCHC_RUNNING; ++step) {
        tinytmux_psram_marchc_step(1000UL + static_cast<oyk_u32>(step * 20), 512);
    }
    const TinyTmuxMarchCStatus* mc = tinytmux_psram_marchc_status();
    assert(mc->state == TMUX_MARCHC_PASSED);
    assert(mc->chip_percent[0] == 100 && mc->chip_percent[1] == 100);
    assert(mc->chip_result[0] == TMUX_MARCHC_CHIP_PASS && mc->chip_result[1] == TMUX_MARCHC_CHIP_PASS);

    /* Fault on CS1 (large pads) detected cleanly by March C- */
    tinytmux_psram_set_sim_fault_mask(1, 0x00000100UL);
    assert(tinytmux_psram_marchc_start(2000UL) == 1);
    for (int step = 0; step < 64 && tinytmux_psram_marchc_status()->state == TMUX_MARCHC_RUNNING; ++step) {
        tinytmux_psram_marchc_step(2000UL + static_cast<oyk_u32>(step * 20), 512);
    }
    mc = tinytmux_psram_marchc_status();
    assert(mc->state == TMUX_MARCHC_FAILED);
    assert(mc->fail_chip == 1);
    assert(strstr(tinytmux_psram_footprint_label(mc->fail_chip), "Large pads") != NULL);

    /* Fault on CS0 (small pads) detected cleanly by March C- */
    tinytmux_psram_set_sim_fault_mask(1, 0);
    tinytmux_psram_set_sim_fault_mask(0, 0x00000008UL);
    assert(tinytmux_psram_marchc_start(3000UL) == 1);
    for (int step = 0; step < 64 && tinytmux_psram_marchc_status()->state == TMUX_MARCHC_RUNNING; ++step) {
        tinytmux_psram_marchc_step(3000UL + static_cast<oyk_u32>(step * 20), 512);
    }
    mc = tinytmux_psram_marchc_status();
    assert(mc->state == TMUX_MARCHC_FAILED);
    assert(mc->fail_chip == 0);
    assert(strstr(tinytmux_psram_footprint_label(mc->fail_chip), "Small pads") != NULL);

    /* Mid-run cancellation */
    tinytmux_psram_set_sim_fault_mask(0, 0);
    assert(tinytmux_psram_marchc_start(4000UL) == 1);
    tinytmux_psram_marchc_step(4010UL, 256);
    assert(tinytmux_psram_marchc_status()->state == TMUX_MARCHC_RUNNING);
    tinytmux_psram_marchc_cancel();
    assert(tinytmux_psram_marchc_status()->state == TMUX_MARCHC_CANCELLED);

    tinytmux_psram_set_sim_chips(NULL, 0, 0, 0, NULL, 0, 0, 0);
    delete[] sim_cs1;
    delete[] sim_cs0;

    printf("[PASS] test_psram_detection_and_self_test\n");
}

struct UsbNetCap {
    int count;
    int last_slot;
    size_t last_len;
    oyk_u8 last_eth[1600];
    int udp4191_count;
    char last_udp4191[128];
};

static oyk_u16 u_read_be16(const oyk_u8* p) {
    return static_cast<oyk_u16>((static_cast<oyk_u16>(p[0]) << 8) | p[1]);
}

static void usb_eth_tx_capture(int slot, const oyk_u8* frame, size_t len, void* ctx) {
    UsbNetCap* cap = static_cast<UsbNetCap*>(ctx);
    cap->count++;
    cap->last_slot = slot;
    cap->last_len = (len <= sizeof(cap->last_eth)) ? len : sizeof(cap->last_eth);
    memcpy(cap->last_eth, frame, cap->last_len);

    if (len >= 42 && frame[12] == 0x08 && frame[13] == 0x00 && frame[23] == 17) {
        size_t ihl = static_cast<size_t>(frame[14] & 0x0F) * 4U;
        if (14U + ihl + 8U <= len) {
            oyk_u16 dport = u_read_be16(frame + 14U + ihl + 2U);
            oyk_u16 ulen  = u_read_be16(frame + 14U + ihl + 4U);
            if (dport == OYK_RF_DEFAULT_PORT && ulen >= 8 && 14U + ihl + ulen <= len) {
                size_t plen = ulen - 8U;
                if (plen >= sizeof(cap->last_udp4191)) plen = sizeof(cap->last_udp4191) - 1;
                memcpy(cap->last_udp4191, frame + 14U + ihl + 8U, plen);
                cap->last_udp4191[plen] = '\0';
                cap->udp4191_count++;
            }
        }
    }
}

struct UsbAcmPipe {
    oyk_u8 data[2048];
    size_t len;
};

static size_t usb_acm_tx_capture(int /*slot*/, const oyk_u8* data, size_t len, void* ctx) {
    UsbAcmPipe* p = static_cast<UsbAcmPipe*>(ctx);
    size_t take = (p->len + len <= sizeof(p->data)) ? len : (sizeof(p->data) - p->len);
    if (take > 0) {
        memcpy(p->data + p->len, data, take);
        p->len += take;
    }
    return take;
}

static int mock_rigctl_exchange(oyk_u32 /*host_ip*/, oyk_u16 /*port*/,
                                const char* req, size_t /*req_len*/,
                                char* reply_out, size_t reply_cap, void* /*ctx*/) {
    if (strncmp(req, "f\n", 2) == 0) {
        strncpy(reply_out, "145050000\n", reply_cap - 1);
    } else if (strncmp(req, "m\n", 2) == 0) {
        strncpy(reply_out, "PKTFM\n12500\n", reply_cap - 1);
    } else if (strncmp(req, "t\n", 2) == 0) {
        strncpy(reply_out, "0\n", reply_cap - 1);
    } else if (strncmp(req, "v\n", 2) == 0) {
        strncpy(reply_out, "VFOA\n", reply_cap - 1);
    } else {
        strncpy(reply_out, "RPRT 0\n", reply_cap - 1);
    }
    reply_out[reply_cap - 1] = '\0';
    return 1;
}

static void pump_usb_acm_ppp_pair(TinyTmuxNetBridge* bridge, int usb_slot,
                                  OykPppLink* peer_ppp,
                                  UsbAcmPipe* node_to_peer, UsbAcmPipe* peer_to_node,
                                  oyk_u32 now_ms) {
    for (int round = 0; round < 10; ++round) {
        int moved = 0;
        if (node_to_peer->len > 0) {
            oyk_u8 tmp[2048];
            size_t n = node_to_peer->len;
            memcpy(tmp, node_to_peer->data, n);
            node_to_peer->len = 0;
            peer_ppp->feed_serial_bytes(tmp, n, now_ms);
            moved = 1;
        }
        if (peer_to_node->len > 0) {
            oyk_u8 tmp[2048];
            size_t n = peer_to_node->len;
            memcpy(tmp, peer_to_node->data, n);
            peer_to_node->len = 0;
            bridge->usb_host()->on_acm_rx(usb_slot, tmp, n);
            moved = 1;
        }
        bridge->poll(now_ms + static_cast<oyk_u32>(round));
        if (!moved) break;
    }
}

static size_t pipe_tx_fn(const oyk_u8* data, size_t len, void* ctx) {
    return usb_acm_tx_capture(0, data, len, ctx);
}

static void test_usb_host_radiofoundry_cdc_net_and_acm() {
    /* 1. Verify USB CDC-NCM 16-bit NTH16 + NDP16 frame wrap & unwrap */
    oyk_u8 raw_eth[64], ntb[128], unwrapped[128];
    memset(raw_eth, 0xAA, sizeof(raw_eth));
    raw_eth[12] = 0x08; raw_eth[13] = 0x00;
    size_t nlen = tinytmux_usb_ncm16_wrap(raw_eth, sizeof(raw_eth), 7, ntb, sizeof(ntb));
    assert(nlen == 28U + sizeof(raw_eth));
    size_t ulen = tinytmux_usb_ncm16_unwrap(ntb, nlen, unwrapped, sizeof(unwrapped));
    assert(ulen == sizeof(raw_eth));
    assert(memcmp(raw_eth, unwrapped, sizeof(raw_eth)) == 0);

    OykConfig* cfg = new OykConfig();
    oyk_config_init_default(cfg);
    oyk_parse_callsign("N0CALL-7", &cfg->callsign);
    strcpy(cfg->log_path, "/tmp/oyk_tmux_logs");
    strcpy(cfg->banlist_path, "/tmp/oyk_tmux_ban.cfg");

    cfg->iface_count = 2;
    memset(&cfg->ifaces[0], 0, sizeof(cfg->ifaces[0]));
    strcpy(cfg->ifaces[0].id, "usbhnet0");
    strcpy(cfg->ifaces[0].type, "usb-cdc-net");
    strcpy(cfg->ifaces[0].device, "usbh0");
    strcpy(cfg->ifaces[0].host, "192.168.7.2");
    cfg->ifaces[0].port = 10093;
    cfg->ifaces[0].peer_count = 1;
    oyk_parse_ipv4("192.168.7.1", &cfg->ifaces[0].peers[0].ip);
    cfg->ifaces[0].peers[0].port = 10093;
    strcpy(cfg->ifaces[0].peers[0].callsign, "RF0NET");

    memset(&cfg->ifaces[1], 0, sizeof(cfg->ifaces[1]));
    strcpy(cfg->ifaces[1].id, "usbhser1");
    strcpy(cfg->ifaces[1].type, "usb-cdc-serial");
    strcpy(cfg->ifaces[1].device, "usbh1");
    strcpy(cfg->ifaces[1].host, "10.44.1.1");
    cfg->ifaces[1].port = 10093;
    cfg->ifaces[1].baud = 115200UL;
    cfg->ifaces[1].peer_count = 1;
    oyk_parse_ipv4("10.44.1.2", &cfg->ifaces[1].peers[0].ip);
    cfg->ifaces[1].peers[0].port = 10093;
    strcpy(cfg->ifaces[1].peers[0].callsign, "RF1SER");

    cfg->rig_enabled = 1;
    strcpy(cfg->rig_protocol, "radiofoundry");
    strcpy(cfg->rig_host, "192.168.7.1");
    cfg->rig_port = OYK_RF_DEFAULT_PORT;
    cfg->rig_poll_sec = 0;
    cfg->rig_notify = 1;

    OykPortableNode* node = new OykPortableNode();
    TinyTmuxNetBridge* bridge = new TinyTmuxNetBridge();
    char err[64];
    assert(node->init(cfg, TinyTmuxNetBridge::node_tx_callback, bridge, err, sizeof(err)) == 1);
    assert(bridge->init(node, 1000) == 1);

    UsbNetCap* net_cap = new UsbNetCap();
    memset(net_cap, 0, sizeof(*net_cap));
    UsbAcmPipe* node_to_radio = new UsbAcmPipe();
    UsbAcmPipe* radio_to_node = new UsbAcmPipe();
    node_to_radio->len = 0;
    radio_to_node->len = 0;
    bridge->usb_host()->set_eth_tx_hook(usb_eth_tx_capture, net_cap);
    bridge->usb_host()->set_acm_tx_hook(usb_acm_tx_capture, node_to_radio);

    /* Attach both a CDC-NCM RadioFoundry net interface AND CDC-ACM serial interface */
    static const oyk_u8 peer_mac[6] = { 0x02, 0x52, 0x46, 0x00, 0x01, 0x01 };
    static const oyk_u8 host_mac[6] = { 0x02, 0x4F, 0x59, 0x4B, 0x00, 0x02 };
    int s0 = bridge->usb_host()->attach_slot(0, TMUX_USBH_CDC_NCM, 2, 0, 0x1D6BU, 0x0104U, host_mac, "RadioFoundry-NCM");
    int s1 = bridge->usb_host()->attach_slot(1, TMUX_USBH_CDC_ACM, 3, 1, 0x1D6BU, 0x0104U, host_mac, "RadioFoundry-ACM");
    assert(s0 == 0 && s1 == 1);
    bridge->usb_host()->configure_slot_ips(0, test_parse_ip("192.168.7.2"), test_parse_ip("192.168.7.1"));

    char usbh_tok[8];
    bridge->usb_host()->format_short_status(usbh_tok, sizeof(usbh_tok));
    assert(strcmp(usbh_tok, "N+S") == 0);
    bridge->poll(1010);
    assert(node->iface(0).running == 1);

    /* 2. ARP request from RadioFoundry over USB CDC-NCM -> ARP Reply emitted */
    oyk_u8 arp_req[42];
    memset(arp_req, 0, sizeof(arp_req));
    memset(arp_req, 0xFF, 6);
    memcpy(arp_req + 6, peer_mac, 6);
    arp_req[12] = 0x08; arp_req[13] = 0x06;
    arp_req[14] = 0x00; arp_req[15] = 0x01;
    arp_req[16] = 0x08; arp_req[17] = 0x00;
    arp_req[18] = 6; arp_req[19] = 4;
    arp_req[20] = 0x00; arp_req[21] = 0x01;
    memcpy(arp_req + 22, peer_mac, 6);
    oyk_u32 rip = test_parse_ip("192.168.7.1");
    oyk_u32 lip = test_parse_ip("192.168.7.2");
    memcpy(arp_req + 28, &rip, 4);
    memcpy(arp_req + 38, &lip, 4);
    bridge->usb_host()->on_eth_rx(0, arp_req, sizeof(arp_req));
    assert(net_cap->count == 1);
    assert(net_cap->last_eth[12] == 0x08 && net_cap->last_eth[13] == 0x06);
    assert(net_cap->last_eth[21] == 0x02); /* ARP Reply */

    /* 3. RadioFoundry UDP 4191 TX & unsolicited status notification over USB CDC-NCM */
    assert(bridge->rig_rf_step_channel(1) == 1);
    assert(net_cap->udp4191_count == 1);
    assert(strcmp(net_cap->last_udp4191, "step?channel=1") == 0);

    const char* rf_status = "zone=2&pos=4&channel_name=\"VHF RPT\"&freq=146520000&mode=\"FM\"";
    oyk_u8 notif_eth[256];
    memset(notif_eth, 0, sizeof(notif_eth));
    memcpy(notif_eth, host_mac, 6);
    memcpy(notif_eth + 6, peer_mac, 6);
    notif_eth[12] = 0x08; notif_eth[13] = 0x00;
    size_t rf_len = strlen(rf_status);
    oyk_u8* nip = notif_eth + 14;
    nip[0] = 0x45;
    oyk_u16 nip_len = static_cast<oyk_u16>(28U + rf_len);
    nip[2] = static_cast<oyk_u8>(nip_len >> 8);
    nip[3] = static_cast<oyk_u8>(nip_len & 0xFF);
    nip[8] = 64; nip[9] = 17;
    memcpy(nip + 12, &rip, 4);
    memcpy(nip + 16, &lip, 4);
    oyk_u8* nudp = nip + 20;
    nudp[0] = static_cast<oyk_u8>(OYK_RF_DEFAULT_PORT >> 8);
    nudp[1] = static_cast<oyk_u8>(OYK_RF_DEFAULT_PORT & 0xFF);
    nudp[2] = nudp[0]; nudp[3] = nudp[1];
    oyk_u16 nulen = static_cast<oyk_u16>(8U + rf_len);
    nudp[4] = static_cast<oyk_u8>(nulen >> 8);
    nudp[5] = static_cast<oyk_u8>(nulen & 0xFF);
    memcpy(nudp + 8, rf_status, rf_len);
    /* Wrap inside NCM16 NTB and inject into bridge via on_ncm_rx! */
    oyk_u8 notif_ntb[320];
    size_t notif_ntb_len = tinytmux_usb_ncm16_wrap(notif_eth, 14U + nip_len, 9, notif_ntb, sizeof(notif_ntb));
    assert(notif_ntb_len > 0);
    bridge->usb_host()->on_ncm_rx(0, notif_ntb, notif_ntb_len);
    assert(bridge->rig_status()->reachable == 1);
    assert(bridge->rig_status()->zone == 2);
    assert(bridge->rig_status()->pos == 4);
    assert(strcmp(bridge->rig_status()->channel_name, "VHF RPT") == 0);
    assert(bridge->rig_status()->freq_hz == 146520000UL);

    /* 4. Negotiate PPP over USB CDC-ACM (usbh1), detach NCM, verify "SER" + rig UDP over PPP! */
    OykPppLink* radio_acm_ppp = new OykPppLink();
    radio_acm_ppp->init(test_parse_ip("10.44.1.2"), test_parse_ip("10.44.1.1"),
                        10093, 0xCAFEBABEUL, pipe_tx_fn, radio_to_node);
    radio_acm_ppp->open(1100);
    pump_usb_acm_ppp_pair(bridge, 1, radio_acm_ppp, node_to_radio, radio_to_node, 1110);
    assert(bridge->ppp_link(0) != NULL && bridge->ppp_link(0)->is_up() == 1);

    bridge->usb_host()->detach_slot(0);
    bridge->usb_host()->format_short_status(usbh_tok, sizeof(usbh_tok));
    assert(strcmp(usbh_tok, "SER") == 0);
    assert(bridge->rig_rf_step_zone(1) == 1);
    assert(node_to_radio->len > 0); /* PPP HDLC frame queued to USB CDC-ACM */

    /* 5. Hamlib rigctld TCP exchange verification */
    strcpy(cfg->rig_protocol, "rigctl");
    cfg->rig_port = OYK_RIGCTL_DEFAULT_PORT;
    OykConfigApplyResult apply_res;
    node->apply_config(cfg, 0, &apply_res);
    bridge->sync_rig_config();
    bridge->set_rig_tcp_hook(mock_rigctl_exchange, NULL);
    assert(bridge->rig_poll_now(1200) == 1);
    assert(bridge->rig_status()->freq_hz == 145050000UL);
    assert(strcmp(bridge->rig_status()->mode, "PKTFM") == 0);
    assert(bridge->rig_set_freq(144390000UL) == 1);
    assert(bridge->rig_status()->freq_hz == 144390000UL);

    delete radio_acm_ppp;
    delete radio_to_node;
    delete node_to_radio;
    delete net_cap;
    delete bridge;
    delete node;
    delete cfg;
    printf("[PASS] test_usb_host_radiofoundry_cdc_net_and_acm\n");
}

struct ConsoleCap {
    char buf[131072];
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

static void test_vt100_console_full_parity() {
    oyk_u32* sim_cs0 = new oyk_u32[256];
    oyk_u32* sim_cs1 = new oyk_u32[256];
    tinytmux_psram_set_sim_fault_mask(0, 0);
    tinytmux_psram_set_sim_fault_mask(1, 0);
    tinytmux_psram_set_sim_chips(sim_cs0, 256, 0x0DU, 0x5DU, sim_cs1, 256, 0x0DU, 0x5DU);
    tinytmux_psram_init_and_test();

    OykConfig* cfg = new OykConfig();
    oyk_config_init_default(cfg);
    oyk_parse_callsign("KR4OYK", &cfg->callsign);
    oyk_parse_callsign_with_default_ssid("KR4OYK", OYK_DEFAULT_NODE_SSID, &cfg->callsign);
    strcpy(cfg->log_path, "/tmp/oyk_tmux_logs");
    strcpy(cfg->banlist_path, "/tmp/oyk_tmux_ban.cfg");
    strcpy(cfg->source_path, "/nonexistent_ro_dir/node.cfg");
    cfg->rig_enabled = 1;
    strcpy(cfg->rig_protocol, "rigctl");
    strcpy(cfg->rig_host, "127.0.0.1");
    cfg->rig_port = OYK_RIGCTL_DEFAULT_PORT;

    OykPortableNode* node = new OykPortableNode();
    TinyTmuxNetBridge* bridge = new TinyTmuxNetBridge();
    char err[64];
    assert(node->init(cfg, TinyTmuxNetBridge::node_tx_callback, bridge, err, sizeof(err)) == 1);
    bridge->init(node, 100);
    bridge->set_rig_tcp_hook(mock_rigctl_exchange, NULL);

    TinyTmuxConsole* con = new TinyTmuxConsole();
    g_test_con = con;
    node->set_local_event_callback(test_con_local_cb, NULL);

    ConsoleCap* cap = new ConsoleCap();
    cap->len = 0;
    cap->buf[0] = '\0';
    oyk_u32 t = 1000UL;
    con->init(node, console_tx_cap, cap);
    con->set_net_bridge(bridge);
    con->set_now_ms(t);
    con->render();
    assert(strstr(cap->buf, "KR4OYK-7") != NULL);
    assert(strstr(cap->buf, "IN:ALLOW") != NULL);
    assert(strstr(cap->buf, "PSRAM:16M") != NULL);
    assert(strstr(cap->buf, "USBH:-") != NULL);

    /* Open PSRAM Diagnostics via Node Menu (M -> P), run quick retest (T) & cooperative March C- (M), close (Esc) */
    cap->len = 0; cap->buf[0] = '\0';
    const char* psram_seq = "mp";
    con->feed_input(reinterpret_cast<const oyk_u8*>(psram_seq), strlen(psram_seq));
    assert(con->view_mode() == TMUX_VIEW_PSRAM);
    assert(strstr(cap->buf, "PSRAM DIAGNOSTICS") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("t"), 1);
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("m"), 1);
    assert(tinytmux_psram_marchc_status()->state == TMUX_MARCHC_RUNNING);
    assert(strstr(cap->buf, "March C-") != NULL);
    for (int s = 0; s < 16 && tinytmux_psram_marchc_status()->state == TMUX_MARCHC_RUNNING; ++s) {
        cap->len = 0; cap->buf[0] = '\0';
        con->tick(t += 150UL);
    }
    assert(tinytmux_psram_marchc_status()->state == TMUX_MARCHC_PASSED);
    assert(strstr(cap->buf, "PASSED") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(t += 50UL);
    assert(con->view_mode() == TMUX_VIEW_CONSOLE);

    /* Open Radio Control (R), poll Hamlib (r), close (Esc) */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("rr"), 2);
    assert(con->view_mode() == TMUX_VIEW_RIG);
    assert(strstr(cap->buf, "RADIO CONTROL") != NULL);
    assert(bridge->rig_status()->freq_hz == 145050000UL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(t += 50UL);
    assert(con->view_mode() == TMUX_VIEW_CONSOLE);

    /* Open Files Area (F), toggle between 1:Pending and 2:Approved tabs, close (Esc) */
    con->feed_input(reinterpret_cast<const oyk_u8*>("f"), 1);
    assert(con->view_mode() == TMUX_VIEW_FILES);
    assert(con->files_tab() == 0);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\t"), 1);
    assert(con->files_tab() == 1);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(t += 50UL);
    assert(con->view_mode() == TMUX_VIEW_CONSOLE);

    /* Verify split-packet CSI, minicom SS3, modified CSI, Home/End, Shift-Tab, unknown CSI, and double-Esc in Settings */
    con->feed_input(reinterpret_cast<const oyk_u8*>("s"), 1);
    assert(con->view_mode() == TMUX_VIEW_SETTINGS);
    assert(con->settings_tab() == 0);
    assert(con->current_settings_sel() == 0);

    /* Split-byte CSI Down (ESC, '[', 'B' across 3 USB-CDC reads) */
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    assert(con->view_mode() == TMUX_VIEW_SETTINGS);
    con->feed_input(reinterpret_cast<const oyk_u8*>("["), 1);
    con->feed_input(reinterpret_cast<const oyk_u8*>("B"), 1);
    assert(con->view_mode() == TMUX_VIEW_SETTINGS);
    assert(con->current_settings_sel() == 1);

    /* Split-byte CSI Up (ESC, '[', 'A') */
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->feed_input(reinterpret_cast<const oyk_u8*>("["), 1);
    con->feed_input(reinterpret_cast<const oyk_u8*>("A"), 1);
    assert(con->current_settings_sel() == 0);

    /* Split-byte and single-buffer minicom SS3 Down/Up (ESC O B / ESC O A) */
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->feed_input(reinterpret_cast<const oyk_u8*>("O"), 1);
    con->feed_input(reinterpret_cast<const oyk_u8*>("B"), 1);
    assert(con->current_settings_sel() == 1);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1bOB"), 3);
    assert(con->current_settings_sel() == 2);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1bOA"), 3);
    assert(con->current_settings_sel() == 1);

    /* Modified arrow, End, Home, SS3 Right/Left, Shift-Tab, and unknown CSI swallow */
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b[1;2B"), 6);
    assert(con->current_settings_sel() == 2);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b[F"), 3);
    assert(con->current_settings_sel() == 10);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b[H"), 3);
    assert(con->current_settings_sel() == 0);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1bOC"), 3);
    assert(con->settings_tab() == 1);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1bOD"), 3);
    assert(con->settings_tab() == 0);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b[Z"), 3);
    assert(con->settings_tab() == 6);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\t"), 1);
    assert(con->settings_tab() == 0);
    con->feed_input(reinterpret_cast<const oyk_u8*>("6"), 1);
    assert(con->settings_tab() == 5);
    con->feed_input(reinterpret_cast<const oyk_u8*>("7"), 1);
    assert(con->settings_tab() == 6);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b[99~"), 5);
    assert(con->view_mode() == TMUX_VIEW_SETTINGS);

    /* Double-Esc cancels immediately without waiting for timeout */
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b\x1b"), 2);
    assert(con->view_mode() == TMUX_VIEW_CONSOLE);

    /* Open 5-tab Settings (S), edit callsign -> K7TMP-7, switch to 4:Interfaces, add usb-cdc-net, save via F2 */
    con->feed_input(reinterpret_cast<const oyk_u8*>("s"), 1);
    assert(con->view_mode() == TMUX_VIEW_SETTINGS);
    assert(con->settings_tab() == 0);
    /* Backspace K7TMP-7 over KR4OYK-7 (10 backspaces clears it) + Enter */
    const char* edit_call = "\r\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7fK7TMP-7\r";
    con->feed_input(reinterpret_cast<const oyk_u8*>(edit_call), strlen(edit_call));
    /* Switch to tab 4:Interfaces, press N, step through 4-step Add Interface wizard (usb-cdc-net -> usbnet0), save via F2 */
    con->feed_input(reinterpret_cast<const oyk_u8*>("4"), 1);
    assert(con->settings_tab() == 3);
    cap->len = 0; cap->buf[0] = '\0';
    const char* add_usb = "n5\r\r\r\r";
    con->feed_input(reinterpret_cast<const oyk_u8*>(add_usb), strlen(add_usb));
    assert(strstr(cap->buf, "Add Interface") != NULL);
    assert(strstr(cap->buf, "[##......] 1/4") != NULL);
    assert(strstr(cap->buf, "[########] 4/4") != NULL);
    assert(strstr(cap->buf, "Review interface configuration") != NULL);
    /* Save & Apply with F2 (ESC O Q); read-only source_path should apply in-memory gracefully */
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1bOQ"), 3);
    assert(con->view_mode() == TMUX_VIEW_CONSOLE);
    char new_cs[16];
    oyk_format_callsign(&node->config().callsign, new_cs, sizeof(new_cs));
    assert(strcmp(new_cs, "K7TMP-7") == 0);
    assert(node->iface_count() == 2);
    assert(strcmp(node->iface(1).cfg.id, "usbnet0") == 0);
    assert(strcmp(node->iface(1).cfg.type, "usb-cdc-net") == 0);
    assert(strstr(con->status_line(), "IN MEMORY ONLY") != NULL);

    /* Switch to Local Terminal via Tab, connect to /bbs, switch back via Esc keeping session active */
    cap->len = 0; cap->buf[0] = '\0';
    const char* seq = "\t/bbs\r\x1b";
    con->feed_input(reinterpret_cast<const oyk_u8*>(seq), strlen(seq));
    con->tick(t += 50UL);
    assert(con->view_mode() == TMUX_VIEW_CONSOLE);
    assert(con->has_active_terminal() == 1);
    assert(strstr(cap->buf, "Tab/F9:Term(ACTIVE)") != NULL);

    /* Misplaced QSPI Flash on CS0 triggers warning banner + startup flag; CS1 flash clears warning */
    tinytmux_psram_set_sim_chips(NULL, 0, 0, 0, NULL, 0, 0, 0);
    tinytmux_psram_set_sim_flash(0, TMUX_FLEXSPI_FLASH_NOR, 0xEFU, 0x40U, 0x18U, 16UL * 1024UL * 1024UL);
    tinytmux_psram_set_sim_flash(1, TMUX_FLEXSPI_NONE, 0, 0, 0, 0);
    tinytmux_psram_retest();
    assert(tinytmux_psram_info()->misplaced_flash_cs0 == 1);
    assert(tinytmux_psram_info()->has_valid_qspi_flash_cs1 == 0);
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("mp"), 2);
    assert(strstr(cap->buf, "WRONG PADS (CS0 / Pin 48") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(t += 50UL);

    tinytmux_psram_set_sim_flash(0, TMUX_FLEXSPI_NONE, 0, 0, 0, 0);
    tinytmux_psram_set_sim_flash(1, TMUX_FLEXSPI_FLASH_NOR, 0xEFU, 0x40U, 0x18U, 16UL * 1024UL * 1024UL);
    tinytmux_psram_retest();
    assert(tinytmux_psram_info()->misplaced_flash_cs0 == 0);
    assert(tinytmux_psram_info()->has_valid_qspi_flash_cs1 == 1);
    tinytmux_psram_set_sim_flash(1, TMUX_FLEXSPI_NONE, 0, 0, 0, 0);

    /* Storage Manager: menu G, select tiers, wrong confirmation rejected, exact FORMAT formats */
    tinytmux_fs_set_sim_tiers(1, 1, 1);
    tinytmux_fs_unmount();
    assert(tinytmux_fs_mount() == 1);
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("mg"), 2);
    assert(con->view_mode() == TMUX_VIEW_STORAGE);
    assert(strstr(cap->buf, "STORAGE MANAGER") != NULL);
    assert(strstr(cap->buf, "microSD Card (SDIO)") != NULL);
    assert(strstr(cap->buf, "ERASES ALL") != NULL || strstr(cap->buf, "ERASES ALL DATA") != NULL);
    /* Wrong confirmation text does NOT format; modal instructs operator to type FORMAT */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("f"), 1);
    assert(strstr(cap->buf, "CONFIRM DESTRUCTIVE FORMAT") != NULL);
    assert(strstr(cap->buf, "Type FORMAT") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("format\r"), 7);
    assert(tinytmux_fs_sim_format_count(TMUX_STORAGE_SD) == 0);
    /* Exact FORMAT formats selected tier (microSD) */
    con->feed_input(reinterpret_cast<const oyk_u8*>("fFORMAT\r"), 8);
    assert(tinytmux_fs_sim_format_count(TMUX_STORAGE_SD) == 1);
    /* Select QSPI (Down) and Internal (End) tiers and format each */
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b[B"), 3);
    con->feed_input(reinterpret_cast<const oyk_u8*>("fFORMAT\r"), 8);
    assert(tinytmux_fs_sim_format_count(TMUX_STORAGE_QSPI) == 1);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b[F"), 3);
    con->feed_input(reinterpret_cast<const oyk_u8*>("fFORMAT\r"), 8);
    assert(tinytmux_fs_sim_format_count(TMUX_STORAGE_INTERNAL) == 1);
    con->feed_input(reinterpret_cast<const oyk_u8*>("r"), 1);
    assert(con->view_mode() == TMUX_VIEW_STORAGE);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(t += 50UL);
    assert(con->view_mode() == TMUX_VIEW_CONSOLE);
    tinytmux_fs_set_sim_tiers(1, 0, 1);

    /* HalfKay Bootloader workflow: cancel first, then confirm with active connection & verify notice + drain */
    assert(tinytmux_board_sim_bootloader_count() == 0);
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("mu"), 2);
    assert(con->view_mode() == TMUX_VIEW_BOOTLOADER);
    assert(strstr(cap->buf, "OFFLINE in HalfKay bootloader") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("n"), 1);
    assert(con->view_mode() == TMUX_VIEW_CONSOLE);
    assert(tinytmux_board_sim_bootloader_count() == 0);

    /* Simulate a connected remote AX.25 user and confirm bootloader entry */
    OykFrame sabm;
    memset(&sabm, 0, sizeof(sabm));
    oyk_parse_callsign("K7RMT-1", &sabm.src);
    sabm.dst = node->config().callsign;
    sabm.is_command = 1;
    sabm.control = 0x3FU;
    node->on_rx_frame(0, &sabm, 0x0A000002UL, 10093U, 0);
    assert(node->total_active_connections() >= 1);
    /* Verify unchanged standard bootloader prompt when no auto-learned peers are pending */
    node->discard_auto_learned_peers();

    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("u"), 1);
    assert(con->view_mode() == TMUX_VIEW_BOOTLOADER);
    assert(strstr(cap->buf, "firmware upgrade") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("y"), 1);
    assert(tinytmux_board_sim_bootloader_count() == 0); /* Still draining */
    con->tick(t + 1000UL);
    assert(tinytmux_board_sim_bootloader_count() == 0); /* Not 3s yet */
    con->tick(t + 3050UL);
    assert(tinytmux_board_sim_bootloader_count() == 1);
    assert(strstr(cap->buf, "73 de ") != NULL);
    assert(node->disconnecting_count() == 0);
    assert(tinytmux_fs_is_mounted() == 0);

    g_test_con = NULL;
    tinytmux_psram_set_sim_chips(NULL, 0, 0, 0, NULL, 0, 0, 0);
    delete[] sim_cs1;
    delete[] sim_cs0;
    delete cap;
    delete con;
    delete bridge;
    delete node;
    delete cfg;
    printf("[PASS] test_vt100_console_full_parity\n");
}

static void test_fs_hooks() {
    assert(tinytmux_fs_is_mounted() == 0);
    assert(strcmp(tinytmux_fs_status_label(), "UNMOUNTED") == 0);

    /* Tier 1: microSD present */
    tinytmux_fs_set_sim_tiers(1, 1, 1);
    assert(tinytmux_fs_mount() == 1);
    assert(tinytmux_fs_is_mounted() == 1);
    assert(tinytmux_fs_active_tier() == TMUX_STORAGE_SD);
    assert(tinytmux_fs_is_flash_storage() == 0);
    assert(strcmp(tinytmux_fs_status_label(), "SDIO MOUNTED") == 0);

    /* Tier 2 fallback: No SD, QSPI Flash present */
    tinytmux_fs_unmount();
    tinytmux_fs_set_sim_tiers(0, 1, 1);
    assert(tinytmux_fs_mount() == 1);
    assert(tinytmux_fs_active_tier() == TMUX_STORAGE_QSPI);
    assert(tinytmux_fs_is_flash_storage() == 1);
    assert(strcmp(tinytmux_fs_status_label(), "QSPI FLASH") == 0);

    /* Tier 3 fallback: Neither SD nor QSPI, Internal 4 MB LittleFS active */
    tinytmux_fs_unmount();
    tinytmux_fs_set_sim_tiers(0, 0, 1);
    assert(tinytmux_fs_mount() == 1);
    assert(tinytmux_fs_active_tier() == TMUX_STORAGE_INTERNAL);
    assert(tinytmux_fs_is_flash_storage() == 1);
    assert(strcmp(tinytmux_fs_status_label(), "INT FLASH 4M") == 0);

    TinyTmuxStorageTierInfo ti;
    assert(tinytmux_fs_tier_info(TMUX_STORAGE_INTERNAL, &ti) == 1);
    assert(ti.mounted == 1 && ti.total_kb == 4096UL);

    assert(oyk_imxrt_mkdir("/test") == -1);
    assert(oyk_imxrt_is_regular_file("/test/a.txt") == 0);
    assert(oyk_imxrt_is_directory("/test") == 0);
    assert(oyk_imxrt_opendir("/") == NULL);
    tinytmux_fs_unmount();
    tinytmux_fs_set_sim_tiers(1, 0, 1);
    assert(tinytmux_fs_is_mounted() == 0);
    assert(strcmp(tinytmux_fs_status_label(), "UNMOUNTED") == 0);
    printf("[PASS] test_fs_hooks\n");
}

struct PrinterSimCap {
    oyk_u8 buf[512];
    size_t len;
    int last_port;
};

static size_t sim_usb_printer_tx(int /*slot*/, const oyk_u8* data, size_t len, void* ctx) {
    PrinterSimCap* c = static_cast<PrinterSimCap*>(ctx);
    if (!c || !data || c->len + len >= sizeof(c->buf)) return 0;
    memcpy(c->buf + c->len, data, len);
    c->len += len;
    c->buf[c->len] = '\0';
    return len;
}

static void sim_uart_printer_tx(int logical_port, const oyk_u8* data, size_t len, void* ctx) {
    PrinterSimCap* c = static_cast<PrinterSimCap*>(ctx);
    if (!c || !data || c->len + len >= sizeof(c->buf)) return;
    c->last_port = logical_port;
    memcpy(c->buf + c->len, data, len);
    c->len += len;
    c->buf[c->len] = '\0';
}

static void test_printer_usb_and_serial() {
    OykConfig* cfg = new OykConfig;
    oyk_config_init_default(cfg);
    oyk_parse_callsign("KR4OYK-7", &cfg->callsign);
    cfg->printer_enabled = 1;
    strcpy(cfg->printer_device, "usb-printer");
    strcpy(cfg->printer_protocol, "mx80");
    cfg->printer_baud = 9600UL;
    cfg->printer_level = OYK_LOG_ERROR | OYK_LOG_BAN | OYK_LOG_INFO;
    cfg->printer_condensed = 1;

    OykPortableNode* node = new OykPortableNode;
    TinyTmuxNetBridge* bridge = new TinyTmuxNetBridge;
    char err[128];
    assert(node->init(cfg, TinyTmuxNetBridge::node_tx_callback, bridge, err, sizeof(err)) == 1);
    assert(bridge->init(node, 1000UL) == 1);

    PrinterSimCap usb_cap;
    memset(&usb_cap, 0, sizeof(usb_cap));
    bridge->usb_host()->set_printer_tx_hook(sim_usb_printer_tx, &usb_cap);
    int pslot = bridge->usb_host()->attach_slot(-1, TMUX_USBH_PRINTER, 0x20U, 0,
                                                0x04B8U, 0x0005U, NULL, "usbprn0");
    assert(pslot >= 0 && bridge->usb_host()->has_printer_device() == 1);
    char hst[16];
    bridge->usb_host()->format_short_status(hst, sizeof(hst));
    assert(strcmp(hst, "PRN") == 0);

    bridge->sync_printer_config();
    assert(usb_cap.len == 3);
    assert(usb_cap.buf[0] == 0x1BU && usb_cap.buf[1] == '@' && usb_cap.buf[2] == 0x0FU);

    node->log_message(OYK_LOG_ERROR, "USB printer failure test");
    const char* s = reinterpret_cast<const char*>(usb_cap.buf);
    assert(strstr(s, "\x1B" "E") != NULL);
    assert(strstr(s, "USB printer failure test\x1B" "F\r\n") != NULL);

    /* Switch live to serial printer on ser3 with ESC/P2 */
    PrinterSimCap ser_cap;
    memset(&ser_cap, 0, sizeof(ser_cap));
    ser_cap.last_port = -1;
    tinytmux_uart_set_sim_tx_hook(sim_uart_printer_tx, &ser_cap);

    strcpy(cfg->printer_device, "ser3");
    strcpy(cfg->printer_protocol, "escp2");
    cfg->printer_baud = 19200UL;
    cfg->printer_condensed = 0;
    OykConfigApplyResult ar;
    assert(node->apply_config(cfg, 0, &ar) == 1);
    bridge->rebind_interfaces(2000UL);

    assert(ser_cap.last_port == 3);
    assert(ser_cap.len >= 7 && ser_cap.buf[0] == 0x1BU && ser_cap.buf[1] == '@' &&
           ser_cap.buf[5] == 0x1BU && ser_cap.buf[6] == 'M');

    node->log_message(OYK_LOG_BAN, "Serial printer ban");
    const char* ss = reinterpret_cast<const char*>(ser_cap.buf + 7);
    assert(strstr(ss, "\x1B" "E\x1B-\x01") != NULL);
    assert(strstr(ss, "Serial printer ban\x1B-\x00\x1B" "F\r\n") != NULL);

    tinytmux_uart_set_sim_tx_hook(NULL, NULL);
    delete bridge;
    delete node;
    delete cfg;
    printf("[PASS] test_printer_usb_and_serial\n");
}

static void test_terminal_inquiry_and_ui_modals() {
    OykConfig* cfg = new OykConfig();
    oyk_config_init_default(cfg);
    oyk_parse_callsign("KR4OYK-7", &cfg->callsign);
    strcpy(cfg->source_path, "/nonexistent_ro_dir/node.cfg");

    OykPortableNode* node = new OykPortableNode();
    TinyTmuxNetBridge* bridge = new TinyTmuxNetBridge();
    char err[64];
    assert(node->init(cfg, TinyTmuxNetBridge::node_tx_callback, bridge, err, sizeof(err)) == 1);
    assert(bridge->init(node, 100UL) == 1);

    TinyTmuxConsole* con = new TinyTmuxConsole();
    ConsoleCap* cap = new ConsoleCap();
    cap->len = 0; cap->buf[0] = '\0';
    con->init(node, console_tx_cap, cap);
    con->set_net_bridge(bridge);
    con->set_now_ms(500UL);

    /* 1. on_usb_connect emits DA1 + DA2 inquiry, starts in VT100 with DEC single-line boxes */
    con->on_usb_connect();
    assert(con->term_type() == TMUX_TERM_VT100);
    assert(strstr(cap->buf, "\033[c\033[>c") != NULL);
    assert(strstr(cap->buf, "\033(0") != NULL);
    assert(strstr(cap->buf, "\033(B") != NULL);

    /* 2. VT220 DA1 reply upgrades capability and enables cursor-hide sequence */
    cap->len = 0; cap->buf[0] = '\0';
    const char* da_vt220 = "\033[?62;1;2;6c";
    con->feed_input(reinterpret_cast<const oyk_u8*>(da_vt220), strlen(da_vt220));
    assert(con->term_type() == TMUX_TERM_VT220);
    assert(strcmp(con->term_type_label(), "VT220") == 0);
    assert(strstr(cap->buf, "\033[?25l") != NULL);
    assert(strstr(cap->buf, "VT220") != NULL);

    /* 3. VT340 DA1 reply with param 22 enables ANSI colors & dynamic dark theme SGRs */
    cap->len = 0; cap->buf[0] = '\0';
    const char* da_vt340 = "\033[?64;1;2;22c";
    con->feed_input(reinterpret_cast<const oyk_u8*>(da_vt340), strlen(da_vt340));
    assert(con->term_type() == TMUX_TERM_VT340);
    assert(strstr(cap->buf, "\033[1;96m") != NULL);
    assert(strstr(cap->buf, "\033[96m") != NULL);

    /* 4. xterm-256color DA2 reply enables 256-color SGR sequences */
    cap->len = 0; cap->buf[0] = '\0';
    const char* da_x256 = "\033[>41;354;0c";
    con->feed_input(reinterpret_cast<const oyk_u8*>(da_x256), strlen(da_x256));
    assert(con->term_type() == TMUX_TERM_XTERM256);
    assert(strcmp(con->term_type_label(), "xterm-256") == 0);
    assert(strstr(cap->buf, "\033[1;38;5;") != NULL);
    assert(strstr(cap->buf, ";48;5;") != NULL);

    /* 5. Add Interface 4-step wizard: corner progress bar, Back navigation, driver switch, and Summary commit */
    con->feed_input(reinterpret_cast<const oyk_u8*>("s4"), 2);
    assert(con->view_mode() == TMUX_VIEW_SETTINGS);
    assert(con->settings_tab() == 3);

    /* Open wizard, pick driver 4 (kiss-serial -> kiss0), advance to Step 4 Summary without committing */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("n4\r\r\r"), 5);
    assert(strstr(cap->buf, "Add Interface") != NULL);
    assert(strstr(cap->buf, "[##......] 1/4") != NULL);
    assert(strstr(cap->buf, "[####....] 2/4") != NULL);
    assert(strstr(cap->buf, "[######..] 3/4") != NULL);
    assert(strstr(cap->buf, "[########] 4/4") != NULL);
    assert(strstr(cap->buf, "kiss0") != NULL);
    assert(node->iface_count() == 1); /* Not committed yet */

    /* Navigate back Step 4 -> 3 -> 2 -> 1 ('b', Ctrl+B, Ctrl+B), switch driver to 1 (axudp), advance to Finish */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("b\x02\x02" "1\r\r\r"), 7);
    assert(strstr(cap->buf, "axudp1") != NULL);
    assert(strstr(cap->buf, "Review interface configuration") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);

    /* Add usb-cdc-net twice via wizard -> usbnet0, then usbnet1 */
    con->feed_input(reinterpret_cast<const oyk_u8*>("n5\r\r\r\r"), 6);
    con->feed_input(reinterpret_cast<const oyk_u8*>("n5\r\r\r\r"), 6);
    /* Save in memory via F2 */
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1bOQ"), 3);
    assert(node->iface_count() == 4);
    assert(strcmp(node->iface(1).cfg.id, "axudp1") == 0);
    assert(strcmp(node->iface(2).cfg.id, "usbnet0") == 0);
    assert(strcmp(node->iface(3).cfg.id, "usbnet1") == 0);

    /* 6. Edit Interface single-view dialog via 'e' on Settings Tab 4 */
    con->feed_input(reinterpret_cast<const oyk_u8*>("s4"), 2);
    assert(con->view_mode() == TMUX_VIEW_SETTINGS);
    assert(con->settings_tab() == 3);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b[H\x1b[B"), 6); /* Home, Down -> axudp1 */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("e"), 1);
    assert(strstr(cap->buf, "Edit Interface") != NULL);
    assert(strstr(cap->buf, "1/4") == NULL);
    assert(strstr(cap->buf, "< Back") == NULL);
    assert(strstr(cap->buf, "axudp1") != NULL);
    assert(strstr(cap->buf, "0.0.0.0") != NULL);
    assert(strstr(cap->buf, "10093") != NULL);
    assert(strstr(cap->buf, "[  Save  ]") != NULL);
    /* Move to Interface ID (field 1), test conflicting ID rejection and same-ID acceptance */
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b[B"), 3);
    const char* dup_id = node->iface(0).cfg.id;
    char edit_seq[64];
    sprintf(edit_seq, "\x7f\x7f\x7f\x7f\x7f\x7f%s", dup_id);
    con->feed_input(reinterpret_cast<const oyk_u8*>(edit_seq), strlen(edit_seq));
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);
    assert(strstr(cap->buf, "Duplicate interface ID") != NULL);
    memset(edit_seq, 0x7f, strlen(dup_id));
    strcpy(edit_seq + strlen(dup_id), "axudp1");
    con->feed_input(reinterpret_cast<const oyk_u8*>(edit_seq), strlen(edit_seq));
    /* Tab across Description -> Host ("0.0.0.0" -> "127.0.0.1") and Port ("10093" -> "10193"), then Save directly */
    const char* fields_seq = "\t\t\x7f\x7f\x7f\x7f\x7f\x7f\x7f" "127.0.0.1\t" "\x7f\x7f\x7f\x7f\x7f" "10193";
    con->feed_input(reinterpret_cast<const oyk_u8*>(fields_seq), strlen(fields_seq));
    con->feed_input(reinterpret_cast<const oyk_u8*>("\r\x1bOQ"), 4);
    assert(node->iface_count() == 4);
    assert(strcmp(node->iface(1).cfg.id, "axudp1") == 0);
    assert(strcmp(node->iface(1).cfg.host, "127.0.0.1") == 0);
    assert(node->iface(1).cfg.port == 10193);

    /* Open Edit Interface and cancel with Esc */
    con->feed_input(reinterpret_cast<const oyk_u8*>("s4e"), 3);
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(550UL);
    assert(strstr(cap->buf, "Edit interface cancelled.") != NULL);

    /* 7. Open Peers tab via 'e' shortcut, add peer via Edit Peer modal, and test Bootloader auto-peer Review flow */
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1); /* back to console */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("e"), 1);
    assert(con->view_mode() == TMUX_VIEW_SETTINGS);
    assert(con->settings_tab() == 4);
    assert(strstr(cap->buf, "5:Peers") != NULL);
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("a"), 1);
    assert(strstr(cap->buf, "Add Peer") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("W1AW-2\tDesc\t\t10.0.0.5\t10095\r\x1bOQ"), 33);
    assert(node->config().peer_count >= 1);
    assert(strcmp(node->config().peers[node->config().peer_count - 1].callsign, "W1AW-2") == 0);

    /* Trigger an auto-learned peer and open Bootloader prompt -> should offer Save/Discard/Review */
    OykFrame sabm_al;
    memset(&sabm_al, 0, sizeof(sabm_al));
    sabm_al.dst = node->config().callsign;
    assert(oyk_parse_callsign("N4AUTO", &sabm_al.src));
    sabm_al.is_command = 1;
    sabm_al.control = 0x3FU;
    node->on_rx_frame(0, &sabm_al, 0x7F000001UL, 10099, 0);
    assert(node->auto_learned_peer_count() == 1);

    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("u"), 1);
    assert(con->view_mode() == TMUX_VIEW_BOOTLOADER);
    assert(strstr(cap->buf, "auto-learned peer") != NULL);
    assert(strstr(cap->buf, "[R] Review Peers") != NULL);
    con->feed_input(reinterpret_cast<const oyk_u8*>("r"), 1);
    assert(con->view_mode() == TMUX_VIEW_SETTINGS);
    assert(con->settings_tab() == 4);
    con->feed_input(reinterpret_cast<const oyk_u8*>("k\x1bOQ"), 4);
    assert(node->auto_learned_peer_count() == 0);

    delete cap;
    delete con;
    delete bridge;
    delete node;
    delete cfg;
    printf("[PASS] test_terminal_inquiry_and_ui_modals\n");
}

static void tmux_save_capture(const char* name, TinyTmuxConsole* con, ConsoleCap* cap) {
    const char* dir = getenv("OYK_UI_CAPTURE_DIR");
    if (!dir || !dir[0]) return;
    cap->len = 0;
    cap->buf[0] = '\0';
    con->render();
    char path[512];
    sprintf(path, "%s/tinytmux_%s.ansi", dir, name);
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(cap->buf, 1, cap->len, f);
        fclose(f);
    }
}

static void test_tinytmux_ui_flows_and_captures() {
    OykConfig* cfg = new OykConfig();
    oyk_config_init_default(cfg);
    oyk_parse_callsign("KR4OYK-7", &cfg->callsign);
    strcpy(cfg->source_path, "/nonexistent_ro_dir/node.cfg");
    cfg->peer_count = 1;
    strcpy(cfg->peers[0].id, "remote0");
    strcpy(cfg->peers[0].callsign, "R3MOT-7");
    strcpy(cfg->peers[0].description, "Remote PoPT Node");
    strcpy(cfg->peers[0].interface_id, "axudp0");
    strcpy(cfg->peers[0].host, "12.34.56.78");
    cfg->peers[0].port = 10093;
    cfg->peers[0].allow_inbound = 1;
    cfg->peers[0].allow_outbound = 1;

    OykPortableNode* node = new OykPortableNode();
    TinyTmuxNetBridge* bridge = new TinyTmuxNetBridge();
    char err[64];
    assert(node->init(cfg, TinyTmuxNetBridge::node_tx_callback, bridge, err, sizeof(err)) == 1);
    assert(bridge->init(node, 100UL) == 1);

    TinyTmuxConsole* con = new TinyTmuxConsole();
    ConsoleCap* cap = new ConsoleCap();
    cap->len = 0; cap->buf[0] = '\0';
    con->init(node, console_tx_cap, cap);
    con->set_net_bridge(bridge);
    con->set_now_ms(500UL);
    con->on_usb_connect();
    /* Negotiate xterm-256color for 256-color dark-grey list headers & box drawing */
    const char* da_x256 = "\033[>41;354;0c";
    con->feed_input(reinterpret_cast<const oyk_u8*>(da_x256), strlen(da_x256));

    /* Connect an inbound station (creates active connection + heard + auto-learned peer) */
    OykFrame sabm;
    memset(&sabm, 0, sizeof(sabm));
    sabm.dst = cfg->callsign;
    assert(oyk_parse_callsign("W3TRM-1", &sabm.src) == 1);
    sabm.is_command = 1;
    sabm.control = 0x3FU;
    node->on_rx_frame(0, &sabm, 0x0A000005UL, 10093U, 0);
    assert(node->total_active_connections() == 1);
    assert(node->auto_learned_peer_count() == 1);

    oyk_u32 active_sid = 0;
    for (int i = 0; i < node->max_sessions(); ++i) {
        if (node->session_slot(i).state == OYK_SESS_CONNECTED) {
            active_sid = node->session_slot(i).id;
            break;
        }
    }
    assert(active_sid > 0);

    /* 1. Main Console */
    cap->len = 0; cap->buf[0] = '\0';
    con->render();
    assert(strstr(cap->buf, "W3TRM-1") != NULL);
    assert(strstr(cap->buf, "axudp0") != NULL);
    tmux_save_capture("main", con, cap);

    /* 2. Terminate Connection centered modal */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("t"), 1);
    assert(strstr(cap->buf, "Terminate Active Connection") != NULL);
    assert(strstr(cap->buf, "W3TRM-1") != NULL);
    tmux_save_capture("terminate", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\r"), 1);
    assert(node->total_active_connections() == 0);

    /* 3. Ban / Unban Callsign centered modal */
    con->feed_input(reinterpret_cast<const oyk_u8*>("bB4NTST\r"), 8);
    assert(node->ban_count() >= 1);
    cap->len = 0; cap->buf[0] = '\0';
    con->render();
    assert(strstr(cap->buf, "Ban / Unban Callsign") != NULL);
    assert(strstr(cap->buf, "Active Bans (1):") != NULL);
    tmux_save_capture("ban", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(550UL);

    /* 4. Settings (Node tab) */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("s"), 1);
    assert(con->view_mode() == TMUX_VIEW_SETTINGS && con->settings_tab() == 0);
    assert(strstr(cap->buf, "Node Settings") != NULL);
    assert(strstr(cap->buf, " Node ") != NULL);
    assert(strstr(cap->buf, "\033[0;90;40m") != NULL);
    assert(strstr(cap->buf, "1:Node") != NULL);
    tmux_save_capture("settings", con, cap);

    /* 5. Add Interface Wizard (Step 3 showing Same-Iface Fwd toggle) */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("4n1\r\r"), 5);
    assert(strstr(cap->buf, "Add Interface") != NULL);
    assert(strstr(cap->buf, "Same-Iface Fwd") != NULL);
    tmux_save_capture("add_iface", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);

    /* 6. Edit Interface Single View */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("e"), 1);
    assert(strstr(cap->buf, "Edit Interface") != NULL);
    assert(strstr(cap->buf, "Same-Iface Fwd") != NULL);
    tmux_save_capture("edit_iface", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);

    /* 7. Peers Tab */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("5"), 1);
    assert(con->settings_tab() == 4);
    assert(strstr(cap->buf, "R3MOT-7") != NULL);
    tmux_save_capture("peers", con, cap);

    /* 8. Edit Peer Modal */
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("e"), 1);
    assert(strstr(cap->buf, "Edit Peer") != NULL);
    assert(strstr(cap->buf, "Allow Inbound") != NULL);
    assert(strstr(cap->buf, "Allow Outbound") != NULL);
    tmux_save_capture("edit_peer", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(600UL);

    /* 8b. Theme Settings Modal (from main console via 'h') */
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(700UL);
    assert(con->view_mode() == TMUX_VIEW_CONSOLE);
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("h"), 1);
    assert(strstr(cap->buf, "Theme Settings") != NULL);
    tmux_save_capture("theme", con, cap);
    con->feed_input(reinterpret_cast<const oyk_u8*>("\x1b"), 1);
    con->tick(800UL);
    assert(con->view_mode() == TMUX_VIEW_CONSOLE);

    /* 9. Auto-Learned Peer 3-way Bootloader Exit + 73 de sign-off */
    const int base_bl = tinytmux_board_sim_bootloader_count();
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("u"), 1);
    assert(con->view_mode() == TMUX_VIEW_BOOTLOADER);
    assert(strstr(cap->buf, "auto-learned peer") != NULL);
    assert(strstr(cap->buf, "before entering HalfKay bootloader?") != NULL);
    tmux_save_capture("exit", con, cap);
    cap->len = 0; cap->buf[0] = '\0';
    con->feed_input(reinterpret_cast<const oyk_u8*>("d"), 1);
    con->tick(3600UL);
    assert(tinytmux_board_sim_bootloader_count() == base_bl + 1);
    assert(strstr(cap->buf, "73 de KR4OYK-7") != NULL);

    delete cap;
    delete con;
    delete bridge;
    delete node;
    delete cfg;
    printf("[PASS] test_tinytmux_ui_flows_and_captures\n");
}

int main() {
    test_board_pinout_and_mac();
    test_psram_detection_and_self_test();
    test_fs_hooks();
    test_ppp_and_dual_ethernet_axudp_axip();
    test_usb_host_radiofoundry_cdc_net_and_acm();
    test_printer_usb_and_serial();
    test_vt100_console_full_parity();
    test_terminal_inquiry_and_ui_modals();
    test_tinytmux_ui_flows_and_captures();
    printf("All TinyTMUX (IMXRT1060 / Teensy 4.1) host tests passed!\n");
    return 0;
}
