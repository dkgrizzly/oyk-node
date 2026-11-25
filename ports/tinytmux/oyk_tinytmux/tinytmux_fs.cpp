/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "tinytmux_fs.hpp"
#include "board_tinytmux.hpp"
#include "tinytmux_psram.hpp"

#include <stdio.h>
#include <string.h>

static int s_fs_mounted = 0;
static int s_active_tier = TMUX_STORAGE_NONE;
static char s_fs_label[24] = "UNMOUNTED";

static void fs_str_copy(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

#if defined(TEENSYDUINO)

#include <Arduino.h>
#include <LittleFS.h>
#include <SD.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>

#define OYK_MAX_OPEN_FILES 16
#define OYK_MAX_OPEN_DIRS   4

static LittleFS_QSPI s_qspi_fs;
static LittleFS_Program s_prog_fs;
static FS* s_active_fs = NULL;
static int s_qspi_begun = 0;
static int s_prog_begun = 0;

static File s_files[OYK_MAX_OPEN_FILES];
static oyk_u8 s_file_open[OYK_MAX_OPEN_FILES];

struct OykDirHandle {
    int in_use;
    File dir;
};
static OykDirHandle s_dirs[OYK_MAX_OPEN_DIRS];

static void normalize_fs_path(const char* in, char* out, size_t out_cap) {
    if (!out || out_cap == 0) return;
    out[0] = '\0';
    if (!in || !in[0]) {
        fs_str_copy(out, out_cap, "/");
        return;
    }
    const char* p = in;
    if (p[0] == '0' && p[1] == ':') p += 2;
    size_t pos = 0;
    if (p[0] != '/') {
        if (pos + 1 < out_cap) out[pos++] = '/';
    }
    while (*p && pos + 1 < out_cap) {
        char c = (*p == '\\') ? '/' : *p;
        if (c == '/' && pos > 0 && out[pos - 1] == '/') {
            ++p;
            continue;
        }
        out[pos++] = c;
        ++p;
    }
    while (pos > 1 && out[pos - 1] == '/') {
        --pos;
    }
    out[pos] = '\0';
    if (out[0] == '\0') fs_str_copy(out, out_cap, "/");
}

static const char* basename_ptr(const char* p) {
    if (!p) return "";
    const char* slash = strrchr(p, '/');
    return slash ? (slash + 1) : p;
}

static void close_all_handles() {
    for (int i = 0; i < OYK_MAX_OPEN_FILES; ++i) {
        if (s_file_open[i]) {
            s_files[i].close();
            s_file_open[i] = 0;
        }
    }
    for (int i = 0; i < OYK_MAX_OPEN_DIRS; ++i) {
        if (s_dirs[i].in_use) {
            s_dirs[i].dir.close();
            s_dirs[i].in_use = 0;
        }
    }
}

extern "C" {

OYK_FLASHMEM int tinytmux_fs_mount() {
    if (s_fs_mounted && s_active_fs) return 1;
    memset(s_file_open, 0, sizeof(s_file_open));
    for (int i = 0; i < OYK_MAX_OPEN_DIRS; ++i) s_dirs[i].in_use = 0;

    if (SD.begin(BUILTIN_SDCARD)) {
        s_active_fs = &SD;
        s_active_tier = TMUX_STORAGE_SD;
        s_fs_mounted = 1;
        fs_str_copy(s_fs_label, sizeof(s_fs_label), "SDIO MOUNTED");
        return 1;
    }

    if (s_qspi_fs.begin()) {
        s_qspi_begun = 1;
        s_active_fs = &s_qspi_fs;
        s_active_tier = TMUX_STORAGE_QSPI;
        s_fs_mounted = 1;
        fs_str_copy(s_fs_label, sizeof(s_fs_label), "QSPI FLASH");
        return 1;
    }

    if (s_prog_fs.begin(TINYTMUX_INTERNAL_FS_BYTES)) {
        s_prog_begun = 1;
        s_active_fs = &s_prog_fs;
        s_active_tier = TMUX_STORAGE_INTERNAL;
        s_fs_mounted = 1;
        fs_str_copy(s_fs_label, sizeof(s_fs_label), "INT FLASH 4M");
        return 1;
    }

    s_active_fs = NULL;
    s_active_tier = TMUX_STORAGE_NONE;
    s_fs_mounted = 0;
    fs_str_copy(s_fs_label, sizeof(s_fs_label), "NO STORAGE");
    return 0;
}

void tinytmux_fs_unmount() {
    close_all_handles();
    s_active_fs = NULL;
    s_active_tier = TMUX_STORAGE_NONE;
    s_fs_mounted = 0;
    fs_str_copy(s_fs_label, sizeof(s_fs_label), "UNMOUNTED");
}

int tinytmux_fs_is_mounted() {
    return s_fs_mounted;
}

int tinytmux_fs_active_tier() {
    return s_active_tier;
}

int tinytmux_fs_is_flash_storage() {
    return (s_fs_mounted &&
            (s_active_tier == TMUX_STORAGE_QSPI || s_active_tier == TMUX_STORAGE_INTERNAL))
           ? 1 : 0;
}

const char* tinytmux_fs_status_label() {
    return s_fs_label;
}

OYK_FLASHMEM int tinytmux_fs_tier_info(int tier, TinyTmuxStorageTierInfo* out) {
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    out->tier = tier;
    if (tier == TMUX_STORAGE_SD) {
        out->mounted = (s_fs_mounted && s_active_tier == TMUX_STORAGE_SD) ? 1 : 0;
        out->detected = out->mounted;
        fs_str_copy(out->label, sizeof(out->label), "microSD Card (SDIO)");
        if (out->mounted) {
            out->total_kb = static_cast<oyk_u32>(SD.totalSize() / 1024ULL);
            out->used_kb = static_cast<oyk_u32>(SD.usedSize() / 1024ULL);
        }
        return 1;
    }
    if (tier == TMUX_STORAGE_QSPI) {
        const TinyTmuxPsramInfo* pinfo = tinytmux_psram_info();
        out->mounted = (s_fs_mounted && s_active_tier == TMUX_STORAGE_QSPI) ? 1 : 0;
        out->detected = (out->mounted || s_qspi_begun || (pinfo && pinfo->has_valid_qspi_flash_cs1)) ? 1 : 0;
        fs_str_copy(out->label, sizeof(out->label), "QSPI Flash (CS1 / Pin 51)");
        if (out->mounted || s_qspi_begun) {
            out->total_kb = static_cast<oyk_u32>(s_qspi_fs.totalSize() / 1024ULL);
            out->used_kb = static_cast<oyk_u32>(s_qspi_fs.usedSize() / 1024ULL);
        } else if (pinfo && pinfo->has_valid_qspi_flash_cs1) {
            out->total_kb = static_cast<oyk_u32>(pinfo->chips[1].size_bytes / 1024UL);
        }
        return 1;
    }
    if (tier == TMUX_STORAGE_INTERNAL) {
        out->detected = 1;
        out->mounted = (s_fs_mounted && s_active_tier == TMUX_STORAGE_INTERNAL) ? 1 : 0;
        fs_str_copy(out->label, sizeof(out->label), "Internal Flash (4 MB)");
        if (out->mounted || s_prog_begun) {
            out->total_kb = static_cast<oyk_u32>(s_prog_fs.totalSize() / 1024ULL);
            out->used_kb = static_cast<oyk_u32>(s_prog_fs.usedSize() / 1024ULL);
        } else {
            out->total_kb = static_cast<oyk_u32>(TINYTMUX_INTERNAL_FS_BYTES / 1024UL);
        }
        return 1;
    }
    return 0;
}

void tinytmux_fs_set_sim_tiers(int /*sd_present*/, int /*qspi_present*/, int /*internal_present*/) {}
int tinytmux_fs_sim_format_count(int /*tier*/) { return 0; }

static void fs_make_default_dirs() {
    if (!s_active_fs) return;
    const char* dirs[4] = { "/logs", "/mail", "/files", "/incoming" };
    for (int i = 0; i < 4; ++i) {
        if (!s_active_fs->exists(dirs[i])) s_active_fs->mkdir(dirs[i]);
    }
}

OYK_FLASHMEM int tinytmux_fs_rescan() {
    tinytmux_fs_unmount();
    return tinytmux_fs_mount();
}

OYK_FLASHMEM int tinytmux_fs_format_tier(int tier, char* msg_out, size_t msg_sz) {
    char tmp[96];
    tmp[0] = '\0';
    int ok = 0;
    close_all_handles();

    if (tier == TMUX_STORAGE_SD) {
        s_active_fs = NULL;
        s_fs_mounted = 0;
        if (!SD.sdfs.cardBegin(SdioConfig(FIFO_SDIO))) {
            fs_str_copy(tmp, sizeof(tmp), "No microSD card detected.");
        } else if (!SD.sdfs.format(NULL)) {
            fs_str_copy(tmp, sizeof(tmp), "microSD format FAILED.");
        } else {
            ok = 1;
            fs_str_copy(tmp, sizeof(tmp), "microSD formatted (FAT32/exFAT by capacity).");
        }
    } else if (tier == TMUX_STORAGE_QSPI) {
        s_active_fs = NULL;
        s_fs_mounted = 0;
        if (!s_qspi_begun) s_qspi_begun = s_qspi_fs.begin() ? 1 : 0;
        if (!s_qspi_begun) {
            fs_str_copy(tmp, sizeof(tmp), "No QSPI Flash detected on CS1 (Pin 51 pads).");
        } else if (!s_qspi_fs.format(0, 0, Serial)) {
            fs_str_copy(tmp, sizeof(tmp), "QSPI Flash format FAILED.");
        } else {
            ok = 1;
            fs_str_copy(tmp, sizeof(tmp), "QSPI Flash LittleFS formatted.");
        }
    } else if (tier == TMUX_STORAGE_INTERNAL) {
        s_active_fs = NULL;
        s_fs_mounted = 0;
        if (!s_prog_begun) s_prog_begun = s_prog_fs.begin(TINYTMUX_INTERNAL_FS_BYTES) ? 1 : 0;
        if (!s_prog_begun) {
            fs_str_copy(tmp, sizeof(tmp), "Internal 4 MB flash unavailable.");
        } else if (!s_prog_fs.format(0, 0, Serial)) {
            fs_str_copy(tmp, sizeof(tmp), "Internal Flash format FAILED.");
        } else {
            ok = 1;
            fs_str_copy(tmp, sizeof(tmp), "Internal 4 MB LittleFS formatted.");
        }
    } else {
        fs_str_copy(tmp, sizeof(tmp), "Unknown storage tier.");
    }

    s_fs_mounted = 0;
    tinytmux_fs_mount();
    if (ok) fs_make_default_dirs();
    fs_str_copy(msg_out, msg_sz, tmp);
    return ok;
}

int oyk_imxrt_mkdir(const char* path) {
    if (!s_fs_mounted || !s_active_fs || !path || !path[0]) return -1;
    char norm[192];
    normalize_fs_path(path, norm, sizeof(norm));
    if (strcmp(norm, "/") == 0) return 0;
    if (oyk_imxrt_is_directory(norm)) return 0;
    return s_active_fs->mkdir(norm) ? 0 : -1;
}

int oyk_imxrt_is_regular_file(const char* path) {
    if (!s_fs_mounted || !s_active_fs || !path || !path[0]) return 0;
    char norm[192];
    normalize_fs_path(path, norm, sizeof(norm));
    if (strcmp(norm, "/") == 0) return 0;
    File f = s_active_fs->open(norm, FILE_READ);
    if (!f) return 0;
    int is_reg = f.isDirectory() ? 0 : 1;
    f.close();
    return is_reg;
}

int oyk_imxrt_is_directory(const char* path) {
    if (!s_fs_mounted || !s_active_fs || !path || !path[0]) return 0;
    char norm[192];
    normalize_fs_path(path, norm, sizeof(norm));
    if (strcmp(norm, "/") == 0) return 1;
    File f = s_active_fs->open(norm, FILE_READ);
    if (!f) return 0;
    int is_dir = f.isDirectory() ? 1 : 0;
    f.close();
    return is_dir;
}

void* oyk_imxrt_opendir(const char* dir_path) {
    if (!s_fs_mounted || !s_active_fs || !dir_path || !dir_path[0]) return NULL;
    int slot = -1;
    for (int i = 0; i < OYK_MAX_OPEN_DIRS; ++i) {
        if (!s_dirs[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return NULL;
    char norm[192];
    normalize_fs_path(dir_path, norm, sizeof(norm));
    File d = s_active_fs->open(norm, FILE_READ);
    if (!d || !d.isDirectory()) {
        if (d) d.close();
        return NULL;
    }
    s_dirs[slot].in_use = 1;
    s_dirs[slot].dir = d;
    return &s_dirs[slot];
}

int oyk_imxrt_readdir(void* dir_handle, char* name_out, size_t name_sz,
                      int* is_dir_out, oyk_u32* size_out) {
    if (!dir_handle || !name_out || name_sz == 0) return 0;
    OykDirHandle* dh = static_cast<OykDirHandle*>(dir_handle);
    if (!dh->in_use) return 0;

    while (true) {
        File entry = dh->dir.openNextFile();
        if (!entry) return 0;
        const char* base = basename_ptr(entry.name());
        if (!base || !base[0] || strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
            entry.close();
            continue;
        }
        fs_str_copy(name_out, name_sz, base);
        int is_dir = entry.isDirectory() ? 1 : 0;
        if (is_dir_out) *is_dir_out = is_dir;
        if (size_out) *size_out = is_dir ? 0UL : static_cast<oyk_u32>(entry.size());
        entry.close();
        return 1;
    }
}

void oyk_imxrt_closedir(void* dir_handle) {
    if (!dir_handle) return;
    OykDirHandle* dh = static_cast<OykDirHandle*>(dir_handle);
    if (dh->in_use) {
        dh->dir.close();
        dh->in_use = 0;
    }
}

int _open(const char* path, int flags, int /*mode*/) {
    if (!s_fs_mounted || !s_active_fs || !path || !path[0]) {
        errno = ENOENT;
        return -1;
    }
    int slot = -1;
    for (int i = 0; i < OYK_MAX_OPEN_FILES; ++i) {
        if (!s_file_open[i]) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        errno = EMFILE;
        return -1;
    }

    char norm[192];
    normalize_fs_path(path, norm, sizeof(norm));

    int acc = flags & O_ACCMODE;
    if (acc == O_RDONLY) {
        File f = s_active_fs->open(norm, FILE_READ);
        if (!f || f.isDirectory()) {
            if (f) f.close();
            errno = ENOENT;
            return -1;
        }
        s_files[slot] = f;
        s_file_open[slot] = 1;
        return slot + 3;
    }

    if (!(flags & O_CREAT) && !s_active_fs->exists(norm)) {
        errno = ENOENT;
        return -1;
    }
    if ((flags & O_TRUNC) && s_active_fs->exists(norm)) {
        s_active_fs->remove(norm);
    }
    uint8_t open_mode = (flags & O_APPEND) ? FILE_WRITE : FILE_WRITE_BEGIN;
    File f = s_active_fs->open(norm, open_mode);
    if (!f) {
        errno = EIO;
        return -1;
    }
    s_files[slot] = f;
    s_file_open[slot] = 1;
    return slot + 3;
}

int _close(int fd) {
    if (fd >= 0 && fd <= 2) return 0;
    int slot = fd - 3;
    if (slot < 0 || slot >= OYK_MAX_OPEN_FILES || !s_file_open[slot]) {
        errno = EBADF;
        return -1;
    }
    s_files[slot].close();
    s_file_open[slot] = 0;
    return 0;
}

int _read(int fd, char* ptr, int len) {
    if (!ptr || len < 0) {
        errno = EINVAL;
        return -1;
    }
    if (fd == 0) {
        int got = 0;
        while (got < len && Serial.available() > 0) {
            int ch = Serial.read();
            if (ch < 0) break;
            ptr[got++] = static_cast<char>(ch);
        }
        return got;
    }
    int slot = fd - 3;
    if (slot < 0 || slot >= OYK_MAX_OPEN_FILES || !s_file_open[slot]) {
        errno = EBADF;
        return -1;
    }
    int r = s_files[slot].read(ptr, static_cast<size_t>(len));
    return (r < 0) ? 0 : r;
}

int _write(int fd, char* ptr, int len) {
    if (!ptr || len < 0) {
        errno = EINVAL;
        return -1;
    }
    int slot = fd - 3;
    if (slot >= 0 && slot < OYK_MAX_OPEN_FILES && s_file_open[slot]) {
        size_t bw = s_files[slot].write(reinterpret_cast<const uint8_t*>(ptr),
                                        static_cast<size_t>(len));
        return static_cast<int>(bw);
    }
    if (fd >= 0 && fd <= 2) {
        return static_cast<int>(Serial.write(reinterpret_cast<const uint8_t*>(ptr),
                                             static_cast<size_t>(len)));
    }
    if (static_cast<uintptr_t>(fd) >= 0x10000UL) {
        Print* p = reinterpret_cast<Print*>(static_cast<uintptr_t>(fd));
        return static_cast<int>(p->write(reinterpret_cast<const uint8_t*>(ptr),
                                         static_cast<size_t>(len)));
    }
    errno = EBADF;
    return -1;
}

int _lseek(int fd, long long offset, int whence) {
    if (fd >= 0 && fd <= 2) return 0;
    int slot = fd - 3;
    if (slot < 0 || slot >= OYK_MAX_OPEN_FILES || !s_file_open[slot]) {
        errno = EBADF;
        return -1;
    }
    File& f = s_files[slot];
    long long target = offset;
    if (whence == SEEK_CUR) {
        target = static_cast<long long>(f.position()) + offset;
    } else if (whence == SEEK_END) {
        target = static_cast<long long>(f.size()) + offset;
    }
    if (target < 0) {
        errno = EINVAL;
        return -1;
    }
    if (!f.seek(static_cast<uint64_t>(target))) {
        errno = EIO;
        return -1;
    }
    return static_cast<int>(f.position());
}

int _fstat(int fd, struct stat* st) {
    if (!st) return -1;
    memset(st, 0, sizeof(*st));
    if (fd >= 0 && fd <= 2) {
        st->st_mode = S_IFCHR;
        return 0;
    }
    int slot = fd - 3;
    if (slot >= 0 && slot < OYK_MAX_OPEN_FILES && s_file_open[slot]) {
        st->st_mode = S_IFREG | 0644;
        st->st_size = static_cast<off_t>(s_files[slot].size());
        return 0;
    }
    errno = EBADF;
    return -1;
}

int _stat(const char* path, struct stat* st) {
    if (!st) return -1;
    memset(st, 0, sizeof(*st));
    if (!s_fs_mounted || !s_active_fs || !path || !path[0]) {
        errno = ENOENT;
        return -1;
    }
    char norm[192];
    normalize_fs_path(path, norm, sizeof(norm));
    if (strcmp(norm, "/") == 0) {
        st->st_mode = S_IFDIR | 0755;
        return 0;
    }
    File f = s_active_fs->open(norm, FILE_READ);
    if (!f) {
        errno = ENOENT;
        return -1;
    }
    if (f.isDirectory()) {
        st->st_mode = S_IFDIR | 0755;
    } else {
        st->st_mode = S_IFREG | 0644;
        st->st_size = static_cast<off_t>(f.size());
    }
    f.close();
    return 0;
}

int _unlink(const char* path) {
    if (!s_fs_mounted || !s_active_fs || !path || !path[0]) { errno = ENOENT; return -1; }
    char norm[192];
    normalize_fs_path(path, norm, sizeof(norm));
    return s_active_fs->remove(norm) ? 0 : -1;
}

int _link(const char* /*oldp*/, const char* /*newp*/) {
    errno = ENOSYS;
    return -1;
}

int _rename(const char* oldp, const char* newp) {
    if (!s_fs_mounted || !s_active_fs || !oldp || !newp) { errno = ENOENT; return -1; }
    char n1[192], n2[192];
    normalize_fs_path(oldp, n1, sizeof(n1));
    normalize_fs_path(newp, n2, sizeof(n2));
    return s_active_fs->rename(n1, n2) ? 0 : -1;
}

int mkdir(const char* path, mode_t /*mode*/) {
    return oyk_imxrt_mkdir(path);
}

} /* extern "C" */

#else /* Host build / unit tests */

static int s_sim_sd_ok = 1;
static int s_sim_qspi_ok = 0;
static int s_sim_int_ok = 1;
static int s_sim_format_counts[4] = { 0, 0, 0, 0 };

extern "C" {

int tinytmux_fs_sim_format_count(int tier) {
    return (tier >= 0 && tier < 4) ? s_sim_format_counts[tier] : 0;
}

int tinytmux_fs_format_tier(int tier, char* msg_out, size_t msg_sz) {
    int ok = 0;
    if (tier == TMUX_STORAGE_SD) ok = s_sim_sd_ok;
    else if (tier == TMUX_STORAGE_QSPI) ok = s_sim_qspi_ok;
    else if (tier == TMUX_STORAGE_INTERNAL) ok = s_sim_int_ok;
    if (ok) {
        s_sim_format_counts[tier]++;
        fs_str_copy(msg_out, msg_sz, "Storage tier formatted.");
    } else {
        fs_str_copy(msg_out, msg_sz, "Storage tier not present.");
    }
    return ok;
}

int tinytmux_fs_rescan() {
    tinytmux_fs_unmount();
    return tinytmux_fs_mount();
}

void tinytmux_fs_set_sim_tiers(int sd_present, int qspi_present, int internal_present) {
    s_sim_sd_ok = sd_present ? 1 : 0;
    s_sim_qspi_ok = qspi_present ? 1 : 0;
    s_sim_int_ok = internal_present ? 1 : 0;
}

OYK_FLASHMEM int tinytmux_fs_mount() {
    if (s_sim_sd_ok) {
        s_fs_mounted = 1;
        s_active_tier = TMUX_STORAGE_SD;
        fs_str_copy(s_fs_label, sizeof(s_fs_label), "SDIO MOUNTED");
        return 1;
    }
    if (s_sim_qspi_ok) {
        s_fs_mounted = 1;
        s_active_tier = TMUX_STORAGE_QSPI;
        fs_str_copy(s_fs_label, sizeof(s_fs_label), "QSPI FLASH");
        return 1;
    }
    if (s_sim_int_ok) {
        s_fs_mounted = 1;
        s_active_tier = TMUX_STORAGE_INTERNAL;
        fs_str_copy(s_fs_label, sizeof(s_fs_label), "INT FLASH 4M");
        return 1;
    }
    s_fs_mounted = 0;
    s_active_tier = TMUX_STORAGE_NONE;
    fs_str_copy(s_fs_label, sizeof(s_fs_label), "NO STORAGE");
    return 0;
}

void tinytmux_fs_unmount() {
    s_fs_mounted = 0;
    s_active_tier = TMUX_STORAGE_NONE;
    fs_str_copy(s_fs_label, sizeof(s_fs_label), "UNMOUNTED");
}

int tinytmux_fs_is_mounted() {
    return s_fs_mounted;
}

int tinytmux_fs_active_tier() {
    return s_active_tier;
}

int tinytmux_fs_is_flash_storage() {
    return (s_fs_mounted &&
            (s_active_tier == TMUX_STORAGE_QSPI || s_active_tier == TMUX_STORAGE_INTERNAL))
           ? 1 : 0;
}

const char* tinytmux_fs_status_label() {
    return s_fs_label;
}

OYK_FLASHMEM int tinytmux_fs_tier_info(int tier, TinyTmuxStorageTierInfo* out) {
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    out->tier = tier;
    if (tier == TMUX_STORAGE_SD) {
        out->detected = s_sim_sd_ok;
        out->mounted = (s_fs_mounted && s_active_tier == TMUX_STORAGE_SD) ? 1 : 0;
        out->total_kb = s_sim_sd_ok ? 32768UL : 0UL;
        fs_str_copy(out->label, sizeof(out->label), "microSD Card (SDIO)");
        return 1;
    }
    if (tier == TMUX_STORAGE_QSPI) {
        const TinyTmuxPsramInfo* pinfo = tinytmux_psram_info();
        out->detected = (s_sim_qspi_ok || (pinfo && pinfo->has_valid_qspi_flash_cs1)) ? 1 : 0;
        out->mounted = (s_fs_mounted && s_active_tier == TMUX_STORAGE_QSPI) ? 1 : 0;
        out->total_kb = out->detected ? 16384UL : 0UL;
        fs_str_copy(out->label, sizeof(out->label), "QSPI Flash (CS1 / Pin 51)");
        return 1;
    }
    if (tier == TMUX_STORAGE_INTERNAL) {
        out->detected = s_sim_int_ok;
        out->mounted = (s_fs_mounted && s_active_tier == TMUX_STORAGE_INTERNAL) ? 1 : 0;
        out->total_kb = s_sim_int_ok ? static_cast<oyk_u32>(TINYTMUX_INTERNAL_FS_BYTES / 1024UL) : 0UL;
        fs_str_copy(out->label, sizeof(out->label), "Internal Flash (4 MB)");
        return 1;
    }
    return 0;
}

int oyk_imxrt_mkdir(const char* /*path*/) { return -1; }
int oyk_imxrt_is_regular_file(const char* /*path*/) { return 0; }
int oyk_imxrt_is_directory(const char* /*path*/) { return 0; }
void* oyk_imxrt_opendir(const char* /*dir_path*/) { return NULL; }
int oyk_imxrt_readdir(void* /*dir_handle*/, char* /*name_out*/, size_t /*name_sz*/,
                      int* /*is_dir_out*/, oyk_u32* /*size_out*/) { return 0; }
void oyk_imxrt_closedir(void* /*dir_handle*/) {}

} /* extern "C" */

#endif /* TEENSYDUINO */
