/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_AMIGA_POLICY_HPP
#define OYK_AMIGA_POLICY_HPP

#include "oyk_gui.hpp"

enum OykAmigaKickstartTier {
    OYK_AMIGA_KS_13  = 13,
    OYK_AMIGA_KS_204 = 204,
    OYK_AMIGA_KS_31  = 31
};

enum {
    OYK_AMIGA_MIN_CUSTOM_THEME_COLORS = 32,
    OYK_AMIGA_MIN_CUSTOM_THEME_DEPTH  = 5
};

OykAmigaKickstartTier oyk_amiga_classify_kickstart(int exec_version);
int oyk_amiga_has_gadtools(int exec_version);
int oyk_amiga_has_setrgb32(int exec_version);

int oyk_amiga_colors_from_depth(int depth);
int oyk_amiga_policy_allows_custom_themes(int screen_colors);
int oyk_amiga_policy_allows_custom_themes_depth(int depth);

void oyk_amiga_resolve_effective_theme(int screen_colors,
                                       const OykThemeConfig* os_native_pal,
                                       const OykThemeConfig* requested_pal,
                                       OykThemeConfig* out_pal,
                                       int* out_locked_to_native);

int oyk_amiga_should_use_private_screen(const OykConfig* cfg,
                                        int cli_private_screen,
                                        int tooltype_private_screen);
int oyk_amiga_parse_tooltype_privatescreen(const char* tt_val);
void oyk_amiga_parse_cli_flags(int argc, char** argv,
                               int* out_cli_private_screen,
                               int* out_ui_test_mode,
                               const char** out_cfg_path);

#endif /* OYK_AMIGA_POLICY_HPP */
