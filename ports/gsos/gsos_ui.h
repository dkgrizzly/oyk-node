/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_GSOS_UI_H
#define OYK_GSOS_UI_H

#include "gsos_core.h"
#include "gsos_net.h"

#ifdef __cplusplus
extern "C" {
#endif

void oyk_gsos_query_os_native_theme(OykGsosThemeConfig *out_pal);
int  oyk_gsos_ui_run(OykGsosNode *node, OykGsosNetState *net, int ui_test_mode);
int  oyk_gsos_ui_run_theme_preview_self_test(OykGsosNode *node,
                                             OykGsosNetState *net,
                                             int *out_bg_changes,
                                             int *out_fg_changes);

#ifdef __cplusplus
}
#endif

#endif /* OYK_GSOS_UI_H */
