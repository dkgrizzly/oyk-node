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
#include "../compat/oyk_printer.hpp"
#include "dos_usb.hpp"
#include "dos_video.hpp"
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

/* Direct MS-DOS line-by-line printer output (BIOS INT 17h LPT1..3 or COM1..4) */
static int g_prn_mode = 0; /* 0=none, 1=BIOS INT 17h LPT, 2=COM serial */
static int g_prn_lpt = 0;
static int g_prn_proto = OYK_PRN_NONE;
static OykDosSerial g_prn_serial;

static void dos_printer_close() {
    oyk_dos_serial_close(&g_prn_serial);
    g_prn_mode = 0;
    g_prn_proto = OYK_PRN_NONE;
}

static int dos_printer_write_bytes(const oyk_u8* data, size_t len) {
    if (!data || len == 0) return 1;
    if (g_prn_mode == 1) {
        for (size_t i = 0; i < len; ++i) {
            union REGS regs;
            memset(&regs, 0, sizeof(regs));
            regs.h.ah = 0x00;
            regs.h.al = data[i];
            regs.x.dx = static_cast<unsigned int>(g_prn_lpt);
            int86(0x17, &regs, &regs);
            if ((regs.h.ah & 0x29U) != 0U) return 0;
        }
        return 1;
    }
    if (g_prn_mode == 2 && g_prn_serial.active) {
        for (size_t i = 0; i < len; ++i) {
            if (!oyk_dos_serial_write_byte(&g_prn_serial, data[i])) return 0;
        }
        return 1;
    }
    return 0;
}

static void dos_printer_init() {
    dos_printer_close();
    const OykConfig& cfg = g_node.config();
    if (!cfg.printer_enabled || !cfg.printer_device[0]) return;
    int proto = oyk_printer_parse_protocol(cfg.printer_protocol);
    if (proto == OYK_PRN_NONE) return;

    char dev[24];
    size_t dn = strlen(cfg.printer_device);
    if (dn >= sizeof(dev)) dn = sizeof(dev) - 1;
    for (size_t i = 0; i < dn; ++i) {
        char c = cfg.printer_device[i];
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        dev[i] = c;
    }
    dev[dn] = '\0';
    while (dn > 0 && dev[dn - 1] == ':') dev[--dn] = '\0';

    if (strncmp(dev, "LPT", 3) == 0 && dev[3] >= '1' && dev[3] <= '3') {
        g_prn_lpt = dev[3] - '1';
        g_prn_mode = 1;
        g_prn_proto = proto;
    } else if (strcmp(dev, "PRN") == 0) {
        g_prn_lpt = 0;
        g_prn_mode = 1;
        g_prn_proto = proto;
    } else if (strncmp(dev, "COM", 3) == 0 && dev[3] >= '1' && dev[3] <= '4') {
        oyk_u32 baud = cfg.printer_baud ? cfg.printer_baud : 9600UL;
        if (oyk_dos_serial_open(&g_prn_serial, dev, baud)) {
            g_prn_mode = 2;
            g_prn_proto = proto;
        }
    }

    if (g_prn_mode != 0) {
        unsigned char init_buf[32];
        size_t ilen = oyk_printer_build_init(g_prn_proto, cfg.printer_condensed,
                                             init_buf, sizeof(init_buf));
        if (ilen > 0) {
            (void)dos_printer_write_bytes(init_buf, ilen);
        }
    }
}

static void dos_printer_callback(oyk_u32 level, const char* formatted_line, void* /*ctx*/) {
    if (g_prn_mode == 0 || g_prn_proto == OYK_PRN_NONE || !formatted_line) return;
    unsigned char out[384];
    size_t len = oyk_printer_format_line(g_prn_proto, static_cast<unsigned long>(level),
                                         formatted_line, out, sizeof(out));
    if (len > 0) {
        (void)dos_printer_write_bytes(out, len);
    }
}

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

/* ==================== Direct 80x25 Text Video RAM UI (Dirty-Diff, Zero Snow-Blank) ==================== */

static OykDosVideo* g_vid = NULL;

static void vid_puts(int row, int col, oyk_u8 attr, const char* s, int max_w) {
    if (g_vid) dos_vid_puts(g_vid, row, col, attr, s, max_w);
}

static void vid_clear(oyk_u8 attr) {
    if (g_vid) dos_vid_clear(g_vid, attr);
}

static int dos_local_term_is_active();

static void render_dos_screen() {
    char line[82];
    char call_s[16];
    oyk_format_callsign(&g_node.config().callsign, call_s, sizeof(call_s));

    oyk_u8 menu_bg  = dos_vid_attr(g_vid, 0x70U, 0x70U);
    oyk_u8 menu_sel = dos_vid_attr(g_vid, 0x30U, 0x0FU);
    oyk_u8 top_hdr  = dos_vid_attr(g_vid, 0x1FU, 0x70U);
    oyk_u8 allow_at = dos_vid_attr(g_vid, 0x2FU, 0x0FU);
    oyk_u8 deny_at  = dos_vid_attr(g_vid, 0x4FU, 0x70U);
    oyk_u8 bdr_at   = dos_vid_attr(g_vid, 0x03U, 0x07U);
    oyk_u8 ttl_at   = dos_vid_attr(g_vid, 0x0BU, 0x0FU);
    oyk_u8 list_hdr = dos_vid_list_hdr_attr(g_vid);
    oyk_u8 norm_at  = 0x07U;
    oyk_u8 dim_at   = dos_vid_attr(g_vid, 0x08U, 0x07U);
    oyk_u8 run_up   = dos_vid_attr(g_vid, 0x0AU, 0x0FU);
    oyk_u8 run_dn   = dos_vid_attr(g_vid, 0x0CU, 0x07U);
    oyk_u8 stat_at  = dos_vid_attr(g_vid, 0x0EU, 0x0FU);

    /* Row 0: Top Menu Bar (matching GUI and ncurses menu bar) */
    vid_puts(0, 0,  menu_bg, "", 80);
    vid_puts(0, 1,  menu_sel, " Node ", 6);
    if (dos_local_term_is_active()) {
        vid_puts(0, 44, menu_bg, "Tab/F9:Term(ACTIVE) F10/M:Menu ", 36);
    } else {
        vid_puts(0, 52, menu_bg, "Tab/F9:Term  F10/M:Menu ", 28);
    }

    /* Row 1: Header bar */
    sprintf(line, " OYK NODE (DOS): %-9s | Up:%lus | C:%d | %s%s",
            call_s,
            static_cast<unsigned long>(g_node.uptime_seconds()),
            g_node.total_active_connections(),
            g_ems.available() ? "EMS " : "",
            g_usb_stack.installed ? "USB" : (g_mtcp_active ? "mTCP" : "STD"));
    vid_puts(1, 0, top_hdr, line, 61);
    vid_puts(1, 61, g_node.accept_incoming() ? allow_at : deny_at,
             g_node.accept_incoming() ? " [INCOMING: ALLOW] " : " [INCOMING: DENY]  ", 19);

    /* Row 2: Single-line border separating Interfaces (0..43) & New Heard (43..79) */
    dos_vid_putc(g_vid, 2, 0, bdr_at, OYK_BOX_TL);
    dos_vid_hline(g_vid, 2, 1, 42, bdr_at, OYK_BOX_H);
    dos_vid_title(g_vid, 2, 2, 39, ttl_at, "Radio Interfaces (Active Conn)");
    dos_vid_putc(g_vid, 2, 43, bdr_at, OYK_BOX_TT);
    dos_vid_hline(g_vid, 2, 44, 35, bdr_at, OYK_BOX_H);
    dos_vid_title(g_vid, 2, 45, 33, ttl_at, "Last 5 New Callsigns Heard");
    dos_vid_putc(g_vid, 2, 79, bdr_at, OYK_BOX_TR);

    /* Row 3: Dark-grey list headers (0x8F color / 0x70 mono) */
    dos_vid_putc(g_vid, 3, 0, bdr_at, OYK_BOX_V);
    vid_puts(3, 1, list_hdr, "Port    Type   Conn Status       RX/TX    ", 42);
    dos_vid_putc(g_vid, 3, 43, bdr_at, OYK_BOX_V);
    vid_puts(3, 44, list_hdr, "#  Callsign   Port     Heard       ", 35);
    dos_vid_putc(g_vid, 3, 79, bdr_at, OYK_BOX_V);

    /* Rows 4..8: Interfaces + New Callsigns Heard */
    for (int i = 0; i < 5; ++i) {
        dos_vid_putc(g_vid, 4 + i, 0, bdr_at, OYK_BOX_V);
        if (i < g_node.iface_count()) {
            const OykIfaceRuntime& r = g_node.iface(i);
            sprintf(line, "%-7.7s %-6.6s %-4d %-12.12s %lu/%lu",
                    r.cfg.id, r.cfg.type, g_node.active_connections_on(i), r.status,
                    static_cast<unsigned long>(r.rx_frames),
                    static_cast<unsigned long>(r.tx_frames));
            vid_puts(4 + i, 1, r.running ? run_up : run_dn, line, 42);
        } else {
            vid_puts(4 + i, 1, norm_at, "", 42);
        }

        dos_vid_putc(g_vid, 4 + i, 43, bdr_at, OYK_BOX_V);
        if (i < g_node.new_heard_count()) {
            const OykNewHeard& nh = g_node.new_heard(i);
            sprintf(line, "%d. %-10.10s %-8.8s %-8.8s",
                    i + 1, nh.callsign, nh.iface_id, nh.time_hms);
            vid_puts(4 + i, 44, norm_at, line, 35);
        } else {
            vid_puts(4 + i, 44, dim_at,
                     (i == 0 && g_node.new_heard_count() == 0) ? "(none heard yet)" : "", 35);
        }
        dos_vid_putc(g_vid, 4 + i, 79, bdr_at, OYK_BOX_V);
    }

    /* Row 9..15: Active Connections section */
    dos_vid_hdiv(g_vid, 9, 0, 80, bdr_at);
    dos_vid_putc(g_vid, 9, 43, bdr_at, OYK_BOX_BT);
    sprintf(line, "Active Connections (%d) | Banned: %d",
            g_node.total_active_connections(), g_node.ban_count());
    dos_vid_title(g_vid, 9, 2, 76, ttl_at, line);

    dos_vid_putc(g_vid, 10, 0, bdr_at, OYK_BOX_V);
    vid_puts(10, 1, list_hdr, "ID   Callsign   Port     Since    Mode", 78);
    dos_vid_putc(g_vid, 10, 79, bdr_at, OYK_BOX_V);

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
            dos_vid_putc(g_vid, row, 0, bdr_at, OYK_BOX_V);
            vid_puts(row, 1, norm_at, line, 78);
            dos_vid_putc(g_vid, row, 79, bdr_at, OYK_BOX_V);
            ++row;
        }
    }
    while (row < 16) {
        dos_vid_putc(g_vid, row, 0, bdr_at, OYK_BOX_V);
        vid_puts(row, 1, norm_at, "", 78);
        dos_vid_putc(g_vid, row, 79, bdr_at, OYK_BOX_V);
        ++row;
    }

    /* Rows 16..23: Node Activity Log section */
    dos_vid_hdiv(g_vid, 16, 0, 80, bdr_at);
    dos_vid_title(g_vid, 16, 2, 76, ttl_at, "Node Activity Log");
    int total_logs = g_node.console_log_count();
    int start = (total_logs > 6) ? (total_logs - 6) : 0;
    for (int r = 0; r < 6; ++r) {
        int idx = start + r;
        dos_vid_putc(g_vid, 17 + r, 0, bdr_at, OYK_BOX_V);
        vid_puts(17 + r, 1, norm_at, (idx < total_logs) ? g_node.console_log_line(idx) : "", 78);
        dos_vid_putc(g_vid, 17 + r, 79, bdr_at, OYK_BOX_V);
    }
    dos_vid_putc(g_vid, 23, 0, bdr_at, OYK_BOX_BL);
    dos_vid_hline(g_vid, 23, 1, 78, bdr_at, OYK_BOX_H);
    dos_vid_putc(g_vid, 23, 79, bdr_at, OYK_BOX_BR);

    /* Row 24: Status message line */
    vid_puts(24, 0, stat_at, g_status_line, 80);
    dos_vid_flush(g_vid);
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
    dos_vid_flush_row(g_vid, 24);
    int len = 0;
    out[0] = '\0';
    int pcol = static_cast<int>(strlen(prompt));
    while (!g_break_flag) {
        dos_pump_network();
        g_node.tick();
        if (!kbhit()) continue;
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
        dos_vid_flush_row(g_vid, 24);
    }
}

static void run_dos_settings(int initial_tab = 0);

static int confirm_dos_exit() {
    const int auto_cnt = g_node.auto_learned_peer_count();
    if (auto_cnt > 0) {
        const int top = 6, left = 4, h = 11, w = 72;
        oyk_u8 bdr_at  = dos_vid_attr(g_vid, 0x1FU, 0x0FU);
        oyk_u8 fill_at = dos_vid_attr(g_vid, 0x17U, 0x07U);
        oyk_u8 ttl_at  = dos_vid_attr(g_vid, 0x1FU, 0x70U);
        oyk_u8 warn_at = dos_vid_attr(g_vid, 0x1EU, 0x0FU);
        oyk_u8 btn_at  = dos_vid_attr(g_vid, 0x70U, 0x07U);
        oyk_u8 sel_at  = dos_vid_attr(g_vid, 0x30U, 0x70U);

        static const char* BTNS[4] = {
            "[ Save & Exit ]", "[ Discard & Exit ]", "[ Review Peers ]", "[ Cancel ]"
        };
        int focus = 2; /* Review Peers */
        int act = 3;   /* Cancel */
        int dirty = 1;
        while (!g_break_flag) {
            dos_pump_network();
            g_node.tick();
            if (dirty) {
                dirty = 0;
                dos_vid_box(g_vid, top, left, h, w, bdr_at, fill_at, 1);
                dos_vid_title(g_vid, top, left + 2, w - 4, ttl_at, "Auto-Learned Peers");
                char l1[68];
                sprintf(l1, "%d auto-learned peer(s) were added during this session.", auto_cnt);
                vid_puts(top + 2, left + 3, warn_at, l1, w - 6);
                vid_puts(top + 3, left + 3, fill_at,
                         "Save them to configuration, discard them, or review in Peers?", w - 6);
                int bx = left + 3;
                for (int i = 0; i < 4; ++i) {
                    int bl = static_cast<int>(strlen(BTNS[i]));
                    vid_puts(top + 6, bx, (focus == i) ? sel_at : btn_at, BTNS[i], bl);
                    bx += bl + 2;
                }
                dos_vid_hdiv(g_vid, top + 8, left, w, bdr_at);
                vid_puts(top + 9, left + 2, fill_at,
                         "Left/Right/Tab:Select  Enter:OK  S:Save  D:Discard  R:Review  Esc", w - 4);
                dos_vid_flush(g_vid);
            }
            if (!kbhit()) continue;
            dirty = 1;
            int ch = getch();
            if (ch == 0 || ch == 0xE0) {
                int ext = getch();
                if (ext == 75) focus = (focus + 3) % 4;
                else if (ext == 77) focus = (focus + 1) % 4;
                continue;
            }
            if (ch == '\t') focus = (focus + 1) % 4;
            else if (ch == 27 || ch == 'c' || ch == 'C') { act = 3; break; }
            else if (ch == 's' || ch == 'S') { act = 0; break; }
            else if (ch == 'd' || ch == 'D') { act = 1; break; }
            else if (ch == 'r' || ch == 'R') { act = 2; break; }
            else if (ch == '\r' || ch == '\n' || ch == ' ') { act = focus; break; }
        }
        if (act == 2) {
            run_dos_settings(4);
            render_dos_screen();
            return 0;
        }
        if (act == 0) {
            char err[64];
            g_node.persist_auto_learned_peers(err, sizeof(err));
            return 1;
        }
        if (act == 1) {
            g_node.discard_auto_learned_peers();
            return 1;
        }
        render_dos_screen();
        return 0;
    }

    const int top = 7, left = 12, h = 9, w = 56;
    oyk_u8 bdr_at  = dos_vid_attr(g_vid, 0x1FU, 0x0FU);
    oyk_u8 fill_at = dos_vid_attr(g_vid, 0x17U, 0x07U);
    oyk_u8 ttl_at  = dos_vid_attr(g_vid, 0x1FU, 0x70U);
    oyk_u8 warn_at = dos_vid_attr(g_vid, 0x1EU, 0x0FU);
    oyk_u8 btn_at  = dos_vid_attr(g_vid, 0x70U, 0x07U);
    oyk_u8 sel_at  = dos_vid_attr(g_vid, 0x30U, 0x70U);

    int focus = 1; /* Default to [ No / Cancel ] */
    int confirmed = 0;
    int last_active = -1;
    int dirty = 1;

    while (!g_break_flag) {
        dos_pump_network();
        g_node.tick();
        int active = g_node.total_active_connections();
        if (active != last_active) {
            last_active = active;
            dirty = 1;
        }
        if (dirty) {
            dirty = 0;
            dos_vid_box(g_vid, top, left, h, w, bdr_at, fill_at, 1);
            dos_vid_title(g_vid, top, left + 2, w - 4, ttl_at, "Exit OYK Node");

            char line1[60];
            if (active > 0) {
                sprintf(line1, "Exit OYK Node? (%d active connection%s)",
                        active, (active == 1) ? "" : "s");
                vid_puts(top + 2, left + 3, warn_at, line1, w - 6);
                vid_puts(top + 3, left + 3, fill_at,
                         "Active sessions will be gracefully disconnected.", w - 6);
            } else {
                vid_puts(top + 2, left + 3, fill_at,
                         "Are you sure you want to exit OYK Node?", w - 6);
                vid_puts(top + 3, left + 3, fill_at,
                         "(No active connections)", w - 6);
            }

            /* Centered buttons at row top + 5 */
            vid_puts(top + 5, left + 13, (focus == 0) ? sel_at : btn_at, "[ Yes / Exit ]", 14);
            vid_puts(top + 5, left + 30, (focus == 1) ? sel_at : btn_at, "[ No / Cancel ]", 15);

            dos_vid_hdiv(g_vid, top + 6, left, w, bdr_at);
            vid_puts(top + 7, left + 2, fill_at,
                     "Left/Right/Tab:Select   Enter:OK   Y/N/Esc", w - 4);
            dos_vid_flush(g_vid);
        }

        if (!kbhit()) continue;
        dirty = 1;
        int ch = getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 75 || ext == 77) focus = 1 - focus; /* Left / Right */
            continue;
        }
        if (ch == '\t') {
            focus = 1 - focus;
        } else if (ch == '\r' || ch == '\n' || ch == ' ') {
            confirmed = (focus == 0) ? 1 : 0;
            break;
        } else if (ch == 'y' || ch == 'Y') {
            confirmed = 1;
            break;
        } else if (ch == 'n' || ch == 'N' || ch == 27) {
            confirmed = 0;
            break;
        }
    }

    if (!confirmed) {
        render_dos_screen();
    }
    return confirmed;
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
static int g_dterm_dirty = 1;

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
    g_dterm_dirty = 1;
}

static int dos_yapp_send_cb(const oyk_u8* data, size_t len, void* /*ctx*/) {
    if (g_dterm_sid == 0 || !g_dterm_connected) return 0;
    return g_node.send_local_bytes(g_dterm_sid, data, len);
}

static int dos_yapp_overwrite_cb(const char* target_path, const char* /*filename*/, void* /*ctx*/) {
    char prompt[80];
    sprintf(prompt, "Overwrite %.48s? [Y/N]: ", target_path);
    vid_puts(24, 0, 0x4F, prompt, 80);
    dos_vid_flush_row(g_vid, 24);
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
        g_dterm_dirty = 1;
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
    int dirty = 1;
    const int panel_x = 8;
    const int panel_w = 64;
    const int list_y = 5;
    const int list_h = 15;
    oyk_u8 bdr_at  = dos_vid_attr(g_vid, 0x1FU, 0x0FU);
    oyk_u8 hdr_at  = dos_vid_list_hdr_attr(g_vid);
    oyk_u8 dir_at  = dos_vid_attr(g_vid, 0x1EU, 0x0FU);
    oyk_u8 file_at = dos_vid_attr(g_vid, 0x1FU, 0x07U);
    oyk_u8 sel_at  = dos_vid_attr(g_vid, 0x30U, 0x70U);

    while (!g_break_flag) {
        dos_pump_network();

        if (sel >= count) sel = (count > 0) ? (count - 1) : 0;
        if (sel < top) top = sel;
        if (sel >= top + list_h) top = sel - list_h + 1;

        if (dirty) {
            dirty = 0;
            /* Draw Norton Commander single-pane box with single-line borders */
            dos_vid_box(g_vid, 1, panel_x, 20, panel_w, bdr_at, file_at, 0);
            dos_vid_title(g_vid, 1, panel_x + 2, panel_w - 4, bdr_at,
                          title ? title : "Select File");
            if (!getcwd(cur_dir, sizeof(cur_dir))) strcpy(cur_dir, ".");
            char hdr[66];
            sprintf(hdr, " %-60.60s ", cur_dir);
            dos_vid_putc(g_vid, 2, panel_x, bdr_at, OYK_BOX_V);
            vid_puts(2, panel_x + 1, dos_vid_attr(g_vid, 0x1BU, 0x07U), hdr, panel_w - 2);
            dos_vid_putc(g_vid, 2, panel_x + panel_w - 1, bdr_at, OYK_BOX_V);
            dos_vid_hdiv(g_vid, 3, panel_x, panel_w, bdr_at);

            /* Row 4: Dark-grey column header row */
            dos_vid_putc(g_vid, 4, panel_x, bdr_at, OYK_BOX_V);
            vid_puts(4, panel_x + 1, hdr_at, " Name                                            Size     ", panel_w - 2);
            dos_vid_putc(g_vid, 4, panel_x + panel_w - 1, bdr_at, OYK_BOX_V);

            for (int i = 0; i < list_h; ++i) {
                int idx = top + i;
                dos_vid_putc(g_vid, list_y + i, panel_x, bdr_at, OYK_BOX_V);
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
                    sprintf(row, " %-47.47s %11s ", e.name, sz_s);
                    oyk_u8 attr = (idx == sel) ? sel_at : (e.is_dir ? dir_at : file_at);
                    vid_puts(list_y + i, panel_x + 1, attr, row, panel_w - 2);
                } else {
                    vid_puts(list_y + i, panel_x + 1, file_at, "", panel_w - 2);
                }
                dos_vid_putc(g_vid, list_y + i, panel_x + panel_w - 1, bdr_at, OYK_BOX_V);
            }
            vid_puts(21, panel_x, dos_vid_attr(g_vid, 0x30U, 0x70U),
                     " Up/Dn:Move  Enter:Open/Pick  Bksp:Up  Esc:Cancel              ", panel_w);
            dos_vid_flush(g_vid);
        }

        if (!kbhit()) continue;
        int ch = getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 72 && sel > 0) { --sel; dirty = 1; }                              /* Up */
            else if (ext == 80 && sel + 1 < count) { ++sel; dirty = 1; }                 /* Down */
            else if (ext == 73) { sel = (sel > list_h) ? (sel - list_h) : 0; dirty = 1; }/* PgUp */
            else if (ext == 81) { sel = (sel + list_h < count) ? (sel + list_h) : (count > 0 ? count - 1 : 0); dirty = 1; } /* PgDn */
            else if (ext == 71) { sel = 0; dirty = 1; }                                  /* Home */
            else if (ext == 79 && count > 0) { sel = count - 1; dirty = 1; }             /* End */
        } else if (ch == 27) {
            break;
        } else if (ch == 8) {
            if (chdir("..") == 0) {
                count = dos_fs_load(entries, DOS_FS_MAX_ENTRIES);
                sel = 0;
                top = 0;
                dirty = 1;
            }
        } else if ((ch == '\r' || ch == '\n') && count > 0) {
            const DosFsEntry& e = entries[sel];
            if (e.is_dir) {
                if (chdir(e.name) == 0) {
                    count = dos_fs_load(entries, DOS_FS_MAX_ENTRIES);
                    sel = 0;
                    top = 0;
                    dirty = 1;
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
    oyk_u8 bdr_at  = dos_vid_attr(g_vid, 0x03U, 0x07U);
    oyk_u8 ttl_at  = dos_vid_attr(g_vid, 0x0BU, 0x0FU);
    oyk_u8 top_hdr = dos_vid_attr(g_vid, 0x1FU, 0x70U);
    oyk_u8 bar_at  = dos_vid_attr(g_vid, 0x30U, 0x70U);

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
    vid_puts(0, 0, top_hdr, hdr, 80);

    dos_vid_box(g_vid, 1, 0, 21, 80, bdr_at, 0x07U, 0);
    dos_vid_title(g_vid, 1, 2, 76, ttl_at, "Terminal Output");

    int body_h = 19;
    int start = (g_dterm_line_count > body_h) ? (g_dterm_line_count - body_h) : 0;
    for (int r = 0; r < body_h; ++r) {
        int idx = start + r;
        dos_vid_putc(g_vid, 2 + r, 0, bdr_at, OYK_BOX_V);
        vid_puts(2 + r, 1, 0x07U, (idx < g_dterm_line_count) ? g_dterm_lines[idx] : "", 78);
        dos_vid_putc(g_vid, 2 + r, 79, bdr_at, OYK_BOX_V);
    }

    char bar[82];
    if (g_dterm_yapp.state != OYK_YAPP_IDLE && g_dterm_yapp.status[0]) {
        sprintf(bar, " %.56s (%lu/%lu B)", g_dterm_yapp.status,
                static_cast<unsigned long>(g_dterm_yapp.bytes_transferred),
                static_cast<unsigned long>(g_dterm_yapp.total_bytes));
    } else {
        strcpy(bar, " F2:Connect  F3:YAPP Upload  F4:Disconnect  F6:Abort YAPP  F9/Esc:Console");
    }
    vid_puts(22, 0, bar_at, bar, 80);

    char in_row[82];
    sprintf(in_row, "> %.77s", input_buf);
    vid_puts(23, 0, 0x0FU, in_row, 80);
    vid_puts(24, 0, dos_vid_attr(g_vid, 0x0EU, 0x07U),
             "F9/Esc switches to Console without closing active terminal session.", 80);
    dos_vid_flush(g_vid);
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

    g_dterm_dirty = 1;
    oyk_u32 last_yapp_bytes = g_dterm_yapp.bytes_transferred;
    int prev_yapp_state = g_dterm_yapp.state;

    while (!g_break_flag) {
        dos_pump_network();
        g_node.tick();

        if (g_dterm_yapp.bytes_transferred != last_yapp_bytes ||
            g_dterm_yapp.state != prev_yapp_state) {
            last_yapp_bytes = g_dterm_yapp.bytes_transferred;
            prev_yapp_state = g_dterm_yapp.state;
            g_dterm_dirty = 1;
        }
        if (g_dterm_dirty) {
            g_dterm_dirty = 0;
            render_dos_terminal(g_dterm_in_buf);
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
            g_dterm_dirty = 1;
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
            g_dterm_dirty = 1;
        } else if ((ch == 8 || ch == 127) && g_dterm_in_len > 0) {
            g_dterm_in_buf[--g_dterm_in_len] = '\0';
            g_dterm_dirty = 1;
        } else if (ch >= 32 && ch < 127 && g_dterm_in_len < 76) {
            g_dterm_in_buf[g_dterm_in_len++] = static_cast<char>(ch);
            g_dterm_in_buf[g_dterm_in_len] = '\0';
            g_dterm_dirty = 1;
        }
    }

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
    int dirty = 1;
    oyk_u8 bdr_at = dos_vid_attr(g_vid, 0x1FU, 0x0FU);
    oyk_u8 hdr_at = dos_vid_list_hdr_attr(g_vid);
    oyk_u8 tab_bg = dos_vid_attr(g_vid, 0x70U, 0x07U);
    oyk_u8 tab_hi = dos_vid_attr(g_vid, 0x1FU, 0x70U);
    oyk_u8 row_at = dos_vid_attr(g_vid, 0x1FU, 0x07U);
    oyk_u8 sel_at = dos_vid_attr(g_vid, 0x30U, 0x70U);

    while (!g_break_flag) {
        dos_pump_network();
        g_node.tick();

        int count = (tab == 0) ? inc_count : app_count;
        if (sel[tab] >= count) sel[tab] = (count > 0) ? (count - 1) : 0;
        if (sel[tab] < top[tab]) top[tab] = sel[tab];
        if (sel[tab] >= top[tab] + list_h) top[tab] = sel[tab] - list_h + 1;

        if (dirty) {
            dirty = 0;
            dos_vid_box(g_vid, 2, panel_x, 17, panel_w, bdr_at, row_at, 0);
            dos_vid_title(g_vid, 2, panel_x + 2, panel_w - 4, bdr_at, "Manage BBS Files Area");

            vid_puts(3, panel_x + 1, tab_bg, "", panel_w - 2);
            dos_vid_putc(g_vid, 3, panel_x, bdr_at, OYK_BOX_V);
            vid_puts(3, panel_x + 2, (tab == 0) ? tab_hi : tab_bg, " 1:Pending Incoming ", 20);
            vid_puts(3, panel_x + 23, (tab == 1) ? tab_hi : tab_bg, " 2:Approved Files ", 18);
            dos_vid_putc(g_vid, 3, panel_x + panel_w - 1, bdr_at, OYK_BOX_V);

            dos_vid_putc(g_vid, 4, panel_x, bdr_at, OYK_BOX_V);
            if (tab == 0) {
                vid_puts(4, panel_x + 1, hdr_at,
                         " Uploader      Filename                                     Size      ", panel_w - 2);
            } else {
                vid_puts(4, panel_x + 1, hdr_at,
                         " Approved File Path                                         Size      ", panel_w - 2);
            }
            dos_vid_putc(g_vid, 4, panel_x + panel_w - 1, bdr_at, OYK_BOX_V);
            dos_vid_hdiv(g_vid, 5, panel_x, panel_w, bdr_at);

            for (int i = 0; i < list_h; ++i) {
                int idx = top[tab] + i;
                dos_vid_putc(g_vid, list_y + i, panel_x, bdr_at, OYK_BOX_V);
                if (count == 0 && i == 1) {
                    vid_puts(list_y + i, panel_x + 1, row_at,
                             (tab == 0) ? " (No pending files in incoming\\<CALLSIGN-SSID>\\)"
                                        : " (No approved files in BBS files area)",
                             panel_w - 2);
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
                    vid_puts(list_y + i, panel_x + 1, (idx == sel[tab]) ? sel_at : row_at, row, panel_w - 2);
                } else {
                    vid_puts(list_y + i, panel_x + 1, row_at, "", panel_w - 2);
                }
                dos_vid_putc(g_vid, list_y + i, panel_x + panel_w - 1, bdr_at, OYK_BOX_V);
            }
            vid_puts(list_y + list_h + 1, panel_x, dos_vid_attr(g_vid, 0x30U, 0x70U),
                     (tab == 0)
                         ? " Tab/1-2:Tabs  Enter/A:Approve  D:Delete  R:Refresh  Esc:Close        "
                         : " Tab/1-2:Tabs  D:Remove Approved File  R:Refresh  Esc:Close           ",
                     panel_w);
            vid_puts(24, 0, dos_vid_attr(g_vid, 0x0EU, 0x0FU), feedback, 80);
            dos_vid_flush(g_vid);
        }

        if (!kbhit()) continue;
        dirty = 1;
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
    int avail = 80 - pcol;
    int start = (len > avail) ? (len - avail) : 0;
    vid_puts(24, pcol, 0x4F, buf + start, avail);
    dos_vid_flush_row(g_vid, 24);
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
        start = (len > avail) ? (len - avail) : 0;
        vid_puts(24, pcol, 0x4F, buf + start, avail);
        dos_vid_flush_row(g_vid, 24);
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

    int dirty = 1;
    int prev_rf = g_rf_reply_flag;
    oyk_u8 top_at  = dos_vid_attr(g_vid, 0x30U, 0x70U);
    oyk_u8 bdr_at  = dos_vid_attr(g_vid, 0x1BU, 0x07U);
    oyk_u8 fill_at = dos_vid_attr(g_vid, 0x17U, 0x07U);
    oyk_u8 val_at  = dos_vid_attr(g_vid, 0x1FU, 0x0FU);
    oyk_u8 hdr_at  = dos_vid_attr(g_vid, 0x1EU, 0x0FU);

    while (!g_break_flag) {
        dos_pump_network();
        g_node.tick();

        if (g_rf_reply_flag != prev_rf) {
            prev_rf = g_rf_reply_flag;
            dirty = 1;
        }
        if (dirty) {
            dirty = 0;
            vid_clear(fill_at);
            vid_puts(0, 0, top_at, " OYK RADIO / RIG CONTROL (MS-DOS)                              Esc:Close", 80);

            dos_vid_box(g_vid, 2, 0, 19, 80, bdr_at, fill_at, 0);
            dos_vid_title(g_vid, 2, 2, 76, val_at, "Radio / Rig Control");

            char row[82];
            sprintf(row, " Protocol : %-14s  Endpoint: %s:%u  [%s]",
                    oyk_rig_protocol_name(proto),
                    cfg.rig_host[0] ? cfg.rig_host : "127.0.0.1",
                    static_cast<unsigned int>(cfg.rig_port),
                    cfg.rig_enabled ? (g_rig_status.reachable ? "ONLINE" : "NO REPLY") : "DISABLED");
            vid_puts(3, 1, val_at, row, 78);
            dos_vid_hdiv(g_vid, 4, 0, 80, bdr_at);

            if (proto == OYK_RIG_PROTO_RADIOFOUNDRY) {
                sprintf(row, " Zone / Pos   : Zone %d, Channel Pos %d (0-based)", g_rig_status.zone, g_rig_status.pos);
                vid_puts(5, 1, val_at, row, 78);
                sprintf(row, " Channel Name : %s", g_rig_status.channel_name[0] ? g_rig_status.channel_name : "(unknown)");
                vid_puts(6, 1, val_at, row, 78);
                sprintf(row, " Frequency    : %lu Hz", static_cast<unsigned long>(g_rig_status.freq_hz));
                vid_puts(7, 1, val_at, row, 78);
                if (g_rig_status.is_dmr) {
                    sprintf(row, " DMR Params   : CC %d  TS %d  TG %ld",
                            g_rig_status.color_code, g_rig_status.timeslot, g_rig_status.talkgroup);
                } else {
                    strcpy(row, " DMR Params   : (analog / not reported)");
                }
                vid_puts(8, 1, val_at, row, 78);
                sprintf(row, " Raw Status   : %.60s", g_rig_status.raw_status[0] ? g_rig_status.raw_status : "(none)");
                vid_puts(10, 1, fill_at, row, 78);
                dos_vid_hdiv(g_vid, 12, 0, 80, bdr_at);
                dos_vid_title(g_vid, 12, 2, 76, hdr_at, "Commands (RadioFoundry / takRF UDP 4191)");
                vid_puts(14, 1, fill_at, "  [Q/R] Query (select)    [Z] Select Zone & Pos   [N] Stop Notify", 78);
                vid_puts(15, 1, fill_at, "  [+/-] Step Channel      [[/]] Step Zone         [Esc] Close", 78);
            } else {
                sprintf(row, " Frequency    : %lu Hz", static_cast<unsigned long>(g_rig_status.freq_hz));
                vid_puts(5, 1, val_at, row, 78);
                sprintf(row, " Mode / PB    : %s (%lu Hz)",
                        g_rig_status.mode[0] ? g_rig_status.mode : "(unknown)",
                        static_cast<unsigned long>(g_rig_status.passband_hz));
                vid_puts(6, 1, val_at, row, 78);
                sprintf(row, " PTT / VFO    : %s  |  VFO: %s",
                        g_rig_status.ptt > 0 ? "TX" : (g_rig_status.ptt == 0 ? "RX" : "UNKNOWN"),
                        g_rig_status.vfo[0] ? g_rig_status.vfo : "(unknown)");
                vid_puts(7, 1, val_at, row, 78);
                dos_vid_hdiv(g_vid, 12, 0, 80, bdr_at);
                dos_vid_title(g_vid, 12, 2, 76, hdr_at, "Commands (Hamlib rigctld TCP 4532)");
                vid_puts(14, 1, fill_at, "  [Q/R] Query Rig         [F] Set Frequency (Hz)  [M] Set Mode", 78);
                vid_puts(15, 1, fill_at, "  [P]   Toggle PTT RX/TX  [V] Set VFO             [Esc] Close", 78);
            }

            vid_puts(23, 0, dos_vid_attr(g_vid, 0x0EU, 0x0FU), fb, 80);
            vid_puts(24, 0, dos_vid_attr(g_vid, 0x70U, 0x70U),
                     " Q/R:Refresh  Esc:Return to Operator Console                                    ", 80);
            dos_vid_flush(g_vid);
        }

        if (!kbhit()) continue;
        dirty = 1;
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

struct DosAddWizState {
    int step;
    int drv;
    int last_drv;
    int focus;
    int btn; /* 0=Back, 1=Next/Finish, 2=Cancel */
    char id[16];
    char desc[32];
    char ep[32];
    char port[12];
    char mask[16];
    char peer[32];
    int allow_in;
    int allow_out;
    int allow_same;
    oyk_u8 lpol;
    char err[64];
};

static const char* const DOS_IFACE_TYPES[5] = {
    "axudp", "kiss-serial", "usb-cdc-serial", "usb-cdc-net", "telnet"
};

static int dos_strieq(const char* a, const char* b) {
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? static_cast<char>(*a - 32) : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? static_cast<char>(*b - 32) : *b;
        if (ca != cb) return 0;
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

static void dos_iface_apply_driver_defaults(int drv,
                                            char* ep,
                                            char* port,
                                            char* mask,
                                            char* peer,
                                            int* allow_in,
                                            int* allow_out,
                                            int* allow_same,
                                            oyk_u8* lpol) {
    const char* t = DOS_IFACE_TYPES[drv];
    mask[0] = '\0';
    peer[0] = '\0';
    *allow_in = 1;
    *allow_out = (drv == 4) ? 0 : 1;
    *allow_same = 0;
    *lpol = OYK_LEARN_INHERIT;
    if (strcmp(t, "kiss-serial") == 0) {
        strcpy(ep, "COM1");
        strcpy(port, "9600");
    } else if (strcmp(t, "usb-cdc-serial") == 0) {
        strcpy(ep, "USBCOM1");
        strcpy(port, "115200");
    } else if (strcmp(t, "telnet") == 0) {
        strcpy(ep, "0.0.0.0");
        strcpy(port, "8010");
    } else {
        strcpy(ep, "0.0.0.0");
        strcpy(port, "10093");
    }
}

static oyk_u8 dos_cycle_lpol(oyk_u8 cur, int dir) {
    int v = (static_cast<int>(cur) + dir + 5) % 5;
    return static_cast<oyk_u8>(v);
}

static void dos_build_iface_config(int drv,
                                   const char* id,
                                   const char* desc,
                                   const char* ep,
                                   const char* port,
                                   const char* mask,
                                   const char* peer,
                                   int allow_in,
                                   int allow_out,
                                   int allow_same,
                                   oyk_u8 lpol,
                                   const OykIfaceConfig* prev_ic,
                                   OykIfaceConfig* out_ic) {
    memset(out_ic, 0, sizeof(*out_ic));
    strncpy(out_ic->id, id, sizeof(out_ic->id) - 1);
    strncpy(out_ic->type, DOS_IFACE_TYPES[drv], sizeof(out_ic->type) - 1);
    strncpy(out_ic->description, desc, sizeof(out_ic->description) - 1);
    out_ic->block_inbound = allow_in ? 0 : 1;
    out_ic->block_outbound = (drv == 4 || !allow_out) ? 1 : 0;
    out_ic->allow_same_interface = (drv != 4 && allow_same) ? 1 : 0;
    out_ic->learned_peer_policy = (drv == 4) ? OYK_LEARN_INHERIT : lpol;
    if (drv == 1 || drv == 2) {
        strncpy(out_ic->device, ep, sizeof(out_ic->device) - 1);
        long bv = atol(port);
        out_ic->baud = (bv >= 300L) ? static_cast<oyk_u32>(bv) : ((drv == 2) ? 115200UL : 9600UL);
    } else {
        strncpy(out_ic->host, ep, sizeof(out_ic->host) - 1);
        if ((drv == 0 || drv == 3) && mask && mask[0]) {
            strncpy(out_ic->netmask, mask, sizeof(out_ic->netmask) - 1);
        }
        long pv = atol(port);
        out_ic->port = (pv > 0L && pv <= 65535L)
                           ? static_cast<oyk_u16>(pv)
                           : ((drv == 4) ? 8010U : 10093U);
        if (drv == 0) {
            if (prev_ic && dos_strieq(prev_ic->type, "axudp")) {
                for (int p = 1; p < prev_ic->peer_count && out_ic->peer_count < OYK_MAX_PEERS; ++p) {
                    out_ic->peers[out_ic->peer_count++] = prev_ic->peers[p];
                }
            }
            if (peer[0]) {
                OykPeerEntry pe;
                memset(&pe, 0, sizeof(pe));
                if (oyk_parse_peer_spec(peer, pe.callsign, sizeof(pe.callsign),
                                        &pe.ip, &pe.port, out_ic->port ? out_ic->port : 10093U)) {
                    if (out_ic->peer_count > 0) {
                        for (int p = (out_ic->peer_count < OYK_MAX_PEERS ? out_ic->peer_count : OYK_MAX_PEERS - 1); p > 0; --p) {
                            out_ic->peers[p] = out_ic->peers[p - 1];
                        }
                        out_ic->peers[0] = pe;
                        if (out_ic->peer_count < OYK_MAX_PEERS) out_ic->peer_count++;
                    } else {
                        out_ic->peers[0] = pe;
                        out_ic->peer_count = 1;
                    }
                }
            }
        }
    }
}

static int dos_wiz_field_count(const DosAddWizState* st) {
    if (st->step == 1) return 2;
    if (st->step == 2) {
        if (st->drv == 0) return 8;
        if (st->drv == 3) return 7;
        if (st->drv == 4) return 3;
        return 6;
    }
    return 0;
}

static void dos_wiz_apply_defaults(const OykConfig* draft, DosAddWizState* st) {
    oyk_derive_default_iface_id(draft, DOS_IFACE_TYPES[st->drv], st->id, sizeof(st->id));
    st->desc[0] = '\0';
    dos_iface_apply_driver_defaults(st->drv, st->ep, st->port, st->mask, st->peer,
                                    &st->allow_in, &st->allow_out, &st->allow_same, &st->lpol);
    st->last_drv = st->drv;
}

static int dos_wiz_try_advance(const OykConfig* draft, DosAddWizState* st, OykIfaceConfig* out_ic) {
    st->err[0] = '\0';
    if (st->step == 0) {
        if (st->drv != st->last_drv) {
            dos_wiz_apply_defaults(draft, st);
        }
        st->step = 1;
        st->focus = 0;
        st->btn = 1;
        return 0;
    }
    if (st->step == 1) {
        if (!st->id[0]) {
            strcpy(st->err, "Interface ID cannot be empty.");
            st->focus = 0;
            return 0;
        }
        for (int i = 0; i < draft->iface_count; ++i) {
            if (dos_strieq(draft->ifaces[i].id, st->id)) {
                sprintf(st->err, "Duplicate interface ID: %.24s", st->id);
                st->focus = 0;
                return 0;
            }
        }
        st->step = 2;
        st->focus = 0;
        st->btn = 1;
        return 0;
    }
    if (st->step == 2) {
        if (!st->ep[0]) {
            strcpy(st->err, (st->drv == 1 || st->drv == 2) ? "Serial device cannot be empty." : "Host cannot be empty.");
            st->focus = 0;
            return 0;
        }
        if (!st->port[0]) {
            strcpy(st->err, (st->drv == 1 || st->drv == 2) ? "Baud rate cannot be empty." : "Port cannot be empty.");
            st->focus = 1;
            return 0;
        }
        if ((st->drv == 0 || st->drv == 3) && oyk_is_all_digits(st->ep)) {
            oyk_u32 did = 0;
            if (!oyk_parse_dmr_id(st->ep, &did)) {
                strcpy(st->err, "Invalid DMR ID (allowed 1..16777214).");
                st->focus = 0;
                return 0;
            }
        }
        if ((st->drv == 0 || st->drv == 3) && st->mask[0]) {
            oyk_u32 mip = 0;
            if (!oyk_parse_ipv4(st->mask, &mip)) {
                strcpy(st->err, "Invalid IPv4 netmask (e.g. 255.0.0.0).");
                st->focus = 2;
                return 0;
            }
        }
        st->step = 3;
        st->focus = 0;
        st->btn = 1;
        return 0;
    }
    if (st->step == 3) {
        dos_build_iface_config(st->drv, st->id, st->desc, st->ep, st->port, st->mask, st->peer,
                               st->allow_in, st->allow_out, st->allow_same, st->lpol, NULL, out_ic);
        return 1;
    }
    return 0;
}

static int run_dos_add_iface_wizard(const OykConfig* draft, OykIfaceConfig* out_ic) {
    static const char* DRV_DESCS[5] = {
        "AX.25 over UDP (mTCP / Packet Driver)",
        "KISS TNC over Serial (COM1..COM4)",
        "KISS TNC over USB-CDC Serial",
        "AXUDP over USB-CDC Network",
        "Plaintext Telnet User Port"
    };
    static const char* PROG[4] = {
        "[##......] 1/4", "[####....] 2/4", "[######..] 3/4", "[########] 4/4"
    };

    const int top = 3, left = 6, h = 19, w = 68;
    oyk_u8 bdr_at  = dos_vid_attr(g_vid, 0x1FU, 0x0FU);
    oyk_u8 fill_at = dos_vid_attr(g_vid, 0x17U, 0x07U);
    oyk_u8 ttl_at  = dos_vid_attr(g_vid, 0x1FU, 0x70U);
    oyk_u8 hdr_at  = dos_vid_list_hdr_attr(g_vid);
    oyk_u8 sel_at  = dos_vid_attr(g_vid, 0x30U, 0x70U);
    oyk_u8 inp_at  = dos_vid_attr(g_vid, 0x70U, 0x70U);
    oyk_u8 err_at  = dos_vid_attr(g_vid, 0x4FU, 0x70U);
    oyk_u8 bar_at  = dos_vid_attr(g_vid, 0x70U, 0x07U);

    DosAddWizState st;
    memset(&st, 0, sizeof(st));
    st.last_drv = -1;
    st.btn = 1;
    st.allow_in = 1;
    st.allow_out = 1;
    int dirty = 1;

    while (!g_break_flag) {
        dos_pump_network();
        g_node.tick();

        if (dirty) {
            dirty = 0;
            dos_vid_box(g_vid, top, left, h, w, bdr_at, fill_at, 1);
            dos_vid_title(g_vid, top, left + 2, 20, ttl_at, "Add Interface");
            dos_vid_title(g_vid, top, left + w - 18, 16, ttl_at, PROG[st.step]);

            const char* prompt = "Select the driver for the new interface:";
            if (st.step == 1) {
                prompt = "Enter a unique interface ID and optional description:";
            } else if (st.step == 2) {
                if (st.drv == 1 || st.drv == 2) prompt = "Configure serial device, baud rate, and policy:";
                else if (st.drv == 4) prompt = "Configure Telnet bind host IP, TCP port, and policy:";
                else if (st.drv == 3) prompt = "Configure USB-CDC host/DMR, port, netmask, and policy:";
                else prompt = "Configure AXUDP bind host/DMR, port, netmask, and policy:";
            } else if (st.step == 3) {
                prompt = "Review interface configuration before committing:";
            }
            vid_puts(top + 1, left + 2, ttl_at, prompt, w - 4);

            if (st.step == 0) {
                vid_puts(top + 2, left + 1, hdr_at,
                         " #  Driver           Description", w - 2);
                for (int i = 0; i < 5; ++i) {
                    char row[68];
                    sprintf(row, " %d. %-15.15s %-44.44s",
                            i + 1, DOS_IFACE_TYPES[i], DRV_DESCS[i]);
                    vid_puts(top + 3 + i, left + 1, (i == st.drv) ? sel_at : fill_at, row, w - 2);
                }
            } else if (st.step == 1) {
                char row[68];
                sprintf(row, "Driver       : %s", DOS_IFACE_TYPES[st.drv]);
                vid_puts(top + 3, left + 3, fill_at, row, w - 6);
                vid_puts(top + 5, left + 3, fill_at, "Interface ID : ", 15);
                vid_puts(top + 5, left + 18, (st.focus == 0) ? sel_at : inp_at, st.id, 36);
                vid_puts(top + 7, left + 3, fill_at, "Description  : ", 15);
                vid_puts(top + 7, left + 18, (st.focus == 1) ? sel_at : inp_at, st.desc, 36);
            } else if (st.step == 2) {
                char row[68], lplbl[24];
                sprintf(row, "Interface    : %s (%s)", st.id, DOS_IFACE_TYPES[st.drv]);
                vid_puts(top + 2, left + 3, fill_at, row, w - 6);
                sprintf(lplbl, "< %s >", oyk_format_learned_policy(st.lpol));
                int r = top + 4;
                vid_puts(r, left + 3, fill_at,
                         (st.drv == 1 || st.drv == 2) ? "Serial Device  : " : "Host / DMR ID  : ", 17);
                vid_puts(r, left + 20, (st.focus == 0) ? sel_at : inp_at, st.ep, 36);
                ++r;
                vid_puts(r, left + 3, fill_at,
                         (st.drv == 1 || st.drv == 2) ? "Baud Rate      : " : ((st.drv == 4) ? "TCP Port       : " : "UDP Port       : "), 17);
                vid_puts(r, left + 20, (st.focus == 1) ? sel_at : inp_at, st.port, 36);
                ++r;
                int fi = 2;
                if (st.drv == 0 || st.drv == 3) {
                    vid_puts(r, left + 3, fill_at, "Netmask (opt)  : ", 17);
                    vid_puts(r, left + 20, (st.focus == fi) ? sel_at : inp_at, st.mask, 36);
                    ++r; ++fi;
                }
                if (st.drv == 0) {
                    vid_puts(r, left + 3, fill_at, "Peer (opt)     : ", 17);
                    vid_puts(r, left + 20, (st.focus == fi) ? sel_at : inp_at, st.peer, 36);
                    ++r; ++fi;
                }
                vid_puts(r, left + 3, fill_at, "Allow Inbound  : ", 17);
                vid_puts(r, left + 20, (st.focus == fi) ? sel_at : inp_at, st.allow_in ? "[ON]" : "[OFF]", 16);
                ++r; ++fi;
                if (st.drv == 4) {
                    vid_puts(r, left + 3, fill_at, "Allow Outbound : [OFF] (disabled for telnet)", 46);
                    ++r;
                    vid_puts(r, left + 3, fill_at, "Same-Iface Fwd : [OFF] (disabled for telnet)", 46);
                } else {
                    vid_puts(r, left + 3, fill_at, "Allow Outbound : ", 17);
                    vid_puts(r, left + 20, (st.focus == fi) ? sel_at : inp_at, st.allow_out ? "[ON]" : "[OFF]", 16);
                    ++r; ++fi;
                    vid_puts(r, left + 3, fill_at, "Same-Iface Fwd : ", 17);
                    vid_puts(r, left + 20, (st.focus == fi) ? sel_at : inp_at, st.allow_same ? "[ON]" : "[OFF]", 16);
                    ++r; ++fi;
                    vid_puts(r, left + 3, fill_at, "Learned Policy : ", 17);
                    vid_puts(r, left + 20, (st.focus == fi) ? sel_at : inp_at, lplbl, 24);
                }
            } else {
                char row[68];
                sprintf(row, "Driver       : %-46.46s", DOS_IFACE_TYPES[st.drv]);
                vid_puts(top + 3, left + 3, fill_at, row, w - 6);
                sprintf(row, "Interface ID : %-46.46s", st.id);
                vid_puts(top + 4, left + 3, fill_at, row, w - 6);
                sprintf(row, "Description  : %-46.46s", st.desc[0] ? st.desc : "(none)");
                vid_puts(top + 5, left + 3, fill_at, row, w - 6);
                sprintf(row, "%s: %-46.46s",
                        (st.drv == 1 || st.drv == 2) ? "Serial Device" : "Host / DMR   ", st.ep);
                vid_puts(top + 6, left + 3, fill_at, row, w - 6);
                sprintf(row, "%s: %-46.46s",
                        (st.drv == 1 || st.drv == 2) ? "Baud Rate    " : "Port         ", st.port);
                vid_puts(top + 7, left + 3, fill_at, row, w - 6);
                if (st.drv == 0 || st.drv == 3) {
                    sprintf(row, "Netmask      : %-46.46s", st.mask[0] ? st.mask : "(any)");
                    vid_puts(top + 8, left + 3, fill_at, row, w - 6);
                }
                if (st.drv == 0) {
                    sprintf(row, "Static Peer  : %-46.46s", st.peer[0] ? st.peer : "(auto-learn)");
                    vid_puts(top + 9, left + 3, fill_at, row, w - 6);
                }
                sprintf(row, "Policy       : In=%s  Out=%s  SameIf=%s  Learn=%s",
                        st.allow_in ? "ON" : "OFF",
                        (st.drv == 4) ? "OFF" : (st.allow_out ? "ON" : "OFF"),
                        (st.drv == 4) ? "OFF" : (st.allow_same ? "ON" : "OFF"),
                        (st.drv == 4) ? "n/a" : oyk_format_learned_policy(st.lpol));
                vid_puts(top + 10, left + 3, fill_at, row, w - 6);
                vid_puts(top + 12, left + 3, ttl_at,
                         "Select Finish to add this interface to Settings draft.", w - 6);
            }

            if (st.err[0]) {
                vid_puts(top + 15, left + 2, err_at, st.err, w - 4);
            }
            dos_vid_hdiv(g_vid, top + 16, left, w, bdr_at);

            int fcnt = dos_wiz_field_count(&st);
            int on_bar = (st.step == 0 || st.step == 3 || st.focus == fcnt);
            if (st.step > 0) {
                vid_puts(top + 17, left + 2, (on_bar && st.btn == 0) ? sel_at : bar_at, "[ < Back ]", 10);
            } else {
                vid_puts(top + 17, left + 2, fill_at, "  < Back  ", 10);
            }
            vid_puts(top + 17, left + 14, (on_bar && st.btn == 1) ? sel_at : bar_at,
                     (st.step == 3) ? "[ Finish ]" : "[ Next > ]", 10);
            vid_puts(top + 17, left + 26, (on_bar && st.btn == 2) ? sel_at : bar_at, "[ Cancel ]", 10);
            vid_puts(top + 17, left + 38, fill_at,
                     (st.step > 0) ? "PgUp/F3/Ctrl+B:Back" : "Up/Dn/1-5:Pick", w - 40);
            dos_vid_flush(g_vid);
        }

        if (!kbhit()) continue;
        dirty = 1;
        int ch = getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 73 || ext == 61) { /* PgUp / F3 -> Back */
                if (st.step > 0) { st.step--; st.focus = 0; st.btn = 1; st.err[0] = '\0'; }
                continue;
            }
            if (st.step == 0) {
                if (ext == 72) st.drv = (st.drv + 4) % 5;
                else if (ext == 80) st.drv = (st.drv + 1) % 5;
                else if (ext == 71) st.drv = 0;
                else if (ext == 79) st.drv = 4;
                else if (ext == 75 || ext == 77) st.btn = (st.btn == 1) ? 2 : 1;
            } else if (st.step == 3) {
                if (ext == 75) st.btn = (st.btn + 2) % 3;
                else if (ext == 77) st.btn = (st.btn + 1) % 3;
            } else {
                int fcnt = dos_wiz_field_count(&st);
                if (ext == 72) st.focus = (st.focus + fcnt) % (fcnt + 1);
                else if (ext == 80) st.focus = (st.focus + 1) % (fcnt + 1);
                else if (ext == 75 || ext == 77) {
                    if (st.focus == fcnt) {
                        if (ext == 75) st.btn = (st.btn + 2) % 3;
                        else st.btn = (st.btn + 1) % 3;
                    } else if (st.step == 2) {
                        int tbase = (st.drv == 0) ? 4 : ((st.drv == 3) ? 3 : 2);
                        int rel = st.focus - tbase;
                        if (rel == 0) st.allow_in = !st.allow_in;
                        else if (rel == 1 && st.drv != 4) st.allow_out = !st.allow_out;
                        else if (rel == 2 && st.drv != 4) st.allow_same = !st.allow_same;
                        else if (rel == 3 && st.drv != 4) st.lpol = dos_cycle_lpol(st.lpol, (ext == 75) ? -1 : 1);
                    }
                }
            }
            continue;
        }

        if (ch == 27) return 0;
        if (ch == 2 && st.step > 0) { /* Ctrl+B -> Back */
            st.step--; st.focus = 0; st.btn = 1; st.err[0] = '\0';
            continue;
        }

        if (st.step == 0) {
            if (ch >= '1' && ch <= '5') st.drv = ch - '1';
            else if (ch == '\t') st.btn = (st.btn == 1) ? 2 : 1;
            else if (ch == '\r' || ch == '\n') {
                if (st.btn == 2) return 0;
                if (dos_wiz_try_advance(draft, &st, out_ic)) return 1;
            }
            continue;
        }

        if (st.step == 3) {
            if (ch == '\t') st.btn = (st.btn + 1) % 3;
            else if (ch == 'b' || ch == 'B') {
                st.step = 2; st.focus = 0; st.btn = 1; st.err[0] = '\0';
            } else if (ch == '\r' || ch == '\n' || ch == ' ') {
                if (st.btn == 0) { st.step = 2; st.focus = 0; st.btn = 1; st.err[0] = '\0'; }
                else if (st.btn == 2) return 0;
                else if (dos_wiz_try_advance(draft, &st, out_ic)) return 1;
            }
            continue;
        }

        int fcnt = dos_wiz_field_count(&st);
        if (ch == '\t') {
            st.focus = (st.focus + 1) % (fcnt + 1);
            continue;
        }
        if (st.focus == fcnt) {
            if (ch == '\r' || ch == '\n' || ch == ' ') {
                if (st.btn == 0) { st.step--; st.focus = 0; st.btn = 1; st.err[0] = '\0'; }
                else if (st.btn == 2) return 0;
                else if (dos_wiz_try_advance(draft, &st, out_ic)) return 1;
            }
            continue;
        }

        if (st.step == 2) {
            int tbase = (st.drv == 0) ? 4 : ((st.drv == 3) ? 3 : 2);
            if (st.focus >= tbase) {
                if (ch == ' ') {
                    int rel = st.focus - tbase;
                    if (rel == 0) st.allow_in = !st.allow_in;
                    else if (rel == 1 && st.drv != 4) st.allow_out = !st.allow_out;
                    else if (rel == 2 && st.drv != 4) st.allow_same = !st.allow_same;
                    else if (rel == 3 && st.drv != 4) st.lpol = dos_cycle_lpol(st.lpol, 1);
                } else if (ch == '\r' || ch == '\n') {
                    if (dos_wiz_try_advance(draft, &st, out_ic)) return 1;
                }
                continue;
            }
        }

        char* target = NULL;
        size_t cap = 0;
        if (st.step == 1) {
            if (st.focus == 0) { target = st.id; cap = sizeof(st.id); }
            else { target = st.desc; cap = sizeof(st.desc); }
        } else {
            if (st.focus == 0) { target = st.ep; cap = sizeof(st.ep); }
            else if (st.focus == 1) { target = st.port; cap = sizeof(st.port); }
            else if ((st.drv == 0 || st.drv == 3) && st.focus == 2) { target = st.mask; cap = sizeof(st.mask); }
            else if (st.drv == 0 && st.focus == 3) { target = st.peer; cap = sizeof(st.peer); }
        }

        if (ch == '\r' || ch == '\n') {
            if (dos_wiz_try_advance(draft, &st, out_ic)) return 1;
        } else if (target && (ch == 8 || ch == 127)) {
            size_t n = strlen(target);
            if (n > 0) target[n - 1] = '\0';
            st.err[0] = '\0';
        } else if (target && ch >= 32 && ch < 127) {
            size_t n = strlen(target);
            if (n + 1 < cap) {
                target[n] = static_cast<char>(ch);
                target[n + 1] = '\0';
            }
            st.err[0] = '\0';
        }
    }
    return 0;
}

struct DosEditIfaceState {
    int drv;
    int focus;
    int btn; /* 0=Save, 1=Cancel */
    char id[16];
    char desc[32];
    char ep[32];
    char port[12];
    char mask[16];
    char peer[32];
    int allow_in;
    int allow_out;
    int allow_same;
    oyk_u8 lpol;
    char err[64];
};

static int dos_edit_field_count(int drv) {
    if (drv == 0) return 10;
    if (drv == 3) return 9;
    if (drv == 4) return 6;
    return 8;
}

static int dos_edit_try_save(const OykConfig* draft,
                             int edit_idx,
                             DosEditIfaceState* st,
                             OykIfaceConfig* out_ic) {
    st->err[0] = '\0';
    if (!st->id[0]) {
        strcpy(st->err, "Interface ID cannot be empty.");
        st->focus = 1;
        return 0;
    }
    for (int i = 0; i < draft->iface_count; ++i) {
        if (i == edit_idx) continue;
        if (dos_strieq(draft->ifaces[i].id, st->id)) {
            sprintf(st->err, "Duplicate interface ID: %.24s", st->id);
            st->focus = 1;
            return 0;
        }
    }
    if (!st->ep[0]) {
        strcpy(st->err, (st->drv == 1 || st->drv == 2) ? "Serial device cannot be empty." : "Host cannot be empty.");
        st->focus = 3;
        return 0;
    }
    if (!st->port[0]) {
        strcpy(st->err, (st->drv == 1 || st->drv == 2) ? "Baud rate cannot be empty." : "Port cannot be empty.");
        st->focus = 4;
        return 0;
    }
    if ((st->drv == 0 || st->drv == 3) && oyk_is_all_digits(st->ep)) {
        oyk_u32 did = 0;
        if (!oyk_parse_dmr_id(st->ep, &did)) {
            strcpy(st->err, "Invalid DMR ID (allowed 1..16777214).");
            st->focus = 3;
            return 0;
        }
    }
    if ((st->drv == 0 || st->drv == 3) && st->mask[0]) {
        oyk_u32 mip = 0;
        if (!oyk_parse_ipv4(st->mask, &mip)) {
            strcpy(st->err, "Invalid IPv4 netmask (e.g. 255.0.0.0).");
            st->focus = 5;
            return 0;
        }
    }
    dos_build_iface_config(st->drv, st->id, st->desc, st->ep, st->port, st->mask, st->peer,
                           st->allow_in, st->allow_out, st->allow_same, st->lpol, &draft->ifaces[edit_idx], out_ic);
    return 1;
}

static int run_dos_edit_iface_dialog(const OykConfig* draft, int edit_idx, OykIfaceConfig* out_ic) {
    if (edit_idx < 0 || edit_idx >= draft->iface_count) return 0;

    const int top = 3, left = 6, h = 19, w = 68;
    oyk_u8 bdr_at  = dos_vid_attr(g_vid, 0x1FU, 0x0FU);
    oyk_u8 fill_at = dos_vid_attr(g_vid, 0x17U, 0x07U);
    oyk_u8 ttl_at  = dos_vid_attr(g_vid, 0x1FU, 0x70U);
    oyk_u8 sel_at  = dos_vid_attr(g_vid, 0x30U, 0x70U);
    oyk_u8 inp_at  = dos_vid_attr(g_vid, 0x70U, 0x70U);
    oyk_u8 err_at  = dos_vid_attr(g_vid, 0x4FU, 0x70U);
    oyk_u8 bar_at  = dos_vid_attr(g_vid, 0x70U, 0x07U);

    DosEditIfaceState st;
    memset(&st, 0, sizeof(st));
    const OykIfaceConfig& ic = draft->ifaces[edit_idx];
    for (int i = 0; i < 5; ++i) {
        if (dos_strieq(ic.type, DOS_IFACE_TYPES[i])) { st.drv = i; break; }
    }
    strncpy(st.id, ic.id, sizeof(st.id) - 1);
    strncpy(st.desc, ic.description, sizeof(st.desc) - 1);
    st.allow_in = ic.block_inbound ? 0 : 1;
    st.allow_out = (st.drv == 4 || ic.block_outbound) ? 0 : 1;
    st.allow_same = (st.drv != 4 && ic.allow_same_interface) ? 1 : 0;
    st.lpol = ic.learned_peer_policy;
    if (st.drv == 1 || st.drv == 2) {
        strncpy(st.ep, ic.device[0] ? ic.device : ((st.drv == 2) ? "USBCOM1" : "COM1"), sizeof(st.ep) - 1);
        sprintf(st.port, "%lu", static_cast<unsigned long>(ic.baud ? ic.baud : ((st.drv == 2) ? 115200UL : 9600UL)));
    } else {
        strncpy(st.ep, ic.host[0] ? ic.host : "0.0.0.0", sizeof(st.ep) - 1);
        sprintf(st.port, "%u", static_cast<unsigned int>(ic.port ? ic.port : ((st.drv == 4) ? 8010U : 10093U)));
        strncpy(st.mask, ic.netmask, sizeof(st.mask) - 1);
        if (st.drv == 0 && ic.peer_count > 0) {
            oyk_format_peer_spec(&ic.peers[0], st.peer, sizeof(st.peer));
        }
    }
    int dirty = 1;

    while (!g_break_flag) {
        dos_pump_network();
        g_node.tick();

        int fcnt = dos_edit_field_count(st.drv);
        if (st.focus > fcnt) st.focus = fcnt;

        if (dirty) {
            dirty = 0;
            dos_vid_box(g_vid, top, left, h, w, bdr_at, fill_at, 1);
            dos_vid_title(g_vid, top, left + 2, 20, ttl_at, "Edit Interface");
            vid_puts(top + 1, left + 2, ttl_at, "Edit interface settings and select Save to update draft:", w - 4);

            char drv_lbl[32], lp_lbl[24];
            sprintf(drv_lbl, "< %s >", DOS_IFACE_TYPES[st.drv]);
            sprintf(lp_lbl, "< %s >", oyk_format_learned_policy(st.lpol));
            vid_puts(top + 3, left + 3, fill_at, "Driver (Lt/Rt) : ", 17);
            vid_puts(top + 3, left + 20, (st.focus == 0) ? sel_at : inp_at, drv_lbl, 36);
            vid_puts(top + 4, left + 3, fill_at, "Interface ID   : ", 17);
            vid_puts(top + 4, left + 20, (st.focus == 1) ? sel_at : inp_at, st.id, 36);
            vid_puts(top + 5, left + 3, fill_at, "Description    : ", 17);
            vid_puts(top + 5, left + 20, (st.focus == 2) ? sel_at : inp_at, st.desc, 36);
            vid_puts(top + 6, left + 3, fill_at,
                     (st.drv == 1 || st.drv == 2) ? "Serial Device  : " : "Host / DMR ID  : ", 17);
            vid_puts(top + 6, left + 20, (st.focus == 3) ? sel_at : inp_at, st.ep, 36);
            vid_puts(top + 7, left + 3, fill_at,
                     (st.drv == 1 || st.drv == 2) ? "Baud Rate      : " : ((st.drv == 4) ? "TCP Port       : " : "UDP Port       : "), 17);
            vid_puts(top + 7, left + 20, (st.focus == 4) ? sel_at : inp_at, st.port, 36);
            int r = top + 8;
            int fi = 5;
            if (st.drv == 0 || st.drv == 3) {
                vid_puts(r, left + 3, fill_at, "Netmask (opt)  : ", 17);
                vid_puts(r, left + 20, (st.focus == fi) ? sel_at : inp_at, st.mask, 36);
                ++r; ++fi;
            }
            if (st.drv == 0) {
                vid_puts(r, left + 3, fill_at, "Static Peer    : ", 17);
                vid_puts(r, left + 20, (st.focus == fi) ? sel_at : inp_at, st.peer, 36);
                ++r; ++fi;
            }
            vid_puts(r, left + 3, fill_at, "Allow Inbound  : ", 17);
            vid_puts(r, left + 20, (st.focus == fi) ? sel_at : inp_at, st.allow_in ? "[ON]" : "[OFF]", 16);
            ++r; ++fi;
            if (st.drv == 4) {
                vid_puts(r, left + 3, fill_at, "Allow Outbound : [OFF] (disabled for telnet)", 46);
                ++r;
                vid_puts(r, left + 3, fill_at, "Same-Iface Fwd : [OFF] (disabled for telnet)", 46);
            } else {
                vid_puts(r, left + 3, fill_at, "Allow Outbound : ", 17);
                vid_puts(r, left + 20, (st.focus == fi) ? sel_at : inp_at, st.allow_out ? "[ON]" : "[OFF]", 16);
                ++r; ++fi;
                vid_puts(r, left + 3, fill_at, "Same-Iface Fwd : ", 17);
                vid_puts(r, left + 20, (st.focus == fi) ? sel_at : inp_at, st.allow_same ? "[ON]" : "[OFF]", 16);
                ++r; ++fi;
                vid_puts(r, left + 3, fill_at, "Learned Policy : ", 17);
                vid_puts(r, left + 20, (st.focus == fi) ? sel_at : inp_at, lp_lbl, 24);
            }

            if (st.err[0]) {
                vid_puts(top + 15, left + 2, err_at, st.err, w - 4);
            }
            dos_vid_hdiv(g_vid, top + 16, left, w, bdr_at);

            int on_bar = (st.focus == fcnt);
            vid_puts(top + 17, left + 2,  (on_bar && st.btn == 0) ? sel_at : bar_at, "[  Save  ]", 10);
            vid_puts(top + 17, left + 14, (on_bar && st.btn == 1) ? sel_at : bar_at, "[ Cancel ]", 10);
            vid_puts(top + 17, left + 26, fill_at, "Up/Dn:Field  Enter:Save  Esc:Cancel", w - 28);
            dos_vid_flush(g_vid);
        }

        if (!kbhit()) continue;
        dirty = 1;
        int ch = getch();
        int tbase = (st.drv == 0) ? 7 : ((st.drv == 3) ? 6 : 5);
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 72) st.focus = (st.focus + fcnt) % (fcnt + 1);
            else if (ext == 80) st.focus = (st.focus + 1) % (fcnt + 1);
            else if (ext == 75 || ext == 77) {
                if (st.focus == 0) {
                    st.drv = (ext == 75) ? ((st.drv + 4) % 5) : ((st.drv + 1) % 5);
                    dos_iface_apply_driver_defaults(st.drv, st.ep, st.port, st.mask, st.peer,
                                                    &st.allow_in, &st.allow_out, &st.allow_same, &st.lpol);
                    st.err[0] = '\0';
                } else if (st.focus == fcnt) {
                    st.btn = (st.btn == 0) ? 1 : 0;
                } else if (st.focus >= tbase) {
                    int rel = st.focus - tbase;
                    if (rel == 0) st.allow_in = !st.allow_in;
                    else if (rel == 1 && st.drv != 4) st.allow_out = !st.allow_out;
                    else if (rel == 2 && st.drv != 4) st.allow_same = !st.allow_same;
                    else if (rel == 3 && st.drv != 4) st.lpol = dos_cycle_lpol(st.lpol, (ext == 75) ? -1 : 1);
                }
            }
            continue;
        }

        if (ch == 27) return 0;
        if (ch == '\t') {
            st.focus = (st.focus + 1) % (fcnt + 1);
            continue;
        }
        if (st.focus == fcnt) {
            if (ch == '\r' || ch == '\n' || ch == ' ') {
                if (st.btn == 1) return 0;
                if (dos_edit_try_save(draft, edit_idx, &st, out_ic)) return 1;
            }
            continue;
        }
        if (st.focus == 0) {
            if (ch == ' ') {
                st.drv = (st.drv + 1) % 5;
                dos_iface_apply_driver_defaults(st.drv, st.ep, st.port, st.mask, st.peer,
                                                &st.allow_in, &st.allow_out, &st.allow_same, &st.lpol);
                st.err[0] = '\0';
            } else if (ch == '\r' || ch == '\n') {
                if (dos_edit_try_save(draft, edit_idx, &st, out_ic)) return 1;
            }
            continue;
        }
        if (st.focus >= tbase) {
            if (ch == ' ') {
                int rel = st.focus - tbase;
                if (rel == 0) st.allow_in = !st.allow_in;
                else if (rel == 1 && st.drv != 4) st.allow_out = !st.allow_out;
                else if (rel == 2 && st.drv != 4) st.allow_same = !st.allow_same;
                else if (rel == 3 && st.drv != 4) st.lpol = dos_cycle_lpol(st.lpol, 1);
            } else if (ch == '\r' || ch == '\n') {
                if (dos_edit_try_save(draft, edit_idx, &st, out_ic)) return 1;
            }
            continue;
        }

        char* target = NULL;
        size_t cap = 0;
        if (st.focus == 1) { target = st.id; cap = sizeof(st.id); }
        else if (st.focus == 2) { target = st.desc; cap = sizeof(st.desc); }
        else if (st.focus == 3) { target = st.ep; cap = sizeof(st.ep); }
        else if (st.focus == 4) { target = st.port; cap = sizeof(st.port); }
        else if ((st.drv == 0 || st.drv == 3) && st.focus == 5) { target = st.mask; cap = sizeof(st.mask); }
        else if (st.drv == 0 && st.focus == 6) { target = st.peer; cap = sizeof(st.peer); }

        if (ch == '\r' || ch == '\n') {
            if (dos_edit_try_save(draft, edit_idx, &st, out_ic)) return 1;
        } else if (target && (ch == 8 || ch == 127)) {
            size_t n = strlen(target);
            if (n > 0) target[n - 1] = '\0';
            st.err[0] = '\0';
        } else if (target && ch >= 32 && ch < 127) {
            size_t n = strlen(target);
            if (n + 1 < cap) {
                target[n] = static_cast<char>(ch);
                target[n + 1] = '\0';
            }
            st.err[0] = '\0';
        }
    }
    return 0;
}

struct DosEditPeerState {
    char id[20];
    char callsign[16];
    char desc[48];
    char iface[16];
    char host[48];
    char port[8];
    int allow_in;
    int allow_out;
    int id_custom;
    char err[68];
};

static int dos_edit_peer_save(const OykConfig* draft,
                              int edit_idx,
                              DosEditPeerState* st,
                              OykPeerConfig* out_p) {
    OykCallsign cs;
    if (!oyk_parse_callsign(st->callsign, &cs)) {
        strcpy(st->err, "Invalid AX.25 callsign.");
        return 0;
    }
    char cfmt[16];
    oyk_format_callsign(&cs, cfmt, sizeof(cfmt));
    if (!st->id[0]) {
        oyk_derive_default_peer_id(draft, cfmt, st->id, sizeof(st->id));
    }
    for (int i = 0; i < draft->peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
        if (i == edit_idx) continue;
        if (stricmp(draft->peers[i].id, st->id) == 0) {
            sprintf(st->err, "Peer ID '%s' already exists.", st->id);
            return 0;
        }
    }
    oyk_u16 pnum = 0;
    if (st->port[0]) {
        long pv = atol(st->port);
        if (pv < 0L || pv > 65535L) {
            strcpy(st->err, "Port must be 0..65535.");
            return 0;
        }
        pnum = static_cast<oyk_u16>(pv);
    }
    if (st->host[0] && oyk_is_all_digits(st->host)) {
        oyk_u32 did = 0;
        if (!oyk_parse_dmr_id(st->host, &did)) {
            strcpy(st->err, "Invalid DMR ID (allowed 1..16777214).");
            return 0;
        }
    }
    memset(out_p, 0, sizeof(*out_p));
    strncpy(out_p->id, st->id, sizeof(out_p->id) - 1);
    strncpy(out_p->callsign, cfmt, sizeof(out_p->callsign) - 1);
    strncpy(out_p->description, st->desc, sizeof(out_p->description) - 1);
    strncpy(out_p->interface_id, st->iface, sizeof(out_p->interface_id) - 1);
    strncpy(out_p->host, st->host, sizeof(out_p->host) - 1);
    out_p->port = pnum;
    out_p->allow_inbound = st->allow_in ? 1 : 0;
    out_p->allow_outbound = st->allow_out ? 1 : 0;
    out_p->auto_learned = 0;
    return 1;
}

static int run_dos_edit_peer_dialog(const OykConfig* draft, int edit_idx, OykPeerConfig* out_p) {
    DosEditPeerState st;
    memset(&st, 0, sizeof(st));
    if (edit_idx >= 0 && edit_idx < draft->peer_count) {
        const OykPeerConfig& src = draft->peers[edit_idx];
        strncpy(st.id, src.id, sizeof(st.id) - 1);
        strncpy(st.callsign, src.callsign, sizeof(st.callsign) - 1);
        strncpy(st.desc, src.description, sizeof(st.desc) - 1);
        strncpy(st.iface, src.interface_id, sizeof(st.iface) - 1);
        strncpy(st.host, src.host, sizeof(st.host) - 1);
        if (src.port > 0) sprintf(st.port, "%u", static_cast<unsigned int>(src.port));
        st.allow_in = src.allow_inbound ? 1 : 0;
        st.allow_out = src.allow_outbound ? 1 : 0;
        st.id_custom = 1;
    } else {
        st.allow_in = 1;
        st.allow_out = 1;
        for (int i = 0; i < draft->iface_count; ++i) {
            if (stricmp(draft->ifaces[i].type, "telnet") != 0 &&
                stricmp(draft->ifaces[i].type, "local") != 0) {
                strncpy(st.iface, draft->ifaces[i].id, sizeof(st.iface) - 1);
                break;
            }
        }
    }

    const char* valid_if[OYK_MAX_INTERFACES];
    int valid_cnt = 0;
    for (int i = 0; i < draft->iface_count && valid_cnt < OYK_MAX_INTERFACES; ++i) {
        if (stricmp(draft->ifaces[i].type, "telnet") != 0 &&
            stricmp(draft->ifaces[i].type, "local") != 0) {
            valid_if[valid_cnt++] = draft->ifaces[i].id;
        }
    }

    static const char* PLABELS[8] = {
        "Peer ID", "Callsign", "Description", "Interface",
        "Host / DMR ID", "Port (0=default)", "Allow Inbound", "Allow Outbound"
    };

    const int top = 4, left = 6, h = 17, w = 68;
    oyk_u8 bdr_at  = dos_vid_attr(g_vid, 0x1FU, 0x0FU);
    oyk_u8 fill_at = dos_vid_attr(g_vid, 0x17U, 0x07U);
    oyk_u8 ttl_at  = dos_vid_attr(g_vid, 0x1FU, 0x70U);
    oyk_u8 sel_at  = dos_vid_attr(g_vid, 0x30U, 0x70U);
    oyk_u8 err_at  = dos_vid_attr(g_vid, 0x4FU, 0x70U);
    oyk_u8 bar_at  = dos_vid_attr(g_vid, 0x70U, 0x07U);

    int focus = (edit_idx >= 0) ? 0 : 1;
    int btn = 0;
    int committed = 0;
    int dirty = 1;

    while (!g_break_flag) {
        dos_pump_network();
        g_node.tick();
        if (dirty) {
            dirty = 0;
            dos_vid_box(g_vid, top, left, h, w, bdr_at, fill_at, 1);
            dos_vid_title(g_vid, top, left + 2, w - 4, ttl_at,
                          (edit_idx >= 0) ? "Edit Peer" : "Add Peer");
            for (int r = 0; r < 8; ++r) {
                char vbuf[50], row[66];
                if (r == 0) strncpy(vbuf, st.id, 46);
                else if (r == 1) strncpy(vbuf, st.callsign, 46);
                else if (r == 2) strncpy(vbuf, st.desc, 46);
                else if (r == 3) sprintf(vbuf, "< %s >", st.iface[0] ? st.iface : "(any)");
                else if (r == 4) strncpy(vbuf, st.host, 46);
                else if (r == 5) strcpy(vbuf, st.port[0] ? st.port : "(default)");
                else if (r == 6) strcpy(vbuf, st.allow_in ? "[ON]" : "[OFF]");
                else strcpy(vbuf, st.allow_out ? "[ON]" : "[OFF]");
                vbuf[46] = '\0';
                sprintf(row, " %-16.16s: %-45.45s", PLABELS[r], vbuf);
                vid_puts(top + 2 + r, left + 2, (focus == r) ? sel_at : fill_at, row, w - 4);
            }
            if (st.err[0]) vid_puts(top + 11, left + 2, err_at, st.err, w - 4);
            vid_puts(top + 13, left + 20, (focus == 8 && btn == 0) ? sel_at : bar_at, "[ Save ]", 8);
            vid_puts(top + 13, left + 36, (focus == 8 && btn == 1) ? sel_at : bar_at, "[ Cancel ]", 10);
            dos_vid_hdiv(g_vid, top + 14, left, w, bdr_at);
            vid_puts(top + 15, left + 2, fill_at,
                     "Up/Dn/Tab:Field  Lt/Rt/Spc:Toggle  Enter:Save  Esc:Cancel", w - 4);
            dos_vid_flush(g_vid);
        }
        if (!kbhit()) continue;
        dirty = 1;
        int ch = getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 72) focus = (focus + 8) % 9;
            else if (ext == 80) focus = (focus + 1) % 9;
            else if (ext == 75 || ext == 77) {
                if (focus == 8) btn = 1 - btn;
                else if (focus == 3 && valid_cnt > 0) {
                    int idx = 0;
                    for (int i = 0; i < valid_cnt; ++i) {
                        if (stricmp(valid_if[i], st.iface) == 0) { idx = i; break; }
                    }
                    idx = (ext == 75) ? (idx + valid_cnt - 1) % valid_cnt : (idx + 1) % valid_cnt;
                    strncpy(st.iface, valid_if[idx], sizeof(st.iface) - 1);
                } else if (focus == 6) st.allow_in = !st.allow_in;
                else if (focus == 7) st.allow_out = !st.allow_out;
            }
            continue;
        }
        if (ch == 27) break;
        if (ch == '\t') { focus = (focus + 1) % 9; continue; }
        if (focus == 8) {
            if (ch == '\r' || ch == '\n' || ch == ' ') {
                if (btn == 1) break;
                if (dos_edit_peer_save(draft, edit_idx, &st, out_p)) { committed = 1; break; }
            }
            continue;
        }
        if (focus == 3 && ch == ' ' && valid_cnt > 0) {
            int idx = 0;
            for (int i = 0; i < valid_cnt; ++i) {
                if (stricmp(valid_if[i], st.iface) == 0) { idx = i; break; }
            }
            idx = (idx + 1) % valid_cnt;
            strncpy(st.iface, valid_if[idx], sizeof(st.iface) - 1);
            continue;
        }
        if ((focus == 6 || focus == 7) && ch == ' ') {
            if (focus == 6) st.allow_in = !st.allow_in;
            else st.allow_out = !st.allow_out;
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            if (dos_edit_peer_save(draft, edit_idx, &st, out_p)) { committed = 1; break; }
            continue;
        }
        char* fld = NULL;
        size_t cap = 0;
        if (focus == 0) { fld = st.id; cap = sizeof(st.id) - 1; }
        else if (focus == 1) { fld = st.callsign; cap = sizeof(st.callsign) - 1; }
        else if (focus == 2) { fld = st.desc; cap = sizeof(st.desc) - 1; }
        else if (focus == 4) { fld = st.host; cap = sizeof(st.host) - 1; }
        else if (focus == 5) { fld = st.port; cap = sizeof(st.port) - 1; }
        if (fld) {
            size_t len = strlen(fld);
            if (ch == '\b' || ch == 127) {
                if (len > 0) fld[len - 1] = '\0';
                if (focus == 0) st.id_custom = (st.id[0] != '\0');
                if (focus == 1 && !st.id_custom) {
                    if (st.callsign[0]) oyk_derive_default_peer_id(draft, st.callsign, st.id, sizeof(st.id));
                    else st.id[0] = '\0';
                }
                st.err[0] = '\0';
            } else if (ch >= 32 && ch < 127 && len < cap) {
                fld[len] = static_cast<char>(ch);
                fld[len + 1] = '\0';
                if (focus == 0) st.id_custom = 1;
                if (focus == 1 && !st.id_custom) {
                    oyk_derive_default_peer_id(draft, st.callsign, st.id, sizeof(st.id));
                }
                st.err[0] = '\0';
            }
        }
    }
    return committed;
}

static void run_dos_settings(int initial_tab) {
    struct DraftGuard {
        OykConfig* p;
        DraftGuard() : p(new OykConfig) {}
        ~DraftGuard() { delete p; }
    } draft_guard;
    if (!draft_guard.p) return;
    OykConfig& draft = *draft_guard.p;
    draft = g_node.config();

    static const char* TAB_NAMES[7] = {
        " 1:Node ", " 2:BBS ", " 3:CHAT ", " 4:Ifaces ", " 5:Peers ", " 6:Rig ", " 7:Printer "
    };
    static const char* NODE_LABELS[9] = {
        "Node Callsign",
        "Node Alias",
        "Welcome Message",
        "Info Message",
        "Allow Incoming (Default)",
        "Learned Peer Policy",
        "New User Welcome",
        "Interfaces File",
        "Peers File"
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
    static const char* PRN_LABELS[6] = {
        "Enable Line Printer",
        "Device (LPT1:/COM1:)",
        "Protocol (generic/mx80/escp2)",
        "Serial Baud Rate",
        "Printer Log Mask (0x..)",
        "Condensed 132-Col Mode"
    };

    int tab = (initial_tab >= 0 && initial_tab < 7) ? initial_tab : 0;
    int sel[7] = { 0, 0, 0, 0, 0, 0, 0 };
    int dirty = 1;
    char fb[78];
    sprintf(fb, "Config: %.68s", draft.source_path[0] ? draft.source_path : "node.cfg");

    oyk_u8 top_at  = dos_vid_attr(g_vid, 0x30U, 0x70U);
    oyk_u8 bdr_at  = dos_vid_attr(g_vid, 0x1BU, 0x07U);
    oyk_u8 fill_at = dos_vid_attr(g_vid, 0x17U, 0x07U);
    oyk_u8 tab_bg  = dos_vid_attr(g_vid, 0x70U, 0x07U);
    oyk_u8 tab_hi  = dos_vid_attr(g_vid, 0x1FU, 0x70U);
    oyk_u8 sel_at  = dos_vid_attr(g_vid, 0x30U, 0x70U);
    oyk_u8 hdr_at  = dos_vid_list_hdr_attr(g_vid);

    while (!g_break_flag) {
        dos_pump_network();
        g_node.tick();

        int cnt = (tab == 0) ? 9 : ((tab == 1) ? 8 : ((tab == 2) ? 6 : ((tab == 3) ? draft.iface_count : ((tab == 4) ? draft.peer_count : 6))));
        if (cnt <= 0) sel[tab] = 0;
        else if (sel[tab] >= cnt) sel[tab] = cnt - 1;

        if (dirty) {
            dirty = 0;
            vid_clear(fill_at);
            vid_puts(0, 0, top_at, " OYK NODE SETTINGS (MS-DOS)                      F2/S:Save & Apply  Esc:Cancel", 80);

            /* Row 1: Tab bar */
            vid_puts(1, 0, tab_bg, "", 80);
            int tx = 1;
            for (int t = 0; t < 7; ++t) {
                int tlen = static_cast<int>(strlen(TAB_NAMES[t]));
                vid_puts(1, tx, (t == tab) ? tab_hi : tab_bg, TAB_NAMES[t], tlen);
                tx += tlen + 1;
            }

            dos_vid_box(g_vid, 2, 0, 20, 80, bdr_at, fill_at, 0);

            if (tab != 3 && tab != 4) {
                const char** labels = (tab == 0) ? NODE_LABELS : ((tab == 1) ? BBS_LABELS : ((tab == 2) ? CHAT_LABELS : ((tab == 5) ? RIG_LABELS : PRN_LABELS)));
                for (int i = 0; i < cnt; ++i) {
                    char val[56], cs[16], row[82];
                    val[0] = '\0';
                    if (tab == 0) {
                        if (i == 0) oyk_format_callsign(&draft.callsign, val, sizeof(val));
                        else if (i == 1) { if (draft.alias.base[0]) oyk_format_callsign(&draft.alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                        else if (i == 2) { strncpy(val, draft.welcome_text, 46); val[46] = '\0'; }
                        else if (i == 3) { strncpy(val, draft.info_text, 46); val[46] = '\0'; }
                        else if (i == 4) strcpy(val, draft.accept_incoming ? "[ON]" : "[OFF]");
                        else if (i == 5) strcpy(val, oyk_format_learned_policy(draft.learned_peer_policy));
                        else if (i == 6) {
                            if (draft.new_user_welcome[0]) { strncpy(val, draft.new_user_welcome, 46); val[46] = '\0'; }
                            else strcpy(val, "(use default)");
                        } else if (i == 7) {
                            strncpy(val, draft.interfaces_file, 46); val[46] = '\0';
                        } else if (i == 8) {
                            strncpy(val, draft.peers_file, 46); val[46] = '\0';
                        }
                    } else if (tab == 1) {
                        if (i == 0) strcpy(val, draft.bbs_enabled ? "[ON]" : "[OFF]");
                        else if (i == 1) { OykCallsign bc; oyk_config_resolved_bbs_callsign(&draft, &bc); oyk_format_callsign(&bc, cs, sizeof(cs)); sprintf(val, "%u (%s)", static_cast<unsigned int>(draft.bbs_ssid), cs); }
                        else if (i == 2) { if (draft.bbs_callsign.base[0]) oyk_format_callsign(&draft.bbs_callsign, val, sizeof(val)); else strcpy(val, "(auto)"); }
                        else if (i == 3) { if (draft.bbs_alias.base[0]) oyk_format_callsign(&draft.bbs_alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                        else if (i == 4) strcpy(val, draft.bbs_files_enabled ? "[ON]" : "[OFF]");
                        else if (i == 5) { strncpy(val, draft.bbs_files_path, 46); val[46] = '\0'; }
                        else if (i == 6) { strncpy(val, draft.bbs_mail_path, 46); val[46] = '\0'; }
                        else if (i == 7) { strncpy(val, draft.bbs_welcome, 46); val[46] = '\0'; }
                    } else if (tab == 2) {
                        if (i == 0) strcpy(val, draft.chat_enabled ? "[ON]" : "[OFF]");
                        else if (i == 1) { OykCallsign cc; oyk_config_resolved_chat_callsign(&draft, &cc); oyk_format_callsign(&cc, cs, sizeof(cs)); sprintf(val, "%u (%s)", static_cast<unsigned int>(draft.chat_ssid), cs); }
                        else if (i == 2) { if (draft.chat_callsign.base[0]) oyk_format_callsign(&draft.chat_callsign, val, sizeof(val)); else strcpy(val, "(auto)"); }
                        else if (i == 3) { if (draft.chat_alias.base[0]) oyk_format_callsign(&draft.chat_alias, val, sizeof(val)); else strcpy(val, "(none)"); }
                        else if (i == 4) { strncpy(val, draft.chat_topic, 46); val[46] = '\0'; }
                        else if (i == 5) { strncpy(val, draft.chat_welcome, 46); val[46] = '\0'; }
                    } else if (tab == 5) {
                        if (i == 0) strcpy(val, draft.rig_enabled ? "[ON]" : "[OFF]");
                        else if (i == 1) { strncpy(val, draft.rig_protocol[0] ? draft.rig_protocol : "rigctl", 46); val[46] = '\0'; }
                        else if (i == 2) { strncpy(val, draft.rig_host[0] ? draft.rig_host : "127.0.0.1", 46); val[46] = '\0'; }
                        else if (i == 3) sprintf(val, "%u", static_cast<unsigned int>(draft.rig_port));
                        else if (i == 4) sprintf(val, "%u s", static_cast<unsigned int>(draft.rig_poll_sec));
                        else if (i == 5) strcpy(val, draft.rig_notify ? "[ON]" : "[OFF]");
                    } else {
                        if (i == 0) strcpy(val, draft.printer_enabled ? "[ON]" : "[OFF]");
                        else if (i == 1) { strncpy(val, draft.printer_device[0] ? draft.printer_device : "(none)", 46); val[46] = '\0'; }
                        else if (i == 2) { strncpy(val, draft.printer_protocol[0] ? draft.printer_protocol : "generic", 46); val[46] = '\0'; }
                        else if (i == 3) sprintf(val, "%lu", static_cast<unsigned long>(draft.printer_baud ? draft.printer_baud : 9600UL));
                        else if (i == 4) sprintf(val, "0x%lX", static_cast<unsigned long>(draft.printer_level));
                        else if (i == 5) strcpy(val, draft.printer_condensed ? "[ON]" : "[OFF]");
                    }
                    sprintf(row, " %-29.29s: %-46.46s", labels[i], val);
                    vid_puts(3 + i, 1, (i == sel[tab]) ? sel_at : fill_at, row, 78);
                    if (tab == 0 && (i == 7 || i == 8) && val[0] == '\0' && g_vid && g_vid->is_color) {
                        vid_puts(3 + i, 33, (i == sel[tab]) ? sel_at : dos_vid_attr(g_vid, 0x18U, 0x07U), "<inline>", 8);
                    }
                }
            } else if (tab == 3) {
                vid_puts(3, 1, hdr_at, " #  ID          Type         Endpoint / Device             Peers", 78);
                if (draft.iface_count == 0) {
                    vid_puts(4, 1, fill_at, " (no interfaces configured - press A to add)", 78);
                } else {
                    for (int i = 0; i < draft.iface_count && i < 16; ++i) {
                        const OykIfaceConfig& ic = draft.ifaces[i];
                        int pcnt = ic.peer_count;
                        for (int pi = 0; pi < draft.peer_count && pi < OYK_MAX_CONFIG_PEERS; ++pi) {
                            if (stricmp(draft.peers[pi].interface_id, ic.id) == 0) ++pcnt;
                        }
                        char row[82], ep[32];
                        if (strcmp(ic.type, "kiss-serial") == 0 || strcmp(ic.type, "usb-cdc-serial") == 0) {
                            sprintf(ep, "%.14s @%lu", ic.device[0] ? ic.device : "COM1",
                                    static_cast<unsigned long>(ic.baud ? ic.baud : 9600UL));
                        } else {
                            sprintf(ep, "%.20s:%u", ic.host[0] ? ic.host : "0.0.0.0",
                                    static_cast<unsigned int>(ic.port));
                        }
                        sprintf(row, " %-2d %-11.11s %-12.12s %-29.29s %d",
                                i + 1, ic.id, ic.type, ep, pcnt);
                        vid_puts(4 + i, 1, (i == sel[3]) ? sel_at : fill_at, row, 78);
                    }
                }
            } else {
                vid_puts(3, 1, hdr_at, " ID         Callsign  Iface    Endpoint               ACL     Tag", 78);
                if (draft.peer_count == 0) {
                    vid_puts(4, 1, fill_at, " (no peers configured - press A to add)", 78);
                } else {
                    for (int i = 0; i < draft.peer_count && i < 16; ++i) {
                        const OykPeerConfig& pc = draft.peers[i];
                        char ep[28], acl[10], row[82];
                        if (pc.host[0] && pc.port > 0) sprintf(ep, "%.15s:%u", pc.host, static_cast<unsigned int>(pc.port));
                        else if (pc.host[0]) sprintf(ep, "%.21s", pc.host);
                        else strcpy(ep, "(any)");
                        sprintf(acl, "%s/%s", pc.allow_inbound ? "IN" : "--", pc.allow_outbound ? "OUT" : "---");
                        sprintf(row, " %-10.10s %-9.9s %-8.8s %-22.22s %-7.7s %s",
                                pc.id, pc.callsign, pc.interface_id, ep, acl,
                                pc.auto_learned ? "[AUTO]" : "");
                        vid_puts(4 + i, 1, (i == sel[4]) ? sel_at : fill_at, row, 78);
                    }
                }
            }

            vid_puts(23, 0, dos_vid_attr(g_vid, 0x0EU, 0x0FU), fb, 80);
            if (tab == 3) {
                vid_puts(24, 0, tab_bg, " Tab/1-7:Tabs  A:Add  Enter/E:Edit  D:Remove  S/F2:Save  Esc:Cancel ", 80);
            } else if (tab == 4) {
                vid_puts(24, 0, tab_bg, " 1-7:Tabs A:Add E:Edit D:Del I:In O:Out K:KeepAuto S/F2:Save Esc:Cancel", 80);
            } else {
                vid_puts(24, 0, tab_bg, " Tab/Lt/Rt/1-7:Tabs  Enter/Space:Edit  S/F2:Save & Apply  Esc:Cancel ", 80);
            }
            dos_vid_flush(g_vid);
        }

        if (!kbhit()) continue;
        dirty = 1;
        int ch = getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 72 && sel[tab] > 0) --sel[tab];             /* Up */
            else if (ext == 80 && sel[tab] + 1 < cnt) ++sel[tab];  /* Down */
            else if (ext == 75) tab = (tab + 6) % 7;               /* Left */
            else if (ext == 77) tab = (tab + 1) % 7;               /* Right */
            else if (ext == 60) ch = 'S';                          /* F2 */
            else continue;
            if (ch != 'S') continue;
        }
        if (ch == 27) break;
        if (ch == '\t') { tab = (tab + 1) % 7; continue; }
        if (ch >= '1' && ch <= '7') { tab = ch - '1'; continue; }

        if (ch == 's' || ch == 'S') {
            static OykConfigApplyResult res;
            if (!g_node.apply_config(&draft, 1, &res)) {
                sprintf(fb, "Error: %.68s", res.error);
                continue;
            }
            if (res.interfaces_changed) {
                rebind_dos_transports();
            }
            dos_printer_init();
            oyk_rig_status_init(&g_rig_status, oyk_rig_parse_protocol(g_node.config().rig_protocol));
            if (!res.saved_to_disk) {
                vid_clear(0x4F);
                vid_puts(8,  4, 0x4F, "WARNING: CONFIGURATION FILE NOT WRITABLE (READ-ONLY FILESYSTEM?)", 72);
                vid_puts(10, 4, 0x4F, "Changes have been applied to the running node in memory only", 72);
                vid_puts(11, 4, 0x4F, "and will be lost when the software exits.", 72);
                vid_puts(14, 4, 0x4E, "Press any key to continue...", 72);
                dos_vid_flush(g_vid);
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
                if (run_dos_add_iface_wizard(&draft, &ic)) {
                    draft.ifaces[draft.iface_count++] = ic;
                    sel[3] = draft.iface_count - 1;
                    sprintf(fb, "Added interface %s (press S/F2 to Save & Apply).", ic.id);
                } else {
                    strcpy(fb, "Add interface cancelled.");
                }
            } else if ((ch == '\r' || ch == '\n' || ch == 'e' || ch == 'E') && draft.iface_count > 0) {
                if (sel[3] >= 0 && sel[3] < draft.iface_count) {
                    OykIfaceConfig ic;
                    if (run_dos_edit_iface_dialog(&draft, sel[3], &ic)) {
                        draft.ifaces[sel[3]] = ic;
                        sprintf(fb, "Updated interface %s (press S/F2 to Save & Apply).", ic.id);
                    } else {
                        strcpy(fb, "Edit interface cancelled.");
                    }
                }
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

        if (tab == 4) {
            if (ch == 'a' || ch == 'A') {
                if (draft.peer_count >= OYK_MAX_CONFIG_PEERS) {
                    strcpy(fb, "Maximum peer count reached.");
                    continue;
                }
                OykPeerConfig pc;
                if (run_dos_edit_peer_dialog(&draft, -1, &pc)) {
                    draft.peers[draft.peer_count++] = pc;
                    sel[4] = draft.peer_count - 1;
                    sprintf(fb, "Added peer %s (press S/F2 to Save & Apply).", pc.id);
                } else {
                    strcpy(fb, "Add peer cancelled.");
                }
            } else if ((ch == '\r' || ch == '\n' || ch == 'e' || ch == 'E') && draft.peer_count > 0) {
                if (sel[4] >= 0 && sel[4] < draft.peer_count) {
                    OykPeerConfig pc;
                    if (run_dos_edit_peer_dialog(&draft, sel[4], &pc)) {
                        draft.peers[sel[4]] = pc;
                        sprintf(fb, "Updated peer %s (press S/F2 to Save & Apply).", pc.id);
                    } else {
                        strcpy(fb, "Edit peer cancelled.");
                    }
                }
            } else if ((ch == 'd' || ch == 'D') && draft.peer_count > 0) {
                char yn[4], pr[48];
                sprintf(pr, "Remove peer %s? [Y/N]: ", draft.peers[sel[4]].id);
                prompt_line(pr, yn, sizeof(yn));
                if (yn[0] == 'y' || yn[0] == 'Y') {
                    for (int i = sel[4] + 1; i < draft.peer_count; ++i) {
                        draft.peers[i - 1] = draft.peers[i];
                    }
                    draft.peer_count--;
                    strcpy(fb, "Peer removed.");
                }
            } else if ((ch == 'i' || ch == 'I') && draft.peer_count > 0) {
                OykPeerConfig& pc = draft.peers[sel[4]];
                pc.allow_inbound = pc.allow_inbound ? 0 : 1;
                pc.auto_learned = 0;
                sprintf(fb, "Peer %s allow_inbound=%s", pc.id, pc.allow_inbound ? "true" : "false");
            } else if ((ch == 'o' || ch == 'O') && draft.peer_count > 0) {
                OykPeerConfig& pc = draft.peers[sel[4]];
                pc.allow_outbound = pc.allow_outbound ? 0 : 1;
                pc.auto_learned = 0;
                sprintf(fb, "Peer %s allow_outbound=%s", pc.id, pc.allow_outbound ? "true" : "false");
            } else if (ch == 'k' || ch == 'K') {
                int kept = 0;
                for (int i = 0; i < draft.peer_count && i < OYK_MAX_CONFIG_PEERS; ++i) {
                    if (draft.peers[i].auto_learned) {
                        draft.peers[i].auto_learned = 0;
                        ++kept;
                    }
                }
                sprintf(fb, "Marked %d auto-learned peer(s) persistent.", kept);
            }
            continue;
        }

        if (ch == '\r' || ch == '\n' || ch == ' ') {
            int f = sel[tab];
            if (tab == 0 && f == 4) { draft.accept_incoming = !draft.accept_incoming; continue; }
            if (tab == 0 && f == 5) {
                if (draft.learned_peer_policy == OYK_LEARN_NONE) draft.learned_peer_policy = OYK_LEARN_INBOUND;
                else if (draft.learned_peer_policy == OYK_LEARN_INBOUND) draft.learned_peer_policy = OYK_LEARN_OUTBOUND;
                else if (draft.learned_peer_policy == OYK_LEARN_OUTBOUND) draft.learned_peer_policy = OYK_LEARN_BOTH;
                else draft.learned_peer_policy = OYK_LEARN_NONE;
                continue;
            }
            if (tab == 1 && f == 0) { draft.bbs_enabled = !draft.bbs_enabled; continue; }
            if (tab == 1 && f == 4) { draft.bbs_files_enabled = !draft.bbs_files_enabled; continue; }
            if (tab == 2 && f == 0) { draft.chat_enabled = !draft.chat_enabled; continue; }
            if (tab == 5 && f == 0) { draft.rig_enabled = !draft.rig_enabled; continue; }
            if (tab == 5 && f == 1) {
                if (oyk_rig_parse_protocol(draft.rig_protocol) == OYK_RIG_PROTO_RIGCTL) {
                    strcpy(draft.rig_protocol, "radiofoundry");
                    if (draft.rig_port == 4532 || draft.rig_port == 0) draft.rig_port = 4191;
                } else {
                    strcpy(draft.rig_protocol, "rigctl");
                    if (draft.rig_port == 4191 || draft.rig_port == 0) draft.rig_port = 4532;
                }
                continue;
            }
            if (tab == 5 && f == 5) { draft.rig_notify = !draft.rig_notify; continue; }
            if (tab == 6 && f == 0) { draft.printer_enabled = !draft.printer_enabled; continue; }
            if (tab == 6 && f == 2) {
                int pp = oyk_printer_parse_protocol(draft.printer_protocol);
                if (pp == OYK_PRN_GENERIC) strcpy(draft.printer_protocol, "mx80");
                else if (pp == OYK_PRN_MX80) strcpy(draft.printer_protocol, "escp2");
                else strcpy(draft.printer_protocol, "generic");
                continue;
            }
            if (tab == 6 && f == 5) { draft.printer_condensed = !draft.printer_condensed; continue; }

            const char** labels = (tab == 0) ? NODE_LABELS : ((tab == 1) ? BBS_LABELS : ((tab == 2) ? CHAT_LABELS : ((tab == 5) ? RIG_LABELS : PRN_LABELS)));
            char ed[96];
            ed[0] = '\0';
            if (tab == 0) {
                if (f == 0) oyk_format_callsign(&draft.callsign, ed, sizeof(ed));
                else if (f == 1 && draft.alias.base[0]) oyk_format_callsign(&draft.alias, ed, sizeof(ed));
                else if (f == 2) { strncpy(ed, draft.welcome_text, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; }
                else if (f == 3) { strncpy(ed, draft.info_text, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; }
                else if (f == 6) { strncpy(ed, draft.new_user_welcome, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; }
                else if (f == 7) { strncpy(ed, draft.interfaces_file, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; }
                else if (f == 8) { strncpy(ed, draft.peers_file, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; }
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
            } else if (tab == 5) {
                if (f == 2) { strncpy(ed, draft.rig_host, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; }
                else if (f == 3) sprintf(ed, "%u", static_cast<unsigned int>(draft.rig_port));
                else if (f == 4) sprintf(ed, "%u", static_cast<unsigned int>(draft.rig_poll_sec));
            } else {
                if (f == 1) { strncpy(ed, draft.printer_device, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; }
                else if (f == 3) sprintf(ed, "%lu", static_cast<unsigned long>(draft.printer_baud ? draft.printer_baud : 9600UL));
                else if (f == 4) sprintf(ed, "0x%lX", static_cast<unsigned long>(draft.printer_level));
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
                else if (f == 6) { strncpy(draft.new_user_welcome, ed, sizeof(draft.new_user_welcome) - 1); draft.new_user_welcome[sizeof(draft.new_user_welcome) - 1] = '\0'; }
                else if (f == 7) { strncpy(draft.interfaces_file, ed, sizeof(draft.interfaces_file) - 1); draft.interfaces_file[sizeof(draft.interfaces_file) - 1] = '\0'; }
                else if (f == 8) { strncpy(draft.peers_file, ed, sizeof(draft.peers_file) - 1); draft.peers_file[sizeof(draft.peers_file) - 1] = '\0'; }
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
            } else if (tab == 5) {
                if (f == 2) { if (ed[0]) { strncpy(draft.rig_host, ed, sizeof(draft.rig_host) - 1); draft.rig_host[sizeof(draft.rig_host) - 1] = '\0'; } }
                else if (f == 3) { long pv = atol(ed); if (pv > 0L && pv <= 65535L) draft.rig_port = static_cast<oyk_u16>(pv); else strcpy(fb, "Port must be 1..65535."); }
                else if (f == 4) { long iv = atol(ed); if (iv >= 0L && iv <= 3600L) draft.rig_poll_sec = static_cast<oyk_u16>(iv); }
            } else {
                if (f == 1) { strncpy(draft.printer_device, ed, sizeof(draft.printer_device) - 1); draft.printer_device[sizeof(draft.printer_device) - 1] = '\0'; }
                else if (f == 3) { long bv = atol(ed); if (bv >= 300L) draft.printer_baud = static_cast<oyk_u32>(bv); else strcpy(fb, "Baud must be >= 300."); }
                else if (f == 4) { if (!oyk_parse_log_mask(ed, &draft.printer_level)) strcpy(fb, "Invalid log mask."); }
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
        run_dos_settings(0);
    } else if (chosen == 6) {
        run_dos_settings(4);
    } else if (chosen == 7) {
        char id_buf[12];
        prompt_line("Terminate session ID (Esc=cancel): ", id_buf, sizeof(id_buf));
        if (id_buf[0]) {
            g_node.terminate_session(static_cast<oyk_u32>(atol(id_buf)),
                                     g_status_line, sizeof(g_status_line));
        }
    } else if (chosen == 8) {
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
    } else if (chosen == 9) {
        if (confirm_dos_exit()) {
            *running = 0;
        }
    }
}

static void open_dos_node_menu(int* running) {
    static const char* ITEMS[10] = {
        " [A] Allow Incoming Connections ",
        " [D] Deny Incoming Connections  ",
        " [L] Local Terminal...          ",
        " [F] Manage Files Area...       ",
        " [R] Radio Control...           ",
        " [S] Settings...                ",
        " [P] Peers...                   ",
        " [T] Terminate Connection...    ",
        " [B] Ban / Unban Callsign...    ",
        " [X] Exit                       "
    };
    static const int ITEM_ROWS[10] = { 2, 3, 5, 6, 7, 8, 9, 10, 11, 13 };

    int sel = 0;
    int chosen = -1;
    int dirty = 1;
    oyk_u8 menu_bg  = dos_vid_attr(g_vid, 0x70U, 0x70U);
    oyk_u8 menu_sel = dos_vid_attr(g_vid, 0x1FU, 0x0FU);
    while (chosen < 0 && !g_break_flag) {
        dos_pump_network();
        g_node.tick();
        if (dirty) {
            dirty = 0;
            dos_vid_box(g_vid, 1, 1, 14, 34, menu_bg, menu_bg, 0);
            dos_vid_hdiv(g_vid, 4, 1, 34, menu_bg);
            dos_vid_hdiv(g_vid, 12, 1, 34, menu_bg);
            for (int i = 0; i < 10; ++i) {
                dos_vid_putc(g_vid, ITEM_ROWS[i], 1, menu_bg, OYK_BOX_V);
                vid_puts(ITEM_ROWS[i], 2, (i == sel) ? menu_sel : menu_bg, ITEMS[i], 32);
                dos_vid_putc(g_vid, ITEM_ROWS[i], 34, menu_bg, OYK_BOX_V);
            }
            dos_vid_flush(g_vid);
        }

        if (!kbhit()) continue;
        dirty = 1;
        int ch = getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 72) sel = (sel + 9) % 10;      /* Up arrow */
            else if (ext == 80) sel = (sel + 1) % 10; /* Down arrow */
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
        } else if (ch == 'p' || ch == 'P') {
            chosen = 6;
        } else if (ch == 't' || ch == 'T') {
            chosen = 7;
        } else if (ch == 'b' || ch == 'B') {
            chosen = 8;
        } else if (ch == 'x' || ch == 'X' || ch == 'q' || ch == 'Q') {
            chosen = 9;
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

static oyk_u32 dos_screen_signature() {
    oyk_u32 sig = g_node.uptime_seconds();
    sig = (sig * 1315423911UL) ^ static_cast<oyk_u32>(g_node.total_active_connections());
    sig = (sig << 5) ^ static_cast<oyk_u32>(g_node.ban_count());
    sig = (sig << 5) ^ static_cast<oyk_u32>(g_node.console_log_count());
    sig = (sig << 5) ^ static_cast<oyk_u32>(g_node.new_heard_count());
    sig ^= g_node.accept_incoming() ? 0x10000UL : 0UL;
    sig ^= dos_local_term_is_active() ? 0x20000UL : 0UL;
    for (int i = 0; i < g_node.iface_count(); ++i) {
        const OykIfaceRuntime& r = g_node.iface(i);
        sig ^= (r.rx_frames + r.tx_frames + (r.running ? 1UL : 0UL)) << ((i & 3) * 4);
    }
    return sig;
}

int main(int argc, char* argv[]) {
    struct MainCfgGuard {
        OykConfig* p;
        MainCfgGuard() : p(new OykConfig) {}
        ~MainCfgGuard() { delete p; }
    } cfg_guard;
    if (!cfg_guard.p) {
        printf("Out of memory allocating node config.\n");
        return 1;
    }
    OykConfig& cfg = *cfg_guard.p;
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
    g_node.set_printer_callback(dos_printer_callback, NULL);
    dos_printer_init();

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
        g_vid = new OykDosVideo;
        dos_vid_init(g_vid);

        int running = 1;
        render_dos_screen();
        oyk_u32 last_sig = dos_screen_signature();

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
                } else if (ch == 'p' || ch == 'P') {
                    run_dos_menu_action(6, &running);
                } else if (ch == 't' || ch == 'T') {
                    run_dos_menu_action(7, &running);
                } else if (ch == 'b' || ch == 'B') {
                    run_dos_menu_action(8, &running);
                } else if (ch == 'q' || ch == 'Q' || ch == 'x' || ch == 'X') {
                    run_dos_menu_action(9, &running);
                }
                render_dos_screen();
                last_sig = dos_screen_signature();
            } else {
                oyk_u32 sig = dos_screen_signature();
                if (sig != last_sig) {
                    last_sig = sig;
                    render_dos_screen();
                }
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
    dos_printer_close();
    oyk_dos_pktdrv_shutdown(&g_pktdrv);
    g_ems.shutdown();
    if (!headless && g_vid) {
        dos_vid_shutdown(g_vid);
        delete g_vid;
        g_vid = NULL;
    }
    {
        char signoff_call[24];
        oyk_format_callsign(&g_node.config().callsign, signoff_call, sizeof(signoff_call));
        printf("73 de %s\n", signoff_call);
        fflush(stdout);
    }
    return 0;
}
