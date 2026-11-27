/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * Shared Strict ANSI C89 OSF/Motif 1.2 + X11R5 GUI Layer
 * Reused by Motorola 88K SVR4 (m88k_motif_main.c) and OpenVMS DECwindows
 * Motif (vms_motif_main.c).
 */

#ifndef OYK_SYSV_MOTIF_UI_H
#define OYK_SYSV_MOTIF_UI_H

#include "sysv_core.h"

#ifdef __cplusplus
extern "C" {
#endif

int oyk_sysv_motif_run(OykSysvApp *app, int argc, char **argv);
int oyk_sysv_motif_test_mode_supported(void);

#ifdef __cplusplus
}
#endif

#endif /* OYK_SYSV_MOTIF_UI_H */
