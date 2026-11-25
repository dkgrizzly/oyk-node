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
#include "resource.h"

#if defined(_WIN32) || defined(_WIN64)
  #define OYK_DLG_RET         INT_PTR CALLBACK
  #define OYK_WND_RET         LRESULT CALLBACK
  #define OYK_CMD_ID(wp, lp)  LOWORD(wp)
  #define OYK_HMENU(id)       ((HMENU)(INT_PTR)(id))
  #define OYK_HBRUSH(idx)     ((HBRUSH)(INT_PTR)(idx))
  #define OYK_WPARAM_H(h)     ((WPARAM)(h))
  #define OYK_LPARAM_STR(s)   ((LPARAM)(LPCSTR)(s))
  #define OYK_EDIT_SETSEL(h, a, b) do { \
      SendMessage((h), EM_SETSEL, (WPARAM)(a), (LPARAM)(b)); \
      SendMessage((h), EM_SCROLLCARET, 0, 0); \
  } while (0)
#else
  /* 16-bit Windows 3.1 */
  #define OYK_DLG_RET         BOOL FAR PASCAL __export
  #define OYK_WND_RET         LRESULT FAR PASCAL __export
  #define OYK_CMD_ID(wp, lp)  ((WORD)(wp))
  #define OYK_HMENU(id)       ((HMENU)(id))
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
}

OYK_DLG_RET ApproveFilesDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    static OykPendingIncoming* s_items = NULL;
    static int s_count = 0;

    switch (msg) {
        case WM_INITDIALOG: {
            if (!s_items) s_items = new OykPendingIncoming[32];
            SendDlgItemMessage(hdlg, IDC_APP_LIST, WM_SETFONT, OYK_WPARAM_H(g_mono_font), FALSE);
            populate_approve_list(hdlg, s_items, &s_count);
            char st[128];
            sprintf(st, "Files Area: %.72s (%d pending)", g_node.config().bbs_files_path, s_count);
            SetDlgItemText(hdlg, IDC_APP_STATUS, st);
            return TRUE;
        }
        case WM_DESTROY:
            if (s_items) {
                delete[] s_items;
                s_items = NULL;
            }
            s_count = 0;
            return TRUE;
        case WM_COMMAND: {
            WORD id = OYK_CMD_ID(wp, lp);
            if (id == IDC_APP_REFRESH) {
                populate_approve_list(hdlg, s_items, &s_count);
                char st[96];
                sprintf(st, "Refreshed (%d pending file(s)).", s_count);
                SetDlgItemText(hdlg, IDC_APP_STATUS, st);
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

OYK_DLG_RET AddIfaceDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    switch (msg) {
        case WM_INITDIALOG: {
            SetDlgItemText(hdlg, IDC_AIF_ID, "axudp1");
            HWND cb = GetDlgItem(hdlg, IDC_AIF_TYPE);
            SendMessage(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("axudp"));
            SendMessage(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("axip"));
            SendMessage(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("kiss-serial"));
            SendMessage(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("kiss-tcp"));
            SendMessage(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("telnet"));
            SendMessage(cb, CB_SETCURSEL, 0, 0);
            SetDlgItemText(hdlg, IDC_AIF_HOST, "0.0.0.0");
            SetDlgItemText(hdlg, IDC_AIF_PORT, "10093");
            g_add_if_ok = 0;
            return TRUE;
        }
        case WM_COMMAND: {
            WORD id = OYK_CMD_ID(wp, lp);
            if (id == IDOK) {
                memset(&g_add_if_out, 0, sizeof(g_add_if_out));
                GetDlgItemText(hdlg, IDC_AIF_ID, g_add_if_out.id, sizeof(g_add_if_out.id));
                if (!g_add_if_out.id[0]) {
                    MessageBox(hdlg, "Interface ID cannot be empty.", "Add Interface", MB_OK | MB_ICONEXCLAMATION);
                    return TRUE;
                }
                int tsel = static_cast<int>(SendDlgItemMessage(hdlg, IDC_AIF_TYPE, CB_GETCURSEL, 0, 0));
                static const char* TYPES[5] = { "axudp", "axip", "kiss-serial", "kiss-tcp", "telnet" };
                if (tsel < 0 || tsel > 4) tsel = 0;
                strcpy(g_add_if_out.type, TYPES[tsel]);
                GetDlgItemText(hdlg, IDC_AIF_DESC, g_add_if_out.description, sizeof(g_add_if_out.description));
                char host_b[32], port_b[16], peer_b[64];
                GetDlgItemText(hdlg, IDC_AIF_HOST, host_b, sizeof(host_b));
                GetDlgItemText(hdlg, IDC_AIF_PORT, port_b, sizeof(port_b));
                GetDlgItemText(hdlg, IDC_AIF_PEER, peer_b, sizeof(peer_b));

                if (win_strieq(g_add_if_out.type, "kiss-serial")) {
                    strncpy(g_add_if_out.device, host_b[0] ? host_b : "COM1", sizeof(g_add_if_out.device) - 1);
                    long bv = atol(port_b);
                    g_add_if_out.baud = (bv > 0L) ? static_cast<oyk_u32>(bv) : 9600UL;
                } else {
                    strncpy(g_add_if_out.host, host_b[0] ? host_b : "0.0.0.0", sizeof(g_add_if_out.host) - 1);
                    long pv = atol(port_b);
                    if (pv > 0L && pv <= 65535L) g_add_if_out.port = static_cast<oyk_u16>(pv);
                    else if (win_strieq(g_add_if_out.type, "telnet")) g_add_if_out.port = 8010;
                    else if (win_strieq(g_add_if_out.type, "kiss-tcp")) g_add_if_out.port = 8001;
                    else if (win_strieq(g_add_if_out.type, "axudp")) g_add_if_out.port = 10093;
                }
                if (peer_b[0]) {
                    OykPeerEntry pe;
                    memset(&pe, 0, sizeof(pe));
                    if (oyk_parse_peer_spec(peer_b, pe.callsign, sizeof(pe.callsign),
                                            &pe.ip, &pe.port,
                                            g_add_if_out.port ? g_add_if_out.port : static_cast<oyk_u16>(10093))) {
                        g_add_if_out.peers[0] = pe;
                        g_add_if_out.peer_count = 1;
                    }
                }
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
        IDC_SET_L_IF, IDC_SET_IF_LIST, IDC_SET_IF_ADD, IDC_SET_IF_REM
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

    SetDlgItemText(hdlg, IDC_SET_TAB_NODE, (tab == 0) ? "[ Node ]" : "Node");
    SetDlgItemText(hdlg, IDC_SET_TAB_BBS,  (tab == 1) ? "[ BBS ]" : "BBS");
    SetDlgItemText(hdlg, IDC_SET_TAB_CHAT, (tab == 2) ? "[ CHAT ]" : "CHAT");
    SetDlgItemText(hdlg, IDC_SET_TAB_IF,   (tab == 3) ? "[ Interfaces ]" : "Interfaces");

    static const char* BOX_TITLES[4] = {
        "Node Settings", "Packet BBS Settings", "Multi-User CHAT Settings", "Interface Mappings"
    };
    SetDlgItemText(hdlg, IDC_SET_TAB_BOX, BOX_TITLES[(tab >= 0 && tab < 4) ? tab : 0]);
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

                static OykConfigApplyResult res;
                if (!g_node.apply_config(&g_set_draft, 1, &res)) {
                    MessageBox(hdlg, res.error[0] ? res.error : "Invalid configuration.",
                               "Settings Error", MB_OK | MB_ICONSTOP);
                    return TRUE;
                }
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
