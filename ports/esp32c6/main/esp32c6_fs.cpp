/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "esp32c6_fs.hpp"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define C6L_FS_MAX_FILES 20
#define C6L_FS_MAX_BYTES 2048U
#define C6L_FD_OFFSET    32

struct C6lMemFile {
    int used;
    int is_dir;
    char path[96];
    oyk_u8 data[C6L_FS_MAX_BYTES];
    size_t size;
};

struct C6lOpenSlot {
    int open;
    int file_slot;
    size_t pos;
    int writable;
    int append;
};

static Esp32C6FsStatus s_fs_status;
static C6lMemFile      s_files[C6L_FS_MAX_FILES];
static C6lOpenSlot     s_fds[8];

static void norm_path(const char* raw, char* out, size_t out_sz) {
    if (!raw || !out || out_sz == 0) return;
    while (*raw == '/' || *raw == '\\') ++raw;
    size_t j = 0;
    for (size_t i = 0; raw[i] && j + 1 < out_sz; ++i) {
        out[j++] = (raw[i] == '\\') ? '/' : raw[i];
    }
    while (j > 0 && out[j - 1] == '/') --j;
    out[j] = '\0';
}

static int find_slot(const char* path, int want_dir) {
    char clean[96];
    norm_path(path, clean, sizeof(clean));
    if (!clean[0]) return -1;
    for (int i = 0; i < C6L_FS_MAX_FILES; ++i) {
        if (!s_files[i].used) continue;
        if (want_dir >= 0 && s_files[i].is_dir != want_dir) continue;
        if (strcmp(s_files[i].path, clean) == 0) return i;
    }
    return -1;
}

static int alloc_slot(const char* path, int is_dir) {
    char clean[96];
    norm_path(path, clean, sizeof(clean));
    if (!clean[0]) return -1;
    int existing = find_slot(clean, -1);
    if (existing >= 0) return existing;
    for (int i = 0; i < C6L_FS_MAX_FILES; ++i) {
        if (!s_files[i].used) {
            memset(&s_files[i], 0, sizeof(s_files[i]));
            s_files[i].used = 1;
            s_files[i].is_dir = is_dir ? 1 : 0;
            strncpy(s_files[i].path, clean, sizeof(s_files[i].path) - 1);
            return i;
        }
    }
    return -1;
}

static void refresh_fs_stats() {
    size_t used_b = 0;
    for (int i = 0; i < C6L_FS_MAX_FILES; ++i) {
        if (s_files[i].used) used_b += s_files[i].size + 128U;
    }
    s_fs_status.used_kb = static_cast<oyk_u32>((used_b + 1023U) / 1024U);
}

int esp32c6_fs_init() {
    memset(&s_fs_status, 0, sizeof(s_fs_status));
    memset(s_files, 0, sizeof(s_files));
    memset(s_fds, 0, sizeof(s_fds));
    s_fs_status.flash_ready = 1;
    s_fs_status.flash_partition_offset = 0x310000UL;
    s_fs_status.flash_partition_size = 0xCF0000UL;
    s_fs_status.total_kb = static_cast<oyk_u32>(s_fs_status.flash_partition_size / 1024UL);
#ifdef ESP_PLATFORM
    s_fs_status.active_medium = C6L_FS_FLASH_LITTLEFS;
    strcpy(s_fs_status.medium_label, "16MB Flash (LittleFS)");
#else
    s_fs_status.active_medium = C6L_FS_RAM_SIM;
    strcpy(s_fs_status.medium_label, "16MB Flash LittleFS (Sim)");
#endif
    refresh_fs_stats();
    return 1;
}

const Esp32C6FsStatus* esp32c6_fs_status() { return &s_fs_status; }

int esp32c6_fs_format_storage(const char* confirm_word) {
    if (!confirm_word || strcmp(confirm_word, "FORMAT") != 0) return 0;
    memset(s_files, 0, sizeof(s_files));
    memset(s_fds, 0, sizeof(s_fds));
    refresh_fs_stats();
    return 1;
}

int esp32c6_fs_exists(const char* path) {
    return (find_slot(path, -1) >= 0) ? 1 : 0;
}

int esp32c6_fs_write_text_file(const char* path, const char* text) {
    if (!path || !text) return 0;
    int idx = alloc_slot(path, 0);
    if (idx < 0) return 0;
    size_t len = strlen(text);
    if (len > C6L_FS_MAX_BYTES) len = C6L_FS_MAX_BYTES;
    memcpy(s_files[idx].data, text, len);
    s_files[idx].size = len;
    refresh_fs_stats();
    return 1;
}

int esp32c6_fs_read_text_file(const char* path, char* out, size_t out_sz) {
    if (!path || !out || out_sz == 0) return 0;
    out[0] = '\0';
    int idx = find_slot(path, 0);
    if (idx < 0) return 0;
    size_t cp = s_files[idx].size;
    if (cp >= out_sz) cp = out_sz - 1;
    memcpy(out, s_files[idx].data, cp);
    out[cp] = '\0';
    return 1;
}

#ifndef ESP_PLATFORM
/* Portable filesystem hooks linked by oyk_portable.cpp in C6L host unit tests */
static int find_free_fd() {
    for (int i = 0; i < 8; ++i) {
        if (!s_fds[i].open) return i;
    }
    return -1;
}

extern "C" int oyk_fs_mkdir(const char* path) {
    return (alloc_slot(path, 1) >= 0) ? 0 : -1;
}
extern "C" int oyk_fs_unlink(const char* path) {
    int idx = find_slot(path, -1);
    if (idx < 0) return -1;
    memset(&s_files[idx], 0, sizeof(s_files[idx]));
    refresh_fs_stats();
    return 0;
}
extern "C" int oyk_fs_open(const char* path, int oflag) {
    int wr = ((oflag & O_WRONLY) || (oflag & O_RDWR)) ? 1 : 0;
    int idx = find_slot(path, 0);
    if (idx < 0) {
        if (!(oflag & O_CREAT)) return -1;
        idx = alloc_slot(path, 0);
        if (idx < 0) return -1;
    } else if ((oflag & O_TRUNC) && wr) {
        s_files[idx].size = 0;
    }
    int fd = find_free_fd();
    if (fd < 0) return -1;
    s_fds[fd].open = 1;
    s_fds[fd].file_slot = idx;
    s_fds[fd].writable = wr;
    s_fds[fd].append = (oflag & O_APPEND) ? 1 : 0;
    s_fds[fd].pos = s_fds[fd].append ? s_files[idx].size : 0;
    return fd + C6L_FD_OFFSET;
}
extern "C" int oyk_fs_read(int fd, void* buf, unsigned int len) {
    int slot = fd - C6L_FD_OFFSET;
    if (slot < 0 || slot >= 8 || !s_fds[slot].open) return -1;
    C6lMemFile* mf = &s_files[s_fds[slot].file_slot];
    if (s_fds[slot].pos >= mf->size) return 0;
    size_t avail = mf->size - s_fds[slot].pos;
    if (avail > len) avail = len;
    memcpy(buf, mf->data + s_fds[slot].pos, avail);
    s_fds[slot].pos += avail;
    return static_cast<int>(avail);
}
extern "C" int oyk_fs_write(int fd, const void* buf, unsigned int len) {
    int slot = fd - C6L_FD_OFFSET;
    if (slot < 0 || slot >= 8 || !s_fds[slot].open || !s_fds[slot].writable) return -1;
    C6lMemFile* mf = &s_files[s_fds[slot].file_slot];
    if (s_fds[slot].append) s_fds[slot].pos = mf->size;
    size_t avail = (s_fds[slot].pos < C6L_FS_MAX_BYTES) ? (C6L_FS_MAX_BYTES - s_fds[slot].pos) : 0;
    size_t n = (len < avail) ? len : avail;
    if (n > 0) {
        memcpy(mf->data + s_fds[slot].pos, buf, n);
        s_fds[slot].pos += n;
        if (s_fds[slot].pos > mf->size) mf->size = s_fds[slot].pos;
        refresh_fs_stats();
    }
    return static_cast<int>(n);
}
extern "C" int oyk_fs_close(int fd) {
    int slot = fd - C6L_FD_OFFSET;
    if (slot < 0 || slot >= 8 || !s_fds[slot].open) return -1;
    memset(&s_fds[slot], 0, sizeof(s_fds[slot]));
    return 0;
}
#endif
