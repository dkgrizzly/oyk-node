/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */
#ifndef OYK_TI99_TUI_H
#define OYK_TI99_TUI_H

#include "ti99_core.h"
#include "ti99_vdp.h"

#ifdef __cplusplus
extern "C" {
#endif

void ti99_tui_render(Ti99VdpState *vdp, const Ti99App *app);

#ifdef __cplusplus
}
#endif

#endif /* OYK_TI99_TUI_H */
