/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_tui.hpp"

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
        hint = term_active ? "Tab/F9:Term(ACTIVE)  F10/M:Menu " : "Tab/F9:Term  F10/M:Menu ";
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
