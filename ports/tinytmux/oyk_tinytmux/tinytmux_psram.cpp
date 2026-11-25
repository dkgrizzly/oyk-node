/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "tinytmux_psram.hpp"
#include "board_tinytmux.hpp"

#include <stdio.h>
#include <string.h>

static TinyTmuxPsramInfo g_psram_info;

#if !defined(OYK_TARGET_IMXRT1060)
static oyk_u32* s_sim_buf[TINYTMUX_PSRAM_MAX_CHIPS] = { NULL, NULL };
static size_t s_sim_words[TINYTMUX_PSRAM_MAX_CHIPS] = { 0, 0 };
static oyk_u8 s_sim_mfg[TINYTMUX_PSRAM_MAX_CHIPS] = { 0, 0 };
static oyk_u8 s_sim_kgd[TINYTMUX_PSRAM_MAX_CHIPS] = { 0, 0 };
static oyk_u32 s_sim_fault_mask[TINYTMUX_PSRAM_MAX_CHIPS] = { 0UL, 0UL };
#endif

static void safe_copy_psram(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/*
 * Check if fault injection is active in host tests; on hardware this reads/writes
 * directly to the FlexSPI2 memory-mapped aperture.
 */
static void psram_wr32(int sim_chip_idx, volatile oyk_u32* base, size_t idx, oyk_u32 val) {
#if !defined(OYK_TARGET_IMXRT1060)
    if (sim_chip_idx >= 0 && sim_chip_idx < TINYTMUX_PSRAM_MAX_CHIPS) {
        val |= s_sim_fault_mask[sim_chip_idx];
    }
#else
    (void)sim_chip_idx;
#endif
    base[idx] = val;
}

static int run_word_test_internal(int sim_chip_idx,
                                  volatile oyk_u32* base,
                                  size_t word_count,
                                  oyk_u32 logical_base_addr,
                                  oyk_u32* fail_addr_out) {
    if (!base || word_count < 16) return 0;

    /* 1. Data bus walking-1 and walking-0 across all 32 data lines */
    for (int b = 0; b < 32; ++b) {
        oyk_u32 pat1 = (1UL << b);
        psram_wr32(sim_chip_idx, base, 0, pat1);
        if (base[0] != pat1) {
            if (fail_addr_out) *fail_addr_out = logical_base_addr;
            return 0;
        }
        oyk_u32 pat0 = ~pat1;
        psram_wr32(sim_chip_idx, base, 0, pat0);
        if (base[0] != pat0) {
            if (fail_addr_out) *fail_addr_out = logical_base_addr;
            return 0;
        }
    }

    /* 2. Power-of-two address-line alias check */
    psram_wr32(sim_chip_idx, base, 0, 0xA55A0000UL);
    for (size_t off = 1; off < word_count; off <<= 1) {
        psram_wr32(sim_chip_idx, base, off, 0x5AA50000UL ^ static_cast<oyk_u32>(off));
        if (base[0] != 0xA55A0000UL) {
            if (fail_addr_out) *fail_addr_out = logical_base_addr + static_cast<oyk_u32>(off * 4UL);
            return 0;
        }
    }
    for (size_t off = 1; off < word_count; off <<= 1) {
        if (base[off] != (0x5AA50000UL ^ static_cast<oyk_u32>(off))) {
            if (fail_addr_out) *fail_addr_out = logical_base_addr + static_cast<oyk_u32>(off * 4UL);
            return 0;
        }
        psram_wr32(sim_chip_idx, base, off, 0UL);
    }
    psram_wr32(sim_chip_idx, base, 0, 0UL);

    /* 3. Sampled multi-page sweep (up to 256 evenly distributed pages, 4 words/page) */
    const size_t max_samples = 256;
    const size_t words_per_sample = 4;
    size_t step = word_count / max_samples;
    if (step < words_per_sample) step = words_per_sample;

    /* Pass A: 0xAA55AA55 checkerboard + address uniqueness write & verify */
    for (size_t base_idx = 0; base_idx + words_per_sample <= word_count; base_idx += step) {
        for (size_t w = 0; w < words_per_sample; ++w) {
            oyk_u32 addr = logical_base_addr + static_cast<oyk_u32>((base_idx + w) * 4UL);
            oyk_u32 pat = ((w & 1U) == 0U) ? 0xAA55AA55UL : (addr ^ 0xA55A5AA5UL);
            psram_wr32(sim_chip_idx, base, base_idx + w, pat);
        }
    }
    for (size_t base_idx = 0; base_idx + words_per_sample <= word_count; base_idx += step) {
        for (size_t w = 0; w < words_per_sample; ++w) {
            oyk_u32 addr = logical_base_addr + static_cast<oyk_u32>((base_idx + w) * 4UL);
            oyk_u32 expect = ((w & 1U) == 0U) ? 0xAA55AA55UL : (addr ^ 0xA55A5AA5UL);
            if (base[base_idx + w] != expect) {
                if (fail_addr_out) *fail_addr_out = addr;
                return 0;
            }
        }
    }

    /* Pass B: Complement 0x55AA55AA checkerboard verify, then clear cleanly to 0 */
    for (size_t base_idx = 0; base_idx + words_per_sample <= word_count; base_idx += step) {
        for (size_t w = 0; w < words_per_sample; ++w) {
            psram_wr32(sim_chip_idx, base, base_idx + w, 0x55AA55AAUL);
        }
    }
    for (size_t base_idx = 0; base_idx + words_per_sample <= word_count; base_idx += step) {
        for (size_t w = 0; w < words_per_sample; ++w) {
            oyk_u32 addr = logical_base_addr + static_cast<oyk_u32>((base_idx + w) * 4UL);
            if (base[base_idx + w] != 0x55AA55AAUL) {
                if (fail_addr_out) *fail_addr_out = addr;
                return 0;
            }
            psram_wr32(sim_chip_idx, base, base_idx + w, 0UL);
        }
    }
    return 1;
}

int tinytmux_psram_test_region_words(volatile oyk_u32* base,
                                     size_t word_count,
                                     oyk_u32 logical_base_addr,
                                     oyk_u32* fail_addr_out) {
    return run_word_test_internal(-1, base, word_count, logical_base_addr, fail_addr_out);
}

#if defined(TEENSYDUINO)

extern "C" uint8_t external_psram_size;

static void hardware_probe_and_test(TinyTmuxPsramInfo* info) {
    /*
     * Teensyduino startup.c configures FlexSPI2 at 0x70000000 and sets
     * external_psram_size to 0, 8, or 16 MB.
     */
    oyk_u8 mb = static_cast<oyk_u8>(external_psram_size);
    int chips = (mb >= 16U) ? 2 : ((mb >= 8U) ? 1 : 0);
    for (int i = 0; i < chips && i < TINYTMUX_PSRAM_MAX_CHIPS; ++i) {
        oyk_u32 base_addr = TINYTMUX_PSRAM_BASE + static_cast<oyk_u32>(i) * TINYTMUX_PSRAM_CHIP_BYTES;
        info->chips[i].detected = 1;
        info->chips[i].mfg_id = 0x0DU;
        info->chips[i].kgd = 0x5DU;
        info->chips[i].base_addr = base_addr;
        info->chips[i].size_bytes = TINYTMUX_PSRAM_CHIP_BYTES;

        volatile oyk_u32* ptr = reinterpret_cast<volatile oyk_u32*>(base_addr);
        size_t words = TINYTMUX_PSRAM_CHIP_BYTES / 4UL;
        oyk_u32 fail_addr = 0;
        info->chips[i].passed = run_word_test_internal(i, ptr, words, base_addr, &fail_addr);
        info->chips[i].fail_addr = fail_addr;
    }
}

#else

static void hardware_probe_and_test(TinyTmuxPsramInfo* info) {
    for (int i = 0; i < TINYTMUX_PSRAM_MAX_CHIPS; ++i) {
        if (s_sim_buf[i] != NULL && s_sim_words[i] >= 16 && s_sim_kgd[i] == 0x5DU) {
            oyk_u32 base_addr = TINYTMUX_PSRAM_BASE + static_cast<oyk_u32>(i) * TINYTMUX_PSRAM_CHIP_BYTES;
            info->chips[i].detected = 1;
            info->chips[i].mfg_id = s_sim_mfg[i];
            info->chips[i].kgd = s_sim_kgd[i];
            info->chips[i].base_addr = base_addr;
            info->chips[i].size_bytes = TINYTMUX_PSRAM_CHIP_BYTES;
            oyk_u32 fail_addr = 0;
            info->chips[i].passed = run_word_test_internal(i, s_sim_buf[i], s_sim_words[i], base_addr, &fail_addr);
            info->chips[i].fail_addr = fail_addr;
        }
    }
}

#endif

static void finalize_psram_summary(TinyTmuxPsramInfo* info, oyk_u32 elapsed_ms) {
    info->chip_count = 0;
    info->usable_chip_count = 0;
    info->detected_bytes = 0;
    info->usable_bytes = 0;
    info->base_addr = 0;
    info->test_time_ms = elapsed_ms;

    for (int i = 0; i < TINYTMUX_PSRAM_MAX_CHIPS; ++i) {
        if (info->chips[i].detected) {
            info->chip_count++;
            info->detected_bytes += info->chips[i].size_bytes;
        }
    }
    /* Contiguous usable memory always starts at CS0 (0x70000000) */
    if (info->chips[0].detected && info->chips[0].passed) {
        info->base_addr = info->chips[0].base_addr;
        info->usable_chip_count = 1;
        info->usable_bytes = info->chips[0].size_bytes;
        if (info->chips[1].detected && info->chips[1].passed) {
            info->usable_chip_count = 2;
            info->usable_bytes += info->chips[1].size_bytes;
        }
    }
}

const TinyTmuxPsramInfo* tinytmux_psram_init_and_test() {
    memset(&g_psram_info, 0, sizeof(g_psram_info));
    oyk_u32 t0 = tinytmux_board_millis();
    hardware_probe_and_test(&g_psram_info);
    oyk_u32 t1 = tinytmux_board_millis();
    finalize_psram_summary(&g_psram_info, (t1 >= t0) ? (t1 - t0) : 0UL);
    return &g_psram_info;
}

const TinyTmuxPsramInfo* tinytmux_psram_retest() {
    return tinytmux_psram_init_and_test();
}

const TinyTmuxPsramInfo* tinytmux_psram_info() {
    return &g_psram_info;
}

void tinytmux_psram_format_short(const TinyTmuxPsramInfo* info, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    if (!info || info->chip_count == 0) {
        safe_copy_psram(out, out_sz, "NO");
        return;
    }
    if (info->usable_chip_count == 0) {
        safe_copy_psram(out, out_sz, "ERR");
        return;
    }
    if (info->usable_bytes >= 16UL * 1024UL * 1024UL) {
        safe_copy_psram(out, out_sz, "16M");
    } else if (info->usable_bytes >= 8UL * 1024UL * 1024UL) {
        safe_copy_psram(out, out_sz, (info->chip_count > 1) ? "8M!" : "8M");
    } else {
        safe_copy_psram(out, out_sz, "ERR");
    }
}

void tinytmux_psram_set_sim_chips(oyk_u32* cs0_buf, size_t cs0_words, oyk_u8 cs0_mfg, oyk_u8 cs0_kgd,
                                  oyk_u32* cs1_buf, size_t cs1_words, oyk_u8 cs1_mfg, oyk_u8 cs1_kgd) {
#if !defined(OYK_TARGET_IMXRT1060)
    s_sim_buf[0] = cs0_buf;
    s_sim_words[0] = cs0_words;
    s_sim_mfg[0] = cs0_mfg;
    s_sim_kgd[0] = cs0_kgd;
    s_sim_buf[1] = cs1_buf;
    s_sim_words[1] = cs1_words;
    s_sim_mfg[1] = cs1_mfg;
    s_sim_kgd[1] = cs1_kgd;
#else
    (void)cs0_buf; (void)cs0_words; (void)cs0_mfg; (void)cs0_kgd;
    (void)cs1_buf; (void)cs1_words; (void)cs1_mfg; (void)cs1_kgd;
#endif
}

void tinytmux_psram_set_sim_fault_mask(int chip_idx, oyk_u32 stuck_or_mask) {
#if !defined(OYK_TARGET_IMXRT1060)
    if (chip_idx >= 0 && chip_idx < TINYTMUX_PSRAM_MAX_CHIPS) {
        s_sim_fault_mask[chip_idx] = stuck_or_mask;
    }
#else
    (void)chip_idx;
    (void)stuck_or_mask;
#endif
}
