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
static char g_status_line[78] = "Ready. Press F10 or M to open the Node menu.";

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

    /* Row 0: Top Menu Bar (matching GUI and ncurses menu bar) */
    vid_puts(0, 0,  0x70, "", 80);
    vid_puts(0, 1,  0x30, " Node ", 6);
    vid_puts(0, 65, 0x70, "F10 / M: Menu ", 15);

    /* Row 1: Header bar */
    sprintf(line, " OYK PACKET NODE (DOS): %-9s | Up:%lus | Conn:%d | EMS:%s",
            call_s,
            static_cast<unsigned long>(g_node.uptime_seconds()),
            g_node.total_active_connections(),
            g_ems.available() ? "4.0" : "NO ");
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
                    g_node.iface(s.iface_idx).cfg.id, s.since_hms,
                    s.restricted_ban ? "BAN NOTICE" : "NODE SHELL");
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
    return (Udp::sendUdp(dst_ip, g_axudp_port, peer_port, static_cast<uint16_t>(n), raw, 0) == 0) ? 1 : 0;
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

static void run_dos_menu_action(int chosen, int* running) {
    if (chosen == 0) {
        g_node.set_accept_incoming(1);
        strcpy(g_status_line, "New incoming connections are now ALLOWED.");
    } else if (chosen == 1) {
        g_node.set_accept_incoming(0);
        strcpy(g_status_line, "New incoming connections are now DENIED.");
    } else if (chosen == 2) {
        char id_buf[12];
        prompt_line("Terminate session ID (Esc=cancel): ", id_buf, sizeof(id_buf));
        if (id_buf[0]) {
            g_node.terminate_session(static_cast<oyk_u32>(atol(id_buf)),
                                     g_status_line, sizeof(g_status_line));
        }
    } else if (chosen == 3) {
        char call_buf[16];
        prompt_line("Ban callsign (prefix '-' to unban): ", call_buf, sizeof(call_buf));
        if (call_buf[0] == '-') {
            g_node.unban_callsign(call_buf + 1, g_status_line, sizeof(g_status_line));
        } else if (call_buf[0]) {
            g_node.ban_callsign(call_buf, OYK_BAN_APPEAL_ALLOWED,
                                "Banned by DOS sysop.", "permanent",
                                g_status_line, sizeof(g_status_line));
        }
    } else if (chosen == 4) {
        if (confirm_dos_exit()) {
            *running = 0;
        }
    }
}

static void open_dos_node_menu(int* running) {
    static const char* ITEMS[5] = {
        " [A] Allow Incoming Connections ",
        " [D] Deny Incoming Connections  ",
        " [T] Terminate Connection...    ",
        " [B] Ban / Unban Callsign...    ",
        " [X] Exit                       "
    };
    static const int ITEM_ROWS[5] = { 2, 3, 5, 6, 8 };

    int sel = 0;
    int chosen = -1;
    while (chosen < 0 && !g_break_flag) {
        vid_puts(1, 1, 0x70, "+--------------------------------+", 34);
        vid_puts(4, 1, 0x70, "+--------------------------------+", 34);
        vid_puts(7, 1, 0x70, "+--------------------------------+", 34);
        vid_puts(9, 1, 0x70, "+--------------------------------+", 34);
        for (int i = 0; i < 5; ++i) {
            vid_puts(ITEM_ROWS[i], 1, 0x70, "|", 1);
            vid_puts(ITEM_ROWS[i], 2, (i == sel) ? 0x1F : 0x70, ITEMS[i], 32);
            vid_puts(ITEM_ROWS[i], 34, 0x70, "|", 1);
        }

        int ch = getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 72) sel = (sel + 4) % 5;      /* Up arrow */
            else if (ext == 80) sel = (sel + 1) % 5; /* Down arrow */
            else if (ext == 68) break;               /* F10 closes menu */
        } else if (ch == 27) {
            break;
        } else if (ch == '\r' || ch == '\n') {
            chosen = sel;
        } else if (ch == 'a' || ch == 'A') {
            chosen = 0;
        } else if (ch == 'd' || ch == 'D') {
            chosen = 1;
        } else if (ch == 't' || ch == 'T') {
            chosen = 2;
        } else if (ch == 'b' || ch == 'B') {
            chosen = 3;
        } else if (ch == 'x' || ch == 'X' || ch == 'q' || ch == 'Q') {
            chosen = 4;
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

int main(int argc, char* argv[]) {
    static OykConfig cfg;
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
            if (ch == 0 || ch == 0xE0) {
                int ext = getch();
                if (ext == 68) { /* F10 */
                    open_dos_node_menu(&running);
                }
            } else if (ch == 'm' || ch == 'M' || ch == 'n' || ch == 'N' || ch == '\r' || ch == '\n') {
                open_dos_node_menu(&running);
            } else if (ch == 'a' || ch == 'A') {
                run_dos_menu_action(0, &running);
            } else if (ch == 'd' || ch == 'D') {
                run_dos_menu_action(1, &running);
            } else if (ch == 't' || ch == 'T') {
                run_dos_menu_action(2, &running);
            } else if (ch == 'b' || ch == 'B') {
                run_dos_menu_action(3, &running);
            } else if (ch == 'q' || ch == 'Q' || ch == 'x' || ch == 'X') {
                run_dos_menu_action(4, &running);
            }
            render_dos_screen();
        }

        if (Timer_diff(last_draw, TIMER_GET_CURRENT()) >= TIMER_MS_TO_TICKS(250)) {
            last_draw = TIMER_GET_CURRENT();
            render_dos_screen();
        }
    }

    if (g_node.begin_shutdown() > 0) {
        clockTicks_t shut_start = TIMER_GET_CURRENT();
        while (g_node.disconnecting_count() > 0 &&
               Timer_diff(shut_start, TIMER_GET_CURRENT()) < TIMER_MS_TO_TICKS(1000)) {
            PACKET_PROCESS_SINGLE;
            Arp::driveArp();
            Tcp::drivePackets();
        }
    }
    g_node.finish_shutdown();
    if (g_axudp_iface_idx >= 0) {
        Udp::unregisterCallback(g_axudp_port);
    }
    Utils::endStack();
    g_ems.shutdown();
    vid_clear(0x07);
    printf("OYK Packet Node (MS-DOS) exited cleanly.\n");
    return 0;
}
