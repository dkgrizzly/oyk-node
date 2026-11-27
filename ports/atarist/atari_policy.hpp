/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_ATARI_POLICY_HPP
#define OYK_ATARI_POLICY_HPP

#include "oyk_gui.hpp"

enum OykAtariDisplayTier {
    OYK_ATARI_MONO    = 0, /* 640x400 1-bit ST High */
    OYK_ATARI_MED4    = 1, /* 640x200 4-color ST Medium */
    OYK_ATARI_COLOR16 = 2  /* 640x480 16+ color TT / Falcon / VDI */
};

OykAtariDisplayTier oyk_atari_classify_display(int width, int height, int colors);
int                 oyk_atari_planes_from_colors(int colors);

/* Convert Atari GEM VDI 0..1000 RGB scale <-> 24-bit 0x00RRGGBB */
oyk_u32 oyk_atari_vdi_to_rgb24(short r1000, short g1000, short b1000);
void    oyk_atari_rgb24_to_vdi(oyk_u32 rgb, short* r1000, short* g1000, short* b1000);

/* Resolve effective theme (defaults to OS-native VDI palette; supports full preview) */
void oyk_atari_resolve_theme(int colors,
                             const OykThemeConfig* os_native_pal,
                             const OykThemeConfig* requested_pal,
                             OykThemeConfig* out_pal);

/* Map hardware 4-plane shifter pixel index (0..15) <-> GEM VDI color index (0..15) */
int oyk_atari_hw_to_vdi_idx(int hw_idx, int planes);
int oyk_atari_vdi_to_hw_idx(int vdi_idx, int planes);

void oyk_atari_parse_cli_flags(int argc, char** argv,
                               int* out_ui_test_mode,
                               const char** out_cfg_path);

#endif /* OYK_ATARI_POLICY_HPP */
