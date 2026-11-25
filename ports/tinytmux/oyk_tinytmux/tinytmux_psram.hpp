/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_TINYTMUX_PSRAM_HPP
#define OYK_TINYTMUX_PSRAM_HPP

/*
 * External QSPI PSRAM (FlexSPI2) Probe & Startup Self-Test for Teensy 4.1 (IMXRT1062).
 *
 * Teensy 4.1 supports 0, 1 (8 MB @ CS0), or 2 (16 MB @ CS0+CS1) QSPI PSRAM chips
 * (APS6404L / IPS6404) mapped via FlexSPI2 at 0x70000000..0x70FFFFFF.
 *
 * Strictly C++98 with zero STL, zero exceptions, and graceful degradation to internal
 * SRAM if 0 chips are populated or if any chip fails the startup self-test.
 */

#include "oyk_tinytmux_config.h"
#include "oyk_portable.hpp"

#define TINYTMUX_PSRAM_BASE       0x70000000UL
#define TINYTMUX_PSRAM_CHIP_BYTES (8UL * 1024UL * 1024UL) /* 8 MB per chip */
#define TINYTMUX_PSRAM_MAX_CHIPS  2

#define IMXRT_FLEXSPI2_BASE       0x402A4000UL

enum TinyTmuxFlexSpiChipKind {
    TMUX_FLEXSPI_NONE = 0,
    TMUX_FLEXSPI_PSRAM,
    TMUX_FLEXSPI_FLASH_NOR,
    TMUX_FLEXSPI_FLASH_NAND
};

struct TinyTmuxPsramChipInfo {
    int detected;       /* 1 if PSRAM or QSPI Flash was detected on this FlexSPI2 CS */
    int passed;         /* 1 if PSRAM memory self-test passed */
    int chip_kind;      /* TinyTmuxFlexSpiChipKind */
    oyk_u8 mfg_id;      /* JEDEC Manufacturer ID (e.g. 0x0D PSRAM, 0xEF Winbond Flash) */
    oyk_u8 kgd;         /* JEDEC byte 1 (0x5D = PSRAM KGD, or Flash memory type) */
    oyk_u8 jedec_id[3]; /* Raw 3-byte JEDEC 0x9F ID */
    oyk_u32 base_addr;  /* 0x70000000 (CS0) or 0x70800000 (CS1) for PSRAM */
    oyk_u32 size_bytes; /* Detected chip capacity in bytes */
    oyk_u32 fail_addr;  /* First failing logical address if passed == 0 */
    char chip_label[32];
};

struct TinyTmuxPsramInfo {
    int chip_count;               /* 0, 1, or 2 detected PSRAM chips */
    int usable_chip_count;        /* 0, 1, or 2 contiguous PSRAM chips passing self-test */
    int misplaced_flash_cs0;      /* 1 if QSPI Flash was soldered to small pads (CS0 / Pin 48) */
    int has_valid_qspi_flash_cs1; /* 1 if QSPI Flash was detected on large pads (CS1 / Pin 51) */
    oyk_u32 detected_bytes;       /* Total detected PSRAM bytes (0 / 8 MB / 16 MB) */
    oyk_u32 usable_bytes;         /* Contiguous usable PSRAM bytes starting at base_addr */
    oyk_u32 base_addr;            /* 0x70000000 when usable_bytes > 0 */
    oyk_u32 test_time_ms;
    TinyTmuxPsramChipInfo chips[TINYTMUX_PSRAM_MAX_CHIPS];
};

enum TinyTmuxMarchCState {
    TMUX_MARCHC_IDLE = 0,
    TMUX_MARCHC_RUNNING,
    TMUX_MARCHC_PASSED,
    TMUX_MARCHC_FAILED,
    TMUX_MARCHC_CANCELLED
};

enum TinyTmuxMarchCChipRes {
    TMUX_MARCHC_CHIP_NONE = 0,
    TMUX_MARCHC_CHIP_PENDING,
    TMUX_MARCHC_CHIP_RUNNING,
    TMUX_MARCHC_CHIP_PASS,
    TMUX_MARCHC_CHIP_FAIL
};

struct TinyTmuxMarchCStatus {
    int state;
    int active_chip;
    int active_pass;
    char pass_label[24];
    oyk_u8 chip_percent[TINYTMUX_PSRAM_MAX_CHIPS];
    int chip_result[TINYTMUX_PSRAM_MAX_CHIPS];
    int fail_chip;
    int fail_pass;
    oyk_u32 fail_addr;
    oyk_u32 fail_expected;
    oyk_u32 fail_actual;
    oyk_u32 start_ms;
    oyk_u32 elapsed_ms;
};

/*
 * Fast multi-pattern verification (~20-50 ms per 8 MB chip at 600 MHz):
 *   1. Data bus walking-1 and walking-0 across all 32 data bits
 *   2. Power-of-two address-line alias test
 *   3. Sampled multi-page sweep (up to 256 evenly distributed pages) with
 *      0xAA55AA55 / 0x55AA55AA checkerboard and address-uniqueness patterns
 * Restores tested words to 0 on exit. Returns 1 on pass, 0 on failure.
 */
int tinytmux_psram_test_region_words(volatile oyk_u32* base,
                                     size_t word_count,
                                     oyk_u32 logical_base_addr,
                                     oyk_u32* fail_addr_out);

/* Probe FlexSPI2 CS0 & CS1, run startup test, and return cached status. */
const TinyTmuxPsramInfo* tinytmux_psram_init_and_test();

/* Re-run the self-test live on already-probed PSRAM chips (for operator diagnostics). */
const TinyTmuxPsramInfo* tinytmux_psram_retest();

/* Return current cached PSRAM status (always non-NULL). */
const TinyTmuxPsramInfo* tinytmux_psram_info();

/* Short header badge string: "16M", "8M", "NO", or "ERR". */
void tinytmux_psram_format_short(const TinyTmuxPsramInfo* info, char* out, size_t out_sz);

/* Descriptive Teensy 4.1 PCB footprint label for chip_idx (0=CS0 small pads, 1=CS1 large pads). */
const char* tinytmux_psram_footprint_label(int chip_idx);

/* Cooperative chunked full 6-element / 10-op March C- PSRAM test (manual trigger). */
int tinytmux_psram_marchc_start(oyk_u32 now_ms);
int tinytmux_psram_marchc_step(oyk_u32 now_ms, size_t max_words_per_step);
void tinytmux_psram_marchc_cancel();
const TinyTmuxMarchCStatus* tinytmux_psram_marchc_status();

/* Host/unit-test simulation hooks (no-op on IMXRT1060 hardware). */
void tinytmux_psram_set_sim_chips(oyk_u32* cs0_buf, size_t cs0_words, oyk_u8 cs0_mfg, oyk_u8 cs0_kgd,
                                  oyk_u32* cs1_buf, size_t cs1_words, oyk_u8 cs1_mfg, oyk_u8 cs1_kgd);
void tinytmux_psram_set_sim_fault_mask(int chip_idx, oyk_u32 stuck_or_mask);
void tinytmux_psram_set_sim_flash(int chip_idx, int kind, oyk_u8 mfg, oyk_u8 type, oyk_u8 cap, oyk_u32 size_bytes);

#endif /* OYK_TINYTMUX_PSRAM_HPP */
