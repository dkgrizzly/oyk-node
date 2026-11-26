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

int  oyk_mac_ui_run(OykPortableNode* node, OykMacNetState* net, int ui_test_mode);
void oyk_mac_ui_show_fatal(const char* msg);

#endif /* OYK_MAC_UI_HPP */
