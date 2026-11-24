/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * OYK Amateur Radio Packet Node - Native Windows GUI Port
 *
 * Compatible with:
 *   - 16-bit Windows 3.1 (OpenWatcom v2/1.9, Winsock 1.1)
 *   - 32-bit Windows 95, 98, ME & Windows NT 3.51 through Windows 11 (x86)
 *   - 64-bit Windows XP x64 through Windows 11 (x64)
 *
 * Uses classic Win16/Win32 USER/GDI + Winsock 1.1 APIs (no Vista+ dependencies).
 * Supports AXUDP (UDP) and incoming Telnet (TCP, RFC 854) interfaces.
 * Layout mirrors the Linux ncurses, GTK+ 2.0, and MS-DOS interfaces.
 */

#include <windows.h>
#include <winsock.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../compat/oyk_portable.hpp"
#include "resource.h"

#if defined(_WIN32) || defined(_WIN64)
  #define OYK_DLG_RET         INT_PTR CALLBACK
  #define OYK_WND_RET         LRESULT CALLBACK
  #define OYK_CMD_ID(wp, lp)  LOWORD(wp)
  #define OYK_HMENU(id)       ((HMENU)(INT_PTR)(id))
  #define OYK_HBRUSH(idx)     ((HBRUSH)(INT_PTR)(idx))
  #define OYK_WPARAM_H(h)     ((WPARAM)(h))
  #define OYK_LPARAM_STR(s)   ((LPARAM)(LPCSTR)(s))
#else
  /* 16-bit Windows 3.1 */
  #define OYK_DLG_RET         BOOL FAR PASCAL __export
  #define OYK_WND_RET         LRESULT FAR PASCAL __export
  #define OYK_CMD_ID(wp, lp)  ((WORD)(wp))
  #define OYK_HMENU(id)       ((HMENU)(id))
  #define OYK_HBRUSH(idx)     ((HBRUSH)(idx))
  #define OYK_WPARAM_H(h)     ((WPARAM)(h))
  #define OYK_LPARAM_STR(s)   ((LPARAM)(LPSTR)(s))
  #ifndef INVALID_SOCKET
    #define INVALID_SOCKET    ((SOCKET)(~0))
  #endif
  #ifndef BST_CHECKED
    #define BST_CHECKED       1
  #endif
  #ifndef MAKEWORD
    #define MAKEWORD(a, b)    ((WORD)(((BYTE)(a)) | (((WORD)((BYTE)(b))) << 8)))
  #endif
#endif

#ifndef INADDR_NONE
  #define INADDR_NONE         0xFFFFFFFFUL
#endif

#define OYK_MAX_TELNET_CLIENTS 12

/* RFC 854 Telnet command bytes */
#define OYK_TELNET_IAC     255U
#define OYK_TELNET_DONT    254U
#define OYK_TELNET_DO      253U
#define OYK_TELNET_WONT    252U
#define OYK_TELNET_WILL    251U
#define OYK_TELNET_SB      250U
#define OYK_TELNET_SE      240U
#define OYK_TELNET_OPT_SGA   3U

enum WinIfaceKind {
    WIN_IF_NONE = 0,
    WIN_IF_AXUDP,
    WIN_IF_TELNET
};

struct WinIfaceNet {
    WinIfaceKind kind;
    SOCKET sock;
};

enum TelnetStage {
    TSTAGE_LOGIN = 0,
    TSTAGE_CONNECTING,
    TSTAGE_CONNECTED
};

enum TelnetIacState {
    TIAC_NORMAL = 0,
    TIAC_IAC,
    TIAC_OPT,
    TIAC_SUBNEG,
    TIAC_SUBNEG_IAC
};

struct WinTelnetClient {
    int in_use;
    SOCKET sock;
    int iface_idx;
    oyk_u16 handle;
    oyk_u32 peer_ip;
    oyk_u16 peer_port;
    TelnetStage stage;
    TelnetIacState iac_state;
    oyk_u8 iac_cmd;
    int last_was_cr;
    int login_attempts;
    OykCallsign callsign;
    oyk_u8 vs;
    oyk_u8 vr;
    int line_len;
    char line_buf[256];
};

static HINSTANCE g_hinst = NULL;
static HWND g_hwnd_main = NULL;
static HWND g_ctl_header = NULL;
static HWND g_ctl_badge = NULL;
static HWND g_ctl_ifaces = NULL;
static HWND g_ctl_heard = NULL;
static HWND g_ctl_sessions = NULL;
static HWND g_ctl_logs = NULL;
static HWND g_ctl_status = NULL;
static HFONT g_mono_font = NULL;

static OykPortableNode g_node;
static WinIfaceNet g_ifaces[OYK_MAX_INTERFACES];
static WinTelnetClient g_telnet_clients[OYK_MAX_TELNET_CLIENTS];
static int g_last_log_count = -1;
static char g_status_msg[128] = "Ready.";

static int win_strieq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (toupper(static_cast<unsigned char>(*a)) !=
            toupper(static_cast<unsigned char>(*b))) {
            return 0;
        }
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

static int resolve_bind_addr(const char* host, unsigned long* addr_out) {
    if (!host || !host[0] || strcmp(host, "0.0.0.0") == 0 || strcmp(host, "*") == 0) {
        *addr_out = INADDR_ANY;
        return 1;
    }
    unsigned long ip = inet_addr(host);
    if (ip == INADDR_NONE) return 0;
    *addr_out = ip;
    return 1;
}

/* ==================== Winsock 1.1 Telnet & AXUDP Helpers ==================== */

static void telnet_send_raw(SOCKET sock, const void* data, int len) {
    if (sock == INVALID_SOCKET || !data || len <= 0) return;
    const char* ptr = static_cast<const char*>(data);
    int sent = 0;
    while (sent < len) {
        int n = send(sock, ptr + sent, len - sent, 0);
        if (n > 0) {
            sent += n;
        } else {
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
                fd_set wfds;
                FD_ZERO(&wfds);
                FD_SET(sock, &wfds);
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 100000;
                if (select(0, NULL, &wfds, NULL, &tv) > 0) continue;
            }
            break;
        }
    }
}

static void telnet_send_str(SOCKET sock, const char* s) {
    if (!s || !s[0]) return;
    telnet_send_raw(sock, s, static_cast<int>(strlen(s)));
}

static void telnet_close_client(WinTelnetClient* c) {
    if (!c || !c->in_use) return;
    if (c->sock != INVALID_SOCKET) {
        closesocket(c->sock);
        c->sock = INVALID_SOCKET;
    }
    c->in_use = 0;
}

static WinTelnetClient* telnet_find_client(int iface_idx, oyk_u16 handle, const OykCallsign* dst) {
    if (handle >= 1 && handle <= OYK_MAX_TELNET_CLIENTS) {
        WinTelnetClient* c = &g_telnet_clients[handle - 1];
        if (c->in_use && c->iface_idx == iface_idx) return c;
    }
    if (dst) {
        for (int i = 0; i < OYK_MAX_TELNET_CLIENTS; ++i) {
            WinTelnetClient* c = &g_telnet_clients[i];
            if (c->in_use && c->iface_idx == iface_idx &&
                c->stage != TSTAGE_LOGIN && oyk_callsign_eq(&c->callsign, dst)) {
                return c;
            }
        }
    }
    return NULL;
}

static void telnet_accept_client(int iface_idx, SOCKET listen_sock) {
    struct sockaddr_in peer;
    int peer_len = sizeof(peer);
    SOCKET cfd = accept(listen_sock, reinterpret_cast<struct sockaddr*>(&peer), &peer_len);
    if (cfd == INVALID_SOCKET) return;

    int one = 1;
    setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
    u_long nb = 1;
    ioctlsocket(cfd, FIONBIO, &nb);

    int slot = -1;
    for (int i = 0; i < OYK_MAX_TELNET_CLIENTS; ++i) {
        if (!g_telnet_clients[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        telnet_send_str(cfd, "*** Server busy (too many telnet connections).\r\n");
        closesocket(cfd);
        return;
    }

    WinTelnetClient* c = &g_telnet_clients[slot];
    memset(c, 0, sizeof(*c));
    c->in_use = 1;
    c->sock = cfd;
    c->iface_idx = iface_idx;
    c->handle = static_cast<oyk_u16>(slot + 1);
    c->peer_ip = static_cast<oyk_u32>(peer.sin_addr.s_addr);
    c->peer_port = ntohs(peer.sin_port);
    c->stage = TSTAGE_LOGIN;
    c->iac_state = TIAC_NORMAL;

    char node_s[16], banner[128];
    oyk_format_callsign(&g_node.config().callsign, node_s, sizeof(node_s));
    sprintf(banner, "OYK Packet Node Telnet Access (%s)\r\nCallsign: ", node_s);
    telnet_send_str(c->sock, banner);
}

static void telnet_process_clean_byte(WinTelnetClient* c, oyk_u8 b) {
    if (!c->in_use) return;

    /* Collapse Telnet NVT CR-LF and CR-NUL to a single line submission */
    if (c->last_was_cr) {
        c->last_was_cr = 0;
        if (b == '\n' || b == '\0') return;
    }

    if (b == '\r' || b == '\n') {
        if (b == '\r') c->last_was_cr = 1;
        c->line_buf[c->line_len] = '\0';

        if (c->stage == TSTAGE_LOGIN) {
            OykCallsign parsed;
            int ok = oyk_parse_callsign(c->line_buf, &parsed);
            c->line_len = 0;
            c->line_buf[0] = '\0';

            if (!ok) {
                c->login_attempts++;
                if (c->login_attempts >= 3) {
                    telnet_send_str(c->sock, "Too many invalid attempts. Goodbye.\r\n");
                    telnet_close_client(c);
                } else {
                    telnet_send_str(c->sock, "Invalid callsign. Callsign: ");
                }
                return;
            }

            /* Reject duplicate simultaneous callsign on this Telnet interface */
            for (int i = 0; i < OYK_MAX_TELNET_CLIENTS; ++i) {
                WinTelnetClient* o = &g_telnet_clients[i];
                if (o != c && o->in_use && o->iface_idx == c->iface_idx &&
                    o->stage != TSTAGE_LOGIN && oyk_callsign_eq(&o->callsign, &parsed)) {
                    telnet_send_str(c->sock, "*** Callsign already connected on this telnet port.\r\n");
                    telnet_close_client(c);
                    return;
                }
            }

            c->callsign = parsed;
            c->stage = TSTAGE_CONNECTING;
            c->vs = 0;
            c->vr = 0;

            static OykFrame sabm;
            memset(&sabm, 0, sizeof(sabm));
            sabm.dst = g_node.config().callsign;
            sabm.src = c->callsign;
            sabm.is_command = 1;
            sabm.control = 0x3FU; /* SABM P=1 */
            g_node.on_rx_frame(c->iface_idx, &sabm, c->peer_ip, c->peer_port, c->handle);
            return;
        }

        if (c->stage == TSTAGE_CONNECTED) {
            static char line_copy[260];
            int llen = c->line_len;
            if (llen > 254) llen = 254;
            memcpy(line_copy, c->line_buf, static_cast<size_t>(llen));
            line_copy[llen++] = '\r';
            line_copy[llen] = '\0';
            c->line_len = 0;
            c->line_buf[0] = '\0';

            int pos = 0;
            while (pos < llen && c->in_use && c->stage == TSTAGE_CONNECTED) {
                int chunk = llen - pos;
                if (chunk > 200) chunk = 200;
                static OykFrame ifr;
                memset(&ifr, 0, sizeof(ifr));
                ifr.dst = g_node.config().callsign;
                ifr.src = c->callsign;
                ifr.is_command = 1;
                ifr.control = static_cast<oyk_u8>(((c->vs & 0x07U) << 1) | ((c->vr & 0x07U) << 5));
                ifr.has_pid = 1;
                ifr.pid = 0xF0U;
                ifr.info_len = static_cast<oyk_u16>(chunk);
                memcpy(ifr.info, line_copy + pos, static_cast<size_t>(chunk));
                c->vs = static_cast<oyk_u8>((c->vs + 1) & 0x07U);
                g_node.on_rx_frame(c->iface_idx, &ifr, c->peer_ip, c->peer_port, c->handle);
                pos += chunk;
            }
        }
        return;
    }

    if (b == '\b' || b == 0x7FU) {
        if (c->line_len > 0) c->line_len--;
        return;
    }

    if (b >= 0x20U && b < 0x7FU) {
        if (c->line_len < static_cast<int>(sizeof(c->line_buf) - 2)) {
            c->line_buf[c->line_len++] = static_cast<char>(b);
        }
    }
}

static void telnet_feed_bytes(WinTelnetClient* c, const oyk_u8* data, int len) {
    for (int i = 0; i < len && c->in_use; ++i) {
        oyk_u8 b = data[i];
        switch (c->iac_state) {
            case TIAC_NORMAL:
                if (b == OYK_TELNET_IAC) {
                    c->iac_state = TIAC_IAC;
                } else {
                    telnet_process_clean_byte(c, b);
                }
                break;
            case TIAC_IAC:
                if (b == OYK_TELNET_IAC) {
                    c->iac_state = TIAC_NORMAL;
                } else if (b == OYK_TELNET_WILL || b == OYK_TELNET_WONT ||
                           b == OYK_TELNET_DO || b == OYK_TELNET_DONT) {
                    c->iac_cmd = b;
                    c->iac_state = TIAC_OPT;
                } else if (b == OYK_TELNET_SB) {
                    c->iac_state = TIAC_SUBNEG;
                } else {
                    c->iac_state = TIAC_NORMAL;
                }
                break;
            case TIAC_OPT: {
                if (c->iac_cmd == OYK_TELNET_WILL) {
                    oyk_u8 resp[3];
                    resp[0] = OYK_TELNET_IAC;
                    resp[1] = (b == OYK_TELNET_OPT_SGA) ? OYK_TELNET_DO : OYK_TELNET_DONT;
                    resp[2] = b;
                    telnet_send_raw(c->sock, resp, 3);
                } else if (c->iac_cmd == OYK_TELNET_DO) {
                    oyk_u8 resp[3];
                    resp[0] = OYK_TELNET_IAC;
                    resp[1] = (b == OYK_TELNET_OPT_SGA) ? OYK_TELNET_WILL : OYK_TELNET_WONT;
                    resp[2] = b;
                    telnet_send_raw(c->sock, resp, 3);
                }
                c->iac_state = TIAC_NORMAL;
                break;
            }
            case TIAC_SUBNEG:
                if (b == OYK_TELNET_IAC) c->iac_state = TIAC_SUBNEG_IAC;
                break;
            case TIAC_SUBNEG_IAC:
                if (b == OYK_TELNET_SE) {
                    c->iac_state = TIAC_NORMAL;
                } else if (b != OYK_TELNET_IAC) {
                    c->iac_state = TIAC_SUBNEG;
                }
                break;
        }
    }
}

static int win_tx_callback(int iface_idx, const OykFrame* frame,
                           oyk_u32 peer_ip, oyk_u16 peer_port,
                           oyk_u16 peer_handle, void* /*ctx*/) {
    if (iface_idx < 0 || iface_idx >= OYK_MAX_INTERFACES || !frame) return 0;

    if (g_ifaces[iface_idx].kind == WIN_IF_AXUDP) {
        SOCKET sock = g_ifaces[iface_idx].sock;
        if (sock == INVALID_SOCKET || peer_ip == 0 || peer_port == 0) return 0;

        static oyk_u8 raw[320];
        size_t n = oyk_ax25_encode(frame, raw, sizeof(raw) - 2);
        if (n == 0) return 0;
        oyk_u16 fcs = oyk_crc16_fcs(raw, n);
        raw[n++] = static_cast<oyk_u8>(fcs & 0xFFU);
        raw[n++] = static_cast<oyk_u8>((fcs >> 8) & 0xFFU);

        struct sockaddr_in dst;
        memset(&dst, 0, sizeof(dst));
        dst.sin_family = AF_INET;
        dst.sin_addr.s_addr = peer_ip;
        dst.sin_port = htons(peer_port);

        int sent = sendto(sock, reinterpret_cast<const char*>(raw),
                          static_cast<int>(n), 0,
                          reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst));
        return (sent == static_cast<int>(n)) ? 1 : 0;
    }

    if (g_ifaces[iface_idx].kind == WIN_IF_TELNET) {
        OykFrameType ft = oyk_frame_type(frame);
        if (ft == OYK_FT_UI) return 1;

        WinTelnetClient* c = telnet_find_client(iface_idx, peer_handle, &frame->dst);
        if (!c) return 0;

        switch (ft) {
            case OYK_FT_UA:
                if (c->stage == TSTAGE_CONNECTING) {
                    c->stage = TSTAGE_CONNECTED;
                }
                return 1;
            case OYK_FT_DM:
                telnet_send_str(c->sock,
                                "\r\n*** Connection rejected (banned or incoming disabled).\r\n");
                telnet_close_client(c);
                return 1;
            case OYK_FT_DISC:
                telnet_send_str(c->sock, "\r\n*** Disconnected.\r\n");
                telnet_close_client(c);
                return 1;
            case OYK_FT_I: {
                static char out[600];
                int opos = 0;
                for (oyk_u16 i = 0; i < frame->info_len && opos < static_cast<int>(sizeof(out) - 2); ++i) {
                    char ch = static_cast<char>(frame->info[i]);
                    if (ch == '\r') {
                        out[opos++] = '\r';
                        if (i + 1 >= frame->info_len || frame->info[i + 1] != '\n') {
                            out[opos++] = '\n';
                        }
                    } else {
                        out[opos++] = ch;
                    }
                }
                telnet_send_raw(c->sock, out, opos);
                c->vr = static_cast<oyk_u8>((((frame->control >> 1) & 0x07U) + 1U) & 0x07U);
                return 1;
            }
            case OYK_FT_RR:
            case OYK_FT_RNR:
            case OYK_FT_REJ:
                return 1;
            default:
                return 0;
        }
    }

    return 0;
}

static void pump_winsock() {
    static oyk_u8 buf[1024];
    static OykFrame frame;

    /* 1. Pump all AXUDP and Telnet listening sockets */
    for (int idx = 0; idx < g_node.iface_count() && idx < OYK_MAX_INTERFACES; ++idx) {
        SOCKET sock = g_ifaces[idx].sock;
        if (sock == INVALID_SOCKET) continue;

        if (g_ifaces[idx].kind == WIN_IF_AXUDP) {
            for (int iter = 0; iter < 16; ++iter) {
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(sock, &rfds);
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 0;
                if (select(0, &rfds, NULL, NULL, &tv) <= 0) break;

                struct sockaddr_in src;
                int slen = sizeof(src);
                int n = recvfrom(sock, reinterpret_cast<char*>(buf),
                                 sizeof(buf), 0,
                                 reinterpret_cast<struct sockaddr*>(&src), &slen);
                if (n <= 2) break;
                if (!oyk_verify_fcs(buf, static_cast<size_t>(n))) continue;
                if (!oyk_ax25_decode(buf, static_cast<size_t>(n - 2), &frame)) continue;

                g_node.on_rx_frame(idx, &frame,
                                   static_cast<oyk_u32>(src.sin_addr.s_addr),
                                   ntohs(src.sin_port), 0);
            }
        } else if (g_ifaces[idx].kind == WIN_IF_TELNET) {
            for (int iter = 0; iter < 4; ++iter) {
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(sock, &rfds);
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 0;
                if (select(0, &rfds, NULL, NULL, &tv) <= 0) break;
                telnet_accept_client(idx, sock);
            }
        }
    }

    /* 2. Pump connected Telnet client sockets */
    for (int i = 0; i < OYK_MAX_TELNET_CLIENTS; ++i) {
        WinTelnetClient* c = &g_telnet_clients[i];
        if (!c->in_use || c->sock == INVALID_SOCKET) continue;

        for (int iter = 0; iter < 8 && c->in_use; ++iter) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(c->sock, &rfds);
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 0;
            if (select(0, &rfds, NULL, NULL, &tv) <= 0) break;

            int n = recv(c->sock, reinterpret_cast<char*>(buf), sizeof(buf), 0);
            if (n > 0) {
                telnet_feed_bytes(c, buf, n);
            } else {
                if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) break;
                int notify_disc = (c->stage != TSTAGE_LOGIN) ? 1 : 0;
                int if_idx = c->iface_idx;
                OykCallsign cs = c->callsign;
                oyk_u32 pip = c->peer_ip;
                oyk_u16 pport = c->peer_port;
                oyk_u16 hdl = c->handle;
                telnet_close_client(c);
                if (notify_disc) {
                    memset(&frame, 0, sizeof(frame));
                    frame.dst = g_node.config().callsign;
                    frame.src = cs;
                    frame.is_command = 1;
                    frame.control = 0x53U; /* DISC P=1 */
                    g_node.on_rx_frame(if_idx, &frame, pip, pport, hdl);
                }
                break;
            }
        }
    }
}

/* ==================== UI Refresh & Dialogs ==================== */

static void set_status(const char* text) {
    strncpy(g_status_msg, text, sizeof(g_status_msg) - 1);
    g_status_msg[sizeof(g_status_msg) - 1] = '\0';
    if (g_ctl_status) SetWindowText(g_ctl_status, g_status_msg);
}

static void refresh_gui() {
    char buf[160];
    char call_s[16];
    oyk_format_callsign(&g_node.config().callsign, call_s, sizeof(call_s));

    sprintf(buf, "OYK PACKET NODE: %s  |  Uptime: %lus  |  Active Connections: %d  |  Banned: %d",
            call_s,
            static_cast<unsigned long>(g_node.uptime_seconds()),
            g_node.total_active_connections(),
            g_node.ban_count());
    SetWindowText(g_ctl_header, buf);

    SetWindowText(g_ctl_badge, g_node.accept_incoming() ? "[INCOMING: ALLOW]" : "[INCOMING: DENY]");

    /* Radio Interfaces (active connections per interface) */
    SendMessage(g_ctl_ifaces, LB_RESETCONTENT, 0, 0);
    SendMessage(g_ctl_ifaces, LB_ADDSTRING, 0,
                OYK_LPARAM_STR("Port      Type     Conn  Status          RX/TX"));
    for (int i = 0; i < g_node.iface_count(); ++i) {
        const OykIfaceRuntime& r = g_node.iface(i);
        sprintf(buf, "%-9.9s %-8.8s %-5d %-15.15s %lu/%lu",
                r.cfg.id, r.cfg.type, g_node.active_connections_on(i), r.status,
                static_cast<unsigned long>(r.rx_frames),
                static_cast<unsigned long>(r.tx_frames));
        SendMessage(g_ctl_ifaces, LB_ADDSTRING, 0, OYK_LPARAM_STR(buf));
    }

    /* Last 5 New Callsigns Heard */
    SendMessage(g_ctl_heard, LB_RESETCONTENT, 0, 0);
    SendMessage(g_ctl_heard, LB_ADDSTRING, 0,
                OYK_LPARAM_STR("#  Callsign   Port      Heard"));
    for (int i = 0; i < g_node.new_heard_count(); ++i) {
        const OykNewHeard& nh = g_node.new_heard(i);
        sprintf(buf, "%d. %-10.10s %-9.9s %s", i + 1, nh.callsign, nh.iface_id, nh.time_hms);
        SendMessage(g_ctl_heard, LB_ADDSTRING, 0, OYK_LPARAM_STR(buf));
    }

    /* Active Connections */
    SendMessage(g_ctl_sessions, LB_RESETCONTENT, 0, 0);
    SendMessage(g_ctl_sessions, LB_ADDSTRING, 0,
                OYK_LPARAM_STR("ID    Callsign    Port      Since     Mode"));
    for (int i = 0; i < g_node.max_sessions(); ++i) {
        const OykSession& s = g_node.session_slot(i);
        if (s.state == OYK_SESS_CONNECTED) {
            char cs[12];
            oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
            sprintf(buf, "#%-4lu %-11.11s %-9.9s %-9.9s %s",
                    static_cast<unsigned long>(s.id), cs,
                    g_node.iface(s.iface_idx).cfg.id, s.since_hms,
                    s.restricted_ban ? "BAN NOTICE" : "NODE SHELL");
            SendMessage(g_ctl_sessions, LB_ADDSTRING, 0, OYK_LPARAM_STR(buf));
        }
    }

    /* Node Activity Log */
    int log_n = g_node.console_log_count();
    if (log_n != g_last_log_count) {
        g_last_log_count = log_n;
        SendMessage(g_ctl_logs, LB_RESETCONTENT, 0, 0);
        for (int i = 0; i < log_n; ++i) {
            SendMessage(g_ctl_logs, LB_ADDSTRING, 0,
                        OYK_LPARAM_STR(g_node.console_log_line(i)));
        }
        if (log_n > 0) {
            SendMessage(g_ctl_logs, LB_SETTOPINDEX, static_cast<WPARAM>(log_n - 1), 0);
        }
    }
}

OYK_DLG_RET TerminateDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    static oyk_u32 s_ids[OYK_MAX_SESSIONS];
    static int s_count = 0;

    switch (msg) {
        case WM_INITDIALOG: {
            HWND lb = GetDlgItem(hdlg, IDC_TERM_LIST);
            s_count = 0;
            for (int i = 0; i < g_node.max_sessions(); ++i) {
                const OykSession& s = g_node.session_slot(i);
                if (s.state == OYK_SESS_CONNECTED) {
                    char cs[12], row[80];
                    oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
                    sprintf(row, "#%lu  %s on %s", static_cast<unsigned long>(s.id),
                            cs, g_node.iface(s.iface_idx).cfg.id);
                    SendMessage(lb, LB_ADDSTRING, 0, OYK_LPARAM_STR(row));
                    s_ids[s_count++] = s.id;
                }
            }
            if (s_count > 0) SendMessage(lb, LB_SETCURSEL, 0, 0);
            return TRUE;
        }
        case WM_COMMAND: {
            WORD id = OYK_CMD_ID(wp, lp);
            if (id == IDOK) {
                int sel = static_cast<int>(SendDlgItemMessage(hdlg, IDC_TERM_LIST, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < s_count) {
                    char fb[96];
                    g_node.terminate_session(s_ids[sel], fb, sizeof(fb));
                    set_status(fb);
                }
                EndDialog(hdlg, IDOK);
                return TRUE;
            }
            if (id == IDCANCEL) {
                EndDialog(hdlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

static void fill_ban_listbox(HWND lb) {
    SendMessage(lb, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_node.ban_count(); ++i) {
        const OykBanEntry& e = g_node.ban_entry(i);
        char row[140];
        sprintf(row, "%-10s [%s] (%s) %s",
                e.key, (e.flags & OYK_BAN_HARD_BAN) ? "HARD" : "SOFT",
                e.lift_at, e.reason);
        SendMessage(lb, LB_ADDSTRING, 0, OYK_LPARAM_STR(row));
    }
}

OYK_DLG_RET BanDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    switch (msg) {
        case WM_INITDIALOG:
            SetDlgItemText(hdlg, IDC_BAN_REASON, "Banned by sysop.");
            CheckDlgButton(hdlg, IDC_BAN_APPEAL, BST_CHECKED);
            fill_ban_listbox(GetDlgItem(hdlg, IDC_BAN_LIST));
            return TRUE;
        case WM_COMMAND: {
            WORD id = OYK_CMD_ID(wp, lp);
            if (id == IDC_BAN_ADD) {
                char call[16], reason[96], fb[96];
                GetDlgItemText(hdlg, IDC_BAN_CALL, call, sizeof(call));
                GetDlgItemText(hdlg, IDC_BAN_REASON, reason, sizeof(reason));
                oyk_u32 flags = 0;
                if (IsDlgButtonChecked(hdlg, IDC_BAN_HARD)) flags |= OYK_BAN_HARD_BAN;
                if (IsDlgButtonChecked(hdlg, IDC_BAN_APPEAL)) flags |= OYK_BAN_APPEAL_ALLOWED;
                if (g_node.ban_callsign(call, flags, reason, "permanent", fb, sizeof(fb))) {
                    SetDlgItemText(hdlg, IDC_BAN_CALL, "");
                    set_status(fb);
                    fill_ban_listbox(GetDlgItem(hdlg, IDC_BAN_LIST));
                } else {
                    MessageBox(hdlg, fb, "Ban Error", MB_OK | MB_ICONEXCLAMATION);
                }
                return TRUE;
            }
            if (id == IDC_BAN_REMOVE) {
                HWND lb = GetDlgItem(hdlg, IDC_BAN_LIST);
                int sel = static_cast<int>(SendMessage(lb, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < g_node.ban_count()) {
                    char fb[96];
                    g_node.unban_callsign(g_node.ban_entry(sel).key, fb, sizeof(fb));
                    set_status(fb);
                    fill_ban_listbox(lb);
                }
                return TRUE;
            }
            if (id == IDCANCEL) {
                EndDialog(hdlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

static void layout_controls(HWND /*hwnd*/, int cw, int ch) {
    const int pad = 6;
    const int hdr_h = 20;
    const int st_h = 18;
    const int badge_w = 140;

    MoveWindow(g_ctl_header, pad, pad, cw - badge_w - pad * 3, hdr_h, TRUE);
    MoveWindow(g_ctl_badge, cw - badge_w - pad, pad, badge_w, hdr_h, TRUE);

    int body_y = pad + hdr_h + pad;
    int body_h = ch - body_y - st_h - pad * 3;
    if (body_h < 120) body_h = 120;

    int top_h = body_h / 3;
    int mid_h = body_h / 3;
    int bot_h = body_h - top_h - mid_h - pad * 2;
    int left_w = (cw - pad * 3) * 58 / 100;
    int right_w = cw - pad * 3 - left_w;

    MoveWindow(g_ctl_ifaces, pad, body_y, left_w, top_h, TRUE);
    MoveWindow(g_ctl_heard, pad * 2 + left_w, body_y, right_w, top_h, TRUE);
    MoveWindow(g_ctl_sessions, pad, body_y + top_h + pad, cw - pad * 2, mid_h, TRUE);
    MoveWindow(g_ctl_logs, pad, body_y + top_h + mid_h + pad * 2, cw - pad * 2, bot_h, TRUE);

    int st_y = ch - st_h - pad;
    MoveWindow(g_ctl_status, pad, st_y, cw - pad * 2, st_h, TRUE);
}

OYK_WND_RET MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_mono_font = (HFONT)GetStockObject(OEM_FIXED_FONT);
            g_ctl_header = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE,
                                        0, 0, 100, 20, hwnd, OYK_HMENU(IDC_HEADER_TEXT), g_hinst, NULL);
            g_ctl_badge = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE | SS_CENTER | WS_BORDER,
                                       0, 0, 100, 20, hwnd, OYK_HMENU(IDC_INCOMING_BADGE), g_hinst, NULL);
            g_ctl_ifaces = CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                        0, 0, 100, 100, hwnd, OYK_HMENU(IDC_IFACE_LIST), g_hinst, NULL);
            g_ctl_heard = CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                       0, 0, 100, 100, hwnd, OYK_HMENU(IDC_HEARD_LIST), g_hinst, NULL);
            g_ctl_sessions = CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                          0, 0, 100, 100, hwnd, OYK_HMENU(IDC_SESSION_LIST), g_hinst, NULL);
            g_ctl_logs = CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                      0, 0, 100, 100, hwnd, OYK_HMENU(IDC_LOG_LIST), g_hinst, NULL);
            g_ctl_status = CreateWindow("STATIC", g_status_msg, WS_CHILD | WS_VISIBLE,
                                        0, 0, 100, 18, hwnd, OYK_HMENU(IDC_STATUS_TEXT), g_hinst, NULL);

            SendMessage(g_ctl_ifaces, WM_SETFONT, OYK_WPARAM_H(g_mono_font), FALSE);
            SendMessage(g_ctl_heard, WM_SETFONT, OYK_WPARAM_H(g_mono_font), FALSE);
            SendMessage(g_ctl_sessions, WM_SETFONT, OYK_WPARAM_H(g_mono_font), FALSE);
            SendMessage(g_ctl_logs, WM_SETFONT, OYK_WPARAM_H(g_mono_font), FALSE);

            SetTimer(hwnd, 1, 50, NULL);  /* Network pump */
            SetTimer(hwnd, 2, 250, NULL); /* GUI refresh */
            refresh_gui();
            return 0;
        }
        case WM_SIZE:
            layout_controls(hwnd, LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_TIMER:
            if (wp == 1) {
                pump_winsock();
                g_node.tick();
            } else if (wp == 2) {
                refresh_gui();
            }
            return 0;
        case WM_COMMAND: {
            WORD id = OYK_CMD_ID(wp, lp);
            if (id == IDM_ALLOW_INCOMING) {
                g_node.set_accept_incoming(1);
                set_status("New incoming connections are now ALLOWED.");
                refresh_gui();
            } else if (id == IDM_DENY_INCOMING) {
                g_node.set_accept_incoming(0);
                set_status("New incoming connections are now DENIED.");
                refresh_gui();
            } else if (id == IDM_TERMINATE) {
                if (g_node.total_active_connections() == 0) {
                    set_status("No active connections to terminate.");
                } else {
                    DialogBox(g_hinst, MAKEINTRESOURCE(IDD_TERMINATE), hwnd, (DLGPROC)TerminateDlgProc);
                    refresh_gui();
                }
            } else if (id == IDM_BAN) {
                DialogBox(g_hinst, MAKEINTRESOURCE(IDD_BAN), hwnd, (DLGPROC)BanDlgProc);
                refresh_gui();
            } else if (id == IDM_EXIT) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        case WM_CLOSE: {
            char msg_buf[64];
            int active = g_node.total_active_connections();
            if (active > 0) {
                sprintf(msg_buf, "Exit node? (%d active)", active);
            } else {
                strcpy(msg_buf, "Exit node?");
            }
            if (MessageBox(hwnd, msg_buf, "Exit", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            KillTimer(hwnd, 2);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static void resolve_win_config(char* out, size_t out_sz) {
    if (!out || out_sz < 20) return;
    const char* home = getenv("USERPROFILE");
    if (!home || !home[0]) home = getenv("HOME");
    FILE* fp = NULL;
    if (home && home[0] && strlen(home) + 16 < out_sz) {
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

static void init_win_interfaces() {
    for (int i = 0; i < OYK_MAX_INTERFACES; ++i) {
        g_ifaces[i].kind = WIN_IF_NONE;
        g_ifaces[i].sock = INVALID_SOCKET;
    }
    memset(g_telnet_clients, 0, sizeof(g_telnet_clients));
    for (int i = 0; i < OYK_MAX_TELNET_CLIENTS; ++i) {
        g_telnet_clients[i].sock = INVALID_SOCKET;
    }

    for (int i = 0; i < g_node.iface_count() && i < OYK_MAX_INTERFACES; ++i) {
        const OykIfaceConfig& ic = g_node.iface(i).cfg;

        if (win_strieq(ic.type, "axudp")) {
            unsigned long bind_ip = INADDR_ANY;
            if (!resolve_bind_addr(ic.host, &bind_ip)) {
                g_node.set_iface_status(i, 0, "BAD HOST");
                continue;
            }
            oyk_u16 port = ic.port ? ic.port : static_cast<oyk_u16>(10093);
            SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
            if (s == INVALID_SOCKET) {
                g_node.set_iface_status(i, 0, "SOCK ERR");
                continue;
            }
            int reuse = 1;
            setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
            u_long nb = 1;
            ioctlsocket(s, FIONBIO, &nb);

            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = bind_ip;
            addr.sin_port = htons(port);
            if (bind(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
                g_ifaces[i].kind = WIN_IF_AXUDP;
                g_ifaces[i].sock = s;
                char st[32];
                sprintf(st, "UP :%u", static_cast<unsigned int>(port));
                g_node.set_iface_status(i, 1, st);
            } else {
                closesocket(s);
                g_node.set_iface_status(i, 0, "BIND ERR");
            }
        } else if (win_strieq(ic.type, "telnet")) {
            unsigned long bind_ip = INADDR_ANY;
            if (!resolve_bind_addr(ic.host, &bind_ip)) {
                g_node.set_iface_status(i, 0, "BAD HOST");
                continue;
            }
            oyk_u16 port = ic.port ? ic.port : static_cast<oyk_u16>(8010);
            SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
            if (s == INVALID_SOCKET) {
                g_node.set_iface_status(i, 0, "SOCK ERR");
                continue;
            }
            int reuse = 1;
            setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = bind_ip;
            addr.sin_port = htons(port);
            if (bind(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0 &&
                listen(s, 8) == 0) {
                u_long nb = 1;
                ioctlsocket(s, FIONBIO, &nb);
                g_ifaces[i].kind = WIN_IF_TELNET;
                g_ifaces[i].sock = s;
                char st[32], log_msg[96];
                sprintf(st, "UP :%u", static_cast<unsigned int>(port));
                g_node.set_iface_status(i, 1, st);
                sprintf(log_msg, "Telnet interface %s listening on %s:%u",
                        ic.id, ic.host[0] ? ic.host : "0.0.0.0",
                        static_cast<unsigned int>(port));
                g_node.log_message(OYK_LOG_INFO, log_msg);
            } else {
                closesocket(s);
                g_node.set_iface_status(i, 0, "BIND ERR");
            }
        } else {
            g_node.set_iface_status(i, 0, "UNSUPPORTED");
        }
    }
}

static void shutdown_win_interfaces() {
    for (int i = 0; i < OYK_MAX_TELNET_CLIENTS; ++i) {
        telnet_close_client(&g_telnet_clients[i]);
    }
    for (int i = 0; i < OYK_MAX_INTERFACES; ++i) {
        if (g_ifaces[i].sock != INVALID_SOCKET) {
            closesocket(g_ifaces[i].sock);
            g_ifaces[i].sock = INVALID_SOCKET;
        }
        g_ifaces[i].kind = WIN_IF_NONE;
    }
}

int PASCAL WinMain(HINSTANCE hinst, HINSTANCE hprev, LPSTR cmdline, int nshow) {
    g_hinst = hinst;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(1, 1), &wsa) != 0) {
        MessageBox(NULL, "Failed to initialize Winsock 1.1.", "OYK Packet Node", MB_OK | MB_ICONSTOP);
        return 1;
    }

    static OykConfig cfg;
    char cfg_path[128], err[96];
    cfg_path[0] = '\0';
    if (cmdline && cmdline[0]) {
        strncpy(cfg_path, cmdline, sizeof(cfg_path) - 1);
        cfg_path[sizeof(cfg_path) - 1] = '\0';
    } else {
        resolve_win_config(cfg_path, sizeof(cfg_path));
    }

    if (cfg_path[0]) {
        if (!oyk_config_load(cfg_path, &cfg, err, sizeof(err))) {
            MessageBox(NULL, err, "OYK Config Error", MB_OK | MB_ICONSTOP);
            WSACleanup();
            return 1;
        }
    } else {
        oyk_config_init_default(&cfg);
    }

    if (!g_node.init(&cfg, win_tx_callback, NULL, err, sizeof(err))) {
        MessageBox(NULL, err, "OYK Init Error", MB_OK | MB_ICONSTOP);
        WSACleanup();
        return 1;
    }

    init_win_interfaces();

    if (!hprev) {
        WNDCLASS wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = MainWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = OYK_HBRUSH(COLOR_BTNFACE + 1);
        wc.lpszMenuName = MAKEINTRESOURCE(IDR_MAINMENU);
        wc.lpszClassName = "OYKNodeWinClass";
        RegisterClass(&wc);
    }

    g_hwnd_main = CreateWindow(
        "OYKNodeWinClass",
        "OYK Amateur Radio Packet Node",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 760, 520,
        NULL, NULL, hinst, NULL);

    ShowWindow(g_hwnd_main, nshow);
    UpdateWindow(g_hwnd_main);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_node.shutdown();
    shutdown_win_interfaces();
    WSACleanup();
    return static_cast<int>(msg.wParam);
}
