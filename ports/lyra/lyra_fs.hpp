/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_LYRA_FS_HPP
#define OYK_LYRA_FS_HPP

#include "oyk_lyra_config.h"
#include "oyk_portable.hpp"

enum LyraStorageBackend {
    LYRA_STORE_NONE = 0,
    LYRA_STORE_SD_FATFS,
    LYRA_STORE_NAND_LITTLEFS
};

struct LyraFsStatus {
    LyraStorageBackend active_backend;
    int sd_present;
    int nand_ready;
    oyk_u32 total_kb;
    oyk_u32 used_kb;
    char root_path[32];
    char medium_label[40];
};

int lyra_fs_init(int prefer_sd, int sim_sd_present, int sim_nand_present);
void lyra_fs_set_sim_media(int sd_present, int nand_present);
const LyraFsStatus* lyra_fs_status();
int lyra_fs_format_storage(const char* confirm_word);
int lyra_fs_exists(const char* path);
int lyra_fs_write_text_file(const char* path, const char* text);
int lyra_fs_read_text_file(const char* path, char* out, size_t out_sz);

#endif /* OYK_LYRA_FS_HPP */
