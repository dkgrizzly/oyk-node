/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "tinytmux_psram.hpp"
#include "board_tinytmux.hpp"

#include <stdio.h>
#include <string.h>

static TinyTmuxPsramInfo g_psram_info;
static TinyTmuxMarchCStatus g_marchc_status;
static size_t s_mc_done = 0;

static const char* const s_marchc_pass_names[6] = {
    "1/6 up w0",
    "2/6 up r0,w1",
    "3/6 up r1,w0",
    "4/6 dn r0,w1",
    "5/6 dn r1,w0",
    "6/6 up r0"
};

#if !defined(OYK_TARGET_IMXRT1060)
static oyk_u32* s_sim_buf[TINYTMUX_PSRAM_MAX_CHIPS] = { NULL, NULL };
static size_t s_sim_words[TINYTMUX_PSRAM_MAX_CHIPS] = { 0, 0 };
static oyk_u8 s_sim_mfg[TINYTMUX_PSRAM_MAX_CHIPS] = { 0, 0 };
static oyk_u8 s_sim_kgd[TINYTMUX_PSRAM_MAX_CHIPS] = { 0, 0 };
static oyk_u32 s_sim_fault_mask[TINYTMUX_PSRAM_MAX_CHIPS] = { 0UL, 0UL };
static int s_sim_flash_kind[TINYTMUX_PSRAM_MAX_CHIPS] = { 0, 0 };
static oyk_u8 s_sim_flash_id[TINYTMUX_PSRAM_MAX_CHIPS][3] = { {0,0,0}, {0,0,0} };
static oyk_u32 s_sim_flash_bytes[TINYTMUX_PSRAM_MAX_CHIPS] = { 0UL, 0UL };
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

#include <Arduino.h>

extern "C" uint8_t external_psram_size;

static void flexspi2_probe_jedec_id(uint32_t probe_addr, oyk_u8 id_out[3]) {
    id_out[0] = 0; id_out[1] = 0; id_out[2] = 0;
    FLEXSPI2_LUTKEY = FLEXSPI_LUTKEY_VALUE;
    FLEXSPI2_LUTCR = FLEXSPI_LUTCR_UNLOCK;
    FLEXSPI2_LUT32 = FLEXSPI_LUT_INSTRUCTION(FLEXSPI_LUT_OPCODE_CMD_SDR, FLEXSPI_LUT_NUM_PADS_1, 0x9F) |
                     (FLEXSPI_LUT_INSTRUCTION(FLEXSPI_LUT_OPCODE_READ_SDR, FLEXSPI_LUT_NUM_PADS_1, 1) << 16);
    FLEXSPI2_LUT33 = 0;

    FLEXSPI2_INTR = FLEXSPI_INTR_IPRXWA | FLEXSPI_INTR_IPCMDDONE | FLEXSPI_INTR_IPCMDERR;
    FLEXSPI2_IPRXFCR = FLEXSPI_IPRXFCR_CLRIPRXF | FLEXSPI_IPRXFCR_RXWMRK(0);
    FLEXSPI2_IPCR0 = probe_addr;
    FLEXSPI2_IPCR1 = FLEXSPI_IPCR1_ISEQID(8) | FLEXSPI_IPCR1_IDATSZ(4);
    FLEXSPI2_IPCMD = FLEXSPI_IPCMD_TRG;

    oyk_u32 guard = 100000UL;
    while (!(FLEXSPI2_INTR & FLEXSPI_INTR_IPCMDDONE) && --guard > 0UL) {
    }
    uint32_t raw = FLEXSPI2_RFDR0;
    FLEXSPI2_INTR = FLEXSPI_INTR_IPCMDDONE | FLEXSPI_INTR_IPRXWA | FLEXSPI_INTR_IPCMDERR;
    id_out[0] = static_cast<oyk_u8>(raw & 0xFFU);
    id_out[1] = static_cast<oyk_u8>((raw >> 8) & 0xFFU);
    id_out[2] = static_cast<oyk_u8>((raw >> 16) & 0xFFU);
}

static void classify_qspi_flash_id(const oyk_u8 id[3], TinyTmuxPsramChipInfo* c) {
    /* Exclude empty/floating bus and PSRAM KGD (0x5D) */
    if ((id[0] == 0x00U && id[1] == 0x00U) || (id[0] == 0xFFU && id[1] == 0xFFU && id[2] == 0xFFU)) return;
    if (id[1] == 0x5DU && (id[0] == 0x0DU || id[0] == 0x9DU)) return;

    /* Common QSPI SPI-NAND IDs (1-byte dummy in 0x9F, or direct mfg/dev) */
    if ((id[0] == 0xFFU && id[1] == 0xEFU && (id[2] == 0xAAU || id[2] == 0xABU)) ||
        (id[0] == 0xEFU && (id[1] == 0xAAU || id[1] == 0xABU)) ||
        (id[0] == 0xC8U && (id[1] == 0xD1U || id[1] == 0xD2U || id[1] == 0xD4U ||
                            id[1] == 0xB1U || id[1] == 0xB2U || id[1] == 0xB4U))) {
        c->detected = 1;
        c->chip_kind = TMUX_FLEXSPI_FLASH_NAND;
        c->mfg_id = (id[0] == 0xFFU) ? id[1] : id[0];
        c->kgd = (id[0] == 0xFFU) ? id[2] : id[1];
        c->jedec_id[0] = id[0]; c->jedec_id[1] = id[1]; c->jedec_id[2] = id[2];
        c->size_bytes = 128UL * 1024UL * 1024UL;
        safe_copy_psram(c->chip_label, sizeof(c->chip_label), "QSPI NAND Flash");
        return;
    }

    /* Standard JEDEC QSPI NOR Flash (W25Q, MX25, GD25, S25FL, IS25, MT25, AT25, SST26) */
    int known_mfg = (id[0] == 0xEFU || id[0] == 0xC2U || id[0] == 0xC8U || id[0] == 0x01U ||
                     id[0] == 0x20U || id[0] == 0x9DU || id[0] == 0x1FU || id[0] == 0xBFU ||
                     id[0] == 0x85U);
    if (known_mfg && id[2] >= 0x14U && id[2] <= 0x22U) {
        c->detected = 1;
        c->chip_kind = TMUX_FLEXSPI_FLASH_NOR;
        c->mfg_id = id[0];
        c->kgd = id[1];
        c->jedec_id[0] = id[0]; c->jedec_id[1] = id[1]; c->jedec_id[2] = id[2];
        c->size_bytes = (1UL << id[2]);
        safe_copy_psram(c->chip_label, sizeof(c->chip_label), "QSPI NOR Flash");
    }
}

OYK_FLASHMEM static void hardware_probe_and_test(TinyTmuxPsramInfo* info) {
    /*
     * Teensyduino startup.c configures FlexSPI2 at 0x70000000 and sets
     * external_psram_size to 0, 8, or 16 MB.
     */
    oyk_u8 mb = static_cast<oyk_u8>(external_psram_size);
    int chips = (mb >= 16U) ? 2 : ((mb >= 8U) ? 1 : 0);
    for (int i = 0; i < chips && i < TINYTMUX_PSRAM_MAX_CHIPS; ++i) {
        oyk_u32 base_addr = TINYTMUX_PSRAM_BASE + static_cast<oyk_u32>(i) * TINYTMUX_PSRAM_CHIP_BYTES;
        info->chips[i].detected = 1;
        info->chips[i].chip_kind = TMUX_FLEXSPI_PSRAM;
        info->chips[i].mfg_id = 0x0DU;
        info->chips[i].kgd = 0x5DU;
        info->chips[i].jedec_id[0] = 0x0DU;
        info->chips[i].jedec_id[1] = 0x5DU;
        info->chips[i].jedec_id[2] = 0x00U;
        info->chips[i].base_addr = base_addr;
        info->chips[i].size_bytes = TINYTMUX_PSRAM_CHIP_BYTES;
        safe_copy_psram(info->chips[i].chip_label, sizeof(info->chips[i].chip_label), "QSPI PSRAM");

        volatile oyk_u32* ptr = reinterpret_cast<volatile oyk_u32*>(base_addr);
        size_t words = TINYTMUX_PSRAM_CHIP_BYTES / 4UL;
        oyk_u32 fail_addr = 0;
        info->chips[i].passed = run_word_test_internal(i, ptr, words, base_addr, &fail_addr);
        info->chips[i].fail_addr = fail_addr;
    }

    /* Probe non-PSRAM FlexSPI2 slots via JEDEC 0x9F to detect QSPI Flash on CS1 or misplaced on CS0 */
    if (chips == 0) {
        oyk_u8 id0[3];
        flexspi2_probe_jedec_id(0UL, id0);
        classify_qspi_flash_id(id0, &info->chips[0]);
        if (info->chips[0].detected) {
            info->chips[0].base_addr = TINYTMUX_PSRAM_BASE;
        }
    }
    if (chips < 2) {
        uint32_t cs1_off = (FLEXSPI2_FLSHA1CR0 & 0x7FFFFFUL) << 10;
        oyk_u8 id1[3];
        flexspi2_probe_jedec_id(cs1_off, id1);
        classify_qspi_flash_id(id1, &info->chips[1]);
        if (info->chips[1].detected) {
            info->chips[1].base_addr = TINYTMUX_PSRAM_BASE + cs1_off;
        }
    }
}

#else

OYK_FLASHMEM static void hardware_probe_and_test(TinyTmuxPsramInfo* info) {
    for (int i = 0; i < TINYTMUX_PSRAM_MAX_CHIPS; ++i) {
        if (s_sim_buf[i] != NULL && s_sim_words[i] >= 16 && s_sim_kgd[i] == 0x5DU) {
            oyk_u32 base_addr = TINYTMUX_PSRAM_BASE + static_cast<oyk_u32>(i) * TINYTMUX_PSRAM_CHIP_BYTES;
            info->chips[i].detected = 1;
            info->chips[i].chip_kind = TMUX_FLEXSPI_PSRAM;
            info->chips[i].mfg_id = s_sim_mfg[i];
            info->chips[i].kgd = s_sim_kgd[i];
            info->chips[i].jedec_id[0] = s_sim_mfg[i];
            info->chips[i].jedec_id[1] = s_sim_kgd[i];
            info->chips[i].jedec_id[2] = 0x00U;
            info->chips[i].base_addr = base_addr;
            info->chips[i].size_bytes = TINYTMUX_PSRAM_CHIP_BYTES;
            safe_copy_psram(info->chips[i].chip_label, sizeof(info->chips[i].chip_label), "QSPI PSRAM");
            oyk_u32 fail_addr = 0;
            info->chips[i].passed = run_word_test_internal(i, s_sim_buf[i], s_sim_words[i], base_addr, &fail_addr);
            info->chips[i].fail_addr = fail_addr;
        } else if (s_sim_flash_kind[i] == TMUX_FLEXSPI_FLASH_NOR ||
                   s_sim_flash_kind[i] == TMUX_FLEXSPI_FLASH_NAND) {
            info->chips[i].detected = 1;
            info->chips[i].chip_kind = s_sim_flash_kind[i];
            info->chips[i].mfg_id = s_sim_flash_id[i][0];
            info->chips[i].kgd = s_sim_flash_id[i][1];
            info->chips[i].jedec_id[0] = s_sim_flash_id[i][0];
            info->chips[i].jedec_id[1] = s_sim_flash_id[i][1];
            info->chips[i].jedec_id[2] = s_sim_flash_id[i][2];
            info->chips[i].base_addr = TINYTMUX_PSRAM_BASE + static_cast<oyk_u32>(i) * TINYTMUX_PSRAM_CHIP_BYTES;
            info->chips[i].size_bytes = s_sim_flash_bytes[i];
            safe_copy_psram(info->chips[i].chip_label, sizeof(info->chips[i].chip_label),
                            (s_sim_flash_kind[i] == TMUX_FLEXSPI_FLASH_NAND) ? "QSPI NAND Flash" : "QSPI NOR Flash");
        }
    }
}

#endif

static void finalize_psram_summary(TinyTmuxPsramInfo* info, oyk_u32 elapsed_ms) {
    info->chip_count = 0;
    info->usable_chip_count = 0;
    info->misplaced_flash_cs0 = 0;
    info->has_valid_qspi_flash_cs1 = 0;
    info->detected_bytes = 0;
    info->usable_bytes = 0;
    info->base_addr = 0;
    info->test_time_ms = elapsed_ms;

    if (info->chips[0].chip_kind == TMUX_FLEXSPI_FLASH_NOR ||
        info->chips[0].chip_kind == TMUX_FLEXSPI_FLASH_NAND) {
        info->misplaced_flash_cs0 = 1;
    }
    if (info->chips[1].chip_kind == TMUX_FLEXSPI_FLASH_NOR ||
        info->chips[1].chip_kind == TMUX_FLEXSPI_FLASH_NAND) {
        info->has_valid_qspi_flash_cs1 = 1;
    }

    for (int i = 0; i < TINYTMUX_PSRAM_MAX_CHIPS; ++i) {
        if (info->chips[i].detected && info->chips[i].chip_kind == TMUX_FLEXSPI_PSRAM) {
            info->chip_count++;
            info->detected_bytes += info->chips[i].size_bytes;
        }
    }
    /* Contiguous usable PSRAM memory always starts at CS0 (0x70000000) */
    if (info->chips[0].detected && info->chips[0].chip_kind == TMUX_FLEXSPI_PSRAM && info->chips[0].passed) {
        info->base_addr = info->chips[0].base_addr;
        info->usable_chip_count = 1;
        info->usable_bytes = info->chips[0].size_bytes;
        if (info->chips[1].detected && info->chips[1].chip_kind == TMUX_FLEXSPI_PSRAM && info->chips[1].passed) {
            info->usable_chip_count = 2;
            info->usable_bytes += info->chips[1].size_bytes;
        }
    }
}

OYK_FLASHMEM const TinyTmuxPsramInfo* tinytmux_psram_init_and_test() {
    memset(&g_psram_info, 0, sizeof(g_psram_info));
    memset(&g_marchc_status, 0, sizeof(g_marchc_status));
    g_marchc_status.fail_chip = -1;
    g_marchc_status.active_chip = -1;
    s_mc_done = 0;
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

const char* tinytmux_psram_footprint_label(int chip_idx) {
    if (chip_idx == 0) {
        return "Small pads (U5 / CS0 / Pin 48, closer to microSD)";
    }
    if (chip_idx == 1) {
        return "Large pads (U6 / CS1 / Pin 51, closer to Ethernet)";
    }
    return "Unknown footprint";
}

static volatile oyk_u32* marchc_chip_ptr(int chip_idx, size_t* words_out) {
#if !defined(OYK_TARGET_IMXRT1060)
    if (chip_idx >= 0 && chip_idx < TINYTMUX_PSRAM_MAX_CHIPS &&
        g_psram_info.chips[chip_idx].chip_kind == TMUX_FLEXSPI_PSRAM &&
        s_sim_buf[chip_idx] != NULL && s_sim_words[chip_idx] >= 16) {
        if (words_out) *words_out = s_sim_words[chip_idx];
        return s_sim_buf[chip_idx];
    }
    if (words_out) *words_out = 0;
    return NULL;
#else
    if (chip_idx >= 0 && chip_idx < TINYTMUX_PSRAM_MAX_CHIPS &&
        g_psram_info.chips[chip_idx].detected &&
        g_psram_info.chips[chip_idx].chip_kind == TMUX_FLEXSPI_PSRAM &&
        g_psram_info.chips[chip_idx].size_bytes >= 64) {
        if (words_out) *words_out = static_cast<size_t>(g_psram_info.chips[chip_idx].size_bytes / 4UL);
        return reinterpret_cast<volatile oyk_u32*>(g_psram_info.chips[chip_idx].base_addr);
    }
    if (words_out) *words_out = 0;
    return NULL;
#endif
}

static void marchc_record_failure(int chip_idx, size_t word_idx, oyk_u32 expect_val, oyk_u32 actual_val, oyk_u32 now_ms) {
    oyk_u32 addr = g_psram_info.chips[chip_idx].base_addr + static_cast<oyk_u32>(word_idx * 4UL);
    g_marchc_status.state = TMUX_MARCHC_FAILED;
    g_marchc_status.chip_result[chip_idx] = TMUX_MARCHC_CHIP_FAIL;
    g_marchc_status.fail_chip = chip_idx;
    g_marchc_status.fail_pass = g_marchc_status.active_pass;
    g_marchc_status.fail_addr = addr;
    g_marchc_status.fail_expected = expect_val;
    g_marchc_status.fail_actual = actual_val;
    g_marchc_status.elapsed_ms = (now_ms >= g_marchc_status.start_ms) ? (now_ms - g_marchc_status.start_ms) : 0UL;
    g_psram_info.chips[chip_idx].passed = 0;
    g_psram_info.chips[chip_idx].fail_addr = addr;
    finalize_psram_summary(&g_psram_info, g_marchc_status.elapsed_ms);
}

int tinytmux_psram_marchc_start(oyk_u32 now_ms) {
    memset(&g_marchc_status, 0, sizeof(g_marchc_status));
    g_marchc_status.fail_chip = -1;
    g_marchc_status.active_chip = -1;
    g_marchc_status.start_ms = now_ms;
    s_mc_done = 0;

    int first_chip = -1;
    for (int i = 0; i < TINYTMUX_PSRAM_MAX_CHIPS; ++i) {
        size_t w = 0;
        if (g_psram_info.chips[i].detected && marchc_chip_ptr(i, &w) != NULL) {
            g_marchc_status.chip_result[i] = TMUX_MARCHC_CHIP_PENDING;
            if (first_chip < 0) first_chip = i;
        } else {
            g_marchc_status.chip_result[i] = TMUX_MARCHC_CHIP_NONE;
        }
    }
    if (first_chip < 0) {
        g_marchc_status.state = TMUX_MARCHC_IDLE;
        safe_copy_psram(g_marchc_status.pass_label, sizeof(g_marchc_status.pass_label), "No PSRAM");
        return 0;
    }

    g_marchc_status.state = TMUX_MARCHC_RUNNING;
    g_marchc_status.active_chip = first_chip;
    g_marchc_status.active_pass = 0;
    g_marchc_status.chip_result[first_chip] = TMUX_MARCHC_CHIP_RUNNING;
    safe_copy_psram(g_marchc_status.pass_label, sizeof(g_marchc_status.pass_label), s_marchc_pass_names[0]);
    return 1;
}

int tinytmux_psram_marchc_step(oyk_u32 now_ms, size_t max_words_per_step) {
    if (g_marchc_status.state != TMUX_MARCHC_RUNNING) return 0;
    if (max_words_per_step == 0) max_words_per_step = 4096;

    int chip = g_marchc_status.active_chip;
    size_t words = 0;
    volatile oyk_u32* base = marchc_chip_ptr(chip, &words);
    if (!base || words < 16) {
        marchc_record_failure(chip >= 0 ? chip : 0, 0, 0UL, 0xFFFFFFFFUL, now_ms);
        return 0;
    }

    int pass = g_marchc_status.active_pass;
    size_t budget = max_words_per_step;
    while (budget > 0 && s_mc_done < words) {
        size_t idx = (pass == 3 || pass == 4) ? (words - 1U - s_mc_done) : s_mc_done;
        if (pass == 0) {
            psram_wr32(chip, base, idx, 0UL);
        } else if (pass == 1 || pass == 3) {
            oyk_u32 got = base[idx];
            if (got != 0UL) {
                marchc_record_failure(chip, idx, 0UL, got, now_ms);
                return 0;
            }
            psram_wr32(chip, base, idx, 0xFFFFFFFFUL);
        } else if (pass == 2 || pass == 4) {
            oyk_u32 got = base[idx];
            if (got != 0xFFFFFFFFUL) {
                marchc_record_failure(chip, idx, 0xFFFFFFFFUL, got, now_ms);
                return 0;
            }
            psram_wr32(chip, base, idx, 0UL);
        } else {
            oyk_u32 got = base[idx];
            if (got != 0UL) {
                marchc_record_failure(chip, idx, 0UL, got, now_ms);
                return 0;
            }
        }
        s_mc_done++;
        budget--;
    }

    oyk_u32 pct = (static_cast<oyk_u32>(pass) * 100UL +
                   static_cast<oyk_u32>((s_mc_done * 100UL) / words)) / 6UL;
    if (pct > 100UL) pct = 100UL;
    g_marchc_status.chip_percent[chip] = static_cast<oyk_u8>(pct);
    g_marchc_status.elapsed_ms = (now_ms >= g_marchc_status.start_ms) ? (now_ms - g_marchc_status.start_ms) : 0UL;

    if (s_mc_done < words) {
        return 1;
    }

    s_mc_done = 0;
    if (pass + 1 < 6) {
        g_marchc_status.active_pass = pass + 1;
        safe_copy_psram(g_marchc_status.pass_label, sizeof(g_marchc_status.pass_label),
                        s_marchc_pass_names[g_marchc_status.active_pass]);
        return 1;
    }

    g_marchc_status.chip_percent[chip] = 100;
    g_marchc_status.chip_result[chip] = TMUX_MARCHC_CHIP_PASS;
    g_psram_info.chips[chip].passed = 1;
    g_psram_info.chips[chip].fail_addr = 0UL;

    int next_chip = -1;
    for (int i = chip + 1; i < TINYTMUX_PSRAM_MAX_CHIPS; ++i) {
        size_t nw = 0;
        if (g_marchc_status.chip_result[i] == TMUX_MARCHC_CHIP_PENDING && marchc_chip_ptr(i, &nw) != NULL) {
            next_chip = i;
            break;
        }
    }
    if (next_chip >= 0) {
        g_marchc_status.active_chip = next_chip;
        g_marchc_status.active_pass = 0;
        g_marchc_status.chip_result[next_chip] = TMUX_MARCHC_CHIP_RUNNING;
        safe_copy_psram(g_marchc_status.pass_label, sizeof(g_marchc_status.pass_label),
                        s_marchc_pass_names[0]);
        return 1;
    }

    g_marchc_status.active_chip = -1;
    g_marchc_status.state = TMUX_MARCHC_PASSED;
    safe_copy_psram(g_marchc_status.pass_label, sizeof(g_marchc_status.pass_label), "Complete");
    finalize_psram_summary(&g_psram_info, g_marchc_status.elapsed_ms);
    return 0;
}

void tinytmux_psram_marchc_cancel() {
    if (g_marchc_status.state == TMUX_MARCHC_RUNNING) {
        g_marchc_status.state = TMUX_MARCHC_CANCELLED;
        g_marchc_status.active_chip = -1;
        safe_copy_psram(g_marchc_status.pass_label, sizeof(g_marchc_status.pass_label), "Cancelled");
    }
}

const TinyTmuxMarchCStatus* tinytmux_psram_marchc_status() {
    return &g_marchc_status;
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

void tinytmux_psram_set_sim_flash(int chip_idx, int kind, oyk_u8 mfg, oyk_u8 type, oyk_u8 cap, oyk_u32 size_bytes) {
#if !defined(OYK_TARGET_IMXRT1060)
    if (chip_idx >= 0 && chip_idx < TINYTMUX_PSRAM_MAX_CHIPS) {
        s_sim_flash_kind[chip_idx] = kind;
        s_sim_flash_id[chip_idx][0] = mfg;
        s_sim_flash_id[chip_idx][1] = type;
        s_sim_flash_id[chip_idx][2] = cap;
        s_sim_flash_bytes[chip_idx] = size_bytes;
    }
#else
    (void)chip_idx; (void)kind; (void)mfg; (void)type; (void)cap; (void)size_bytes;
#endif
}
