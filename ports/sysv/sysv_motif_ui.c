/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "sysv_motif_ui.h"
#include "sysv_tui.h"
#include "sysv_logo_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(OYK_HAS_MOTIF)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <Xm/Xm.h>
#include <Xm/MainW.h>
#include <Xm/RowColumn.h>
#include <Xm/CascadeB.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/Label.h>
#include <Xm/TextF.h>
#include <Xm/Text.h>
#include <Xm/List.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Separator.h>
#include <Xm/PanedW.h>
#include <Xm/DrawingA.h>
#include <Xm/DialogS.h>

typedef struct {
    OykSysvApp   *app;
    XtAppContext  xt_app;
    Display      *dpy;
    Widget        top_shell;
    Widget        main_win;
    Widget        menu_bar;
    Widget        node_cascade;
    Widget        node_menu;
    Widget        main_form;
    Widget        header_lbl;
    Widget        pill_lbl;
    Widget        iface_list;
    Widget        heard_list;
    Widget        conn_list;
    Widget        log_list;
    Widget        footer_row;
    Widget        modal_shell;
    Pixmap        icon_pixmap;
    Pixmap        icon_mask;
    Pixel         px_bg;
    Pixel         px_fg;
    Pixel         px_hdr_bg;
    Pixel         px_hdr_fg;
    Pixel         px_allow_bg;
    Pixel         px_deny_bg;
    Pixel         px_panel_bg;
    Pixel         px_sel_bg;
    Pixel         px_sel_fg;
    int           test_step;
    int           quit;
} MotifUiCtx;

static MotifUiCtx g_mui;

static Pixel alloc_rgb_pixel(Display *dpy, sysv_u32 rgb) {
    Colormap cm;
    XColor xc;
    cm = DefaultColormap(dpy, DefaultScreen(dpy));
    xc.red   = (unsigned short)(((rgb >> 16) & 0xFFUL) * 257UL);
    xc.green = (unsigned short)(((rgb >> 8) & 0xFFUL) * 257UL);
    xc.blue  = (unsigned short)((rgb & 0xFFUL) * 257UL);
    xc.flags = DoRed | DoGreen | DoBlue;
    if (XAllocColor(dpy, cm, &xc)) return xc.pixel;
    return ((rgb & 0xFFFFFFUL) > 0x7F7F7FUL)
         ? WhitePixel(dpy, DefaultScreen(dpy))
         : BlackPixel(dpy, DefaultScreen(dpy));
}

static void apply_theme_colors(MotifUiCtx *ctx) {
    OykSysvThemeConfig pal;
    Arg args[4];
    int n;
    oyk_sysv_theme_resolve(&ctx->app->theme_ctrl, &pal);
    ctx->px_bg       = alloc_rgb_pixel(ctx->dpy, pal.colors[OYK_SYSV_TR_BG]);
    ctx->px_fg       = alloc_rgb_pixel(ctx->dpy, pal.colors[OYK_SYSV_TR_FG]);
    ctx->px_hdr_bg   = alloc_rgb_pixel(ctx->dpy, pal.colors[OYK_SYSV_TR_HEADER_BG]);
    ctx->px_hdr_fg   = alloc_rgb_pixel(ctx->dpy, pal.colors[OYK_SYSV_TR_HEADER_FG]);
    ctx->px_allow_bg = alloc_rgb_pixel(ctx->dpy, pal.colors[OYK_SYSV_TR_OK]);
    ctx->px_deny_bg  = alloc_rgb_pixel(ctx->dpy, pal.colors[OYK_SYSV_TR_ERROR]);
    ctx->px_panel_bg = alloc_rgb_pixel(ctx->dpy, pal.colors[OYK_SYSV_TR_PANEL_BG]);
    ctx->px_sel_bg   = alloc_rgb_pixel(ctx->dpy, pal.colors[OYK_SYSV_TR_SELECTION_BG]);
    ctx->px_sel_fg   = alloc_rgb_pixel(ctx->dpy, pal.colors[OYK_SYSV_TR_SELECTION_FG]);

    n = 0;
    XtSetArg(args[n], XmNbackground, ctx->px_bg); ++n;
    XtSetArg(args[n], XmNforeground, ctx->px_fg); ++n;
    if (ctx->main_form) XtSetValues(ctx->main_form, args, n);
    if (ctx->header_lbl) {
        n = 0;
        XtSetArg(args[n], XmNbackground, ctx->px_hdr_bg); ++n;
        XtSetArg(args[n], XmNforeground, ctx->px_hdr_fg); ++n;
        XtSetValues(ctx->header_lbl, args, n);
    }
    if (ctx->pill_lbl) {
        n = 0;
        XtSetArg(args[n], XmNbackground,
                 ctx->app->node.cfg.accept_incoming ? ctx->px_allow_bg : ctx->px_deny_bg); ++n;
        XtSetArg(args[n], XmNforeground, ctx->px_hdr_fg); ++n;
        XtSetValues(ctx->pill_lbl, args, n);
    }
    if (ctx->iface_list) {
        n = 0;
        XtSetArg(args[n], XmNbackground, ctx->px_panel_bg); ++n;
        XtSetArg(args[n], XmNforeground, ctx->px_fg); ++n;
        XtSetValues(ctx->iface_list, args, n);
    }
    if (ctx->heard_list) {
        n = 0;
        XtSetArg(args[n], XmNbackground, ctx->px_panel_bg); ++n;
        XtSetArg(args[n], XmNforeground, ctx->px_fg); ++n;
        XtSetValues(ctx->heard_list, args, n);
    }
    if (ctx->conn_list) {
        n = 0;
        XtSetArg(args[n], XmNbackground, ctx->px_panel_bg); ++n;
        XtSetArg(args[n], XmNforeground, ctx->px_fg); ++n;
        XtSetValues(ctx->conn_list, args, n);
    }
    if (ctx->log_list) {
        n = 0;
        XtSetArg(args[n], XmNbackground, ctx->px_panel_bg); ++n;
        XtSetArg(args[n], XmNforeground, ctx->px_fg); ++n;
        XtSetValues(ctx->log_list, args, n);
    }
}

static void list_set_rows(Widget lw, const char * const *rows, int count) {
    XmString items[32];
    int i;
    int n;
    n = (count > 32) ? 32 : count;
    for (i = 0; i < n; ++i) {
        items[i] = XmStringCreateLocalized((char *)rows[i]);
    }
    XmListDeleteAllItems(lw);
    if (n > 0) XmListAddItems(lw, items, n, 1);
    for (i = 0; i < n; ++i) XmStringFree(items[i]);
}

static void set_label_text(Widget w, const char *text) {
    XmString s;
    Arg args[1];
    s = XmStringCreateLocalized((char *)(text ? text : ""));
    XtSetArg(args[0], XmNlabelString, s);
    XtSetValues(w, args, 1);
    XmStringFree(s);
}

static void refresh_main_console(MotifUiCtx *ctx) {
    const OykSysvNode *n;
    char call_s[16];
    char alias_s[16];
    char hdr[140];
    const char *rows[32];
    char rbufs[32][96];
    int rc;
    int i;
    n = &ctx->app->node;
    oyk_sysv_format_callsign(&n->cfg.callsign, call_s, sizeof(call_s));
    oyk_sysv_format_callsign(&n->cfg.alias, alias_s, sizeof(alias_s));
    oyk_sysv_format_console_header(call_s, alias_s, n->uptime_sec,
                                   oyk_sysv_node_active_conn_count(n),
                                   n->cfg.accept_incoming, 0, hdr, sizeof(hdr));
    set_label_text(ctx->header_lbl, hdr);
    set_label_text(ctx->pill_lbl, oyk_sysv_format_incoming_pill(n->cfg.accept_incoming, 0));

    rc = 0;
    sprintf(rbufs[rc], "%-8s %-11s %-4s %-6s %s", "Port", "Type", "Conn", "Status", "RX/TX");
    rows[rc] = rbufs[rc]; ++rc;
    for (i = 0; i < n->cfg.iface_count && rc < 32; ++i) {
        sprintf(rbufs[rc], "%-8.8s %-11.11s %-4d %-6s %lu/%lu",
                n->cfg.ifaces[i].id, n->cfg.ifaces[i].type, 1,
                n->iface_rt[i].running ? "UP" : "DOWN",
                (unsigned long)n->iface_rt[i].rx_frames,
                (unsigned long)n->iface_rt[i].tx_frames);
        rows[rc] = rbufs[rc]; ++rc;
    }
    list_set_rows(ctx->iface_list, rows, rc);

    rc = 0;
    sprintf(rbufs[rc], "%-2s %-9s %-7s %s", "#", "Callsign", "Port", "Heard");
    rows[rc] = rbufs[rc]; ++rc;
    for (i = 0; i < n->heard_count && rc < 32; ++i) {
        sprintf(rbufs[rc], "%-2d %-9.9s %-7.7s %s", i + 1,
                n->heard[i].callsign, n->heard[i].iface_id, n->heard[i].time_hms);
        rows[rc] = rbufs[rc]; ++rc;
    }
    list_set_rows(ctx->heard_list, rows, rc);

    rc = 0;
    sprintf(rbufs[rc], "%-4s %-11s %-9s %-4s %-9s %-8s %s",
            "ID", "Callsign", "Port", "Dir", "State", "Since", "Role");
    rows[rc] = rbufs[rc]; ++rc;
    for (i = 0; i < OYK_SYSV_MAX_SESSIONS && rc < 32; ++i) {
        if (n->sessions[i].state == 2) {
            sprintf(rbufs[rc], "%-4lu %-11.11s %-9.9s %-4.4s %-9s %-8.8s %s",
                    (unsigned long)n->sessions[i].id,
                    n->sessions[i].callsign, n->sessions[i].port_id,
                    n->sessions[i].dir, "CONNECTED",
                    n->sessions[i].since_hms, n->sessions[i].role);
            rows[rc] = rbufs[rc]; ++rc;
        }
    }
    list_set_rows(ctx->conn_list, rows, rc);

    rc = 0;
    for (i = 0; i < n->log_count && rc < 32; ++i) {
        oyk_sysv_copy_str(rbufs[rc], sizeof(rbufs[rc]), n->logs[i]);
        rows[rc] = rbufs[rc]; ++rc;
    }
    list_set_rows(ctx->log_list, rows, rc);
    apply_theme_colors(ctx);
}

static void close_modal_dialog(MotifUiCtx *ctx) {
    if (ctx->node_menu && XtIsManaged(ctx->node_menu)) {
        XtUnmanageChild(ctx->node_menu);
    }
    if (ctx->modal_shell) {
        XtDestroyWidget(ctx->modal_shell);
        ctx->modal_shell = NULL;
    }
}

static void on_close_modal_cb(Widget w, XtPointer client, XtPointer call) {
    (void)w; (void)call;
    close_modal_dialog((MotifUiCtx *)client);
    ((MotifUiCtx *)client)->app->screen = OYK_SYSV_SCR_MAIN;
}

static void on_modal_key_ev(Widget w, XtPointer client, XEvent *ev, Boolean *cont) {
    MotifUiCtx *ctx;
    KeySym ks;
    (void)w; (void)cont;
    ctx = (MotifUiCtx *)client;
    if (!ev || ev->type != KeyPress) return;
    ks = XLookupKeysym(&ev->xkey, 0);
    if (ks == XK_Escape) {
        close_modal_dialog(ctx);
        ctx->app->screen = OYK_SYSV_SCR_MAIN;
        *cont = False;
    }
}

static void on_about_expose(Widget w, XtPointer client, XtPointer call) {
    MotifUiCtx *ctx;
    Window win;
    GC gc;
    int y;
    int x;
    Pixel c_navy;
    Pixel c_white;
    Pixel c_dark;
    (void)call;
    ctx = (MotifUiCtx *)client;
    win = XtWindow(w);
    if (!win) return;
    gc = XCreateGC(ctx->dpy, win, 0, NULL);
    c_navy  = alloc_rgb_pixel(ctx->dpy, 0x1E354DUL);
    c_white = alloc_rgb_pixel(ctx->dpy, 0xFFFFFFUL);
    c_dark  = alloc_rgb_pixel(ctx->dpy, 0x101010UL);
    for (y = 0; y < 32; ++y) {
        for (x = 0; x < 32; ++x) {
            unsigned char idx;
            idx = kOykSysvLogoSolid32Pal[y * 32 + x];
            if (idx == 0) continue;
            XSetForeground(ctx->dpy, gc,
                           (idx == 2) ? c_white : ((idx == 3) ? c_dark : c_navy));
            XFillRectangle(ctx->dpy, win, gc, 4 + x * 2, 4 + y * 2, 2, 2);
        }
    }
    XFreeGC(ctx->dpy, gc);
}

static void add_labeled_field(Widget parent, const char *label, const char *val) {
    Widget row;
    Widget lbl;
    Widget tf;
    Arg args[4];
    row = XmCreateRowColumn(parent, "frow", NULL, 0);
    XtVaSetValues(row, XmNorientation, XmHORIZONTAL, XmNbackground, g_mui.px_bg, NULL);
    lbl = XtVaCreateManagedWidget("flbl", xmLabelWidgetClass, row,
                                  XmNbackground, g_mui.px_bg,
                                  XmNforeground, g_mui.px_fg,
                                  XmNwidth, 160, XmNrecomputeSize, False,
                                  XmNalignment, XmALIGNMENT_BEGINNING, NULL);
    set_label_text(lbl, label);
    args[0].name = XmNcolumns; args[0].value = 26;
    args[1].name = XmNvalue;   args[1].value = (XtArgVal)(val ? val : "");
    tf = XmCreateTextField(row, "ftf", args, 2);
    XtManageChild(tf);
    XtManageChild(row);
}

static void open_modal_for_screen(MotifUiCtx *ctx) {
    Widget dlg;
    Widget box;
    Widget btn_row;
    Widget b;
    char buf[160];
    int i;
    const OykSysvApp *app;
    Position mx, my;
    Dimension mw, mh;

    close_modal_dialog(ctx);
    app = ctx->app;
    if (app->screen == OYK_SYSV_SCR_MAIN) return;
    if (app->screen == OYK_SYSV_SCR_MENU) {
        XtVaGetValues(ctx->node_cascade, XmNx, &mx, XmNy, &my, XmNheight, &mh, NULL);
        {
            Position rx, ry;
            XtTranslateCoords(ctx->node_cascade, 0, (Position)mh, &rx, &ry);
            XtVaSetValues(ctx->node_menu, XmNx, rx, XmNy, ry, NULL);
        }
        XtManageChild(ctx->node_menu);
        return;
    }

    XtVaGetValues(ctx->top_shell, XmNx, &mx, XmNy, &my, XmNwidth, &mw, XmNheight, &mh, NULL);
    ctx->modal_shell = XmCreateDialogShell(ctx->top_shell, "modal", NULL, 0);
    XtVaSetValues(ctx->modal_shell,
                  XmNtitle, (char *)oyk_sysv_screen_title(app->screen),
                  XmNdeleteResponse, XmDO_NOTHING,
                  NULL);

    dlg = XmCreateForm(ctx->modal_shell, "dlg_form", NULL, 0);
    XtVaSetValues(dlg, XmNbackground, ctx->px_bg, XmNforeground, ctx->px_fg,
                  XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL,
                  XmNmarginWidth, 12, XmNmarginHeight, 10, NULL);
    XtAddEventHandler(ctx->modal_shell, KeyPressMask, False, on_modal_key_ev, ctx);
    XtAddEventHandler(dlg, KeyPressMask, False, on_modal_key_ev, ctx);

    box = XmCreateRowColumn(dlg, "dlg_body", NULL, 0);
    XtVaSetValues(box, XmNorientation, XmVERTICAL, XmNspacing, 6,
                  XmNbackground, ctx->px_bg,
                  XmNtopAttachment, XmATTACH_FORM,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, NULL);

    if (app->screen == OYK_SYSV_SCR_SETTINGS) {
        Widget tabs;
        char call_s[16];
        char alias_s[16];
        tabs = XmCreateRowColumn(box, "tabs", NULL, 0);
        XtVaSetValues(tabs, XmNorientation, XmHORIZONTAL, XmNbackground, ctx->px_bg, NULL);
        for (i = 0; i < 8; ++i) {
            Widget tb;
            tb = XmCreatePushButton(tabs, "tab", NULL, 0);
            XtVaSetValues(tb,
                          XmNbackground, (i == app->settings_tab) ? ctx->px_sel_bg : ctx->px_panel_bg,
                          XmNforeground, (i == app->settings_tab) ? ctx->px_sel_fg : ctx->px_fg,
                          NULL);
            set_label_text(tb, oyk_sysv_settings_tab_title(i));
            XtManageChild(tb);
        }
        XtManageChild(tabs);
        XtManageChild(XmCreateSeparator(box, "tsep", NULL, 0));

        if (app->settings_tab == 0) {
            Widget cb;
            oyk_sysv_format_callsign(&app->node.cfg.callsign, call_s, sizeof(call_s));
            oyk_sysv_format_callsign(&app->node.cfg.alias, alias_s, sizeof(alias_s));
            add_labeled_field(box, "Node Callsign:", call_s);
            add_labeled_field(box, "Node Alias:", alias_s);
            cb = XmCreateToggleButton(box, "Accept Incoming Connections", NULL, 0);
            XmToggleButtonSetState(cb, app->node.cfg.accept_incoming ? True : False, False);
            XtVaSetValues(cb, XmNbackground, ctx->px_bg, XmNforeground, ctx->px_fg, NULL);
            XtManageChild(cb);
        } else if (app->settings_tab == 1) {
            Widget cb1;
            Widget cb2;
            cb1 = XmCreateToggleButton(box, "Enable Internal BBS", NULL, 0);
            XmToggleButtonSetState(cb1, app->node.cfg.bbs_enabled ? True : False, False);
            XtVaSetValues(cb1, XmNbackground, ctx->px_bg, XmNforeground, ctx->px_fg, NULL);
            XtManageChild(cb1);
            cb2 = XmCreateToggleButton(box, "Enable BBS Files Area", NULL, 0);
            XmToggleButtonSetState(cb2, app->node.cfg.bbs_files_enabled ? True : False, False);
            XtVaSetValues(cb2, XmNbackground, ctx->px_bg, XmNforeground, ctx->px_fg, NULL);
            XtManageChild(cb2);
            add_labeled_field(box, "BBS Mail Path:", app->node.cfg.bbs_mail_path);
            add_labeled_field(box, "BBS Files Path:", app->node.cfg.bbs_files_path);
        } else if (app->settings_tab == 2) {
            Widget cb;
            cb = XmCreateToggleButton(box, "Enable Multi-User CHAT", NULL, 0);
            XmToggleButtonSetState(cb, app->node.cfg.chat_enabled ? True : False, False);
            XtVaSetValues(cb, XmNbackground, ctx->px_bg, XmNforeground, ctx->px_fg, NULL);
            XtManageChild(cb);
            add_labeled_field(box, "Default Topic:", app->node.cfg.chat_topic);
        } else if (app->settings_tab == 3) {
            Widget lst;
            const char *rows[8];
            char rb[8][80];
            for (i = 0; i < app->node.cfg.iface_count && i < 8; ++i) {
                sprintf(rb[i], "%-8s %-12s %-18s:%u",
                        app->node.cfg.ifaces[i].id, app->node.cfg.ifaces[i].type,
                        app->node.cfg.ifaces[i].host, (unsigned int)app->node.cfg.ifaces[i].port);
                rows[i] = rb[i];
            }
            lst = XmCreateScrolledList(box, "iflst", NULL, 0);
            XtVaSetValues(lst, XmNvisibleItemCount, 5, XmNbackground, ctx->px_panel_bg, NULL);
            list_set_rows(lst, rows, app->node.cfg.iface_count);
            XtManageChild(lst);
        } else if (app->settings_tab == 4) {
            Widget lst;
            const char *rows[8];
            char rb[8][80];
            for (i = 0; i < app->node.cfg.peer_count && i < 8; ++i) {
                sprintf(rb[i], "%-8s %-10s %-8s %s",
                        app->node.cfg.peers[i].id, app->node.cfg.peers[i].callsign,
                        app->node.cfg.peers[i].interface_id, app->node.cfg.peers[i].host);
                rows[i] = rb[i];
            }
            lst = XmCreateScrolledList(box, "plst", NULL, 0);
            XtVaSetValues(lst, XmNvisibleItemCount, 5, XmNbackground, ctx->px_panel_bg, NULL);
            list_set_rows(lst, rows, app->node.cfg.peer_count);
            XtManageChild(lst);
        } else if (app->settings_tab == 5) {
            Widget cb;
            cb = XmCreateToggleButton(box, "Enable Rig Control (rigctld)", NULL, 0);
            XmToggleButtonSetState(cb, app->node.cfg.rig_enabled ? True : False, False);
            XtVaSetValues(cb, XmNbackground, ctx->px_bg, XmNforeground, ctx->px_fg, NULL);
            XtManageChild(cb);
            add_labeled_field(box, "Rig Host:", app->node.cfg.rig_host);
            sprintf(buf, "%u", (unsigned int)app->node.cfg.rig_port);
            add_labeled_field(box, "Rig Port:", buf);
        } else if (app->settings_tab == 6) {
            Widget cb;
            cb = XmCreateToggleButton(box, "Enable Hardcopy Printer Log", NULL, 0);
            XmToggleButtonSetState(cb, app->node.cfg.printer_enabled ? True : False, False);
            XtVaSetValues(cb, XmNbackground, ctx->px_bg, XmNforeground, ctx->px_fg, NULL);
            XtManageChild(cb);
            add_labeled_field(box, "Printer Device:", app->node.cfg.printer_device);
            add_labeled_field(box, "Printer Protocol:", app->node.cfg.printer_protocol);
        } else {
            OykSysvThemeConfig pal;
            Widget hdr;
            Widget lst;
            const char *rows[12];
            char rb[12][64];
            oyk_sysv_theme_resolve(&app->theme_ctrl, &pal);
            sprintf(buf, "Theme: < %s >   (Whole-screen live preview active)", pal.name);
            hdr = XtVaCreateManagedWidget("thdr", xmLabelWidgetClass, box,
                                          XmNbackground, ctx->px_bg, XmNforeground, ctx->px_fg, NULL);
            set_label_text(hdr, buf);
            for (i = 0; i < 8; ++i) {
                sprintf(rb[i], "%-14s %-12s #%06lX",
                        oyk_sysv_theme_role_name(i),
                        oyk_sysv_theme_role_key(i),
                        (unsigned long)(pal.colors[i] & 0xFFFFFFUL));
                rows[i] = rb[i];
            }
            lst = XmCreateScrolledList(box, "thlst", NULL, 0);
            XtVaSetValues(lst, XmNvisibleItemCount, 6, XmNbackground, ctx->px_panel_bg, NULL);
            list_set_rows(lst, rows, 8);
            XtManageChild(lst);
        }
    } else if (app->screen == OYK_SYSV_SCR_ADD_IFACE) {
        Widget step_lbl;
        step_lbl = XtVaCreateManagedWidget("wstep", xmLabelWidgetClass, box,
                                           XmNbackground, ctx->px_bg,
                                           XmNforeground, ctx->px_fg, NULL);
        set_label_text(step_lbl, oyk_sysv_wiz_step_prompt(&app->wiz));
        if (app->wiz.step == 0) {
            Widget rbox;
            rbox = XmCreateRadioBox(box, "drv_radio", NULL, 0);
            XtVaSetValues(rbox, XmNbackground, ctx->px_bg, NULL);
            for (i = 0; i < oyk_sysv_wiz_driver_count(); ++i) {
                Widget rb;
                sprintf(buf, "%s - %s", oyk_sysv_wiz_driver_name(i), oyk_sysv_wiz_driver_desc(i));
                rb = XmCreateToggleButton(rbox, buf, NULL, 0);
                XmToggleButtonSetState(rb, (i == app->wiz.selected_driver_idx) ? True : False, False);
                XtVaSetValues(rb, XmNbackground, ctx->px_bg, XmNforeground, ctx->px_fg, NULL);
                XtManageChild(rb);
            }
            XtManageChild(rbox);
        } else if (app->wiz.step == 1) {
            add_labeled_field(box, "Interface ID:", app->wiz.draft.id);
            add_labeled_field(box, "Description:", app->wiz.draft.description);
        } else if (app->wiz.step == 2) {
            Widget cb;
            Widget plbl;
            sprintf(buf, "%s:%u", app->wiz.draft.host, (unsigned int)app->wiz.draft.port);
            add_labeled_field(box, "Endpoint:", buf);
            cb = XmCreateToggleButton(box, "Allow Same-Interface Forwarding", NULL, 0);
            XmToggleButtonSetState(cb, app->wiz.draft.allow_same_interface ? True : False, False);
            XtVaSetValues(cb, XmNbackground, ctx->px_bg, XmNforeground, ctx->px_fg, NULL);
            XtManageChild(cb);
            sprintf(buf, "Learned Peer Policy: %s",
                    oyk_sysv_format_learned_policy(app->wiz.draft.learned_peer_policy));
            plbl = XtVaCreateManagedWidget("lpol", xmLabelWidgetClass, box,
                                           XmNbackground, ctx->px_bg, XmNforeground, ctx->px_fg, NULL);
            set_label_text(plbl, buf);
        } else {
            Widget slbl;
            oyk_sysv_wiz_format_summary(&app->wiz, buf, sizeof(buf));
            slbl = XtVaCreateManagedWidget("wsum", xmLabelWidgetClass, box,
                                           XmNbackground, ctx->px_bg, XmNforeground, ctx->px_fg, NULL);
            set_label_text(slbl, buf);
        }
    } else if (app->screen == OYK_SYSV_SCR_EDIT_IFACE) {
        Widget cb;
        add_labeled_field(box, "Interface ID:", app->node.cfg.ifaces[0].id);
        add_labeled_field(box, "Endpoint Host:", app->node.cfg.ifaces[0].host);
        cb = XmCreateToggleButton(box, "Allow Same-Interface", NULL, 0);
        XmToggleButtonSetState(cb, app->node.cfg.ifaces[0].allow_same_interface ? True : False, False);
        XtVaSetValues(cb, XmNbackground, ctx->px_bg, XmNforeground, ctx->px_fg, NULL);
        XtManageChild(cb);
        sprintf(buf, "Learned Policy: %s",
                oyk_sysv_format_learned_policy(app->node.cfg.ifaces[0].learned_peer_policy));
        add_labeled_field(box, "Policy:", buf);
    } else if (app->screen == OYK_SYSV_SCR_EDIT_PEER) {
        add_labeled_field(box, "Peer ID:", app->node.cfg.peers[0].id);
        add_labeled_field(box, "Callsign:", app->node.cfg.peers[0].callsign);
        add_labeled_field(box, "Interface:", app->node.cfg.peers[0].interface_id);
        add_labeled_field(box, "Host:", app->node.cfg.peers[0].host);
    } else if (app->screen == OYK_SYSV_SCR_TERMINATE) {
        Widget lst;
        const char *rows[8];
        char rb[8][64];
        int rc;
        rc = 0;
        sprintf(rb[rc++], "%-4s %-12s %-10s %s", "ID", "Callsign", "Port", "State");
        rows[0] = rb[0];
        for (i = 0; i < OYK_SYSV_MAX_SESSIONS && rc < 8; ++i) {
            if (app->node.sessions[i].state == 2) {
                sprintf(rb[rc], "%-4lu %-12.12s %-10.10s CONNECTED",
                        (unsigned long)app->node.sessions[i].id,
                        app->node.sessions[i].callsign,
                        app->node.sessions[i].port_id);
                rows[rc] = rb[rc];
                ++rc;
            }
        }
        lst = XmCreateScrolledList(box, "tlst", NULL, 0);
        XtVaSetValues(lst, XmNvisibleItemCount, 5, XmNbackground, ctx->px_panel_bg, NULL);
        list_set_rows(lst, rows, rc);
        XtManageChild(lst);
    } else if (app->screen == OYK_SYSV_SCR_BAN) {
        add_labeled_field(box, "Callsign:", app->node.bans[0].key);
        add_labeled_field(box, "Reason:", app->node.bans[0].reason);
    } else if (app->screen == OYK_SYSV_SCR_EXIT_PEERS) {
        Widget lbl;
        sprintf(buf, "%d auto-learned peer(s) pending. Save before exit?",
                oyk_sysv_node_auto_peer_count(&app->node));
        lbl = XtVaCreateManagedWidget("exlbl", xmLabelWidgetClass, box,
                                      XmNbackground, ctx->px_bg, XmNforeground, ctx->px_fg, NULL);
        set_label_text(lbl, buf);
    } else if (app->screen == OYK_SYSV_SCR_THEME) {
        OykSysvThemeConfig pal;
        Widget hdr;
        Widget lst;
        const char *rows[12];
        char rb[12][64];
        oyk_sysv_theme_resolve(&app->theme_ctrl, &pal);
        sprintf(buf, "Theme: < %s >   (Whole-screen live preview active)", pal.name);
        hdr = XtVaCreateManagedWidget("thdr", xmLabelWidgetClass, box,
                                      XmNbackground, ctx->px_bg, XmNforeground, ctx->px_fg, NULL);
        set_label_text(hdr, buf);
        for (i = 0; i < 8; ++i) {
            sprintf(rb[i], "%-14s %-12s #%06lX",
                    oyk_sysv_theme_role_name(i),
                    oyk_sysv_theme_role_key(i),
                    (unsigned long)(pal.colors[i] & 0xFFFFFFUL));
            rows[i] = rb[i];
        }
        lst = XmCreateScrolledList(box, "thlst", NULL, 0);
        XtVaSetValues(lst, XmNvisibleItemCount, 6, XmNbackground, ctx->px_panel_bg, NULL);
        list_set_rows(lst, rows, 8);
        XtManageChild(lst);
    } else if (app->screen == OYK_SYSV_SCR_TERMINAL) {
        Widget txt;
        Widget bar;
        Arg targs[5];
        targs[0].name = XmNrows; targs[0].value = 10;
        targs[1].name = XmNcolumns; targs[1].value = 60;
        targs[2].name = XmNeditable; targs[2].value = False;
        targs[3].name = XmNeditMode; targs[3].value = XmMULTI_LINE_EDIT;
        targs[4].name = XmNvalue; targs[4].value = (XtArgVal)"*** Connected to KR4OYK-1 Packet BBS\nType H for BBS commands.";
        txt = XmCreateText(box, "term_out", targs, 5);
        XtManageChild(txt);
        bar = XtVaCreateManagedWidget("hbar", xmLabelWidgetClass, box,
                                      XmNbackground, ctx->px_hdr_bg, XmNforeground, ctx->px_hdr_fg, NULL);
        set_label_text(bar, oyk_sysv_term_hotkey_bar_text(0));
        add_labeled_field(box, "Input:", "");
    } else if (app->screen == OYK_SYSV_SCR_TERM_HELP) {
        Widget lst;
        const char *rows[8];
        char rb[8][64];
        for (i = 0; i < oyk_sysv_term_help_count() && i < 8; ++i) {
            sprintf(rb[i], "%-10s %-34s %s",
                    oyk_sysv_term_help_key(i),
                    oyk_sysv_term_help_action(i),
                    oyk_sysv_term_help_alt(i));
            rows[i] = rb[i];
        }
        lst = XmCreateScrolledList(box, "hlst", NULL, 0);
        XtVaSetValues(lst, XmNvisibleItemCount, 8, XmNbackground, ctx->px_panel_bg, NULL);
        list_set_rows(lst, rows, oyk_sysv_term_help_count());
        XtManageChild(lst);
    } else if (app->screen == OYK_SYSV_SCR_FILES) {
        Widget paned;
        Widget lp;
        Widget rp;
        Widget llist;
        Widget rlist;
        const char *lrows[8];
        const char *rrows[8];
        char lb[8][48];
        char rb[8][48];
        char lt[48];
        char rt[48];
        paned = XmCreateRowColumn(box, "fpanes", NULL, 0);
        XtVaSetValues(paned, XmNorientation, XmHORIZONTAL, XmNspacing, 10,
                      XmNbackground, ctx->px_bg, NULL);
        lp = XmCreateRowColumn(paned, "incp", NULL, 0);
        rp = XmCreateRowColumn(paned, "filp", NULL, 0);
        oyk_sysv_filemgr_pane_title(&app->fm.inc, "Incoming:", lt, sizeof(lt));
        oyk_sysv_filemgr_pane_title(&app->fm.files, "Files:", rt, sizeof(rt));
        set_label_text(XtVaCreateManagedWidget("ll", xmLabelWidgetClass, lp, XmNbackground, ctx->px_bg, NULL), lt);
        set_label_text(XtVaCreateManagedWidget("rl", xmLabelWidgetClass, rp, XmNbackground, ctx->px_bg, NULL), rt);
        for (i = 0; i < app->fm.inc.count && i < 8; ++i) {
            sprintf(lb[i], "%-16s %s", app->fm.inc.entries[i].name, app->fm.inc.entries[i].is_dir ? "<DIR>" : "FILE");
            lrows[i] = lb[i];
        }
        for (i = 0; i < app->fm.files.count && i < 8; ++i) {
            sprintf(rb[i], "%-16s %s", app->fm.files.entries[i].name, app->fm.files.entries[i].is_dir ? "<DIR>" : "FILE");
            rrows[i] = rb[i];
        }
        llist = XmCreateScrolledList(lp, "ill", NULL, 0);
        rlist = XmCreateScrolledList(rp, "rll", NULL, 0);
        XtVaSetValues(llist, XmNvisibleItemCount, 5, XmNwidth, 220, XmNbackground, ctx->px_panel_bg, NULL);
        XtVaSetValues(rlist, XmNvisibleItemCount, 5, XmNwidth, 220, XmNbackground, ctx->px_panel_bg, NULL);
        list_set_rows(llist, lrows, app->fm.inc.count);
        list_set_rows(rlist, rrows, app->fm.files.count);
        XtManageChild(llist);
        XtManageChild(rlist);
        XtManageChild(lp);
        XtManageChild(rp);
        XtManageChild(paned);
    } else if (app->screen == OYK_SYSV_SCR_ABOUT) {
        Widget row;
        Widget logo_da;
        Widget info_lbl;
        row = XmCreateRowColumn(box, "abrow", NULL, 0);
        XtVaSetValues(row, XmNorientation, XmHORIZONTAL, XmNspacing, 16, XmNbackground, ctx->px_bg, NULL);
        logo_da = XtVaCreateManagedWidget("logo", xmDrawingAreaWidgetClass, row,
                                          XmNwidth, 76, XmNheight, 76,
                                          XmNbackground, ctx->px_bg, NULL);
        XtAddCallback(logo_da, XmNexposeCallback, on_about_expose, ctx);
        sprintf(buf, "%s\n\n%s", oyk_sysv_about_summary(), app->node.platform_label);
        info_lbl = XtVaCreateManagedWidget("abtxt", xmLabelWidgetClass, row,
                                           XmNalignment, XmALIGNMENT_BEGINNING,
                                           XmNbackground, ctx->px_bg,
                                           XmNforeground, ctx->px_fg, NULL);
        set_label_text(info_lbl, buf);
        XtManageChild(row);
    }

    XtManageChild(box);
    btn_row = XmCreateRowColumn(dlg, "btn_row", NULL, 0);
    XtVaSetValues(btn_row, XmNorientation, XmHORIZONTAL, XmNspacing, 8,
                  XmNbackground, ctx->px_bg,
                  XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, box,
                  XmNtopOffset, 10,
                  XmNrightAttachment, XmATTACH_FORM,
                  XmNbottomAttachment, XmATTACH_FORM, NULL);
    if (app->screen == OYK_SYSV_SCR_EXIT_PEERS) {
        static const char * const ex_btns[4] = { "Save & Exit", "Discard", "Review Peers", "Cancel" };
        for (i = 0; i < 4; ++i) {
            b = XmCreatePushButton(btn_row, "exb", NULL, 0);
            set_label_text(b, ex_btns[i]);
            XtAddCallback(b, XmNactivateCallback, on_close_modal_cb, ctx);
            XtManageChild(b);
        }
    } else {
        b = XmCreatePushButton(btn_row, "okb", NULL, 0);
        set_label_text(b, (app->screen == OYK_SYSV_SCR_ADD_IFACE && app->wiz.step < 3) ? "Next" : "OK");
        XtAddCallback(b, XmNactivateCallback, on_close_modal_cb, ctx);
        XtManageChild(b);
        b = XmCreatePushButton(btn_row, "clb", NULL, 0);
        set_label_text(b, "Close");
        XtAddCallback(b, XmNactivateCallback, on_close_modal_cb, ctx);
        XtManageChild(b);
    }
    XtManageChild(btn_row);
    XtManageChild(dlg);
    XtRealizeWidget(ctx->modal_shell);
    {
        Dimension dw, dh;
        XtVaGetValues(ctx->modal_shell, XmNwidth, &dw, XmNheight, &dh, NULL);
        XtVaSetValues(ctx->modal_shell,
                      XmNx, (Position)(mx + ((int)mw - (int)dw) / 2),
                      XmNy, (Position)(my + ((int)mh - (int)dh) / 2),
                      NULL);
    }
    XtPopup(ctx->modal_shell, XtGrabNone);
}

static void on_menu_item_cb(Widget w, XtPointer client, XtPointer call) {
    int action;
    (void)w; (void)call;
    if (g_mui.modal_shell != NULL) return;
    action = (int)(long)client;
    oyk_sysv_app_exec_menu_action(g_mui.app, action);
    if (!g_mui.app->running) {
        g_mui.quit = 1;
        return;
    }
    refresh_main_console(&g_mui);
    open_modal_for_screen(&g_mui);
}

static void flush_events_and_wait_ack(MotifUiCtx *ctx, const char *screen_name) {
    XEvent ev;
    int loops;
    for (loops = 0; loops < 40; ++loops) {
        while (XtAppPending(ctx->xt_app)) {
            XtAppNextEvent(ctx->xt_app, &ev);
            XtDispatchEvent(&ev);
        }
        XSync(ctx->dpy, False);
    }
    printf("OYK_CAPTURE_READY %s\n", screen_name);
    fflush(stdout);
    {
        char ack[32];
        if (!fgets(ack, sizeof(ack), stdin)) {
            /* Ignore EOF if non-interactive */
        }
    }
}

static void on_test_timer(XtPointer client, XtIntervalId *id) {
    static const char * const screens[23] = {
        "main", "menu", "settings", "settings_bbs", "settings_chat",
        "settings_ifaces", "peers", "settings_rig", "settings_printer",
        "add_iface_1", "add_iface_2", "add_iface_3", "add_iface_4",
        "edit_iface", "edit_peer", "terminate", "ban", "exit",
        "theme", "terminal", "terminal_help", "files", "about"
    };
    MotifUiCtx *ctx;
    (void)id;
    ctx = (MotifUiCtx *)client;
    if (ctx->test_step >= 23) {
        close_modal_dialog(ctx);
        ctx->quit = 1;
        return;
    }
    oyk_sysv_tui_setup_screen(ctx->app, screens[ctx->test_step]);
    if (strcmp(screens[ctx->test_step], "theme") == 0) {
        oyk_sysv_theme_select(&ctx->app->theme_ctrl, "dark");
    } else {
        oyk_sysv_theme_select(&ctx->app->theme_ctrl, "native");
    }
    refresh_main_console(ctx);
    open_modal_for_screen(ctx);
    flush_events_and_wait_ack(ctx, screens[ctx->test_step]);
    ++ctx->test_step;
    XtAppAddTimeOut(ctx->xt_app, 100UL, on_test_timer, ctx);
}

int oyk_sysv_motif_run(OykSysvApp *app, int argc, char **argv) {
    Widget hdr_row;
    Widget top_row;
    const char *env_test;
    int i;
    if (!app) return 1;
    memset(&g_mui, 0, sizeof(g_mui));
    g_mui.app = app;

    g_mui.top_shell = XtVaAppInitialize(&g_mui.xt_app, "OykNodeMotif", NULL, 0,
                                        &argc, argv, NULL,
                                        XmNtitle, "OYK Amateur Radio Packet Node",
                                        XmNwidth, 760,
                                        XmNheight, 520,
                                        XmNx, 32,
                                        XmNy, 32,
                                        NULL);
    g_mui.dpy = XtDisplay(g_mui.top_shell);

    g_mui.icon_pixmap = XCreateBitmapFromData(g_mui.dpy, DefaultRootWindow(g_mui.dpy),
                                              (char *)kOykSysvLogoLine32Bits, 32, 32);
    g_mui.icon_mask   = XCreateBitmapFromData(g_mui.dpy, DefaultRootWindow(g_mui.dpy),
                                              (char *)kOykSysvLogoLine32Mask, 32, 32);
    XtVaSetValues(g_mui.top_shell,
                  XmNiconPixmap, g_mui.icon_pixmap,
                  XmNiconMask, g_mui.icon_mask,
                  NULL);

    g_mui.main_win = XmCreateMainWindow(g_mui.top_shell, "main_win", NULL, 0);
    g_mui.menu_bar = XmCreateMenuBar(g_mui.main_win, "menu_bar", NULL, 0);
    g_mui.node_menu = XmCreatePulldownMenu(g_mui.menu_bar, "node_menu", NULL, 0);
    for (i = 0; i < oyk_sysv_node_menu_count(); ++i) {
        const OykSysvMenuItem *it;
        it = oyk_sysv_node_menu_item(i);
        if (it->is_separator) {
            XtManageChild(XmCreateSeparator(g_mui.node_menu, "msep", NULL, 0));
        } else {
            Widget mi;
            mi = XmCreatePushButton(g_mui.node_menu, "mi", NULL, 0);
            set_label_text(mi, it->label);
            XtAddCallback(mi, XmNactivateCallback, on_menu_item_cb, (XtPointer)(long)it->action);
            XtManageChild(mi);
        }
    }
    g_mui.node_cascade = XtVaCreateManagedWidget("Node", xmCascadeButtonWidgetClass,
                                                 g_mui.menu_bar,
                                                 XmNmnemonic, 'N',
                                                 XmNsubMenuId, g_mui.node_menu, NULL);
    XtManageChild(g_mui.menu_bar);

    g_mui.main_form = XmCreateRowColumn(g_mui.main_win, "work", NULL, 0);
    XtVaSetValues(g_mui.main_form, XmNorientation, XmVERTICAL, XmNspacing, 6,
                  XmNmarginWidth, 8, XmNmarginHeight, 8, NULL);

    hdr_row = XmCreateForm(g_mui.main_form, "hdr_row", NULL, 0);
    g_mui.header_lbl = XtVaCreateManagedWidget("hdr_lbl", xmLabelWidgetClass, hdr_row,
                                               XmNtopAttachment, XmATTACH_FORM,
                                               XmNbottomAttachment, XmATTACH_FORM,
                                               XmNleftAttachment, XmATTACH_FORM,
                                               XmNalignment, XmALIGNMENT_BEGINNING, NULL);
    g_mui.pill_lbl = XtVaCreateManagedWidget("pill_lbl", xmLabelWidgetClass, hdr_row,
                                             XmNtopAttachment, XmATTACH_FORM,
                                             XmNbottomAttachment, XmATTACH_FORM,
                                             XmNrightAttachment, XmATTACH_FORM,
                                             XmNmarginWidth, 8, NULL);
    XtVaSetValues(g_mui.header_lbl, XmNrightAttachment, XmATTACH_WIDGET,
                  XmNrightWidget, g_mui.pill_lbl, NULL);
    XtManageChild(hdr_row);

    top_row = XmCreateRowColumn(g_mui.main_form, "top_row", NULL, 0);
    XtVaSetValues(top_row, XmNorientation, XmHORIZONTAL, XmNspacing, 8, NULL);
    {
        Widget lcol, rcol;
        lcol = XmCreateRowColumn(top_row, "icol", NULL, 0);
        rcol = XmCreateRowColumn(top_row, "hcol", NULL, 0);
        set_label_text(XtVaCreateManagedWidget("ilbl", xmLabelWidgetClass, lcol, NULL), "Interfaces");
        set_label_text(XtVaCreateManagedWidget("hlbl", xmLabelWidgetClass, rcol, NULL), "Last 5 New Callsigns Heard");
        g_mui.iface_list = XmCreateScrolledList(lcol, "iflist", NULL, 0);
        g_mui.heard_list = XmCreateScrolledList(rcol, "hdlist", NULL, 0);
        XtVaSetValues(g_mui.iface_list, XmNvisibleItemCount, 5, XmNwidth, 400, NULL);
        XtVaSetValues(g_mui.heard_list, XmNvisibleItemCount, 5, XmNwidth, 320, NULL);
        XtManageChild(g_mui.iface_list);
        XtManageChild(g_mui.heard_list);
        XtManageChild(lcol);
        XtManageChild(rcol);
    }
    XtManageChild(top_row);

    set_label_text(XtVaCreateManagedWidget("clbl", xmLabelWidgetClass, g_mui.main_form, NULL), "Active Connections");
    g_mui.conn_list = XmCreateScrolledList(g_mui.main_form, "cnlist", NULL, 0);
    XtVaSetValues(g_mui.conn_list, XmNvisibleItemCount, 4, XmNwidth, 720, NULL);
    XtManageChild(g_mui.conn_list);

    set_label_text(XtVaCreateManagedWidget("llbl", xmLabelWidgetClass, g_mui.main_form, NULL), "Node Activity Log");
    g_mui.log_list = XmCreateScrolledList(g_mui.main_form, "lglist", NULL, 0);
    XtVaSetValues(g_mui.log_list, XmNvisibleItemCount, 5, XmNwidth, 720, NULL);
    XtManageChild(g_mui.log_list);

    g_mui.footer_row = XmCreateRowColumn(g_mui.main_form, "footer", NULL, 0);
    XtVaSetValues(g_mui.footer_row, XmNorientation, XmHORIZONTAL, XmNspacing, 6, NULL);
    {
        static const struct { const char *lbl; int act; } fbtns[4] = {
            { "Terminal", OYK_SYSV_ACT_LOCAL_TERM },
            { "Files",    OYK_SYSV_ACT_FILES },
            { "Settings", OYK_SYSV_ACT_SETTINGS },
            { "About",    OYK_SYSV_ACT_ABOUT }
        };
        for (i = 0; i < 4; ++i) {
            Widget fb;
            fb = XmCreatePushButton(g_mui.footer_row, "fb", NULL, 0);
            set_label_text(fb, fbtns[i].lbl);
            XtAddCallback(fb, XmNactivateCallback, on_menu_item_cb, (XtPointer)(long)fbtns[i].act);
            XtManageChild(fb);
        }
    }
    XtManageChild(g_mui.footer_row);

    XtManageChild(g_mui.main_form);
    XmMainWindowSetAreas(g_mui.main_win, g_mui.menu_bar, NULL, NULL, NULL, g_mui.main_form);
    XtManageChild(g_mui.main_win);
    XtRealizeWidget(g_mui.top_shell);
    refresh_main_console(&g_mui);

    env_test = getenv("OYK_UI_TEST_MODE");
    if (env_test && strcmp(env_test, "1") == 0) {
        g_mui.test_step = 0;
        XtAppAddTimeOut(g_mui.xt_app, 300UL, on_test_timer, &g_mui);
    }

    while (!g_mui.quit) {
        XEvent ev;
        XtAppNextEvent(g_mui.xt_app, &ev);
        XtDispatchEvent(&ev);
    }
    return 0;
}

int oyk_sysv_motif_test_mode_supported(void) {
    return 1;
}

#else

int oyk_sysv_motif_run(OykSysvApp *app, int argc, char **argv) {
    (void)app;
    (void)argc;
    (void)argv;
    return 1;
}

int oyk_sysv_motif_test_mode_supported(void) {
    return 0;
}

#endif /* OYK_HAS_MOTIF */
