/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */
#ifndef OYK_TI99_VDP_H
#define OYK_TI99_VDP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TI99_VDP_ROWS_MAX       30
#define TI99_VDP_COLS_80        80
#define TI99_VDP_COLS_40        40

typedef struct Ti99VdpState {
    uint8_t is_f18a;
    uint8_t cols;
    uint8_t rows;
    uint8_t fg_color;
    uint8_t bg_color;
    uint8_t screen[TI99_VDP_ROWS_MAX][TI99_VDP_COLS_80];
    uint8_t attr[TI99_VDP_ROWS_MAX][TI99_VDP_COLS_80];
} Ti99VdpState;

int ti99_vdp_init(Ti99VdpState *vdp, int prefer_f18a);
void ti99_vdp_clear(Ti99VdpState *vdp, uint8_t attr);
void ti99_vdp_putc(Ti99VdpState *vdp, int row, int col, char ch, uint8_t attr);
void ti99_vdp_puts(Ti99VdpState *vdp, int row, int col, const char *s, uint8_t attr);
int ti99_vdp_find_text(const Ti99VdpState *vdp, const char *needle);

#ifdef __cplusplus
}
#endif

#endif /* OYK_TI99_VDP_H */
