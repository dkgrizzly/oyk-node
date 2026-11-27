/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "atari_policy.hpp"

#include <string.h>

OykAtariDisplayTier oyk_atari_classify_display(int width, int height, int colors) {
    (void)width;
    (void)height;
    if (colors <= 2) return OYK_ATARI_MONO;
    if (colors <= 4) return OYK_ATARI_MED4;
    return OYK_ATARI_COLOR16;
}

int oyk_atari_planes_from_colors(int colors) {
    if (colors <= 2) return 1;
    if (colors <= 4) return 2;
    return 4;
}

static unsigned int at_clamp_1000_to_255(short v) {
    if (v <= 0) return 0U;
    if (v >= 1000) return 255U;
    return static_cast<unsigned int>((static_cast<unsigned long>(v) * 255UL + 500UL) / 1000UL);
}

static short at_255_to_1000(unsigned int v) {
    if (v >= 255U) return 1000;
    return static_cast<short>((static_cast<unsigned long>(v) * 1000UL + 127UL) / 255UL);
}

oyk_u32 oyk_atari_vdi_to_rgb24(short r1000, short g1000, short b1000) {
    oyk_u32 r = static_cast<oyk_u32>(at_clamp_1000_to_255(r1000));
    oyk_u32 g = static_cast<oyk_u32>(at_clamp_1000_to_255(g1000));
    oyk_u32 b = static_cast<oyk_u32>(at_clamp_1000_to_255(b1000));
    return (r << 16) | (g << 8) | b;
}

void oyk_atari_rgb24_to_vdi(oyk_u32 rgb, short* r1000, short* g1000, short* b1000) {
    unsigned int r = static_cast<unsigned int>((rgb >> 16) & 0xFFU);
    unsigned int g = static_cast<unsigned int>((rgb >> 8)  & 0xFFU);
    unsigned int b = static_cast<unsigned int>(rgb & 0xFFU);
    if (r1000) *r1000 = at_255_to_1000(r);
    if (g1000) *g1000 = at_255_to_1000(g);
    if (b1000) *b1000 = at_255_to_1000(b);
}

void oyk_atari_resolve_theme(int colors,
                             const OykThemeConfig* os_native_pal,
                             const OykThemeConfig* requested_pal,
                             OykThemeConfig* out_pal) {
    (void)colors;
    if (!out_pal) return;
    if (requested_pal) {
        *out_pal = *requested_pal;
    } else if (os_native_pal) {
        *out_pal = *os_native_pal;
    } else {
        oyk_theme_init_builtin(out_pal, 1);
        strcpy(out_pal->name, "native");
    }
}

/* Standard TOS VDI <-> Atari 4-plane shifter register index table */
static const int kHwToVdi4[16] = {
    0, 2, 3, 6, 4, 7, 5, 8, 9, 10, 11, 14, 12, 15, 13, 1
};
static const int kVdiToHw4[16] = {
    0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13
};

int oyk_atari_hw_to_vdi_idx(int hw_idx, int planes) {
    if (planes <= 1) return (hw_idx & 1);
    if (planes == 2) {
        static const int kHwToVdi2[4] = { 0, 2, 3, 1 };
        return kHwToVdi2[hw_idx & 3];
    }
    return kHwToVdi4[hw_idx & 15];
}

int oyk_atari_vdi_to_hw_idx(int vdi_idx, int planes) {
    if (planes <= 1) return (vdi_idx & 1);
    if (planes == 2) {
        static const int kVdiToHw2[4] = { 0, 3, 1, 2 };
        return kVdiToHw2[vdi_idx & 3];
    }
    return kVdiToHw4[vdi_idx & 15];
}

void oyk_atari_parse_cli_flags(int argc, char** argv,
                               int* out_ui_test_mode,
                               const char** out_cfg_path) {
    if (out_ui_test_mode) *out_ui_test_mode = 0;
    if (out_cfg_path) *out_cfg_path = "NODE.CFG";
    if (!argv) return;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (!arg) continue;
        if (strcmp(arg, "--ui-test") == 0) {
            if (out_ui_test_mode) *out_ui_test_mode = 1;
        } else if ((strcmp(arg, "--config") == 0 || strcmp(arg, "-c") == 0) &&
                   i + 1 < argc && argv[i + 1]) {
            if (out_cfg_path) *out_cfg_path = argv[++i];
        }
    }
}
