/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include <stdio.h>
#include <string.h>

#include "oyk_portable.hpp"
#include "oyk_gui.hpp"
#include "atari_policy.hpp"

static int g_fail = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
            ++g_fail; \
        } \
    } while (0)

int main() {
    /* 1. Display tier classification */
    CHECK(oyk_atari_classify_display(640, 400, 2) == OYK_ATARI_MONO);
    CHECK(oyk_atari_classify_display(640, 200, 4) == OYK_ATARI_MED4);
    CHECK(oyk_atari_classify_display(640, 480, 16) == OYK_ATARI_COLOR16);
    CHECK(oyk_atari_planes_from_colors(2) == 1);
    CHECK(oyk_atari_planes_from_colors(4) == 2);
    CHECK(oyk_atari_planes_from_colors(16) == 4);

    /* 2. VDI 0..1000 <-> 24-bit RGB round-trip */
    CHECK(oyk_atari_vdi_to_rgb24(1000, 0, 1000) == 0xFF00FFUL);
    short r1000 = 0, g1000 = 0, b1000 = 0;
    oyk_atari_rgb24_to_vdi(0xFF00FFUL, &r1000, &g1000, &b1000);
    CHECK(r1000 == 1000 && g1000 == 0 && b1000 == 1000);

    /* 3. Atari 4-plane shifter <-> VDI color index bijection */
    for (int i = 0; i < 16; ++i) {
        int hw = oyk_atari_vdi_to_hw_idx(i, 4);
        CHECK(oyk_atari_hw_to_vdi_idx(hw, 4) == i);
    }

    /* 4. Theme resolution defaults to OS native */
    OykThemeConfig nat, dark, eff;
    oyk_theme_init_builtin(&nat, 1);
    strcpy(nat.name, "native");
    oyk_theme_init_builtin(&dark, 0);
    oyk_atari_resolve_theme(16, &nat, NULL, &eff);
    CHECK(strcmp(eff.name, "native") == 0);
    oyk_atari_resolve_theme(16, &nat, &dark, &eff);
    CHECK(strcmp(eff.name, "dark") == 0);

    printf("oyk_atarist_tests: failures=%d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
