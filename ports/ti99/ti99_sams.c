/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */
#include "ti99_sams.h"

#include <stdlib.h>
#include <string.h>

static uint8_t ti99_sams_slot_from_addr(uint16_t cpu_addr)
{
    return (uint8_t)((cpu_addr >> 12) & 0x0FU);
}

#ifdef __TMS9900__
static void sams_cru_out(uint16_t cru_bit_addr, int val)
{
    if (val) {
        __asm__ volatile("mov %0,r12\n\tsbo 0" : : "r"(cru_bit_addr) : "r12");
    } else {
        __asm__ volatile("mov %0,r12\n\tsbz 0" : : "r"(cru_bit_addr) : "r12");
    }
}
#endif

int ti99_sams_probe(Ti99SamsState *st)
{
    uint16_t i;
    if (!st) {
        return -1;
    }
    memset(st, 0, sizeof(*st));
    for (i = 0; i < 16; ++i) {
        st->regs[i] = i;
    }
#ifdef __TMS9900__
    {
        volatile uint16_t *probe_lo = (volatile uint16_t *)0x2000U;
        volatile uint16_t *map_reg = (volatile uint16_t *)(0x4000U + 4U); /* >2000 bank reg */
        sams_cru_out(TI99_SAMS_CRU_BASE, 1);
        *map_reg = (uint16_t)((16U << 8) | 16U);
        sams_cru_out(TI99_SAMS_CRU_BASE + 2U, 1);
        probe_lo[0] = 0x5A5AU;
        *map_reg = (uint16_t)((17U << 8) | 17U);
        probe_lo[0] = 0xA5A5U;
        *map_reg = (uint16_t)((16U << 8) | 16U);
        st->detected = (probe_lo[0] == 0x5A5AU) ? 1U : 0U;
        st->mapping_enabled = st->detected;
        sams_cru_out(TI99_SAMS_CRU_BASE, 0);
    }
#else
    st->host_sim_ram = (uint8_t *)calloc((size_t)TI99_SAMS_MAX_BANKS, TI99_SAMS_PAGE_SIZE);
    st->detected = st->host_sim_ram ? 1U : 0U;
    st->mapping_enabled = st->detected;
#endif
    return st->detected ? 0 : -1;
}

void ti99_sams_enable_mapping(Ti99SamsState *st, int enable)
{
    if (!st || !st->detected) {
        return;
    }
    st->mapping_enabled = enable ? 1U : 0U;
#ifdef __TMS9900__
    sams_cru_out(TI99_SAMS_CRU_BASE + 2U, enable);
#endif
}

void ti99_sams_map_page(Ti99SamsState *st, uint16_t cpu_addr, uint16_t page)
{
    uint8_t slot;
    if (!st || !st->detected) {
        return;
    }
    slot = ti99_sams_slot_from_addr(cpu_addr);
    st->regs[slot] = page & 0xFFU;
#ifdef __TMS9900__
    {
        volatile uint16_t *map_reg = (volatile uint16_t *)(0x4000U + ((uint16_t)slot << 1));
        uint16_t p = page & 0xFFU;
        sams_cru_out(TI99_SAMS_CRU_BASE, 1);
        *map_reg = (uint16_t)((p << 8) | p);
        sams_cru_out(TI99_SAMS_CRU_BASE, 0);
    }
#endif
}

uint16_t ti99_sams_get_page(const Ti99SamsState *st, uint16_t cpu_addr)
{
    if (!st) {
        return 0U;
    }
    return st->regs[ti99_sams_slot_from_addr(cpu_addr)];
}

int ti99_sams_write_bank(Ti99SamsState *st, uint16_t page, uint16_t offset,
                         const void *src, size_t len)
{
    if (!st || !st->detected || !src || page >= TI99_SAMS_MAX_BANKS ||
        (size_t)offset + len > TI99_SAMS_PAGE_SIZE) {
        return -1;
    }
#ifdef __TMS9900__
    ti99_sams_map_page(st, 0x3000U, page);
    memcpy((void *)(0x3000U + offset), src, len);
    ti99_sams_map_page(st, 0x3000U, 3U);
#else
    if (!st->host_sim_ram) {
        return -1;
    }
    memcpy(st->host_sim_ram + ((size_t)page * TI99_SAMS_PAGE_SIZE) + offset, src, len);
#endif
    return 0;
}

int ti99_sams_read_bank(const Ti99SamsState *st, uint16_t page, uint16_t offset,
                        void *dst, size_t len)
{
    if (!st || !st->detected || !dst || page >= TI99_SAMS_MAX_BANKS ||
        (size_t)offset + len > TI99_SAMS_PAGE_SIZE) {
        return -1;
    }
#ifdef __TMS9900__
    ti99_sams_map_page((Ti99SamsState *)st, 0x3000U, page);
    memcpy(dst, (const void *)(0x3000U + offset), len);
    ti99_sams_map_page((Ti99SamsState *)st, 0x3000U, 3U);
#else
    if (!st->host_sim_ram) {
        return -1;
    }
    memcpy(dst, st->host_sim_ram + ((size_t)page * TI99_SAMS_PAGE_SIZE) + offset, len);
#endif
    return 0;
}

void ti99_sams_shutdown(Ti99SamsState *st)
{
    if (!st) {
        return;
    }
    if (st->host_sim_ram) {
        free(st->host_sim_ram);
        st->host_sim_ram = NULL;
    }
    st->detected = 0;
    st->mapping_enabled = 0;
}
