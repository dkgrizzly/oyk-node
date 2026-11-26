/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_TUI_HPP
#define OYK_TUI_HPP

/*
 * Shared C++98 / 16-bit-safe TUI Middle Layer for all text & framebuffer ports
 * (POSIX ncurses, MS-DOS, MS-DOS Graphical, TinyTMUX VT100, and Raspberry Pi Pico).
 *
 * Computes full-display dynamic layouts for the Operator Console and Local Terminal,
 * draws canonical drop-down Node menus, floating modals with [X] close buttons and
 * L-shaped drop shadows, and provides uniform coordinate hit-testing.
 */

#include "oyk_portable.hpp"

enum OykTuiStyle {
    OYK_TUI_STYLE_BG = 0,
    OYK_TUI_STYLE_NORMAL,
    OYK_TUI_STYLE_DIM,
    OYK_TUI_STYLE_BORDER,
    OYK_TUI_STYLE_TITLE,
    OYK_TUI_STYLE_HEADER,
    OYK_TUI_STYLE_MENU_BAR,
    OYK_TUI_STYLE_MENU_SEL,
    OYK_TUI_STYLE_LIST_HDR,
    OYK_TUI_STYLE_STATUS,
    OYK_TUI_STYLE_OK,
    OYK_TUI_STYLE_ALLOW,
    OYK_TUI_STYLE_DENY,
    OYK_TUI_STYLE_WARN,
    OYK_TUI_STYLE_ERROR,
    OYK_TUI_STYLE_DLG_BORDER,
    OYK_TUI_STYLE_DLG_FILL,
    OYK_TUI_STYLE_DLG_SEL,
    OYK_TUI_STYLE_DLG_INPUT,
    OYK_TUI_STYLE_DLG_BAR,
    OYK_TUI_STYLE_CLOSE_BTN
};

struct OykTuiRect {
    int top;
    int left;
    int h;
    int w;
};

struct OykTuiConsoleLayout {
    int rows;
    int cols;
    int too_small;
    int menu_row;
    int banner_row;
    int status_row;
    OykTuiRect iface_box;
    OykTuiRect heard_box;
    OykTuiRect conn_box;
    OykTuiRect log_box;
    int iface_vis_rows;
    int heard_vis_rows;
    int conn_vis_rows;
    int log_vis_rows;
};

struct OykTuiTermLayout {
    int rows;
    int cols;
    int header_row;
    int body_top;
    int body_rows;
    int hotkey_row;
    int input_row;
};

struct OykTuiBackend {
    void* ctx;
    void (*get_size)(void* ctx, int* out_rows, int* out_cols);
    void (*clear)(void* ctx, int style);
    void (*put_text)(void* ctx, int row, int col, int max_w, int style, const char* text);
    void (*put_box)(void* ctx, int top, int left, int h, int w,
                    int border_style, int fill_style, int fill_interior);
    void (*put_hdiv)(void* ctx, int row, int left, int w, int style);
    void (*put_shadow)(void* ctx, int top, int left, int h, int w);
    void (*present)(void* ctx);
};

/* Layout calculators filling all available display dimensions */
void oyk_tui_compute_console_layout(int rows, int cols, OykTuiConsoleLayout* out);
void oyk_tui_compute_term_layout(int rows, int cols, OykTuiTermLayout* out);
void oyk_tui_center_modal(int screen_rows, int screen_cols, int pref_h, int pref_w, OykTuiRect* out);

/* Top-right [X] close button geometry & hit-testing */
int  oyk_tui_close_btn_col(const OykTuiRect* rect);
int  oyk_tui_hit_close_btn(const OykTuiRect* rect, int row, int col);
int  oyk_tui_point_in_rect(const OykTuiRect* rect, int row, int col);

/* Shared UI Chrome & Widgets */
void oyk_tui_draw_menu_bar(const OykTuiBackend* b, int cols, int term_active, const char* right_hint);
void oyk_tui_draw_status_banner(const OykTuiBackend* b, int cols, const char* left_text,
                                int incoming_allow, int compact_pill);
void oyk_tui_draw_status_bar(const OykTuiBackend* b, int row, int cols, const char* text);
void oyk_tui_draw_modal(const OykTuiBackend* b, const OykTuiRect* rect,
                        const char* title, int with_close_btn);
void oyk_tui_draw_tabs(const OykTuiBackend* b, int row, int left, int inner_w,
                       const char* const* labels, int count, int active_idx);
void oyk_tui_draw_modal_footer(const OykTuiBackend* b, const OykTuiRect* rect,
                               const char* feedback, const char* keys_hint);
void oyk_tui_draw_dropdown_menu(const OykTuiBackend* b, const OykTuiRect* rect,
                                const char* const* items, const int* item_rows, int item_count,
                                const int* sep_rows, int sep_count, int sel_idx);

#endif /* OYK_TUI_HPP */
