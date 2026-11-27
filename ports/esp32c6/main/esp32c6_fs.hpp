/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_ESP32C6_FS_HPP
#define OYK_ESP32C6_FS_HPP

#include "oyk_esp32c6_config.h"
#include "oyk_portable.hpp"

enum Esp32C6FsMedium {
    C6L_FS_NONE = 0,
    C6L_FS_RAM_SIM,
    C6L_FS_FLASH_LITTLEFS
};

struct Esp32C6FsStatus {
    Esp32C6FsMedium active_medium;
    int flash_ready;
    oyk_u32 total_kb;
    oyk_u32 used_kb;
    oyk_u32 flash_partition_offset;
    oyk_u32 flash_partition_size;
    char medium_label[32];
};

int esp32c6_fs_init();
const Esp32C6FsStatus* esp32c6_fs_status();
int esp32c6_fs_format_storage(const char* confirm_word);
int esp32c6_fs_exists(const char* path);
int esp32c6_fs_write_text_file(const char* path, const char* text);
int esp32c6_fs_read_text_file(const char* path, char* out, size_t out_sz);

#endif /* OYK_ESP32C6_FS_HPP */
