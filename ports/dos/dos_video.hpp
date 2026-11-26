/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_DOS_VIDEO_HPP
#define OYK_DOS_VIDEO_HPP

/*
 * Direct 80x25 Text-Mode Double-Buffered Video Driver for MS-DOS
 *
 * Strict C++98, zero STL, 16-bit large-model safe.
 * - Composes off-screen into `back[]` and diffs against `front[]` on flush so
 *   only changed cells touch video RAM (eliminating flicker/flashing).
 * - Automatically detects MDA/Hercules (0xB000, mono mode 7) vs CGA/EGA/VGA
 *   (0xB800) and intentionally bypasses CGA snow-removal video blanking (never
 *   clears bit 3 of 0x3D8 during updates).
 * - Disables bit-7 blink on color adapters once at init so 0x8F renders as
 *   Bright White on Dark Grey for list-view header rows.
 * - Compiles cleanly on POSIX/Windows hosts for differential-flush unit tests.
 */

#include "../compat/oyk_portable.hpp"

#define OYK_DOS_VID_COLS 80
#define OYK_DOS_VID_ROWS 25
#define OYK_DOS_VID_CELLS (OYK_DOS_VID_COLS * OYK_DOS_VID_ROWS)

/* CP437 single-line box-drawing characters */
#define OYK_BOX_TL 0xDAU /* top-left corner */
#define OYK_BOX_TR 0xBFU /* top-right corner */
#define OYK_BOX_BL 0xC0U /* bottom-left corner */
#define OYK_BOX_BR 0xD9U /* bottom-right corner */
#define OYK_BOX_H  0xC4U /* horizontal line */
#define OYK_BOX_V  0xB3U /* vertical line */
#define OYK_BOX_LT 0xC3U /* left tee (|-) */
#define OYK_BOX_RT 0xB4U /* right tee (-|) */
#define OYK_BOX_TT 0xC2U /* top tee */
#define OYK_BOX_BT 0xC1U /* bottom tee */
#define OYK_BOX_X  0xC5U /* cross */

struct OykDosVideo {
    oyk_u16 back[OYK_DOS_VID_CELLS];
    oyk_u16 front[OYK_DOS_VID_CELLS];
    oyk_u16 vseg;
    int is_color;
    int is_ega_vga;
    oyk_u8 orig_cga_mode;
    oyk_u16 orig_cursor;
    oyk_u16* sim_vram;
    oyk_u32 last_flush_writes;
};

void dos_vid_init(OykDosVideo* v);
void dos_vid_init_sim(OykDosVideo* v, int is_color, oyk_u16* sim_vram);
void dos_vid_shutdown(OykDosVideo* v);

oyk_u8 dos_vid_attr(const OykDosVideo* v, oyk_u8 color_attr, oyk_u8 mono_attr);
oyk_u8 dos_vid_list_hdr_attr(const OykDosVideo* v);

void dos_vid_clear(OykDosVideo* v, oyk_u8 attr);
void dos_vid_putc(OykDosVideo* v, int row, int col, oyk_u8 attr, oyk_u8 ch);
void dos_vid_puts(OykDosVideo* v, int row, int col, oyk_u8 attr, const char* s, int max_w);
void dos_vid_hline(OykDosVideo* v, int row, int col, int len, oyk_u8 attr, oyk_u8 ch);
void dos_vid_vline(OykDosVideo* v, int row, int col, int len, oyk_u8 attr, oyk_u8 ch);
void dos_vid_box(OykDosVideo* v, int top, int left, int h, int w,
                 oyk_u8 border_attr, oyk_u8 fill_attr, int fill_interior);
void dos_vid_shadow(OykDosVideo* v, int top, int left, int h, int w);
void dos_vid_hdiv(OykDosVideo* v, int row, int left, int w, oyk_u8 attr);
void dos_vid_title(OykDosVideo* v, int row, int col, int max_w, oyk_u8 attr, const char* title);

oyk_u32 dos_vid_flush_row(OykDosVideo* v, int row);
oyk_u32 dos_vid_flush(OykDosVideo* v);

#endif /* OYK_DOS_VIDEO_HPP */
