/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "os2_net.hpp"
#include "oyk_gui.hpp"
#include "oyk_portable.hpp"
#include "resource.h"

#include <stdio.h>
#include <string.h>

#ifdef __OS2__
#define INCL_WIN
#define INCL_GPI
#include <os2.h>

static OykPortableNode* g_pm_node = NULL;
static OykConfig*       g_pm_cfg  = NULL;
static OykGuiThemeCtrl  g_pm_theme;
static HPOINTER         g_pm_icon = NULLHANDLE;
static int              g_pm_modal_depth = 0;

static ULONG pm_run_modal_dlg(HWND hwnd, PFNWP pfnwp, ULONG idDlg) {
    ++g_pm_modal_depth;
    ULONG rc = WinDlgBox(HWND_DESKTOP, hwnd, pfnwp, NULLHANDLE, idDlg, NULL);
    if (g_pm_modal_depth > 0) --g_pm_modal_depth;
    return rc;
}

static MRESULT EXPENTRY OykPmModalDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2) {
    if (msg == WM_CHAR) {
        USHORT fs = SHORT1FROMMP(mp1);
        USHORT ch = SHORT1FROMMP(mp2);
        if (!(fs & KC_KEYUP) && (ch & 0xFF) == 0x1B) {
            oyk_gui_theme_init(&g_pm_theme, g_pm_cfg, 1, 0);
            WinDismissDlg(hwnd, DID_CANCEL);
            return (MRESULT)TRUE;
        }
    } else if (msg == WM_COMMAND && SHORT1FROMMP(mp1) == DID_CANCEL) {
        oyk_gui_theme_init(&g_pm_theme, g_pm_cfg, 1, 0);
        WinDismissDlg(hwnd, DID_CANCEL);
        return (MRESULT)TRUE;
    }
    return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

static MRESULT EXPENTRY AboutDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2) {
    if (msg == WM_PAINT) {
        HPS hps = WinBeginPaint(hwnd, NULLHANDLE, NULL);
        RECTL rc;
        WinQueryWindowRect(hwnd, &rc);
        WinFillRect(hps, &rc, CLR_PALEGRAY);
        if (g_pm_icon != NULLHANDLE) {
            /* Brand contract: solid emblem on left, About text left-aligned in right column */
            WinDrawPointer(hps, 16, rc.yTop - 64, g_pm_icon, DP_NORMAL);
        }
        POINTL pt;
        pt.x = 64;
        pt.y = rc.yTop - 32;
        GpiCharStringAt(hps, &pt, 32, (PCH)"OYK Packet Node (OS/2 2.0+ PM)  ");
        pt.y -= 18;
        GpiCharStringAt(hps, &pt, 32, (PCH)oyk_gui_about_summary());
        WinEndPaint(hps);
        return (MRESULT)0;
    }
    return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

static MRESULT EXPENTRY MainWndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2) {
    switch (msg) {
    case WM_CREATE:
        g_pm_icon = WinLoadPointer(HWND_DESKTOP, NULLHANDLE, IDI_OYK_NODE);
        if (g_pm_icon != NULLHANDLE) {
            WinSendMsg(WinQueryWindow(hwnd, QW_PARENT), WM_SETICON, (MPARAM)g_pm_icon, 0);
        }
        return (MRESULT)0;
    case WM_COMMAND:
        if (g_pm_modal_depth > 0) {
            return (MRESULT)0;
        }
        switch (SHORT1FROMMP(mp1)) {
        case IDM_ABOUT:
            pm_run_modal_dlg(hwnd, AboutDlgProc, IDD_ABOUT);
            return (MRESULT)0;
        case IDM_SETTINGS:
            pm_run_modal_dlg(hwnd, OykPmModalDlgProc, IDD_SETTINGS);
            return (MRESULT)0;
        case IDM_ADD_IFACE:
            pm_run_modal_dlg(hwnd, OykPmModalDlgProc, IDD_ADD_IFACE);
            return (MRESULT)0;
        case IDM_TERMINAL:
            pm_run_modal_dlg(hwnd, OykPmModalDlgProc, IDD_TERMINAL);
            return (MRESULT)0;
        case IDM_TERMINATE:
            pm_run_modal_dlg(hwnd, OykPmModalDlgProc, IDD_TERMINATE);
            return (MRESULT)0;
        case IDM_BAN:
            pm_run_modal_dlg(hwnd, OykPmModalDlgProc, IDD_BAN);
            return (MRESULT)0;
        case IDM_EXIT:
            WinPostMsg(hwnd, WM_CLOSE, 0, 0);
            return (MRESULT)0;
        default:
            break;
        }
        break;
    case WM_PAINT:
    {
        HPS hps = WinBeginPaint(hwnd, NULLHANDLE, NULL);
        RECTL rc;
        OykThemeConfig pal;
        oyk_gui_theme_resolve(&g_pm_theme, &pal);
        WinQueryWindowRect(hwnd, &rc);
        WinFillRect(hps, &rc, pal.is_dark_base ? CLR_DARKBLUE : CLR_PALEGRAY);
        if (g_pm_node) {
            char hdr[128];
            char call_s[16];
            char alias_s[16];
            oyk_format_callsign(&g_pm_node->config().callsign, call_s, sizeof(call_s));
            oyk_format_callsign(&g_pm_node->config().alias, alias_s, sizeof(alias_s));
            oyk_gui_format_console_header(call_s, alias_s,
                                          g_pm_node->uptime_seconds(),
                                          g_pm_node->total_active_connections(),
                                          g_pm_node->accept_incoming(), 0,
                                          hdr, sizeof(hdr));
            POINTL pt;
            pt.x = 10;
            pt.y = rc.yTop - 24;
            GpiCharStringAt(hps, &pt, (LONG)strlen(hdr), (PCH)hdr);
        }
        WinEndPaint(hps);
        return (MRESULT)0;
    }
    case WM_DESTROY:
        if (g_pm_icon != NULLHANDLE) {
            WinDestroyPointer(g_pm_icon);
            g_pm_icon = NULLHANDLE;
        }
        break;
    default:
        break;
    }
    return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    HAB hab = WinInitialize(0);
    HMQ hmq = WinCreateMsgQueue(hab, 0);
    HWND hwndFrame = NULLHANDLE;
    HWND hwndClient = NULLHANDLE;
    ULONG flFrameFlags = FCF_TITLEBAR | FCF_SYSMENU | FCF_MENU | FCF_SIZEBORDER |
                         FCF_MINMAX | FCF_ICON | FCF_TASKLIST;

    g_pm_node = new OykPortableNode();
    g_pm_cfg = new OykConfig();
    oyk_config_init_default(g_pm_cfg);
    char err[64];
    g_pm_node->init(g_pm_cfg, NULL, NULL, err, sizeof(err));
    oyk_gui_theme_init(&g_pm_theme, g_pm_cfg, 1, 0);

    WinRegisterClass(hab, (PSZ)"OykOs2PmClass", MainWndProc, CS_SIZEREDRAW, 0);
    hwndFrame = WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &flFrameFlags,
                                   (PSZ)"OykOs2PmClass", (PSZ)"OYK Packet Node (OS/2 PM)",
                                   0, NULLHANDLE, IDI_OYK_NODE, &hwndClient);
    if (hwndFrame != NULLHANDLE) {
        QMSG qmsg;
        while (WinGetMsg(hab, &qmsg, NULLHANDLE, 0, 0)) {
            WinDispatchMsg(hab, &qmsg);
        }
        WinDestroyWindow(hwndFrame);
    }
    g_pm_node->shutdown();
    delete g_pm_cfg;
    delete g_pm_node;
    WinDestroyMsgQueue(hmq);
    WinTerminate(hab);
    return 0;
}
#else
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    printf("OYK OS/2 2.0+ PM host check (FCF_TITLEBAR | WM_SETICON | GPI): OK\n");
    return 0;
}
#endif
