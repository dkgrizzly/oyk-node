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
#include "../compat/oyk_yapp.hpp"
#include "ems.hpp"

#include <conio.h>
#include <direct.h>
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
        PACKET_PROCESS_SINGLE;
        Arp::driveArp();
        Tcp::drivePackets();
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
        PACKET_PROCESS_SINGLE;
        Arp::driveArp();
        Tcp::drivePackets();

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

    clockTicks_t last_draw = 0;

    while (!g_break_flag) {
        PACKET_PROCESS_SINGLE;
        Arp::driveArp();
        Tcp::drivePackets();
        g_node.tick();

        if (Timer_diff(last_draw, TIMER_GET_CURRENT()) >= TIMER_MS_TO_TICKS(100)) {
            render_dos_terminal(g_dterm_in_buf);
            last_draw = TIMER_GET_CURRENT();
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
    int count = oyk_bbs_list_incoming("incoming", items, 32);
    int sel = 0;
    int top = 0;
    char feedback[78];
    sprintf(feedback, "Target BBS Files Area: %.50s", g_node.config().bbs_files_path);

    const int panel_x = 4;
    const int panel_w = 72;
    const int list_y = 5;
    const int list_h = 13;

    while (!g_break_flag) {
        PACKET_PROCESS_SINGLE;
        Arp::driveArp();
        Tcp::drivePackets();
        g_node.tick();

        if (sel >= count) sel = (count > 0) ? (count - 1) : 0;
        if (sel < top) top = sel;
        if (sel >= top + list_h) top = sel - list_h + 1;

        vid_puts(2, panel_x, 0x1F,
                 "+-[ Approve Incoming Files (incoming/ -> BBS files/) ]-----------------+", panel_w);
        char hdr[74];
        sprintf(hdr, "| Incoming: incoming/  ->  Files Area: %-30.30s |",
                g_node.config().bbs_files_path);
        vid_puts(3, panel_x, 0x1E, hdr, panel_w);
        vid_puts(4, panel_x, 0x1F,
                 "| Uploader      Filename                                     Size     |", panel_w);

        for (int i = 0; i < list_h; ++i) {
            int idx = top + i;
            if (count == 0 && i == 1) {
                vid_puts(list_y + i, panel_x, 0x1F, "|", 1);
                vid_puts(list_y + i, panel_x + 1, 0x1F,
                         " (No pending files in incoming\\<CALLSIGN-SSID>\\)", panel_w - 2);
                vid_puts(list_y + i, panel_x + panel_w - 1, 0x1F, "|", 1);
            } else if (idx < count) {
                char row[72];
                sprintf(row, " %-13.13s %-42.42s %9lu ",
                        items[idx].uploader, items[idx].filename,
                        static_cast<unsigned long>(items[idx].size));
                oyk_u8 attr = (idx == sel) ? 0x30 : 0x1F;
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
                 " Enter/A:Approve & Move  D:Delete  R:Refresh  Esc:Close               ", panel_w);
        vid_puts(24, 0, 0x0E, feedback, 80);

        if (!kbhit()) continue;
        int ch = getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 72 && sel > 0) --sel;
            else if (ext == 80 && sel + 1 < count) ++sel;
            else if (ext == 73) sel = (sel > list_h) ? (sel - list_h) : 0;
            else if (ext == 81 && count > 0) sel = (sel + list_h < count) ? (sel + list_h) : (count - 1);
        } else if (ch == 27 || ch == 'q' || ch == 'Q') {
            break;
        } else if (ch == 'r' || ch == 'R') {
            count = oyk_bbs_list_incoming("incoming", items, 32);
            sprintf(feedback, "Refreshed (%d pending file(s)).", count);
        } else if ((ch == '\r' || ch == '\n' || ch == 'a' || ch == 'A') && count > 0) {
            char subdir[48], dest[160], err[80];
            prompt_line("Subfolder in files area (blank=root): ", subdir, sizeof(subdir));
            int exists = 0;
            int ok = oyk_bbs_approve_incoming_file("incoming", g_node.config().bbs_files_path,
                                                   items[sel].uploader, items[sel].filename,
                                                   subdir, 0, dest, sizeof(dest),
                                                   &exists, err, sizeof(err));
            if (!ok && exists) {
                char yn[4];
                prompt_line("File exists in files area. Overwrite? [Y/N]: ", yn, sizeof(yn));
                if (yn[0] == 'y' || yn[0] == 'Y') {
                    ok = oyk_bbs_approve_incoming_file("incoming", g_node.config().bbs_files_path,
                                                       items[sel].uploader, items[sel].filename,
                                                       subdir, 1, dest, sizeof(dest),
                                                       &exists, err, sizeof(err));
                } else {
                    strcpy(feedback, "Overwrite declined; file remains in incoming/.");
                    continue;
                }
            }
            if (ok) {
                char lmsg[96];
                sprintf(lmsg, "Approved %s/%s -> %.52s", items[sel].uploader, items[sel].filename, dest);
                g_node.log_message(OYK_LOG_INFO, lmsg);
                strncpy(feedback, lmsg, sizeof(feedback) - 1);
                feedback[sizeof(feedback) - 1] = '\0';
                strncpy(g_status_line, lmsg, sizeof(g_status_line) - 1);
                g_status_line[sizeof(g_status_line) - 1] = '\0';
                count = oyk_bbs_list_incoming("incoming", items, 32);
            } else {
                sprintf(feedback, "Error: %.68s", err);
            }
        } else if ((ch == 'd' || ch == 'D') && count > 0) {
            char cp[72], yn[4], err[80];
            sprintf(cp, "Delete incoming\\%s\\%s? [Y/N]: ", items[sel].uploader, items[sel].filename);
            prompt_line(cp, yn, sizeof(yn));
            if (yn[0] == 'y' || yn[0] == 'Y') {
                if (oyk_bbs_delete_incoming_file("incoming", items[sel].uploader, items[sel].filename,
                                                 err, sizeof(err))) {
                    char lmsg[80];
                    sprintf(lmsg, "Deleted incoming file %s/%s", items[sel].uploader, items[sel].filename);
                    g_node.log_message(OYK_LOG_INFO, lmsg);
                    strncpy(feedback, lmsg, sizeof(feedback) - 1);
                    feedback[sizeof(feedback) - 1] = '\0';
                    strncpy(g_status_line, lmsg, sizeof(g_status_line) - 1);
                    g_status_line[sizeof(g_status_line) - 1] = '\0';
                    count = oyk_bbs_list_incoming("incoming", items, 32);
                } else {
                    sprintf(feedback, "Error: %.68s", err);
                }
            } else {
                strcpy(feedback, "Delete cancelled.");
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
        PACKET_PROCESS_SINGLE;
        Arp::driveArp();
        Tcp::drivePackets();
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

static void rebind_dos_axudp() {
    if (g_axudp_iface_idx >= 0) {
        Udp::unregisterCallback(g_axudp_port);
        g_axudp_iface_idx = -1;
    }
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
}

static void run_dos_settings_interfaces(OykConfig* draft) {
    int sel = 0;
    char fb[78];
    strcpy(fb, "A:Add Interface  D:Remove Selected  Esc/Enter:Done");

    while (!g_break_flag) {
        PACKET_PROCESS_SINGLE;
        Arp::driveArp();
        Tcp::drivePackets();
        g_node.tick();

        if (draft->iface_count == 0) sel = 0;
        else if (sel >= draft->iface_count) sel = draft->iface_count - 1;

        vid_clear(0x17);
        char hdr[80];
        sprintf(hdr, " INTERFACE MAPPINGS (%d/%d) | Esc:Back",
                draft->iface_count, OYK_MAX_INTERFACES);
        vid_puts(0, 0, 0x30, hdr, 80);
        vid_puts(1, 0, 0x1E, " #  ID          Type         Endpoint / Device             Peers", 80);
        if (draft->iface_count == 0) {
            vid_puts(2, 0, 0x17, " (no interfaces configured - press A to add)", 80);
        } else {
            for (int i = 0; i < draft->iface_count; ++i) {
                const OykIfaceConfig& ic = draft->ifaces[i];
                char row[82], ep[32];
                if (strcmp(ic.type, "kiss-serial") == 0) {
                    sprintf(ep, "%.14s @%lu", ic.device[0] ? ic.device : "COM1",
                            static_cast<unsigned long>(ic.baud ? ic.baud : 9600UL));
                } else {
                    sprintf(ep, "%.20s:%u", ic.host[0] ? ic.host : "0.0.0.0",
                            static_cast<unsigned int>(ic.port));
                }
                sprintf(row, " %-2d %-11.11s %-12.12s %-29.29s %d",
                        i + 1, ic.id, ic.type, ep, ic.peer_count);
                vid_puts(2 + i, 0, (i == sel) ? 0x30 : 0x17, row, 80);
            }
        }
        vid_puts(23, 0, 0x1E, fb, 80);
        vid_puts(24, 0, 0x70, " A:Add Interface  D:Remove Selected  Esc/Enter:Return to Settings ", 80);

        if (!kbhit()) continue;
        int ch = getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 72 && sel > 0) --sel;
            else if (ext == 80 && sel + 1 < draft->iface_count) ++sel;
            continue;
        }
        if (ch == 27 || ch == '\r' || ch == '\n') break;
        if (ch == 'a' || ch == 'A') {
            if (draft->iface_count >= OYK_MAX_INTERFACES) {
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
            for (int i = 0; i < draft->iface_count; ++i) {
                if (strcmp(draft->ifaces[i].id, ic.id) == 0) { dup = 1; break; }
            }
            if (dup) { strcpy(fb, "Duplicate interface ID."); continue; }

            strcpy(ic.type, "axudp");
            prompt_line_prefill("Type [axudp|axip|kiss-serial|kiss-tcp|telnet]: ", ic.type, sizeof(ic.type), &ok);
            if (!ok || !ic.type[0]) { strcpy(fb, "Add cancelled."); continue; }

            if (strcmp(ic.type, "kiss-serial") == 0) {
                strcpy(ic.device, "COM1");
                prompt_line_prefill("Serial Device (e.g. COM1): ", ic.device, sizeof(ic.device), &ok);
                if (!ok) continue;
                char bb[12]; strcpy(bb, "9600");
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
            draft->ifaces[draft->iface_count++] = ic;
            sel = draft->iface_count - 1;
            sprintf(fb, "Added interface %s.", ic.id);
        } else if ((ch == 'd' || ch == 'D') && draft->iface_count > 0) {
            char yn[4], pr[48];
            sprintf(pr, "Remove interface %s? [Y/N]: ", draft->ifaces[sel].id);
            prompt_line(pr, yn, sizeof(yn));
            if (yn[0] == 'y' || yn[0] == 'Y') {
                for (int i = sel + 1; i < draft->iface_count; ++i) {
                    draft->ifaces[i - 1] = draft->ifaces[i];
                }
                draft->iface_count--;
                strcpy(fb, "Interface removed.");
            }
        }
    }
}

static void run_dos_settings() {
    static OykConfig draft;
    draft = g_node.config();

    static const char* LABELS[19] = {
        "Node Callsign",
        "Node Alias",
        "Node Welcome Msg",
        "Node Info Msg",
        "Accept Incoming (Cfg)",
        "BBS Enabled",
        "BBS SSID (0-15)",
        "BBS Callsign Override",
        "BBS Alias",
        "BBS Files Area",
        "BBS Files Path",
        "BBS Welcome Msg",
        "CHAT Enabled",
        "CHAT SSID (0-15)",
        "CHAT Callsign Override",
        "CHAT Alias",
        "CHAT Default Topic",
        "CHAT Welcome Msg",
        "Interface Mappings"
    };

    int sel = 0;
    int scroll = 0;
    const int list_h = 19;
    char fb[78];
    sprintf(fb, "Config: %.68s", draft.source_path[0] ? draft.source_path : "node.cfg");

    while (!g_break_flag) {
        PACKET_PROCESS_SINGLE;
        Arp::driveArp();
        Tcp::drivePackets();
        g_node.tick();

        if (sel < scroll) scroll = sel;
        if (sel >= scroll + list_h) scroll = sel - list_h + 1;

        vid_clear(0x17);
        vid_puts(0, 0, 0x30, " OYK NODE SETTINGS (MS-DOS)                      F2/S:Save & Apply  Esc:Cancel", 80);

        for (int r = 0; r < list_h && scroll + r < 19; ++r) {
            int idx = scroll + r;
            char val[56], cs[16], row[82];
            val[0] = '\0';
            switch (idx) {
                case 0: oyk_format_callsign(&draft.callsign, val, sizeof(val)); break;
                case 1: if (draft.alias.base[0]) oyk_format_callsign(&draft.alias, val, sizeof(val)); else strcpy(val, "(none)"); break;
                case 2: strncpy(val, draft.welcome_text, 52); val[52] = '\0'; break;
                case 3: strncpy(val, draft.info_text, 52); val[52] = '\0'; break;
                case 4: strcpy(val, draft.accept_incoming ? "[ON]" : "[OFF]"); break;
                case 5: strcpy(val, draft.bbs_enabled ? "[ON]" : "[OFF]"); break;
                case 6: { OykCallsign bc; oyk_config_resolved_bbs_callsign(&draft, &bc); oyk_format_callsign(&bc, cs, sizeof(cs)); sprintf(val, "%u (%s)", static_cast<unsigned int>(draft.bbs_ssid), cs); break; }
                case 7: if (draft.bbs_callsign.base[0]) oyk_format_callsign(&draft.bbs_callsign, val, sizeof(val)); else strcpy(val, "(auto)"); break;
                case 8: if (draft.bbs_alias.base[0]) oyk_format_callsign(&draft.bbs_alias, val, sizeof(val)); else strcpy(val, "(none)"); break;
                case 9: strcpy(val, draft.bbs_files_enabled ? "[ON]" : "[OFF]"); break;
                case 10: strncpy(val, draft.bbs_files_path, 52); val[52] = '\0'; break;
                case 11: strncpy(val, draft.bbs_welcome, 52); val[52] = '\0'; break;
                case 12: strcpy(val, draft.chat_enabled ? "[ON]" : "[OFF]"); break;
                case 13: { OykCallsign cc; oyk_config_resolved_chat_callsign(&draft, &cc); oyk_format_callsign(&cc, cs, sizeof(cs)); sprintf(val, "%u (%s)", static_cast<unsigned int>(draft.chat_ssid), cs); break; }
                case 14: if (draft.chat_callsign.base[0]) oyk_format_callsign(&draft.chat_callsign, val, sizeof(val)); else strcpy(val, "(auto)"); break;
                case 15: if (draft.chat_alias.base[0]) oyk_format_callsign(&draft.chat_alias, val, sizeof(val)); else strcpy(val, "(none)"); break;
                case 16: strncpy(val, draft.chat_topic, 52); val[52] = '\0'; break;
                case 17: strncpy(val, draft.chat_welcome, 52); val[52] = '\0'; break;
                case 18: sprintf(val, "%d configured [Enter to Add/Remove]", draft.iface_count); break;
                default: break;
            }
            sprintf(row, " %-23.23s: %-52.52s", LABELS[idx], val);
            vid_puts(1 + r, 0, (idx == sel) ? 0x30 : 0x17, row, 80);
        }

        vid_puts(23, 0, 0x1E, fb, 80);
        vid_puts(24, 0, 0x70, " Up/Dn:Select  Enter/Space:Edit  I:Interfaces  S/F2:Save & Apply  Esc:Cancel ", 80);

        if (!kbhit()) continue;
        int ch = getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 72 && sel > 0) --sel;
            else if (ext == 80 && sel + 1 < 19) ++sel;
            else if (ext == 60) ch = 'S'; /* F2 */
            else continue;
        }
        if (ch == 27) break;
        if (ch == 'i' || ch == 'I') {
            run_dos_settings_interfaces(&draft);
            continue;
        }
        if (ch == 's' || ch == 'S') {
            static OykConfigApplyResult res;
            if (!g_node.apply_config(&draft, 1, &res)) {
                sprintf(fb, "Error: %.68s", res.error);
                continue;
            }
            if (res.interfaces_changed) {
                rebind_dos_axudp();
            }
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
        if (ch == '\r' || ch == '\n' || ch == ' ') {
            if (sel == 4) { draft.accept_incoming = !draft.accept_incoming; continue; }
            if (sel == 5) { draft.bbs_enabled = !draft.bbs_enabled; continue; }
            if (sel == 9) { draft.bbs_files_enabled = !draft.bbs_files_enabled; continue; }
            if (sel == 12) { draft.chat_enabled = !draft.chat_enabled; continue; }
            if (sel == 18) { run_dos_settings_interfaces(&draft); continue; }

            char ed[96];
            ed[0] = '\0';
            switch (sel) {
                case 0: oyk_format_callsign(&draft.callsign, ed, sizeof(ed)); break;
                case 1: if (draft.alias.base[0]) oyk_format_callsign(&draft.alias, ed, sizeof(ed)); break;
                case 2: strncpy(ed, draft.welcome_text, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; break;
                case 3: strncpy(ed, draft.info_text, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; break;
                case 6: sprintf(ed, "%u", static_cast<unsigned int>(draft.bbs_ssid)); break;
                case 7: if (draft.bbs_callsign.base[0]) oyk_format_callsign(&draft.bbs_callsign, ed, sizeof(ed)); break;
                case 8: if (draft.bbs_alias.base[0]) oyk_format_callsign(&draft.bbs_alias, ed, sizeof(ed)); break;
                case 10: strncpy(ed, draft.bbs_files_path, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; break;
                case 11: strncpy(ed, draft.bbs_welcome, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; break;
                case 13: sprintf(ed, "%u", static_cast<unsigned int>(draft.chat_ssid)); break;
                case 14: if (draft.chat_callsign.base[0]) oyk_format_callsign(&draft.chat_callsign, ed, sizeof(ed)); break;
                case 15: if (draft.chat_alias.base[0]) oyk_format_callsign(&draft.chat_alias, ed, sizeof(ed)); break;
                case 16: strncpy(ed, draft.chat_topic, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; break;
                case 17: strncpy(ed, draft.chat_welcome, sizeof(ed) - 1); ed[sizeof(ed) - 1] = '\0'; break;
                default: break;
            }
            char pr[36];
            sprintf(pr, "%s: ", LABELS[sel]);
            int ok = 0;
            prompt_line_prefill(pr, ed, sizeof(ed), &ok);
            if (!ok) continue;

            if (sel == 0) {
                if (!oyk_parse_callsign_with_default_ssid(ed, OYK_DEFAULT_NODE_SSID, &draft.callsign)) strcpy(fb, "Invalid node callsign.");
            } else if (sel == 1) {
                if (!ed[0]) memset(&draft.alias, 0, sizeof(draft.alias));
                else if (!oyk_parse_callsign(ed, &draft.alias)) strcpy(fb, "Invalid alias.");
            } else if (sel == 2) {
                strncpy(draft.welcome_text, ed, sizeof(draft.welcome_text) - 1); draft.welcome_text[sizeof(draft.welcome_text) - 1] = '\0';
            } else if (sel == 3) {
                strncpy(draft.info_text, ed, sizeof(draft.info_text) - 1); draft.info_text[sizeof(draft.info_text) - 1] = '\0';
            } else if (sel == 6) {
                int sv = atoi(ed);
                if (sv >= 0 && sv <= 15) draft.bbs_ssid = static_cast<oyk_u8>(sv);
                else strcpy(fb, "SSID must be 0..15.");
            } else if (sel == 7) {
                if (!ed[0]) memset(&draft.bbs_callsign, 0, sizeof(draft.bbs_callsign));
                else if (!oyk_parse_callsign_with_default_ssid(ed, draft.bbs_ssid, &draft.bbs_callsign)) strcpy(fb, "Invalid BBS callsign.");
            } else if (sel == 8) {
                if (!ed[0]) memset(&draft.bbs_alias, 0, sizeof(draft.bbs_alias));
                else if (!oyk_parse_callsign(ed, &draft.bbs_alias)) strcpy(fb, "Invalid BBS alias.");
            } else if (sel == 10) {
                if (ed[0]) { strncpy(draft.bbs_files_path, ed, sizeof(draft.bbs_files_path) - 1); draft.bbs_files_path[sizeof(draft.bbs_files_path) - 1] = '\0'; }
            } else if (sel == 11) {
                strncpy(draft.bbs_welcome, ed, sizeof(draft.bbs_welcome) - 1); draft.bbs_welcome[sizeof(draft.bbs_welcome) - 1] = '\0';
            } else if (sel == 13) {
                int sv = atoi(ed);
                if (sv >= 0 && sv <= 15) draft.chat_ssid = static_cast<oyk_u8>(sv);
                else strcpy(fb, "SSID must be 0..15.");
            } else if (sel == 14) {
                if (!ed[0]) memset(&draft.chat_callsign, 0, sizeof(draft.chat_callsign));
                else if (!oyk_parse_callsign_with_default_ssid(ed, draft.chat_ssid, &draft.chat_callsign)) strcpy(fb, "Invalid CHAT callsign.");
            } else if (sel == 15) {
                if (!ed[0]) memset(&draft.chat_alias, 0, sizeof(draft.chat_alias));
                else if (!oyk_parse_callsign(ed, &draft.chat_alias)) strcpy(fb, "Invalid CHAT alias.");
            } else if (sel == 16) {
                if (ed[0]) { strncpy(draft.chat_topic, ed, sizeof(draft.chat_topic) - 1); draft.chat_topic[sizeof(draft.chat_topic) - 1] = '\0'; }
            } else if (sel == 17) {
                strncpy(draft.chat_welcome, ed, sizeof(draft.chat_welcome) - 1); draft.chat_welcome[sizeof(draft.chat_welcome) - 1] = '\0';
            }
        }
    }
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
        run_dos_settings();
    } else if (chosen == 5) {
        char id_buf[12];
        prompt_line("Terminate session ID (Esc=cancel): ", id_buf, sizeof(id_buf));
        if (id_buf[0]) {
            g_node.terminate_session(static_cast<oyk_u32>(atol(id_buf)),
                                     g_status_line, sizeof(g_status_line));
        }
    } else if (chosen == 6) {
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
    } else if (chosen == 7) {
        if (confirm_dos_exit()) {
            *running = 0;
        }
    }
}

static void open_dos_node_menu(int* running) {
    static const char* ITEMS[8] = {
        " [A] Allow Incoming Connections ",
        " [D] Deny Incoming Connections  ",
        " [L] Local Terminal...          ",
        " [F] Approve Incoming Files...  ",
        " [S] Settings...                ",
        " [T] Terminate Connection...    ",
        " [B] Ban / Unban Callsign...    ",
        " [X] Exit                       "
    };
    static const int ITEM_ROWS[8] = { 2, 3, 5, 6, 7, 8, 9, 11 };

    int sel = 0;
    int chosen = -1;
    while (chosen < 0 && !g_break_flag) {
        vid_puts(1, 1, 0x70, "+--------------------------------+", 34);
        vid_puts(4, 1, 0x70, "+--------------------------------+", 34);
        vid_puts(10, 1, 0x70, "+--------------------------------+", 34);
        vid_puts(12, 1, 0x70, "+--------------------------------+", 34);
        for (int i = 0; i < 8; ++i) {
            vid_puts(ITEM_ROWS[i], 1, 0x70, "|", 1);
            vid_puts(ITEM_ROWS[i], 2, (i == sel) ? 0x1F : 0x70, ITEMS[i], 32);
            vid_puts(ITEM_ROWS[i], 34, 0x70, "|", 1);
        }

        int ch = getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = getch();
            if (ext == 72) sel = (sel + 7) % 8;      /* Up arrow */
            else if (ext == 80) sel = (sel + 1) % 8; /* Down arrow */
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
        } else if (ch == 's' || ch == 'S') {
            chosen = 4;
        } else if (ch == 't' || ch == 'T') {
            chosen = 5;
        } else if (ch == 'b' || ch == 'B') {
            chosen = 6;
        } else if (ch == 'x' || ch == 'X' || ch == 'q' || ch == 'Q') {
            chosen = 7;
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

    if (headless) {
        char call_s[16];
        oyk_format_callsign(&cfg.callsign, call_s, sizeof(call_s));
        printf("OYK Packet Node (MS-DOS) %s running in headless mode. Press Ctrl+C to stop.\n", call_s);
        int printed_logs = 0;
        while (!g_break_flag) {
            PACKET_PROCESS_SINGLE;
            Arp::driveArp();
            Tcp::drivePackets();
            g_node.tick();
            while (printed_logs < g_node.console_log_count()) {
                printf("%s\n", g_node.console_log_line(printed_logs++));
            }
        }
    } else {
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
                } else if (ch == 's' || ch == 'S') {
                    run_dos_menu_action(4, &running);
                } else if (ch == 't' || ch == 'T') {
                    run_dos_menu_action(5, &running);
                } else if (ch == 'b' || ch == 'B') {
                    run_dos_menu_action(6, &running);
                } else if (ch == 'q' || ch == 'Q' || ch == 'x' || ch == 'X') {
                    run_dos_menu_action(7, &running);
                }
                render_dos_screen();
            }

            if (Timer_diff(last_draw, TIMER_GET_CURRENT()) >= TIMER_MS_TO_TICKS(250)) {
                last_draw = TIMER_GET_CURRENT();
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
