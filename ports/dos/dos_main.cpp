/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * OYK Amateur Radio Packet Node - MS-DOS Port
 *
 * Target: IBM PC/XT (8088 / NEC V20), 640KB conventional RAM, optional LIM EMS 4.0
 * Compiler: OpenWatcom C/C++ v2 / 1.9 (-0 -ml -os -bt=dos)
 * Optional Transports:
 *   - Michael Brutman's mTCP (optional at build time & runtime)
 *   - Bret Johnson's DOS USB Stack (USBUHCI/USBUHCIL, optional at runtime)
 *   - Crynwr Packet Driver (INT 60h..7Fh) + standalone mini IPv4/ARP/UDP
 *   - Direct COM1..COM4 UART / BIOS INT 14h (KISS serial or RadioFoundry PPP)
 *
 * Local UI: Direct 80x25 CGA/MDA/VGA text-mode console matching the ncurses layout.
 */

#include "../compat/oyk_portable.hpp"
#include "../compat/oyk_yapp.hpp"
#include "../compat/oyk_rig.hpp"
#include "dos_usb.hpp"
#include "ems.hpp"

#include <conio.h>
#include <direct.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef OYK_DOS_HAS_MTCP
/* Michael Brutman's mTCP headers (optional) */
#include "types.h"
#include "utils.h"
#include "packet.h"
#include "arp.h"
#include "udp.h"
#include "tcp.h"
#include "tcpsockm.h"
#include "timer.h"
#endif

static OykPortableNode g_node;
static OykEms4 g_ems;
static OykDosUsbStackInfo g_usb_stack;
static OykDosPktDrv g_pktdrv;
static OykDosSerial g_serial;
static OykDosMiniNet g_mininet;
static OykKissDecoder g_kiss_dec;
static int g_serial_iface_idx = -1;
static int g_serial_is_ppp = 0;
static int g_mtcp_active = 0;

static volatile int g_break_flag = 0;
static int g_axudp_iface_idx = -1;
static oyk_u16 g_axudp_port = 10093;
static char g_status_line[78] = "Ready. Press F10 or M to open the Node menu.";

/* Rig control state (Hamlib rigctl TCP + RadioFoundry UDP 4191) */
static OykRigStatus g_rig_status;
#ifdef OYK_DOS_HAS_MTCP
static int g_rf_udp_bound = 0;
#endif
static oyk_u16 g_rf_local_port = 41910U;
static int g_rf_reply_flag = 0;

static void __interrupt __far ctrl_break_handler() {
    g_break_flag = 1;
}

static void __interrupt __far ctrl_c_handler() {
    g_break_flag = 1;
}

/* Standalone BIOS 18.2 Hz tick counter (0040:006Ch) - works with or without mTCP */
static oyk_u32 dos_ticks_now() {
    return *reinterpret_cast<volatile oyk_u32 __far*>(MK_FP(0x0040, 0x006C));
}

static oyk_u32 dos_ticks_elapsed_ms(oyk_u32 start_ticks) {
    oyk_u32 now = dos_ticks_now();
    oyk_u32 diff = (now >= start_ticks) ? (now - start_ticks) : (1573040UL - start_ticks + now);
    return diff * 55UL; /* ~54.925 ms per BIOS tick */
}

static void dos_pump_network();

/* ==================== Direct 80x25 Text Video RAM UI ==================== */

static oyk_u16 detect_video_seg() {
    oyk_u8 mode = *reinterpret_cast<oyk_u8 __far*>(MK_FP(0x0040, 0x0049));
    return (mode == 7) ? 0xB000U : 0xB800U; /* MDA/Hercules vs CGA/EGA/VGA */
}

static oyk_u16 g_vseg = 0xB800U;

static void vid_puts(int row, int col, oyk_u8 attr, const char* s, int max_w) {
    if (row < 0 || row >= 25 || col < 0 || col >= 80) return;
    oyk_u16 __far* scr = reinterpret_cast<oyk_u16 __far*>(MK_FP(g_vseg, (row * 80 + col) * 2));
    int i = 0;
    while (i < max_w && col + i < 80 && s[i] != '\0') {
        scr[i] = static_cast<oyk_u16>((static_cast<oyk_u16>(attr) << 8) |
                                      static_cast<oyk_u8>(s[i]));
        ++i;
    }
    while (i < max_w && col + i < 80) {
        scr[i] = static_cast<oyk_u16>((static_cast<oyk_u16>(attr) << 8) | ' ');
        ++i;
    }
}

static void vid_clear(oyk_u8 attr) {
    oyk_u16 __far* scr = reinterpret_cast<oyk_u16 __far*>(MK_FP(g_vseg, 0));
    oyk_u16 cell = static_cast<oyk_u16>((static_cast<oyk_u16>(attr) << 8) | ' ');
    for (int i = 0; i < 80 * 25; ++i) scr[i] = cell;
}

static int dos_local_term_is_active();

static void render_dos_screen() {
    char line[82];
    char call_s[16];
    oyk_format_callsign(&g_node.config().callsign, call_s, sizeof(call_s));

    /* Row 0: Top Menu Bar (matching GUI and ncurses menu bar) */
    vid_puts(0, 0,  0x70, "", 80);
    vid_puts(0, 1,  0x30, " Node ", 6);
    if (dos_local_term_is_active()) {
        vid_puts(0, 44, 0x70, "Tab/F9:Term(ACTIVE) F10/M:Menu ", 36);
    } else {
        vid_puts(0, 52, 0x70, "Tab/F9:Term  F10/M:Menu ", 28);
    }

    /* Row 1: Header bar */
    sprintf(line, " OYK NODE (DOS): %-9s | Up:%lus | C:%d | %s%s",
            call_s,
            static_cast<unsigned long>(g_node.uptime_seconds()),
            g_node.total_active_connections(),
            g_ems.available() ? "EMS " : "",
            g_usb_stack.installed ? "USB" : (g_mtcp_active ? "mTCP" : "STD"));
    vid_puts(1, 0, 0x1F, line, 61);
    vid_puts(1, 61, g_node.accept_incoming() ? 0x2F : 0x4F,
             g_node.accept_incoming() ? " [INCOMING: ALLOW] " : " [INCOMING: DENY]  ", 19);

    /* Rows 2..8: Interfaces (left 0..43) + Last 5 New Callsigns Heard (right 44..79) */
    vid_puts(2, 0,  0x0B, "Radio Interfaces (Active Conn)              ", 44);
    vid_puts(2, 44, 0x0B, "Last 5 New Callsigns Heard          ", 36);
    vid_puts(3, 0,  0x0F, "Port     Type    Conn Status         RX/TX  ", 44);
    vid_puts(3, 44, 0x0F, "#  Callsign   Port     Heard        ", 36);

    for (int i = 0; i < 5; ++i) {
        if (i < g_node.iface_count()) {
            const OykIfaceRuntime& r = g_node.iface(i);
            sprintf(line, "%-8.8s %-7.7s %-4d %-14.14s %lu/%lu",
                    r.cfg.id, r.cfg.type, g_node.active_connections_on(i), r.status,
                    static_cast<unsigned long>(r.rx_frames),
                    static_cast<unsigned long>(r.tx_frames));
            vid_puts(4 + i, 0, r.running ? 0x0A : 0x0C, line, 44);
        } else {
            vid_puts(4 + i, 0, 0x07, "", 44);
        }

        if (i < g_node.new_heard_count()) {
            const OykNewHeard& nh = g_node.new_heard(i);
            sprintf(line, "%d. %-10.10s %-8.8s %-8.8s",
                    i + 1, nh.callsign, nh.iface_id, nh.time_hms);
            vid_puts(4 + i, 44, 0x07, line, 36);
        } else {
            vid_puts(4 + i, 44, 0x08, (i == 0 && g_node.new_heard_count() == 0) ? "(none heard yet)" : "", 36);
        }
    }

    /* Rows 9..15: Active Connections */
    sprintf(line, "Active Connections (%d) | Banned: %d",
            g_node.total_active_connections(), g_node.ban_count());
    vid_puts(9, 0, 0x0B, line, 80);
    vid_puts(10, 0, 0x0F, "ID   Callsign   Port     Since    Mode", 80);
    int row = 11;
    for (int i = 0; i < g_node.max_sessions() && row < 16; ++i) {
        const OykSession& s = g_node.session_slot(i);
        if (s.state == OYK_SESS_CONNECTED) {
            char cs[12];
            oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
            sprintf(line, "#%-3lu %-10.10s %-8.8s %-8.8s %s",
                    static_cast<unsigned long>(s.id), cs,
                    (s.iface_idx >= 0) ? g_node.iface(s.iface_idx).cfg.id : "local", s.since_hms,
                    s.restricted_ban ? "BAN NOTICE"
                                     : (s.in_bbs ? "BBS"
                                                 : (s.in_chat ? "CHAT"
                                                              : (s.is_local_term ? "LOCAL TERM" : "NODE SHELL"))));
            vid_puts(row++, 0, 0x07, line, 80);
        }
    }
    while (row < 16) vid_puts(row++, 0, 0x07, "", 80);

    /* Rows 16..23: Node Activity Log */
    vid_puts(16, 0, 0x0B, "Node Activity Log", 80);
    int total_logs = g_node.console_log_count();
    int start = (total_logs > 7) ? (total_logs - 7) : 0;
    for (int r = 0; r < 7; ++r) {
        int idx = start + r;
        vid_puts(17 + r, 0, 0x07, (idx < total_logs) ? g_node.console_log_line(idx) : "", 80);
    }

    /* Row 24: Status message line (no bottom button bar) */
    vid_puts(24, 0, 0x0E, g_status_line, 80);
}

/* ==================== Transport Bridge (Optional mTCP / USB CDC / Serial) ==================== */

static void dos_handle_axudp_payload(oyk_u32 src_ip, oyk_u16 src_port,
                                     const oyk_u8* payload, size_t pay_len) {
    if (g_axudp_iface_idx < 0 || !payload || pay_len <= 2) return;
    if (!oyk_verify_fcs(payload, pay_len)) return;
    OykFrame frame;
    if (!oyk_ax25_decode(payload, pay_len - 2, &frame)) return;
    g_node.on_rx_frame(g_axudp_iface_idx, &frame, src_ip, src_port, 0);
}

static void dos_handle_rf_udp_payload(const oyk_u8* payload, size_t pay_len) {
    if (!payload || pay_len == 0) return;
    static char text[256];
    size_t n = (pay_len < sizeof(text) - 1) ? pay_len : (sizeof(text) - 1);
    memcpy(text, payload, n);
    text[n] = '\0';
    if (oyk_rf_parse_status(text, &g_rig_status)) {
        g_rig_status.reachable = 1;
        g_rig_status.last_error[0] = '\0';
        g_rf_reply_flag = 1;
    }
}

static void mininet_udp_rx_cb(oyk_u32 src_ip, oyk_u16 src_port, oyk_u16 dst_port,
                              const oyk_u8* payload, size_t len, void* /*ctx*/) {
    if (g_axudp_iface_idx >= 0 && dst_port == g_axudp_port) {
        dos_handle_axudp_payload(src_ip, src_port, payload, len);
    } else if (dst_port == g_rf_local_port || src_port == g_node.config().rig_port || src_port == OYK_RF_DEFAULT_PORT) {
        dos_handle_rf_udp_payload(payload, len);
    }
}

static int mininet_tx_serial_cb(oyk_u8 b, void* /*ctx*/) {
    return oyk_dos_serial_write_byte(&g_serial, b);
}

static int mininet_tx_eth_cb(const oyk_u8* frame, size_t len, void* /*ctx*/) {
    return oyk_dos_pktdrv_send(&g_pktdrv, frame, len);
}

#ifdef OYK_DOS_HAS_MTCP
static void axudp_rx_callback(const unsigned char* packet, const UdpHeader* udp) {
    if (g_axudp_iface_idx < 0 || !packet || !udp) return;
    oyk_u16 udp_len = ntohs(udp->len);
    if (udp_len <= sizeof(UdpHeader) + 2) return;
    size_t pay_len = udp_len - sizeof(UdpHeader);
    const oyk_u8* payload = reinterpret_cast<const oyk_u8*>(packet + sizeof(EthHeader) + sizeof(IpHeader) + sizeof(UdpHeader));
    const IpHeader* ip = reinterpret_cast<const IpHeader*>(packet + sizeof(EthHeader));
    oyk_u32 src_ip = 0;
    memcpy(&src_ip, ip->ip_src, 4);
    oyk_u16 src_port = ntohs(udp->src);
    dos_handle_axudp_payload(src_ip, src_port, payload, pay_len);
}

static void rf_udp_rx_callback(const unsigned char* packet, const UdpHeader* udp) {
    if (!packet || !udp) return;
    oyk_u16 udp_len = ntohs(udp->len);
    if (udp_len <= sizeof(UdpHeader)) return;
    size_t pay_len = udp_len - sizeof(UdpHeader);
    const oyk_u8* payload = reinterpret_cast<const oyk_u8*>(packet + sizeof(EthHeader) + sizeof(IpHeader) + sizeof(UdpHeader));
    dos_handle_rf_udp_payload(payload, pay_len);
}
#endif

static void dos_pump_network() {
#ifdef OYK_DOS_HAS_MTCP
    if (g_mtcp_active) {
        PACKET_PROCESS_SINGLE;
        Arp::driveArp();
        Tcp::drivePackets();
    }
#endif
    if (g_pktdrv.active && !g_mtcp_active) {
        static oyk_u8 eth_buf[600];
        int n = oyk_dos_pktdrv_poll_rx(&g_pktdrv, eth_buf, sizeof(eth_buf));
        if (n > 0) {
            oyk_dos_mininet_feed_eth(&g_mininet, eth_buf, static_cast<size_t>(n));
        }
    }
    if (g_serial.active) {
        oyk_u8 b = 0;
        for (int burst = 0; burst < 64 && oyk_dos_serial_read_byte(&g_serial, &b); ++burst) {
            if (g_serial_is_ppp) {
                oyk_dos_mininet_feed_ppp_byte(&g_mininet, b);
            } else if (g_serial_iface_idx >= 0) {
                oyk_u8 kport = 0, kcmd = 0;
                if (oyk_kiss_decoder_feed_byte(&g_kiss_dec, b, &kport, &kcmd) && kcmd == 0) {
                    OykFrame frame;
                    if (oyk_ax25_decode(g_kiss_dec.buf, g_kiss_dec.len, &frame)) {
                        g_node.on_rx_frame(g_serial_iface_idx, &frame, 0, 0, 0);
                    }
                }
            }
        }
    }
}

static int dos_tx_callback(int iface_idx, const OykFrame* frame,
                           oyk_u32 peer_ip, oyk_u16 peer_port,
                           oyk_u16 /*peer_handle*/, void* /*ctx*/) {
    if (iface_idx == g_serial_iface_idx && g_serial.active && !g_serial_is_ppp) {
        static oyk_u8 raw[320];
        static oyk_u8 kbuf[384];
        size_t n = oyk_ax25_encode(frame, raw, sizeof(raw));
        if (n == 0) return 0;
        oyk_u8 kport = g_node.iface(iface_idx).cfg.kiss_port;
        size_t kn = oyk_kiss_encode(kport, raw, n, kbuf, sizeof(kbuf));
        if (kn == 0) return 0;
        for (size_t i = 0; i < kn; ++i) {
            if (!oyk_dos_serial_write_byte(&g_serial, kbuf[i])) return 0;
        }
        return 1;
    }

    if (iface_idx != g_axudp_iface_idx || peer_ip == 0 || peer_port == 0) return 0;

    oyk_u8 raw[320];
    size_t n = oyk_ax25_encode(frame, raw, sizeof(raw) - 2);
    if (n == 0) return 0;
    oyk_u16 fcs = oyk_crc16_fcs(raw, n);
    raw[n++] = static_cast<oyk_u8>(fcs & 0xFFU);
    raw[n++] = static_cast<oyk_u8>((fcs >> 8) & 0xFFU);

#ifdef OYK_DOS_HAS_MTCP
    if (g_mtcp_active) {
        IpAddr_t dst_ip;
        memcpy(dst_ip, &peer_ip, 4);
        return (Udp::sendUdp(dst_ip, g_axudp_port, peer_port, static_cast<uint16_t>(n), raw, 0) == 0) ? 1 : 0;
    }
#endif
    if (g_mininet.tx_eth || g_mininet.tx_byte) {
        return oyk_dos_mininet_send_udp(&g_mininet, peer_ip, g_axudp_port, peer_port, raw, n);
    }
    return 0;
}

/* ==================== DOS Rig Control (Hamlib rigctl + RadioFoundry UDP 4191) ==================== */

static int dos_rig_send_rf_cmd(const char* cmd, char* err_out, size_t err_sz) {
    const OykConfig& cfg = g_node.config();
    if (!cfg.rig_enabled || oyk_rig_parse_protocol(cfg.rig_protocol) != OYK_RIG_PROTO_RADIOFOUNDRY) {
        strncpy(err_out, "RadioFoundry rig control is not enabled.", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    oyk_u32 dst_ip = 0;
    if (!oyk_parse_ipv4(cfg.rig_host, &dst_ip)) {
        strncpy(err_out, "Invalid rig IPv4 address (use dotted quad).", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    oyk_u16 dst_port = cfg.rig_port ? cfg.rig_port : static_cast<oyk_u16>(OYK_RF_DEFAULT_PORT);
    g_rf_reply_flag = 0;
    int sent = 0;
#ifdef OYK_DOS_HAS_MTCP
    if (g_mtcp_active) {
        if (!g_rf_udp_bound) {
            if (Udp::registerCallback(g_rf_local_port, rf_udp_rx_callback) == 0) {
                g_rf_udp_bound = 1;
            }
        }
        IpAddr_t ip_a;
        memcpy(ip_a, &dst_ip, 4);
        unsigned char ubuf[96];
        size_t clen = strlen(cmd);
        if (clen > sizeof(ubuf)) clen = sizeof(ubuf);
        memcpy(ubuf, cmd, clen);
        sent = (Udp::sendUdp(ip_a, g_rf_local_port, dst_port,
                             static_cast<uint16_t>(clen), ubuf, 0) == 0) ? 1 : 0;
    }
#endif
    if (!sent && (g_mininet.tx_eth || g_mininet.tx_byte)) {
        sent = oyk_dos_mininet_send_udp(&g_mininet, dst_ip, g_rf_local_port, dst_port,
                                        reinterpret_cast<const oyk_u8*>(cmd), strlen(cmd));
    }
    if (!sent) {
        strncpy(err_out, "No active network transport (mTCP/PacketDrv/PPP).", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    oyk_u32 start = dos_ticks_now();
    while (!g_rf_reply_flag && dos_ticks_elapsed_ms(start) < 450UL && !g_break_flag) {
        dos_pump_network();
    }
    if (!g_rf_reply_flag) {
        strncpy(err_out, "No UDP reply from RadioFoundry radio.", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        g_rig_status.reachable = 0;
        return 0;
    }
    return 1;
}

static int dos_rigctl_exchange(const char* cmd, char* reply_out, size_t reply_sz,
                               char* err_out, size_t err_sz) {
    reply_out[0] = '\0';
#ifdef OYK_DOS_HAS_MTCP
    if (!g_mtcp_active) {
        strncpy(err_out, "rigctl TCP requires mTCP (unavailable).", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    const OykConfig& cfg = g_node.config();
    oyk_u32 dst_ip = 0;
    if (!oyk_parse_ipv4(cfg.rig_host, &dst_ip)) {
        strncpy(err_out, "Invalid rigctld IPv4 address.", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    oyk_u16 port = cfg.rig_port ? cfg.rig_port : static_cast<oyk_u16>(OYK_RIGCTL_DEFAULT_PORT);
    TcpSocket* sock = TcpSocketMgr::getSocket();
    if (!sock) {
        strncpy(err_out, "Out of mTCP sockets.", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    sock->setRecvBuffer(256);
    IpAddr_t ip_a;
    memcpy(ip_a, &dst_ip, 4);
    if (sock->connect(static_cast<uint16_t>(42000U + (dos_ticks_now() & 0x3FFU)), ip_a, port, 800) != 0) {
        TcpSocketMgr::freeSocket(sock);
        strncpy(err_out, "Unable to connect to rigctld.", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    uint8_t tbuf[96];
    size_t clen = strlen(cmd);
    if (clen > sizeof(tbuf)) clen = sizeof(tbuf);
    memcpy(tbuf, cmd, clen);
    sock->send(tbuf, static_cast<uint16_t>(clen));
    size_t got = 0;
    oyk_u32 start = dos_ticks_now();
    while (dos_ticks_elapsed_ms(start) < 600UL && got + 1 < reply_sz && !g_break_flag) {
        dos_pump_network();
        int16_t n = sock->recv(reinterpret_cast<uint8_t*>(reply_out + got),
                               static_cast<uint16_t>(reply_sz - 1 - got));
        if (n > 0) {
            got += static_cast<size_t>(n);
            reply_out[got] = '\0';
            if (strchr(reply_out, '\n') != NULL) break;
        }
        if (sock->isRemoteClosed()) break;
    }
    sock->close();
    TcpSocketMgr::freeSocket(sock);
    if (got == 0) {
        strncpy(err_out, "No reply from rigctld.", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    return 1;
#else
    (void)cmd;
    (void)reply_sz;
    strncpy(err_out, "rigctl TCP requires mTCP (built with USE_MTCP=0).", err_sz - 1);
    err_out[err_sz - 1] = '\0';
    return 0;
#endif
}

static int dos_rig_refresh(char* err_out, size_t err_sz) {
    const OykConfig& cfg = g_node.config();
    int proto = oyk_rig_parse_protocol(cfg.rig_protocol);
    if (!cfg.rig_enabled || proto == OYK_RIG_PROTO_NONE) {
        strncpy(err_out, "Rig control is disabled (configure in Settings -> 5:Rig).", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    if (proto == OYK_RIG_PROTO_RADIOFOUNDRY) {
        char cmd[24];
        oyk_rf_build_select(-1, -1, cmd, sizeof(cmd));
        return dos_rig_send_rf_cmd(cmd, err_out, err_sz);
    }
    char cmd[16], rep[128];
    oyk_rigctl_build_get_freq(cmd, sizeof(cmd));
    if (!dos_rigctl_exchange(cmd, rep, sizeof(rep), err_out, err_sz)) {
        g_rig_status.reachable = 0;
        return 0;
    }
    oyk_rig_status_init(&g_rig_status, OYK_RIG_PROTO_RIGCTL);
    int rprt = 0;
    if (!oyk_rigctl_parse_freq_reply(rep, &g_rig_status.freq_hz, &rprt)) {
        sprintf(err_out, "rigctld error (RPRT %d)", rprt);
        return 0;
    }
    g_rig_status.reachable = 1;
    char opt_err[64];
    oyk_rigctl_build_get_mode(cmd, sizeof(cmd));
    if (dos_rigctl_exchange(cmd, rep, sizeof(rep), opt_err, sizeof(opt_err))) {
        oyk_rigctl_parse_mode_reply(rep, g_rig_status.mode, sizeof(g_rig_status.mode),
                                    &g_rig_status.passband_hz, &rprt);
    }
    oyk_rigctl_build_get_ptt(cmd, sizeof(cmd));
    if (dos_rigctl_exchange(cmd, rep, sizeof(rep), opt_err, sizeof(opt_err))) {
        oyk_rigctl_parse_ptt_reply(rep, &g_rig_status.ptt, &rprt);
    }
    oyk_rigctl_build_get_vfo(cmd, sizeof(cmd));
    if (dos_rigctl_exchange(cmd, rep, sizeof(rep), opt_err, sizeof(opt_err))) {
        oyk_rigctl_parse_vfo_reply(rep, g_rig_status.vfo, sizeof(g_rig_status.vfo), &rprt);
    }
    return 1;
}

static void prompt_line(const char* prompt, char* out, int max_len) {
    vid_puts(24, 0, 0x4F, prompt, 80);
    int len = 0;
    out[0] = '\0';
    int pcol = static_cast<int>(strlen(prompt));
    while (1) {
        int ch = getch();
        if (ch == 0 || ch == 0xE0) { getch(); continue; }
        if (ch == 27) { out[0] = '\0'; return; }
        if (ch == '\r' || ch == '\n') { out[len] = '\0'; return; }
        if ((ch == 8 || ch == 127) && len > 0) {
            out[--len] = '\0';
        } else if (ch >= 32 && ch < 127 && len < max_len - 1) {
            out[len++] = static_cast<char>(ch);
            out[len] = '\0';
        }
        vid_puts(24, pcol, 0x4F, out, 80 - pcol);
    }
}

static int confirm_dos_exit() {
    char prompt[48], yn[4];
    int active = g_node.total_active_connections();
    if (active > 0) {
        sprintf(prompt, "Exit node? (%d active) [Y/N]: ", active);
    } else {
        strcpy(prompt, "Exit node? [Y/N]: ");
    }
    prompt_line(prompt, yn, sizeof(yn));
    return (yn[0] == 'y' || yn[0] == 'Y') ? 1 : 0;
}

/* ==================== DOS Local Terminal, YAPP & Commander File Selector ==================== */

#define DOS_TERM_MAX_LINES 48
#define DOS_FS_MAX_ENTRIES 128

struct DosFsEntry {
    char name[14];
    int is_dir;
    oyk_u32 size;
};

static oyk_u32 g_dterm_sid = 0;
static int g_dterm_inited = 0;
static int g_dterm_connected = 0;
static int g_dterm_connecting = 0;
static int g_dterm_is_local = 0;
static OykCallsign g_dterm_remote;
static OykYappSession g_dterm_yapp;
static char g_dterm_lines[DOS_TERM_MAX_LINES][80];
static int g_dterm_line_count = 0;
static char g_dterm_curline[80];
static int g_dterm_curline_len = 0;
static char g_dterm_last_yapp[128];
static char g_dterm_in_buf[160];
static int g_dterm_in_len = 0;

static int dos_local_term_is_active() {
    return (g_dterm_connected || g_dterm_connecting) ? 1 : 0;
}

static void dterm_push_line(const char* s) {
    if (!s) return;
    if (g_dterm_line_count < DOS_TERM_MAX_LINES) {
        strncpy(g_dterm_lines[g_dterm_line_count], s, 79);
        g_dterm_lines[g_dterm_line_count][79] = '\0';
        g_dterm_line_count++;
    } else {
        for (int i = 1; i < DOS_TERM_MAX_LINES; ++i) {
            memcpy(g_dterm_lines[i - 1], g_dterm_lines[i], 80);
        }
        strncpy(g_dterm_lines[DOS_TERM_MAX_LINES - 1], s, 79);
        g_dterm_lines[DOS_TERM_MAX_LINES - 1][79] = '\0';
    }
}

static int dos_yapp_send_cb(const oyk_u8* data, size_t len, void* /*ctx*/) {
    if (g_dterm_sid == 0 || !g_dterm_connected) return 0;
    return g_node.send_local_bytes(g_dterm_sid, data, len);
}

static int dos_yapp_overwrite_cb(const char* target_path, const char* /*filename*/, void* /*ctx*/) {
    char prompt[80];
    sprintf(prompt, "Overwrite %.48s? [Y/N]: ", target_path);
    vid_puts(24, 0, 0x4F, prompt, 80);
    while (!g_break_flag) {
        dos_pump_network();
        if (kbhit()) {
            int ch = getch();
            if (ch == 0 || ch == 0xE0) { getch(); continue; }
            if (ch == 'y' || ch == 'Y') return 1;
            if (ch == 'n' || ch == 'N' || ch == 27) return 0;
        }
    }
    return 0;
}

static void dos_local_term_event_cb(oyk_u32 sid, int event_type,
                                    const oyk_u8* data, size_t len,
                                    const char* reason, void* /*ctx*/) {
    if (sid != g_dterm_sid) return;
    if (event_type == 1) {
        g_dterm_connecting = 0;
        g_dterm_connected = 1;
        char cs[16], msg[64];
        oyk_format_callsign(&g_dterm_remote, cs, sizeof(cs));
        sprintf(msg, "*** Connected to %s", cs);
        dterm_push_line(msg);
    } else if (event_type == 2 && data && len > 0) {
        const oyk_u8* text_ptr = data;
        size_t text_len = len;
        static oyk_u8 plain[256];
        if (!g_dterm_is_local) {
            text_len = oyk_yapp_feed_bytes(&g_dterm_yapp, data, len, plain, sizeof(plain));
            text_ptr = plain;
        }
        for (size_t i = 0; i < text_len; ++i) {
            oyk_u8 ch = text_ptr[i];
            if (ch == '\r' || ch == '\n') {
                if (g_dterm_curline_len > 0 || ch == '\r') {
                    g_dterm_curline[g_dterm_curline_len] = '\0';
                    dterm_push_line(g_dterm_curline);
                    g_dterm_curline_len = 0;
                }
            } else if (ch >= 0x20U && ch < 0x7FU) {
                if (g_dterm_curline_len < 78) {
                    g_dterm_curline[g_dterm_curline_len++] = static_cast<char>(ch);
                }
            }
        }
        if (!g_dterm_is_local && g_dterm_yapp.status[0] && strcmp(g_dterm_yapp.status, g_dterm_last_yapp) != 0) {
            strncpy(g_dterm_last_yapp, g_dterm_yapp.status, sizeof(g_dterm_last_yapp) - 1);
            g_dterm_last_yapp[sizeof(g_dterm_last_yapp) - 1] = '\0';
            char line[80];
            sprintf(line, "*** %.74s", g_dterm_last_yapp);
            dterm_push_line(line);
        }
    } else if (event_type == 3) {
        g_dterm_sid = 0;
        g_dterm_connecting = 0;
        g_dterm_connected = 0;
        g_dterm_is_local = 0;
        oyk_yapp_reset(&g_dterm_yapp);
        char msg[80];
        sprintf(msg, "*** Disconnected (%.56s)", reason ? reason : "Closed");
        dterm_push_line(msg);
    }
}

static int dos_fs_cmp(const void* a, const void* b) {
    const DosFsEntry* ea = static_cast<const DosFsEntry*>(a);
    const DosFsEntry* eb = static_cast<const DosFsEntry*>(b);
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir;
    if (strcmp(ea->name, "..") == 0) return -1;
    if (strcmp(eb->name, "..") == 0) return 1;
    return strcmp(ea->name, eb->name);
}

static int dos_fs_load(DosFsEntry* list, int cap) {
    int count = 0;
    struct find_t ff;
    unsigned rc = _dos_findfirst("*.*", _A_SUBDIR | _A_NORMAL | _A_RDONLY | _A_ARCH, &ff);
    while (rc == 0 && count < cap) {
        if (strcmp(ff.name, ".") != 0) {
            DosFsEntry* e = &list[count++];
            strncpy(e->name, ff.name, sizeof(e->name) - 1);
            e->name[sizeof(e->name) - 1] = '\0';
            e->is_dir = (ff.attrib & _A_SUBDIR) ? 1 : 0;
            e->size = static_cast<oyk_u32>(ff.size);
        }
        rc = _dos_findnext(&ff);
    }
    if (count > 1) {
        qsort(list, static_cast<size_t>(count), sizeof(DosFsEntry), dos_fs_cmp);
    }
    return count;
}

/* Single-pane Norton Commander / Midnight Commander file selector */
static int dos_file_selector(const char* title, char* out_path, size_t out_sz) {
    static DosFsEntry entries[DOS_FS_MAX_ENTRIES];
    static char orig_dir[128];
    static char cur_dir[128];

    if (!getcwd(orig_dir, sizeof(orig_dir))) strcpy(orig_dir, ".");
    strcpy(cur_dir, orig_dir);

    int count = dos_fs_load(entries, DOS_FS_MAX_ENTRIES);
    int sel = 0;
    int top = 0;
    int picked = 0;
    const int panel_x = 8;
    const int panel_w = 64;
    const int list_y = 4;
    const int list_h = 16;

    while (!g_break_flag) {
        dos_pump_network();

        if (sel >= count) sel = (count > 0) ? (count - 1) : 0;
        if (sel < top) top = sel;
        if (sel >= top + list_h) top = sel - list_h + 1;

        /* Draw Norton Commander single-pane blue box */
        char hdr[66];
        sprintf(hdr, "+-- %-58.58s+", title ? title : "Select File");
        vid_puts(1, panel_x, 0x1F, hdr, panel_w);
        if (!getcwd(cur_dir, sizeof(cur_dir))) strcpy(cur_dir, ".");
        sprintf(hdr, "| %-60.60s |", cur_dir);
        vid_puts(2, panel_x, 0x1B, hdr, panel_w);
        vid_puts(3, panel_x, 0x1F, "+--------------------------------------------------------------+", panel_w);

        for (int i = 0; i < list_h; ++i) {
            int idx = top + i;
            if (idx < count) {
                const DosFsEntry& e = entries[idx];
                char sz_s[12];
                if (e.is_dir) {
                    strcpy(sz_s, "[DIR]");
                } else if (e.size >= 1024UL) {
                    sprintf(sz_s, "%luK", static_cast<unsigned long>(e.size / 1024UL));
                } else {
                    sprintf(sz_s, "%luB", static_cast<unsigned long>(e.size));
                }
                char row[66];
                sprintf(row, " %-46.46s %11s ", e.name, sz_s);
                oyk_u8 attr = (idx == sel) ? 0x30 : (e.is_dir ? 0x1E : 0x1F);
                vid_puts(list_y + i, panel_x, 0x1F, "|", 1);
                vid_puts(list_y + i, panel_x + 1, attr, row, panel_w - 2);
                vid_puts(list_y + i, panel_x + panel_w - 1, 0x1F, "|", 1);
            } else {
                vid_puts(list_y + i, panel_x, 0x1F, "|", 1);
                vid_puts(list_y + i, panel_x + 1, 0x1F, "", panel_w - 2);
                vid_puts(list_y + i, panel_x + panel_w - 1, 0x1F, "|", 1);
            }
        }
        vid_puts(list_y + list_h, panel_x, 0x1F,
                 "+--------------------------------------------------------------+", panel_w);
        vid_puts(list_y + list_h + 1, panel_x, 0x30,
                 " Up/Dn:Move  Enter:Open/Pick  Bksp:Up  Esc:Cancel              ", panel_w);

        if (!kbhit()) continue;
        int ch = getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 72 && sel > 0) --sel;                      /* Up */
            else if (ext == 80 && sel + 1 < count) ++sel;         /* Down */
            else if (ext == 73) sel = (sel > list_h) ? (sel - list_h) : 0; /* PgUp */
            else if (ext == 81) sel = (sel + list_h < count) ? (sel + list_h) : (count > 0 ? count - 1 : 0); /* PgDn */
            else if (ext == 71) sel = 0;                          /* Home */
            else if (ext == 79 && count > 0) sel = count - 1;     /* End */
        } else if (ch == 27) {
            break;
        } else if (ch == 8) {
            if (chdir("..") == 0) {
                count = dos_fs_load(entries, DOS_FS_MAX_ENTRIES);
                sel = 0;
                top = 0;
            }
        } else if ((ch == '\r' || ch == '\n') && count > 0) {
            const DosFsEntry& e = entries[sel];
            if (e.is_dir) {
                if (chdir(e.name) == 0) {
                    count = dos_fs_load(entries, DOS_FS_MAX_ENTRIES);
                    sel = 0;
                    top = 0;
                }
            } else {
                if (!getcwd(cur_dir, sizeof(cur_dir))) strcpy(cur_dir, ".");
                size_t dlen = strlen(cur_dir);
                if (dlen > 0 && cur_dir[dlen - 1] == '\\') {
                    if (dlen + strlen(e.name) + 1 <= out_sz) {
                        sprintf(out_path, "%s%s", cur_dir, e.name);
                        picked = 1;
                    }
                } else if (dlen + 1 + strlen(e.name) + 1 <= out_sz) {
                    sprintf(out_path, "%s\\%s", cur_dir, e.name);
                    picked = 1;
                }
                break;
            }
        }
    }

    chdir(orig_dir);
    return picked;
}

static void render_dos_terminal(const char* input_buf) {
    char hdr[82];
    if (g_dterm_connected) {
        char cs[16];
        oyk_format_callsign(&g_dterm_remote, cs, sizeof(cs));
        sprintf(hdr, " LOCAL TERMINAL | CONNECTED: %-10s (%s)",
                cs, g_dterm_is_local ? "LOCAL - YAPP disabled" : "YAPP ready");
    } else if (g_dterm_connecting) {
        char cs[16];
        oyk_format_callsign(&g_dterm_remote, cs, sizeof(cs));
        sprintf(hdr, " LOCAL TERMINAL | CONNECTING: %-10s...", cs);
    } else {
        strcpy(hdr, " LOCAL TERMINAL | DISCONNECTED (Press F2 to Connect)");
    }
    vid_puts(0, 0, 0x1F, hdr, 80);

    int body_h = 21;
    int start = (g_dterm_line_count > body_h) ? (g_dterm_line_count - body_h) : 0;
    for (int r = 0; r < body_h; ++r) {
        int idx = start + r;
        vid_puts(1 + r, 0, 0x07, (idx < g_dterm_line_count) ? g_dterm_lines[idx] : "", 80);
    }

    char bar[82];
    if (g_dterm_yapp.state != OYK_YAPP_IDLE && g_dterm_yapp.status[0]) {
        sprintf(bar, " %.56s (%lu/%lu B)", g_dterm_yapp.status,
                static_cast<unsigned long>(g_dterm_yapp.bytes_transferred),
                static_cast<unsigned long>(g_dterm_yapp.total_bytes));
    } else {
        strcpy(bar, " F2:Connect  F3:YAPP Upload  F4:Disconnect  F6:Abort YAPP  F9/Esc:Console");
    }
    vid_puts(22, 0, 0x30, bar, 80);

    char in_row[82];
    sprintf(in_row, "> %.77s", input_buf);
    vid_puts(23, 0, 0x0F, in_row, 80);
    vid_puts(24, 0, 0x0E, "F9/Esc switches to Console without closing active terminal session.", 80);
}

static void run_dos_local_terminal() {
    if (!g_dterm_inited) {
        g_dterm_inited = 1;
        g_dterm_sid = 0;
        g_dterm_connected = 0;
        g_dterm_connecting = 0;
        g_dterm_is_local = 0;
        g_dterm_line_count = 0;
        g_dterm_curline_len = 0;
        g_dterm_last_yapp[0] = '\0';
        g_dterm_in_buf[0] = '\0';
        g_dterm_in_len = 0;
        oyk_yapp_init(&g_dterm_yapp, "incoming",
                      dos_yapp_send_cb, NULL,
                      dos_yapp_overwrite_cb, NULL);
        g_node.set_local_event_callback(dos_local_term_event_cb, NULL);

        dterm_push_line("*** OYK MS-DOS Local Packet Terminal (YAPP / YAPP-C enabled)");
        dterm_push_line("*** F2:Connect (LOCAL, CHAT, CALL@ip:port) | F3:YAPP Upload | F9/Esc:Console");
    }

    oyk_u32 last_draw = 0UL;

    while (!g_break_flag) {
        dos_pump_network();
        g_node.tick();

        if (dos_ticks_elapsed_ms(last_draw) >= 100UL) {
            render_dos_terminal(g_dterm_in_buf);
            last_draw = dos_ticks_now();
        }

        if (!kbhit()) continue;
        int ch = getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 67) { /* F9: Switch back to Operator Console without closing session */
                break;
            } else if (ext == 60) { /* F2: Connect */
                char target[64], err[64];
                prompt_line("Target (LOCAL, CHAT, or CALL@ip:port): ", target, sizeof(target));
                if (target[0]) {
                    if (g_dterm_sid != 0) g_node.close_local_terminal(g_dterm_sid);
                    oyk_u32 sid = 0;
                    int is_local = 0;
                    g_dterm_is_local = 0;
                    if (g_node.open_local_terminal(g_axudp_iface_idx, target, &sid,
                                                   &g_dterm_remote, &is_local, err, sizeof(err))) {
                        g_dterm_sid = sid;
                        g_dterm_is_local = is_local;
                        g_dterm_connecting = is_local ? 0 : 1;
                        g_dterm_connected = is_local ? 1 : 0;
                        oyk_yapp_reset(&g_dterm_yapp);
                        oyk_yapp_set_remote(&g_dterm_yapp, &g_dterm_remote);
                        g_dterm_last_yapp[0] = '\0';
                        char cs[16], msg[78];
                        oyk_format_callsign(&g_dterm_remote, cs, sizeof(cs));
                        if (is_local) {
                            sprintf(msg, "*** Connected locally to %s (YAPP disabled)", cs);
                        } else {
                            sprintf(msg, "*** Connecting to %s...", cs);
                        }
                        dterm_push_line(msg);
                    } else {
                        char msg[80];
                        sprintf(msg, "*** Connect error: %.56s", err);
                        dterm_push_line(msg);
                    }
                }
            } else if (ext == 61) { /* F3: YAPP Upload */
                if (!g_dterm_connected || g_dterm_sid == 0) {
                    dterm_push_line("*** Connect to a remote node first (F2).");
                } else if (g_dterm_is_local) {
                    dterm_push_line("*** YAPP is disallowed when both sides are local.");
                } else {
                    static char chosen[160];
                    if (dos_file_selector("YAPP Upload - Select File", chosen, sizeof(chosen))) {
                        char err[64];
                        if (!oyk_yapp_start_send(&g_dterm_yapp, chosen, err, sizeof(err))) {
                            char msg[80];
                            sprintf(msg, "*** YAPP error: %.58s", err);
                            dterm_push_line(msg);
                        } else {
                            char msg[80];
                            sprintf(msg, "*** %.74s", g_dterm_yapp.status);
                            dterm_push_line(msg);
                        }
                    }
                }
            } else if (ext == 62) { /* F4: Disconnect */
                if (g_dterm_sid != 0) {
                    oyk_yapp_cancel(&g_dterm_yapp, "Disconnected");
                    g_node.close_local_terminal(g_dterm_sid);
                }
            } else if (ext == 64) { /* F6: Abort YAPP */
                oyk_yapp_cancel(&g_dterm_yapp, "Cancelled by user");
                dterm_push_line("*** YAPP transfer aborted.");
            }
            render_dos_terminal(g_dterm_in_buf);
        } else if (ch == 27) {
            break;
        } else if (ch == '\r' || ch == '\n') {
            g_dterm_in_buf[g_dterm_in_len] = '\0';
            char echo[80];
            sprintf(echo, "> %.77s", g_dterm_in_buf);
            dterm_push_line(echo);
            if (g_dterm_connected && g_dterm_sid != 0) {
                g_dterm_in_buf[g_dterm_in_len++] = '\r';
                g_node.send_local_bytes(g_dterm_sid, reinterpret_cast<const oyk_u8*>(g_dterm_in_buf),
                                        static_cast<size_t>(g_dterm_in_len));
            } else {
                dterm_push_line("*** Not connected. Press F2 to connect.");
            }
            g_dterm_in_len = 0;
            g_dterm_in_buf[0] = '\0';
            render_dos_terminal(g_dterm_in_buf);
        } else if ((ch == 8 || ch == 127) && g_dterm_in_len > 0) {
            g_dterm_in_buf[--g_dterm_in_len] = '\0';
            render_dos_terminal(g_dterm_in_buf);
        } else if (ch >= 32 && ch < 127 && g_dterm_in_len < 76) {
            g_dterm_in_buf[g_dterm_in_len++] = static_cast<char>(ch);
            g_dterm_in_buf[g_dterm_in_len] = '\0';
            render_dos_terminal(g_dterm_in_buf);
        }
    }

    vid_clear(0x07);
    if (g_dterm_connected || g_dterm_connecting) {
        strcpy(g_status_line, "Operator Console (Terminal active in background; Tab/F9 to return).");
    } else {
        strcpy(g_status_line, "Returned to Operator Console.");
    }
}

static void run_dos_approve_incoming_files() {
    static OykPendingIncoming items[32];
    static OykApprovedFile approved[32];
    int inc_count = oyk_bbs_list_incoming("incoming", items, 32);
    int app_count = oyk_bbs_list_approved(g_node.config().bbs_files_path, approved, 32);
    int tab = 0; /* 0 = Pending Incoming, 1 = Approved Files */
    int sel[2] = { 0, 0 };
    int top[2] = { 0, 0 };
    char feedback[78];
    sprintf(feedback, "BBS Files Area: %.56s", g_node.config().bbs_files_path);

    const int panel_x = 4;
    const int panel_w = 72;
    const int list_y = 6;
    const int list_h = 12;

    while (!g_break_flag) {
        dos_pump_network();
        g_node.tick();

        int count = (tab == 0) ? inc_count : app_count;
        if (sel[tab] >= count) sel[tab] = (count > 0) ? (count - 1) : 0;
        if (sel[tab] < top[tab]) top[tab] = sel[tab];
        if (sel[tab] >= top[tab] + list_h) top[tab] = sel[tab] - list_h + 1;

        vid_puts(2, panel_x, 0x1F,
                 "+-[ Manage BBS Files Area ]--------------------------------------------+", panel_w);
        vid_puts(3, panel_x, 0x70, "", panel_w);
        vid_puts(3, panel_x, 0x1F, "|", 1);
        vid_puts(3, panel_x + 2, (tab == 0) ? 0x1F : 0x70, " 1:Pending Incoming ", 20);
        vid_puts(3, panel_x + 23, (tab == 1) ? 0x1F : 0x70, " 2:Approved Files ", 18);
        vid_puts(3, panel_x + panel_w - 1, 0x1F, "|", 1);

        if (tab == 0) {
            vid_puts(4, panel_x, 0x1E,
                     "| Uploader      Filename                                     Size     |", panel_w);
        } else {
            vid_puts(4, panel_x, 0x1E,
                     "| Approved File Path                                         Size     |", panel_w);
        }
        vid_puts(5, panel_x, 0x1F,
                 "+----------------------------------------------------------------------+", panel_w);

        for (int i = 0; i < list_h; ++i) {
            int idx = top[tab] + i;
            if (count == 0 && i == 1) {
                vid_puts(list_y + i, panel_x, 0x1F, "|", 1);
                vid_puts(list_y + i, panel_x + 1, 0x1F,
                         (tab == 0) ? " (No pending files in incoming\\<CALLSIGN-SSID>\\)"
                                    : " (No approved files in BBS files area)",
                         panel_w - 2);
                vid_puts(list_y + i, panel_x + panel_w - 1, 0x1F, "|", 1);
            } else if (idx < count) {
                char row[72];
                if (tab == 0) {
                    sprintf(row, " %-13.13s %-42.42s %9lu ",
                            items[idx].uploader, items[idx].filename,
                            static_cast<unsigned long>(items[idx].size));
                } else {
                    sprintf(row, " %-56.56s %9lu ",
                            approved[idx].rel_path,
                            static_cast<unsigned long>(approved[idx].size));
                }
                oyk_u8 attr = (idx == sel[tab]) ? 0x30 : 0x1F;
                vid_puts(list_y + i, panel_x, 0x1F, "|", 1);
                vid_puts(list_y + i, panel_x + 1, attr, row, panel_w - 2);
                vid_puts(list_y + i, panel_x + panel_w - 1, 0x1F, "|", 1);
            } else {
                vid_puts(list_y + i, panel_x, 0x1F, "|", 1);
                vid_puts(list_y + i, panel_x + 1, 0x1F, "", panel_w - 2);
                vid_puts(list_y + i, panel_x + panel_w - 1, 0x1F, "|", 1);
            }
        }
        vid_puts(list_y + list_h, panel_x, 0x1F,
                 "+----------------------------------------------------------------------+", panel_w);
        vid_puts(list_y + list_h + 1, panel_x, 0x30,
                 (tab == 0)
                     ? " Tab/1-2:Tabs  Enter/A:Approve  D:Delete  R:Refresh  Esc:Close        "
                     : " Tab/1-2:Tabs  D:Remove Approved File  R:Refresh  Esc:Close           ",
                 panel_w);
        vid_puts(24, 0, 0x0E, feedback, 80);

        if (!kbhit()) continue;
        int ch = getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 72 && sel[tab] > 0) --sel[tab];
            else if (ext == 80 && sel[tab] + 1 < count) ++sel[tab];
            else if (ext == 75 || ext == 77) tab = 1 - tab;
            else if (ext == 73) sel[tab] = (sel[tab] > list_h) ? (sel[tab] - list_h) : 0;
            else if (ext == 81 && count > 0) sel[tab] = (sel[tab] + list_h < count) ? (sel[tab] + list_h) : (count - 1);
        } else if (ch == 27 || ch == 'q' || ch == 'Q') {
            break;
        } else if (ch == '\t') {
            tab = 1 - tab;
        } else if (ch == '1') {
            tab = 0;
        } else if (ch == '2') {
            tab = 1;
        } else if (ch == 'r' || ch == 'R') {
            inc_count = oyk_bbs_list_incoming("incoming", items, 32);
            app_count = oyk_bbs_list_approved(g_node.config().bbs_files_path, approved, 32);
            sprintf(feedback, "Refreshed (%d pending, %d approved).", inc_count, app_count);
        } else if (tab == 0 && (ch == '\r' || ch == '\n' || ch == 'a' || ch == 'A') && count > 0) {
            char subdir[48], dest[160], err[80];
            prompt_line("Subfolder in files area (blank=root): ", subdir, sizeof(subdir));
            int exists = 0;
            int ok = oyk_bbs_approve_incoming_file("incoming", g_node.config().bbs_files_path,
                                                   items[sel[0]].uploader, items[sel[0]].filename,
                                                   subdir, 0, dest, sizeof(dest),
                                                   &exists, err, sizeof(err));
            if (!ok && exists) {
                char yn[4];
                prompt_line("File exists in files area. Overwrite? [Y/N]: ", yn, sizeof(yn));
                if (yn[0] == 'y' || yn[0] == 'Y') {
                    ok = oyk_bbs_approve_incoming_file("incoming", g_node.config().bbs_files_path,
                                                       items[sel[0]].uploader, items[sel[0]].filename,
                                                       subdir, 1, dest, sizeof(dest),
                                                       &exists, err, sizeof(err));
                } else {
                    strcpy(feedback, "Overwrite declined; file remains in incoming/.");
                    continue;
                }
            }
            if (ok) {
                char lmsg[96];
                sprintf(lmsg, "Approved %s/%s -> %.52s", items[sel[0]].uploader, items[sel[0]].filename, dest);
                g_node.log_message(OYK_LOG_INFO, lmsg);
                strncpy(feedback, lmsg, sizeof(feedback) - 1);
                feedback[sizeof(feedback) - 1] = '\0';
                strncpy(g_status_line, lmsg, sizeof(g_status_line) - 1);
                g_status_line[sizeof(g_status_line) - 1] = '\0';
                inc_count = oyk_bbs_list_incoming("incoming", items, 32);
                app_count = oyk_bbs_list_approved(g_node.config().bbs_files_path, approved, 32);
            } else {
                sprintf(feedback, "Error: %.68s", err);
            }
        } else if (tab == 0 && (ch == 'd' || ch == 'D') && count > 0) {
            char cp[72], yn[4], err[80];
            sprintf(cp, "Delete incoming\\%s\\%s? [Y/N]: ", items[sel[0]].uploader, items[sel[0]].filename);
            prompt_line(cp, yn, sizeof(yn));
            if (yn[0] == 'y' || yn[0] == 'Y') {
                if (oyk_bbs_delete_incoming_file("incoming", items[sel[0]].uploader, items[sel[0]].filename,
                                                 err, sizeof(err))) {
                    char lmsg[80];
                    sprintf(lmsg, "Deleted incoming file %s/%s", items[sel[0]].uploader, items[sel[0]].filename);
                    g_node.log_message(OYK_LOG_INFO, lmsg);
                    strncpy(feedback, lmsg, sizeof(feedback) - 1);
                    feedback[sizeof(feedback) - 1] = '\0';
                    strncpy(g_status_line, lmsg, sizeof(g_status_line) - 1);
                    g_status_line[sizeof(g_status_line) - 1] = '\0';
                    inc_count = oyk_bbs_list_incoming("incoming", items, 32);
                } else {
                    sprintf(feedback, "Error: %.68s", err);
                }
            } else {
                strcpy(feedback, "Delete cancelled.");
            }
        } else if (tab == 1 && (ch == 'd' || ch == 'D') && count > 0) {
            char cp[72], yn[4], err[80];
            sprintf(cp, "Remove approved /%.40s? [Y/N]: ", approved[sel[1]].rel_path);
            prompt_line(cp, yn, sizeof(yn));
            if (yn[0] == 'y' || yn[0] == 'Y') {
                if (oyk_bbs_delete_approved_file(g_node.config().bbs_files_path,
                                                 approved[sel[1]].rel_path, err, sizeof(err))) {
                    char lmsg[96];
                    sprintf(lmsg, "Removed approved file /%.56s", approved[sel[1]].rel_path);
                    g_node.log_message(OYK_LOG_INFO, lmsg);
                    strncpy(feedback, lmsg, sizeof(feedback) - 1);
                    feedback[sizeof(feedback) - 1] = '\0';
                    strncpy(g_status_line, lmsg, sizeof(g_status_line) - 1);
                    g_status_line[sizeof(g_status_line) - 1] = '\0';
                    app_count = oyk_bbs_list_approved(g_node.config().bbs_files_path, approved, 32);
                } else {
                    sprintf(feedback, "Error: %.68s", err);
                }
            } else {
                strcpy(feedback, "Remove cancelled.");
            }
        }
    }
}

static void prompt_line_prefill(const char* prompt, char* buf, int max_len, int* accepted_out) {
    int len = static_cast<int>(strlen(buf));
    if (len >= max_len) { len = max_len - 1; buf[len] = '\0'; }
    int pcol = static_cast<int>(strlen(prompt));
    if (pcol > 60) pcol = 60;
    vid_puts(24, 0, 0x4F, prompt, 80);
    vid_puts(24, pcol, 0x4F, buf, 80 - pcol);
    if (accepted_out) *accepted_out = 0;
    while (!g_break_flag) {
        dos_pump_network();
        g_node.tick();
        if (!kbhit()) continue;
        int ch = getch();
        if (ch == 0 || ch == 0xE0) { getch(); continue; }
        if (ch == 27) { if (accepted_out) *accepted_out = 0; return; }
        if (ch == '\r' || ch == '\n') { buf[len] = '\0'; if (accepted_out) *accepted_out = 1; return; }
        if ((ch == 8 || ch == 127) && len > 0) {
            buf[--len] = '\0';
        } else if (ch >= 32 && ch < 127 && len < max_len - 1) {
            buf[len++] = static_cast<char>(ch);
            buf[len] = '\0';
        }
        int avail = 80 - pcol;
        int start = (len > avail) ? (len - avail) : 0;
        vid_puts(24, pcol, 0x4F, buf + start, avail);
    }
}

static void rebind_dos_transports() {
#ifdef OYK_DOS_HAS_MTCP
    if (g_mtcp_active && g_axudp_iface_idx >= 0) {
        Udp::unregisterCallback(g_axudp_port);
    }
#endif
    if (g_serial.active) {
        oyk_dos_serial_close(&g_serial);
    }
    g_axudp_iface_idx = -1;
    g_serial_iface_idx = -1;
    g_serial_is_ppp = 0;

    for (int i = 0; i < g_node.iface_count(); ++i) {
        const OykIfaceConfig& ic = g_node.iface(i).cfg;
        if (strcmp(ic.type, "axudp") == 0) {
            g_axudp_iface_idx = i;
            g_axudp_port = ic.port ? ic.port : 10093;
#ifdef OYK_DOS_HAS_MTCP
            if (g_mtcp_active) {
                if (Udp::registerCallback(g_axudp_port, axudp_rx_callback) == 0) {
                    char st[32];
                    sprintf(st, "UP :%u", static_cast<unsigned int>(g_axudp_port));
                    g_node.set_iface_status(i, 1, st);
                } else {
                    g_node.set_iface_status(i, 0, "UDP ERR");
                }
            } else
#endif
            if (g_pktdrv.active) {
                char st[32];
                sprintf(st, "PKT :%u", static_cast<unsigned int>(g_axudp_port));
                g_node.set_iface_status(i, 1, st);
            } else {
                g_node.set_iface_status(i, 0, "NO NET");
            }
        } else if (strcmp(ic.type, "kiss-serial") == 0 && !g_serial.active) {
            oyk_kiss_decoder_init(&g_kiss_dec);
            if (oyk_dos_serial_open(&g_serial, ic.device[0] ? ic.device : "COM1",
                                    ic.baud ? ic.baud : 9600UL)) {
                g_serial_iface_idx = i;
                g_serial_is_ppp = 0;
                char st[32];
                sprintf(st, "KISS %s", ic.device[0] ? ic.device : "COM1");
                g_node.set_iface_status(i, 1, st);
            } else {
                g_node.set_iface_status(i, 0, "COM ERR");
            }
        } else if (strcmp(ic.type, "usb-cdc-serial") == 0 && !g_serial.active) {
            if (oyk_dos_serial_open(&g_serial, ic.device[0] ? ic.device : "USBCOM1",
                                    ic.baud ? ic.baud : 115200UL)) {
                g_serial_iface_idx = i;
                g_serial_is_ppp = 1;
                g_mininet.tx_byte = mininet_tx_serial_cb;
                g_mininet.tx_byte_ctx = NULL;
                oyk_dos_mininet_ppp_open(&g_mininet);
                g_node.set_iface_status(i, 1, "USB PPP");
            } else {
                g_node.set_iface_status(i, 0, "USB ERR");
            }
        } else if (strcmp(ic.type, "usb-cdc-net") == 0) {
            if (g_mtcp_active) {
                g_node.set_iface_status(i, 1, "USB mTCP");
            } else if (g_pktdrv.active) {
                g_node.set_iface_status(i, 1, "USB PKT");
            } else if (g_usb_stack.installed) {
                g_node.set_iface_status(i, 1, "USBUHCI");
            } else {
                g_node.set_iface_status(i, 0, "NO USB");
            }
        }
    }
}

static void run_dos_rig_control() {
    const OykConfig& cfg = g_node.config();
    int proto = oyk_rig_parse_protocol(cfg.rig_protocol);
    if (g_rig_status.protocol != proto) {
        oyk_rig_status_init(&g_rig_status, proto);
    }

    char fb[78];
    if (!cfg.rig_enabled || proto == OYK_RIG_PROTO_NONE) {
        strcpy(fb, "Rig control disabled. Enable it in Node -> Settings -> 5:Rig.");
    } else {
        char err[78];
        if (dos_rig_refresh(err, sizeof(err))) {
            strcpy(fb, "Queried radio status.");
        } else {
            sprintf(fb, "%.76s", err);
        }
    }

    while (!g_break_flag) {
        dos_pump_network();
        g_node.tick();

        vid_clear(0x17);
        vid_puts(0, 0, 0x30, " OYK RADIO / RIG CONTROL (MS-DOS)                              Esc:Close", 80);

        char row[82];
        sprintf(row, " Protocol : %-14s  Endpoint: %s:%u  [%s]",
                oyk_rig_protocol_name(proto),
                cfg.rig_host[0] ? cfg.rig_host : "127.0.0.1",
                static_cast<unsigned int>(cfg.rig_port),
                cfg.rig_enabled ? (g_rig_status.reachable ? "ONLINE" : "NO REPLY") : "DISABLED");
        vid_puts(2, 0, 0x1F, row, 80);
        vid_puts(3, 0, 0x1B, "--------------------------------------------------------------------------------", 80);

        if (proto == OYK_RIG_PROTO_RADIOFOUNDRY) {
            sprintf(row, " Zone / Pos   : Zone %d, Channel Pos %d (0-based)", g_rig_status.zone, g_rig_status.pos);
            vid_puts(5, 0, 0x1F, row, 80);
            sprintf(row, " Channel Name : %s", g_rig_status.channel_name[0] ? g_rig_status.channel_name : "(unknown)");
            vid_puts(6, 0, 0x1F, row, 80);
            sprintf(row, " Frequency    : %lu Hz", static_cast<unsigned long>(g_rig_status.freq_hz));
            vid_puts(7, 0, 0x1F, row, 80);
            if (g_rig_status.is_dmr) {
                sprintf(row, " DMR Params   : CC %d  TS %d  TG %ld",
                        g_rig_status.color_code, g_rig_status.timeslot, g_rig_status.talkgroup);
            } else {
                strcpy(row, " DMR Params   : (analog / not reported)");
            }
            vid_puts(8, 0, 0x1F, row, 80);
            sprintf(row, " Raw Status   : %.60s", g_rig_status.raw_status[0] ? g_rig_status.raw_status : "(none)");
            vid_puts(10, 0, 0x17, row, 80);
            vid_puts(13, 0, 0x1E, " Commands (RadioFoundry / takRF UDP 4191):", 80);
            vid_puts(14, 0, 0x17, "  [Q/R] Query (select)    [Z] Select Zone & Pos   [N] Stop Notify", 80);
            vid_puts(15, 0, 0x17, "  [+/-] Step Channel      [[/]] Step Zone         [Esc] Close", 80);
        } else {
            sprintf(row, " Frequency    : %lu Hz", static_cast<unsigned long>(g_rig_status.freq_hz));
            vid_puts(5, 0, 0x1F, row, 80);
            sprintf(row, " Mode / PB    : %s (%lu Hz)",
                    g_rig_status.mode[0] ? g_rig_status.mode : "(unknown)",
                    static_cast<unsigned long>(g_rig_status.passband_hz));
            vid_puts(6, 0, 0x1F, row, 80);
            sprintf(row, " PTT / VFO    : %s  |  VFO: %s",
                    g_rig_status.ptt > 0 ? "TX" : (g_rig_status.ptt == 0 ? "RX" : "UNKNOWN"),
                    g_rig_status.vfo[0] ? g_rig_status.vfo : "(unknown)");
            vid_puts(7, 0, 0x1F, row, 80);
            vid_puts(13, 0, 0x1E, " Commands (Hamlib rigctld TCP 4532):", 80);
            vid_puts(14, 0, 0x17, "  [Q/R] Query Rig         [F] Set Frequency (Hz)  [M] Set Mode", 80);
            vid_puts(15, 0, 0x17, "  [P]   Toggle PTT RX/TX  [V] Set VFO             [Esc] Close", 80);
        }

        vid_puts(23, 0, 0x1E, fb, 80);
        vid_puts(24, 0, 0x70, " Q/R:Refresh  Esc:Return to Operator Console                                    ", 80);

        if (!kbhit()) continue;
        int ch = getch();
        if (ch == 0 || ch == 0xE0) { getch(); continue; }
        if (ch == 27) break;

        char err[78], cmd[48], rep[128];
        if (ch == 'q' || ch == 'Q' || ch == 'r' || ch == 'R') {
            if (dos_rig_refresh(err, sizeof(err))) strcpy(fb, "Status refreshed.");
            else sprintf(fb, "%.76s", err);
        } else if (proto == OYK_RIG_PROTO_RADIOFOUNDRY) {
            if (ch == '+' || ch == '=') {
                oyk_rf_build_step_channel(1, cmd, sizeof(cmd));
                if (dos_rig_send_rf_cmd(cmd, err, sizeof(err))) strcpy(fb, "Stepped channel +1.");
                else sprintf(fb, "%.76s", err);
            } else if (ch == '-') {
                oyk_rf_build_step_channel(-1, cmd, sizeof(cmd));
                if (dos_rig_send_rf_cmd(cmd, err, sizeof(err))) strcpy(fb, "Stepped channel -1.");
                else sprintf(fb, "%.76s", err);
            } else if (ch == ']') {
                oyk_rf_build_step_zone(1, cmd, sizeof(cmd));
                if (dos_rig_send_rf_cmd(cmd, err, sizeof(err))) strcpy(fb, "Stepped zone +1.");
                else sprintf(fb, "%.76s", err);
            } else if (ch == '[') {
                oyk_rf_build_step_zone(-1, cmd, sizeof(cmd));
                if (dos_rig_send_rf_cmd(cmd, err, sizeof(err))) strcpy(fb, "Stepped zone -1.");
                else sprintf(fb, "%.76s", err);
            } else if (ch == 'n' || ch == 'N') {
                oyk_rf_build_stop_notify(cmd, sizeof(cmd));
                if (dos_rig_send_rf_cmd(cmd, err, sizeof(err))) strcpy(fb, "Sent select?notify=0.");
                else sprintf(fb, "%.76s", err);
            } else if (ch == 'z' || ch == 'Z') {
                char zb[12], pb[12];
                int ok = 0;
                sprintf(zb, "%d", g_rig_status.zone >= 0 ? g_rig_status.zone : 0);
                prompt_line_prefill("Zone (0-based): ", zb, sizeof(zb), &ok);
                if (ok) {
                    sprintf(pb, "%d", g_rig_status.pos >= 0 ? g_rig_status.pos : 0);
                    prompt_line_prefill("Channel Pos (0-based): ", pb, sizeof(pb), &ok);
                    if (ok) {
                        oyk_rf_build_select(atoi(zb), atoi(pb), cmd, sizeof(cmd));
                        if (dos_rig_send_rf_cmd(cmd, err, sizeof(err))) strcpy(fb, "Selected zone & channel.");
                        else sprintf(fb, "%.76s", err);
                    }
                }
            }
        } else if (proto == OYK_RIG_PROTO_RIGCTL) {
            if (ch == 'f' || ch == 'F') {
                char fbuf[20];
                int ok = 0;
                sprintf(fbuf, "%lu", static_cast<unsigned long>(g_rig_status.freq_hz ? g_rig_status.freq_hz : 145050000UL));
                prompt_line_prefill("Frequency (Hz): ", fbuf, sizeof(fbuf), &ok);
                if (ok && fbuf[0]) {
                    oyk_u32 hz = static_cast<oyk_u32>(strtoul(fbuf, NULL, 10));
                    oyk_rigctl_build_set_freq(hz, cmd, sizeof(cmd));
                    if (dos_rigctl_exchange(cmd, rep, sizeof(rep), err, sizeof(err))) {
                        g_rig_status.freq_hz = hz;
                        strcpy(fb, "Frequency updated.");
                    } else sprintf(fb, "%.76s", err);
                }
            } else if (ch == 'm' || ch == 'M') {
                char mb[16], pb[12];
                int ok = 0;
                strcpy(mb, g_rig_status.mode[0] ? g_rig_status.mode : "FM");
                prompt_line_prefill("Mode (FM/USB/LSB/CW/PKTFM): ", mb, sizeof(mb), &ok);
                if (ok && mb[0]) {
                    sprintf(pb, "%lu", static_cast<unsigned long>(g_rig_status.passband_hz));
                    prompt_line_prefill("Passband Hz (0=default): ", pb, sizeof(pb), &ok);
                    if (ok) {
                        oyk_u32 pbhz = static_cast<oyk_u32>(strtoul(pb, NULL, 10));
                        oyk_rigctl_build_set_mode(mb, pbhz, cmd, sizeof(cmd));
                        if (dos_rigctl_exchange(cmd, rep, sizeof(rep), err, sizeof(err))) {
                            strncpy(g_rig_status.mode, mb, sizeof(g_rig_status.mode) - 1);
                            g_rig_status.passband_hz = pbhz;
                            strcpy(fb, "Mode updated.");
                        } else sprintf(fb, "%.76s", err);
                    }
                }
            } else if (ch == 'p' || ch == 'P') {
                int next_ptt = (g_rig_status.ptt > 0) ? 0 : 1;
                oyk_rigctl_build_set_ptt(next_ptt, cmd, sizeof(cmd));
                if (dos_rigctl_exchange(cmd, rep, sizeof(rep), err, sizeof(err))) {
                    g_rig_status.ptt = next_ptt;
                    strcpy(fb, next_ptt ? "PTT set to TX." : "PTT set to RX.");
                } else sprintf(fb, "%.76s", err);
            } else if (ch == 'v' || ch == 'V') {
                char vb[16];
                int ok = 0;
                strcpy(vb, g_rig_status.vfo[0] ? g_rig_status.vfo : "VFOA");
                prompt_line_prefill("VFO (VFOA/VFOB): ", vb, sizeof(vb), &ok);
                if (ok && vb[0]) {
                    oyk_rigctl_build_set_vfo(vb, cmd, sizeof(cmd));
                    if (dos_rigctl_exchange(cmd, rep, sizeof(rep), err, sizeof(err))) {
                        strncpy(g_rig_status.vfo, vb, sizeof(g_rig_status.vfo) - 1);
                        strcpy(fb, "VFO updated.");
                    } else sprintf(fb, "%.76s", err);
                }
            }
        }
    }
    vid_clear(0x07);
}

static void run_dos_settings() {
    static OykConfig draft;
    draft = g_node.config();

    /* Five logical tabs matching GTK/ncurses/Windows: 0=Node, 1=BBS, 2=CHAT, 3=Interfaces, 4=Rig */
    static const char* TAB_NAMES[5] = { " 1:Node ", " 2:BBS ", " 3:CHAT ", " 4:Interfaces ", " 5:Rig " };
    static const char* NODE_LABELS[5] = {
        "Node Callsign",
        "Node Alias",
        "Welcome Message",
        "Info Message",
        "Allow Incoming (Default)"
    };
    static const char* BBS_LABELS[8] = {
        "Enable Packet BBS",
        "BBS SSID (0-15)",
        "Callsign Override",
        "BBS Alias",
        "Enable BBS Files Area",
        "Files Area Root Path",
        "Mail Storage Path",
        "BBS Welcome Message"
    };
    static const char* CHAT_LABELS[6] = {
        "Enable Multi-User CHAT",
        "CHAT SSID (0-15)",
        "Callsign Override",
        "CHAT Alias",
        "Default Topic",
        "CHAT Welcome Message"
    };
    static const char* RIG_LABELS[6] = {
        "Enable Rig Control",
        "Protocol (rigctl/radiofoundry)",
        "Radio Host / IP",
        "Port (4532 / 4191)",
        "Poll Interval (sec)",
        "RadioFoundry Notify"
    };

    int tab = 0;
    int sel[5] = { 0, 0, 0, 0, 0 };
    char fb[78];
    sprintf(fb, "Config: %.68s", draft.source_path[0] ? draft.source_path : "node.cfg");

    while (!g_break_flag) {
        dos_pump_network();
        g_node.tick();

        int cnt = (tab == 0) ? 5 : ((tab == 1) ? 8 : ((tab == 2) ? 6 : ((tab == 3) ? draft.iface_count : 6)));
        if (cnt <= 0) sel[tab] = 0;
        else if (sel[tab] >= cnt) sel[tab] = cnt - 1;

        vid_clear(0x17);
        vid_puts(0, 0, 0x30, " OYK NODE SETTINGS (MS-DOS)                      F2/S:Save & Apply  Esc:Cancel", 80);

        /* Row 1: Tab bar */
        vid_puts(1, 0, 0x70, "", 80);
        int tx = 1;
        for (int t = 0; t < 5; ++t) {
            int tlen = static_cast<int>(strlen(TAB_NAMES[t]));
            vid_puts(1, tx, (t == tab) ? 0x1F : 0x70, TAB_NAMES[t], tlen);
            tx += tlen + 1;
        }
        vid_puts(2, 0, 0x1B, "--------------------------------------------------------------------------------", 80);

        if (tab != 3) {
            const char** labels = (tab == 0) ? NODE_LABELS : ((tab == 1) ? BBS_LABELS : ((tab == 2) ? CHAT_LABELS : RIG_LABELS));
            for (int i = 0; i < cnt; ++i) {
                char val[56], cs[16], row[82];
                val[0] = '\0';
                if (tab == 0) {
                    if (i == 0) oyk_format_callsign(&draft.callsign, val, sizeof(val));
                    else if (i == 1) { if (draft.alias.base[0]) oyk_format_callsign(&draft.alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                    else if (i == 2) { strncpy(val, draft.welcome_text, 50); val[50] = '\0'; }
                    else if (i == 3) { strncpy(val, draft.info_text, 50); val[50] = '\0'; }
                    else if (i == 4) strcpy(val, draft.accept_incoming ? "[ON]" : "[OFF]");
                } else if (tab == 1) {
                    if (i == 0) strcpy(val, draft.bbs_enabled ? "[ON]" : "[OFF]");
                    else if (i == 1) { OykCallsign bc; oyk_config_resolved_bbs_callsign(&draft, &bc); oyk_format_callsign(&bc, cs, sizeof(cs)); sprintf(val, "%u (%s)", static_cast<unsigned int>(draft.bbs_ssid), cs); }
                    else if (i == 2) { if (draft.bbs_callsign.base[0]) oyk_format_callsign(&draft.bbs_callsign, val, sizeof(val)); else strcpy(val, "(auto)"); }
                    else if (i == 3) { if (draft.bbs_alias.base[0]) oyk_format_callsign(&draft.bbs_alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                    else if (i == 4) strcpy(val, draft.bbs_files_enabled ? "[ON]" : "[OFF]");
                    else if (i == 5) { strncpy(val, draft.bbs_files_path, 50); val[50] = '\0'; }
                    else if (i == 6) { strncpy(val, draft.bbs_mail_path, 50); val[50] = '\0'; }
                    else if (i == 7) { strncpy(val, draft.bbs_welcome, 50); val[50] = '\0'; }
                } else if (tab == 2) {
                    if (i == 0) strcpy(val, draft.chat_enabled ? "[ON]" : "[OFF]");
                    else if (i == 1) { OykCallsign cc; oyk_config_resolved_chat_callsign(&draft, &cc); oyk_format_callsign(&cc, cs, sizeof(cs)); sprintf(val, "%u (%s)", static_cast<unsigned int>(draft.chat_ssid), cs); }
                    else if (i == 2) { if (draft.chat_callsign.base[0]) oyk_format_callsign(&draft.chat_callsign, val, sizeof(val)); else strcpy(val, "(auto)"); }
                    else if (i == 3) { if (draft.chat_alias.base[0]) oyk_format_callsign(&draft.chat_alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                    else if (i == 4) { strncpy(val, draft.chat_topic, 50); val[50] = '\0'; }
                    else if (i == 5) { strncpy(val, draft.chat_welcome, 50); val[50] = '\0'; }
                } else {
                    if (i == 0) strcpy(val, draft.rig_enabled ? "[ON]" : "[OFF]");
                    else if (i == 1) { strncpy(val, draft.rig_protocol[0] ? draft.rig_protocol : "rigctl", 50); val[50] = '\0'; }
                    else if (i == 2) { strncpy(val, draft.rig_host[0] ? draft.rig_host : "127.0.0.1", 50); val[50] = '\0'; }
                    else if (i == 3) sprintf(val, "%u", static_cast<unsigned int>(draft.rig_port));
                    else if (i == 4) sprintf(val, "%u s", static_cast<unsigned int>(draft.rig_poll_sec));
                    else if (i == 5) strcpy(val, draft.rig_notify ? "[ON]" : "[OFF]");
                }
                sprintf(row, " %-30.30s: %-45.45s", labels[i], val);
                vid_puts(3 + i, 0, (i == sel[tab]) ? 0x30 : 0x17, row, 80);
            }
        } else {
            vid_puts(3, 0, 0x1E, " #  ID          Type         Endpoint / Device             Peers", 80);
            if (draft.iface_count == 0) {
                vid_puts(4, 0, 0x17, " (no interfaces configured - press A to add)", 80);
            } else {
                for (int i = 0; i < draft.iface_count; ++i) {
                    const OykIfaceConfig& ic = draft.ifaces[i];
                    char row[82], ep[32];
                    if (strcmp(ic.type, "kiss-serial") == 0 || strcmp(ic.type, "usb-cdc-serial") == 0) {
                        sprintf(ep, "%.14s @%lu", ic.device[0] ? ic.device : "COM1",
                                static_cast<unsigned long>(ic.baud ? ic.baud : 9600UL));
                    } else {
                        sprintf(ep, "%.20s:%u", ic.host[0] ? ic.host : "0.0.0.0",
                                static_cast<unsigned int>(ic.port));
                    }
                    sprintf(row, " %-2d %-11.11s %-12.12s %-29.29s %d",
                            i + 1, ic.id, ic.type, ep, ic.peer_count);
                    vid_puts(4 + i, 0, (i == sel[3]) ? 0x30 : 0x17, row, 80);
                }
            }
        }

        vid_puts(23, 0, 0x1E, fb, 80);
        if (tab == 3) {
            vid_puts(24, 0, 0x70, " Tab/1-5:Tabs  A:Add Interface  D:Remove  S/F2:Save & Apply  Esc:Cancel ", 80);
        } else {
            vid_puts(24, 0, 0x70, " Tab/Lt/Rt/1-5:Tabs  Enter/Space:Edit  S/F2:Save & Apply  Esc:Cancel ", 80);
        }

        if (!kbhit()) continue;
        int ch = getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 72 && sel[tab] > 0) --sel[tab];             /* Up */
            else if (ext == 80 && sel[tab] + 1 < cnt) ++sel[tab];  /* Down */
            else if (ext == 75) tab = (tab + 4) % 5;               /* Left */
            else if (ext == 77) tab = (tab + 1) % 5;               /* Right */
            else if (ext == 60) ch = 'S';                          /* F2 */
            else continue;
            if (ch != 'S') continue;
        }
        if (ch == 27) break;
        if (ch == '\t') { tab = (tab + 1) % 5; continue; }
        if (ch >= '1' && ch <= '5') { tab = ch - '1'; continue; }

        if (ch == 's' || ch == 'S') {
            static OykConfigApplyResult res;
            if (!g_node.apply_config(&draft, 1, &res)) {
                sprintf(fb, "Error: %.68s", res.error);
                continue;
            }
            if (res.interfaces_changed) {
                rebind_dos_transports();
            }
            oyk_rig_status_init(&g_rig_status, oyk_rig_parse_protocol(g_node.config().rig_protocol));
            if (!res.saved_to_disk) {
                vid_clear(0x4F);
                vid_puts(8,  4, 0x4F, "WARNING: CONFIGURATION FILE NOT WRITABLE (READ-ONLY FILESYSTEM?)", 72);
                vid_puts(10, 4, 0x4F, "Changes have been applied to the running node in memory only", 72);
                vid_puts(11, 4, 0x4F, "and will be lost when the software exits.", 72);
                vid_puts(14, 4, 0x4E, "Press any key to continue...", 72);
                getch();
                strcpy(g_status_line, "Settings applied IN MEMORY ONLY (config not writable).");
            } else {
                sprintf(g_status_line, "Settings saved to %.52s and applied.", res.save_path);
            }
            break;
        }

        if (tab == 3) {
            if (ch == 'a' || ch == 'A') {
                if (draft.iface_count >= OYK_MAX_INTERFACES) {
                    strcpy(fb, "Maximum interface count reached.");
                    continue;
                }
                OykIfaceConfig ic;
                memset(&ic, 0, sizeof(ic));
                int ok = 0;
                strcpy(ic.id, "axudp1");
                prompt_line_prefill("Interface ID: ", ic.id, sizeof(ic.id), &ok);
                if (!ok || !ic.id[0]) { strcpy(fb, "Add cancelled."); continue; }
                int dup = 0;
                for (int i = 0; i < draft.iface_count; ++i) {
                    if (strcmp(draft.ifaces[i].id, ic.id) == 0) { dup = 1; break; }
                }
                if (dup) { strcpy(fb, "Duplicate interface ID."); continue; }

                strcpy(ic.type, "axudp");
                prompt_line_prefill("Type [axudp|kiss-serial|usb-cdc-serial|usb-cdc-net|telnet]: ", ic.type, sizeof(ic.type), &ok);
                if (!ok || !ic.type[0]) { strcpy(fb, "Add cancelled."); continue; }

                if (strcmp(ic.type, "kiss-serial") == 0 || strcmp(ic.type, "usb-cdc-serial") == 0) {
                    strcpy(ic.device, (strcmp(ic.type, "usb-cdc-serial") == 0) ? "USBCOM1" : "COM1");
                    prompt_line_prefill("Serial Device (COM1..4 or USBCOM1): ", ic.device, sizeof(ic.device), &ok);
                    if (!ok) continue;
                    char bb[12];
                    strcpy(bb, (strcmp(ic.type, "usb-cdc-serial") == 0) ? "115200" : "9600");
                    prompt_line_prefill("Baud rate: ", bb, sizeof(bb), &ok);
                    ic.baud = atol(bb) > 0L ? static_cast<oyk_u32>(atol(bb)) : 9600UL;
                } else {
                    strcpy(ic.host, "0.0.0.0");
                    prompt_line_prefill("Host / Bind IP: ", ic.host, sizeof(ic.host), &ok);
                    if (!ok) continue;
                    char pb[10];
                    if (strcmp(ic.type, "telnet") == 0) strcpy(pb, "8010");
                    else if (strcmp(ic.type, "kiss-tcp") == 0) strcpy(pb, "8001");
                    else strcpy(pb, "10093");
                    prompt_line_prefill("Port: ", pb, sizeof(pb), &ok);
                    if (!ok) continue;
                    ic.port = static_cast<oyk_u16>(atoi(pb));
                }
                draft.ifaces[draft.iface_count++] = ic;
                sel[3] = draft.iface_count - 1;
                sprintf(fb, "Added interface %s (press S/F2 to Save & Apply).", ic.id);
            } else if ((ch == 'd' || ch == 'D') && draft.iface_count > 0) {
                char yn[4], pr[48];
                sprintf(pr, "Remove interface %s? [Y/N]: ", draft.ifaces[sel[3]].id);
                prompt_line(pr, yn, sizeof(yn));
                if (yn[0] == 'y' || yn[0] == 'Y') {
                    for (int i = sel[3] + 1; i < draft.iface_count; ++i) {
                        draft.ifaces[i - 1] = draft.ifaces[i];
                    }
                    draft.iface_count--;
                    strcpy(fb, "Interface removed.");
                }
            }
            continue;
        }

        if (ch == '\r' || ch == '\n' || ch == ' ') {
            int f = sel[tab];
            if (tab == 0 && f == 4) { draft.accept_incoming = !draft.accept_incoming; continue; }
            if (tab == 1 && f == 0) { draft.bbs_enabled = !draft.bbs_enabled; continue; }
            if (tab == 1 && f == 4) { draft.bbs_files_enabled = !draft.bbs_files_enabled; continue; }
            if (tab == 2 && f == 0) { draft.chat_enabled = !draft.chat_enabled; continue; }
            if (tab == 4 && f == 0) { draft.rig_enabled = !draft.rig_enabled; continue; }
            if (tab == 4 && f == 1) {
                if (oyk_rig_parse_protocol(draft.rig_protocol) == OYK_RIG_PROTO_RIGCTL) {
                    strcpy(draft.rig_protocol, "radiofoundry");
                    if (draft.rig_port == 4532 || draft.rig_port == 0) draft.rig_port = 4191;
                } else {
                    strcpy(draft.rig_protocol, "rigctl");
                    if (draft.rig_port == 4191 || draft.rig_port == 0) draft.rig_port = 4532;
                }
                continue;
            }
            if (tab == 4 && f == 5) { draft.rig_notify = !draft.rig_notify; continue; }

            const char** labels = (tab == 0) ? NODE_LABELS : ((tab == 1) ? BBS_LABELS : ((tab == 2) ? CHAT_LABELS : RIG_LABELS));
            char ed[96];
            ed[0] = '\0';
            if (tab == 0) {
                if (f == 0) oyk_format_callsign(&draft.callsign, ed, sizeof(ed));
                else if (f == 1 && draft.alias.base[0]) oyk_format_callsign(&draft.alias, ed, sizeof(ed));
                else if (f == 2) { strncpy(ed, draft.welcome_text, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; }
                else if (f == 3) { strncpy(ed, draft.info_text, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; }
            } else if (tab == 1) {
                if (f == 1) sprintf(ed, "%u", static_cast<unsigned int>(draft.bbs_ssid));
                else if (f == 2 && draft.bbs_callsign.base[0]) oyk_format_callsign(&draft.bbs_callsign, ed, sizeof(ed));
                else if (f == 3 && draft.bbs_alias.base[0]) oyk_format_callsign(&draft.bbs_alias, ed, sizeof(ed));
                else if (f == 5) { strncpy(ed, draft.bbs_files_path, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; }
                else if (f == 6) { strncpy(ed, draft.bbs_mail_path, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; }
                else if (f == 7) { strncpy(ed, draft.bbs_welcome, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; }
            } else if (tab == 2) {
                if (f == 1) sprintf(ed, "%u", static_cast<unsigned int>(draft.chat_ssid));
                else if (f == 2 && draft.chat_callsign.base[0]) oyk_format_callsign(&draft.chat_callsign, ed, sizeof(ed));
                else if (f == 3 && draft.chat_alias.base[0]) oyk_format_callsign(&draft.chat_alias, ed, sizeof(ed));
                else if (f == 4) { strncpy(ed, draft.chat_topic, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; }
                else if (f == 5) { strncpy(ed, draft.chat_welcome, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; }
            } else {
                if (f == 2) { strncpy(ed, draft.rig_host, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; }
                else if (f == 3) sprintf(ed, "%u", static_cast<unsigned int>(draft.rig_port));
                else if (f == 4) sprintf(ed, "%u", static_cast<unsigned int>(draft.rig_poll_sec));
            }

            char pr[36];
            sprintf(pr, "%.28s: ", labels[f]);
            int ok = 0;
            prompt_line_prefill(pr, ed, sizeof(ed), &ok);
            if (!ok) continue;

            if (tab == 0) {
                if (f == 0) { if (!oyk_parse_callsign_with_default_ssid(ed, OYK_DEFAULT_NODE_SSID, &draft.callsign)) strcpy(fb, "Invalid node callsign."); }
                else if (f == 1) { if (!ed[0]) memset(&draft.alias, 0, sizeof(draft.alias)); else if (!oyk_parse_callsign(ed, &draft.alias)) strcpy(fb, "Invalid alias."); }
                else if (f == 2) { strncpy(draft.welcome_text, ed, sizeof(draft.welcome_text) - 1); draft.welcome_text[sizeof(draft.welcome_text) - 1] = '\0'; }
                else if (f == 3) { strncpy(draft.info_text, ed, sizeof(draft.info_text) - 1); draft.info_text[sizeof(draft.info_text) - 1] = '\0'; }
            } else if (tab == 1) {
                if (f == 1) { int sv = atoi(ed); if (sv >= 0 && sv <= 15) draft.bbs_ssid = static_cast<oyk_u8>(sv); else strcpy(fb, "SSID must be 0..15."); }
                else if (f == 2) { if (!ed[0]) memset(&draft.bbs_callsign, 0, sizeof(draft.bbs_callsign)); else if (!oyk_parse_callsign_with_default_ssid(ed, draft.bbs_ssid, &draft.bbs_callsign)) strcpy(fb, "Invalid BBS callsign."); }
                else if (f == 3) { if (!ed[0]) memset(&draft.bbs_alias, 0, sizeof(draft.bbs_alias)); else if (!oyk_parse_callsign(ed, &draft.bbs_alias)) strcpy(fb, "Invalid BBS alias."); }
                else if (f == 5) { if (ed[0]) { strncpy(draft.bbs_files_path, ed, sizeof(draft.bbs_files_path) - 1); draft.bbs_files_path[sizeof(draft.bbs_files_path) - 1] = '\0'; } }
                else if (f == 6) { if (ed[0]) { strncpy(draft.bbs_mail_path, ed, sizeof(draft.bbs_mail_path) - 1); draft.bbs_mail_path[sizeof(draft.bbs_mail_path) - 1] = '\0'; } }
                else if (f == 7) { strncpy(draft.bbs_welcome, ed, sizeof(draft.bbs_welcome) - 1); draft.bbs_welcome[sizeof(draft.bbs_welcome) - 1] = '\0'; }
            } else if (tab == 2) {
                if (f == 1) { int sv = atoi(ed); if (sv >= 0 && sv <= 15) draft.chat_ssid = static_cast<oyk_u8>(sv); else strcpy(fb, "SSID must be 0..15."); }
                else if (f == 2) { if (!ed[0]) memset(&draft.chat_callsign, 0, sizeof(draft.chat_callsign)); else if (!oyk_parse_callsign_with_default_ssid(ed, draft.chat_ssid, &draft.chat_callsign)) strcpy(fb, "Invalid CHAT callsign."); }
                else if (f == 3) { if (!ed[0]) memset(&draft.chat_alias, 0, sizeof(draft.chat_alias)); else if (!oyk_parse_callsign(ed, &draft.chat_alias)) strcpy(fb, "Invalid CHAT alias."); }
                else if (f == 4) { if (ed[0]) { strncpy(draft.chat_topic, ed, sizeof(draft.chat_topic) - 1); draft.chat_topic[sizeof(draft.chat_topic) - 1] = '\0'; } }
                else if (f == 5) { strncpy(draft.chat_welcome, ed, sizeof(draft.chat_welcome) - 1); draft.chat_welcome[sizeof(draft.chat_welcome) - 1] = '\0'; }
            } else {
                if (f == 2) { if (ed[0]) { strncpy(draft.rig_host, ed, sizeof(draft.rig_host) - 1); draft.rig_host[sizeof(draft.rig_host) - 1] = '\0'; } }
                else if (f == 3) { long pv = atol(ed); if (pv > 0L && pv <= 65535L) draft.rig_port = static_cast<oyk_u16>(pv); else strcpy(fb, "Port must be 1..65535."); }
                else if (f == 4) { long iv = atol(ed); if (iv >= 0L && iv <= 3600L) draft.rig_poll_sec = static_cast<oyk_u16>(iv); }
            }
        }
    }
    vid_clear(0x07);
}

static void run_dos_menu_action(int chosen, int* running) {
    if (chosen == 0) {
        g_node.set_accept_incoming(1);
        strcpy(g_status_line, "New incoming connections are now ALLOWED.");
    } else if (chosen == 1) {
        g_node.set_accept_incoming(0);
        strcpy(g_status_line, "New incoming connections are now DENIED.");
    } else if (chosen == 2) {
        run_dos_local_terminal();
    } else if (chosen == 3) {
        run_dos_approve_incoming_files();
    } else if (chosen == 4) {
        run_dos_rig_control();
    } else if (chosen == 5) {
        run_dos_settings();
    } else if (chosen == 6) {
        char id_buf[12];
        prompt_line("Terminate session ID (Esc=cancel): ", id_buf, sizeof(id_buf));
        if (id_buf[0]) {
            g_node.terminate_session(static_cast<oyk_u32>(atol(id_buf)),
                                     g_status_line, sizeof(g_status_line));
        }
    } else if (chosen == 7) {
        char call_buf[16];
        prompt_line("Ban callsign (prefix '-' to unban): ", call_buf, sizeof(call_buf));
        if (call_buf[0] == '-' && call_buf[1]) {
            char confirm_p[48], yn[4];
            sprintf(confirm_p, "Unban %s? [Y/N]: ", call_buf + 1);
            prompt_line(confirm_p, yn, sizeof(yn));
            if (yn[0] == 'y' || yn[0] == 'Y') {
                g_node.unban_callsign(call_buf + 1, g_status_line, sizeof(g_status_line));
            } else {
                strcpy(g_status_line, "Unban cancelled.");
            }
        } else if (call_buf[0]) {
            char lift_buf[32];
            prompt_line("Lift [permanent, 30m, 12h, 7d, YYYY-MM-DD]: ", lift_buf, sizeof(lift_buf));
            g_node.ban_callsign(call_buf, OYK_BAN_APPEAL_ALLOWED,
                                "Banned by DOS sysop.",
                                lift_buf[0] ? lift_buf : "permanent",
                                g_status_line, sizeof(g_status_line));
        }
    } else if (chosen == 8) {
        if (confirm_dos_exit()) {
            *running = 0;
        }
    }
}

static void open_dos_node_menu(int* running) {
    static const char* ITEMS[9] = {
        " [A] Allow Incoming Connections ",
        " [D] Deny Incoming Connections  ",
        " [L] Local Terminal...          ",
        " [F] Manage Files Area...       ",
        " [R] Radio Control...           ",
        " [S] Settings...                ",
        " [T] Terminate Connection...    ",
        " [B] Ban / Unban Callsign...    ",
        " [X] Exit                       "
    };
    static const int ITEM_ROWS[9] = { 2, 3, 5, 6, 7, 8, 9, 10, 12 };

    int sel = 0;
    int chosen = -1;
    while (chosen < 0 && !g_break_flag) {
        vid_puts(1, 1, 0x70, "+--------------------------------+", 34);
        vid_puts(4, 1, 0x70, "+--------------------------------+", 34);
        vid_puts(11, 1, 0x70, "+--------------------------------+", 34);
        vid_puts(13, 1, 0x70, "+--------------------------------+", 34);
        for (int i = 0; i < 9; ++i) {
            vid_puts(ITEM_ROWS[i], 1, 0x70, "|", 1);
            vid_puts(ITEM_ROWS[i], 2, (i == sel) ? 0x1F : 0x70, ITEMS[i], 32);
            vid_puts(ITEM_ROWS[i], 34, 0x70, "|", 1);
        }

        int ch = getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 72) sel = (sel + 8) % 9;      /* Up arrow */
            else if (ext == 80) sel = (sel + 1) % 9; /* Down arrow */
            else if (ext == 68) break;               /* F10 closes menu */
        } else if (ch == 27) {
            break;
        } else if (ch == '\r' || ch == '\n') {
            chosen = sel;
        } else if (ch == 'a' || ch == 'A') {
            chosen = 0;
        } else if (ch == 'd' || ch == 'D') {
            chosen = 1;
        } else if (ch == 'l' || ch == 'L') {
            chosen = 2;
        } else if (ch == 'f' || ch == 'F') {
            chosen = 3;
        } else if (ch == 'r' || ch == 'R') {
            chosen = 4;
        } else if (ch == 's' || ch == 'S') {
            chosen = 5;
        } else if (ch == 't' || ch == 'T') {
            chosen = 6;
        } else if (ch == 'b' || ch == 'B') {
            chosen = 7;
        } else if (ch == 'x' || ch == 'X' || ch == 'q' || ch == 'Q') {
            chosen = 8;
        }
    }

    render_dos_screen();
    if (chosen >= 0) {
        run_dos_menu_action(chosen, running);
    }
}

static void resolve_dos_config_path(char* out, size_t out_sz) {
    /* Search order: %HOME%\.oyk\node.cfg -> \etc\oyk\node.cfg -> .\node.cfg */
    const char* home = getenv("HOME");
    FILE* fp = NULL;
    if (home && home[0]) {
        sprintf(out, "%s\\.oyk\\node.cfg", home);
        fp = fopen(out, "rb");
        if (fp) { fclose(fp); return; }
    }
    fp = fopen("\\etc\\oyk\\node.cfg", "rb");
    if (fp) { fclose(fp); strcpy(out, "\\etc\\oyk\\node.cfg"); return; }
    fp = fopen("node.cfg", "rb");
    if (fp) { fclose(fp); strcpy(out, "node.cfg"); return; }
    out[0] = '\0';
}

static void print_dos_usage(const char* prog) {
    printf("OYK Amateur Radio Packet Node (MS-DOS)\n");
    printf("Usage: %s [options]\n\n", prog ? prog : "oyknode.exe");
    printf("Options:\n");
    printf("  -c, --config <path>      Load configuration file\n");
    printf("      --callsign <CALL>    Override node callsign (e.g. N0CALL or N0CALL-7; default SSID 7)\n");
    printf("      --alias <ALIAS>      Override node alias (e.g. OYKNOD)\n");
    printf("      --port <UDP_PORT>    Override first AXUDP bind port (default 10093)\n");
    printf("      --headless           Run without full-screen UI (log to stdout)\n");
    printf("  -h, --help               Show this help\n");
}

int main(int argc, char* argv[]) {
    static OykConfig cfg;
    char cfg_path[128];
    char override_call[16];
    char override_alias[16];
    oyk_u16 override_port = 0;
    int headless = 0;
    char err[96];
    cfg_path[0] = '\0';
    override_call[0] = '\0';
    override_alias[0] = '\0';

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const char* eq = strchr(a, '=');
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0 || strcmp(a, "/?") == 0) {
            print_dos_usage(argv[0]);
            return 0;
        } else if ((strcmp(a, "-c") == 0 || strcmp(a, "--config") == 0) && i + 1 < argc) {
            strncpy(cfg_path, argv[++i], sizeof(cfg_path) - 1);
            cfg_path[sizeof(cfg_path) - 1] = '\0';
        } else if (strncmp(a, "--config=", 9) == 0 && eq) {
            strncpy(cfg_path, eq + 1, sizeof(cfg_path) - 1);
            cfg_path[sizeof(cfg_path) - 1] = '\0';
        } else if (strcmp(a, "--callsign") == 0 && i + 1 < argc) {
            strncpy(override_call, argv[++i], sizeof(override_call) - 1);
            override_call[sizeof(override_call) - 1] = '\0';
        } else if (strncmp(a, "--callsign=", 11) == 0 && eq) {
            strncpy(override_call, eq + 1, sizeof(override_call) - 1);
            override_call[sizeof(override_call) - 1] = '\0';
        } else if (strcmp(a, "--alias") == 0 && i + 1 < argc) {
            strncpy(override_alias, argv[++i], sizeof(override_alias) - 1);
            override_alias[sizeof(override_alias) - 1] = '\0';
        } else if (strncmp(a, "--alias=", 8) == 0 && eq) {
            strncpy(override_alias, eq + 1, sizeof(override_alias) - 1);
            override_alias[sizeof(override_alias) - 1] = '\0';
        } else if (strcmp(a, "--port") == 0 && i + 1 < argc) {
            long pv = atol(argv[++i]);
            if (pv <= 0L || pv > 65535L) {
                printf("Invalid --port value: %s\n", argv[i]);
                return 1;
            }
            override_port = static_cast<oyk_u16>(pv);
        } else if (strncmp(a, "--port=", 7) == 0 && eq) {
            long pv = atol(eq + 1);
            if (pv <= 0L || pv > 65535L) {
                printf("Invalid --port value: %s\n", eq + 1);
                return 1;
            }
            override_port = static_cast<oyk_u16>(pv);
        } else if (strcmp(a, "--headless") == 0) {
            headless = 1;
        } else {
            printf("Unknown or incomplete argument: %s\n", a);
            print_dos_usage(argv[0]);
            return 1;
        }
    }
    if (!cfg_path[0]) resolve_dos_config_path(cfg_path, sizeof(cfg_path));

    if (cfg_path[0]) {
        if (!oyk_config_load(cfg_path, &cfg, err, sizeof(err))) {
            printf("Config error (%s): %s\n", cfg_path, err);
            return 1;
        }
    } else {
        oyk_config_init_default(&cfg);
    }

    if (override_call[0]) {
        if (!oyk_parse_callsign_with_default_ssid(override_call, cfg.callsign.ssid, &cfg.callsign)) {
            printf("Invalid callsign: %s\n", override_call);
            return 1;
        }
    }
    if (override_alias[0]) {
        if (!oyk_parse_callsign(override_alias, &cfg.alias)) {
            printf("Invalid alias: %s\n", override_alias);
            return 1;
        }
    }
    if (override_port > 0 && cfg.iface_count > 0) {
        int updated = 0;
        for (int i = 0; i < cfg.iface_count; ++i) {
            if (strcmp(cfg.ifaces[i].type, "axudp") == 0) {
                cfg.ifaces[i].port = override_port;
                updated = 1;
                break;
            }
        }
        if (!updated) cfg.ifaces[0].port = override_port;
    }

    /* Optional LIM EMS 4.0 initialization (4 x 16KB = 64KB expanded scrollback if available) */
    g_ems.init(4);

    /* Optional Bret Johnson DOS USB Stack detection (USBUHCI / USBUHCIL) */
    oyk_dos_usb_detect(&g_usb_stack);

    /* Initialize standalone mini IPv4/ARP/UDP stack (used for USB CDC/PPP/PacketDrv when mTCP is absent) */
    oyk_u32 def_loc_ip = 0, def_peer_ip = 0;
    oyk_parse_ipv4("192.168.7.2", &def_loc_ip);
    oyk_parse_ipv4("192.168.7.1", &def_peer_ip);
    oyk_dos_mininet_init(&g_mininet, def_loc_ip, def_peer_ip);
    g_mininet.udp_cb = mininet_udp_rx_cb;
    g_mininet.udp_ctx = NULL;

    /* Optional mTCP initialization: never a hard requirement at build time or runtime */
    g_mtcp_active = 0;
#ifdef OYK_DOS_HAS_MTCP
    if (Utils::parseEnv() == 0 &&
        Utils::initStack(4, 8, ctrl_break_handler, ctrl_c_handler) == 0) {
        g_mtcp_active = 1;
    }
#endif
    if (!g_mtcp_active) {
        _dos_setvect(0x1B, ctrl_break_handler);
        _dos_setvect(0x23, ctrl_c_handler);
        if (oyk_dos_pktdrv_init(&g_pktdrv)) {
            memcpy(g_mininet.local_mac, g_pktdrv.mac, 6);
            g_mininet.tx_eth = mininet_tx_eth_cb;
            g_mininet.tx_eth_ctx = NULL;
        }
    }

    if (!g_node.init(&cfg, dos_tx_callback, NULL, err, sizeof(err))) {
        printf("Node init failed: %s\n", err);
#ifdef OYK_DOS_HAS_MTCP
        if (g_mtcp_active) Utils::endStack();
#endif
        oyk_dos_pktdrv_shutdown(&g_pktdrv);
        g_ems.shutdown();
        return 1;
    }

    if (g_usb_stack.installed) {
        char umsg[80];
        sprintf(umsg, "Detected Bret Johnson DOS USB stack: %.8s %.8s (AMIS 0x%02X)",
                g_usb_stack.vendor, g_usb_stack.product,
                static_cast<unsigned int>(g_usb_stack.amis_mux_id));
        g_node.log_message(OYK_LOG_INFO, umsg);
    }
    if (!g_mtcp_active) {
        if (g_pktdrv.active) {
            g_node.log_message(OYK_LOG_INFO, "mTCP inactive; using standalone Packet Driver UDP transport.");
        } else {
            g_node.log_message(OYK_LOG_WARNING, "mTCP inactive; running with local/serial/USB transports.");
        }
    }

    oyk_rig_status_init(&g_rig_status, oyk_rig_parse_protocol(cfg.rig_protocol));
    rebind_dos_transports();

    if (headless) {
        char call_s[16];
        oyk_format_callsign(&cfg.callsign, call_s, sizeof(call_s));
        printf("OYK Packet Node (MS-DOS) %s running in headless mode. Press Ctrl+C to stop.\n", call_s);
        int printed_logs = 0;
        while (!g_break_flag) {
            dos_pump_network();
            g_node.tick();
            while (printed_logs < g_node.console_log_count()) {
                printf("%s\n", g_node.console_log_line(printed_logs++));
            }
        }
    } else {
        g_vseg = detect_video_seg();
        vid_clear(0x07);

        int running = 1;
        oyk_u32 last_draw = dos_ticks_now();

        while (running && !g_break_flag) {
            dos_pump_network();
            g_node.tick();

            if (kbhit()) {
                int ch = getch();
                if (ch == 0 || ch == 0xE0) {
                    int ext = getch();
                    if (ext == 68) { /* F10 */
                        open_dos_node_menu(&running);
                    } else if (ext == 67) { /* F9: Switch to Local Terminal */
                        run_dos_local_terminal();
                    }
                } else if (ch == '\t') {
                    run_dos_local_terminal();
                } else if (ch == 'm' || ch == 'M' || ch == 'n' || ch == 'N' || ch == '\r' || ch == '\n') {
                    open_dos_node_menu(&running);
                } else if (ch == 'a' || ch == 'A') {
                    run_dos_menu_action(0, &running);
                } else if (ch == 'd' || ch == 'D') {
                    run_dos_menu_action(1, &running);
                } else if (ch == 'l' || ch == 'L') {
                    run_dos_menu_action(2, &running);
                } else if (ch == 'f' || ch == 'F') {
                    run_dos_menu_action(3, &running);
                } else if (ch == 'r' || ch == 'R') {
                    run_dos_menu_action(4, &running);
                } else if (ch == 's' || ch == 'S') {
                    run_dos_menu_action(5, &running);
                } else if (ch == 't' || ch == 'T') {
                    run_dos_menu_action(6, &running);
                } else if (ch == 'b' || ch == 'B') {
                    run_dos_menu_action(7, &running);
                } else if (ch == 'q' || ch == 'Q' || ch == 'x' || ch == 'X') {
                    run_dos_menu_action(8, &running);
                }
                render_dos_screen();
            }

            if (dos_ticks_elapsed_ms(last_draw) >= 250UL) {
                last_draw = dos_ticks_now();
                render_dos_screen();
            }
        }
    }

    if (g_dterm_sid != 0) {
        oyk_yapp_reset(&g_dterm_yapp);
        g_node.close_local_terminal(g_dterm_sid);
        g_dterm_sid = 0;
    }
    g_node.set_local_event_callback(NULL, NULL);

    if (g_node.begin_shutdown() > 0) {
        oyk_u32 shut_start = dos_ticks_now();
        while (g_node.disconnecting_count() > 0 &&
               dos_ticks_elapsed_ms(shut_start) < 1000UL) {
            dos_pump_network();
        }
    }
    g_node.finish_shutdown();
#ifdef OYK_DOS_HAS_MTCP
    if (g_mtcp_active) {
        if (g_axudp_iface_idx >= 0) {
            Udp::unregisterCallback(g_axudp_port);
        }
        if (g_rf_udp_bound) {
            Udp::unregisterCallback(g_rf_local_port);
            g_rf_udp_bound = 0;
        }
        Utils::endStack();
    }
#endif
    oyk_dos_serial_close(&g_serial);
    oyk_dos_pktdrv_shutdown(&g_pktdrv);
    g_ems.shutdown();
    vid_clear(0x07);
    printf("OYK Packet Node (MS-DOS) exited cleanly.\n");
    return 0;
}
