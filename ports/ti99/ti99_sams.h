/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */
#ifndef OYK_TI99_SAMS_H
#define OYK_TI99_SAMS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TI99_SAMS_CRU_BASE      0x1E00U
#define TI99_SAMS_PAGE_SIZE     4096U
#define TI99_SAMS_MAX_BANKS     256U

typedef struct Ti99SamsState {
    uint8_t detected;
    uint8_t mapping_enabled;
    uint16_t regs[16];
    uint8_t *host_sim_ram;
} Ti99SamsState;

int ti99_sams_probe(Ti99SamsState *st);
void ti99_sams_enable_mapping(Ti99SamsState *st, int enable);
void ti99_sams_map_page(Ti99SamsState *st, uint16_t cpu_addr, uint16_t page);
uint16_t ti99_sams_get_page(const Ti99SamsState *st, uint16_t cpu_addr);
int ti99_sams_write_bank(Ti99SamsState *st, uint16_t page, uint16_t offset,
                         const void *src, size_t len);
int ti99_sams_read_bank(const Ti99SamsState *st, uint16_t page, uint16_t offset,
                        void *dst, size_t len);
void ti99_sams_shutdown(Ti99SamsState *st);

#ifdef __cplusplus
}
#endif

#endif /* OYK_TI99_SAMS_H */
