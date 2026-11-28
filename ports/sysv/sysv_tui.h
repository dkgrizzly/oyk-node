/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * Shared Strict ANSI C89 Canonical 23-Screen TUI Renderer
 * Renders into a flat OykSysvCanvas cell grid matching the AGENTS.md Canonical
 * TUI Layout Contract for System V curses (Motorola 88K, AT&T 3B2) and
 * OpenVMS SMG$/VT100 consoles.
 */

#ifndef OYK_SYSV_TUI_H
#define OYK_SYSV_TUI_H

#include "sysv_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OYK_SYSV_TUI_MAX_ROWS 50
#define OYK_SYSV_TUI_MAX_COLS 132

enum OykSysvTuiStyle {
    OYK_SYSV_ST_BG = 0,
    OYK_SYSV_ST_NORMAL,
    OYK_SYSV_ST_DIM,
    OYK_SYSV_ST_BORDER,
    OYK_SYSV_ST_TITLE,
    OYK_SYSV_ST_HEADER,
    OYK_SYSV_ST_MENU_BAR,
    OYK_SYSV_ST_MENU_SEL,
    OYK_SYSV_ST_LIST_HDR,
    OYK_SYSV_ST_STATUS,
    OYK_SYSV_ST_OK,
    OYK_SYSV_ST_ALLOW,
    OYK_SYSV_ST_DENY,
    OYK_SYSV_ST_WARN,
    OYK_SYSV_ST_ERROR,
    OYK_SYSV_ST_DLG_BORDER,
    OYK_SYSV_ST_DLG_FILL,
    OYK_SYSV_ST_DLG_SEL,
    OYK_SYSV_ST_DLG_INPUT,
    OYK_SYSV_ST_DLG_BAR,
    OYK_SYSV_ST_CLOSE_BTN
};

typedef struct {
    int top;
    int left;
    int h;
    int w;
} OykSysvRect;

typedef struct {
    int  rows;
    int  cols;
    char chars[OYK_SYSV_TUI_MAX_ROWS][OYK_SYSV_TUI_MAX_COLS];
    sysv_u8 styles[OYK_SYSV_TUI_MAX_ROWS][OYK_SYSV_TUI_MAX_COLS];
} OykSysvCanvas;

void oyk_sysv_canvas_init(OykSysvCanvas *cv, int rows, int cols);
void oyk_sysv_canvas_clear(OykSysvCanvas *cv, int style);
void oyk_sysv_canvas_put_text(OykSysvCanvas *cv, int row, int col, int max_w,
                              int style, const char *text);
void oyk_sysv_canvas_put_box(OykSysvCanvas *cv, int top, int left, int h, int w,
                             int border_style, int fill_style, int fill_interior);
void oyk_sysv_canvas_put_hdiv(OykSysvCanvas *cv, int row, int left, int w, int style);
void oyk_sysv_canvas_put_shadow(OykSysvCanvas *cv, int top, int left, int h, int w);
void oyk_sysv_canvas_row_text(const OykSysvCanvas *cv, int row, char *out, size_t cap);
int  oyk_sysv_canvas_find_text(const OykSysvCanvas *cv, const char *needle);

enum OykSysvKey {
    OYK_SYSV_KEY_NONE   = 0,
    OYK_SYSV_KEY_CTRL_A = 1,
    OYK_SYSV_KEY_TAB    = 9,
    OYK_SYSV_KEY_ENTER  = 13,
    OYK_SYSV_KEY_CTRL_N = 14,
    OYK_SYSV_KEY_ESC    = 27,
    OYK_SYSV_KEY_BS     = 127,
    OYK_SYSV_KEY_UP     = 1001,
    OYK_SYSV_KEY_DOWN,
    OYK_SYSV_KEY_LEFT,
    OYK_SYSV_KEY_RIGHT,
    OYK_SYSV_KEY_HOME,
    OYK_SYSV_KEY_END,
    OYK_SYSV_KEY_PGUP,
    OYK_SYSV_KEY_PGDN,
    OYK_SYSV_KEY_F9,
    OYK_SYSV_KEY_F10
};

/* Canonical button & modal helpers */
int  oyk_sysv_tui_close_btn_col(const OykSysvRect *r);
void oyk_sysv_tui_render(OykSysvCanvas *cv, const OykSysvApp *app, int rows, int cols);
int  oyk_sysv_is_modal_screen(int screen_id);
int  oyk_sysv_tui_handle_key(OykSysvApp *app, int key);

/* Maps all 23 canonical gallery screen names to OykSysvApp state */
int  oyk_sysv_tui_setup_screen(OykSysvApp *app, const char *screen_name);
void oyk_sysv_tui_to_ansi(const OykSysvCanvas *cv, int color_mode,
                          char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* OYK_SYSV_TUI_H */
