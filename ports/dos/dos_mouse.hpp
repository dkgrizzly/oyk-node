/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_DOS_MOUSE_HPP
#define OYK_DOS_MOUSE_HPP

/*
 * Optional MS-DOS Mouse Driver Interface (INT 33h + CuteMouse wheel AX=0011h).
 * Gracefully reports available() == 0 when no mouse TSR is loaded.
 */

#include "../compat/oyk_portable.hpp"

#define OYK_MOUSE_NONE     0
#define OYK_MOUSE_MOVE     1
#define OYK_MOUSE_DOWN     2
#define OYK_MOUSE_UP       3
#define OYK_MOUSE_DBLCLICK 4
#define OYK_MOUSE_WHEEL    5

#define OYK_MOUSE_BTN_LEFT   1
#define OYK_MOUSE_BTN_RIGHT  2
#define OYK_MOUSE_BTN_MIDDLE 4

struct OykDosMouseEvent {
    int kind;
    int row;
    int col;
    int px_x;
    int px_y;
    int button;
    int wheel_delta;
};

struct OykDosMouse {
    int available;
    int has_wheel;
    int is_gfx;
    int max_px_w;
    int max_px_h;
    int char_w;
    int char_h;
    int cols;
    int rows;

    int cur_px_x;
    int cur_px_y;
    int cur_row;
    int cur_col;
    int prev_buttons;

    oyk_u32 last_click_ms;
    int last_click_row;
    int last_click_col;

    OykDosMouseEvent sim_queue[16];
    int sim_head;
    int sim_tail;
};

int  oyk_dos_mouse_init(OykDosMouse* m, int is_gfx, int max_w, int max_h, int char_w, int char_h);
void oyk_dos_mouse_shutdown(OykDosMouse* m);
void oyk_dos_mouse_show(OykDosMouse* m, int visible);
int  oyk_dos_mouse_poll(OykDosMouse* m, oyk_u32 now_ms, OykDosMouseEvent* out_ev);
void oyk_dos_mouse_push_sim(OykDosMouse* m, const OykDosMouseEvent* ev);

#endif /* OYK_DOS_MOUSE_HPP */
