/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_tui.hpp"
#include "oyk_gui.hpp"
#include "oyk_logo_data.hpp"
#include "oyk_term.hpp"

#include <stdio.h>
#include <string.h>

#ifdef TEENSYDUINO
#define OYK_TUI_FLASHMEM __attribute__((section(".flashmem")))
#else
#define OYK_TUI_FLASHMEM
#endif

OYK_TUI_FLASHMEM void oyk_tui_compute_console_layout(int rows, int cols, OykTuiConsoleLayout* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->rows = rows;
    out->cols = cols;
    out->menu_row = 0;
    out->banner_row = 1;
    out->status_row = (rows > 1) ? (rows - 1) : 0;

    if (rows < 16 || cols < 60) {
        out->too_small = 1;
        return;
    }

    int avail_h = rows - 3; /* excludes row 0 menu, row 1 banner, row rows-1 status */
    int top_h = (rows >= 30) ? 8 : 7;
    if (top_h > avail_h - 6) top_h = 6;

    int conn_h = 7;
    if (rows >= 36) conn_h = 9;
    else if (rows >= 29) conn_h = 8;
    if (conn_h > avail_h - top_h - 4) {
        conn_h = avail_h - top_h - 4;
        if (conn_h < 4) conn_h = 4;
    }

    int log_h = avail_h - top_h - conn_h;
    if (log_h < 4) {
        log_h = 4;
        if (top_h + conn_h + log_h > avail_h) {
            conn_h = avail_h - top_h - log_h;
            if (conn_h < 3) conn_h = 3;
        }
    }

    int left_w = (cols * 56) / 100;
    if (left_w < 34) left_w = 34;
    if (left_w > cols - 26) left_w = cols - 26;
    int right_w = cols - left_w + 1; /* overlaps 1 column on shared center vertical divider */

    out->iface_box.top = 2;
    out->iface_box.left = 0;
    out->iface_box.h = top_h;
    out->iface_box.w = left_w;

    out->heard_box.top = 2;
    out->heard_box.left = left_w - 1;
    out->heard_box.h = top_h;
    out->heard_box.w = right_w;

    out->conn_box.top = 2 + top_h;
    out->conn_box.left = 0;
    out->conn_box.h = conn_h;
    out->conn_box.w = cols;

    out->log_box.top = out->conn_box.top + conn_h;
    out->log_box.left = 0;
    out->log_box.h = (rows - 1) - out->log_box.top;
    out->log_box.w = cols;

    /* 2 border rows + 1 list-header row inside iface, heard, conn; log box has 2 border rows */
    out->iface_vis_rows = (top_h > 3) ? (top_h - 3) : 1;
    out->heard_vis_rows = (top_h > 3) ? (top_h - 3) : 1;
    out->conn_vis_rows  = (conn_h > 3) ? (conn_h - 3) : 1;
    out->log_vis_rows   = (out->log_box.h > 2) ? (out->log_box.h - 2) : 1;
}

OYK_TUI_FLASHMEM void oyk_tui_compute_term_layout(int rows, int cols, OykTuiTermLayout* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (rows < 6) rows = 6;
    if (cols < 20) cols = 20;
    out->rows = rows;
    out->cols = cols;
    out->header_row = 0;
    out->body_top = 1;
    out->hotkey_row = rows - 2;
    out->input_row = rows - 1;
    out->body_rows = rows - 3;
    if (out->body_rows < 1) out->body_rows = 1;
}

OYK_TUI_FLASHMEM void oyk_tui_center_modal(int screen_rows, int screen_cols,
                                           int pref_h, int pref_w, OykTuiRect* out) {
    if (!out) return;
    int max_h = (screen_rows > 2) ? (screen_rows - 2) : screen_rows;
    int max_w = (screen_cols > 4) ? (screen_cols - 4) : screen_cols;
    int h = (pref_h <= max_h) ? pref_h : max_h;
    int w = (pref_w <= max_w) ? pref_w : max_w;
    if (h < 5) h = 5;
    if (w < 16) w = 16;
    int top = (screen_rows - h) / 2;
    int left = (screen_cols - w) / 2;
    if (top < 1) top = 1;
    if (left < 1) left = 1;
    out->top = top;
    out->left = left;
    out->h = h;
    out->w = w;
}

OYK_TUI_FLASHMEM int oyk_tui_close_btn_col(const OykTuiRect* rect) {
    if (!rect || rect->w < 12) return -1;
    return rect->left + rect->w - 5;
}

OYK_TUI_FLASHMEM int oyk_tui_hit_close_btn(const OykTuiRect* rect, int row, int col) {
    int c = oyk_tui_close_btn_col(rect);
    if (c < 0) return 0;
    return (row == rect->top && col >= c && col <= c + 2) ? 1 : 0;
}

OYK_TUI_FLASHMEM int oyk_tui_point_in_rect(const OykTuiRect* rect, int row, int col) {
    if (!rect) return 0;
    return (row >= rect->top && row < rect->top + rect->h &&
            col >= rect->left && col < rect->left + rect->w) ? 1 : 0;
}

OYK_TUI_FLASHMEM void oyk_tui_draw_menu_bar(const OykTuiBackend* b, int cols,
                                            int term_active, const char* right_hint) {
    if (!b || !b->put_text || cols <= 0) return;
    b->put_text(b->ctx, 0, 0, cols, OYK_TUI_STYLE_MENU_BAR, "");
    b->put_text(b->ctx, 0, 1, 6, OYK_TUI_STYLE_MENU_SEL, " Node ");
    const char* hint = right_hint;
    if (!hint || !hint[0]) {
        hint = term_active ? "Tab/F9:Term(ACTIVE)  Ctrl-N:Menu " : "Tab/F9:Term  Ctrl-N:Menu ";
    }
    int hlen = static_cast<int>(strlen(hint));
    int start_col = cols - hlen - 1;
    if (start_col < 8) start_col = 8;
    if (start_col < cols) {
        b->put_text(b->ctx, 0, start_col, cols - start_col, OYK_TUI_STYLE_MENU_BAR, hint);
    }
}

OYK_TUI_FLASHMEM void oyk_tui_draw_status_banner(const OykTuiBackend* b, int cols,
                                                 const char* left_text,
                                                 int incoming_allow, int compact_pill) {
    if (!b || !b->put_text || cols <= 0) return;
    const char* pill = compact_pill
        ? (incoming_allow ? " [IN:ALLOW] " : " [IN:DENY]  ")
        : (incoming_allow ? " [INCOMING: ALLOW] " : " [INCOMING: DENY]  ");
    int plen = static_cast<int>(strlen(pill));
    int left_w = cols - plen;
    if (left_w < 10) left_w = cols;
    b->put_text(b->ctx, 1, 0, left_w, OYK_TUI_STYLE_HEADER, left_text ? left_text : "");
    if (cols > plen) {
        b->put_text(b->ctx, 1, cols - plen, plen,
                    incoming_allow ? OYK_TUI_STYLE_ALLOW : OYK_TUI_STYLE_DENY, pill);
    }
}

OYK_TUI_FLASHMEM void oyk_tui_draw_status_bar(const OykTuiBackend* b, int row,
                                              int cols, const char* text) {
    if (!b || !b->put_text || row < 0 || cols <= 0) return;
    b->put_text(b->ctx, row, 0, cols, OYK_TUI_STYLE_STATUS, text ? text : "");
}

OYK_TUI_FLASHMEM void oyk_tui_draw_modal(const OykTuiBackend* b, const OykTuiRect* rect,
                                         const char* title, int with_close_btn) {
    if (!b || !rect || !b->put_box) return;
    b->put_box(b->ctx, rect->top, rect->left, rect->h, rect->w,
               OYK_TUI_STYLE_DLG_BORDER, OYK_TUI_STYLE_DLG_FILL, 1);

    int close_c = with_close_btn ? oyk_tui_close_btn_col(rect) : -1;
    int max_title = (close_c >= 0) ? (close_c - (rect->left + 2) - 1) : (rect->w - 4);
    if (title && title[0] && max_title > 2 && b->put_text) {
        char buf[136];
        int tlen = static_cast<int>(strlen(title));
        if (tlen > max_title - 2) tlen = max_title - 2;
        if (tlen > 130) tlen = 130;
        buf[0] = ' ';
        memcpy(buf + 1, title, static_cast<size_t>(tlen));
        buf[1 + tlen] = ' ';
        buf[2 + tlen] = '\0';
        b->put_text(b->ctx, rect->top, rect->left + 2, tlen + 2, OYK_TUI_STYLE_DLG_BORDER, buf);
    }
    if (close_c >= 0 && b->put_text) {
        b->put_text(b->ctx, rect->top, close_c, 3, OYK_TUI_STYLE_CLOSE_BTN, "[X]");
    }
    if (b->put_shadow) {
        b->put_shadow(b->ctx, rect->top, rect->left, rect->h, rect->w);
    }
}

OYK_TUI_FLASHMEM void oyk_tui_draw_tabs(const OykTuiBackend* b, int row, int left, int inner_w,
                                        const char* const* labels, int count, int active_idx) {
    if (!b || !b->put_text || !labels || count <= 0 || inner_w <= 0) return;
    b->put_text(b->ctx, row, left, inner_w, OYK_TUI_STYLE_DLG_FILL, "");
    int c = left + 1;
    int end_c = left + inner_w;
    for (int i = 0; i < count && c < end_c; ++i) {
        char item[32];
        int llen = static_cast<int>(strlen(labels[i]));
        if (llen > 26) llen = 26;
        if (i == active_idx) {
            item[0] = '[';
            memcpy(item + 1, labels[i], static_cast<size_t>(llen));
            item[1 + llen] = ']';
            item[2 + llen] = '\0';
        } else {
            item[0] = ' ';
            memcpy(item + 1, labels[i], static_cast<size_t>(llen));
            item[1 + llen] = ' ';
            item[2 + llen] = '\0';
        }
        int w = llen + 2;
        if (c + w > end_c) w = end_c - c;
        if (w > 0) {
            b->put_text(b->ctx, row, c, w,
                        (i == active_idx) ? OYK_TUI_STYLE_DLG_SEL : OYK_TUI_STYLE_DLG_BAR, item);
        }
        c += w + 1;
    }
}

OYK_TUI_FLASHMEM void oyk_tui_draw_modal_footer(const OykTuiBackend* b, const OykTuiRect* rect,
                                                const char* feedback, const char* keys_hint) {
    if (!b || !rect || rect->h < 5 || rect->w < 10) return;
    int inner_w = rect->w - 2;
    if (b->put_text && feedback && feedback[0]) {
        b->put_text(b->ctx, rect->top + rect->h - 4, rect->left + 2, inner_w - 2,
                    OYK_TUI_STYLE_WARN, feedback);
    }
    if (b->put_hdiv) {
        b->put_hdiv(b->ctx, rect->top + rect->h - 3, rect->left, rect->w, OYK_TUI_STYLE_DLG_BORDER);
    }
    if (b->put_text && keys_hint) {
        b->put_text(b->ctx, rect->top + rect->h - 2, rect->left + 2, inner_w - 2,
                    OYK_TUI_STYLE_DLG_BAR, keys_hint);
    }
}

OYK_TUI_FLASHMEM void oyk_tui_draw_dropdown_menu(const OykTuiBackend* b, const OykTuiRect* rect,
                                                 const char* const* items, const int* item_rows,
                                                 int item_count, const int* sep_rows, int sep_count,
                                                 int sel_idx) {
    if (!b || !rect || !b->put_box) return;
    b->put_box(b->ctx, rect->top, rect->left, rect->h, rect->w,
               OYK_TUI_STYLE_DLG_BORDER, OYK_TUI_STYLE_DLG_FILL, 1);
    if (b->put_hdiv && sep_rows) {
        for (int s = 0; s < sep_count; ++s) {
            b->put_hdiv(b->ctx, rect->top + sep_rows[s], rect->left, rect->w, OYK_TUI_STYLE_DLG_BORDER);
        }
    }
    if (b->put_text && items && item_rows) {
        int inner_w = rect->w - 2;
        for (int i = 0; i < item_count; ++i) {
            b->put_text(b->ctx, rect->top + item_rows[i], rect->left + 1, inner_w,
                        (i == sel_idx) ? OYK_TUI_STYLE_DLG_SEL : OYK_TUI_STYLE_DLG_FILL,
                        items[i] ? items[i] : "");
        }
    }
    if (b->put_shadow) {
        b->put_shadow(b->ctx, rect->top, rect->left, rect->h, rect->w);
    }
}

OYK_TUI_FLASHMEM int oyk_tui_button_width(const char* label) {
    if (!label) return 6;
    return static_cast<int>(strlen(label)) + 6; /* " [ " + label + " ] " */
}

OYK_TUI_FLASHMEM void oyk_tui_draw_button(const OykTuiBackend* b, int row, int col,
                                          const char* label, int focused, int enabled) {
    if (!b || !b->put_text || !label) return;
    int w = oyk_tui_button_width(label);
    char buf[48];
    if (w > 46) w = 46;
    if (enabled) {
        sprintf(buf, " [ %.38s ] ", label);
        b->put_text(b->ctx, row, col, w,
                    focused ? OYK_TUI_STYLE_DLG_SEL : OYK_TUI_STYLE_DLG_BAR, buf);
    } else {
        memset(buf, ' ', static_cast<size_t>(w));
        buf[w] = '\0';
        b->put_text(b->ctx, row, col, w, OYK_TUI_STYLE_DLG_FILL, buf);
    }
}

OYK_TUI_FLASHMEM int oyk_tui_draw_button_row(const OykTuiBackend* b, int row, int start_col,
                                             int max_w, const OykTuiButton* buttons, int count,
                                             int focused_idx, const char* trailing_hint) {
    if (!b || !b->put_text || row < 0 || max_w <= 0) return start_col;
    b->put_text(b->ctx, row, start_col, max_w, OYK_TUI_STYLE_DLG_FILL, "");
    int c = start_col;
    int end_c = start_col + max_w;
    for (int i = 0; i < count && buttons; ++i) {
        const char* lbl = buttons[i].label ? buttons[i].label : "";
        int bw = oyk_tui_button_width(lbl);
        if (c + bw > end_c) break;
        oyk_tui_draw_button(b, row, c, lbl, (i == focused_idx) ? 1 : 0, buttons[i].enabled);
        c += bw + 2; /* Keep 2 gap cells unpainted in DLG_FILL between buttons */
    }
    if (trailing_hint && trailing_hint[0] && c < end_c) {
        b->put_text(b->ctx, row, c, end_c - c, OYK_TUI_STYLE_DLG_FILL, trailing_hint);
    }
    return c;
}

OYK_TUI_FLASHMEM int oyk_tui_hit_button_row(int row, int start_col, int max_w,
                                            const OykTuiButton* buttons, int count,
                                            int click_row, int click_col) {
    if (click_row != row || !buttons || count <= 0 || max_w <= 0) return -1;
    int c = start_col;
    int end_c = start_col + max_w;
    for (int i = 0; i < count; ++i) {
        const char* lbl = buttons[i].label ? buttons[i].label : "";
        int bw = oyk_tui_button_width(lbl);
        if (c + bw > end_c) break;
        if (buttons[i].enabled && click_col >= c && click_col < c + bw) {
            return i;
        }
        c += bw + 2;
    }
    return -1;
}

OYK_TUI_FLASHMEM void oyk_tui_compute_dual_pane_layout(int screen_rows, int screen_cols,
                                                       OykTuiDualPaneLayout* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    oyk_tui_center_modal(screen_rows, screen_cols, 21, 76, &out->modal);
    const OykTuiRect& m = out->modal;
    int inner_w = m.w - 2;
    int left_w = inner_w / 2;
    int right_w = inner_w - left_w;

    out->path_row = m.top + 1;
    out->hdr_row  = m.top + 2;
    out->list_top = m.top + 3;
    out->feedback_row = m.top + m.h - 4;
    out->div_row  = m.top + m.h - 3;
    out->btn_row  = m.top + m.h - 2;
    out->list_rows = out->feedback_row - out->list_top;
    if (out->list_rows < 1) out->list_rows = 1;

    out->left_pane.top  = out->path_row;
    out->left_pane.left = m.left + 1;
    out->left_pane.h    = out->feedback_row - out->path_row;
    out->left_pane.w    = left_w;

    out->right_pane.top  = out->path_row;
    out->right_pane.left = m.left + 1 + left_w;
    out->right_pane.h    = out->feedback_row - out->path_row;
    out->right_pane.w    = right_w;
}

OYK_TUI_FLASHMEM void oyk_tui_draw_term_help_modal_ex(const OykTuiBackend* b,
                                                      int screen_rows, int screen_cols,
                                                      int with_close_btn) {
    if (!b || !b->put_text) return;
    OykTuiRect mr;
    oyk_tui_center_modal(screen_rows, screen_cols, 19, 68, &mr);
    oyk_tui_draw_modal(b, &mr, "Terminal Hotkey Help", with_close_btn);
    int inner_w = mr.w - 4;
    b->put_text(b->ctx, mr.top + 1, mr.left + 2, inner_w, OYK_TUI_STYLE_TITLE,
                "Command prefix: Ctrl-A (press Ctrl-A, then hotkey)");
    if (b->put_hdiv) {
        b->put_hdiv(b->ctx, mr.top + 2, mr.left, mr.w, OYK_TUI_STYLE_DLG_BORDER);
    }
    int n = oyk_term_help_count();
    for (int i = 0; i < n && (mr.top + 3 + i) < (mr.top + mr.h - 3); ++i) {
        const OykTermHelpEntry* e = oyk_term_help_entry(i);
        if (!e) continue;
        char row[80];
        sprintf(row, "%-8.8s %-38.38s %-12.12s", e->key_label, e->action, e->alt_key);
        b->put_text(b->ctx, mr.top + 3 + i, mr.left + 2, inner_w, OYK_TUI_STYLE_DLG_FILL, row);
    }
    oyk_tui_draw_modal_footer(b, &mr, "",
                              with_close_btn
                                  ? "Press hotkey to run, or Esc / Enter / [X] to close"
                                  : "Press hotkey to run, or Esc / Enter to close");
}

OYK_TUI_FLASHMEM void oyk_tui_draw_term_help_modal(const OykTuiBackend* b,
                                                   int screen_rows, int screen_cols) {
    oyk_tui_draw_term_help_modal_ex(b, screen_rows, screen_cols, 1);
}

OYK_TUI_FLASHMEM void oyk_tui_draw_about_body(const OykTuiBackend* b, const OykTuiRect* rect,
                                              const char* platform_line) {
    if (!b || !b->put_text || !rect || rect->h < 10 || rect->w < 36) return;
    const int logo_col = rect->left + 2;
    const int logo_rows = (rect->h - 5 < OYK_LOGO_ASCII_ROWS) ? (rect->h - 5) : OYK_LOGO_ASCII_ROWS;
    for (int i = 0; i < logo_rows; ++i) {
        b->put_text(b->ctx, rect->top + 2 + i, logo_col, OYK_LOGO_ASCII_COLS,
                    OYK_TUI_STYLE_TITLE, kOykLogoAscii[i]);
    }
    const int text_col = rect->left + 2 + OYK_LOGO_ASCII_COLS + 2;
    const int text_w = (rect->left + rect->w - 2) - text_col;
    if (text_w <= 4) return;

    const char* lines[8] = {
        "OYK Amateur Radio Packet Node (OYK-Node)",
        "Copyright (C) 2026 KR4OYK - David Kuder",
        "License: GPL-3.0-or-later",
        "",
        (platform_line && platform_line[0]) ? platform_line : "Platform: Portable Node",
        "",
        "AX.25 v2.2 / KISS / AXUDP / AXIP / Telnet / YAPP",
        "Hotkeys: Ctrl-N Menu | Tab/F9 Term | Esc Close"
    };
    const int body_rows = rect->h - 5;
    for (int i = 0; i < 8 && i < body_rows; ++i) {
        if (lines[i][0]) {
            b->put_text(b->ctx, rect->top + 2 + i, text_col, text_w,
                        (i == 0) ? OYK_TUI_STYLE_TITLE : OYK_TUI_STYLE_DLG_FILL, lines[i]);
        }
    }
}

OYK_TUI_FLASHMEM void oyk_tui_draw_about_modal_ex(const OykTuiBackend* b, int screen_rows, int screen_cols,
                                                  const char* platform_line, int with_close_btn) {
    if (!b || !b->put_text) return;
    OykTuiRect mr;
    oyk_tui_center_modal(screen_rows, screen_cols, 16, 72, &mr);
    oyk_tui_draw_modal(b, &mr, "About OYK Amateur Radio Packet Node", with_close_btn);
    oyk_tui_draw_about_body(b, &mr, platform_line);
    if (b->put_hdiv) {
        b->put_hdiv(b->ctx, mr.top + mr.h - 3, mr.left, mr.w, OYK_TUI_STYLE_DLG_BORDER);
    }
    OykTuiButton abtns[1];
    abtns[0].label = "Close";
    abtns[0].enabled = 1;
    oyk_tui_draw_button_row(b, mr.top + mr.h - 2, mr.left + 2, mr.w - 4,
                            abtns, 1, 0, "Enter / Esc: Close");
}

OYK_TUI_FLASHMEM void oyk_tui_draw_about_modal(const OykTuiBackend* b, int screen_rows, int screen_cols,
                                               const char* platform_line) {
    oyk_tui_draw_about_modal_ex(b, screen_rows, screen_cols, platform_line, 1);
}

static void tui_format_fm_entry_row(const OykBbsDirEntry* e, int width,
                                    char* out, size_t cap) {
    char sz[16];
    int name_w;
    if (!e || !out || cap == 0 || width <= 0) return;
    if (e->is_parent) {
        strcpy(sz, "<UP>");
    } else if (e->is_dir) {
        strcpy(sz, "<DIR>");
    } else if (e->size >= 1024UL) {
        sprintf(sz, "%luK", static_cast<unsigned long>(e->size / 1024UL));
    } else {
        sprintf(sz, "%luB", static_cast<unsigned long>(e->size));
    }
    name_w = (width > 8) ? (width - 8) : 4;
    if (name_w > 32) name_w = 32;
    sprintf(out, "%-*.*s %6.6s", name_w, name_w, e->name, sz);
}

static void tui_draw_fm_pane(const OykTuiBackend* b, const OykGuiFilePane* p,
                             const char* label, int active, int pane_left,
                             int pane_w, int path_row, int hdr_row,
                             int list_top, int list_rows) {
    char tit[48];
    char hdr[48];
    oyk_gui_filemgr_pane_title(p, label, tit, sizeof(tit));
    b->put_text(b->ctx, path_row, pane_left, pane_w,
                active ? OYK_TUI_STYLE_TITLE : OYK_TUI_STYLE_DIM, tit);
    int nw = (pane_w > 8) ? (pane_w - 8) : 4;
    sprintf(hdr, "%-*.*s %6.6s", nw, nw, "Name", "Size");
    b->put_text(b->ctx, hdr_row, pane_left, pane_w, OYK_TUI_STYLE_LIST_HDR, hdr);
    int scroll = p->scroll;
    if (p->sel < scroll) scroll = p->sel;
    if (p->sel >= scroll + list_rows) scroll = p->sel - list_rows + 1;
    if (scroll < 0) scroll = 0;
    for (int i = 0; i < list_rows; ++i) {
        int idx = scroll + i;
        if (idx < p->count) {
            char row[48];
            tui_format_fm_entry_row(&p->entries[idx], pane_w, row, sizeof(row));
            int st = (active && idx == p->sel) ? OYK_TUI_STYLE_DLG_SEL
                                               : OYK_TUI_STYLE_DLG_FILL;
            b->put_text(b->ctx, list_top + i, pane_left, pane_w, st, row);
        } else if (i == 0 && p->count == 0) {
            b->put_text(b->ctx, list_top, pane_left, pane_w,
                        OYK_TUI_STYLE_DIM, "(empty directory)");
        } else {
            b->put_text(b->ctx, list_top + i, pane_left, pane_w,
                        OYK_TUI_STYLE_DLG_FILL, "");
        }
    }
}

OYK_TUI_FLASHMEM void oyk_tui_draw_dual_pane_modal_ex(const OykTuiBackend* b,
                                                      int screen_rows, int screen_cols,
                                                      const OykGuiFileMgr* fm, int focused_btn,
                                                      const char* title, const char* preview_text,
                                                      int preview_active, int with_close_btn) {
    if (!b || !b->put_text || !fm) return;
    OykTuiDualPaneLayout lay;
    oyk_tui_compute_dual_pane_layout(screen_rows, screen_cols, &lay);
    oyk_tui_draw_modal(b, &lay.modal, title ? title : "Files Area Manager", with_close_btn);

    tui_draw_fm_pane(b, &fm->inc, "Incoming", fm->active_pane == 0,
                     lay.left_pane.left, lay.left_pane.w,
                     lay.path_row, lay.hdr_row, lay.list_top, lay.list_rows);
    for (int r = lay.path_row; r < lay.list_top + lay.list_rows; ++r) {
        b->put_text(b->ctx, r, lay.left_pane.left + lay.left_pane.w, 1,
                    OYK_TUI_STYLE_DLG_BORDER, "|");
    }
    tui_draw_fm_pane(b, &fm->files, "Files", fm->active_pane == 1,
                     lay.right_pane.left, lay.right_pane.w,
                     lay.path_row, lay.hdr_row, lay.list_top, lay.list_rows);

    b->put_text(b->ctx, lay.feedback_row, lay.modal.left + 2, lay.modal.w - 4,
                OYK_TUI_STYLE_WARN, fm->feedback[0] ? fm->feedback : "Tab/Lt/Rt:Pane  Enter:Open  Bksp:Up");
    if (b->put_hdiv) {
        b->put_hdiv(b->ctx, lay.div_row, lay.modal.left, lay.modal.w,
                    OYK_TUI_STYLE_DLG_BORDER);
    }
    OykTuiButton btns[4];
    btns[0].label = "Upload";  btns[0].enabled = 1;
    btns[1].label = "Approve"; btns[1].enabled = (fm->active_pane == 0) ? 1 : 0;
    btns[2].label = "View";    btns[2].enabled = 1;
    btns[3].label = "Close";   btns[3].enabled = 1;
    oyk_tui_draw_button_row(b, lay.btn_row, lay.modal.left + 2, lay.modal.w - 4,
                            btns, 4, focused_btn, "M:Ren N:Dir D:Del");

    if (preview_active && preview_text) {
        OykTuiRect pm;
        oyk_tui_center_modal(screen_rows, screen_cols, 15, 68, &pm);
        oyk_tui_draw_modal(b, &pm, "File Preview", with_close_btn);
        const char* p = preview_text;
        for (int r = 0; r < pm.h - 4 && *p; ++r) {
            char line[66];
            int k = 0;
            while (*p && *p != '\n' && *p != '\r' && k < 62) {
                line[k++] = (*p >= 32 && *p < 127) ? *p : '.';
                ++p;
            }
            line[k] = '\0';
            if (*p == '\r') ++p;
            if (*p == '\n') ++p;
            b->put_text(b->ctx, pm.top + 1 + r, pm.left + 2, pm.w - 4,
                        OYK_TUI_STYLE_DLG_FILL, line);
        }
        oyk_tui_draw_modal_footer(b, &pm, "",
                                  with_close_btn ? "Esc / Enter / [X]: Close Preview"
                                                 : "Esc / Enter: Close Preview");
    }
}

OYK_TUI_FLASHMEM void oyk_tui_draw_dual_pane_modal(const OykTuiBackend* b,
                                                   int screen_rows, int screen_cols,
                                                   const OykGuiFileMgr* fm, int focused_btn,
                                                   const char* title, const char* preview_text,
                                                   int preview_active) {
    oyk_tui_draw_dual_pane_modal_ex(b, screen_rows, screen_cols, fm, focused_btn,
                                    title, preview_text, preview_active, 1);
}
