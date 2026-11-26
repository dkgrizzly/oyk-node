/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_DOS_FONT8X16_HPP
#define OYK_DOS_FONT8X16_HPP

/*
 * CP437 8x16 (VGA/VBE) and 8x14 (EGA) bitmap font support for graphical MS-DOS.
 * Loads hardware ROM fonts via BIOS INT 10h AX=1130h when available on DOS,
 * falling back to an embedded C++98 CP437 + ASCII glyph set.
 */

#include "../compat/oyk_portable.hpp"

struct OykDosFont {
    oyk_u8* bios_8x16; /* 256 * 16 = 4096 bytes if loaded from BIOS */
    oyk_u8* bios_8x14; /* 256 * 14 = 3584 bytes if loaded from BIOS */
    int has_bios_16;
    int has_bios_14;
};

void oyk_dos_font_init(OykDosFont* f);
void oyk_dos_font_shutdown(OykDosFont* f);

/* Writes char_h scanlines (14 or 16 bytes, MSB=left pixel) into out_glyph[0..char_h-1]. */
void oyk_dos_font_get_glyph(const OykDosFont* f, int char_h, oyk_u8 ch, oyk_u8* out_glyph);

#endif /* OYK_DOS_FONT8X16_HPP */
