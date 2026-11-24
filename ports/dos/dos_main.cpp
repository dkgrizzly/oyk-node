/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * OYK Amateur Radio Packet Node - MS-DOS Port
 *
 * Target: IBM PC/XT (8088 / NEC V20), 640KB conventional RAM, optional LIM EMS 4.0
 * Compiler: OpenWatcom C/C++ 1.9 (-0 -ml -os -bt=dos)
 * TCP/IP Stack: Michael Brutman's mTCP (Packet Driver + ARP + IP + UDP + TCP)
 *
 * Local UI: Direct 80x25 CGA/MDA/VGA text-mode console matching the ncurses layout.
 */

#include "../compat/oyk_portable.hpp"
#include "ems.hpp"

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Michael Brutman's mTCP headers */
#include "types.h"
#include "utils.h"
#include "packet.h"
#include "arp.h"
#include "udp.h"
#include "tcp.h"
#include "tcpsockm.h"
#include "timer.h"

static OykPortableNode g_node;
static OykEms4 g_ems;
static volatile int g_break_flag = 0;
static int g_axudp_iface_idx = -1;
static oyk_u16 g_axudp_port = 10093;
static char g_status_line[78] = "Ready. Press [1] Allow/Deny  [2] Term  [3] Ban  [4] Exit";

static void __interrupt __far ctrl_break_handler() {
    g_break_flag = 1;
}

static void __interrupt __far ctrl_c_handler() {
    g_break_flag = 1;
}

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

static void render_dos_screen() {
    char line[82];
    char call_s[16];
    oyk_format_callsign(&g_node.config().callsign, call_s, sizeof(call_s));

    /* Row 0: Header bar */
    sprintf(line, " OYK PACKET NODE (DOS): %-9s | Up:%lus | Conn:%d | EMS:%s",
            call_s,
            static_cast<unsigned long>(g_node.uptime_seconds()),
            g_node.total_active_connections(),
            g_ems.available() ? "4.0" : "NO ");
    vid_puts(0, 0, 0x1F, line, 61);
    vid_puts(0, 61, g_node.accept_incoming() ? 0x2F : 0x4F,
             g_node.accept_incoming() ? " [INCOMING: ALLOW] " : " [INCOMING: DENY]  ", 19);

    /* Rows 1..7: Interfaces (left 0..43) + Last 5 New Callsigns Heard (right 44..79) */
    vid_puts(1, 0,  0x0B, "Radio Interfaces (Active Conn)              ", 44);
    vid_puts(1, 44, 0x0B, "Last 5 New Callsigns Heard          ", 36);
    vid_puts(2, 0,  0x0F, "Port     Type    Conn Status         RX/TX  ", 44);
    vid_puts(2, 44, 0x0F, "#  Callsign   Port     Heard        ", 36);

    for (int i = 0; i < 5; ++i) {
        if (i < g_node.iface_count()) {
            const OykIfaceRuntime& r = g_node.iface(i);
            sprintf(line, "%-8.8s %-7.7s %-4d %-14.14s %lu/%lu",
                    r.cfg.id, r.cfg.type, g_node.active_connections_on(i), r.status,
                    static_cast<unsigned long>(r.rx_frames),
                    static_cast<unsigned long>(r.tx_frames));
            vid_puts(3 + i, 0, r.running ? 0x0A : 0x0C, line, 44);
        } else {
            vid_puts(3 + i, 0, 0x07, "", 44);
        }

        if (i < g_node.new_heard_count()) {
            const OykNewHeard& nh = g_node.new_heard(i);
            sprintf(line, "%d. %-10.10s %-8.8s %-8.8s",
                    i + 1, nh.callsign, nh.iface_id, nh.time_hms);
            vid_puts(3 + i, 44, 0x07, line, 36);
        } else {
            vid_puts(3 + i, 44, 0x08, (i == 0 && g_node.new_heard_count() == 0) ? "(none heard yet)" : "", 36);
        }
    }

    /* Rows 8..14: Active Connections */
    sprintf(line, "Active Connections (%d) | Banned: %d",
            g_node.total_active_connections(), g_node.ban_count());
    vid_puts(8, 0, 0x0B, line, 80);
    vid_puts(9, 0, 0x0F, "ID   Callsign   Port     Since    Mode", 80);
    int row = 10;
    for (int i = 0; i < g_node.max_sessions() && row < 15; ++i) {
        const OykSession& s = g_node.session_slot(i);
        if (s.state == OYK_SESS_CONNECTED) {
            char cs[12];
            oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
            sprintf(line, "#%-3lu %-10.10s %-8.8s %-8.8s %s",
                    static_cast<unsigned long>(s.id), cs,
                    g_node.iface(s.iface_idx).cfg.id, s.since_hms,
                    s.restricted_ban ? "BAN NOTICE" : "NODE SHELL");
            vid_puts(row++, 0, 0x07, line, 80);
        }
    }
    while (row < 15) vid_puts(row++, 0, 0x07, "", 80);

    /* Rows 15..22: Node Activity Log */
    vid_puts(15, 0, 0x0B, "Node Activity Log", 80);
    int total_logs = g_node.console_log_count();
    int start = (total_logs > 7) ? (total_logs - 7) : 0;
    for (int r = 0; r < 7; ++r) {
        int idx = start + r;
        vid_puts(16 + r, 0, 0x07, (idx < total_logs) ? g_node.console_log_line(idx) : "", 80);
    }

    /* Row 23: Status message */
    vid_puts(23, 0, 0x0E, g_status_line, 80);

    /* Row 24: Menu bar */
    vid_puts(24, 0, 0x30,
             " [1] Allow/Deny Incoming  [2] Terminate Conn  [3] Ban/Unban  [4] Exit Node ",
             80);
}

/* ==================== mTCP AXUDP Transport Bridge ==================== */

static void axudp_rx_callback(const unsigned char* packet, const UdpHeader* udp) {
    if (g_axudp_iface_idx < 0 || !packet || !udp) return;
    oyk_u16 udp_len = ntohs(udp->len);
    if (udp_len <= sizeof(UdpHeader) + 2) return;
    size_t pay_len = udp_len - sizeof(UdpHeader);
    const oyk_u8* payload = reinterpret_cast<const oyk_u8*>(packet + sizeof(EthHeader) + sizeof(IpHeader) + sizeof(UdpHeader));

    if (!oyk_verify_fcs(payload, pay_len)) return;

    OykFrame frame;
    if (!oyk_ax25_decode(payload, pay_len - 2, &frame)) return;

    const IpHeader* ip = reinterpret_cast<const IpHeader*>(packet + sizeof(EthHeader));
    oyk_u32 src_ip = 0;
    memcpy(&src_ip, ip->ip_src, 4);
    oyk_u16 src_port = ntohs(udp->src);

    g_node.on_rx_frame(g_axudp_iface_idx, &frame, src_ip, src_port, 0);
}

static int dos_tx_callback(int iface_idx, const OykFrame* frame,
                           oyk_u32 peer_ip, oyk_u16 peer_port,
                           oyk_u16 /*peer_handle*/, void* /*ctx*/) {
    if (iface_idx != g_axudp_iface_idx || peer_ip == 0 || peer_port == 0) return 0;

    oyk_u8 raw[320];
    size_t n = oyk_ax25_encode(frame, raw, sizeof(raw) - 2);
    if (n == 0) return 0;
    oyk_u16 fcs = oyk_crc16_fcs(raw, n);
    raw[n++] = static_cast<oyk_u8>(fcs & 0xFFU);
    raw[n++] = static_cast<oyk_u8>((fcs >> 8) & 0xFFU);

    IpAddr_t dst_ip;
    memcpy(dst_ip, &peer_ip, 4);
    return (Udp::sendUdp(dst_ip, g_axudp_port, peer_port, static_cast<uint16_t>(n), raw, 1) == 0) ? 1 : 0;
}

static void prompt_line(const char* prompt, char* out, int max_len) {
    vid_puts(23, 0, 0x4F, prompt, 80);
    int len = 0;
    out[0] = '\0';
    int pcol = static_cast<int>(strlen(prompt));
    while (1) {
        int ch = getch();
        if (ch == 27) { out[0] = '\0'; return; }
        if (ch == '\r' || ch == '\n') { out[len] = '\0'; return; }
        if ((ch == 8 || ch == 127) && len > 0) {
            out[--len] = '\0';
        } else if (ch >= 32 && ch < 127 && len < max_len - 1) {
            out[len++] = static_cast<char>(ch);
            out[len] = '\0';
        }
        vid_puts(23, pcol, 0x4F, out, 80 - pcol);
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

int main(int argc, char* argv[]) {
    OykConfig cfg;
    char cfg_path[128];
    char err[96];
    cfg_path[0] = '\0';

    for (int i = 1; i < argc; ++i) {
        if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) && i + 1 < argc) {
            strncpy(cfg_path, argv[++i], sizeof(cfg_path) - 1);
            cfg_path[sizeof(cfg_path) - 1] = '\0';
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

    /* Optional LIM EMS 4.0 initialization (4 x 16KB = 64KB expanded scrollback if available) */
    g_ems.init(4);

    if (Utils::parseEnv() != 0) {
        printf("Error: MTCPCFG environment variable not set or invalid.\n");
        g_ems.shutdown();
        return 1;
    }
    if (Utils::initStack(4, 8, ctrl_break_handler, ctrl_c_handler) != 0) {
        printf("Error: Failed to initialize mTCP stack.\n");
        g_ems.shutdown();
        return 1;
    }

    if (!g_node.init(&cfg, dos_tx_callback, NULL, err, sizeof(err))) {
        printf("Node init failed: %s\n", err);
        Utils::endStack();
        g_ems.shutdown();
        return 1;
    }

    /* Register first AXUDP interface with mTCP UDP layer */
    for (int i = 0; i < g_node.iface_count(); ++i) {
        if (strcmp(g_node.iface(i).cfg.type, "axudp") == 0) {
            g_axudp_iface_idx = i;
            g_axudp_port = g_node.iface(i).cfg.port ? g_node.iface(i).cfg.port : 10093;
            if (Udp::registerCallback(g_axudp_port, axudp_rx_callback) == 0) {
                char st[32];
                sprintf(st, "UP :%u", static_cast<unsigned int>(g_axudp_port));
                g_node.set_iface_status(i, 1, st);
            } else {
                g_node.set_iface_status(i, 0, "UDP ERR");
            }
            break;
        }
    }

    g_vseg = detect_video_seg();
    vid_clear(0x07);

    int running = 1;
    clockTicks_t last_draw = TIMER_GET_CURRENT();

    while (running && !g_break_flag) {
        /* Cooperative mTCP packet pump */
        PACKET_PROCESS_SINGLE;
        Arp::driveArp();
        Tcp::drivePackets();
        g_node.tick();

        if (kbhit()) {
            int ch = getch();
            if (ch == '1' || ch == 'a' || ch == 'A') {
                g_node.set_accept_incoming(!g_node.accept_incoming());
                strcpy(g_status_line, g_node.accept_incoming()
                           ? "Incoming connections ALLOWED."
                           : "Incoming connections DENIED.");
            } else if (ch == '2' || ch == 't' || ch == 'T') {
                char id_buf[12];
                prompt_line("Terminate session ID (Esc=cancel): ", id_buf, sizeof(id_buf));
                if (id_buf[0]) {
                    g_node.terminate_session(static_cast<oyk_u32>(atol(id_buf)),
                                             g_status_line, sizeof(g_status_line));
                }
            } else if (ch == '3' || ch == 'b' || ch == 'B') {
                char call_buf[16];
                prompt_line("Ban callsign (prefix '-' to unban): ", call_buf, sizeof(call_buf));
                if (call_buf[0] == '-') {
                    g_node.unban_callsign(call_buf + 1, g_status_line, sizeof(g_status_line));
                } else if (call_buf[0]) {
                    g_node.ban_callsign(call_buf, OYK_BAN_APPEAL_ALLOWED,
                                        "Banned by DOS sysop.", "permanent",
                                        g_status_line, sizeof(g_status_line));
                }
            } else if (ch == '4' || ch == 'q' || ch == 'Q') {
                char yn[4];
                prompt_line("Exit node cleanly? [Y/N]: ", yn, sizeof(yn));
                if (yn[0] == 'y' || yn[0] == 'Y') running = 0;
            }
            render_dos_screen();
        }

        if (Timer_diff(last_draw, TIMER_GET_CURRENT()) >= TIMER_MS_TO_TICKS(250)) {
            last_draw = TIMER_GET_CURRENT();
            render_dos_screen();
        }
    }

    g_node.shutdown();
    if (g_axudp_iface_idx >= 0) {
        Udp::unregisterCallback(g_axudp_port);
    }
    Utils::endStack();
    g_ems.shutdown();
    vid_clear(0x07);
    printf("OYK Packet Node (MS-DOS) exited cleanly.\n");
    return 0;
}
