/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "amiga_policy.hpp"

#include <ctype.h>
#include <string.h>

static int ap_strieq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower(static_cast<unsigned char>(*a)) !=
            tolower(static_cast<unsigned char>(*b))) {
            return 0;
        }
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

OykAmigaKickstartTier oyk_amiga_classify_kickstart(int exec_version) {
    if (exec_version >= 39) return OYK_AMIGA_KS_31;
    if (exec_version >= 36) return OYK_AMIGA_KS_204;
    return OYK_AMIGA_KS_13;
}

int oyk_amiga_has_gadtools(int exec_version) {
    return (exec_version >= 36) ? 1 : 0;
}

int oyk_amiga_has_setrgb32(int exec_version) {
    return (exec_version >= 39) ? 1 : 0;
}

int oyk_amiga_colors_from_depth(int depth) {
    if (depth <= 0) return 2;
    if (depth >= 8) return 256;
    return 1 << depth;
}

int oyk_amiga_policy_allows_custom_themes(int screen_colors) {
    return (screen_colors >= OYK_AMIGA_MIN_CUSTOM_THEME_COLORS) ? 1 : 0;
}

int oyk_amiga_policy_allows_custom_themes_depth(int depth) {
    return (depth >= OYK_AMIGA_MIN_CUSTOM_THEME_DEPTH) ? 1 : 0;
}

void oyk_amiga_resolve_effective_theme(int screen_colors,
                                       const OykThemeConfig* os_native_pal,
                                       const OykThemeConfig* requested_pal,
                                       OykThemeConfig* out_pal,
                                       int* out_locked_to_native) {
    int custom_ok = oyk_amiga_policy_allows_custom_themes(screen_colors);
    if (out_locked_to_native) *out_locked_to_native = custom_ok ? 0 : 1;
    if (!out_pal) return;

    if (!custom_ok || !requested_pal) {
        if (os_native_pal) {
            *out_pal = *os_native_pal;
        } else {
            oyk_theme_init_builtin(out_pal, 1);
            strcpy(out_pal->name, "native");
        }
        return;
    }
    *out_pal = *requested_pal;
}

int oyk_amiga_should_use_private_screen(const OykConfig* cfg,
                                        int cli_private_screen,
                                        int tooltype_private_screen) {
    /* Precedence: explicit enable (>0), explicit disable (-2), then cfg option (default 0). */
    if (cli_private_screen > 0 || tooltype_private_screen > 0) return 1;
    if (cli_private_screen == -2 || tooltype_private_screen == -2) return 0;
    if (cfg && cfg->amiga_private_screen) return 1;
    return 0;
}

int oyk_amiga_parse_tooltype_privatescreen(const char* tt_val) {
    if (!tt_val) return 1; /* Bare PRIVATESCREEN ToolType without =VALUE means enabled. */
    while (*tt_val == ' ' || *tt_val == '\t') ++tt_val;
    if (tt_val[0] == '\0') return 1;
    if (ap_strieq(tt_val, "1") || ap_strieq(tt_val, "true") ||
        ap_strieq(tt_val, "yes") || ap_strieq(tt_val, "on")) {
        return 1;
    }
    return 0;
}

void oyk_amiga_parse_cli_flags(int argc, char** argv,
                               int* out_cli_private_screen,
                               int* out_ui_test_mode,
                               const char** out_cfg_path) {
    int i;
    if (out_cli_private_screen) *out_cli_private_screen = -1;
    if (out_ui_test_mode) *out_ui_test_mode = 0;
    if (out_cfg_path) *out_cfg_path = "NODE.CFG";
    if (!argv) return;
    for (i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (!arg) continue;
        if (strcmp(arg, "--private-screen") == 0 || strcmp(arg, "-p") == 0) {
            if (out_cli_private_screen) *out_cli_private_screen = 1;
        } else if (strcmp(arg, "--no-private-screen") == 0 || strcmp(arg, "--workbench") == 0) {
            if (out_cli_private_screen) *out_cli_private_screen = -2;
        } else if (strcmp(arg, "--ui-test") == 0) {
            if (out_ui_test_mode) *out_ui_test_mode = 1;
        } else if ((strcmp(arg, "--config") == 0 || strcmp(arg, "-c") == 0) &&
                   i + 1 < argc && argv[i + 1]) {
            if (out_cfg_path) *out_cfg_path = argv[++i];
        }
    }
}
