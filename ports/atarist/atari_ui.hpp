/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_ATARI_UI_HPP
#define OYK_ATARI_UI_HPP

#include "oyk_gui.hpp"
#include "oyk_term.hpp"
#include "atari_net.hpp"
#include "atari_policy.hpp"

void oyk_atari_query_os_native_theme(short vdi_handle, int colors, OykThemeConfig* out_pal);
int  oyk_atari_ui_run(OykPortableNode* node,
                      OykAtariNetState* net,
                      int ui_test_mode);
void oyk_atari_ui_show_fatal(const char* msg);
int  oyk_atari_ui_run_theme_preview_self_test(OykPortableNode* node,
                                              OykAtariNetState* net,
                                              short vdi_handle,
                                              int colors,
                                              int* out_bg_changes,
                                              int* out_fg_changes);

#endif /* OYK_ATARI_UI_HPP */
