/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_MAC_UI_HPP
#define OYK_MAC_UI_HPP

/*
 * Classic Macintosh Toolbox UI (System 6.0.8 - Mac OS 9.2.2)
 *
 * Adaptive renderer driven by shared oyk_gui & oyk_term controllers:
 *   - 512x342 1-bit monochrome QuickDraw (Macintosh Plus / SE)
 *   - 640x480+ Color QuickDraw with OYK themes (Mac II / Quadra / PowerMac)
 */

#include "oyk_gui.hpp"
#include "oyk_term.hpp"
#include "mac_net.hpp"

struct OykMacUiState;

void oyk_mac_query_os_native_theme(void* win_ptr, int has_color_qd, OykThemeConfig* out_pal);
int  oyk_mac_ui_run(OykPortableNode* node, OykMacNetState* net, int ui_test_mode);
void oyk_mac_ui_show_fatal(const char* msg);
int  oyk_mac_ui_run_theme_preview_self_test(OykPortableNode* node,
                                            OykMacNetState* net,
                                            int* out_bg_changes,
                                            int* out_fg_changes);

#endif /* OYK_MAC_UI_HPP */