/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "pico_psram.hpp"
#include "board_pico.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PicoPsramInfo s_psram = {};
static oyk_u32 s_xip_pool_offset = 0;

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE && defined(PICO_RP2350) && PICO_RP2350 && defined(OYK_PICO_HAVE_HARDWARE_PSRAM)
#include "hardware/psram.h"

int pico_psram_init_and_test() {
    memset(&s_psram, 0, sizeof(s_psram));
    s_xip_pool_offset = 0;

    const PicoBoardProfile* prof = pico_current_board_profile();
    if (prof->psram_cs_pin < 0 || prof->max_psram_bytes == 0) {
        s_psram.status = PICO_PSRAM_ABSENT;
        strcpy(s_psram.chip_label, "None (Board has no QMI PSRAM)");
        return 0;
    }

    size_t sz = psram_is_available() ? psram_get_size() : 0U;
    if (sz == 0) {
        s_psram.status = PICO_PSRAM_ABSENT;
        strcpy(s_psram.chip_label, "Not populated on CS1");
        return 0;
    }

    /* Non-destructive probe at tail test word of detected PSRAM */
    volatile oyk_u32* xip_uncached = reinterpret_cast<volatile oyk_u32*>(0x15000000UL);
    const size_t test_idx = (sz / sizeof(oyk_u32)) - 4U;
    void* test_cached_addr = reinterpret_cast<void*>(0x11000000UL + (test_idx * sizeof(oyk_u32)));
    if (!psram_check_address(test_cached_addr)) {
        s_psram.status = PICO_PSRAM_TEST_FAILED;
        strcpy(s_psram.chip_label, "PSRAM address range check failed");
        return 0;
    }
    const oyk_u32 saved = xip_uncached[test_idx];
    xip_uncached[test_idx] = 0x5A3CC3A5UL;
    const int ok = (xip_uncached[test_idx] == 0x5A3CC3A5UL);
    xip_uncached[test_idx] = saved;

    s_psram.size_bytes = static_cast<oyk_u32>(sz);
    s_psram.pool_total_bytes = static_cast<oyk_u32>(sz - 4096U);
    s_psram.self_test_passed = ok ? 1 : 0;
    s_psram.status = ok ? PICO_PSRAM_READY : PICO_PSRAM_TEST_FAILED;
    sprintf(s_psram.chip_label, "QMI PSRAM (%lu MB)", static_cast<unsigned long>(sz / (1024UL * 1024UL)));
    return ok;
}

void* pico_psram_alloc(size_t bytes) {
    if (bytes == 0) return NULL;
    const size_t aligned = (bytes + 7U) & ~static_cast<size_t>(7U);
    if (s_psram.status == PICO_PSRAM_READY &&
        (s_xip_pool_offset + aligned) <= s_psram.pool_total_bytes) {
        void* p = reinterpret_cast<void*>(0x11000000UL + s_xip_pool_offset);
        s_xip_pool_offset += static_cast<oyk_u32>(aligned);
        s_psram.pool_used_bytes = s_xip_pool_offset;
        return p;
    }
    void* p = malloc(bytes);
    if (p) s_psram.sram_fallback_used_bytes += static_cast<oyk_u32>(bytes);
    return p;
}

void pico_psram_free(void* ptr, size_t bytes) {
    if (!ptr) return;
    oyk_u32 addr = reinterpret_cast<oyk_u32>(ptr);
    if (addr >= 0x11000000UL && addr < (0x11000000UL + s_psram.size_bytes)) {
        return; /* Bump pool region inside PSRAM */
    }
    free(ptr);
    if (s_psram.sram_fallback_used_bytes >= bytes) {
        s_psram.sram_fallback_used_bytes -= static_cast<oyk_u32>(bytes);
    }
}

void pico_psram_set_sim_available(int /*present*/, oyk_u32 /*size_bytes*/, const char* /*label*/) {}

#else

static int s_sim_psram_present = -1;
static oyk_u32 s_sim_psram_bytes = 0;
static char s_sim_psram_label[40] = "Sim QMI PSRAM (8 MB)";

void pico_psram_set_sim_available(int present, oyk_u32 size_bytes, const char* label) {
    s_sim_psram_present = present ? 1 : 0;
    s_sim_psram_bytes = size_bytes;
    if (label && label[0]) {
        strncpy(s_sim_psram_label, label, sizeof(s_sim_psram_label) - 1);
        s_sim_psram_label[sizeof(s_sim_psram_label) - 1] = '\0';
    }
}

int pico_psram_init_and_test() {
    memset(&s_psram, 0, sizeof(s_psram));
    s_xip_pool_offset = 0;

    int present = 0;
    oyk_u32 sz = 0;
    const PicoBoardProfile* prof = pico_current_board_profile();
    if (s_sim_psram_present >= 0) {
        present = s_sim_psram_present;
        sz = present ? s_sim_psram_bytes : 0UL;
    } else if (prof->max_psram_bytes > 0) {
        present = 1;
        sz = prof->max_psram_bytes;
    }

    if (!present || sz == 0) {
        s_psram.status = PICO_PSRAM_ABSENT;
        strcpy(s_psram.chip_label, "None (SRAM fallback active)");
        return 0;
    }

    s_psram.status = PICO_PSRAM_READY;
    s_psram.size_bytes = sz;
    s_psram.pool_total_bytes = sz - 4096UL;
    s_psram.self_test_passed = 1;
    strncpy(s_psram.chip_label, s_sim_psram_label, sizeof(s_psram.chip_label) - 1);
    s_psram.chip_label[sizeof(s_psram.chip_label) - 1] = '\0';
    return 1;
}

void* pico_psram_alloc(size_t bytes) {
    if (bytes == 0) return NULL;
    void* p = malloc(bytes);
    if (!p) return NULL;
    if (s_psram.status == PICO_PSRAM_READY &&
        (s_xip_pool_offset + bytes) <= s_psram.pool_total_bytes) {
        s_xip_pool_offset += static_cast<oyk_u32>(bytes);
        s_psram.pool_used_bytes = s_xip_pool_offset;
    } else {
        s_psram.sram_fallback_used_bytes += static_cast<oyk_u32>(bytes);
    }
    return p;
}

void pico_psram_free(void* ptr, size_t bytes) {
    if (!ptr) return;
    free(ptr);
    if (s_psram.sram_fallback_used_bytes >= bytes) {
        s_psram.sram_fallback_used_bytes -= static_cast<oyk_u32>(bytes);
    }
}

#endif

const PicoPsramInfo* pico_psram_info() {
    return &s_psram;
}

void pico_psram_format_short(const PicoPsramInfo* info, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    if (!info || info->status != PICO_PSRAM_READY) {
        strncpy(out, "0M", out_sz - 1);
        out[out_sz - 1] = '\0';
        return;
    }
    char buf[16];
    sprintf(buf, "%luM", static_cast<unsigned long>(info->size_bytes / (1024UL * 1024UL)));
    strncpy(out, buf, out_sz - 1);
    out[out_sz - 1] = '\0';
}
