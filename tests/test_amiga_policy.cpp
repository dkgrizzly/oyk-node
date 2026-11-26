/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include <stdio.h>
#include <string.h>

#include "oyk_portable.hpp"
#include "oyk_gui.hpp"
#include "amiga_policy.hpp"

static int g_fail = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
            ++g_fail; \
        } \
    } while (0)

int main() {
    OykConfig cfg;
    oyk_config_init_default(&cfg);

    /* 1. Private screen must be disabled (0) by default! */
    CHECK(cfg.amiga_private_screen == 0);
    CHECK(oyk_amiga_should_use_private_screen(&cfg, -1, -1) == 0);

    /* 2. Enabled via config, CLI, or ToolType (with explicit --workbench override) */
    cfg.amiga_private_screen = 1;
    CHECK(oyk_amiga_should_use_private_screen(&cfg, -1, -1) == 1);
    CHECK(oyk_amiga_should_use_private_screen(&cfg, -2, -1) == 0);
    cfg.amiga_private_screen = 0;
    CHECK(oyk_amiga_should_use_private_screen(&cfg, 1, -1) == 1);
    CHECK(oyk_amiga_should_use_private_screen(&cfg, -1, 1) == 1);
    CHECK(oyk_amiga_parse_tooltype_privatescreen("") == 1);
    CHECK(oyk_amiga_parse_tooltype_privatescreen("YES") == 1);
    CHECK(oyk_amiga_parse_tooltype_privatescreen("FALSE") == 0);

    /* 3. Config parse/serialize preserves amiga_private_screen and EOL */
    OykConfig saved_cfg;
    oyk_config_init_default(&saved_cfg);
    oyk_parse_callsign("KR4OYK-7", &saved_cfg.callsign);
    saved_cfg.amiga_private_screen = 1;
    const char* tmp_cfg = "/tmp/oyk_amiga_cfg_test.cfg";
    CHECK(oyk_config_save(&saved_cfg, tmp_cfg) == 1);
    oyk_config_init_default(&cfg);
    CHECK(cfg.amiga_private_screen == 0);
    char err[96];
    err[0] = '\0';
    CHECK(oyk_config_load(tmp_cfg, &cfg, err, sizeof(err)) == 1);
    CHECK(cfg.amiga_private_screen == 1);
    remove(tmp_cfg);

    /* 4. Kickstart 3.1 / 2.04 / 1.3 detection tiers */
    CHECK(oyk_amiga_classify_kickstart(34) == OYK_AMIGA_KS_13);
    CHECK(oyk_amiga_has_gadtools(34) == 0);
    CHECK(oyk_amiga_has_setrgb32(34) == 0);
    CHECK(oyk_amiga_classify_kickstart(37) == OYK_AMIGA_KS_204);
    CHECK(oyk_amiga_has_gadtools(37) == 1);
    CHECK(oyk_amiga_has_setrgb32(37) == 0);
    CHECK(oyk_amiga_classify_kickstart(40) == OYK_AMIGA_KS_31);
    CHECK(oyk_amiga_has_gadtools(40) == 1);
    CHECK(oyk_amiga_has_setrgb32(40) == 1);
    CHECK(oyk_amiga_classify_kickstart(51) == OYK_AMIGA_KS_31);

    /* 5. Color-depth theme lock (< 32 colors locked to OS native; >= 32 unlocked) */
    CHECK(oyk_amiga_colors_from_depth(2) == 4);
    CHECK(oyk_amiga_colors_from_depth(4) == 16);
    CHECK(oyk_amiga_colors_from_depth(5) == 32);
    CHECK(oyk_amiga_colors_from_depth(6) == 64);

    CHECK(oyk_amiga_policy_allows_custom_themes(2) == 0);
    CHECK(oyk_amiga_policy_allows_custom_themes(4) == 0);
    CHECK(oyk_amiga_policy_allows_custom_themes(8) == 0);
    CHECK(oyk_amiga_policy_allows_custom_themes(16) == 0);
    CHECK(oyk_amiga_policy_allows_custom_themes(31) == 0);
    CHECK(oyk_amiga_policy_allows_custom_themes(32) == 1);
    CHECK(oyk_amiga_policy_allows_custom_themes(64) == 1);
    CHECK(oyk_amiga_policy_allows_custom_themes(256) == 1);
    CHECK(oyk_amiga_policy_allows_custom_themes_depth(4) == 0);
    CHECK(oyk_amiga_policy_allows_custom_themes_depth(5) == 1);
    CHECK(oyk_amiga_policy_allows_custom_themes_depth(6) == 1);

    OykThemeConfig os_nat, dark_pal, eff;
    memset(&os_nat, 0, sizeof(os_nat));
    strcpy(os_nat.name, "native");
    os_nat.colors[OYK_TR_BG] = 0xAAAAAAUL;
    os_nat.colors[OYK_TR_FG] = 0x000000UL;
    os_nat.colors[OYK_TR_ACCENT] = 0x6688BBUL;
    oyk_theme_init_builtin(&dark_pal, 0);

    int locked = 0;
    oyk_amiga_resolve_effective_theme(4, &os_nat, &dark_pal, &eff, &locked);
    CHECK(locked == 1 && strcmp(eff.name, "native") == 0 && eff.colors[OYK_TR_BG] == 0xAAAAAAUL);
    oyk_amiga_resolve_effective_theme(16, &os_nat, &dark_pal, &eff, &locked);
    CHECK(locked == 1 && strcmp(eff.name, "native") == 0);
    oyk_amiga_resolve_effective_theme(32, &os_nat, &dark_pal, &eff, &locked);
    CHECK(locked == 0 && strcmp(eff.name, "dark") == 0 && eff.colors[OYK_TR_BG] == dark_pal.colors[OYK_TR_BG]);
    oyk_amiga_resolve_effective_theme(64, &os_nat, &dark_pal, &eff, &locked);
    CHECK(locked == 0 && strcmp(eff.name, "dark") == 0);

    printf("oyk_amiga_tests: failures=%d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
