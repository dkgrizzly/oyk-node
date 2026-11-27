/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "lyra_fs.hpp"
#include "board_lyra.hpp"

#include <stdio.h>
#include <string.h>

#define LYRA_FS_MAX_FILES 24
#define LYRA_FS_MAX_BYTES 2048U

struct LyraMemFile {
    int used;
    int is_dir;
    char path[96];
    oyk_u8 data[LYRA_FS_MAX_BYTES];
    size_t size;
};

static LyraFsStatus s_lyra_fs = {};
static LyraMemFile s_lyra_files[LYRA_FS_MAX_FILES];
static int s_sim_sd_ok = 0;
static int s_sim_nand_ok = 0;

static void lyra_norm_path(const char* raw, char* out, size_t out_sz) {
    if (!raw || !out || out_sz == 0) return;
    while (*raw == '/' || *raw == '\\') ++raw;
    size_t j = 0;
    for (size_t i = 0; raw[i] && j + 1 < out_sz; ++i) {
        out[j++] = (raw[i] == '\\') ? '/' : raw[i];
    }
    while (j > 0 && out[j - 1] == '/') --j;
    out[j] = '\0';
}

static int lyra_find_slot(const char* path, int want_dir) {
    char clean[96];
    lyra_norm_path(path, clean, sizeof(clean));
    if (!clean[0]) return -1;
    for (int i = 0; i < LYRA_FS_MAX_FILES; ++i) {
        if (!s_lyra_files[i].used) continue;
        if (want_dir >= 0 && s_lyra_files[i].is_dir != want_dir) continue;
        if (strcmp(s_lyra_files[i].path, clean) == 0) return i;
    }
    return -1;
}

static int lyra_alloc_slot(const char* path, int is_dir) {
    char clean[96];
    lyra_norm_path(path, clean, sizeof(clean));
    if (!clean[0]) return -1;
    int existing = lyra_find_slot(clean, -1);
    if (existing >= 0) return existing;
    for (int i = 0; i < LYRA_FS_MAX_FILES; ++i) {
        if (!s_lyra_files[i].used) {
            memset(&s_lyra_files[i], 0, sizeof(s_lyra_files[i]));
            s_lyra_files[i].used = 1;
            s_lyra_files[i].is_dir = is_dir ? 1 : 0;
            strncpy(s_lyra_files[i].path, clean, sizeof(s_lyra_files[i].path) - 1);
            return i;
        }
    }
    return -1;
}

#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD

#include <dfs_posix.h>
#include <dfs_fs.h>

static int lyra_dfs_mount_sd() {
    if (dfs_mount("sd0", OYK_LYRA_SD_MOUNT, "elm", 0, 0) == 0) return 1;
    struct stat st;
    return (stat(OYK_LYRA_SD_MOUNT, &st) == 0) ? 1 : 0;
}

static int lyra_dfs_mount_nand(oyk_u16 nand_mb) {
    if (nand_mb == 0U) return 0;
    if (dfs_mount("spi_nand0", OYK_LYRA_NAND_MOUNT, "lfs", 0, 0) == 0) return 1;
    struct stat st;
    return (stat(OYK_LYRA_NAND_MOUNT, &st) == 0) ? 1 : 0;
}

static void lyra_dfs_build_abs(const char* rel, char* abs_out, size_t sz) {
    char clean[96];
    lyra_norm_path(rel, clean, sizeof(clean));
    snprintf(abs_out, sz, "%s/%s",
             s_lyra_fs.root_path[0] ? s_lyra_fs.root_path : OYK_LYRA_SD_MOUNT,
             clean);
}

#else

static int lyra_dfs_mount_sd() { return s_sim_sd_ok ? 1 : 0; }
static int lyra_dfs_mount_nand(oyk_u16 nand_mb) { return (nand_mb > 0U && s_sim_nand_ok) ? 1 : 0; }

#endif

void lyra_fs_set_sim_media(int sd_present, int nand_present) {
    s_sim_sd_ok = sd_present ? 1 : 0;
    s_sim_nand_ok = nand_present ? 1 : 0;
}

int lyra_fs_init(int prefer_sd, int sim_sd_present, int sim_nand_present) {
    const LyraBoardProfile* prof = lyra_board_current_profile();
    s_sim_sd_ok = sim_sd_present ? 1 : 0;
    s_sim_nand_ok = sim_nand_present ? 1 : 0;
    memset(&s_lyra_fs, 0, sizeof(s_lyra_fs));
    memset(s_lyra_files, 0, sizeof(s_lyra_files));

    int sd_ok = prefer_sd ? lyra_dfs_mount_sd() : 0;
    int nand_ok = lyra_dfs_mount_nand(prof->nand_mb);
    s_lyra_fs.sd_present = sd_ok ? 1 : 0;
    s_lyra_fs.nand_ready = nand_ok ? 1 : 0;

    if (sd_ok) {
        s_lyra_fs.active_backend = LYRA_STORE_SD_FATFS;
        s_lyra_fs.total_kb = 1024UL * 32UL;
        strcpy(s_lyra_fs.root_path, OYK_LYRA_SD_MOUNT);
        strcpy(s_lyra_fs.medium_label, "SD (DFS ELM FatFS /sd)");
    } else if (nand_ok) {
        s_lyra_fs.active_backend = LYRA_STORE_NAND_LITTLEFS;
        s_lyra_fs.total_kb = static_cast<oyk_u32>(prof->nand_mb) * 1024UL;
        strcpy(s_lyra_fs.root_path, OYK_LYRA_NAND_MOUNT);
        strcpy(s_lyra_fs.medium_label, "SPI NAND (DFS LittleFS /flash)");
    } else {
        s_lyra_fs.active_backend = LYRA_STORE_NONE;
        s_lyra_fs.total_kb = 0UL;
        strcpy(s_lyra_fs.root_path, "");
        strcpy(s_lyra_fs.medium_label, "NONE (Mount Unavailable)");
        return 0;
    }

    lyra_alloc_slot("mail", 1);
    lyra_alloc_slot("files", 1);
    lyra_alloc_slot("logs", 1);
    return 1;
}

const LyraFsStatus* lyra_fs_status() {
    oyk_u32 used = 0;
    for (int i = 0; i < LYRA_FS_MAX_FILES; ++i) {
        if (s_lyra_files[i].used && !s_lyra_files[i].is_dir) {
            used += static_cast<oyk_u32>(s_lyra_files[i].size);
        }
    }
    s_lyra_fs.used_kb = (used + 1023UL) / 1024UL;
    return &s_lyra_fs;
}

int lyra_fs_format_storage(const char* confirm_word) {
    if (!confirm_word || strcmp(confirm_word, "FORMAT") != 0) return 0;
    if (s_lyra_fs.active_backend == LYRA_STORE_NONE) return 0;
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
    if (s_lyra_fs.active_backend == LYRA_STORE_SD_FATFS) {
        dfs_mkfs("elm", "sd0");
        dfs_mount("sd0", OYK_LYRA_SD_MOUNT, "elm", 0, 0);
    } else if (s_lyra_fs.active_backend == LYRA_STORE_NAND_LITTLEFS) {
        dfs_mkfs("lfs", "spi_nand0");
        dfs_mount("spi_nand0", OYK_LYRA_NAND_MOUNT, "lfs", 0, 0);
    }
#endif
    memset(s_lyra_files, 0, sizeof(s_lyra_files));
    lyra_alloc_slot("mail", 1);
    lyra_alloc_slot("files", 1);
    lyra_alloc_slot("logs", 1);
    return 1;
}

int lyra_fs_exists(const char* path) {
    if (s_lyra_fs.active_backend == LYRA_STORE_NONE) return 0;
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
    char abs_p[140];
    lyra_dfs_build_abs(path, abs_p, sizeof(abs_p));
    struct stat st;
    if (stat(abs_p, &st) == 0) return 1;
#endif
    return (lyra_find_slot(path, -1) >= 0) ? 1 : 0;
}

int lyra_fs_write_text_file(const char* path, const char* text) {
    if (s_lyra_fs.active_backend == LYRA_STORE_NONE) return 0;
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
    char abs_p[140];
    lyra_dfs_build_abs(path, abs_p, sizeof(abs_p));
    int fd = open(abs_p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        size_t len = text ? strlen(text) : 0U;
        if (len > 0U) write(fd, text, len);
        close(fd);
    }
#endif
    int slot = lyra_alloc_slot(path, 0);
    if (slot < 0) return 0;
    size_t n = text ? strlen(text) : 0U;
    if (n > LYRA_FS_MAX_BYTES) n = LYRA_FS_MAX_BYTES;
    if (n > 0) memcpy(s_lyra_files[slot].data, text, n);
    s_lyra_files[slot].size = n;
    return 1;
}

int lyra_fs_read_text_file(const char* path, char* out, size_t out_sz) {
    if (!out || out_sz == 0 || s_lyra_fs.active_backend == LYRA_STORE_NONE) return 0;
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
    char abs_p[140];
    lyra_dfs_build_abs(path, abs_p, sizeof(abs_p));
    int fd = open(abs_p, O_RDONLY, 0);
    if (fd >= 0) {
        int r = read(fd, out, out_sz - 1U);
        close(fd);
        if (r >= 0) {
            out[r] = '\0';
            return 1;
        }
    }
#endif
    int slot = lyra_find_slot(path, 0);
    if (slot < 0) {
        out[0] = '\0';
        return 0;
    }
    size_t n = s_lyra_files[slot].size;
    if (n >= out_sz) n = out_sz - 1U;
    if (n > 0) memcpy(out, s_lyra_files[slot].data, n);
    out[n] = '\0';
    return 1;
}
