/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "sysv_tui.h"
#include "sysv_logo_data.h"

#include <stdio.h>
#include <string.h>

#ifdef OYK_SYSV_SVR32
static const char *s_svr32_strstr(const char *hay, const char *needle) {
    size_t nlen;
    if (!hay || !needle) return NULL;
    if (!needle[0]) return hay;
    nlen = strlen(needle);
    while (*hay) {
        if (*hay == *needle && strncmp(hay, needle, nlen) == 0) return hay;
        ++hay;
    }
    return NULL;
}
#define strstr s_svr32_strstr
#endif

typedef struct {
    const char *label;
    int         enabled;
} TuiBtn;

static int t_clamp_rows(int rows) {
    if (rows < 24) return 24;
    if (rows > OYK_SYSV_TUI_MAX_ROWS) return OYK_SYSV_TUI_MAX_ROWS;
    return rows;
}

static int t_clamp_cols(int cols) {
    if (cols < 80) return 80;
    if (cols > OYK_SYSV_TUI_MAX_COLS) return OYK_SYSV_TUI_MAX_COLS;
    return cols;
}

void oyk_sysv_canvas_init(OykSysvCanvas *cv, int rows, int cols) {
    if (!cv) return;
    cv->rows = t_clamp_rows(rows);
    cv->cols = t_clamp_cols(cols);
    oyk_sysv_canvas_clear(cv, OYK_SYSV_ST_BG);
}

void oyk_sysv_canvas_clear(OykSysvCanvas *cv, int style) {
    int r;
    int c;
    if (!cv) return;
    for (r = 0; r < cv->rows; ++r) {
        for (c = 0; c < cv->cols; ++c) {
            cv->chars[r][c] = ' ';
            cv->styles[r][c] = (sysv_u8)style;
        }
    }
}

void oyk_sysv_canvas_put_text(OykSysvCanvas *cv, int row, int col, int max_w,
                              int style, const char *text) {
    int i;
    int tlen;
    if (!cv || row < 0 || row >= cv->rows || max_w <= 0) return;
    tlen = text ? (int)strlen(text) : 0;
    for (i = 0; i < max_w; ++i) {
        int cc;
        cc = col + i;
        if (cc < 0) continue;
        if (cc >= cv->cols) break;
        cv->chars[row][cc] = (i < tlen) ? text[i] : ' ';
        cv->styles[row][cc] = (sysv_u8)style;
    }
}

void oyk_sysv_canvas_put_box(OykSysvCanvas *cv, int top, int left, int h, int w,
                             int border_style, int fill_style, int fill_interior) {
    int r;
    int c;
    if (!cv || h < 2 || w < 2) return;
    for (r = 0; r < h; ++r) {
        int sr;
        sr = top + r;
        if (sr < 0 || sr >= cv->rows) continue;
        for (c = 0; c < w; ++c) {
            int sc;
            int is_edge;
            char ch;
            sc = left + c;
            if (sc < 0 || sc >= cv->cols) continue;
            is_edge = (r == 0 || r == h - 1 || c == 0 || c == w - 1);
            if (!is_edge) {
                if (fill_interior) {
                    cv->chars[sr][sc] = ' ';
                    cv->styles[sr][sc] = (sysv_u8)fill_style;
                }
                continue;
            }
            if (r == 0 && c == 0) ch = '+';
            else if (r == 0 && c == w - 1) ch = '+';
            else if (r == h - 1 && c == 0) ch = '+';
            else if (r == h - 1 && c == w - 1) ch = '+';
            else if (r == 0 || r == h - 1) ch = '-';
            else ch = '|';
            cv->chars[sr][sc] = ch;
            cv->styles[sr][sc] = (sysv_u8)border_style;
        }
    }
}

void oyk_sysv_canvas_put_hdiv(OykSysvCanvas *cv, int row, int left, int w, int style) {
    int c;
    if (!cv || row < 0 || row >= cv->rows || w < 2) return;
    for (c = 0; c < w; ++c) {
        int sc;
        sc = left + c;
        if (sc < 0 || sc >= cv->cols) continue;
        cv->chars[row][sc] = (c == 0 || c == w - 1) ? '+' : '-';
        cv->styles[row][sc] = (sysv_u8)style;
    }
}

void oyk_sysv_canvas_put_shadow(OykSysvCanvas *cv, int top, int left, int h, int w) {
    int r;
    int c;
    if (!cv) return;
    for (r = top + 1; r <= top + h; ++r) {
        int sc;
        if (r < 0 || r >= cv->rows) continue;
        for (c = 0; c < 2; ++c) {
            sc = left + w + c;
            if (sc >= 0 && sc < cv->cols) cv->styles[r][sc] = OYK_SYSV_ST_DIM;
        }
    }
    if (top + h >= 0 && top + h < cv->rows) {
        for (c = left + 2; c < left + w + 2; ++c) {
            if (c >= 0 && c < cv->cols) cv->styles[top + h][c] = OYK_SYSV_ST_DIM;
        }
    }
}

void oyk_sysv_canvas_row_text(const OykSysvCanvas *cv, int row, char *out, size_t cap) {
    int c;
    size_t n;
    if (!out || cap == 0U) return;
    out[0] = '\0';
    if (!cv || row < 0 || row >= cv->rows) return;
    n = (size_t)cv->cols;
    if (n >= cap) n = cap - 1U;
    for (c = 0; c < (int)n; ++c) out[c] = cv->chars[row][c];
    out[n] = '\0';
}

int oyk_sysv_canvas_find_text(const OykSysvCanvas *cv, const char *needle) {
    int r;
    char line[OYK_SYSV_TUI_MAX_COLS + 4];
    if (!cv || !needle || !needle[0]) return 0;
    for (r = 0; r < cv->rows; ++r) {
        oyk_sysv_canvas_row_text(cv, r, line, sizeof(line));
        if (strstr(line, needle) != NULL) return 1;
    }
    return 0;
}

static void center_modal(int srows, int scols, int ph, int pw, OykSysvRect *out) {
    int mh;
    int mw;
    mh = (srows > 2) ? (srows - 2) : srows;
    mw = (scols > 4) ? (scols - 4) : scols;
    out->h = (ph <= mh) ? ph : mh;
    out->w = (pw <= mw) ? pw : mw;
    if (out->h < 6) out->h = 6;
    if (out->w < 20) out->w = 20;
    out->top = (srows - out->h) / 2;
    out->left = (scols - out->w) / 2;
    if (out->top < 1) out->top = 1;
    if (out->left < 1) out->left = 1;
}

int oyk_sysv_tui_close_btn_col(const OykSysvRect *r) {
    if (!r || r->w < 12) return -1;
    return r->left + r->w - 5;
}

static void draw_modal(OykSysvCanvas *cv, const OykSysvRect *r, const char *title, int with_close) {
    int close_c;
    oyk_sysv_canvas_put_box(cv, r->top, r->left, r->h, r->w,
                            OYK_SYSV_ST_DLG_BORDER, OYK_SYSV_ST_DLG_FILL, 1);
    close_c = with_close ? oyk_sysv_tui_close_btn_col(r) : -1;
    if (title && title[0]) {
        char tb[96];
        int max_t;
        int tl;
        max_t = (close_c >= 0) ? (close_c - (r->left + 2) - 1) : (r->w - 4);
        tl = (int)strlen(title);
        if (tl > max_t - 2) tl = max_t - 2;
        if (tl > 88) tl = 88;
        if (tl > 0) {
            tb[0] = ' ';
            memcpy(tb + 1, title, (size_t)tl);
            tb[1 + tl] = ' ';
            tb[2 + tl] = '\0';
            oyk_sysv_canvas_put_text(cv, r->top, r->left + 2, tl + 2, OYK_SYSV_ST_DLG_BORDER, tb);
        }
    }
    if (close_c >= 0) {
        oyk_sysv_canvas_put_text(cv, r->top, close_c, 3, OYK_SYSV_ST_CLOSE_BTN, "[X]");
    }
    oyk_sysv_canvas_put_shadow(cv, r->top, r->left, r->h, r->w);
}

static void draw_btn_row(OykSysvCanvas *cv, int row, int col, int max_w,
                         const TuiBtn *btns, int count, int focused_idx) {
    int cur;
    int i;
    cur = col;
    for (i = 0; i < count; ++i) {
        char lbl[32];
        int bw;
        int st;
        sprintf(lbl, " [ %s ] ", btns[i].label);
        bw = (int)strlen(lbl);
        if (cur - col + bw > max_w) break;
        if (!btns[i].enabled) st = OYK_SYSV_ST_DIM;
        else if (i == focused_idx) st = OYK_SYSV_ST_DLG_SEL;
        else st = OYK_SYSV_ST_DLG_BAR;
        oyk_sysv_canvas_put_text(cv, row, cur, bw, st, lbl);
        cur += bw + 2;
    }
}

static void draw_main_console(OykSysvCanvas *cv, const OykSysvApp *app) {
    const OykSysvNode *n;
    char call_s[16];
    char alias_s[16];
    char banner[140];
    const char *pill;
    int plen;
    int top_h;
    int conn_h;
    int left_w;
    int right_w;
    int conn_top;
    int log_top;
    int log_h;
    int i;
    int r;

    n = &app->node;
    /* Row 0: Menu bar */
    oyk_sysv_canvas_put_text(cv, 0, 0, cv->cols, OYK_SYSV_ST_MENU_BAR, "");
    oyk_sysv_canvas_put_text(cv, 0, 1, 6, OYK_SYSV_ST_MENU_SEL, " Node ");
    oyk_sysv_canvas_put_text(cv, 0, cv->cols - 27, 26, OYK_SYSV_ST_MENU_BAR, "Tab/F9:Term  Ctrl-N:Menu ");

    /* Row 1: Status banner + [INCOMING: ALLOW/DENY] */
    oyk_sysv_format_callsign(&n->cfg.callsign, call_s, sizeof(call_s));
    oyk_sysv_format_callsign(&n->cfg.alias, alias_s, sizeof(alias_s));
    pill = oyk_sysv_format_incoming_pill(n->cfg.accept_incoming, 0);
    plen = (int)strlen(pill) + 2;
    oyk_sysv_format_console_header(call_s, alias_s, n->uptime_sec,
                                   oyk_sysv_node_active_conn_count(n),
                                   n->cfg.accept_incoming, 1, banner, sizeof(banner));
    oyk_sysv_canvas_put_text(cv, 1, 0, cv->cols - plen, OYK_SYSV_ST_HEADER, banner);
    {
        char pbuf[24];
        sprintf(pbuf, " %s ", pill);
        oyk_sysv_canvas_put_text(cv, 1, cv->cols - (int)strlen(pbuf), (int)strlen(pbuf),
                                 n->cfg.accept_incoming ? OYK_SYSV_ST_ALLOW : OYK_SYSV_ST_DENY, pbuf);
    }

    top_h = (cv->rows >= 30) ? 8 : 7;
    conn_h = (cv->rows >= 30) ? 7 : 6;
    left_w = (cv->cols * 56) / 100;
    right_w = cv->cols - left_w + 1;
    conn_top = 2 + top_h;
    log_top = conn_top + conn_h;
    log_h = (cv->rows - 1) - log_top;

    /* Interfaces pane */
    oyk_sysv_canvas_put_box(cv, 2, 0, top_h, left_w, OYK_SYSV_ST_BORDER, OYK_SYSV_ST_BG, 1);
    oyk_sysv_canvas_put_text(cv, 2, 2, 12, OYK_SYSV_ST_TITLE, " Interfaces ");
    oyk_sysv_canvas_put_text(cv, 3, 1, left_w - 2, OYK_SYSV_ST_LIST_HDR,
                             "Port     Type        Conn Stat RX/TX");
    for (i = 0; i < n->cfg.iface_count && i < top_h - 3; ++i) {
        char row[80];
        sprintf(row, "%-8.8s %-11.11s %4d %-4.4s %lu/%lu",
                n->cfg.ifaces[i].id, n->cfg.ifaces[i].type,
                n->cfg.iface_count > 0 ? 1 : 0,
                n->iface_rt[i].running ? "UP" : "DOWN",
                (unsigned long)n->iface_rt[i].rx_frames,
                (unsigned long)n->iface_rt[i].tx_frames);
        oyk_sysv_canvas_put_text(cv, 4 + i, 1, left_w - 2, OYK_SYSV_ST_NORMAL, row);
    }

    /* Last 5 New Callsigns Heard pane */
    oyk_sysv_canvas_put_box(cv, 2, left_w - 1, top_h, right_w, OYK_SYSV_ST_BORDER, OYK_SYSV_ST_BG, 1);
    oyk_sysv_canvas_put_text(cv, 2, left_w + 1, 29, OYK_SYSV_ST_TITLE, " Last 5 New Callsigns Heard ");
    oyk_sysv_canvas_put_text(cv, 3, left_w, right_w - 2, OYK_SYSV_ST_LIST_HDR,
                             "# Callsign  Port   Heard");
    for (i = 0; i < n->heard_count && i < top_h - 3; ++i) {
        char row[64];
        sprintf(row, "%d %-9.9s %-6.6s %s", i + 1,
                n->heard[i].callsign, n->heard[i].iface_id, n->heard[i].time_hms);
        oyk_sysv_canvas_put_text(cv, 4 + i, left_w, right_w - 2, OYK_SYSV_ST_NORMAL, row);
    }

    /* Active Connections pane */
    oyk_sysv_canvas_put_box(cv, conn_top, 0, conn_h, cv->cols, OYK_SYSV_ST_BORDER, OYK_SYSV_ST_BG, 1);
    oyk_sysv_canvas_put_text(cv, conn_top, 2, 20, OYK_SYSV_ST_TITLE, " Active Connections ");
    oyk_sysv_canvas_put_text(cv, conn_top + 1, 1, cv->cols - 2, OYK_SYSV_ST_LIST_HDR,
                             "ID   Callsign    Port       Dir  State      Since     Role");
    r = 0;
    for (i = 0; i < OYK_SYSV_MAX_SESSIONS && r < conn_h - 3; ++i) {
        if (n->sessions[i].state == 2) {
            char row[100];
            sprintf(row, "%-4lu %-11.11s %-10.10s %-4.4s %-10.10s %-9.9s %s",
                    (unsigned long)n->sessions[i].id,
                    n->sessions[i].callsign, n->sessions[i].port_id,
                    n->sessions[i].dir, "CONNECTED",
                    n->sessions[i].since_hms, n->sessions[i].role);
            oyk_sysv_canvas_put_text(cv, conn_top + 2 + r, 1, cv->cols - 2, OYK_SYSV_ST_NORMAL, row);
            ++r;
        }
    }

    /* Node Activity Log pane */
    oyk_sysv_canvas_put_box(cv, log_top, 0, log_h, cv->cols, OYK_SYSV_ST_BORDER, OYK_SYSV_ST_BG, 1);
    oyk_sysv_canvas_put_text(cv, log_top, 2, 19, OYK_SYSV_ST_TITLE, " Node Activity Log ");
    for (i = 0; i < n->log_count && i < log_h - 2; ++i) {
        oyk_sysv_canvas_put_text(cv, log_top + 1 + i, 1, cv->cols - 2,
                                 OYK_SYSV_ST_NORMAL, n->logs[i]);
    }

    /* Bottom status bar */
    oyk_sysv_canvas_put_text(cv, cv->rows - 1, 0, cv->cols, OYK_SYSV_ST_STATUS,
                             "Ctrl-N/F10:Node Menu   Tab/F9:Local Terminal   Esc:Close Dialog");
}

static void draw_dropdown_menu(OykSysvCanvas *cv, const OykSysvApp *app) {
    OykSysvRect r;
    int i;
    int row;
    r.top = 1;
    r.left = 1;
    r.h = 13;
    r.w = 34;
    /* Dropdown Node Menu has NO [X] close button */
    draw_modal(cv, &r, NULL, 0);
    row = r.top + 1;
    for (i = 0; i < oyk_sysv_node_menu_count() && row < r.top + r.h - 1; ++i) {
        const OykSysvMenuItem *it;
        it = oyk_sysv_node_menu_item(i);
        if (it->is_separator) {
            oyk_sysv_canvas_put_hdiv(cv, row, r.left, r.w, OYK_SYSV_ST_DLG_BORDER);
        } else {
            char line[36];
            int st;
            if (it->shortcut_key) sprintf(line, "[%c] %-26.26s", it->shortcut_key, it->label);
            else sprintf(line, "    %-26.26s", it->label);
            st = (i == app->menu_sel) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_FILL;
            oyk_sysv_canvas_put_text(cv, row, r.left + 1, r.w - 2, st, line);
        }
        ++row;
    }
}

static void draw_settings_modal(OykSysvCanvas *cv, const OykSysvApp *app) {
    OykSysvRect r;
    const OykSysvConfig *cfg;
    char call_s[16];
    char alias_s[16];
    char buf[96];
    int c;
    int i;
    center_modal(cv->rows, cv->cols, 20, 76, &r);
    draw_modal(cv, &r, "Node Settings", 1);
    cfg = &app->node.cfg;

    /* 8 canonical tabs inside row r.top + 1 */
    c = r.left + 2;
    for (i = 0; i < 8; ++i) {
        const char *tl;
        char tok[16];
        int st;
        tl = oyk_sysv_settings_tab_title(i);
        if (i == app->settings_tab) sprintf(tok, "[%s]", tl);
        else sprintf(tok, " %s ", tl);
        st = (i == app->settings_tab) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_BAR;
        oyk_sysv_canvas_put_text(cv, r.top + 1, c, (int)strlen(tok), st, tok);
        c += (int)strlen(tok) + 1;
    }
    oyk_sysv_canvas_put_hdiv(cv, r.top + 2, r.left, r.w, OYK_SYSV_ST_DLG_BORDER);

    if (app->settings_tab == 0) {
        oyk_sysv_format_callsign(&cfg->callsign, call_s, sizeof(call_s));
        oyk_sysv_format_callsign(&cfg->alias, alias_s, sizeof(alias_s));
        sprintf(buf, "Callsign: %-12s  Alias: %s", call_s, alias_s);
        oyk_sysv_canvas_put_text(cv, r.top + 4, r.left + 3, r.w - 6,
                                 (app->settings_row == 0) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_FILL, buf);
        sprintf(buf, "Accept Incoming: [%c]    Platform: %s",
                cfg->accept_incoming ? 'X' : ' ', app->node.platform_label);
        oyk_sysv_canvas_put_text(cv, r.top + 6, r.left + 3, r.w - 6,
                                 (app->settings_row == 1) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_FILL, buf);
    } else if (app->settings_tab == 1) {
        sprintf(buf, "BBS Enabled: [%c]  SSID: %u  Files Area: [%c]",
                cfg->bbs_enabled ? 'X' : ' ', (unsigned int)cfg->bbs_ssid,
                cfg->bbs_files_enabled ? 'X' : ' ');
        oyk_sysv_canvas_put_text(cv, r.top + 4, r.left + 3, r.w - 6,
                                 (app->settings_row == 0) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_FILL, buf);
        sprintf(buf, "Mail Path: %.28s  Files Path: %.28s", cfg->bbs_mail_path, cfg->bbs_files_path);
        oyk_sysv_canvas_put_text(cv, r.top + 6, r.left + 3, r.w - 6,
                                 (app->settings_row == 1) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_FILL, buf);
    } else if (app->settings_tab == 2) {
        sprintf(buf, "CHAT Enabled: [%c]  SSID: %u  Topic: %s",
                cfg->chat_enabled ? 'X' : ' ', (unsigned int)cfg->chat_ssid, cfg->chat_topic);
        oyk_sysv_canvas_put_text(cv, r.top + 4, r.left + 3, r.w - 6,
                                 (app->settings_row == 0) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_FILL, buf);
    } else if (app->settings_tab == 3) {
        oyk_sysv_canvas_put_text(cv, r.top + 4, r.left + 3, r.w - 6, OYK_SYSV_ST_LIST_HDR,
                                 "ID       Type         Endpoint              In/Out");
        for (i = 0; i < cfg->iface_count && i < 6; ++i) {
            sprintf(buf, "%-8s %-12s %-20s %c/%c",
                    cfg->ifaces[i].id, cfg->ifaces[i].type,
                    cfg->ifaces[i].host,
                    cfg->ifaces[i].allow_inbound ? 'Y' : 'N',
                    cfg->ifaces[i].allow_outbound ? 'Y' : 'N');
            oyk_sysv_canvas_put_text(cv, r.top + 5 + i, r.left + 3, r.w - 6,
                                     (i == app->iface_sel) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_FILL,
                                     buf);
        }
    } else if (app->settings_tab == 4) {
        oyk_sysv_canvas_put_text(cv, r.top + 4, r.left + 3, r.w - 6, OYK_SYSV_ST_LIST_HDR,
                                 "ID       Callsign   Interface  Endpoint          Auto");
        for (i = 0; i < cfg->peer_count && i < 6; ++i) {
            sprintf(buf, "%-8s %-10s %-10s %-17s %s",
                    cfg->peers[i].id, cfg->peers[i].callsign,
                    cfg->peers[i].interface_id, cfg->peers[i].host,
                    cfg->peers[i].auto_learned ? "YES" : "NO");
            oyk_sysv_canvas_put_text(cv, r.top + 5 + i, r.left + 3, r.w - 6,
                                     (i == app->peer_sel) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_FILL,
                                     buf);
        }
    } else if (app->settings_tab == 5) {
        sprintf(buf, "Rig Control: [%c]  Protocol: %s",
                cfg->rig_enabled ? 'X' : ' ', cfg->rig_protocol);
        oyk_sysv_canvas_put_text(cv, r.top + 4, r.left + 3, r.w - 6,
                                 (app->settings_row == 0) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_FILL, buf);
        sprintf(buf, "Host: %s:%u  Poll: %ds", cfg->rig_host,
                (unsigned int)cfg->rig_port, cfg->rig_poll_sec);
        oyk_sysv_canvas_put_text(cv, r.top + 6, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_FILL, buf);
    } else if (app->settings_tab == 6) {
        sprintf(buf, "Printer Log: [%c]  Device: %s",
                cfg->printer_enabled ? 'X' : ' ', cfg->printer_device);
        oyk_sysv_canvas_put_text(cv, r.top + 4, r.left + 3, r.w - 6,
                                 (app->settings_row == 0) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_FILL, buf);
        sprintf(buf, "Protocol: %s  Baud: %lu  Condensed: [%c]",
                cfg->printer_protocol, (unsigned long)cfg->printer_baud,
                cfg->printer_condensed ? 'X' : ' ');
        oyk_sysv_canvas_put_text(cv, r.top + 6, r.left + 3, r.w - 6,
                                 (app->settings_row == 1) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_FILL, buf);
    } else {
        OykSysvThemeConfig pal;
        oyk_sysv_theme_resolve(&app->theme_ctrl, &pal);
        sprintf(buf, "Active Theme: < %s >   (Space/Enter to cycle preset)", pal.name);
        oyk_sysv_canvas_put_text(cv, r.top + 4, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_SEL, buf);
        oyk_sysv_canvas_put_text(cv, r.top + 6, r.left + 3, r.w - 6, OYK_SYSV_ST_LIST_HDR,
                                 "Role          Key         Color    Preview");
        for (i = 0; i < 8 && i < OYK_SYSV_THEME_COLOR_COUNT; ++i) {
            sprintf(buf, "%-13s %-11s #%06lX",
                    oyk_sysv_theme_role_name(i),
                    oyk_sysv_theme_role_key(i),
                    (unsigned long)(pal.colors[i] & 0xFFFFFFUL));
            oyk_sysv_canvas_put_text(cv, r.top + 7 + i, r.left + 3, r.w - 14, OYK_SYSV_ST_DLG_FILL, buf);
            oyk_sysv_canvas_put_text(cv, r.top + 7 + i, r.left + r.w - 11, 6,
                                     (i == 0) ? OYK_SYSV_ST_BG : ((i % 2) ? OYK_SYSV_ST_HEADER : OYK_SYSV_ST_ALLOW),
                                     " #### ");
        }
    }

    oyk_sysv_canvas_put_text(cv, r.top + r.h - 4, r.left + 3, r.w - 6, OYK_SYSV_ST_WARN, app->feedback);
    oyk_sysv_canvas_put_hdiv(cv, r.top + r.h - 3, r.left, r.w, OYK_SYSV_ST_DLG_BORDER);
    oyk_sysv_canvas_put_text(cv, r.top + r.h - 2, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_BAR,
                             "1-8:Tabs  Lt/Rt:Tab  Up/Dn:Row  Spc/Ent:Act  Esc:Close");
}

static void draw_wizard_modal(OykSysvCanvas *cv, const OykSysvApp *app) {
    OykSysvRect r;
    const OykSysvAddIfaceWiz *w;
    char buf[120];
    TuiBtn btns[3];
    int i;
    w = &app->wiz;
    center_modal(cv->rows, cv->cols, 17, 68, &r);
    draw_modal(cv, &r, "Add Interface", 1);
    oyk_sysv_canvas_put_text(cv, r.top + 2, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_BORDER,
                             oyk_sysv_wiz_step_prompt(w));
    if (w->step == 0) {
        for (i = 0; i < oyk_sysv_wiz_driver_count(); ++i) {
            sprintf(buf, "(%c) %-12s - %s",
                    (i == w->selected_driver_idx) ? '*' : ' ',
                    oyk_sysv_wiz_driver_name(i),
                    oyk_sysv_wiz_driver_desc(i));
            oyk_sysv_canvas_put_text(cv, r.top + 4 + i, r.left + 3, r.w - 6,
                                     (i == w->selected_driver_idx) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_FILL,
                                     buf);
        }
    } else if (w->step == 1) {
        sprintf(buf, "Interface ID: %s", w->draft.id);
        oyk_sysv_canvas_put_text(cv, r.top + 4, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_INPUT, buf);
        sprintf(buf, "Driver:       %s", w->draft.type);
        oyk_sysv_canvas_put_text(cv, r.top + 6, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_FILL, buf);
        sprintf(buf, "Description:  %s", w->draft.description);
        oyk_sysv_canvas_put_text(cv, r.top + 8, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_FILL, buf);
    } else if (w->step == 2) {
        sprintf(buf, "Endpoint: %s:%u  (Dev: %s)", w->draft.host, (unsigned int)w->draft.port, w->draft.device);
        oyk_sysv_canvas_put_text(cv, r.top + 4, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_INPUT, buf);
        sprintf(buf, "Allow Same-Interface: [%c]  Learned Policy: <%s>",
                w->draft.allow_same_interface ? 'X' : ' ',
                oyk_sysv_format_learned_policy(w->draft.learned_peer_policy));
        oyk_sysv_canvas_put_text(cv, r.top + 6, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_FILL, buf);
    } else {
        oyk_sysv_wiz_format_summary(w, buf, sizeof(buf));
        oyk_sysv_canvas_put_text(cv, r.top + 4, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_FILL, buf);
    }
    oyk_sysv_canvas_put_hdiv(cv, r.top + r.h - 3, r.left, r.w, OYK_SYSV_ST_DLG_BORDER);
    btns[0].label = "Back"; btns[0].enabled = (w->step > 0);
    btns[1].label = (w->step < 3) ? "Next" : "Save"; btns[1].enabled = 1;
    btns[2].label = "Cancel"; btns[2].enabled = 1;
    draw_btn_row(cv, r.top + r.h - 2, r.left + 3, r.w - 6, btns, 3, app->wiz_btn);
}

static void draw_terminal_view(OykSysvCanvas *cv, const OykSysvApp *app) {
    int i;
    int hdr_row;
    int hotkey_row;
    int input_row;
    char hdr[100];
    char inp[100];
    hdr_row = 0;
    hotkey_row = cv->rows - 2;
    input_row = cv->rows - 1;
    oyk_sysv_canvas_clear(cv, OYK_SYSV_ST_BG);
    sprintf(hdr, " OYK Local Packet Terminal (YAPP) | %s", app->term_status);
    oyk_sysv_canvas_put_text(cv, hdr_row, 0, cv->cols, OYK_SYSV_ST_HEADER, hdr);
    /* Borderless full-width viewport without any inner frame */
    for (i = 0; i < app->term_line_count && (1 + i) < hotkey_row; ++i) {
        oyk_sysv_canvas_put_text(cv, 1 + i, 0, cv->cols, OYK_SYSV_ST_NORMAL, app->term_lines[i]);
    }
    oyk_sysv_canvas_put_text(cv, hotkey_row, 0, cv->cols, OYK_SYSV_ST_STATUS,
                             oyk_sysv_term_hotkey_bar_text(app->term_ctrl_a_pending));
    sprintf(inp, "> %s_", app->term_input);
    oyk_sysv_canvas_put_text(cv, input_row, 0, cv->cols, OYK_SYSV_ST_DLG_INPUT, inp);
}

static void draw_term_help_modal(OykSysvCanvas *cv) {
    OykSysvRect r;
    int i;
    TuiBtn btn;
    center_modal(cv->rows, cv->cols, 15, 64, &r);
    draw_modal(cv, &r, "Terminal Hotkey Help", 1);
    oyk_sysv_canvas_put_text(cv, r.top + 2, r.left + 3, r.w - 6, OYK_SYSV_ST_LIST_HDR,
                             "Hotkey      Action                            Alt");
    for (i = 0; i < oyk_sysv_term_help_count() && i < r.h - 6; ++i) {
        char row[64];
        sprintf(row, "%-11s %-33s %s",
                oyk_sysv_term_help_key(i),
                oyk_sysv_term_help_action(i),
                oyk_sysv_term_help_alt(i));
        oyk_sysv_canvas_put_text(cv, r.top + 3 + i, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_FILL, row);
    }
    oyk_sysv_canvas_put_hdiv(cv, r.top + r.h - 3, r.left, r.w, OYK_SYSV_ST_DLG_BORDER);
    btn.label = "Close";
    btn.enabled = 1;
    draw_btn_row(cv, r.top + r.h - 2, r.left + 3, r.w - 6, &btn, 1, 0);
}

static void draw_files_modal(OykSysvCanvas *cv, const OykSysvApp *app) {
    OykSysvRect r;
    int mid_w;
    char ltitle[48];
    char rtitle[48];
    int i;
    TuiBtn btns[4];
    center_modal(cv->rows, cv->cols, 19, 76, &r);
    draw_modal(cv, &r, "Manage BBS Files Area", 1);
    mid_w = r.w / 2;
    oyk_sysv_filemgr_pane_title(&app->fm.inc, "Incoming:", ltitle, sizeof(ltitle));
    oyk_sysv_filemgr_pane_title(&app->fm.files, "Files:", rtitle, sizeof(rtitle));
    oyk_sysv_canvas_put_text(cv, r.top + 2, r.left + 2, mid_w - 3, OYK_SYSV_ST_LIST_HDR, ltitle);
    oyk_sysv_canvas_put_text(cv, r.top + 2, r.left + mid_w + 1, mid_w - 3, OYK_SYSV_ST_LIST_HDR, rtitle);
    for (i = 0; i < app->fm.inc.count && i < r.h - 7; ++i) {
        char row[40];
        if (app->fm.inc.entries[i].is_dir) sprintf(row, "%-20.20s <DIR>", app->fm.inc.entries[i].name);
        else sprintf(row, "%-20.20s %lu", app->fm.inc.entries[i].name, app->fm.inc.entries[i].size);
        oyk_sysv_canvas_put_text(cv, r.top + 3 + i, r.left + 2, mid_w - 3,
                                 (app->fm.active_pane == 0 && i == app->fm.inc.sel) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_FILL,
                                 row);
    }
    for (i = 0; i < app->fm.files.count && i < r.h - 7; ++i) {
        char row[40];
        if (app->fm.files.entries[i].is_dir) sprintf(row, "%-20.20s <DIR>", app->fm.files.entries[i].name);
        else sprintf(row, "%-20.20s %lu", app->fm.files.entries[i].name, app->fm.files.entries[i].size);
        oyk_sysv_canvas_put_text(cv, r.top + 3 + i, r.left + mid_w + 1, mid_w - 3,
                                 (app->fm.active_pane == 1 && i == app->fm.files.sel) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_FILL,
                                 row);
    }
    oyk_sysv_canvas_put_hdiv(cv, r.top + r.h - 3, r.left, r.w, OYK_SYSV_ST_DLG_BORDER);
    btns[0].label = "Approve"; btns[0].enabled = 1;
    btns[1].label = "Mkdir";   btns[1].enabled = 1;
    btns[2].label = "Delete";  btns[2].enabled = 1;
    btns[3].label = "Close";   btns[3].enabled = 1;
    draw_btn_row(cv, r.top + r.h - 2, r.left + 2, r.w - 4, btns, 4, app->fm.active_btn);
}

static void draw_about_modal(OykSysvCanvas *cv, const OykSysvApp *app) {
    OykSysvRect r;
    int i;
    int tx;
    TuiBtn btn;
    center_modal(cv->rows, cv->cols, 16, 68, &r);
    draw_modal(cv, &r, "About OYK Amateur Radio Packet Node", 1);
    for (i = 0; i < OYK_SYSV_LOGO_ASCII_ROWS && i < 10; ++i) {
        oyk_sysv_canvas_put_text(cv, r.top + 2 + i, r.left + 2, 16,
                                 OYK_SYSV_ST_OK, kOykSysvLogoAscii[i]);
    }
    tx = r.left + 20;
    oyk_sysv_canvas_put_text(cv, r.top + 2, tx, r.w - 22, OYK_SYSV_ST_TITLE, "OYK Amateur Radio Packet Node");
    oyk_sysv_canvas_put_text(cv, r.top + 4, tx, r.w - 22, OYK_SYSV_ST_DLG_FILL, app->node.platform_label);
    oyk_sysv_canvas_put_text(cv, r.top + 6, tx, r.w - 22, OYK_SYSV_ST_DLG_FILL, "AX.25 / KISS / AXUDP / AXIP / Telnet");
    oyk_sysv_canvas_put_text(cv, r.top + 8, tx, r.w - 22, OYK_SYSV_ST_DLG_FILL, "Copyright (C) 2026 KR4OYK - David Kuder");
    oyk_sysv_canvas_put_text(cv, r.top + 9, tx, r.w - 22, OYK_SYSV_ST_DLG_FILL, "License: GPL-3.0-or-later");
    oyk_sysv_canvas_put_hdiv(cv, r.top + r.h - 3, r.left, r.w, OYK_SYSV_ST_DLG_BORDER);
    btn.label = "OK";
    btn.enabled = 1;
    draw_btn_row(cv, r.top + r.h - 2, tx, r.w - 22, &btn, 1, 0);
}

static void draw_simple_modal(OykSysvCanvas *cv, const OykSysvApp *app) {
    OykSysvRect r;
    char buf[96];
    int i;
    if (app->screen == OYK_SYSV_SCR_EDIT_IFACE) {
        const OykSysvIfaceConfig *ifc;
        TuiBtn btns[2];
        ifc = &app->node.cfg.ifaces[0];
        center_modal(cv->rows, cv->cols, 14, 64, &r);
        draw_modal(cv, &r, "Edit Interface", 1);
        sprintf(buf, "ID: %-10s  Type: %s", ifc->id, ifc->type);
        oyk_sysv_canvas_put_text(cv, r.top + 2, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_FILL, buf);
        sprintf(buf, "Host: %s:%u  Allow Same-Interface: [%c]",
                ifc->host, (unsigned int)ifc->port, ifc->allow_same_interface ? 'X' : ' ');
        oyk_sysv_canvas_put_text(cv, r.top + 4, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_FILL, buf);
        sprintf(buf, "Learned Peer Policy: <%s>", oyk_sysv_format_learned_policy(ifc->learned_peer_policy));
        oyk_sysv_canvas_put_text(cv, r.top + 6, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_FILL, buf);
        oyk_sysv_canvas_put_hdiv(cv, r.top + r.h - 3, r.left, r.w, OYK_SYSV_ST_DLG_BORDER);
        btns[0].label = "Save"; btns[0].enabled = 1;
        btns[1].label = "Cancel"; btns[1].enabled = 1;
        draw_btn_row(cv, r.top + r.h - 2, r.left + 3, r.w - 6, btns, 2, 0);
    } else if (app->screen == OYK_SYSV_SCR_EDIT_PEER) {
        const OykSysvPeerConfig *pc;
        TuiBtn btns[2];
        pc = &app->node.cfg.peers[0];
        center_modal(cv->rows, cv->cols, 13, 62, &r);
        draw_modal(cv, &r, "Edit Peer", 1);
        sprintf(buf, "ID: %-10s  Callsign: %s", pc->id, pc->callsign);
        oyk_sysv_canvas_put_text(cv, r.top + 2, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_FILL, buf);
        sprintf(buf, "Interface: %s  Host: %s:%u", pc->interface_id, pc->host, (unsigned int)pc->port);
        oyk_sysv_canvas_put_text(cv, r.top + 4, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_FILL, buf);
        oyk_sysv_canvas_put_hdiv(cv, r.top + r.h - 3, r.left, r.w, OYK_SYSV_ST_DLG_BORDER);
        btns[0].label = "Save"; btns[0].enabled = 1;
        btns[1].label = "Cancel"; btns[1].enabled = 1;
        draw_btn_row(cv, r.top + r.h - 2, r.left + 3, r.w - 6, btns, 2, 0);
    } else if (app->screen == OYK_SYSV_SCR_TERMINATE) {
        TuiBtn btns[2];
        center_modal(cv->rows, cv->cols, 13, 62, &r);
        draw_modal(cv, &r, "Terminate Active Connection", 1);
        oyk_sysv_canvas_put_text(cv, r.top + 2, r.left + 3, r.w - 6, OYK_SYSV_ST_LIST_HDR,
                                 "ID    Callsign    Port       State");
        for (i = 0; i < OYK_SYSV_MAX_SESSIONS && i < 4; ++i) {
            if (app->node.sessions[i].state == 2) {
                sprintf(buf, "%-5lu %-11.11s %-10.10s CONNECTED",
                        (unsigned long)app->node.sessions[i].id,
                        app->node.sessions[i].callsign,
                        app->node.sessions[i].port_id);
                oyk_sysv_canvas_put_text(cv, r.top + 3 + i, r.left + 3, r.w - 6,
                                         (i == app->term_sess_sel) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_FILL,
                                         buf);
            }
        }
        oyk_sysv_canvas_put_hdiv(cv, r.top + r.h - 3, r.left, r.w, OYK_SYSV_ST_DLG_BORDER);
        btns[0].label = "Terminate"; btns[0].enabled = 1;
        btns[1].label = "Cancel"; btns[1].enabled = 1;
        draw_btn_row(cv, r.top + r.h - 2, r.left + 3, r.w - 6, btns, 2, app->modal_btn);
    } else if (app->screen == OYK_SYSV_SCR_BAN) {
        TuiBtn btns[3];
        center_modal(cv->rows, cv->cols, 13, 64, &r);
        draw_modal(cv, &r, "Ban / Unban Callsign", 1);
        oyk_sysv_canvas_put_text(cv, r.top + 2, r.left + 3, r.w - 6, OYK_SYSV_ST_LIST_HDR,
                                 "Callsign   Lift At     Reason");
        for (i = 0; i < app->node.ban_count && i < 4; ++i) {
            sprintf(buf, "%-10s %-11s %s", app->node.bans[i].key,
                    app->node.bans[i].lift_at, app->node.bans[i].reason);
            oyk_sysv_canvas_put_text(cv, r.top + 3 + i, r.left + 3, r.w - 6,
                                     (i == app->ban_sel) ? OYK_SYSV_ST_DLG_SEL : OYK_SYSV_ST_DLG_FILL,
                                     buf);
        }
        oyk_sysv_canvas_put_hdiv(cv, r.top + r.h - 3, r.left, r.w, OYK_SYSV_ST_DLG_BORDER);
        btns[0].label = "Ban"; btns[0].enabled = 1;
        btns[1].label = "Unban"; btns[1].enabled = 1;
        btns[2].label = "Close"; btns[2].enabled = 1;
        draw_btn_row(cv, r.top + r.h - 2, r.left + 3, r.w - 6, btns, 3, app->modal_btn);
    } else if (app->screen == OYK_SYSV_SCR_EXIT_PEERS) {
        TuiBtn btns[4];
        center_modal(cv->rows, cv->cols, 11, 70, &r);
        draw_modal(cv, &r, "Auto-Learned Peers", 1);
        sprintf(buf, "%d auto-learned peer(s) pending. Save to peers.cfg?",
                oyk_sysv_node_auto_peer_count(&app->node));
        oyk_sysv_canvas_put_text(cv, r.top + 3, r.left + 3, r.w - 6, OYK_SYSV_ST_DLG_FILL, buf);
        oyk_sysv_canvas_put_hdiv(cv, r.top + r.h - 3, r.left, r.w, OYK_SYSV_ST_DLG_BORDER);
        btns[0].label = "Save & Exit"; btns[0].enabled = 1;
        btns[1].label = "Discard"; btns[1].enabled = 1;
        btns[2].label = "Review Peers"; btns[2].enabled = 1;
        btns[3].label = "Cancel"; btns[3].enabled = 1;
        draw_btn_row(cv, r.top + r.h - 2, r.left + 2, r.w - 4, btns, 4, app->exit_btn_sel);
    }
}

void oyk_sysv_tui_render(OykSysvCanvas *cv, const OykSysvApp *app, int rows, int cols) {
    if (!cv || !app) return;
    oyk_sysv_canvas_init(cv, rows, cols);

    if (app->screen == OYK_SYSV_SCR_TERMINAL || app->screen == OYK_SYSV_SCR_TERM_HELP) {
        draw_terminal_view(cv, app);
        if (app->screen == OYK_SYSV_SCR_TERM_HELP) {
            draw_term_help_modal(cv);
        }
        return;
    }

    draw_main_console(cv, app);
    if (app->screen == OYK_SYSV_SCR_MENU) draw_dropdown_menu(cv, app);
    else if (app->screen == OYK_SYSV_SCR_SETTINGS) draw_settings_modal(cv, app);
    else if (app->screen == OYK_SYSV_SCR_ADD_IFACE) draw_wizard_modal(cv, app);
    else if (app->screen == OYK_SYSV_SCR_THEME) {
        OykSysvApp tmp;
        tmp = *app;
        tmp.settings_tab = 7;
        draw_settings_modal(cv, &tmp);
    }
    else if (app->screen == OYK_SYSV_SCR_FILES) draw_files_modal(cv, app);
    else if (app->screen == OYK_SYSV_SCR_ABOUT) draw_about_modal(cv, app);
    else if (app->screen != OYK_SYSV_SCR_MAIN) draw_simple_modal(cv, app);
}

int oyk_sysv_is_modal_screen(int screen_id) {
    switch (screen_id) {
        case OYK_SYSV_SCR_SETTINGS:
        case OYK_SYSV_SCR_ADD_IFACE:
        case OYK_SYSV_SCR_EDIT_IFACE:
        case OYK_SYSV_SCR_PEERS:
        case OYK_SYSV_SCR_EDIT_PEER:
        case OYK_SYSV_SCR_TERMINATE:
        case OYK_SYSV_SCR_BAN:
        case OYK_SYSV_SCR_EXIT_PEERS:
        case OYK_SYSV_SCR_THEME:
        case OYK_SYSV_SCR_TERM_HELP:
        case OYK_SYSV_SCR_FILES:
        case OYK_SYSV_SCR_RIG:
        case OYK_SYSV_SCR_ABOUT:
            return 1;
        default:
            return 0;
    }
}

static int s_toupper_ascii(int ch) {
    if (ch >= 'a' && ch <= 'z') return ch - ('a' - 'A');
    return ch;
}

static void s_move_menu_sel(OykSysvApp *app, int delta) {
    int count;
    int cur;
    int step;
    count = oyk_sysv_node_menu_count();
    if (count <= 0) return;
    cur = app->menu_sel;
    for (step = 0; step < count; ++step) {
        const OykSysvMenuItem *it;
        cur = (cur + delta) % count;
        if (cur < 0) cur += count;
        it = oyk_sysv_node_menu_item(cur);
        if (it && !it->is_separator) {
            app->menu_sel = cur;
            return;
        }
    }
}

static void s_close_modal(OykSysvApp *app) {
    if (app->screen == OYK_SYSV_SCR_TERM_HELP && app->term_help_from_term) {
        app->term_help_from_term = 0;
        app->screen = OYK_SYSV_SCR_TERMINAL;
        return;
    }
    if (app->screen == OYK_SYSV_SCR_SETTINGS && app->settings_tab == 7 &&
        app->theme_ctrl.preview_active) {
        oyk_sysv_theme_revert(&app->theme_ctrl);
    }
    app->screen = OYK_SYSV_SCR_MAIN;
}

static int s_handle_settings_key(OykSysvApp *app, int key) {
    int n_tabs;
    int max_row;
    n_tabs = oyk_sysv_settings_tab_count();
    if (key >= '1' && key <= '8') {
        app->settings_tab = key - '1';
        app->settings_row = 0;
        return 1;
    }
    if (key == OYK_SYSV_KEY_LEFT) {
        app->settings_tab = (app->settings_tab + n_tabs - 1) % n_tabs;
        app->settings_row = 0;
        return 1;
    }
    if (key == OYK_SYSV_KEY_RIGHT) {
        app->settings_tab = (app->settings_tab + 1) % n_tabs;
        app->settings_row = 0;
        return 1;
    }
    if (app->settings_tab == 3) {
        max_row = (app->node.cfg.iface_count > 0) ? (app->node.cfg.iface_count - 1) : 0;
        if (key == OYK_SYSV_KEY_UP && app->iface_sel > 0) { --app->iface_sel; return 1; }
        if (key == OYK_SYSV_KEY_DOWN && app->iface_sel < max_row) { ++app->iface_sel; return 1; }
        if (key == 'a' || key == 'A') {
            oyk_sysv_wiz_init(&app->wiz, &app->node.cfg);
            app->wiz_btn = 1;
            app->screen = OYK_SYSV_SCR_ADD_IFACE;
            return 1;
        }
        if ((key == 'e' || key == 'E' || key == OYK_SYSV_KEY_ENTER || key == '\n') &&
            app->node.cfg.iface_count > 0) {
            app->modal_btn = 0;
            app->screen = OYK_SYSV_SCR_EDIT_IFACE;
            return 1;
        }
        return 0;
    }
    if (app->settings_tab == 4) {
        max_row = (app->node.cfg.peer_count > 0) ? (app->node.cfg.peer_count - 1) : 0;
        if (key == OYK_SYSV_KEY_UP && app->peer_sel > 0) { --app->peer_sel; return 1; }
        if (key == OYK_SYSV_KEY_DOWN && app->peer_sel < max_row) { ++app->peer_sel; return 1; }
        if ((key == 'e' || key == 'E' || key == OYK_SYSV_KEY_ENTER || key == '\n') &&
            app->node.cfg.peer_count > 0) {
            app->modal_btn = 0;
            app->screen = OYK_SYSV_SCR_EDIT_PEER;
            return 1;
        }
        return 0;
    }
    if (app->settings_tab == 7) {
        if (key == ' ' || key == OYK_SYSV_KEY_ENTER || key == '\n' ||
            key == '>' || key == ']' || key == 'n' || key == 'N') {
            oyk_sysv_theme_cycle(&app->theme_ctrl, 1);
            oyk_sysv_copy_str(app->theme_ctrl.saved_name, sizeof(app->theme_ctrl.saved_name),
                              app->theme_ctrl.active_name);
            app->theme_ctrl.preview_active = 0;
            oyk_sysv_copy_str(app->feedback, sizeof(app->feedback), "Theme preset updated.");
            return 1;
        }
        if (key == '<' || key == '[' || key == 'p' || key == 'P') {
            oyk_sysv_theme_cycle(&app->theme_ctrl, -1);
            oyk_sysv_copy_str(app->theme_ctrl.saved_name, sizeof(app->theme_ctrl.saved_name),
                              app->theme_ctrl.active_name);
            app->theme_ctrl.preview_active = 0;
            oyk_sysv_copy_str(app->feedback, sizeof(app->feedback), "Theme preset updated.");
            return 1;
        }
        return 0;
    }
    if (key == OYK_SYSV_KEY_UP && app->settings_row > 0) {
        --app->settings_row;
        return 1;
    }
    if (key == OYK_SYSV_KEY_DOWN && app->settings_row < 1) {
        ++app->settings_row;
        return 1;
    }
    if (key == ' ') {
        if (app->settings_tab == 0 && app->settings_row == 1) {
            app->node.cfg.accept_incoming = !app->node.cfg.accept_incoming;
            return 1;
        }
        if (app->settings_tab == 1 && app->settings_row == 0) {
            app->node.cfg.bbs_enabled = !app->node.cfg.bbs_enabled;
            return 1;
        }
        if (app->settings_tab == 2) {
            app->node.cfg.chat_enabled = !app->node.cfg.chat_enabled;
            return 1;
        }
        if (app->settings_tab == 5) {
            app->node.cfg.rig_enabled = !app->node.cfg.rig_enabled;
            return 1;
        }
        if (app->settings_tab == 6) {
            if (app->settings_row == 0) app->node.cfg.printer_enabled = !app->node.cfg.printer_enabled;
            else app->node.cfg.printer_condensed = !app->node.cfg.printer_condensed;
            return 1;
        }
        return 0;
    }
    if (key == OYK_SYSV_KEY_ENTER || key == '\n') {
        oyk_sysv_copy_str(app->feedback, sizeof(app->feedback), "Settings applied.");
        app->screen = OYK_SYSV_SCR_MAIN;
        return 1;
    }
    return 0;
}

int oyk_sysv_tui_handle_key(OykSysvApp *app, int key) {
    if (!app || key == OYK_SYSV_KEY_NONE) return 0;

    /* Modal dialogs strictly preempt ALL global hotkeys (Ctrl-N, F10, Tab switch, F9). */
    if (oyk_sysv_is_modal_screen(app->screen)) {
        if (key == OYK_SYSV_KEY_CTRL_N || key == OYK_SYSV_KEY_F10 || key == OYK_SYSV_KEY_F9) {
            return 0;
        }
        if (key == OYK_SYSV_KEY_ESC) {
            s_close_modal(app);
            return 1;
        }
        if (app->screen == OYK_SYSV_SCR_SETTINGS) {
            return s_handle_settings_key(app, key);
        }
        if (app->screen == OYK_SYSV_SCR_ADD_IFACE) {
            if (app->wiz.step == 0 && (key == OYK_SYSV_KEY_UP || key == OYK_SYSV_KEY_DOWN)) {
                int ndrv;
                int next;
                ndrv = oyk_sysv_wiz_driver_count();
                next = app->wiz.selected_driver_idx + ((key == OYK_SYSV_KEY_DOWN) ? 1 : (ndrv - 1));
                next %= ndrv;
                oyk_sysv_wiz_select_driver(&app->wiz, &app->node.cfg, next);
                return 1;
            }
            if (app->wiz.step == 2 && key == ' ') {
                app->wiz.draft.allow_same_interface = !app->wiz.draft.allow_same_interface;
                return 1;
            }
            if (key == OYK_SYSV_KEY_LEFT) {
                if (app->wiz_btn > 0) --app->wiz_btn;
                if (app->wiz_btn == 0 && app->wiz.step == 0) app->wiz_btn = 1;
                return 1;
            }
            if (key == OYK_SYSV_KEY_RIGHT || key == OYK_SYSV_KEY_TAB) {
                if (app->wiz_btn < 2) ++app->wiz_btn;
                else app->wiz_btn = (app->wiz.step > 0) ? 0 : 1;
                return 1;
            }
            if (key == OYK_SYSV_KEY_ENTER || key == '\n') {
                if (app->wiz_btn == 0 && app->wiz.step > 0) {
                    oyk_sysv_wiz_back(&app->wiz);
                    if (app->wiz.step == 0 && app->wiz_btn == 0) app->wiz_btn = 1;
                } else if (app->wiz_btn == 2) {
                    app->screen = OYK_SYSV_SCR_MAIN;
                } else if (app->wiz.step < 3) {
                    oyk_sysv_wiz_advance(&app->wiz, &app->node.cfg);
                } else {
                    if (app->node.cfg.iface_count < OYK_SYSV_MAX_IFACES) {
                        int idx;
                        idx = app->node.cfg.iface_count++;
                        app->node.cfg.ifaces[idx] = app->wiz.draft;
                        app->node.iface_rt[idx].running = 1;
                        oyk_sysv_copy_str(app->node.iface_rt[idx].status_text,
                                          sizeof(app->node.iface_rt[idx].status_text), "UP");
                    }
                    app->screen = OYK_SYSV_SCR_MAIN;
                }
                return 1;
            }
            return 0;
        }
        if (app->screen == OYK_SYSV_SCR_FILES) {
            OykSysvFilePane *pane;
            pane = (app->fm.active_pane == 0) ? &app->fm.inc : &app->fm.files;
            if (key == OYK_SYSV_KEY_TAB || key == OYK_SYSV_KEY_LEFT || key == OYK_SYSV_KEY_RIGHT) {
                app->fm.active_pane = !app->fm.active_pane;
                return 1;
            }
            if (key == OYK_SYSV_KEY_UP && pane->sel > 0) {
                --pane->sel;
                return 1;
            }
            if (key == OYK_SYSV_KEY_DOWN && pane->sel + 1 < pane->count) {
                ++pane->sel;
                return 1;
            }
            if ((key == OYK_SYSV_KEY_ENTER || key == '\n') &&
                pane->sel >= 0 && pane->sel < pane->count &&
                pane->entries[pane->sel].is_dir) {
                oyk_sysv_filemgr_enter_dir(&app->fm, app->fm.active_pane,
                                           pane->entries[pane->sel].name);
                return 1;
            }
            return 0;
        }
        if (app->screen == OYK_SYSV_SCR_TERMINATE) {
            if (key == OYK_SYSV_KEY_UP && app->term_sess_sel > 0) { --app->term_sess_sel; return 1; }
            if (key == OYK_SYSV_KEY_DOWN && app->term_sess_sel + 1 < OYK_SYSV_MAX_SESSIONS) {
                ++app->term_sess_sel;
                return 1;
            }
            if (key == OYK_SYSV_KEY_LEFT || key == OYK_SYSV_KEY_RIGHT || key == OYK_SYSV_KEY_TAB) {
                app->modal_btn = !app->modal_btn;
                return 1;
            }
            if (key == OYK_SYSV_KEY_ENTER || key == '\n') {
                if (app->modal_btn == 0 && app->term_sess_sel >= 0 &&
                    app->term_sess_sel < OYK_SYSV_MAX_SESSIONS &&
                    app->node.sessions[app->term_sess_sel].state == 2) {
                    oyk_sysv_node_terminate_session(&app->node,
                                                    app->node.sessions[app->term_sess_sel].id);
                }
                app->screen = OYK_SYSV_SCR_MAIN;
                return 1;
            }
            return 0;
        }
        if (app->screen == OYK_SYSV_SCR_BAN) {
            if (key == OYK_SYSV_KEY_UP && app->ban_sel > 0) { --app->ban_sel; return 1; }
            if (key == OYK_SYSV_KEY_DOWN && app->ban_sel + 1 < app->node.ban_count) {
                ++app->ban_sel;
                return 1;
            }
            if (key == OYK_SYSV_KEY_LEFT) { app->modal_btn = (app->modal_btn + 2) % 3; return 1; }
            if (key == OYK_SYSV_KEY_RIGHT || key == OYK_SYSV_KEY_TAB) {
                app->modal_btn = (app->modal_btn + 1) % 3;
                return 1;
            }
            if (key == OYK_SYSV_KEY_ENTER || key == '\n') {
                if (app->modal_btn == 2) app->screen = OYK_SYSV_SCR_MAIN;
                return 1;
            }
            return 0;
        }
        if (app->screen == OYK_SYSV_SCR_EXIT_PEERS) {
            if (key == OYK_SYSV_KEY_LEFT) { app->exit_btn_sel = (app->exit_btn_sel + 3) % 4; return 1; }
            if (key == OYK_SYSV_KEY_RIGHT || key == OYK_SYSV_KEY_TAB) {
                app->exit_btn_sel = (app->exit_btn_sel + 1) % 4;
                return 1;
            }
            if (key == OYK_SYSV_KEY_ENTER || key == '\n') {
                if (app->exit_btn_sel == 0 || app->exit_btn_sel == 1) {
                    app->running = 0;
                    app->screen = OYK_SYSV_SCR_MAIN;
                } else if (app->exit_btn_sel == 2) {
                    app->screen = OYK_SYSV_SCR_SETTINGS;
                    app->settings_tab = 4;
                } else {
                    app->screen = OYK_SYSV_SCR_MAIN;
                }
                return 1;
            }
            return 0;
        }
        if (key == OYK_SYSV_KEY_LEFT || key == OYK_SYSV_KEY_RIGHT || key == OYK_SYSV_KEY_TAB) {
            app->modal_btn = !app->modal_btn;
            return 1;
        }
        if (key == OYK_SYSV_KEY_ENTER || key == '\n') {
            s_close_modal(app);
            return 1;
        }
        return 0;
    }

    /* Dropdown Node Menu */
    if (app->screen == OYK_SYSV_SCR_MENU) {
        int i;
        if (key == OYK_SYSV_KEY_ESC || key == OYK_SYSV_KEY_CTRL_N || key == OYK_SYSV_KEY_F10) {
            app->screen = OYK_SYSV_SCR_MAIN;
            return 1;
        }
        if (key == OYK_SYSV_KEY_UP) { s_move_menu_sel(app, -1); return 1; }
        if (key == OYK_SYSV_KEY_DOWN) { s_move_menu_sel(app, 1); return 1; }
        if (key == OYK_SYSV_KEY_ENTER || key == '\n') {
            const OykSysvMenuItem *it;
            it = oyk_sysv_node_menu_item(app->menu_sel);
            if (it && !it->is_separator) {
                oyk_sysv_app_exec_menu_action(app, it->action);
                return 1;
            }
            return 0;
        }
        for (i = 0; i < oyk_sysv_node_menu_count(); ++i) {
            const OykSysvMenuItem *it;
            it = oyk_sysv_node_menu_item(i);
            if (it && !it->is_separator && it->shortcut_key &&
                s_toupper_ascii(key) == s_toupper_ascii((unsigned char)it->shortcut_key)) {
                oyk_sysv_app_exec_menu_action(app, it->action);
                return 1;
            }
        }
        return 0;
    }

    /* Local Terminal view */
    if (app->screen == OYK_SYSV_SCR_TERMINAL) {
        if (app->term_ctrl_a_pending) {
            int cmd;
            app->term_ctrl_a_pending = 0;
            cmd = oyk_sysv_term_decode_ctrl_a(key);
            if (cmd == OYK_SYSV_TCMD_HELP) {
                app->term_help_from_term = 1;
                app->screen = OYK_SYSV_SCR_TERM_HELP;
                return 1;
            }
            if (cmd == OYK_SYSV_TCMD_CLOSE) {
                app->screen = OYK_SYSV_SCR_MAIN;
                return 1;
            }
            if (cmd == OYK_SYSV_TCMD_CLEAR) {
                app->term_line_count = 0;
                return 1;
            }
            if (cmd == OYK_SYSV_TCMD_WRAP) {
                app->term_eol.word_wrap = !app->term_eol.word_wrap;
                return 1;
            }
            if (cmd == OYK_SYSV_TCMD_EOL) {
                app->term_eol.eol = (OykSysvLineEnding)(((int)app->term_eol.eol + 1) % 3);
                return 1;
            }
            if (cmd == OYK_SYSV_TCMD_ECHO) {
                app->term_eol.echo = !app->term_eol.echo;
                return 1;
            }
            return 1;
        }
        if (key == OYK_SYSV_KEY_CTRL_A) {
            app->term_ctrl_a_pending = 1;
            return 1;
        }
        if (key == OYK_SYSV_KEY_TAB || key == OYK_SYSV_KEY_F9) {
            app->screen = OYK_SYSV_SCR_MAIN;
            return 1;
        }
        if (key == OYK_SYSV_KEY_BS || key == 8) {
            size_t len;
            len = strlen(app->term_input);
            if (len > 0U) {
                app->term_input[len - 1U] = '\0';
                return 1;
            }
            return 0;
        }
        if (key == OYK_SYSV_KEY_ENTER || key == '\n') {
            if (app->term_input[0]) {
                char echo_buf[96];
                sprintf(echo_buf, "> %s", app->term_input);
                oyk_sysv_app_term_append(app, echo_buf);
                if (!app->term_connected) {
                    oyk_sysv_app_term_append(app, "*** Not connected (use C <callsign> once link is active).");
                }
                app->term_input[0] = '\0';
                return 1;
            }
            return 0;
        }
        if (key >= 32 && key <= 126) {
            size_t len;
            len = strlen(app->term_input);
            if (len + 1U < sizeof(app->term_input)) {
                app->term_input[len] = (char)key;
                app->term_input[len + 1U] = '\0';
                return 1;
            }
            return 0;
        }
        return 0;
    }

    /* Main Console (menu closed, no modal): ONLY Ctrl-N / F10 and Tab / F9!
     * Single-letter menu hotkeys are strictly disabled while menu is closed. */
    if (key == OYK_SYSV_KEY_CTRL_N || key == OYK_SYSV_KEY_F10) {
        app->menu_sel = 0;
        app->screen = OYK_SYSV_SCR_MENU;
        return 1;
    }
    if (key == OYK_SYSV_KEY_TAB || key == OYK_SYSV_KEY_F9) {
        app->screen = OYK_SYSV_SCR_TERMINAL;
        return 1;
    }
    return 0;
}

int oyk_sysv_tui_setup_screen(OykSysvApp *app, const char *screen_name) {
    if (!app || !screen_name) return 0;
    app->screen = OYK_SYSV_SCR_MAIN;
    app->settings_tab = 0;
    app->settings_row = 0;
    app->wiz.step = 0;
    if (strcmp(screen_name, "main") == 0) app->screen = OYK_SYSV_SCR_MAIN;
    else if (strcmp(screen_name, "menu") == 0) app->screen = OYK_SYSV_SCR_MENU;
    else if (strcmp(screen_name, "settings") == 0) { app->screen = OYK_SYSV_SCR_SETTINGS; app->settings_tab = 0; }
    else if (strcmp(screen_name, "settings_bbs") == 0) { app->screen = OYK_SYSV_SCR_SETTINGS; app->settings_tab = 1; }
    else if (strcmp(screen_name, "settings_chat") == 0) { app->screen = OYK_SYSV_SCR_SETTINGS; app->settings_tab = 2; }
    else if (strcmp(screen_name, "settings_ifaces") == 0) { app->screen = OYK_SYSV_SCR_SETTINGS; app->settings_tab = 3; }
    else if (strcmp(screen_name, "peers") == 0) { app->screen = OYK_SYSV_SCR_SETTINGS; app->settings_tab = 4; }
    else if (strcmp(screen_name, "settings_rig") == 0) { app->screen = OYK_SYSV_SCR_SETTINGS; app->settings_tab = 5; }
    else if (strcmp(screen_name, "settings_printer") == 0) { app->screen = OYK_SYSV_SCR_SETTINGS; app->settings_tab = 6; }
    else if (strcmp(screen_name, "settings_theme") == 0 || strcmp(screen_name, "theme") == 0) {
        app->screen = OYK_SYSV_SCR_SETTINGS;
        app->settings_tab = 7;
    }
    else if (strcmp(screen_name, "add_iface_1") == 0) { app->screen = OYK_SYSV_SCR_ADD_IFACE; app->wiz.step = 0; }
    else if (strcmp(screen_name, "add_iface_2") == 0) { app->screen = OYK_SYSV_SCR_ADD_IFACE; app->wiz.step = 1; }
    else if (strcmp(screen_name, "add_iface_3") == 0) { app->screen = OYK_SYSV_SCR_ADD_IFACE; app->wiz.step = 2; }
    else if (strcmp(screen_name, "add_iface_4") == 0) { app->screen = OYK_SYSV_SCR_ADD_IFACE; app->wiz.step = 3; }
    else if (strcmp(screen_name, "edit_iface") == 0) app->screen = OYK_SYSV_SCR_EDIT_IFACE;
    else if (strcmp(screen_name, "edit_peer") == 0) app->screen = OYK_SYSV_SCR_EDIT_PEER;
    else if (strcmp(screen_name, "terminate") == 0) app->screen = OYK_SYSV_SCR_TERMINATE;
    else if (strcmp(screen_name, "ban") == 0) app->screen = OYK_SYSV_SCR_BAN;
    else if (strcmp(screen_name, "exit") == 0) app->screen = OYK_SYSV_SCR_EXIT_PEERS;
    else if (strcmp(screen_name, "terminal") == 0) app->screen = OYK_SYSV_SCR_TERMINAL;
    else if (strcmp(screen_name, "terminal_help") == 0) app->screen = OYK_SYSV_SCR_TERM_HELP;
    else if (strcmp(screen_name, "files") == 0) app->screen = OYK_SYSV_SCR_FILES;
    else if (strcmp(screen_name, "about") == 0) app->screen = OYK_SYSV_SCR_ABOUT;
    else return 0;
    return 1;
}

void oyk_sysv_tui_to_ansi(const OykSysvCanvas *cv, int color_mode,
                          char *out, size_t cap) {
    size_t pos;
    int r;
    int c;
    int last_st;
    if (!cv || !out || cap == 0U) return;
    pos = 0U;
    out[0] = '\0';
    last_st = -1;
    if (pos + 8U < cap) {
        memcpy(out + pos, "\033[H\033[2J", 7U);
        pos += 7U;
    }
    for (r = 0; r < cv->rows; ++r) {
        char seq[24];
        size_t sl;
        sprintf(seq, "\033[%d;1H", r + 1);
        sl = strlen(seq);
        if (pos + sl + 1U < cap) {
            memcpy(out + pos, seq, sl);
            pos += sl;
        }
        for (c = 0; c < cv->cols; ++c) {
            int st;
            st = (int)cv->styles[r][c];
            if (st != last_st) {
                const char *esc;
                if (!color_mode) {
                    esc = (st == OYK_SYSV_ST_MENU_SEL || st == OYK_SYSV_ST_DLG_SEL ||
                           st == OYK_SYSV_ST_HEADER || st == OYK_SYSV_ST_ALLOW ||
                           st == OYK_SYSV_ST_DENY || st == OYK_SYSV_ST_LIST_HDR)
                        ? "\033[7m" : "\033[0m";
                } else if (st == OYK_SYSV_ST_ALLOW || st == OYK_SYSV_ST_OK) esc = "\033[1;37;42m";
                else if (st == OYK_SYSV_ST_DENY || st == OYK_SYSV_ST_CLOSE_BTN) esc = "\033[1;37;41m";
                else if (st == OYK_SYSV_ST_HEADER || st == OYK_SYSV_ST_MENU_SEL || st == OYK_SYSV_ST_DLG_SEL) esc = "\033[1;37;44m";
                else if (st == OYK_SYSV_ST_LIST_HDR || st == OYK_SYSV_ST_DLG_BAR || st == OYK_SYSV_ST_STATUS) esc = "\033[30;47m";
                else if (st == OYK_SYSV_ST_WARN) esc = "\033[1;33;40m";
                else esc = "\033[0;37;40m";
                sl = strlen(esc);
                if (pos + sl + 1U < cap) {
                    memcpy(out + pos, esc, sl);
                    pos += sl;
                }
                last_st = st;
            }
            if (pos + 2U < cap) out[pos++] = cv->chars[r][c];
        }
    }
    if (pos + 5U < cap) {
        memcpy(out + pos, "\033[0m", 4U);
        pos += 4U;
    }
    out[pos] = '\0';
}
