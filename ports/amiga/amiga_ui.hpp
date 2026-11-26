/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_AMIGA_UI_HPP
#define OYK_AMIGA_UI_HPP

#include "oyk_gui.hpp"
#include "oyk_term.hpp"
#include "amiga_net.hpp"
#include "amiga_policy.hpp"

struct Screen;

void oyk_amiga_query_os_native_theme(struct Screen* scr, OykThemeConfig* out_pal);
int  oyk_amiga_ui_run(OykPortableNode* node,
                      OykAmigaNetState* net,
                      int use_private_screen,
                      int ui_test_mode);
void oyk_amiga_ui_show_fatal(const char* msg);
int  oyk_amiga_ui_run_theme_preview_self_test(OykPortableNode* node,
                                              OykAmigaNetState* net,
                                              int screen_depth,
                                              int* out_locked_low,
                                              int* out_bg_changes,
                                              int* out_fg_changes);

#endif /* OYK_AMIGA_UI_HPP */
