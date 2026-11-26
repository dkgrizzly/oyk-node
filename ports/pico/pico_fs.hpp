/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_PICO_FS_HPP
#define OYK_PICO_FS_HPP

#include "oyk_pico_config.h"
#include "oyk_portable.hpp"

enum PicoFsMedium {
    PICO_FS_NONE = 0,
    PICO_FS_RAM_SIM,
    PICO_FS_FLASH_KV,
    PICO_FS_SD_SPI
};

struct PicoFsStatus {
    PicoFsMedium active_medium;
    int sd_present;
    int flash_ready;
    oyk_u32 total_kb;
    oyk_u32 used_kb;
    oyk_u32 flash_partition_offset;
    oyk_u32 flash_partition_size;
    char medium_label[32];
};

int pico_fs_init(int prefer_sd, int sim_sd_present);
void pico_fs_set_sim_sd_present(int present);
const PicoFsStatus* pico_fs_status();
int pico_fs_format_storage(const char* confirm_word);
int pico_fs_exists(const char* path);
int pico_fs_write_text_file(const char* path, const char* text);
int pico_fs_read_text_file(const char* path, char* out, size_t out_sz);

#endif /* OYK_PICO_FS_HPP */
