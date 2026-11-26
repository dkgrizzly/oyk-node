/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_PICO_PSRAM_HPP
#define OYK_PICO_PSRAM_HPP

#include "oyk_pico_config.h"
#include "oyk_portable.hpp"

enum PicoPsramStatus {
    PICO_PSRAM_ABSENT = 0,
    PICO_PSRAM_READY,
    PICO_PSRAM_TEST_FAILED
};

struct PicoPsramInfo {
    PicoPsramStatus status;
    oyk_u32 size_bytes;
    oyk_u32 pool_total_bytes;
    oyk_u32 pool_used_bytes;
    oyk_u32 sram_fallback_used_bytes;
    int self_test_passed;
    char chip_label[40];
};

int pico_psram_init_and_test();
const PicoPsramInfo* pico_psram_info();
void pico_psram_format_short(const PicoPsramInfo* info, char* out, size_t out_sz);
void* pico_psram_alloc(size_t bytes);
void pico_psram_free(void* ptr, size_t bytes);
void pico_psram_set_sim_available(int present, oyk_u32 size_bytes, const char* label);

#endif /* OYK_PICO_PSRAM_HPP */
