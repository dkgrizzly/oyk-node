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

#include "../compat/oyk_portable.hpp"

#define TINYTMUX_PSRAM_BASE       0x70000000UL
#define TINYTMUX_PSRAM_CHIP_BYTES (8UL * 1024UL * 1024UL) /* 8 MB per chip */
#define TINYTMUX_PSRAM_MAX_CHIPS  2

#define IMXRT_FLEXSPI2_BASE       0x402A4000UL

struct TinyTmuxPsramChipInfo {
    int detected;       /* 1 if JEDEC Read ID (0x9F) returned valid KGD (0x5D) */
    int passed;         /* 1 if startup memory self-test passed */
    oyk_u8 mfg_id;      /* e.g. 0x0D (APMemory / Lyontek / IPS) */
    oyk_u8 kgd;         /* 0x5D = Known Good Die */
    oyk_u32 base_addr;  /* 0x70000000 (CS0) or 0x70800000 (CS1) */
    oyk_u32 size_bytes; /* 8388608 (8 MB) when detected */
    oyk_u32 fail_addr;  /* First failing logical address if passed == 0 */
};

struct TinyTmuxPsramInfo {
    int chip_count;         /* 0, 1, or 2 detected chips */
    int usable_chip_count;  /* 0, 1, or 2 contiguous chips passing self-test */
    oyk_u32 detected_bytes; /* Total detected bytes (0 / 8 MB / 16 MB) */
    oyk_u32 usable_bytes;   /* Contiguous usable bytes starting at base_addr */
    oyk_u32 base_addr;      /* 0x70000000 when usable_bytes > 0 */
    oyk_u32 test_time_ms;
    TinyTmuxPsramChipInfo chips[TINYTMUX_PSRAM_MAX_CHIPS];
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

/* Host/unit-test simulation hooks (no-op on IMXRT1060 hardware). */
void tinytmux_psram_set_sim_chips(oyk_u32* cs0_buf, size_t cs0_words, oyk_u8 cs0_mfg, oyk_u8 cs0_kgd,
                                  oyk_u32* cs1_buf, size_t cs1_words, oyk_u8 cs1_mfg, oyk_u8 cs1_kgd);
void tinytmux_psram_set_sim_fault_mask(int chip_idx, oyk_u32 stuck_or_mask);

#endif /* OYK_TINYTMUX_PSRAM_HPP */
