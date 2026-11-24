/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * OYK Amateur Radio Packet Node - Native Windows GUI Port
 *
 * Compatible with:
 *   - 16-bit Windows 3.1 (OpenWatcom 1.9, Winsock 1.1)
 *   - 32-bit Windows 95, 98, ME & Windows NT 3.51 through Windows 11 (x86)
 *   - 64-bit Windows XP x64 through Windows 11 (x64)
 *
 * Uses pure classic Win16/Win32 USER/GDI + Winsock 1.1 APIs (no Vista+ dependencies).
 * Layout mirrors the Linux ncurses and GTK+ 2.0 interfaces.
 */

#include <windows.h>
#include <winsock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../compat/oyk_portable.hpp"
#include "resource.h"

#if defined(_WIN32) || defined(_WIN64)
  #define OYK_DLG_RET  INT_PTR CALLBACK
  #define OYK_WND_RET  LRESULT CALLBACK
  #define OYK_CMD_ID(wp, lp)  LOWORD(wp)
#else
  /* 16-bit Windows 3.1 */
  #define OYK_DLG_RET  BOOL FAR PASCAL
  #define OYK_WND_RET  LRESULT FAR PASCAL
  #define OYK_CMD_ID(wp, lp)  (wp)
  #ifndef INVALID_SOCKET
    #define INVALID_SOCKET ((SOCKET)(~0))
  #endif
#endif

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
static SOCKET g_axudp_sock = INVALID_SOCKET;
static int g_axudp_iface_idx = -1;
static int g_last_log_count = -1;
static char g_status_msg[128] = "Ready.";

/* ==================== Winsock 1.1 AXUDP Pump ==================== */

static int win_tx_callback(int iface_idx, const OykFrame* frame,
                           oyk_u32 peer_ip, oyk_u16 peer_port,
                           oyk_u16 /*peer_handle*/, void* /*ctx*/) {
    if (iface_idx != g_axudp_iface_idx || g_axudp_sock == INVALID_SOCKET) return 0;
    if (peer_ip == 0 || peer_port == 0) return 0;

    oyk_u8 raw[320];
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

    int sent = sendto(g_axudp_sock, reinterpret_cast<const char*>(raw),
                      static_cast<int>(n), 0,
                      reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst));
    return (sent == static_cast<int>(n)) ? 1 : 0;
}

static void pump_winsock() {
    if (g_axudp_sock == INVALID_SOCKET || g_axudp_iface_idx < 0) return;

    for (int iter = 0; iter < 16; ++iter) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(g_axudp_sock, &rfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        if (select(0, &rfds, NULL, NULL, &tv) <= 0) break;

        oyk_u8 buf[1024];
        struct sockaddr_in src;
        int slen = sizeof(src);
        int n = recvfrom(g_axudp_sock, reinterpret_cast<char*>(buf),
                         sizeof(buf), 0,
                         reinterpret_cast<struct sockaddr*>(&src), &slen);
        if (n <= 2) break;
        if (!oyk_verify_fcs(buf, static_cast<size_t>(n))) continue;

        OykFrame frame;
        if (!oyk_ax25_decode(buf, static_cast<size_t>(n - 2), &frame)) continue;

        g_node.on_rx_frame(g_axudp_iface_idx, &frame,
                           static_cast<oyk_u32>(src.sin_addr.s_addr),
                           ntohs(src.sin_port), 0);
    }
}

/* ==================== UI Refresh & Dialogs ==================== */

static void set_status(const char* text) {
    strncpy(g_status_msg, text, sizeof(g_status_msg) - 1);
    g_status_msg[sizeof(g_status_msg) - 1] = '\0';
    if (g_ctl_status) SetWindowTextA(g_ctl_status, g_status_msg);
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
    SetWindowTextA(g_ctl_header, buf);

    SetWindowTextA(g_ctl_badge, g_node.accept_incoming() ? "[INCOMING: ALLOW]" : "[INCOMING: DENY]");

    /* Radio Interfaces (active connections per interface) */
    SendMessageA(g_ctl_ifaces, LB_RESETCONTENT, 0, 0);
    SendMessageA(g_ctl_ifaces, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>("Port      Type     Conn  Status          RX/TX"));
    for (int i = 0; i < g_node.iface_count(); ++i) {
        const OykIfaceRuntime& r = g_node.iface(i);
        sprintf(buf, "%-9.9s %-8.8s %-5d %-15.15s %lu/%lu",
                r.cfg.id, r.cfg.type, g_node.active_connections_on(i), r.status,
                static_cast<unsigned long>(r.rx_frames),
                static_cast<unsigned long>(r.tx_frames));
        SendMessageA(g_ctl_ifaces, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(buf));
    }

    /* Last 5 New Callsigns Heard */
    SendMessageA(g_ctl_heard, LB_RESETCONTENT, 0, 0);
    SendMessageA(g_ctl_heard, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>("#  Callsign   Port      Heard"));
    for (int i = 0; i < g_node.new_heard_count(); ++i) {
        const OykNewHeard& nh = g_node.new_heard(i);
        sprintf(buf, "%d. %-10.10s %-9.9s %s", i + 1, nh.callsign, nh.iface_id, nh.time_hms);
        SendMessageA(g_ctl_heard, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(buf));
    }

    /* Active Connections */
    SendMessageA(g_ctl_sessions, LB_RESETCONTENT, 0, 0);
    SendMessageA(g_ctl_sessions, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>("ID    Callsign    Port      Since     Mode"));
    for (int i = 0; i < g_node.max_sessions(); ++i) {
        const OykSession& s = g_node.session_slot(i);
        if (s.state == OYK_SESS_CONNECTED) {
            char cs[12];
            oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
            sprintf(buf, "#%-4lu %-11.11s %-9.9s %-9.9s %s",
                    static_cast<unsigned long>(s.id), cs,
                    g_node.iface(s.iface_idx).cfg.id, s.since_hms,
                    s.restricted_ban ? "BAN NOTICE" : "NODE SHELL");
            SendMessageA(g_ctl_sessions, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(buf));
        }
    }

    /* Node Activity Log */
    int log_n = g_node.console_log_count();
    if (log_n != g_last_log_count) {
        g_last_log_count = log_n;
        SendMessageA(g_ctl_logs, LB_RESETCONTENT, 0, 0);
        for (int i = 0; i < log_n; ++i) {
            SendMessageA(g_ctl_logs, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(g_node.console_log_line(i)));
        }
        if (log_n > 0) {
            SendMessageA(g_ctl_logs, LB_SETTOPINDEX, static_cast<WPARAM>(log_n - 1), 0);
        }
    }
}

static OYK_DLG_RET TerminateDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
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
                    SendMessageA(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row));
                    s_ids[s_count++] = s.id;
                }
            }
            if (s_count > 0) SendMessageA(lb, LB_SETCURSEL, 0, 0);
            return TRUE;
        }
        case WM_COMMAND: {
            WORD id = OYK_CMD_ID(wp, lp);
            if (id == IDOK) {
                int sel = static_cast<int>(SendDlgItemMessageA(hdlg, IDC_TERM_LIST, LB_GETCURSEL, 0, 0));
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
    SendMessageA(lb, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_node.ban_count(); ++i) {
        const OykBanEntry& e = g_node.ban_entry(i);
        char row[140];
        sprintf(row, "%-10s [%s] (%s) %s",
                e.key, (e.flags & OYK_BAN_HARD_BAN) ? "HARD" : "SOFT",
                e.lift_at, e.reason);
        SendMessageA(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row));
    }
}

static OYK_DLG_RET BanDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    switch (msg) {
        case WM_INITDIALOG:
            SetDlgItemTextA(hdlg, IDC_BAN_REASON, "Banned by sysop.");
            CheckDlgButton(hdlg, IDC_BAN_APPEAL, BST_CHECKED);
            fill_ban_listbox(GetDlgItem(hdlg, IDC_BAN_LIST));
            return TRUE;
        case WM_COMMAND: {
            WORD id = OYK_CMD_ID(wp, lp);
            if (id == IDC_BAN_ADD) {
                char call[16], reason[96], fb[96];
                GetDlgItemTextA(hdlg, IDC_BAN_CALL, call, sizeof(call));
                GetDlgItemTextA(hdlg, IDC_BAN_REASON, reason, sizeof(reason));
                oyk_u32 flags = 0;
                if (IsDlgButtonChecked(hdlg, IDC_BAN_HARD)) flags |= OYK_BAN_HARD_BAN;
                if (IsDlgButtonChecked(hdlg, IDC_BAN_APPEAL)) flags |= OYK_BAN_APPEAL_ALLOWED;
                if (g_node.ban_callsign(call, flags, reason, "permanent", fb, sizeof(fb))) {
                    SetDlgItemTextA(hdlg, IDC_BAN_CALL, "");
                    set_status(fb);
                    fill_ban_listbox(GetDlgItem(hdlg, IDC_BAN_LIST));
                } else {
                    MessageBoxA(hdlg, fb, "Ban Error", MB_OK | MB_ICONEXCLAMATION);
                }
                return TRUE;
            }
            if (id == IDC_BAN_REMOVE) {
                HWND lb = GetDlgItem(hdlg, IDC_BAN_LIST);
                int sel = static_cast<int>(SendMessageA(lb, LB_GETCURSEL, 0, 0));
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

static OYK_WND_RET MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_mono_font = static_cast<HFONT>(GetStockObject(OEM_FIXED_FONT));
            g_ctl_header = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE,
                                         0, 0, 100, 20, hwnd, reinterpret_cast<HMENU>(IDC_HEADER_TEXT), g_hinst, NULL);
            g_ctl_badge = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE | SS_CENTER | WS_BORDER,
                                        0, 0, 100, 20, hwnd, reinterpret_cast<HMENU>(IDC_INCOMING_BADGE), g_hinst, NULL);
            g_ctl_ifaces = CreateWindowA("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                         0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_IFACE_LIST), g_hinst, NULL);
            g_ctl_heard = CreateWindowA("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                        0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_HEARD_LIST), g_hinst, NULL);
            g_ctl_sessions = CreateWindowA("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                           0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_SESSION_LIST), g_hinst, NULL);
            g_ctl_logs = CreateWindowA("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                       0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_LOG_LIST), g_hinst, NULL);
            g_ctl_status = CreateWindowA("STATIC", g_status_msg, WS_CHILD | WS_VISIBLE,
                                         0, 0, 100, 18, hwnd, reinterpret_cast<HMENU>(IDC_STATUS_TEXT), g_hinst, NULL);

            SendMessageA(g_ctl_ifaces, WM_SETFONT, reinterpret_cast<WPARAM>(g_mono_font), FALSE);
            SendMessageA(g_ctl_heard, WM_SETFONT, reinterpret_cast<WPARAM>(g_mono_font), FALSE);
            SendMessageA(g_ctl_sessions, WM_SETFONT, reinterpret_cast<WPARAM>(g_mono_font), FALSE);
            SendMessageA(g_ctl_logs, WM_SETFONT, reinterpret_cast<WPARAM>(g_mono_font), FALSE);

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
                    DialogBoxA(g_hinst, MAKEINTRESOURCEA(IDD_TERMINATE), hwnd, TerminateDlgProc);
                    refresh_gui();
                }
            } else if (id == IDM_BAN) {
                DialogBoxA(g_hinst, MAKEINTRESOURCEA(IDD_BAN), hwnd, BanDlgProc);
                refresh_gui();
            } else if (id == IDM_EXIT) {
                PostMessageA(hwnd, WM_CLOSE, 0, 0);
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
            if (MessageBoxA(hwnd, msg_buf, "Exit", MB_YESNO | MB_ICONQUESTION) == IDYES) {
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
    return DefWindowProcA(hwnd, msg, wp, lp);
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

int PASCAL WinMain(HINSTANCE hinst, HINSTANCE hprev, LPSTR cmdline, int nshow) {
    g_hinst = hinst;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(1, 1), &wsa) != 0) {
        MessageBoxA(NULL, "Failed to initialize Winsock 1.1.", "OYK Packet Node", MB_OK | MB_ICONSTOP);
        return 1;
    }

    OykConfig cfg;
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
            MessageBoxA(NULL, err, "OYK Config Error", MB_OK | MB_ICONSTOP);
            WSACleanup();
            return 1;
        }
    } else {
        oyk_config_init_default(&cfg);
    }

    if (!g_node.init(&cfg, win_tx_callback, NULL, err, sizeof(err))) {
        MessageBoxA(NULL, err, "OYK Init Error", MB_OK | MB_ICONSTOP);
        WSACleanup();
        return 1;
    }

    /* Bind first AXUDP interface on Winsock 1.1 */
    for (int i = 0; i < g_node.iface_count(); ++i) {
        if (strcmp(g_node.iface(i).cfg.type, "axudp") == 0) {
            g_axudp_iface_idx = i;
            oyk_u16 port = g_node.iface(i).cfg.port ? g_node.iface(i).cfg.port : 10093;
            g_axudp_sock = socket(AF_INET, SOCK_DGRAM, 0);
            if (g_axudp_sock != INVALID_SOCKET) {
                u_long nb = 1;
                ioctlsocket(g_axudp_sock, FIONBIO, &nb);
                struct sockaddr_in addr;
                memset(&addr, 0, sizeof(addr));
                addr.sin_family = AF_INET;
                addr.sin_addr.s_addr = INADDR_ANY;
                addr.sin_port = htons(port);
                if (bind(g_axudp_sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
                    char st[32];
                    sprintf(st, "UP :%u", static_cast<unsigned int>(port));
                    g_node.set_iface_status(i, 1, st);
                } else {
                    g_node.set_iface_status(i, 0, "BIND ERR");
                }
            }
            break;
        }
    }

    if (!hprev) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = MainWndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszMenuName = MAKEINTRESOURCEA(IDR_MAINMENU);
        wc.lpszClassName = "OYKNodeWinClass";
        RegisterClassA(&wc);
    }

    g_hwnd_main = CreateWindowA(
        "OYKNodeWinClass",
        "OYK Amateur Radio Packet Node",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 760, 520,
        NULL, NULL, hinst, NULL);

    ShowWindow(g_hwnd_main, nshow);
    UpdateWindow(g_hwnd_main);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    g_node.shutdown();
    if (g_axudp_sock != INVALID_SOCKET) {
        closesocket(g_axudp_sock);
        g_axudp_sock = INVALID_SOCKET;
    }
    WSACleanup();
    return static_cast<int>(msg.wParam);
}
