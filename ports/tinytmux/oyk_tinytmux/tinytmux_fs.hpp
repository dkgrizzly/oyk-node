/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_TINYTMUX_FS_HPP
#define OYK_TINYTMUX_FS_HPP

/*
 * 3-Tier Filesystem Mount, Newlib Syscall Retargeting, and Directory Hooks
 * for Teensy 4.1 / IMXRT1062 (TinyTMUX):
 *   1. microSD Card (SDIO, BUILTIN_SDCARD)
 *   2. External QSPI Flash on CS1 / Pin 51 (LittleFS_QSPI)
 *   3. Internal 4 MB Program Flash (LittleFS_Program)
 */

#include "oyk_tinytmux_config.h"
#include "oyk_portable.hpp"

#define TINYTMUX_INTERNAL_FS_BYTES (4UL * 1024UL * 1024UL)

enum TinyTmuxStorageTier {
    TMUX_STORAGE_NONE = 0,
    TMUX_STORAGE_SD,
    TMUX_STORAGE_QSPI,
    TMUX_STORAGE_INTERNAL
};

struct TinyTmuxStorageTierInfo {
    int tier;
    int detected;
    int mounted;
    oyk_u32 total_kb;
    oyk_u32 used_kb;
    char label[32];
};

#ifdef __cplusplus
extern "C" {
#endif

int tinytmux_fs_mount();
void tinytmux_fs_unmount();
int tinytmux_fs_is_mounted();
int tinytmux_fs_active_tier();
int tinytmux_fs_is_flash_storage();
const char* tinytmux_fs_status_label();
int tinytmux_fs_tier_info(int tier, TinyTmuxStorageTierInfo* out);

/* Host/unit-test simulation hook (no-op on Teensy hardware). */
void tinytmux_fs_set_sim_tiers(int sd_present, int qspi_present, int internal_present);
int tinytmux_fs_sim_format_count(int tier);

/*
 * Destructively formats (partitions + creates a fresh filesystem on) the selected storage tier,
 * then remounts using the standard 3-tier priority. Caller MUST obtain operator confirmation first.
 *   microSD: FAT32 (<=32 GB) or exFAT (>32 GB); works on blank/unformatted cards.
 *   QSPI Flash / Internal Flash: LittleFS quick format.
 * Returns 1 on success, 0 on failure (msg_out receives a human-readable status).
 */
int tinytmux_fs_format_tier(int tier, char* msg_out, size_t msg_sz);

/* Unmounts and re-runs the 3-tier mount (e.g., after inserting a new microSD card). */
int tinytmux_fs_rescan();

/* Platform directory & file hooks consumed by ports/compat/oyk_portable.cpp */
int oyk_imxrt_mkdir(const char* path);
int oyk_imxrt_is_regular_file(const char* path);
int oyk_imxrt_is_directory(const char* path);
void* oyk_imxrt_opendir(const char* dir_path);
int oyk_imxrt_readdir(void* dir_handle, char* name_out, size_t name_sz,
                      int* is_dir_out, oyk_u32* size_out);
void oyk_imxrt_closedir(void* dir_handle);

#ifdef __cplusplus
}
#endif

#endif /* OYK_TINYTMUX_FS_HPP */
