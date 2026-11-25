/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "dos_video.hpp"

#include <stdio.h>
#include <string.h>

#if defined(__DOS__) || defined(MSDOS) || defined(__MSDOS__) || defined(_MSDOS)
#include <conio.h>
#include <dos.h>
#include <i86.h>
#endif

static oyk_u16 make_cell(oyk_u8 attr, oyk_u8 ch) {
    return static_cast<oyk_u16>((static_cast<oyk_u16>(attr) << 8) |
                                static_cast<oyk_u16>(ch));
}

void dos_vid_init_sim(OykDosVideo* v, int is_color, oyk_u16* sim_vram) {
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->vseg = is_color ? 0xB800U : 0xB000U;
    v->is_color = is_color ? 1 : 0;
    v->is_ega_vga = 0;
    v->orig_cga_mode = 0;
    v->orig_cursor = 0x0607U;
    v->sim_vram = sim_vram;
    v->last_flush_writes = 0;
    dos_vid_clear(v, 0x07U);
    for (int i = 0; i < OYK_DOS_VID_CELLS; ++i) {
        v->front[i] = 0xFFFFU;
    }
}

void dos_vid_init(OykDosVideo* v) {
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->vseg = 0xB800U;
    v->is_color = 1;

#if defined(__DOS__) || defined(MSDOS) || defined(__MSDOS__) || defined(_MSDOS)
    oyk_u8 mode = *reinterpret_cast<volatile oyk_u8 __far*>(MK_FP(0x0040, 0x0049));
    v->vseg = (mode == 7U) ? 0xB000U : 0xB800U;
    v->is_color = (mode != 7U && mode != 2U && mode != 0U) ? 1 : 0;

    union REGS r;
    /* Save original hardware cursor and hide it to avoid flicker during updates */
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x03;
    r.h.bh = 0x00;
    int86(0x10, &r, &r);
    v->orig_cursor = r.x.cx;

    memset(&r, 0, sizeof(r));
    r.h.ah = 0x01;
    r.h.ch = 0x20;
    r.h.cl = 0x00;
    int86(0x10, &r, &r);

    if (v->is_color) {
        /* Enable 16 background colors (disable bit-7 blink) so 0x8F is dark grey */
        memset(&r, 0, sizeof(r));
        r.h.ah = 0x12;
        r.h.bl = 0x10;
        int86(0x10, &r, &r);
        if (r.h.bl != 0x10) {
            v->is_ega_vga = 1;
            memset(&r, 0, sizeof(r));
            r.x.ax = 0x1003;
            r.h.bl = 0x00;
            int86(0x10, &r, &r);
        } else {
            /* Pure CGA: clear blink bit (0x20), keep video-enable bit (0x08) ON;
             * never perform snow-removal blanking that clears 0x3D8 bit 3. */
            volatile oyk_u8 __far* p_crt =
                reinterpret_cast<volatile oyk_u8 __far*>(MK_FP(0x0040, 0x0065));
            v->orig_cga_mode = *p_crt;
            oyk_u8 m = static_cast<oyk_u8>((v->orig_cga_mode | 0x08U) & ~0x20U);
            *p_crt = m;
            outp(0x3D8U, static_cast<int>(m));
        }
    }
#endif

    dos_vid_clear(v, 0x07U);
    for (int i = 0; i < OYK_DOS_VID_CELLS; ++i) {
        v->front[i] = 0xFFFFU;
    }
}

void dos_vid_shutdown(OykDosVideo* v) {
    if (!v) return;
    dos_vid_clear(v, 0x07U);
    dos_vid_flush(v);

#if defined(__DOS__) || defined(MSDOS) || defined(__MSDOS__) || defined(_MSDOS)
    union REGS r;
    if (v->is_color) {
        if (v->is_ega_vga) {
            memset(&r, 0, sizeof(r));
            r.x.ax = 0x1003;
            r.h.bl = 0x01;
            int86(0x10, &r, &r);
        } else if (v->orig_cga_mode != 0) {
            volatile oyk_u8 __far* p_crt =
                reinterpret_cast<volatile oyk_u8 __far*>(MK_FP(0x0040, 0x0065));
            *p_crt = v->orig_cga_mode;
            outp(0x3D8U, static_cast<int>(v->orig_cga_mode));
        }
    }

    memset(&r, 0, sizeof(r));
    r.h.ah = 0x01;
    r.x.cx = v->orig_cursor ? v->orig_cursor : 0x0607U;
    int86(0x10, &r, &r);

    memset(&r, 0, sizeof(r));
    r.h.ah = 0x02;
    r.h.bh = 0x00;
    r.x.dx = 0x0000;
    int86(0x10, &r, &r);
#endif
}

oyk_u8 dos_vid_attr(const OykDosVideo* v, oyk_u8 color_attr, oyk_u8 mono_attr) {
    return (v && !v->is_color) ? mono_attr : color_attr;
}

oyk_u8 dos_vid_list_hdr_attr(const OykDosVideo* v) {
    /* On color displays use dark grey (0x80) background + bright white (0x0F);
     * on MDA/monochrome avoid bit 7 blink and use crisp reverse video (0x70). */
    return (v && !v->is_color) ? static_cast<oyk_u8>(0x70U) : static_cast<oyk_u8>(0x8FU);
}

void dos_vid_clear(OykDosVideo* v, oyk_u8 attr) {
    if (!v) return;
    oyk_u16 cell = make_cell(attr, static_cast<oyk_u8>(' '));
    for (int i = 0; i < OYK_DOS_VID_CELLS; ++i) {
        v->back[i] = cell;
    }
}

void dos_vid_putc(OykDosVideo* v, int row, int col, oyk_u8 attr, oyk_u8 ch) {
    if (!v || row < 0 || row >= OYK_DOS_VID_ROWS || col < 0 || col >= OYK_DOS_VID_COLS) {
        return;
    }
    v->back[row * OYK_DOS_VID_COLS + col] = make_cell(attr, ch);
}

void dos_vid_puts(OykDosVideo* v, int row, int col, oyk_u8 attr, const char* s, int max_w) {
    if (!v || row < 0 || row >= OYK_DOS_VID_ROWS || col >= OYK_DOS_VID_COLS || max_w <= 0) {
        return;
    }
    int base = row * OYK_DOS_VID_COLS;
    int i = 0;
    if (col < 0) {
        i = -col;
        col = 0;
    }
    if (s) {
        while (i < max_w && col < OYK_DOS_VID_COLS && s[i] != '\0') {
            v->back[base + col] = make_cell(attr, static_cast<oyk_u8>(s[i]));
            ++i;
            ++col;
        }
    }
    while (i < max_w && col < OYK_DOS_VID_COLS) {
        v->back[base + col] = make_cell(attr, static_cast<oyk_u8>(' '));
        ++i;
        ++col;
    }
}

void dos_vid_hline(OykDosVideo* v, int row, int col, int len, oyk_u8 attr, oyk_u8 ch) {
    for (int i = 0; i < len; ++i) {
        dos_vid_putc(v, row, col + i, attr, ch);
    }
}

void dos_vid_vline(OykDosVideo* v, int row, int col, int len, oyk_u8 attr, oyk_u8 ch) {
    for (int i = 0; i < len; ++i) {
        dos_vid_putc(v, row + i, col, attr, ch);
    }
}

void dos_vid_box(OykDosVideo* v, int top, int left, int h, int w,
                 oyk_u8 border_attr, oyk_u8 fill_attr, int fill_interior) {
    if (!v || h < 2 || w < 2) return;
    int bottom = top + h - 1;
    int right = left + w - 1;

    dos_vid_putc(v, top, left, border_attr, OYK_BOX_TL);
    dos_vid_hline(v, top, left + 1, w - 2, border_attr, OYK_BOX_H);
    dos_vid_putc(v, top, right, border_attr, OYK_BOX_TR);

    for (int r = top + 1; r < bottom; ++r) {
        dos_vid_putc(v, r, left, border_attr, OYK_BOX_V);
        if (fill_interior) {
            dos_vid_hline(v, r, left + 1, w - 2, fill_attr, static_cast<oyk_u8>(' '));
        }
        dos_vid_putc(v, r, right, border_attr, OYK_BOX_V);
    }

    dos_vid_putc(v, bottom, left, border_attr, OYK_BOX_BL);
    dos_vid_hline(v, bottom, left + 1, w - 2, border_attr, OYK_BOX_H);
    dos_vid_putc(v, bottom, right, border_attr, OYK_BOX_BR);
}

void dos_vid_hdiv(OykDosVideo* v, int row, int left, int w, oyk_u8 attr) {
    if (!v || w < 2) return;
    dos_vid_putc(v, row, left, attr, OYK_BOX_LT);
    dos_vid_hline(v, row, left + 1, w - 2, attr, OYK_BOX_H);
    dos_vid_putc(v, row, left + w - 1, attr, OYK_BOX_RT);
}

void dos_vid_title(OykDosVideo* v, int row, int col, int max_w, oyk_u8 attr, const char* title) {
    if (!v || !title || !title[0] || max_w <= 2) return;
    char buf[82];
    int tlen = static_cast<int>(strlen(title));
    if (tlen > max_w - 2) tlen = max_w - 2;
    if (tlen > 78) tlen = 78;
    buf[0] = ' ';
    memcpy(buf + 1, title, static_cast<size_t>(tlen));
    buf[1 + tlen] = ' ';
    buf[2 + tlen] = '\0';
    dos_vid_puts(v, row, col, attr, buf, tlen + 2);
}

static oyk_u32 flush_cell_range(OykDosVideo* v, int start_idx, int end_idx) {
    if (!v) return 0;
    if (start_idx < 0) start_idx = 0;
    if (end_idx > OYK_DOS_VID_CELLS) end_idx = OYK_DOS_VID_CELLS;

    oyk_u32 writes = 0;
    if (v->sim_vram) {
        for (int i = start_idx; i < end_idx; ++i) {
            if (v->back[i] != v->front[i]) {
                v->sim_vram[i] = v->back[i];
                v->front[i] = v->back[i];
                ++writes;
            }
        }
    } else {
#if defined(__DOS__) || defined(MSDOS) || defined(__MSDOS__) || defined(_MSDOS)
        /* Direct dirty-cell VRAM copy without CGA retrace blanking */
        oyk_u16 __far* scr = reinterpret_cast<oyk_u16 __far*>(MK_FP(v->vseg, 0));
        for (int i = start_idx; i < end_idx; ++i) {
            if (v->back[i] != v->front[i]) {
                scr[i] = v->back[i];
                v->front[i] = v->back[i];
                ++writes;
            }
        }
#else
        for (int i = start_idx; i < end_idx; ++i) {
            if (v->back[i] != v->front[i]) {
                v->front[i] = v->back[i];
                ++writes;
            }
        }
#endif
    }
    v->last_flush_writes = writes;
    return writes;
}

oyk_u32 dos_vid_flush_row(OykDosVideo* v, int row) {
    if (!v || row < 0 || row >= OYK_DOS_VID_ROWS) return 0;
    int start_idx = row * OYK_DOS_VID_COLS;
    return flush_cell_range(v, start_idx, start_idx + OYK_DOS_VID_COLS);
}

oyk_u32 dos_vid_flush(OykDosVideo* v) {
    return flush_cell_range(v, 0, OYK_DOS_VID_CELLS);
}
