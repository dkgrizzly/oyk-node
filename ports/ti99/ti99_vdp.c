/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */
#include "ti99_vdp.h"

#include <string.h>

#ifdef __TMS9900__
#define TI99_VDP_DATA_RD (*(volatile uint8_t *)0x8800U)
#define TI99_VDP_STAT_RD (*(volatile uint8_t *)0x8802U)
#define TI99_VDP_DATA_WR (*(volatile uint8_t *)0x8C00U)
#define TI99_VDP_ADDR_WR (*(volatile uint8_t *)0x8C02U)

static void vdp_set_reg(uint8_t reg, uint8_t val)
{
    TI99_VDP_ADDR_WR = val;
    TI99_VDP_ADDR_WR = (uint8_t)(0x80U | (reg & 0x3FU));
}

static int probe_f18a_hw(void)
{
    uint8_t sr;
    /* Standard F18A unlock sequence: VR57 = >1C twice, then check SR14 identity */
    vdp_set_reg(57, 0x1CU);
    vdp_set_reg(57, 0x1CU);
    vdp_set_reg(15, 0x0EU);
    sr = TI99_VDP_STAT_RD;
    vdp_set_reg(15, 0x00U);
    return ((sr & 0xE0U) == 0xE0U) ? 1 : 0;
}
#endif

int ti99_vdp_init(Ti99VdpState *vdp, int prefer_f18a)
{
    if (!vdp) {
        return -1;
    }
    memset(vdp, 0, sizeof(*vdp));
#ifdef __TMS9900__
    vdp->is_f18a = (prefer_f18a && probe_f18a_hw()) ? 1U : 0U;
    if (vdp->is_f18a) {
        vdp_set_reg(0, 0x04U); /* TEXT80 */
        vdp_set_reg(1, 0x70U);
        vdp->cols = TI99_VDP_COLS_80;
        vdp->rows = 24U;
    } else {
        vdp_set_reg(0, 0x00U); /* 40-col Text */
        vdp_set_reg(1, 0x70U);
        vdp->cols = TI99_VDP_COLS_40;
        vdp->rows = 24U;
    }
#else
    vdp->is_f18a = prefer_f18a ? 1U : 0U;
    vdp->cols = vdp->is_f18a ? TI99_VDP_COLS_80 : TI99_VDP_COLS_40;
    vdp->rows = 24U;
#endif
    vdp->fg_color = 15U;
    vdp->bg_color = 4U;
    ti99_vdp_clear(vdp, 0xF4U);
    return 0;
}

void ti99_vdp_clear(Ti99VdpState *vdp, uint8_t attr)
{
    uint8_t r;
    uint8_t c;
    if (!vdp) {
        return;
    }
    for (r = 0; r < TI99_VDP_ROWS_MAX; ++r) {
        for (c = 0; c < TI99_VDP_COLS_80; ++c) {
            vdp->screen[r][c] = ' ';
            vdp->attr[r][c] = attr;
        }
    }
}

void ti99_vdp_putc(Ti99VdpState *vdp, int row, int col, char ch, uint8_t attr)
{
    if (!vdp || row < 0 || col < 0 || row >= (int)vdp->rows || col >= (int)vdp->cols) {
        return;
    }
    vdp->screen[row][col] = (uint8_t)ch;
    vdp->attr[row][col] = attr;
#ifdef __TMS9900__
    {
        uint16_t addr = (uint16_t)((uint16_t)row * (uint16_t)vdp->cols + (uint16_t)col);
        TI99_VDP_ADDR_WR = (uint8_t)(addr & 0xFFU);
        TI99_VDP_ADDR_WR = (uint8_t)(0x40U | ((addr >> 8) & 0x3FU));
        TI99_VDP_DATA_WR = (uint8_t)ch;
    }
#endif
}

void ti99_vdp_puts(Ti99VdpState *vdp, int row, int col, const char *s, uint8_t attr)
{
    int i = 0;
    if (!vdp || !s) {
        return;
    }
    while (s[i] != '\0' && (col + i) < (int)vdp->cols) {
        if (col + i >= 0) {
            ti99_vdp_putc(vdp, row, col + i, s[i], attr);
        }
        ++i;
    }
}

int ti99_vdp_find_text(const Ti99VdpState *vdp, const char *needle)
{
    int r;
    size_t nlen;
    if (!vdp || !needle || !needle[0]) {
        return 0;
    }
    nlen = strlen(needle);
    for (r = 0; r < (int)vdp->rows; ++r) {
        int c;
        for (c = 0; c + (int)nlen <= (int)vdp->cols; ++c) {
            if (memcmp(&vdp->screen[r][c], needle, nlen) == 0) {
                return 1;
            }
        }
    }
    return 0;
}
