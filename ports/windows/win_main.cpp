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
#include <commdlg.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../compat/oyk_portable.hpp"
#include "../compat/oyk_yapp.hpp"
#include "../compat/oyk_rig.hpp"
#include "../compat/oyk_printer.hpp"
#include "resource.h"

#if defined(_WIN32) || defined(_WIN64)
  #define OYK_DLG_RET         INT_PTR CALLBACK
  #define OYK_WND_RET         LRESULT CALLBACK
  #define OYK_CMD_ID(wp, lp)     LOWORD(wp)
  #define OYK_CMD_NOTIFY(wp, lp) HIWORD(wp)
  #define OYK_HMENU(id)          ((HMENU)(INT_PTR)(id))
  #define OYK_HBRUSH(idx)     ((HBRUSH)(INT_PTR)(idx))
  #define OYK_WPARAM_H(h)     ((WPARAM)(h))
  #define OYK_LPARAM_STR(s)   ((LPARAM)(LPCSTR)(s))
  #define OYK_EDIT_SETSEL(h, a, b) do { \
      SendMessage((h), EM_SETSEL, (WPARAM)(a), (LPARAM)(b)); \
      SendMessage((h), EM_SCROLLCARET, 0, 0); \
  } while (0)
#else
  /* 16-bit Windows 3.1 */
  #define OYK_DLG_RET            BOOL FAR PASCAL __export
  #define OYK_WND_RET            LRESULT FAR PASCAL __export
  #define OYK_CMD_ID(wp, lp)     ((WORD)(wp))
  #define OYK_CMD_NOTIFY(wp, lp) HIWORD(lp)
  #define OYK_HMENU(id)          ((HMENU)(id))
  #define OYK_HBRUSH(idx)     ((HBRUSH)(idx))
  #define OYK_WPARAM_H(h)     ((WPARAM)(h))
  #define OYK_LPARAM_STR(s)   ((LPARAM)(LPSTR)(s))
  #define OYK_EDIT_SETSEL(h, a, b) SendMessage((h), EM_SETSEL, 0, MAKELONG((WORD)(a), (WORD)(b)))
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
    TSTAGE_CONNECTED,
    TSTAGE_CLOSING
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
    int close_ticks;
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
static char g_last_header[160] = "";
static char g_last_badge[32] = "";
static char g_last_ifaces_txt[512] = "";
static char g_last_heard_txt[384] = "";
static char g_last_sess_txt[768] = "";

/* Draggable splitter handle between Radio Interfaces and Last 5 New Callsigns Heard */
static const int OYK_SPLITTER_W = 8;
static int g_split_pct = 58;
static int g_split_x = 0;
static int g_split_y = 0;
static int g_split_h = 0;
static int g_split_drag = 0;
static int g_client_w = 0;
static int g_client_h = 0;

/* Local Terminal + YAPP state (static for Win16 stack safety) */
static HWND g_lterm_hwnd = NULL;
static oyk_u32 g_lterm_sid = 0;
static int g_lterm_connected = 0;
static int g_lterm_connecting = 0;
static int g_lterm_is_local = 0;
static OykCallsign g_lterm_remote;
static OykYappSession g_lterm_yapp;
static char g_lterm_curline[260];
static int g_lterm_curline_len = 0;
static char g_lterm_last_yapp_st[128];

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

static void telnet_begin_close(WinTelnetClient* c) {
    if (!c || !c->in_use) return;
    if (c->stage == TSTAGE_CLOSING) return;
    if (c->sock != INVALID_SOCKET) {
        shutdown(c->sock, 1); /* SD_SEND: flush queued bytes and send TCP FIN */
        c->stage = TSTAGE_CLOSING;
        c->close_ticks = 0;
    } else {
        c->in_use = 0;
    }
}

static WinTelnetClient* telnet_find_client(int iface_idx, oyk_u16 handle, const OykCallsign* dst) {
    if (handle >= 1 && handle <= OYK_MAX_TELNET_CLIENTS) {
        WinTelnetClient* c = &g_telnet_clients[handle - 1];
        if (c->in_use && c->stage != TSTAGE_CLOSING && c->iface_idx == iface_idx) return c;
    }
    if (dst) {
        for (int i = 0; i < OYK_MAX_TELNET_CLIENTS; ++i) {
            WinTelnetClient* c = &g_telnet_clients[i];
            if (c->in_use && c->iface_idx == iface_idx &&
                c->stage != TSTAGE_LOGIN && c->stage != TSTAGE_CLOSING &&
                oyk_callsign_eq(&c->callsign, dst)) {
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
            telnet_send_str(c->sock, "\r\n");
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
                    o->stage != TSTAGE_LOGIN && o->stage != TSTAGE_CLOSING &&
                    oyk_callsign_eq(&o->callsign, &parsed)) {
                    telnet_send_str(c->sock, "*** Callsign already connected on this telnet port.\r\n");
                    telnet_begin_close(c);
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
            telnet_send_str(c->sock, "\r\n");
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
                telnet_begin_close(c);
                return 1;
            case OYK_FT_DISC:
                telnet_send_str(c->sock, "\r\n*** Disconnected.\r\n");
                telnet_begin_close(c);
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

    /* 2. Pump connected & closing Telnet client sockets */
    for (int i = 0; i < OYK_MAX_TELNET_CLIENTS; ++i) {
        WinTelnetClient* c = &g_telnet_clients[i];
        if (!c->in_use || c->sock == INVALID_SOCKET) continue;

        if (c->stage == TSTAGE_CLOSING) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(c->sock, &rfds);
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 0;
            if (select(0, &rfds, NULL, NULL, &tv) > 0) {
                int n = recv(c->sock, reinterpret_cast<char*>(buf), sizeof(buf), 0);
                if (n <= 0 && (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK)) {
                    telnet_close_client(c);
                    continue;
                }
            }
            c->close_ticks++;
            if (c->close_ticks >= 20) {
                telnet_close_client(c);
            }
            continue;
        }

        for (int iter = 0; iter < 8 && c->in_use && c->stage != TSTAGE_CLOSING; ++iter) {
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

static void update_static_text(HWND ctl, char* cache, size_t cache_sz, const char* text) {
    if (!ctl || !text) return;
    if (cache && strcmp(cache, text) == 0) return;
    if (cache && cache_sz > 0) {
        strncpy(cache, text, cache_sz - 1);
        cache[cache_sz - 1] = '\0';
    }
    SendMessage(ctl, WM_SETREDRAW, FALSE, 0);
    SetWindowText(ctl, text);
    SendMessage(ctl, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(ctl, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

static void update_listbox_rows(HWND lb, char* cache, size_t cache_sz,
                                const char* const* rows, int count, int scroll_bottom) {
    if (!lb) return;
    static char joined[1024];
    size_t pos = 0;
    joined[0] = '\0';
    for (int i = 0; i < count; ++i) {
        const char* r = rows[i] ? rows[i] : "";
        size_t rl = strlen(r);
        if (pos + rl + 2 < sizeof(joined)) {
            memcpy(joined + pos, r, rl);
            pos += rl;
            joined[pos++] = '\n';
            joined[pos] = '\0';
        }
    }
    if (cache && cache_sz > 0) {
        if (strcmp(cache, joined) == 0) return;
        strncpy(cache, joined, cache_sz - 1);
        cache[cache_sz - 1] = '\0';
    }

    SendMessage(lb, WM_SETREDRAW, FALSE, 0);
    SendMessage(lb, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; ++i) {
        SendMessage(lb, LB_ADDSTRING, 0, OYK_LPARAM_STR(rows[i]));
    }
    if (scroll_bottom && count > 0) {
        SendMessage(lb, LB_SETTOPINDEX, static_cast<WPARAM>(count - 1), 0);
    }
    SendMessage(lb, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(lb, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
}

static void set_status(const char* text) {
    if (!text) return;
    if (strcmp(g_status_msg, text) == 0) return;
    strncpy(g_status_msg, text, sizeof(g_status_msg) - 1);
    g_status_msg[sizeof(g_status_msg) - 1] = '\0';
    if (g_ctl_status) {
        SendMessage(g_ctl_status, WM_SETREDRAW, FALSE, 0);
        SetWindowText(g_ctl_status, g_status_msg);
        SendMessage(g_ctl_status, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(g_ctl_status, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }
}

static void refresh_gui() {
    static char row_buf[16][128];
    static const char* row_ptrs[16];
    char buf[160];
    char call_s[16];
    oyk_format_callsign(&g_node.config().callsign, call_s, sizeof(call_s));

    sprintf(buf, "OYK PACKET NODE: %s  |  Uptime: %lus  |  Active Connections: %d  |  Banned: %d",
            call_s,
            static_cast<unsigned long>(g_node.uptime_seconds()),
            g_node.total_active_connections(),
            g_node.ban_count());
    update_static_text(g_ctl_header, g_last_header, sizeof(g_last_header), buf);

    update_static_text(g_ctl_badge, g_last_badge, sizeof(g_last_badge),
                       g_node.accept_incoming() ? "[INCOMING: ALLOW]" : "[INCOMING: DENY]");

    /* Radio Interfaces (active connections per interface) */
    int n = 0;
    strcpy(row_buf[n], "Port      Type     Conn  Status          RX/TX");
    row_ptrs[n] = row_buf[n];
    ++n;
    for (int i = 0; i < g_node.iface_count() && n < 16; ++i) {
        const OykIfaceRuntime& r = g_node.iface(i);
        sprintf(row_buf[n], "%-9.9s %-8.8s %-5d %-15.15s %lu/%lu",
                r.cfg.id, r.cfg.type, g_node.active_connections_on(i), r.status,
                static_cast<unsigned long>(r.rx_frames),
                static_cast<unsigned long>(r.tx_frames));
        row_ptrs[n] = row_buf[n];
        ++n;
    }
    update_listbox_rows(g_ctl_ifaces, g_last_ifaces_txt, sizeof(g_last_ifaces_txt), row_ptrs, n, 0);

    /* Last 5 New Callsigns Heard */
    n = 0;
    strcpy(row_buf[n], "#  Callsign   Port      Heard");
    row_ptrs[n] = row_buf[n];
    ++n;
    for (int i = 0; i < g_node.new_heard_count() && n < 16; ++i) {
        const OykNewHeard& nh = g_node.new_heard(i);
        sprintf(row_buf[n], "%d. %-10.10s %-9.9s %s", i + 1, nh.callsign, nh.iface_id, nh.time_hms);
        row_ptrs[n] = row_buf[n];
        ++n;
    }
    update_listbox_rows(g_ctl_heard, g_last_heard_txt, sizeof(g_last_heard_txt), row_ptrs, n, 0);

    /* Active Connections */
    n = 0;
    strcpy(row_buf[n], "ID    Callsign    Port      Since     Mode");
    row_ptrs[n] = row_buf[n];
    ++n;
    for (int i = 0; i < g_node.max_sessions() && n < 16; ++i) {
        const OykSession& s = g_node.session_slot(i);
        if (s.state == OYK_SESS_CONNECTED) {
            char cs[12];
            oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
            sprintf(row_buf[n], "#%-4lu %-11.11s %-9.9s %-9.9s %s",
                    static_cast<unsigned long>(s.id), cs,
                    (s.iface_idx >= 0) ? g_node.iface(s.iface_idx).cfg.id : "local", s.since_hms,
                    s.restricted_ban ? "BAN NOTICE"
                                     : (s.in_bbs ? "BBS"
                                                 : (s.in_chat ? "CHAT"
                                                              : (s.is_local_term ? "LOCAL TERM" : "NODE SHELL"))));
            row_ptrs[n] = row_buf[n];
            ++n;
        }
    }
    update_listbox_rows(g_ctl_sessions, g_last_sess_txt, sizeof(g_last_sess_txt), row_ptrs, n, 0);

    /* Node Activity Log */
    int log_n = g_node.console_log_count();
    if (log_n != g_last_log_count) {
        g_last_log_count = log_n;
        SendMessage(g_ctl_logs, WM_SETREDRAW, FALSE, 0);
        SendMessage(g_ctl_logs, LB_RESETCONTENT, 0, 0);
        for (int i = 0; i < log_n; ++i) {
            SendMessage(g_ctl_logs, LB_ADDSTRING, 0,
                        OYK_LPARAM_STR(g_node.console_log_line(i)));
        }
        if (log_n > 0) {
            SendMessage(g_ctl_logs, LB_SETTOPINDEX, static_cast<WPARAM>(log_n - 1), 0);
        }
        SendMessage(g_ctl_logs, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(g_ctl_logs, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
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
                            cs, (s.iface_idx >= 0) ? g_node.iface(s.iface_idx).cfg.id : "local");
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
            SetDlgItemText(hdlg, IDC_BAN_LIFT, "permanent");
            SetDlgItemText(hdlg, IDC_BAN_REASON, "Banned by sysop.");
            CheckDlgButton(hdlg, IDC_BAN_APPEAL, BST_CHECKED);
            fill_ban_listbox(GetDlgItem(hdlg, IDC_BAN_LIST));
            return TRUE;
        case WM_COMMAND: {
            WORD id = OYK_CMD_ID(wp, lp);
            if (id == IDC_BAN_ADD) {
                char call[16], lift[32], reason[96], fb[96];
                GetDlgItemText(hdlg, IDC_BAN_CALL, call, sizeof(call));
                GetDlgItemText(hdlg, IDC_BAN_LIFT, lift, sizeof(lift));
                GetDlgItemText(hdlg, IDC_BAN_REASON, reason, sizeof(reason));
                oyk_u32 flags = 0;
                if (IsDlgButtonChecked(hdlg, IDC_BAN_HARD)) flags |= OYK_BAN_HARD_BAN;
                if (IsDlgButtonChecked(hdlg, IDC_BAN_APPEAL)) flags |= OYK_BAN_APPEAL_ALLOWED;
                if (g_node.ban_callsign(call, flags, reason, lift, fb, sizeof(fb))) {
                    SetDlgItemText(hdlg, IDC_BAN_CALL, "");
                    SetDlgItemText(hdlg, IDC_BAN_LIFT, "permanent");
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
                    char confirm[64];
                    sprintf(confirm, "Unban callsign %s?", g_node.ban_entry(sel).key);
                    if (MessageBox(hdlg, confirm, "Confirm Unban",
                                   MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        char fb[96];
                        g_node.unban_callsign(g_node.ban_entry(sel).key, fb, sizeof(fb));
                        set_status(fb);
                        fill_ban_listbox(lb);
                    }
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

/* ==================== Local Terminal & YAPP Dialog ==================== */

static void lterm_append_line(const char* line) {
    if (!g_lterm_hwnd || !line) return;
    HWND ed = GetDlgItem(g_lterm_hwnd, IDC_LTERM_OUTPUT);
    if (!ed) return;
    int cur_len = GetWindowTextLength(ed);
    if (cur_len > 22000) {
        OYK_EDIT_SETSEL(ed, 0, 4096);
        SendMessage(ed, EM_REPLACESEL, FALSE, OYK_LPARAM_STR(""));
        cur_len = GetWindowTextLength(ed);
    }
    static char buf[320];
    size_t n = strlen(line);
    if (n > sizeof(buf) - 3) n = sizeof(buf) - 3;
    memcpy(buf, line, n);
    buf[n++] = '\r';
    buf[n++] = '\n';
    buf[n] = '\0';
    OYK_EDIT_SETSEL(ed, cur_len, cur_len);
    SendMessage(ed, EM_REPLACESEL, FALSE, OYK_LPARAM_STR(buf));
}

static void layout_lterm_controls(HWND hdlg, int cw, int ch) {
    const int pad = 8;
    const int top_h = 22;
    const int st_h = 22;
    const int bot_h = 24;

    if (cw < 320) cw = 320;
    if (ch < 180) ch = 180;

    /* Top bar: Port label + combo + Target label + flex target edit + buttons */
    const int port_lbl_w = 34;
    const int port_cb_w = 84;
    const int tgt_lbl_w = 46;
    const int conn_w = 66;
    const int disc_w = 46;
    const int up_w = 84;
    int fixed_top = port_lbl_w + port_cb_w + tgt_lbl_w + conn_w + disc_w + up_w + pad * 8;
    int tgt_w = cw - fixed_top;
    if (tgt_w < 50) tgt_w = 50;

    int x = pad;
    MoveWindow(GetDlgItem(hdlg, IDC_LTERM_LBL_PORT), x, pad + 3, port_lbl_w, 16, TRUE);
    x += port_lbl_w + pad / 2;
    MoveWindow(GetDlgItem(hdlg, IDC_LTERM_PORT), x, pad, port_cb_w, 120, TRUE);
    x += port_cb_w + pad;
    MoveWindow(GetDlgItem(hdlg, IDC_LTERM_LBL_TGT), x, pad + 3, tgt_lbl_w, 16, TRUE);
    x += tgt_lbl_w + pad / 2;
    MoveWindow(GetDlgItem(hdlg, IDC_LTERM_TARGET), x, pad + 1, tgt_w, top_h - 2, TRUE);
    x += tgt_w + pad;
    MoveWindow(GetDlgItem(hdlg, IDC_LTERM_CONNECT), x, pad, conn_w, top_h, TRUE);
    x += conn_w + pad / 2;
    MoveWindow(GetDlgItem(hdlg, IDC_LTERM_DISC), x, pad, disc_w, top_h, TRUE);
    x += disc_w + pad / 2;
    MoveWindow(GetDlgItem(hdlg, IDC_LTERM_UPLOAD), x, pad, cw - x - pad, top_h, TRUE);

    /* Bottom input row */
    int bot_y = ch - pad - bot_h;
    const int close_w = 68;
    const int send_w = 64;
    int in_w = cw - pad * 4 - send_w - close_w;
    if (in_w < 80) in_w = 80;
    MoveWindow(GetDlgItem(hdlg, IDC_LTERM_INPUT), pad, bot_y, in_w, bot_h, TRUE);
    MoveWindow(GetDlgItem(hdlg, IDC_LTERM_SEND), pad * 2 + in_w, bot_y, send_w, bot_h, TRUE);
    MoveWindow(GetDlgItem(hdlg, IDCANCEL), pad * 3 + in_w + send_w, bot_y, close_w, bot_h, TRUE);

    /* Status + Abort YAPP row */
    int st_y = bot_y - pad - st_h;
    const int abort_w = 86;
    int st_w = cw - pad * 3 - abort_w;
    if (st_w < 80) st_w = 80;
    MoveWindow(GetDlgItem(hdlg, IDC_LTERM_STATUS), pad, st_y + 3, st_w, 16, TRUE);
    MoveWindow(GetDlgItem(hdlg, IDC_LTERM_ABORT), cw - pad - abort_w, st_y, abort_w, st_h, TRUE);

    /* Middle output edit (multi-line without ES_AUTOHSCROLL reflows/word-wraps automatically) */
    int out_y = pad + top_h + pad;
    int out_h = st_y - pad - out_y;
    if (out_h < 40) out_h = 40;
    HWND ed = GetDlgItem(hdlg, IDC_LTERM_OUTPUT);
    MoveWindow(ed, pad, out_y, cw - pad * 2, out_h, TRUE);
    InvalidateRect(ed, NULL, TRUE);
}

static void lterm_update_status() {
    if (!g_lterm_hwnd) return;
    char st[160];
    if (!g_lterm_is_local && g_lterm_yapp.state != OYK_YAPP_IDLE && g_lterm_yapp.status[0]) {
        sprintf(st, "%.100s (%lu/%lu B)", g_lterm_yapp.status,
                static_cast<unsigned long>(g_lterm_yapp.bytes_transferred),
                static_cast<unsigned long>(g_lterm_yapp.total_bytes));
    } else if (g_lterm_connected) {
        char cs[16];
        oyk_format_callsign(&g_lterm_remote, cs, sizeof(cs));
        sprintf(st, "Connected to %s (%s)", cs, g_lterm_is_local ? "LOCAL - YAPP disabled" : "YAPP ready");
    } else if (g_lterm_connecting) {
        char cs[16];
        oyk_format_callsign(&g_lterm_remote, cs, sizeof(cs));
        sprintf(st, "Connecting to %s...", cs);
    } else {
        strcpy(st, "Disconnected (YAPP saves to incoming\\<CALL-SSID>\\)");
    }
    SetDlgItemText(g_lterm_hwnd, IDC_LTERM_STATUS, st);
    EnableWindow(GetDlgItem(g_lterm_hwnd, IDC_LTERM_CONNECT), (!g_lterm_connected && !g_lterm_connecting) ? TRUE : FALSE);
    EnableWindow(GetDlgItem(g_lterm_hwnd, IDC_LTERM_DISC), (g_lterm_connected || g_lterm_connecting) ? TRUE : FALSE);
    EnableWindow(GetDlgItem(g_lterm_hwnd, IDC_LTERM_UPLOAD),
                 (g_lterm_connected && !g_lterm_is_local && !oyk_yapp_is_active(&g_lterm_yapp)) ? TRUE : FALSE);
    EnableWindow(GetDlgItem(g_lterm_hwnd, IDC_LTERM_ABORT),
                 (!g_lterm_is_local && oyk_yapp_is_active(&g_lterm_yapp)) ? TRUE : FALSE);
}

static int win_yapp_send_cb(const oyk_u8* data, size_t len, void* /*ctx*/) {
    if (g_lterm_sid == 0 || !g_lterm_connected) return 0;
    return g_node.send_local_bytes(g_lterm_sid, data, len);
}

static int win_yapp_overwrite_cb(const char* target_path, const char* /*filename*/, void* /*ctx*/) {
    static char prompt[320];
    sprintf(prompt, "File already exists:\n%s\n\nOverwrite existing file?", target_path);
    HWND owner = g_lterm_hwnd ? g_lterm_hwnd : g_hwnd_main;
    return (MessageBox(owner, prompt, "YAPP Overwrite Confirmation",
                       MB_YESNO | MB_ICONEXCLAMATION) == IDYES) ? 1 : 0;
}

static void win_local_term_event_cb(oyk_u32 sid, int event_type,
                                    const oyk_u8* data, size_t len,
                                    const char* reason, void* /*ctx*/) {
    if (sid != g_lterm_sid) return;
    if (event_type == 1) {
        g_lterm_connecting = 0;
        g_lterm_connected = 1;
        char cs[16], msg[80];
        oyk_format_callsign(&g_lterm_remote, cs, sizeof(cs));
        sprintf(msg, "*** Connected to %s", cs);
        lterm_append_line(msg);
        lterm_update_status();
    } else if (event_type == 2 && data && len > 0) {
        const oyk_u8* text_ptr = data;
        size_t text_len = len;
        static oyk_u8 plain[512];
        if (!g_lterm_is_local) {
            text_len = oyk_yapp_feed_bytes(&g_lterm_yapp, data, len, plain, sizeof(plain));
            text_ptr = plain;
        }
        for (size_t i = 0; i < text_len; ++i) {
            oyk_u8 ch = text_ptr[i];
            if (ch == '\r' || ch == '\n') {
                if (g_lterm_curline_len > 0 || ch == '\r') {
                    g_lterm_curline[g_lterm_curline_len] = '\0';
                    lterm_append_line(g_lterm_curline);
                    g_lterm_curline_len = 0;
                }
            } else if (ch >= 0x20U && ch < 0x7FU) {
                if (g_lterm_curline_len < static_cast<int>(sizeof(g_lterm_curline) - 1)) {
                    g_lterm_curline[g_lterm_curline_len++] = static_cast<char>(ch);
                }
            }
        }
        if (!g_lterm_is_local && g_lterm_yapp.status[0] && strcmp(g_lterm_yapp.status, g_lterm_last_yapp_st) != 0) {
            strncpy(g_lterm_last_yapp_st, g_lterm_yapp.status, sizeof(g_lterm_last_yapp_st) - 1);
            g_lterm_last_yapp_st[sizeof(g_lterm_last_yapp_st) - 1] = '\0';
            char line[160];
            sprintf(line, "*** %s", g_lterm_last_yapp_st);
            lterm_append_line(line);
        }
        lterm_update_status();
    } else if (event_type == 3) {
        g_lterm_sid = 0;
        g_lterm_connecting = 0;
        g_lterm_connected = 0;
        g_lterm_is_local = 0;
        oyk_yapp_reset(&g_lterm_yapp);
        char msg[96];
        sprintf(msg, "*** Disconnected (%s)", reason ? reason : "Closed");
        lterm_append_line(msg);
        lterm_update_status();
    }
}

OYK_DLG_RET TerminalDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    static int s_iface_map[OYK_MAX_INTERFACES + 1];
    static int s_iface_cnt = 0;

    switch (msg) {
        case WM_INITDIALOG: {
            g_lterm_hwnd = hdlg;
            g_lterm_sid = 0;
            g_lterm_connected = 0;
            g_lterm_connecting = 0;
            g_lterm_is_local = 0;
            g_lterm_curline_len = 0;
            g_lterm_last_yapp_st[0] = '\0';
            oyk_yapp_init(&g_lterm_yapp, "incoming",
                          win_yapp_send_cb, NULL,
                          win_yapp_overwrite_cb, NULL);
            g_node.set_local_event_callback(win_local_term_event_cb, NULL);

            SendDlgItemMessage(hdlg, IDC_LTERM_OUTPUT, WM_SETFONT, OYK_WPARAM_H(g_mono_font), FALSE);
            SendDlgItemMessage(hdlg, IDC_LTERM_OUTPUT, EM_LIMITTEXT, 28000, 0);

            RECT rc;
            GetClientRect(hdlg, &rc);
            layout_lterm_controls(hdlg, rc.right - rc.left, rc.bottom - rc.top);

            HWND cb = GetDlgItem(hdlg, IDC_LTERM_PORT);
            s_iface_cnt = 0;
            SendMessage(cb, CB_ADDSTRING, 0, OYK_LPARAM_STR("local"));
            s_iface_map[s_iface_cnt++] = -1;
            for (int i = 0; i < g_node.iface_count() && s_iface_cnt <= OYK_MAX_INTERFACES; ++i) {
                if (g_ifaces[i].kind == WIN_IF_AXUDP) {
                    SendMessage(cb, CB_ADDSTRING, 0, OYK_LPARAM_STR(g_node.iface(i).cfg.id));
                    s_iface_map[s_iface_cnt++] = i;
                }
            }
            if (s_iface_cnt > 0) SendMessage(cb, CB_SETCURSEL, 0, 0);

            lterm_append_line("*** Local Packet Terminal (YAPP / YAPP-C enabled for remote links)");
            lterm_append_line("*** Select 'local' for local node/BBS/CHAT, or a radio port for remote nodes.");
            lterm_update_status();
            return TRUE;
        }
        case WM_GETMINMAXINFO: {
#if defined(_WIN32) || defined(_WIN64)
            LPMINMAXINFO mmi = (LPMINMAXINFO)lp;
            if (mmi) {
                mmi->ptMinTrackSize.x = 460;
                mmi->ptMinTrackSize.y = 260;
            }
#else
            POINT FAR* pts = (POINT FAR*)lp;
            if (pts) {
                pts[3].x = 460;
                pts[3].y = 260;
            }
#endif
            return TRUE;
        }
        case WM_SIZE:
            layout_lterm_controls(hdlg, LOWORD(lp), HIWORD(lp));
            return TRUE;
        case WM_COMMAND: {
            WORD id = OYK_CMD_ID(wp, lp);
            if (id == IDC_LTERM_CONNECT) {
                int sel = static_cast<int>(SendDlgItemMessage(hdlg, IDC_LTERM_PORT, CB_GETCURSEL, 0, 0));
                if (sel < 0 || sel >= s_iface_cnt) {
                    lterm_append_line("*** Select a port first.");
                    return TRUE;
                }
                char target[96], err[96];
                GetDlgItemText(hdlg, IDC_LTERM_TARGET, target, sizeof(target));
                oyk_u32 sid = 0;
                int is_local = 0;
                g_lterm_is_local = (s_iface_map[sel] < 0) ? 1 : 0;
                if (g_node.open_local_terminal(s_iface_map[sel], target, &sid, &g_lterm_remote,
                                               &is_local, err, sizeof(err))) {
                    g_lterm_sid = sid;
                    g_lterm_is_local = is_local;
                    g_lterm_connecting = is_local ? 0 : 1;
                    g_lterm_connected = is_local ? 1 : 0;
                    oyk_yapp_reset(&g_lterm_yapp);
                    oyk_yapp_set_remote(&g_lterm_yapp, &g_lterm_remote);
                    g_lterm_last_yapp_st[0] = '\0';
                    char cs[16], line[96];
                    oyk_format_callsign(&g_lterm_remote, cs, sizeof(cs));
                    if (is_local) {
                        sprintf(line, "*** Connected locally to %s (YAPP disabled)", cs);
                    } else {
                        sprintf(line, "*** Connecting to %s on %s...", cs, g_node.iface(s_iface_map[sel]).cfg.id);
                    }
                    lterm_append_line(line);
                } else {
                    g_lterm_is_local = 0;
                    char line[128];
                    sprintf(line, "*** Connect error: %s", err);
                    lterm_append_line(line);
                }
                lterm_update_status();
                return TRUE;
            }
            if (id == IDC_LTERM_DISC) {
                if (g_lterm_sid != 0) {
                    oyk_yapp_cancel(&g_lterm_yapp, "Disconnected");
                    g_node.close_local_terminal(g_lterm_sid);
                }
                lterm_update_status();
                return TRUE;
            }
            if (id == IDC_LTERM_UPLOAD) {
                if (!g_lterm_connected || g_lterm_sid == 0) {
                    lterm_append_line("*** Connect to a remote node before uploading.");
                    return TRUE;
                }
                if (g_lterm_is_local) {
                    lterm_append_line("*** YAPP file transfer is disallowed when both sides are local.");
                    return TRUE;
                }
                static OPENFILENAME ofn;
                static char file_buf[260];
                file_buf[0] = '\0';
                memset(&ofn, 0, sizeof(ofn));
                ofn.lStructSize = sizeof(OPENFILENAME);
                ofn.hwndOwner = hdlg;
                ofn.lpstrFilter = "All Files (*.*)\0*.*\0";
                ofn.lpstrFile = file_buf;
                ofn.nMaxFile = sizeof(file_buf);
                ofn.lpstrTitle = "YAPP Upload - Select File";
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
                if (GetOpenFileName(&ofn)) {
                    char err[96];
                    if (!oyk_yapp_start_send(&g_lterm_yapp, file_buf, err, sizeof(err))) {
                        char line[128];
                        sprintf(line, "*** YAPP upload error: %s", err);
                        lterm_append_line(line);
                    } else {
                        strncpy(g_lterm_last_yapp_st, g_lterm_yapp.status, sizeof(g_lterm_last_yapp_st) - 1);
                        char line[160];
                        sprintf(line, "*** %s", g_lterm_yapp.status);
                        lterm_append_line(line);
                    }
                    lterm_update_status();
                }
                return TRUE;
            }
            if (id == IDC_LTERM_ABORT) {
                oyk_yapp_cancel(&g_lterm_yapp, "Cancelled by user");
                lterm_append_line("*** YAPP transfer aborted.");
                lterm_update_status();
                return TRUE;
            }
            if (id == IDC_LTERM_SEND || id == IDOK) {
                static char in_buf[224];
                GetDlgItemText(hdlg, IDC_LTERM_INPUT, in_buf, sizeof(in_buf) - 2);
                SetDlgItemText(hdlg, IDC_LTERM_INPUT, "");
                char echo[240];
                sprintf(echo, "> %s", in_buf);
                lterm_append_line(echo);
                if (g_lterm_connected && g_lterm_sid != 0) {
                    size_t n = strlen(in_buf);
                    in_buf[n++] = '\r';
                    in_buf[n] = '\0';
                    g_node.send_local_bytes(g_lterm_sid, reinterpret_cast<const oyk_u8*>(in_buf), n);
                } else {
                    lterm_append_line("*** Not connected.");
                }
                return TRUE;
            }
            if (id == IDCANCEL) {
                DestroyWindow(hdlg);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hdlg);
            return TRUE;
        case WM_DESTROY:
            if (g_lterm_sid != 0) {
                oyk_yapp_reset(&g_lterm_yapp);
                g_node.close_local_terminal(g_lterm_sid);
                g_lterm_sid = 0;
            }
            g_lterm_connected = 0;
            g_lterm_connecting = 0;
            g_lterm_is_local = 0;
            g_node.set_local_event_callback(NULL, NULL);
            g_lterm_hwnd = NULL;
            return TRUE;
    }
    return FALSE;
}

static void populate_approve_list(HWND hdlg, OykPendingIncoming* items, int* count_out) {
    HWND lb = GetDlgItem(hdlg, IDC_APP_LIST);
    SendMessage(lb, WM_SETREDRAW, FALSE, 0);
    SendMessage(lb, LB_RESETCONTENT, 0, 0);
    int n = oyk_bbs_list_incoming("incoming", items, 32);
    *count_out = n;
    for (int i = 0; i < n; ++i) {
        char row[96];
        sprintf(row, "%-12.12s %-34.34s %lu B",
                items[i].uploader, items[i].filename,
                static_cast<unsigned long>(items[i].size));
        SendMessage(lb, LB_ADDSTRING, 0, OYK_LPARAM_STR(row));
    }
    if (n > 0) SendMessage(lb, LB_SETCURSEL, 0, 0);
    SendMessage(lb, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(lb, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
}

static void populate_approved_files_list(HWND hdlg, OykApprovedFile* items, int* count_out) {
    HWND lb = GetDlgItem(hdlg, IDC_APP_APPROVED_LIST);
    SendMessage(lb, WM_SETREDRAW, FALSE, 0);
    SendMessage(lb, LB_RESETCONTENT, 0, 0);
    int n = oyk_bbs_list_approved(g_node.config().bbs_files_path, items, 48);
    *count_out = n;
    for (int i = 0; i < n; ++i) {
        char row[112];
        sprintf(row, "%-46.46s %lu B",
                items[i].rel_path,
                static_cast<unsigned long>(items[i].size));
        SendMessage(lb, LB_ADDSTRING, 0, OYK_LPARAM_STR(row));
    }
    if (n > 0) SendMessage(lb, LB_SETCURSEL, 0, 0);
    SendMessage(lb, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(lb, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
}

static void switch_files_tab(HWND hdlg, int tab) {
    static const int INC_IDS[] = {
        IDC_APP_L_INC, IDC_APP_LIST, IDC_APP_L_SUB, IDC_APP_SUBDIR,
        IDC_APP_APPROVE, IDC_APP_DELETE
    };
    static const int APP_IDS[] = {
        IDC_APP_L_APP, IDC_APP_APPROVED_LIST, IDC_APP_REMOVE
    };
    int i;
    for (i = 0; i < static_cast<int>(sizeof(INC_IDS) / sizeof(INC_IDS[0])); ++i) {
        ShowWindow(GetDlgItem(hdlg, INC_IDS[i]), (tab == 0) ? SW_SHOW : SW_HIDE);
    }
    for (i = 0; i < static_cast<int>(sizeof(APP_IDS) / sizeof(APP_IDS[0])); ++i) {
        ShowWindow(GetDlgItem(hdlg, APP_IDS[i]), (tab == 1) ? SW_SHOW : SW_HIDE);
    }
    SetDlgItemText(hdlg, IDC_APP_TAB_INC, (tab == 0) ? "[ Pending Incoming ]" : "Pending Incoming");
    SetDlgItemText(hdlg, IDC_APP_TAB_APPROVED, (tab == 1) ? "[ Approved Files ]" : "Approved Files");
    SetDlgItemText(hdlg, IDC_APP_TAB_BOX,
                   (tab == 0) ? "Pending Incoming Uploads" : "Approved BBS Files Area");
}

OYK_DLG_RET ApproveFilesDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    static OykPendingIncoming* s_items = NULL;
    static int s_count = 0;
    static OykApprovedFile* s_approved = NULL;
    static int s_app_count = 0;

    switch (msg) {
        case WM_INITDIALOG: {
            if (!s_items) s_items = new OykPendingIncoming[32];
            if (!s_approved) s_approved = new OykApprovedFile[48];
            SendDlgItemMessage(hdlg, IDC_APP_LIST, WM_SETFONT, OYK_WPARAM_H(g_mono_font), FALSE);
            SendDlgItemMessage(hdlg, IDC_APP_APPROVED_LIST, WM_SETFONT, OYK_WPARAM_H(g_mono_font), FALSE);
            populate_approve_list(hdlg, s_items, &s_count);
            populate_approved_files_list(hdlg, s_approved, &s_app_count);
            switch_files_tab(hdlg, 0);
            char st[128];
            sprintf(st, "Files Area: %.60s (%d pending, %d approved)",
                    g_node.config().bbs_files_path, s_count, s_app_count);
            SetDlgItemText(hdlg, IDC_APP_STATUS, st);
            return TRUE;
        }
        case WM_DESTROY:
            if (s_items) { delete[] s_items; s_items = NULL; }
            if (s_approved) { delete[] s_approved; s_approved = NULL; }
            s_count = 0;
            s_app_count = 0;
            return TRUE;
        case WM_COMMAND: {
            WORD id = OYK_CMD_ID(wp, lp);
            if (id == IDC_APP_TAB_INC) { switch_files_tab(hdlg, 0); return TRUE; }
            if (id == IDC_APP_TAB_APPROVED) { switch_files_tab(hdlg, 1); return TRUE; }
            if (id == IDC_APP_REFRESH) {
                populate_approve_list(hdlg, s_items, &s_count);
                populate_approved_files_list(hdlg, s_approved, &s_app_count);
                char st[96];
                sprintf(st, "Refreshed (%d pending, %d approved).", s_count, s_app_count);
                SetDlgItemText(hdlg, IDC_APP_STATUS, st);
                return TRUE;
            }
            if (id == IDC_APP_REMOVE) {
                int sel = static_cast<int>(SendDlgItemMessage(hdlg, IDC_APP_APPROVED_LIST, LB_GETCURSEL, 0, 0));
                if (sel < 0 || sel >= s_app_count) {
                    SetDlgItemText(hdlg, IDC_APP_STATUS, "Select an approved file first.");
                    return TRUE;
                }
                char prompt[160], err[96];
                sprintf(prompt, "Remove approved file /%s from the BBS files area?", s_approved[sel].rel_path);
                if (MessageBox(hdlg, prompt, "Confirm Remove", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    if (oyk_bbs_delete_approved_file(g_node.config().bbs_files_path,
                                                     s_approved[sel].rel_path, err, sizeof(err))) {
                        char msg_s[128];
                        sprintf(msg_s, "Removed approved file /%s", s_approved[sel].rel_path);
                        g_node.log_message(OYK_LOG_INFO, msg_s);
                        set_status(msg_s);
                        SetDlgItemText(hdlg, IDC_APP_STATUS, msg_s);
                        populate_approved_files_list(hdlg, s_approved, &s_app_count);
                    } else {
                        SetDlgItemText(hdlg, IDC_APP_STATUS, err);
                    }
                }
                return TRUE;
            }
            if (id == IDC_APP_APPROVE || id == IDC_APP_DELETE) {
                int sel = static_cast<int>(SendDlgItemMessage(hdlg, IDC_APP_LIST, LB_GETCURSEL, 0, 0));
                if (sel < 0 || sel >= s_count) {
                    SetDlgItemText(hdlg, IDC_APP_STATUS, "Select a pending file first.");
                    return TRUE;
                }
                if (id == IDC_APP_APPROVE) {
                    char subdir[64], dest[160], err[96];
                    GetDlgItemText(hdlg, IDC_APP_SUBDIR, subdir, sizeof(subdir));
                    int exists = 0;
                    int ok = oyk_bbs_approve_incoming_file("incoming", g_node.config().bbs_files_path,
                                                           s_items[sel].uploader, s_items[sel].filename,
                                                           subdir, 0, dest, sizeof(dest),
                                                           &exists, err, sizeof(err));
                    if (!ok && exists) {
                        char prompt[240];
                        sprintf(prompt, "File already exists in BBS files area:\n%s\n\nOverwrite existing file?", dest);
                        if (MessageBox(hdlg, prompt, "Confirm Overwrite", MB_YESNO | MB_ICONEXCLAMATION) == IDYES) {
                            ok = oyk_bbs_approve_incoming_file("incoming", g_node.config().bbs_files_path,
                                                               s_items[sel].uploader, s_items[sel].filename,
                                                               subdir, 1, dest, sizeof(dest),
                                                               &exists, err, sizeof(err));
                        } else {
                            SetDlgItemText(hdlg, IDC_APP_STATUS, "Overwrite declined; file remains in incoming.");
                            return TRUE;
                        }
                    }
                    if (ok) {
                        char msg_s[160];
                        sprintf(msg_s, "Approved %s/%s -> %s", s_items[sel].uploader, s_items[sel].filename, dest);
                        g_node.log_message(OYK_LOG_INFO, msg_s);
                        set_status(msg_s);
                        SetDlgItemText(hdlg, IDC_APP_STATUS, msg_s);
                        populate_approve_list(hdlg, s_items, &s_count);
                        populate_approved_files_list(hdlg, s_approved, &s_app_count);
                    } else {
                        SetDlgItemText(hdlg, IDC_APP_STATUS, err);
                    }
                } else {
                    char prompt[128], err[96];
                    sprintf(prompt, "Delete incoming file %s\\%s?",
                            s_items[sel].uploader, s_items[sel].filename);
                    if (MessageBox(hdlg, prompt, "Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        if (oyk_bbs_delete_incoming_file("incoming", s_items[sel].uploader,
                                                         s_items[sel].filename, err, sizeof(err))) {
                            char msg_s[96];
                            sprintf(msg_s, "Deleted incoming file %s/%s",
                                    s_items[sel].uploader, s_items[sel].filename);
                            g_node.log_message(OYK_LOG_INFO, msg_s);
                            set_status(msg_s);
                            SetDlgItemText(hdlg, IDC_APP_STATUS, msg_s);
                            populate_approve_list(hdlg, s_items, &s_count);
                        } else {
                            SetDlgItemText(hdlg, IDC_APP_STATUS, err);
                        }
                    }
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

static int hit_test_splitter(int x, int y) {
    return (g_split_h > 0 &&
            x >= g_split_x - 2 && x <= g_split_x + OYK_SPLITTER_W + 2 &&
            y >= g_split_y && y <= g_split_y + g_split_h) ? 1 : 0;
}

static void paint_splitter_handle(HDC hdc) {
    if (g_split_h <= 4) return;
    RECT rc;
    rc.left = g_split_x;
    rc.top = g_split_y;
    rc.right = g_split_x + OYK_SPLITTER_W;
    rc.bottom = g_split_y + g_split_h;
    FillRect(hdc, &rc, OYK_HBRUSH(COLOR_BTNFACE + 1));

    HPEN hi_pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNHIGHLIGHT));
    HPEN sh_pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
    HPEN old_pen = (HPEN)SelectObject(hdc, hi_pen);

    int cx = g_split_x + OYK_SPLITTER_W / 2;
    int cy = g_split_y + g_split_h / 2;
    int grip_top = cy - 16;
    int grip_bot = cy + 16;
    if (grip_top < g_split_y + 4) grip_top = g_split_y + 4;
    if (grip_bot > g_split_y + g_split_h - 4) grip_bot = g_split_y + g_split_h - 4;

    /* Vertical 3D bevel track + tactile center grip lines */
    MoveToEx(hdc, cx - 1, grip_top, NULL);
    LineTo(hdc, cx - 1, grip_bot);
    SelectObject(hdc, sh_pen);
    MoveToEx(hdc, cx + 1, grip_top, NULL);
    LineTo(hdc, cx + 1, grip_bot);

    SelectObject(hdc, old_pen);
    DeleteObject(hi_pen);
    DeleteObject(sh_pen);
}

static void layout_controls(HWND hwnd, int cw, int ch) {
    if (cw <= 0 || ch <= 0) return;
    g_client_w = cw;
    g_client_h = ch;

    const int pad = 6;
    const int hdr_h = 20;
    const int st_h = 18;
    const int badge_w = 140;

    int hdr_w = cw - badge_w - pad * 3;
    if (hdr_w < 80) hdr_w = 80;

    int body_y = pad + hdr_h + pad;
    int body_h = ch - body_y - st_h - pad * 3;
    if (body_h < 120) body_h = 120;

    int top_h = body_h / 3;
    int mid_h = body_h / 3;
    int bot_h = body_h - top_h - mid_h - pad * 2;
    if (bot_h < 40) bot_h = 40;

    /* Use 32-bit long math so cw*pct NEVER overflows 16-bit signed int on Win16! */
    int avail_w = cw - pad * 2 - OYK_SPLITTER_W;
    if (avail_w < 120) avail_w = 120;
    int left_w = static_cast<int>((static_cast<long>(avail_w) * static_cast<long>(g_split_pct)) / 100L);
    int min_pane = (avail_w > 220) ? 100 : (avail_w / 3);
    if (left_w < min_pane) left_w = min_pane;
    if (left_w > avail_w - min_pane) left_w = avail_w - min_pane;
    int right_w = avail_w - left_w;
    if (right_w < 40) right_w = 40;

    g_split_x = pad + left_w;
    g_split_y = body_y;
    g_split_h = top_h;

    int st_y = ch - st_h - pad;

    SendMessage(hwnd, WM_SETREDRAW, FALSE, 0);
    MoveWindow(g_ctl_header, pad, pad, hdr_w, hdr_h, FALSE);
    MoveWindow(g_ctl_badge, cw - badge_w - pad, pad, badge_w, hdr_h, FALSE);
    MoveWindow(g_ctl_ifaces, pad, body_y, left_w, top_h, FALSE);
    MoveWindow(g_ctl_heard, g_split_x + OYK_SPLITTER_W, body_y, right_w, top_h, FALSE);
    MoveWindow(g_ctl_sessions, pad, body_y + top_h + pad, cw - pad * 2, mid_h, FALSE);
    MoveWindow(g_ctl_logs, pad, body_y + top_h + mid_h + pad * 2, cw - pad * 2, bot_h, FALSE);
    MoveWindow(g_ctl_status, pad, st_y, cw - pad * 2, st_h, FALSE);
    SendMessage(hwnd, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static void init_win_interfaces();
static void shutdown_win_interfaces();

static OykConfig g_set_draft;
static OykIfaceConfig g_add_if_out;
static int g_add_if_ok = 0;

/* ==================== Windows Winsock 1.1 Rig Control ==================== */

static OykRigStatus g_win_rig_st;
static SOCKET g_win_rf_sock = INVALID_SOCKET;

static int win_resolve_ipv4_host(const char* host, unsigned long* ip_out) {
    if (!host || !host[0] || !ip_out) return 0;
    unsigned long a = inet_addr(host);
    if (a != INADDR_NONE) { *ip_out = a; return 1; }
    struct hostent* he = gethostbyname(host);
    if (he && he->h_addr_list && he->h_addr_list[0]) {
        memcpy(ip_out, he->h_addr_list[0], 4);
        return 1;
    }
    return 0;
}

static int win_rf_send_cmd(const char* cmd, char* err_out, size_t err_sz) {
    const OykConfig& cfg = g_node.config();
    if (!cfg.rig_enabled || oyk_rig_parse_protocol(cfg.rig_protocol) != OYK_RIG_PROTO_RADIOFOUNDRY) {
        strncpy(err_out, "RadioFoundry UDP control is not enabled.", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    unsigned long ip = 0;
    if (!win_resolve_ipv4_host(cfg.rig_host, &ip)) {
        strncpy(err_out, "Unable to resolve radio host.", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    if (g_win_rf_sock == INVALID_SOCKET) {
        g_win_rf_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (g_win_rf_sock == INVALID_SOCKET) {
            strncpy(err_out, "Failed to create UDP socket.", err_sz - 1);
            err_out[err_sz - 1] = '\0';
            return 0;
        }
        struct sockaddr_in loc;
        memset(&loc, 0, sizeof(loc));
        loc.sin_family = AF_INET;
        loc.sin_addr.s_addr = INADDR_ANY;
        loc.sin_port = 0;
        bind(g_win_rf_sock, reinterpret_cast<struct sockaddr*>(&loc), sizeof(loc));
        u_long nb = 1;
        ioctlsocket(g_win_rf_sock, FIONBIO, &nb);
    }
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = ip;
    dst.sin_port = htons(cfg.rig_port ? cfg.rig_port : static_cast<oyk_u16>(OYK_RF_DEFAULT_PORT));
    if (sendto(g_win_rf_sock, cmd, static_cast<int>(strlen(cmd)), 0,
               reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst)) <= 0) {
        strncpy(err_out, "UDP sendto failed.", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(g_win_rf_sock, &rfds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 400000L;
    if (select(0, &rfds, NULL, NULL, &tv) > 0) {
        static char rbuf[256];
        struct sockaddr_in from;
        int flen = sizeof(from);
        int n = recvfrom(g_win_rf_sock, rbuf, sizeof(rbuf) - 1, 0,
                         reinterpret_cast<struct sockaddr*>(&from), &flen);
        if (n > 0) {
            rbuf[n] = '\0';
            if (oyk_rf_parse_status(rbuf, &g_win_rig_st)) {
                g_win_rig_st.reachable = 1;
                g_win_rig_st.last_error[0] = '\0';
                return 1;
            }
        }
    }
    g_win_rig_st.reachable = 0;
    strncpy(err_out, "No UDP reply from RadioFoundry radio.", err_sz - 1);
    err_out[err_sz - 1] = '\0';
    return 0;
}

static int win_rigctl_exchange(const char* cmd, char* rep_out, size_t rep_sz,
                               char* err_out, size_t err_sz) {
    rep_out[0] = '\0';
    const OykConfig& cfg = g_node.config();
    unsigned long ip = 0;
    if (!win_resolve_ipv4_host(cfg.rig_host, &ip)) {
        strncpy(err_out, "Unable to resolve rigctld host.", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        strncpy(err_out, "Failed to create TCP socket.", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = ip;
    dst.sin_port = htons(cfg.rig_port ? cfg.rig_port : static_cast<oyk_u16>(OYK_RIGCTL_DEFAULT_PORT));
    connect(s, reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst));

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(s, &wfds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000L;
    if (select(0, NULL, &wfds, NULL, &tv) <= 0) {
        closesocket(s);
        strncpy(err_out, "Unable to connect to rigctld.", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    send(s, cmd, static_cast<int>(strlen(cmd)), 0);

    size_t got = 0;
    DWORD start_ms = GetTickCount();
    while (static_cast<DWORD>(GetTickCount() - start_ms) < 500UL && got + 1 < rep_sz) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000L;
        if (select(0, &rfds, NULL, NULL, &tv) > 0) {
            int n = recv(s, rep_out + got, static_cast<int>(rep_sz - 1 - got), 0);
            if (n <= 0) break;
            got += static_cast<size_t>(n);
            rep_out[got] = '\0';
            if (strstr(rep_out, "RPRT ") != NULL || strchr(rep_out, '\n') != NULL) break;
        }
    }
    closesocket(s);
    if (got == 0) {
        strncpy(err_out, "No response from rigctld.", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    return 1;
}

static int win_rig_refresh(char* err_out, size_t err_sz) {
    const OykConfig& cfg = g_node.config();
    int proto = oyk_rig_parse_protocol(cfg.rig_protocol);
    if (!cfg.rig_enabled || proto == OYK_RIG_PROTO_NONE) {
        strncpy(err_out, "Rig control disabled (enable in Settings -> Rig).", err_sz - 1);
        err_out[err_sz - 1] = '\0';
        return 0;
    }
    if (proto == OYK_RIG_PROTO_RADIOFOUNDRY) {
        char cmd[16];
        oyk_rf_build_select(-1, -1, cmd, sizeof(cmd));
        return win_rf_send_cmd(cmd, err_out, err_sz);
    }
    char cmd[16], rep[128];
    oyk_rigctl_build_get_freq(cmd, sizeof(cmd));
    if (!win_rigctl_exchange(cmd, rep, sizeof(rep), err_out, err_sz)) {
        g_win_rig_st.reachable = 0;
        return 0;
    }
    oyk_rig_status_init(&g_win_rig_st, OYK_RIG_PROTO_RIGCTL);
    int rprt = 0;
    if (!oyk_rigctl_parse_freq_reply(rep, &g_win_rig_st.freq_hz, &rprt)) {
        sprintf(err_out, "rigctld error (RPRT %d)", rprt);
        return 0;
    }
    g_win_rig_st.reachable = 1;
    char oerr[64];
    oyk_rigctl_build_get_mode(cmd, sizeof(cmd));
    if (win_rigctl_exchange(cmd, rep, sizeof(rep), oerr, sizeof(oerr))) {
        oyk_rigctl_parse_mode_reply(rep, g_win_rig_st.mode, sizeof(g_win_rig_st.mode),
                                    &g_win_rig_st.passband_hz, &rprt);
    }
    oyk_rigctl_build_get_ptt(cmd, sizeof(cmd));
    if (win_rigctl_exchange(cmd, rep, sizeof(rep), oerr, sizeof(oerr))) {
        oyk_rigctl_parse_ptt_reply(rep, &g_win_rig_st.ptt, &rprt);
    }
    oyk_rigctl_build_get_vfo(cmd, sizeof(cmd));
    if (win_rigctl_exchange(cmd, rep, sizeof(rep), oerr, sizeof(oerr))) {
        oyk_rigctl_parse_vfo_reply(rep, g_win_rig_st.vfo, sizeof(g_win_rig_st.vfo), &rprt);
    }
    return 1;
}

static void win_rig_update_dlg(HWND hdlg) {
    const OykConfig& cfg = g_node.config();
    int proto = oyk_rig_parse_protocol(cfg.rig_protocol);
    static char hdr[128], body[256];
    sprintf(hdr, "Protocol: %s  |  Endpoint: %s:%u  [%s]",
            oyk_rig_protocol_name(proto),
            cfg.rig_host[0] ? cfg.rig_host : "127.0.0.1",
            static_cast<unsigned int>(cfg.rig_port),
            cfg.rig_enabled ? (g_win_rig_st.reachable ? "ONLINE" : "NO REPLY") : "DISABLED");
    SetDlgItemText(hdlg, IDC_RIG_HEADER, hdr);

    if (!cfg.rig_enabled || proto == OYK_RIG_PROTO_NONE) {
        strcpy(body, "Rig control is disabled.\r\nEnable it in Node -> Settings -> Rig.");
    } else if (proto == OYK_RIG_PROTO_RADIOFOUNDRY) {
        if (g_win_rig_st.is_dmr) {
            sprintf(body, "Zone: %d   Pos: %d   Name: %s\r\nFreq: %lu Hz   DMR CC:%d TS:%d TG:%ld\r\nRaw: %.90s",
                    g_win_rig_st.zone, g_win_rig_st.pos,
                    g_win_rig_st.channel_name[0] ? g_win_rig_st.channel_name : "(unknown)",
                    static_cast<unsigned long>(g_win_rig_st.freq_hz),
                    g_win_rig_st.color_code, g_win_rig_st.timeslot, g_win_rig_st.talkgroup,
                    g_win_rig_st.raw_status);
        } else {
            sprintf(body, "Zone: %d   Pos: %d   Name: %s\r\nFreq: %lu Hz\r\nRaw: %.90s",
                    g_win_rig_st.zone, g_win_rig_st.pos,
                    g_win_rig_st.channel_name[0] ? g_win_rig_st.channel_name : "(unknown)",
                    static_cast<unsigned long>(g_win_rig_st.freq_hz),
                    g_win_rig_st.raw_status);
        }
    } else {
        sprintf(body, "Frequency: %lu Hz\r\nMode: %s (%lu Hz)   VFO: %s\r\nPTT: %s",
                static_cast<unsigned long>(g_win_rig_st.freq_hz),
                g_win_rig_st.mode[0] ? g_win_rig_st.mode : "(unknown)",
                static_cast<unsigned long>(g_win_rig_st.passband_hz),
                g_win_rig_st.vfo[0] ? g_win_rig_st.vfo : "(unknown)",
                g_win_rig_st.ptt > 0 ? "TX" : (g_win_rig_st.ptt == 0 ? "RX" : "UNKNOWN"));
    }
    SetDlgItemText(hdlg, IDC_RIG_STATE, body);
}

OYK_DLG_RET RigControlDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    const OykConfig& cfg = g_node.config();
    int proto = oyk_rig_parse_protocol(cfg.rig_protocol);
    switch (msg) {
        case WM_INITDIALOG: {
            if (g_win_rig_st.protocol != proto) oyk_rig_status_init(&g_win_rig_st, proto);
            if (proto == OYK_RIG_PROTO_RADIOFOUNDRY) {
                SetDlgItemText(hdlg, IDC_RIG_L_A, "Zone (0..):");
                SetDlgItemText(hdlg, IDC_RIG_L_B, "Pos (0..):");
                SetDlgItemText(hdlg, IDC_RIG_L_C, "");
                ShowWindow(GetDlgItem(hdlg, IDC_RIG_L_C), SW_HIDE);
                ShowWindow(GetDlgItem(hdlg, IDC_RIG_EDIT_C), SW_HIDE);
                SetDlgItemText(hdlg, IDC_RIG_EDIT_A, "0");
                SetDlgItemText(hdlg, IDC_RIG_EDIT_B, "0");
                SetDlgItemText(hdlg, IDC_RIG_BTN_1, "Select Z/Pos");
                SetDlgItemText(hdlg, IDC_RIG_BTN_2, "Channel -1");
                SetDlgItemText(hdlg, IDC_RIG_BTN_3, "Channel +1");
                SetDlgItemText(hdlg, IDC_RIG_BTN_4, "Zone -1");
                SetDlgItemText(hdlg, IDC_RIG_BTN_5, "Zone +1");
            } else {
                SetDlgItemText(hdlg, IDC_RIG_L_A, "Freq (Hz):");
                SetDlgItemText(hdlg, IDC_RIG_L_B, "Mode:");
                SetDlgItemText(hdlg, IDC_RIG_L_C, "Passband:");
                SetDlgItemText(hdlg, IDC_RIG_EDIT_A, "145050000");
                SetDlgItemText(hdlg, IDC_RIG_EDIT_B, "FM");
                SetDlgItemText(hdlg, IDC_RIG_EDIT_C, "0");
                SetDlgItemText(hdlg, IDC_RIG_BTN_1, "Set Freq");
                SetDlgItemText(hdlg, IDC_RIG_BTN_2, "Set Mode");
                SetDlgItemText(hdlg, IDC_RIG_BTN_3, "Toggle PTT");
                ShowWindow(GetDlgItem(hdlg, IDC_RIG_BTN_4), SW_HIDE);
                ShowWindow(GetDlgItem(hdlg, IDC_RIG_BTN_5), SW_HIDE);
            }
            char err[96];
            if (win_rig_refresh(err, sizeof(err))) {
                SetDlgItemText(hdlg, IDC_RIG_STATUS, "Queried radio status.");
            } else {
                SetDlgItemText(hdlg, IDC_RIG_STATUS, err);
            }
            win_rig_update_dlg(hdlg);
            return TRUE;
        }
        case WM_COMMAND: {
            WORD id = OYK_CMD_ID(wp, lp);
            char err[96], cmd[64], rep[128], ea[32], eb[32], ec[32];
            if (id == IDC_RIG_BTN_REF) {
                if (win_rig_refresh(err, sizeof(err))) SetDlgItemText(hdlg, IDC_RIG_STATUS, "Refreshed.");
                else SetDlgItemText(hdlg, IDC_RIG_STATUS, err);
                win_rig_update_dlg(hdlg);
                return TRUE;
            }
            if (proto == OYK_RIG_PROTO_RADIOFOUNDRY) {
                if (id == IDC_RIG_BTN_1) {
                    GetDlgItemText(hdlg, IDC_RIG_EDIT_A, ea, sizeof(ea));
                    GetDlgItemText(hdlg, IDC_RIG_EDIT_B, eb, sizeof(eb));
                    oyk_rf_build_select(atoi(ea), atoi(eb), cmd, sizeof(cmd));
                    if (win_rf_send_cmd(cmd, err, sizeof(err))) SetDlgItemText(hdlg, IDC_RIG_STATUS, "Selected zone & pos.");
                    else SetDlgItemText(hdlg, IDC_RIG_STATUS, err);
                    win_rig_update_dlg(hdlg);
                    return TRUE;
                }
                if (id == IDC_RIG_BTN_2 || id == IDC_RIG_BTN_3) {
                    oyk_rf_build_step_channel(id == IDC_RIG_BTN_3 ? 1 : -1, cmd, sizeof(cmd));
                    if (win_rf_send_cmd(cmd, err, sizeof(err))) SetDlgItemText(hdlg, IDC_RIG_STATUS, "Stepped channel.");
                    else SetDlgItemText(hdlg, IDC_RIG_STATUS, err);
                    win_rig_update_dlg(hdlg);
                    return TRUE;
                }
                if (id == IDC_RIG_BTN_4 || id == IDC_RIG_BTN_5) {
                    oyk_rf_build_step_zone(id == IDC_RIG_BTN_5 ? 1 : -1, cmd, sizeof(cmd));
                    if (win_rf_send_cmd(cmd, err, sizeof(err))) SetDlgItemText(hdlg, IDC_RIG_STATUS, "Stepped zone.");
                    else SetDlgItemText(hdlg, IDC_RIG_STATUS, err);
                    win_rig_update_dlg(hdlg);
                    return TRUE;
                }
            } else if (proto == OYK_RIG_PROTO_RIGCTL) {
                if (id == IDC_RIG_BTN_1) {
                    GetDlgItemText(hdlg, IDC_RIG_EDIT_A, ea, sizeof(ea));
                    oyk_u32 hz = static_cast<oyk_u32>(strtoul(ea, NULL, 10));
                    oyk_rigctl_build_set_freq(hz, cmd, sizeof(cmd));
                    if (win_rigctl_exchange(cmd, rep, sizeof(rep), err, sizeof(err))) {
                        g_win_rig_st.freq_hz = hz;
                        SetDlgItemText(hdlg, IDC_RIG_STATUS, "Frequency updated.");
                    } else SetDlgItemText(hdlg, IDC_RIG_STATUS, err);
                    win_rig_update_dlg(hdlg);
                    return TRUE;
                }
                if (id == IDC_RIG_BTN_2) {
                    GetDlgItemText(hdlg, IDC_RIG_EDIT_B, eb, sizeof(eb));
                    GetDlgItemText(hdlg, IDC_RIG_EDIT_C, ec, sizeof(ec));
                    oyk_u32 pb = static_cast<oyk_u32>(strtoul(ec, NULL, 10));
                    oyk_rigctl_build_set_mode(eb, pb, cmd, sizeof(cmd));
                    if (win_rigctl_exchange(cmd, rep, sizeof(rep), err, sizeof(err))) {
                        strncpy(g_win_rig_st.mode, eb, sizeof(g_win_rig_st.mode) - 1);
                        g_win_rig_st.passband_hz = pb;
                        SetDlgItemText(hdlg, IDC_RIG_STATUS, "Mode updated.");
                    } else SetDlgItemText(hdlg, IDC_RIG_STATUS, err);
                    win_rig_update_dlg(hdlg);
                    return TRUE;
                }
                if (id == IDC_RIG_BTN_3) {
                    int next_ptt = (g_win_rig_st.ptt > 0) ? 0 : 1;
                    oyk_rigctl_build_set_ptt(next_ptt, cmd, sizeof(cmd));
                    if (win_rigctl_exchange(cmd, rep, sizeof(rep), err, sizeof(err))) {
                        g_win_rig_st.ptt = next_ptt;
                        SetDlgItemText(hdlg, IDC_RIG_STATUS, next_ptt ? "PTT set to TX." : "PTT set to RX.");
                    } else SetDlgItemText(hdlg, IDC_RIG_STATUS, err);
                    win_rig_update_dlg(hdlg);
                    return TRUE;
                }
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

static void populate_settings_ifaces(HWND hdlg) {
    HWND hlist = GetDlgItem(hdlg, IDC_SET_IF_LIST);
    SendMessage(hlist, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_set_draft.iface_count; ++i) {
        const OykIfaceConfig& ic = g_set_draft.ifaces[i];
        char row[112];
        if (win_strieq(ic.type, "kiss-serial")) {
            sprintf(row, "%-10.10s %-11.11s %s @%lu",
                    ic.id, ic.type, ic.device[0] ? ic.device : "COM1",
                    static_cast<unsigned long>(ic.baud ? ic.baud : 9600UL));
        } else {
            sprintf(row, "%-10.10s %-11.11s %s:%u",
                    ic.id, ic.type, ic.host[0] ? ic.host : "0.0.0.0",
                    static_cast<unsigned int>(ic.port));
        }
        SendMessage(hlist, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row));
    }
}

struct WinDriverOpt {
    const char* type;
    const char* desc;
    const char* def_host;
    const char* def_port;
};

static const WinDriverOpt WIN_DRIVERS[5] = {
    { "axudp",       "AX.25 over UDP (BPQ / AXUDP)", "0.0.0.0",   "10093" },
    { "axip",        "AX.25 over IP (Protocol 93)",  "0.0.0.0",   "0" },
    { "kiss-serial", "KISS TNC over Serial (COM)",   "COM1",      "9600" },
    { "kiss-tcp",    "KISS TNC over TCP (Direwolf)", "127.0.0.1", "8001" },
    { "telnet",      "Plaintext Telnet User Port",   "0.0.0.0",   "8010" }
};

static int g_wiz_step = 0;
static int g_wiz_drv = 0;
static int g_wiz_last_drv = -1;
static int g_edit_if_idx = -1;
static int g_edit_if_drv = 0;

static void win_build_iface_config(int drv,
                                   const char* id_b,
                                   const char* desc_b,
                                   const char* host_b,
                                   const char* port_b,
                                   const char* peer_b,
                                   const OykIfaceConfig* prev_ic,
                                   OykIfaceConfig* out_ic) {
    memset(out_ic, 0, sizeof(*out_ic));
    strncpy(out_ic->id, id_b, sizeof(out_ic->id) - 1);
    strcpy(out_ic->type, WIN_DRIVERS[drv].type);
    strncpy(out_ic->description, desc_b, sizeof(out_ic->description) - 1);

    if (win_strieq(out_ic->type, "kiss-serial")) {
        strncpy(out_ic->device, host_b[0] ? host_b : "COM1", sizeof(out_ic->device) - 1);
        long bv = atol(port_b);
        out_ic->baud = (bv > 0L) ? static_cast<oyk_u32>(bv) : 9600UL;
    } else {
        strncpy(out_ic->host, host_b[0] ? host_b : "0.0.0.0", sizeof(out_ic->host) - 1);
        long pv = atol(port_b);
        if (pv > 0L && pv <= 65535L) out_ic->port = static_cast<oyk_u16>(pv);
        else if (win_strieq(out_ic->type, "telnet")) out_ic->port = 8010;
        else if (win_strieq(out_ic->type, "kiss-tcp")) out_ic->port = 8001;
        else if (win_strieq(out_ic->type, "axudp")) out_ic->port = 10093;
        if (prev_ic && (drv == 0 || drv == 1)) {
            if (win_strieq(prev_ic->type, "axudp") || win_strieq(prev_ic->type, "axip")) {
                for (int p = 1; p < prev_ic->peer_count && out_ic->peer_count < OYK_MAX_PEERS; ++p) {
                    out_ic->peers[out_ic->peer_count++] = prev_ic->peers[p];
                }
            }
        }
        if (peer_b[0] && (drv == 0 || drv == 1)) {
            OykPeerEntry pe;
            memset(&pe, 0, sizeof(pe));
            if (oyk_parse_peer_spec(peer_b, pe.callsign, sizeof(pe.callsign),
                                    &pe.ip, &pe.port,
                                    out_ic->port ? out_ic->port : static_cast<oyk_u16>(10093))) {
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

static void sync_win_wiz_ui(HWND hdlg) {
    static const char* PROG[4] = {
        "[##......] 1/4", "[####....] 2/4", "[######..] 3/4", "[########] 4/4"
    };
    SetDlgItemText(hdlg, IDC_AIF_PROG, PROG[g_wiz_step]);
    SetDlgItemText(hdlg, IDC_AIF_ERR, "");
    EnableWindow(GetDlgItem(hdlg, IDC_AIF_BACK), g_wiz_step > 0 ? TRUE : FALSE);
    SetDlgItemText(hdlg, IDC_AIF_NEXT, (g_wiz_step == 3) ? "&Finish" : "&Next >");

    const char* drv = WIN_DRIVERS[g_wiz_drv].type;
    if (g_wiz_step == 0) {
        SetDlgItemText(hdlg, IDC_AIF_PROMPT, "Select the driver for the new interface:");
    } else if (g_wiz_step == 1) {
        SetDlgItemText(hdlg, IDC_AIF_PROMPT, "Enter a unique interface ID and optional description:");
    } else if (g_wiz_step == 2) {
        if (g_wiz_drv == 0) SetDlgItemText(hdlg, IDC_AIF_PROMPT, "Configure AXUDP bind host, UDP port, and optional peer:");
        else if (g_wiz_drv == 1) SetDlgItemText(hdlg, IDC_AIF_PROMPT, "Configure AXIP bind host and optional static peer:");
        else if (g_wiz_drv == 2) SetDlgItemText(hdlg, IDC_AIF_PROMPT, "Configure KISS serial COM port and baud rate:");
        else if (g_wiz_drv == 3) SetDlgItemText(hdlg, IDC_AIF_PROMPT, "Configure KISS-TCP host and TCP port:");
        else SetDlgItemText(hdlg, IDC_AIF_PROMPT, "Configure Telnet bind host and TCP port:");

        SetDlgItemText(hdlg, IDC_AIF_L_HOST, (g_wiz_drv == 2) ? "Serial Device:" : ((g_wiz_drv == 3) ? "Host / IP:" : "Bind Host:"));
        SetDlgItemText(hdlg, IDC_AIF_L_PORT, (g_wiz_drv == 2) ? "Baud Rate:" : ((g_wiz_drv == 0) ? "UDP Port:" : "TCP Port:"));
    } else {
        SetDlgItemText(hdlg, IDC_AIF_PROMPT, "Review interface settings before committing:");
        char id_b[24], desc_b[64], host_b[48], port_b[16], peer_b[64], sum[360];
        GetDlgItemText(hdlg, IDC_AIF_ID, id_b, sizeof(id_b));
        GetDlgItemText(hdlg, IDC_AIF_DESC, desc_b, sizeof(desc_b));
        GetDlgItemText(hdlg, IDC_AIF_HOST, host_b, sizeof(host_b));
        GetDlgItemText(hdlg, IDC_AIF_PORT, port_b, sizeof(port_b));
        GetDlgItemText(hdlg, IDC_AIF_PEER, peer_b, sizeof(peer_b));
        if (g_wiz_drv == 2) {
            sprintf(sum,
                    "Driver:        %s\r\nInterface ID:  %s\r\nDescription:   %s\r\nSerial Device: %s\r\nBaud Rate:     %s",
                    drv, id_b, desc_b[0] ? desc_b : "(none)", host_b, port_b);
        } else if (g_wiz_drv == 1) {
            sprintf(sum,
                    "Driver:        %s\r\nInterface ID:  %s\r\nDescription:   %s\r\nBind Host:     %s\r\nStatic Peer:   %s",
                    drv, id_b, desc_b[0] ? desc_b : "(none)", host_b, peer_b[0] ? peer_b : "(auto-learn)");
        } else if (g_wiz_drv == 0) {
            sprintf(sum,
                    "Driver:        %s\r\nInterface ID:  %s\r\nDescription:   %s\r\nBind Host:     %s\r\nUDP Port:      %s\r\nStatic Peer:   %s",
                    drv, id_b, desc_b[0] ? desc_b : "(none)", host_b, port_b, peer_b[0] ? peer_b : "(auto-learn)");
        } else {
            sprintf(sum,
                    "Driver:        %s\r\nInterface ID:  %s\r\nDescription:   %s\r\nHost / IP:     %s\r\nTCP Port:      %s",
                    drv, id_b, desc_b[0] ? desc_b : "(none)", host_b, port_b);
        }
        SetDlgItemText(hdlg, IDC_AIF_SUMMARY, sum);
    }

    ShowWindow(GetDlgItem(hdlg, IDC_AIF_DRV_LIST), (g_wiz_step == 0) ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hdlg, IDC_AIF_L_ID),     (g_wiz_step == 1) ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hdlg, IDC_AIF_ID),       (g_wiz_step == 1) ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hdlg, IDC_AIF_L_DESC),   (g_wiz_step == 1) ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hdlg, IDC_AIF_DESC),     (g_wiz_step == 1) ? SW_SHOW : SW_HIDE);

    int show_s2 = (g_wiz_step == 2) ? 1 : 0;
    int show_port = (show_s2 && g_wiz_drv != 1) ? 1 : 0;
    int show_peer = (show_s2 && (g_wiz_drv == 0 || g_wiz_drv == 1)) ? 1 : 0;
    ShowWindow(GetDlgItem(hdlg, IDC_AIF_L_HOST), show_s2 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hdlg, IDC_AIF_HOST),   show_s2 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hdlg, IDC_AIF_L_PORT), show_port ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hdlg, IDC_AIF_PORT),   show_port ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hdlg, IDC_AIF_L_PEER), show_peer ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hdlg, IDC_AIF_PEER),   show_peer ? SW_SHOW : SW_HIDE);

    ShowWindow(GetDlgItem(hdlg, IDC_AIF_SUMMARY), (g_wiz_step == 3) ? SW_SHOW : SW_HIDE);

    if (g_wiz_step == 0) SetFocus(GetDlgItem(hdlg, IDC_AIF_DRV_LIST));
    else if (g_wiz_step == 1) SetFocus(GetDlgItem(hdlg, IDC_AIF_ID));
    else if (g_wiz_step == 2) SetFocus(GetDlgItem(hdlg, IDC_AIF_HOST));
}

OYK_DLG_RET AddIfaceDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
#if defined(_WIN32) || defined(_WIN64)
    (void)lp;
#endif
    switch (msg) {
        case WM_INITDIALOG: {
            HWND hlist = GetDlgItem(hdlg, IDC_AIF_DRV_LIST);
            for (int i = 0; i < 5; ++i) {
                char row[80];
                sprintf(row, "%-12s %s", WIN_DRIVERS[i].type, WIN_DRIVERS[i].desc);
                SendMessage(hlist, LB_ADDSTRING, 0, OYK_LPARAM_STR(row));
            }
            SetWindowText(hdlg, "Add Interface");
            g_wiz_step = 0;
            g_wiz_drv = 0;
            g_wiz_last_drv = -1;
            g_add_if_ok = 0;
            SendMessage(hlist, LB_SETCURSEL, 0, 0);
            sync_win_wiz_ui(hdlg);
            return FALSE;
        }
        case WM_COMMAND: {
            WORD id = OYK_CMD_ID(wp, lp);
            WORD ntf = OYK_CMD_NOTIFY(wp, lp);
            if (id == IDC_AIF_BACK) {
                if (g_wiz_step > 0) {
                    --g_wiz_step;
                    sync_win_wiz_ui(hdlg);
                }
                return TRUE;
            }
            if (id == IDC_AIF_NEXT || id == IDOK || (id == IDC_AIF_DRV_LIST && ntf == LBN_DBLCLK && g_wiz_step == 0)) {
                if (g_wiz_step == 0) {
                    int sel = static_cast<int>(SendDlgItemMessage(hdlg, IDC_AIF_DRV_LIST, LB_GETCURSEL, 0, 0));
                    if (sel < 0 || sel >= 5) sel = 0;
                    g_wiz_drv = sel;
                    if (g_wiz_drv != g_wiz_last_drv) {
                        char next_id[24];
                        oyk_derive_default_iface_id(&g_set_draft, WIN_DRIVERS[g_wiz_drv].type,
                                                    next_id, sizeof(next_id));
                        SetDlgItemText(hdlg, IDC_AIF_ID, next_id);
                        SetDlgItemText(hdlg, IDC_AIF_DESC, "");
                        SetDlgItemText(hdlg, IDC_AIF_HOST, WIN_DRIVERS[g_wiz_drv].def_host);
                        SetDlgItemText(hdlg, IDC_AIF_PORT, WIN_DRIVERS[g_wiz_drv].def_port);
                        SetDlgItemText(hdlg, IDC_AIF_PEER, "");
                        g_wiz_last_drv = g_wiz_drv;
                    }
                    g_wiz_step = 1;
                    sync_win_wiz_ui(hdlg);
                    return TRUE;
                }
                if (g_wiz_step == 1) {
                    char id_b[24];
                    GetDlgItemText(hdlg, IDC_AIF_ID, id_b, sizeof(id_b));
                    if (!id_b[0]) {
                        SetDlgItemText(hdlg, IDC_AIF_ERR, "Interface ID cannot be empty.");
                        SetFocus(GetDlgItem(hdlg, IDC_AIF_ID));
                        return TRUE;
                    }
                    for (int i = 0; i < g_set_draft.iface_count; ++i) {
                        if (win_strieq(g_set_draft.ifaces[i].id, id_b)) {
                            SetDlgItemText(hdlg, IDC_AIF_ERR, "An interface with that ID already exists.");
                            SetFocus(GetDlgItem(hdlg, IDC_AIF_ID));
                            return TRUE;
                        }
                    }
                    g_wiz_step = 2;
                    sync_win_wiz_ui(hdlg);
                    return TRUE;
                }
                if (g_wiz_step == 2) {
                    char host_b[32], port_b[16];
                    GetDlgItemText(hdlg, IDC_AIF_HOST, host_b, sizeof(host_b));
                    GetDlgItemText(hdlg, IDC_AIF_PORT, port_b, sizeof(port_b));
                    if (!host_b[0]) {
                        SetDlgItemText(hdlg, IDC_AIF_ERR, "Host / Serial Device cannot be empty.");
                        SetFocus(GetDlgItem(hdlg, IDC_AIF_HOST));
                        return TRUE;
                    }
                    if (g_wiz_drv != 1 && !port_b[0]) {
                        SetDlgItemText(hdlg, IDC_AIF_ERR, "Port / Baud Rate cannot be empty.");
                        SetFocus(GetDlgItem(hdlg, IDC_AIF_PORT));
                        return TRUE;
                    }
                    g_wiz_step = 3;
                    sync_win_wiz_ui(hdlg);
                    return TRUE;
                }
                char id_b[24], desc_b[64], host_b[32], port_b[16], peer_b[64];
                GetDlgItemText(hdlg, IDC_AIF_ID, id_b, sizeof(id_b));
                GetDlgItemText(hdlg, IDC_AIF_DESC, desc_b, sizeof(desc_b));
                GetDlgItemText(hdlg, IDC_AIF_HOST, host_b, sizeof(host_b));
                GetDlgItemText(hdlg, IDC_AIF_PORT, port_b, sizeof(port_b));
                GetDlgItemText(hdlg, IDC_AIF_PEER, peer_b, sizeof(peer_b));
                win_build_iface_config(g_wiz_drv, id_b, desc_b, host_b, port_b, peer_b, NULL, &g_add_if_out);
                g_add_if_ok = 1;
                EndDialog(hdlg, IDOK);
                return TRUE;
            } else if (id == IDCANCEL) {
                EndDialog(hdlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

static void sync_win_edit_iface_ui(HWND hdlg, int drv) {
    SetDlgItemText(hdlg, IDC_EIF_L_HOST, (drv == 2) ? "Serial Device:" : ((drv == 3) ? "Host / IP:" : "Bind Host:"));
    SetDlgItemText(hdlg, IDC_EIF_L_PORT, (drv == 2) ? "Baud Rate:" : ((drv == 0) ? "UDP Port:" : "TCP Port:"));
    int show_port = (drv != 1) ? 1 : 0;
    int show_peer = (drv == 0 || drv == 1) ? 1 : 0;
    ShowWindow(GetDlgItem(hdlg, IDC_EIF_L_PORT), show_port ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hdlg, IDC_EIF_PORT),   show_port ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hdlg, IDC_EIF_L_PEER), show_peer ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hdlg, IDC_EIF_PEER),   show_peer ? SW_SHOW : SW_HIDE);
}

OYK_DLG_RET EditIfaceDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
#if defined(_WIN32) || defined(_WIN64)
    (void)lp;
#endif
    switch (msg) {
        case WM_INITDIALOG: {
            if (g_edit_if_idx < 0 || g_edit_if_idx >= g_set_draft.iface_count) {
                EndDialog(hdlg, IDCANCEL);
                return FALSE;
            }
            HWND hcb = GetDlgItem(hdlg, IDC_EIF_DRV);
            const OykIfaceConfig& ic = g_set_draft.ifaces[g_edit_if_idx];
            g_edit_if_drv = 0;
            for (int i = 0; i < 5; ++i) {
                char row[80];
                sprintf(row, "%-12s %s", WIN_DRIVERS[i].type, WIN_DRIVERS[i].desc);
                SendMessage(hcb, CB_ADDSTRING, 0, OYK_LPARAM_STR(row));
                if (win_strieq(ic.type, WIN_DRIVERS[i].type)) g_edit_if_drv = i;
            }
            SendMessage(hcb, CB_SETCURSEL, static_cast<WPARAM>(g_edit_if_drv), 0);
            g_add_if_ok = 0;
            SetDlgItemText(hdlg, IDC_EIF_ID, ic.id);
            SetDlgItemText(hdlg, IDC_EIF_DESC, ic.description);
            SetDlgItemText(hdlg, IDC_EIF_ERR, "");
            if (g_edit_if_drv == 2) {
                char bb[16];
                SetDlgItemText(hdlg, IDC_EIF_HOST, ic.device[0] ? ic.device : "COM1");
                sprintf(bb, "%lu", static_cast<unsigned long>(ic.baud ? ic.baud : 9600UL));
                SetDlgItemText(hdlg, IDC_EIF_PORT, bb);
                SetDlgItemText(hdlg, IDC_EIF_PEER, "");
            } else {
                char pb[16], peer_s[64];
                SetDlgItemText(hdlg, IDC_EIF_HOST, ic.host[0] ? ic.host : "0.0.0.0");
                sprintf(pb, "%u", static_cast<unsigned int>(ic.port));
                SetDlgItemText(hdlg, IDC_EIF_PORT, pb);
                peer_s[0] = '\0';
                if (ic.peer_count > 0 && (g_edit_if_drv == 0 || g_edit_if_drv == 1)) {
                    oyk_format_peer_spec(&ic.peers[0], peer_s, sizeof(peer_s));
                }
                SetDlgItemText(hdlg, IDC_EIF_PEER, peer_s);
            }
            sync_win_edit_iface_ui(hdlg, g_edit_if_drv);
            SetFocus(GetDlgItem(hdlg, IDC_EIF_ID));
            return FALSE;
        }
        case WM_COMMAND: {
            WORD id = OYK_CMD_ID(wp, lp);
            WORD ntf = OYK_CMD_NOTIFY(wp, lp);
            if (id == IDC_EIF_DRV && ntf == CBN_SELCHANGE) {
                int sel = static_cast<int>(SendDlgItemMessage(hdlg, IDC_EIF_DRV, CB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < 5 && sel != g_edit_if_drv) {
                    g_edit_if_drv = sel;
                    SetDlgItemText(hdlg, IDC_EIF_HOST, WIN_DRIVERS[g_edit_if_drv].def_host);
                    SetDlgItemText(hdlg, IDC_EIF_PORT, WIN_DRIVERS[g_edit_if_drv].def_port);
                    SetDlgItemText(hdlg, IDC_EIF_PEER, "");
                    SetDlgItemText(hdlg, IDC_EIF_ERR, "");
                    sync_win_edit_iface_ui(hdlg, g_edit_if_drv);
                }
                return TRUE;
            }
            if (id == IDOK) {
                char id_b[24], desc_b[64], host_b[32], port_b[16], peer_b[64];
                GetDlgItemText(hdlg, IDC_EIF_ID, id_b, sizeof(id_b));
                GetDlgItemText(hdlg, IDC_EIF_DESC, desc_b, sizeof(desc_b));
                GetDlgItemText(hdlg, IDC_EIF_HOST, host_b, sizeof(host_b));
                GetDlgItemText(hdlg, IDC_EIF_PORT, port_b, sizeof(port_b));
                GetDlgItemText(hdlg, IDC_EIF_PEER, peer_b, sizeof(peer_b));
                if (!id_b[0]) {
                    SetDlgItemText(hdlg, IDC_EIF_ERR, "Interface ID cannot be empty.");
                    SetFocus(GetDlgItem(hdlg, IDC_EIF_ID));
                    return TRUE;
                }
                for (int i = 0; i < g_set_draft.iface_count; ++i) {
                    if (i == g_edit_if_idx) continue;
                    if (win_strieq(g_set_draft.ifaces[i].id, id_b)) {
                        SetDlgItemText(hdlg, IDC_EIF_ERR, "An interface with that ID already exists.");
                        SetFocus(GetDlgItem(hdlg, IDC_EIF_ID));
                        return TRUE;
                    }
                }
                if (!host_b[0]) {
                    SetDlgItemText(hdlg, IDC_EIF_ERR, "Host / Serial Device cannot be empty.");
                    SetFocus(GetDlgItem(hdlg, IDC_EIF_HOST));
                    return TRUE;
                }
                if (g_edit_if_drv != 1 && !port_b[0]) {
                    SetDlgItemText(hdlg, IDC_EIF_ERR, "Port / Baud Rate cannot be empty.");
                    SetFocus(GetDlgItem(hdlg, IDC_EIF_PORT));
                    return TRUE;
                }
                const OykIfaceConfig* prev =
                    (g_edit_if_idx >= 0 && g_edit_if_idx < g_set_draft.iface_count)
                        ? &g_set_draft.ifaces[g_edit_if_idx]
                        : NULL;
                win_build_iface_config(g_edit_if_drv, id_b, desc_b, host_b, port_b, peer_b, prev, &g_add_if_out);
                g_add_if_ok = 1;
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

static void switch_settings_tab(HWND hdlg, int tab) {
    static const int NODE_IDS[] = {
        IDC_SET_L_CALL, IDC_SET_CALL, IDC_SET_L_ALIAS, IDC_SET_ALIAS,
        IDC_SET_L_WELC, IDC_SET_WELCOME, IDC_SET_L_INFO, IDC_SET_INFO, IDC_SET_ACCEPT
    };
    static const int BBS_IDS[] = {
        IDC_SET_BBS_EN, IDC_SET_L_BSSID, IDC_SET_BBS_SSID, IDC_SET_L_BCALL, IDC_SET_BBS_CALL,
        IDC_SET_L_BALIAS, IDC_SET_BBS_ALIAS, IDC_SET_FILES_EN, IDC_SET_L_FPATH, IDC_SET_FILES_PATH,
        IDC_SET_L_MPATH, IDC_SET_MAIL_PATH, IDC_SET_L_BWELC, IDC_SET_BBS_WELC
    };
    static const int CHAT_IDS[] = {
        IDC_SET_CHAT_EN, IDC_SET_L_CSSID, IDC_SET_CHAT_SSID, IDC_SET_L_CCALL, IDC_SET_CHAT_CALL,
        IDC_SET_L_CALIAS, IDC_SET_CHAT_ALIAS, IDC_SET_L_CTOP, IDC_SET_CHAT_TOPIC,
        IDC_SET_L_CWELC, IDC_SET_CHAT_WELC
    };
    static const int IF_IDS[] = {
        IDC_SET_L_IF, IDC_SET_IF_LIST, IDC_SET_IF_ADD, IDC_SET_IF_EDIT, IDC_SET_IF_REM
    };
    static const int RIG_IDS[] = {
        IDC_SET_RIG_EN, IDC_SET_L_RPROTO, IDC_SET_RIG_PROTO,
        IDC_SET_L_RHOST, IDC_SET_RIG_HOST, IDC_SET_L_RPORT, IDC_SET_RIG_PORT,
        IDC_SET_L_RPOLL, IDC_SET_RIG_POLL, IDC_SET_RIG_NOTIFY
    };

    int i;
    for (i = 0; i < static_cast<int>(sizeof(NODE_IDS) / sizeof(NODE_IDS[0])); ++i) {
        ShowWindow(GetDlgItem(hdlg, NODE_IDS[i]), (tab == 0) ? SW_SHOW : SW_HIDE);
    }
    for (i = 0; i < static_cast<int>(sizeof(BBS_IDS) / sizeof(BBS_IDS[0])); ++i) {
        ShowWindow(GetDlgItem(hdlg, BBS_IDS[i]), (tab == 1) ? SW_SHOW : SW_HIDE);
    }
    for (i = 0; i < static_cast<int>(sizeof(CHAT_IDS) / sizeof(CHAT_IDS[0])); ++i) {
        ShowWindow(GetDlgItem(hdlg, CHAT_IDS[i]), (tab == 2) ? SW_SHOW : SW_HIDE);
    }
    for (i = 0; i < static_cast<int>(sizeof(IF_IDS) / sizeof(IF_IDS[0])); ++i) {
        ShowWindow(GetDlgItem(hdlg, IF_IDS[i]), (tab == 3) ? SW_SHOW : SW_HIDE);
    }
    for (i = 0; i < static_cast<int>(sizeof(RIG_IDS) / sizeof(RIG_IDS[0])); ++i) {
        ShowWindow(GetDlgItem(hdlg, RIG_IDS[i]), (tab == 4) ? SW_SHOW : SW_HIDE);
    }
    static const int PRN_IDS[] = {
        IDC_SET_PRN_EN, IDC_SET_L_PDEV, IDC_SET_PRN_DEV,
        IDC_SET_L_PPROTO, IDC_SET_PRN_PROTO, IDC_SET_L_PBAUD, IDC_SET_PRN_BAUD,
        IDC_SET_L_PLVL, IDC_SET_PRN_LVL, IDC_SET_PRN_COND
    };
    for (i = 0; i < static_cast<int>(sizeof(PRN_IDS) / sizeof(PRN_IDS[0])); ++i) {
        ShowWindow(GetDlgItem(hdlg, PRN_IDS[i]), (tab == 5) ? SW_SHOW : SW_HIDE);
    }

    SetDlgItemText(hdlg, IDC_SET_TAB_NODE, (tab == 0) ? "[ Node ]" : "Node");
    SetDlgItemText(hdlg, IDC_SET_TAB_BBS,  (tab == 1) ? "[ BBS ]" : "BBS");
    SetDlgItemText(hdlg, IDC_SET_TAB_CHAT, (tab == 2) ? "[ CHAT ]" : "CHAT");
    SetDlgItemText(hdlg, IDC_SET_TAB_IF,   (tab == 3) ? "[ Interfaces ]" : "Interfaces");
    SetDlgItemText(hdlg, IDC_SET_TAB_RIG,  (tab == 4) ? "[ Rig ]" : "Rig");
    SetDlgItemText(hdlg, IDC_SET_TAB_PRN,  (tab == 5) ? "[ Printer ]" : "Printer");

    static const char* BOX_TITLES[6] = {
        "Node Settings", "Packet BBS Settings", "Multi-User CHAT Settings",
        "Interface Mappings", "Radio / Rig Control Settings", "Line Printer Logging Settings"
    };
    SetDlgItemText(hdlg, IDC_SET_TAB_BOX, BOX_TITLES[(tab >= 0 && tab < 6) ? tab : 0]);
}

#if defined(_WIN32) || defined(_WIN64)
static HANDLE g_prn_handle = INVALID_HANDLE_VALUE;
#else
static int g_prn_cid = -1;
#endif
static int g_prn_proto = OYK_PRN_NONE;

static void win_printer_close() {
#if defined(_WIN32) || defined(_WIN64)
    if (g_prn_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(g_prn_handle);
        g_prn_handle = INVALID_HANDLE_VALUE;
    }
#else
    if (g_prn_cid >= 0) {
        CloseComm(g_prn_cid);
        g_prn_cid = -1;
    }
#endif
    g_prn_proto = OYK_PRN_NONE;
}

static int win_printer_write_bytes(const unsigned char* data, size_t len) {
    if (!data || len == 0) return 1;
#if defined(_WIN32) || defined(_WIN64)
    if (g_prn_handle == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    if (!WriteFile(g_prn_handle, data, static_cast<DWORD>(len), &written, NULL)) return 0;
    return (written == static_cast<DWORD>(len)) ? 1 : 0;
#else
    if (g_prn_cid < 0) return 0;
    int wrote = WriteComm(g_prn_cid, data, static_cast<int>(len));
    return (wrote > 0) ? 1 : 0;
#endif
}

static void win_printer_init() {
    win_printer_close();
    const OykConfig& cfg = g_node.config();
    if (!cfg.printer_enabled || !cfg.printer_device[0]) return;
    int proto = oyk_printer_parse_protocol(cfg.printer_protocol);
    if (proto == OYK_PRN_NONE) return;

    char dev[48];
    size_t dn = strlen(cfg.printer_device);
    if (dn >= sizeof(dev)) dn = sizeof(dev) - 1;
    memcpy(dev, cfg.printer_device, dn);
    dev[dn] = '\0';
    while (dn > 0 && dev[dn - 1] == ':') dev[--dn] = '\0';
    if (!dev[0]) return;

#if defined(_WIN32) || defined(_WIN64)
    char win_path[56];
    if ((dev[0] == 'C' || dev[0] == 'c') &&
        (dev[1] == 'O' || dev[1] == 'o') &&
        (dev[2] == 'M' || dev[2] == 'm') &&
        dev[3] >= '0' && dev[3] <= '9' && dev[4] >= '0' && dev[4] <= '9') {
        sprintf(win_path, "\\\\.\\%s", dev);
    } else {
        strcpy(win_path, dev);
    }
    HANDLE h = CreateFileA(win_path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;

    if ((dev[0] == 'C' || dev[0] == 'c') &&
        (dev[1] == 'O' || dev[1] == 'o') &&
        (dev[2] == 'M' || dev[2] == 'm')) {
        DCB dcb;
        memset(&dcb, 0, sizeof(dcb));
        dcb.DCBlength = sizeof(dcb);
        if (GetCommState(h, &dcb)) {
            dcb.BaudRate = cfg.printer_baud ? static_cast<DWORD>(cfg.printer_baud) : 9600UL;
            dcb.ByteSize = 8;
            dcb.Parity = NOPARITY;
            dcb.StopBits = ONESTOPBIT;
            SetCommState(h, &dcb);
        }
        COMMTIMEOUTS to;
        memset(&to, 0, sizeof(to));
        to.WriteTotalTimeoutConstant = 250;
        SetCommTimeouts(h, &to);
    }
    g_prn_handle = h;
    g_prn_proto = proto;
#else
    int cid = OpenComm(dev, 256, 256);
    if (cid < 0) return;
    if ((dev[0] == 'C' || dev[0] == 'c') &&
        (dev[1] == 'O' || dev[1] == 'o') &&
        (dev[2] == 'M' || dev[2] == 'm')) {
        DCB dcb;
        if (GetCommState(cid, &dcb) == 0) {
            dcb.BaudRate = static_cast<UINT>(cfg.printer_baud ? cfg.printer_baud : 9600U);
            dcb.ByteSize = 8;
            dcb.Parity = NOPARITY;
            dcb.StopBits = ONESTOPBIT;
            SetCommState(&dcb);
        }
    }
    g_prn_cid = cid;
    g_prn_proto = proto;
#endif

    unsigned char init_buf[32];
    size_t ilen = oyk_printer_build_init(g_prn_proto, cfg.printer_condensed,
                                         init_buf, sizeof(init_buf));
    if (ilen > 0) {
        (void)win_printer_write_bytes(init_buf, ilen);
    }
}

static void win_printer_callback(oyk_u32 level, const char* formatted_line, void* user_ctx) {
    (void)user_ctx;
    if (g_prn_proto == OYK_PRN_NONE || !formatted_line) return;
    unsigned char out[384];
    size_t len = oyk_printer_format_line(g_prn_proto, static_cast<unsigned long>(level),
                                         formatted_line, out, sizeof(out));
    if (len > 0) {
        (void)win_printer_write_bytes(out, len);
    }
}

OYK_DLG_RET SettingsDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    switch (msg) {
        case WM_INITDIALOG: {
            g_set_draft = g_node.config();
            char buf[32];
            oyk_format_callsign(&g_set_draft.callsign, buf, sizeof(buf));
            SetDlgItemText(hdlg, IDC_SET_CALL, buf);
            buf[0] = '\0';
            if (g_set_draft.alias.base[0]) oyk_format_callsign(&g_set_draft.alias, buf, sizeof(buf));
            SetDlgItemText(hdlg, IDC_SET_ALIAS, buf);
            CheckDlgButton(hdlg, IDC_SET_ACCEPT, g_set_draft.accept_incoming ? 1 : 0);
            SetDlgItemText(hdlg, IDC_SET_WELCOME, g_set_draft.welcome_text);
            SetDlgItemText(hdlg, IDC_SET_INFO, g_set_draft.info_text);

            CheckDlgButton(hdlg, IDC_SET_BBS_EN, g_set_draft.bbs_enabled ? 1 : 0);
            sprintf(buf, "%u", static_cast<unsigned int>(g_set_draft.bbs_ssid));
            SetDlgItemText(hdlg, IDC_SET_BBS_SSID, buf);
            buf[0] = '\0';
            if (g_set_draft.bbs_callsign.base[0]) oyk_format_callsign(&g_set_draft.bbs_callsign, buf, sizeof(buf));
            SetDlgItemText(hdlg, IDC_SET_BBS_CALL, buf);
            buf[0] = '\0';
            if (g_set_draft.bbs_alias.base[0]) oyk_format_callsign(&g_set_draft.bbs_alias, buf, sizeof(buf));
            SetDlgItemText(hdlg, IDC_SET_BBS_ALIAS, buf);
            CheckDlgButton(hdlg, IDC_SET_FILES_EN, g_set_draft.bbs_files_enabled ? 1 : 0);
            SetDlgItemText(hdlg, IDC_SET_FILES_PATH, g_set_draft.bbs_files_path);
            SetDlgItemText(hdlg, IDC_SET_MAIL_PATH, g_set_draft.bbs_mail_path);
            SetDlgItemText(hdlg, IDC_SET_BBS_WELC, g_set_draft.bbs_welcome);

            CheckDlgButton(hdlg, IDC_SET_CHAT_EN, g_set_draft.chat_enabled ? 1 : 0);
            sprintf(buf, "%u", static_cast<unsigned int>(g_set_draft.chat_ssid));
            SetDlgItemText(hdlg, IDC_SET_CHAT_SSID, buf);
            buf[0] = '\0';
            if (g_set_draft.chat_callsign.base[0]) oyk_format_callsign(&g_set_draft.chat_callsign, buf, sizeof(buf));
            SetDlgItemText(hdlg, IDC_SET_CHAT_CALL, buf);
            buf[0] = '\0';
            if (g_set_draft.chat_alias.base[0]) oyk_format_callsign(&g_set_draft.chat_alias, buf, sizeof(buf));
            SetDlgItemText(hdlg, IDC_SET_CHAT_ALIAS, buf);
            SetDlgItemText(hdlg, IDC_SET_CHAT_TOPIC, g_set_draft.chat_topic);
            SetDlgItemText(hdlg, IDC_SET_CHAT_WELC, g_set_draft.chat_welcome);

            CheckDlgButton(hdlg, IDC_SET_RIG_EN, g_set_draft.rig_enabled ? 1 : 0);
            HWND rcb = GetDlgItem(hdlg, IDC_SET_RIG_PROTO);
            SendMessage(rcb, CB_ADDSTRING, 0, OYK_LPARAM_STR("rigctl"));
            SendMessage(rcb, CB_ADDSTRING, 0, OYK_LPARAM_STR("radiofoundry"));
            SendMessage(rcb, CB_SETCURSEL,
                        (oyk_rig_parse_protocol(g_set_draft.rig_protocol) == OYK_RIG_PROTO_RADIOFOUNDRY) ? 1 : 0, 0);
            SetDlgItemText(hdlg, IDC_SET_RIG_HOST, g_set_draft.rig_host[0] ? g_set_draft.rig_host : "127.0.0.1");
            sprintf(buf, "%u", static_cast<unsigned int>(g_set_draft.rig_port ? g_set_draft.rig_port : 4532));
            SetDlgItemText(hdlg, IDC_SET_RIG_PORT, buf);
            sprintf(buf, "%u", static_cast<unsigned int>(g_set_draft.rig_poll_sec));
            SetDlgItemText(hdlg, IDC_SET_RIG_POLL, buf);
            CheckDlgButton(hdlg, IDC_SET_RIG_NOTIFY, g_set_draft.rig_notify ? 1 : 0);

            CheckDlgButton(hdlg, IDC_SET_PRN_EN, g_set_draft.printer_enabled ? 1 : 0);
            SetDlgItemText(hdlg, IDC_SET_PRN_DEV, g_set_draft.printer_device);
            HWND pcb = GetDlgItem(hdlg, IDC_SET_PRN_PROTO);
            SendMessage(pcb, CB_ADDSTRING, 0, OYK_LPARAM_STR("generic"));
            SendMessage(pcb, CB_ADDSTRING, 0, OYK_LPARAM_STR("mx80"));
            SendMessage(pcb, CB_ADDSTRING, 0, OYK_LPARAM_STR("escp2"));
            int psel = 0;
            int pp = oyk_printer_parse_protocol(g_set_draft.printer_protocol);
            if (pp == OYK_PRN_MX80) psel = 1;
            else if (pp == OYK_PRN_ESCP2) psel = 2;
            SendMessage(pcb, CB_SETCURSEL, psel, 0);
            sprintf(buf, "%lu", static_cast<unsigned long>(g_set_draft.printer_baud ? g_set_draft.printer_baud : 9600UL));
            SetDlgItemText(hdlg, IDC_SET_PRN_BAUD, buf);
            sprintf(buf, "0x%lX", static_cast<unsigned long>(g_set_draft.printer_level));
            SetDlgItemText(hdlg, IDC_SET_PRN_LVL, buf);
            CheckDlgButton(hdlg, IDC_SET_PRN_COND, g_set_draft.printer_condensed ? 1 : 0);

            populate_settings_ifaces(hdlg);
            switch_settings_tab(hdlg, 0);
            return TRUE;
        }
        case WM_COMMAND: {
            WORD id = OYK_CMD_ID(wp, lp);
            if (id == IDC_SET_TAB_NODE) { switch_settings_tab(hdlg, 0); return TRUE; }
            if (id == IDC_SET_TAB_BBS)  { switch_settings_tab(hdlg, 1); return TRUE; }
            if (id == IDC_SET_TAB_CHAT) { switch_settings_tab(hdlg, 2); return TRUE; }
            if (id == IDC_SET_TAB_IF)   { switch_settings_tab(hdlg, 3); return TRUE; }
            if (id == IDC_SET_TAB_RIG)  { switch_settings_tab(hdlg, 4); return TRUE; }
            if (id == IDC_SET_TAB_PRN)  { switch_settings_tab(hdlg, 5); return TRUE; }
            if (id == IDC_SET_IF_ADD) {
                if (g_set_draft.iface_count >= OYK_MAX_INTERFACES) {
                    MessageBox(hdlg, "Maximum interface count reached.", "Settings", MB_OK | MB_ICONEXCLAMATION);
                    return TRUE;
                }
                if (DialogBox(g_hinst, MAKEINTRESOURCE(IDD_ADD_IFACE), hdlg, (DLGPROC)AddIfaceDlgProc) == IDOK && g_add_if_ok) {
                    for (int i = 0; i < g_set_draft.iface_count; ++i) {
                        if (win_strieq(g_set_draft.ifaces[i].id, g_add_if_out.id)) {
                            MessageBox(hdlg, "Duplicate interface ID.", "Settings", MB_OK | MB_ICONEXCLAMATION);
                            return TRUE;
                        }
                    }
                    g_set_draft.ifaces[g_set_draft.iface_count++] = g_add_if_out;
                    populate_settings_ifaces(hdlg);
                }
                return TRUE;
            } else if (id == IDC_SET_IF_EDIT || (id == IDC_SET_IF_LIST && OYK_CMD_NOTIFY(wp, lp) == LBN_DBLCLK)) {
                int sel = static_cast<int>(SendDlgItemMessage(hdlg, IDC_SET_IF_LIST, LB_GETCURSEL, 0, 0));
                if (sel < 0 || sel >= g_set_draft.iface_count) {
                    if (id == IDC_SET_IF_EDIT) {
                        MessageBox(hdlg, "Select an interface to edit.", "Settings", MB_OK | MB_ICONINFORMATION);
                    }
                    return TRUE;
                }
                g_edit_if_idx = sel;
                if (DialogBox(g_hinst, MAKEINTRESOURCE(IDD_EDIT_IFACE), hdlg, (DLGPROC)EditIfaceDlgProc) == IDOK && g_add_if_ok) {
                    for (int i = 0; i < g_set_draft.iface_count; ++i) {
                        if (i == sel) continue;
                        if (win_strieq(g_set_draft.ifaces[i].id, g_add_if_out.id)) {
                            MessageBox(hdlg, "Duplicate interface ID.", "Settings", MB_OK | MB_ICONEXCLAMATION);
                            g_edit_if_idx = -1;
                            return TRUE;
                        }
                    }
                    g_set_draft.ifaces[sel] = g_add_if_out;
                    populate_settings_ifaces(hdlg);
                    SendDlgItemMessage(hdlg, IDC_SET_IF_LIST, LB_SETCURSEL, static_cast<WPARAM>(sel), 0);
                }
                g_edit_if_idx = -1;
                return TRUE;
            } else if (id == IDC_SET_IF_REM) {
                int sel = static_cast<int>(SendDlgItemMessage(hdlg, IDC_SET_IF_LIST, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < g_set_draft.iface_count) {
                    for (int i = sel + 1; i < g_set_draft.iface_count; ++i) {
                        g_set_draft.ifaces[i - 1] = g_set_draft.ifaces[i];
                    }
                    g_set_draft.iface_count--;
                    populate_settings_ifaces(hdlg);
                }
                return TRUE;
            } else if (id == IDOK) {
                char buf[32];
                GetDlgItemText(hdlg, IDC_SET_CALL, buf, sizeof(buf));
                if (!oyk_parse_callsign_with_default_ssid(buf, OYK_DEFAULT_NODE_SSID, &g_set_draft.callsign)) {
                    MessageBox(hdlg, "Invalid node callsign.", "Settings", MB_OK | MB_ICONEXCLAMATION);
                    return TRUE;
                }
                GetDlgItemText(hdlg, IDC_SET_ALIAS, buf, sizeof(buf));
                if (!buf[0]) memset(&g_set_draft.alias, 0, sizeof(g_set_draft.alias));
                else if (!oyk_parse_callsign(buf, &g_set_draft.alias)) {
                    MessageBox(hdlg, "Invalid node alias.", "Settings", MB_OK | MB_ICONEXCLAMATION);
                    return TRUE;
                }
                g_set_draft.accept_incoming = (IsDlgButtonChecked(hdlg, IDC_SET_ACCEPT) != 0) ? 1 : 0;
                GetDlgItemText(hdlg, IDC_SET_WELCOME, g_set_draft.welcome_text, sizeof(g_set_draft.welcome_text));
                GetDlgItemText(hdlg, IDC_SET_INFO, g_set_draft.info_text, sizeof(g_set_draft.info_text));

                g_set_draft.bbs_enabled = (IsDlgButtonChecked(hdlg, IDC_SET_BBS_EN) != 0) ? 1 : 0;
                GetDlgItemText(hdlg, IDC_SET_BBS_SSID, buf, sizeof(buf));
                int bss = atoi(buf);
                if (bss < 0 || bss > 15) {
                    MessageBox(hdlg, "BBS SSID must be 0..15.", "Settings", MB_OK | MB_ICONEXCLAMATION);
                    return TRUE;
                }
                g_set_draft.bbs_ssid = static_cast<oyk_u8>(bss);
                GetDlgItemText(hdlg, IDC_SET_BBS_CALL, buf, sizeof(buf));
                if (!buf[0]) memset(&g_set_draft.bbs_callsign, 0, sizeof(g_set_draft.bbs_callsign));
                else if (!oyk_parse_callsign_with_default_ssid(buf, g_set_draft.bbs_ssid, &g_set_draft.bbs_callsign)) {
                    MessageBox(hdlg, "Invalid BBS callsign override.", "Settings", MB_OK | MB_ICONEXCLAMATION);
                    return TRUE;
                }
                GetDlgItemText(hdlg, IDC_SET_BBS_ALIAS, buf, sizeof(buf));
                if (!buf[0]) memset(&g_set_draft.bbs_alias, 0, sizeof(g_set_draft.bbs_alias));
                else if (!oyk_parse_callsign(buf, &g_set_draft.bbs_alias)) {
                    MessageBox(hdlg, "Invalid BBS alias.", "Settings", MB_OK | MB_ICONEXCLAMATION);
                    return TRUE;
                }
                g_set_draft.bbs_files_enabled = (IsDlgButtonChecked(hdlg, IDC_SET_FILES_EN) != 0) ? 1 : 0;
                char fpath[96], mpath[96];
                GetDlgItemText(hdlg, IDC_SET_FILES_PATH, fpath, sizeof(fpath));
                if (fpath[0]) strncpy(g_set_draft.bbs_files_path, fpath, sizeof(g_set_draft.bbs_files_path) - 1);
                GetDlgItemText(hdlg, IDC_SET_MAIL_PATH, mpath, sizeof(mpath));
                if (mpath[0]) strncpy(g_set_draft.bbs_mail_path, mpath, sizeof(g_set_draft.bbs_mail_path) - 1);
                GetDlgItemText(hdlg, IDC_SET_BBS_WELC, g_set_draft.bbs_welcome, sizeof(g_set_draft.bbs_welcome));

                g_set_draft.chat_enabled = (IsDlgButtonChecked(hdlg, IDC_SET_CHAT_EN) != 0) ? 1 : 0;
                GetDlgItemText(hdlg, IDC_SET_CHAT_SSID, buf, sizeof(buf));
                int css = atoi(buf);
                if (css < 0 || css > 15) {
                    MessageBox(hdlg, "CHAT SSID must be 0..15.", "Settings", MB_OK | MB_ICONEXCLAMATION);
                    return TRUE;
                }
                g_set_draft.chat_ssid = static_cast<oyk_u8>(css);
                GetDlgItemText(hdlg, IDC_SET_CHAT_CALL, buf, sizeof(buf));
                if (!buf[0]) memset(&g_set_draft.chat_callsign, 0, sizeof(g_set_draft.chat_callsign));
                else if (!oyk_parse_callsign_with_default_ssid(buf, g_set_draft.chat_ssid, &g_set_draft.chat_callsign)) {
                    MessageBox(hdlg, "Invalid CHAT callsign override.", "Settings", MB_OK | MB_ICONEXCLAMATION);
                    return TRUE;
                }
                GetDlgItemText(hdlg, IDC_SET_CHAT_ALIAS, buf, sizeof(buf));
                if (!buf[0]) memset(&g_set_draft.chat_alias, 0, sizeof(g_set_draft.chat_alias));
                else if (!oyk_parse_callsign(buf, &g_set_draft.chat_alias)) {
                    MessageBox(hdlg, "Invalid CHAT alias.", "Settings", MB_OK | MB_ICONEXCLAMATION);
                    return TRUE;
                }
                char ctop[16];
                GetDlgItemText(hdlg, IDC_SET_CHAT_TOPIC, ctop, sizeof(ctop));
                if (ctop[0]) strncpy(g_set_draft.chat_topic, ctop, sizeof(g_set_draft.chat_topic) - 1);
                GetDlgItemText(hdlg, IDC_SET_CHAT_WELC, g_set_draft.chat_welcome, sizeof(g_set_draft.chat_welcome));

                g_set_draft.rig_enabled = (IsDlgButtonChecked(hdlg, IDC_SET_RIG_EN) != 0) ? 1 : 0;
                int rpsel = static_cast<int>(SendDlgItemMessage(hdlg, IDC_SET_RIG_PROTO, CB_GETCURSEL, 0, 0));
                strcpy(g_set_draft.rig_protocol, (rpsel == 1) ? "radiofoundry" : "rigctl");
                char rhost[32], rport_s[16], rpoll_s[16];
                GetDlgItemText(hdlg, IDC_SET_RIG_HOST, rhost, sizeof(rhost));
                if (rhost[0]) strncpy(g_set_draft.rig_host, rhost, sizeof(g_set_draft.rig_host) - 1);
                GetDlgItemText(hdlg, IDC_SET_RIG_PORT, rport_s, sizeof(rport_s));
                long rpv = atol(rport_s);
                if (rpv >= 1L && rpv <= 65535L) g_set_draft.rig_port = static_cast<oyk_u16>(rpv);
                else g_set_draft.rig_port = (rpsel == 1) ? static_cast<oyk_u16>(4191) : static_cast<oyk_u16>(4532);
                GetDlgItemText(hdlg, IDC_SET_RIG_POLL, rpoll_s, sizeof(rpoll_s));
                long rpl = atol(rpoll_s);
                if (rpl >= 0L && rpl <= 3600L) g_set_draft.rig_poll_sec = static_cast<oyk_u16>(rpl);
                g_set_draft.rig_notify = (IsDlgButtonChecked(hdlg, IDC_SET_RIG_NOTIFY) != 0) ? 1 : 0;

                g_set_draft.printer_enabled = (IsDlgButtonChecked(hdlg, IDC_SET_PRN_EN) != 0) ? 1 : 0;
                GetDlgItemText(hdlg, IDC_SET_PRN_DEV, g_set_draft.printer_device, sizeof(g_set_draft.printer_device));
                int ppsel = static_cast<int>(SendDlgItemMessage(hdlg, IDC_SET_PRN_PROTO, CB_GETCURSEL, 0, 0));
                strcpy(g_set_draft.printer_protocol, (ppsel == 1) ? "mx80" : ((ppsel == 2) ? "escp2" : "generic"));
                char pbaud_s[16], plvl_s[24];
                GetDlgItemText(hdlg, IDC_SET_PRN_BAUD, pbaud_s, sizeof(pbaud_s));
                long pbv = atol(pbaud_s);
                if (pbv >= 300L) g_set_draft.printer_baud = static_cast<oyk_u32>(pbv);
                GetDlgItemText(hdlg, IDC_SET_PRN_LVL, plvl_s, sizeof(plvl_s));
                oyk_u32 pm = 0;
                if (oyk_parse_log_mask(plvl_s, &pm)) g_set_draft.printer_level = pm;
                g_set_draft.printer_condensed = (IsDlgButtonChecked(hdlg, IDC_SET_PRN_COND) != 0) ? 1 : 0;

                static OykConfigApplyResult res;
                if (!g_node.apply_config(&g_set_draft, 1, &res)) {
                    MessageBox(hdlg, res.error[0] ? res.error : "Invalid configuration.",
                               "Settings Error", MB_OK | MB_ICONSTOP);
                    return TRUE;
                }
                win_printer_init();
                if (res.identity_restarted || res.interfaces_changed) {
                    /* Sign off any Telnet clients still sitting at the login prompt */
                    if (res.identity_restarted) {
                        char nc[16], so[128];
                        oyk_format_callsign(&g_node.config().callsign, nc, sizeof(nc));
                        sprintf(so, "\r\n*** Node is restarting for configuration changes. "
                                    "Node will be accessible on %s after restart. 73!\r\n", nc);
                        for (int i = 0; i < OYK_MAX_TELNET_CLIENTS; ++i) {
                            if (g_telnet_clients[i].in_use && g_telnet_clients[i].stage != TSTAGE_CLOSING) {
                                telnet_send_str(g_telnet_clients[i].sock, so);
                                telnet_begin_close(&g_telnet_clients[i]);
                            }
                        }
                    }
                    if (res.interfaces_changed) {
                        shutdown_win_interfaces();
                        init_win_interfaces();
                    }
                }
                if (!res.saved_to_disk) {
                    MessageBox(hdlg,
                               res.warning[0]
                                   ? res.warning
                                   : "The configuration file could not be written. Changes remain in "
                                     "memory only while running and will be lost on exit.",
                               "Read-Only Configuration",
                               MB_OK | MB_ICONEXCLAMATION);
                    set_status("Settings applied IN MEMORY ONLY (config not writable).");
                } else {
                    char st[128];
                    sprintf(st, "Settings saved to %.72s and applied.", res.save_path);
                    set_status(st);
                }
                EndDialog(hdlg, IDOK);
                return TRUE;
            } else if (id == IDCANCEL) {
                EndDialog(hdlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
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
            layout_controls(hwnd, static_cast<int>(LOWORD(lp)), static_cast<int>(HIWORD(lp)));
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            paint_splitter_handle(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SETCURSOR: {
            if (LOWORD(lp) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                if (g_split_drag || hit_test_splitter(pt.x, pt.y)) {
                    SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            int mx = static_cast<int>(static_cast<short>(LOWORD(lp)));
            int my = static_cast<int>(static_cast<short>(HIWORD(lp)));
            if (hit_test_splitter(mx, my)) {
                g_split_drag = 1;
                SetCapture(hwnd);
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            if (g_split_drag && g_client_w > 40) {
                int mx = static_cast<int>(static_cast<short>(LOWORD(lp)));
                const int pad = 6;
                int avail_w = g_client_w - pad * 2 - OYK_SPLITTER_W;
                if (avail_w > 40) {
                    long rel = static_cast<long>(mx - pad - OYK_SPLITTER_W / 2);
                    int pct = static_cast<int>((rel * 100L) / static_cast<long>(avail_w));
                    if (pct < 15) pct = 15;
                    if (pct > 85) pct = 85;
                    if (pct != g_split_pct) {
                        g_split_pct = pct;
                        layout_controls(hwnd, g_client_w, g_client_h);
                    }
                }
                return 0;
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (g_split_drag) {
                g_split_drag = 0;
                ReleaseCapture();
                return 0;
            }
            break;
        }
#ifdef WM_CAPTURECHANGED
        case WM_CAPTURECHANGED:
            g_split_drag = 0;
            return 0;
#endif
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
            } else if (id == IDM_TERMINAL) {
                if (g_lterm_hwnd && IsWindow(g_lterm_hwnd)) {
                    ShowWindow(g_lterm_hwnd, SW_RESTORE);
                    BringWindowToTop(g_lterm_hwnd);
                    SetActiveWindow(g_lterm_hwnd);
                } else {
                    g_lterm_hwnd = CreateDialog(g_hinst, MAKEINTRESOURCE(IDD_TERMINAL),
                                                NULL, (DLGPROC)TerminalDlgProc);
                    if (g_lterm_hwnd) {
                        ShowWindow(g_lterm_hwnd, SW_SHOW);
                    }
                }
                refresh_gui();
            } else if (id == IDM_APPROVE_FILES) {
                DialogBox(g_hinst, MAKEINTRESOURCE(IDD_APPROVE), hwnd, (DLGPROC)ApproveFilesDlgProc);
                refresh_gui();
            } else if (id == IDM_RIG_CONTROL) {
                DialogBox(g_hinst, MAKEINTRESOURCE(IDD_RIG_CONTROL), hwnd, (DLGPROC)RigControlDlgProc);
                refresh_gui();
            } else if (id == IDM_SETTINGS) {
                DialogBox(g_hinst, MAKEINTRESOURCE(IDD_SETTINGS), hwnd, (DLGPROC)SettingsDlgProc);
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
            char msg_buf[128];
            int active = g_node.total_active_connections();
            if (active > 0) {
                sprintf(msg_buf,
                        "Exit OYK Node? (%d active connection%s)\nActive sessions will be gracefully disconnected.",
                        active, (active == 1) ? "" : "s");
            } else {
                strcpy(msg_buf, "Are you sure you want to exit OYK Node?");
            }
            if (MessageBox(hwnd, msg_buf, "Exit OYK Node", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_DESTROY:
            if (g_lterm_hwnd && IsWindow(g_lterm_hwnd)) {
                DestroyWindow(g_lterm_hwnd);
                g_lterm_hwnd = NULL;
            }
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

static int has_pending_shutdown_drain() {
    for (int i = 0; i < OYK_MAX_TELNET_CLIENTS; ++i) {
        if (g_telnet_clients[i].in_use && g_telnet_clients[i].stage == TSTAGE_CLOSING) {
            return 1;
        }
    }
    for (int i = 0; i < g_node.max_sessions(); ++i) {
        const OykSession& s = g_node.session_slot(i);
        if (s.state == OYK_SESS_DISCONNECTING &&
            s.iface_idx >= 0 && s.iface_idx < OYK_MAX_INTERFACES &&
            g_ifaces[s.iface_idx].kind == WIN_IF_AXUDP) {
            return 1;
        }
    }
    return 0;
}

static void drain_win_shutdown() {
    g_node.begin_shutdown();

    /* Politely sign off any Telnet clients still sitting at the login prompt */
    char node_s[16], signoff[80];
    oyk_format_callsign(&g_node.config().callsign, node_s, sizeof(node_s));
    sprintf(signoff, "\r\n*** Node shutting down. 73 de %s\r\n", node_s);
    for (int i = 0; i < OYK_MAX_TELNET_CLIENTS; ++i) {
        WinTelnetClient* c = &g_telnet_clients[i];
        if (c->in_use && c->stage != TSTAGE_CLOSING) {
            telnet_send_str(c->sock, signoff);
            telnet_begin_close(c);
        }
    }

    /* Stop accepting new Telnet connections while draining active ones */
    for (int i = 0; i < OYK_MAX_INTERFACES; ++i) {
        if (g_ifaces[i].kind == WIN_IF_TELNET && g_ifaces[i].sock != INVALID_SOCKET) {
            closesocket(g_ifaces[i].sock);
            g_ifaces[i].sock = INVALID_SOCKET;
        }
    }

    if (has_pending_shutdown_drain()) {
        DWORD start_ms = GetTickCount();
        while (has_pending_shutdown_drain() &&
               static_cast<DWORD>(GetTickCount() - start_ms) < 1000UL) {
            fd_set rfds;
            FD_ZERO(&rfds);
            int nfds = 0;
            for (int i = 0; i < OYK_MAX_TELNET_CLIENTS; ++i) {
                if (g_telnet_clients[i].in_use && g_telnet_clients[i].sock != INVALID_SOCKET) {
                    FD_SET(g_telnet_clients[i].sock, &rfds);
                    ++nfds;
                }
            }
            for (int i = 0; i < OYK_MAX_INTERFACES; ++i) {
                if (g_ifaces[i].kind == WIN_IF_AXUDP && g_ifaces[i].sock != INVALID_SOCKET) {
                    FD_SET(g_ifaces[i].sock, &rfds);
                    ++nfds;
                }
            }
            if (nfds == 0) break;
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 25000; /* 25 ms */
            select(0, &rfds, NULL, NULL, &tv);
            pump_winsock();
        }
    }

    g_node.finish_shutdown();
    win_printer_close();
    if (g_win_rf_sock != INVALID_SOCKET) {
        closesocket(g_win_rf_sock);
        g_win_rf_sock = INVALID_SOCKET;
    }
    shutdown_win_interfaces();
}

struct WinCliOpts {
    char config_path[128];
    char override_call[16];
    char override_alias[16];
    oyk_u16 override_port;
    int headless;
    int show_help;
    char error[96];
};

static int split_win_cmdline(const char* cmdline, char* storage, size_t storage_sz,
                             char* argv[], int max_argv) {
    int argc = 0;
    size_t spos = 0;
    const char* p = cmdline ? cmdline : "";
    while (*p && argc < max_argv) {
        while (*p && isspace(static_cast<unsigned char>(*p))) ++p;
        if (!*p) break;
        if (spos + 1 >= storage_sz) break;
        argv[argc++] = storage + spos;
        int in_quote = 0;
        while (*p) {
            if (*p == '"') {
                in_quote = !in_quote;
                ++p;
                continue;
            }
            if (!in_quote && isspace(static_cast<unsigned char>(*p))) break;
            if (spos + 1 < storage_sz) storage[spos++] = *p;
            ++p;
        }
        storage[spos++] = '\0';
    }
    return argc;
}

static int parse_win_cli(const char* cmdline, WinCliOpts* opts) {
    memset(opts, 0, sizeof(*opts));
    static char arg_store[512];
    char* argv[24];
    int argc = split_win_cmdline(cmdline, arg_store, sizeof(arg_store), argv, 24);

    for (int i = 0; i < argc; ++i) {
        const char* a = argv[i];
        const char* eq = strchr(a, '=');
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0 || strcmp(a, "/?") == 0) {
            opts->show_help = 1;
            return 1;
        } else if (strcmp(a, "-c") == 0 || strcmp(a, "--config") == 0) {
            if (i + 1 >= argc) { strcpy(opts->error, "Missing path for --config"); return 0; }
            strncpy(opts->config_path, argv[++i], sizeof(opts->config_path) - 1);
        } else if (strncmp(a, "--config=", 9) == 0 && eq) {
            strncpy(opts->config_path, eq + 1, sizeof(opts->config_path) - 1);
        } else if (strcmp(a, "--callsign") == 0) {
            if (i + 1 >= argc) { strcpy(opts->error, "Missing value for --callsign"); return 0; }
            strncpy(opts->override_call, argv[++i], sizeof(opts->override_call) - 1);
        } else if (strncmp(a, "--callsign=", 11) == 0 && eq) {
            strncpy(opts->override_call, eq + 1, sizeof(opts->override_call) - 1);
        } else if (strcmp(a, "--alias") == 0) {
            if (i + 1 >= argc) { strcpy(opts->error, "Missing value for --alias"); return 0; }
            strncpy(opts->override_alias, argv[++i], sizeof(opts->override_alias) - 1);
        } else if (strncmp(a, "--alias=", 8) == 0 && eq) {
            strncpy(opts->override_alias, eq + 1, sizeof(opts->override_alias) - 1);
        } else if (strcmp(a, "--port") == 0) {
            if (i + 1 >= argc) { strcpy(opts->error, "Missing value for --port"); return 0; }
            long pv = atol(argv[++i]);
            if (pv <= 0L || pv > 65535L) { strcpy(opts->error, "Invalid --port value"); return 0; }
            opts->override_port = static_cast<oyk_u16>(pv);
        } else if (strncmp(a, "--port=", 7) == 0 && eq) {
            long pv = atol(eq + 1);
            if (pv <= 0L || pv > 65535L) { strcpy(opts->error, "Invalid --port value"); return 0; }
            opts->override_port = static_cast<oyk_u16>(pv);
        } else if (strcmp(a, "--headless") == 0) {
            opts->headless = 1;
        } else if (a[0] != '-' && !opts->config_path[0]) {
            /* Backward-compatible bare config path argument */
            strncpy(opts->config_path, a, sizeof(opts->config_path) - 1);
        } else {
            sprintf(opts->error, "Unknown option: %.60s", a);
            return 0;
        }
    }
    return 1;
}

static void show_win_usage() {
    MessageBox(NULL,
               "OYK Amateur Radio Packet Node (Windows)\n\n"
               "Options:\n"
               "  -c, --config <path>      Load configuration file\n"
               "      --callsign <CALL>    Override node callsign (e.g. N0CALL or N0CALL-7; default SSID 7)\n"
               "      --alias <ALIAS>      Override node alias (e.g. OYKNOD)\n"
               "      --port <UDP_PORT>    Override first AXUDP bind port\n"
               "      --headless           Run without showing GUI window\n"
               "  -h, --help               Show this help",
               "OYK Packet Node - Usage",
               MB_OK | MB_ICONINFORMATION);
}

int PASCAL WinMain(HINSTANCE hinst, HINSTANCE hprev, LPSTR cmdline, int nshow) {
    g_hinst = hinst;

    static WinCliOpts cli;
    if (!parse_win_cli(cmdline, &cli)) {
        MessageBox(NULL, cli.error, "OYK Command-Line Error", MB_OK | MB_ICONSTOP);
        show_win_usage();
        return 1;
    }
    if (cli.show_help) {
        show_win_usage();
        return 0;
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(1, 1), &wsa) != 0) {
        MessageBox(NULL, "Failed to initialize Winsock 1.1.", "OYK Packet Node", MB_OK | MB_ICONSTOP);
        return 1;
    }

    static OykConfig cfg;
    char cfg_path[128], err[96];
    cfg_path[0] = '\0';
    if (cli.config_path[0]) {
        strncpy(cfg_path, cli.config_path, sizeof(cfg_path) - 1);
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

    if (cli.override_call[0]) {
        if (!oyk_parse_callsign_with_default_ssid(cli.override_call, cfg.callsign.ssid, &cfg.callsign)) {
            MessageBox(NULL, "Invalid --callsign value.", "OYK Command-Line Error", MB_OK | MB_ICONSTOP);
            WSACleanup();
            return 1;
        }
    }
    if (cli.override_alias[0]) {
        if (!oyk_parse_callsign(cli.override_alias, &cfg.alias)) {
            MessageBox(NULL, "Invalid --alias value.", "OYK Command-Line Error", MB_OK | MB_ICONSTOP);
            WSACleanup();
            return 1;
        }
    }
    if (cli.override_port > 0 && cfg.iface_count > 0) {
        int updated = 0;
        for (int i = 0; i < cfg.iface_count; ++i) {
            if (win_strieq(cfg.ifaces[i].type, "axudp")) {
                cfg.ifaces[i].port = cli.override_port;
                updated = 1;
                break;
            }
        }
        if (!updated) cfg.ifaces[0].port = cli.override_port;
    }

    if (!g_node.init(&cfg, win_tx_callback, NULL, err, sizeof(err))) {
        MessageBox(NULL, err, "OYK Init Error", MB_OK | MB_ICONSTOP);
        WSACleanup();
        return 1;
    }

    g_node.set_printer_callback(win_printer_callback, NULL);
    init_win_interfaces();
    win_printer_init();

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
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 760, 520,
        NULL, NULL, hinst, NULL);

    if (!cli.headless) {
        ShowWindow(g_hwnd_main, nshow);
        UpdateWindow(g_hwnd_main);
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        if (g_lterm_hwnd && IsDialogMessage(g_lterm_hwnd, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    drain_win_shutdown();
    WSACleanup();
    return static_cast<int>(msg.wParam);
}
