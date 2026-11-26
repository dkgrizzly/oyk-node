/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "dos_mouse.hpp"

#include <string.h>

#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
  #include <dos.h>
  #include <i86.h>
#endif

static void clamp_coords(OykDosMouse* m, int px_x, int px_y, int* out_row, int* out_col) {
    if (px_x < 0) px_x = 0;
    if (px_y < 0) px_y = 0;
    if (px_x >= m->max_px_w) px_x = m->max_px_w - 1;
    if (px_y >= m->max_px_h) px_y = m->max_px_h - 1;
    m->cur_px_x = px_x;
    m->cur_px_y = px_y;
    int c = (m->char_w > 0) ? (px_x / m->char_w) : 0;
    int r = (m->char_h > 0) ? (px_y / m->char_h) : 0;
    if (c < 0) c = 0;
    if (c > 79) c = 79;
    if (r < 0) r = 0;
    if (r > 24) r = 24;
    m->cur_col = c;
    m->cur_row = r;
    if (out_row) *out_row = r;
    if (out_col) *out_col = c;
}

int oyk_dos_mouse_init(OykDosMouse* m, int is_gfx, int max_w, int max_h, int char_w, int char_h) {
    if (!m) return 0;
    memset(m, 0, sizeof(*m));
    m->is_gfx = is_gfx ? 1 : 0;
    m->max_px_w = (max_w > 0) ? max_w : 640;
    m->max_px_h = (max_h > 0) ? max_h : 200;
    m->char_w = (char_w > 0) ? char_w : 8;
    m->char_h = (char_h > 0) ? char_h : 8;
    m->last_click_row = -1;
    m->last_click_col = -1;

#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
    union REGS r;
    memset(&r, 0, sizeof(r));
    r.x.ax = 0x0000U;
    int86(0x33, &r, &r);
    if (r.x.ax != 0xFFFFU) {
        return 0;
    }
    m->available = 1;

    /* Clamp horizontal/vertical virtual range to display dimensions */
    memset(&r, 0, sizeof(r));
    r.x.ax = 0x0007U;
    r.x.cx = 0U;
    r.x.dx = static_cast<oyk_u16>(m->max_px_w - 1);
    int86(0x33, &r, &r);

    memset(&r, 0, sizeof(r));
    r.x.ax = 0x0008U;
    r.x.cx = 0U;
    r.x.dx = static_cast<oyk_u16>(m->max_px_h - 1);
    int86(0x33, &r, &r);

    /* Query CuteMouse wheel extension (INT 33h AX=0011h) */
    memset(&r, 0, sizeof(r));
    r.x.ax = 0x0011U;
    int86(0x33, &r, &r);
    if (r.x.ax == 0x574DU && (r.x.cx & 1U) != 0U) {
        m->has_wheel = 1;
    }

    if (!m->is_gfx) {
        oyk_dos_mouse_show(m, 1);
    } else {
        /* Software arrow cursor drawn directly by OykDosGfx */
        oyk_dos_mouse_show(m, 0);
    }
    return 1;
#else
    return 0;
#endif
}

void oyk_dos_mouse_shutdown(OykDosMouse* m) {
    if (!m) return;
    if (m->available && !m->is_gfx) {
        oyk_dos_mouse_show(m, 0);
    }
    m->available = 0;
}

void oyk_dos_mouse_show(OykDosMouse* m, int visible) {
    if (!m || !m->available) return;
#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
    if (!m->is_gfx) {
        union REGS r;
        memset(&r, 0, sizeof(r));
        r.x.ax = visible ? 0x0001U : 0x0002U;
        int86(0x33, &r, &r);
    }
#else
    (void)visible;
#endif
}

void oyk_dos_mouse_push_sim(OykDosMouse* m, const OykDosMouseEvent* ev) {
    if (!m || !ev) return;
    int next = (m->sim_tail + 1) % 16;
    if (next == m->sim_head) return;
    m->sim_queue[m->sim_tail] = *ev;
    m->sim_tail = next;
}

int oyk_dos_mouse_poll(OykDosMouse* m, oyk_u32 now_ms, OykDosMouseEvent* out_ev) {
    if (!m || !out_ev) return 0;
    memset(out_ev, 0, sizeof(*out_ev));

    if (m->sim_head != m->sim_tail) {
        *out_ev = m->sim_queue[m->sim_head];
        m->sim_head = (m->sim_head + 1) % 16;
        if (out_ev->px_x == 0 && out_ev->px_y == 0 && (out_ev->col > 0 || out_ev->row > 0)) {
            out_ev->px_x = out_ev->col * m->char_w;
            out_ev->px_y = out_ev->row * m->char_h;
        }
        clamp_coords(m, out_ev->px_x, out_ev->px_y, &out_ev->row, &out_ev->col);
        return (out_ev->kind != OYK_MOUSE_NONE) ? 1 : 0;
    }

    if (!m->available) return 0;

#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
    union REGS r;
    memset(&r, 0, sizeof(r));
    r.x.ax = 0x0003U;
    int86(0x33, &r, &r);

    int buttons = static_cast<int>(r.h.bl & 0x07U);
    int wheel = 0;
    if (m->has_wheel) {
        signed char wraw = static_cast<signed char>(r.h.bh);
        if (wraw > 0) wheel = 1;
        else if (wraw < 0) wheel = -1;
    }

    int prev_x = m->cur_px_x;
    int prev_y = m->cur_px_y;
    int row = 0, col = 0;
    clamp_coords(m, static_cast<int>(r.x.cx), static_cast<int>(r.x.dx), &row, &col);

    int pressed = buttons & (~m->prev_buttons);
    int released = m->prev_buttons & (~buttons);
    m->prev_buttons = buttons;

    out_ev->row = row;
    out_ev->col = col;
    out_ev->px_x = m->cur_px_x;
    out_ev->px_y = m->cur_px_y;

    if (wheel != 0) {
        out_ev->kind = OYK_MOUSE_WHEEL;
        out_ev->wheel_delta = wheel;
        return 1;
    }
    if (pressed != 0) {
        if ((pressed & OYK_MOUSE_BTN_LEFT) &&
            m->last_click_row == row && m->last_click_col == col &&
            (now_ms - m->last_click_ms) <= 450UL) {
            out_ev->kind = OYK_MOUSE_DBLCLICK;
            m->last_click_row = -1;
        } else {
            out_ev->kind = OYK_MOUSE_DOWN;
            if (pressed & OYK_MOUSE_BTN_LEFT) {
                m->last_click_ms = now_ms;
                m->last_click_row = row;
                m->last_click_col = col;
            }
        }
        out_ev->button = pressed;
        return 1;
    }
    if (released != 0) {
        out_ev->kind = OYK_MOUSE_UP;
        out_ev->button = released;
        return 1;
    }
    if (m->cur_px_x != prev_x || m->cur_px_y != prev_y) {
        out_ev->kind = OYK_MOUSE_MOVE;
        return 1;
    }
#else
    (void)now_ms;
#endif
    return 0;
}
