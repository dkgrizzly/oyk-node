/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "pico_fs.hpp"
#include "board_pico.hpp"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PICO_FS_MAX_FILES  20
#define PICO_FS_MAX_BYTES  2048U
#define PICO_FLASH_STORE_SIZE  (128UL * 1024UL)
#define PICO_FD_OFFSET     16

struct PicoMemFile {
    int used;
    int is_dir;
    char path[96];
    oyk_u8 data[PICO_FS_MAX_BYTES];
    size_t size;
};

struct PicoOpenSlot {
    int open;
    int file_slot;
    size_t pos;
    int writable;
    int append;
};

static PicoFsStatus s_fs_status = {};
static PicoMemFile s_files[PICO_FS_MAX_FILES];
static PicoOpenSlot s_fds[8];
static int s_sim_sd_flag = 0;

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
    for (int i = 0; i < PICO_FS_MAX_FILES; ++i) {
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
    for (int i = 0; i < PICO_FS_MAX_FILES; ++i) {
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

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE

#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include <sys/stat.h>

static spi_inst_t* sd_spi() { return spi0; }

static oyk_u8 sd_xfer(oyk_u8 tx) {
    oyk_u8 rx = 0xFFU;
    spi_write_read_blocking(sd_spi(), &tx, &rx, 1);
    return rx;
}

static oyk_u8 sd_cmd(oyk_u8 cmd, oyk_u32 arg, oyk_u8 crc) {
    sd_xfer(0xFFU);
    sd_xfer(static_cast<oyk_u8>(0x40U | (cmd & 0x3FU)));
    sd_xfer(static_cast<oyk_u8>((arg >> 24) & 0xFFU));
    sd_xfer(static_cast<oyk_u8>((arg >> 16) & 0xFFU));
    sd_xfer(static_cast<oyk_u8>((arg >> 8) & 0xFFU));
    sd_xfer(static_cast<oyk_u8>(arg & 0xFFU));
    sd_xfer(crc);
    oyk_u8 r = 0xFFU;
    for (int i = 0; i < 8; ++i) {
        r = sd_xfer(0xFFU);
        if ((r & 0x80U) == 0) break;
    }
    return r;
}

static int probe_spi_sd_card(const PicoBoardProfile* prof) {
    spi_init(sd_spi(), 400000U);
    gpio_set_function(prof->sd_sck_pin, GPIO_FUNC_SPI);
    gpio_set_function(prof->sd_tx_pin, GPIO_FUNC_SPI);
    gpio_set_function(prof->sd_rx_pin, GPIO_FUNC_SPI);
    gpio_init(prof->sd_cs_pin);
    gpio_set_dir(prof->sd_cs_pin, GPIO_OUT);
    gpio_put(prof->sd_cs_pin, 1);

    for (int i = 0; i < 10; ++i) sd_xfer(0xFFU);
    gpio_put(prof->sd_cs_pin, 0);
    oyk_u8 r1 = sd_cmd(0U, 0UL, 0x95U);
    gpio_put(prof->sd_cs_pin, 1);
    sd_xfer(0xFFU);
    if (r1 != 0x01U) {
        spi_deinit(sd_spi());
        return 0;
    }
    spi_set_baudrate(sd_spi(), 8000000U);
    return 1;
}

struct PicoFlashEraseParam {
    oyk_u32 offset;
    oyk_u32 size;
};

static void flash_erase_cb(void* p) {
    const PicoFlashEraseParam* ep = static_cast<const PicoFlashEraseParam*>(p);
    flash_range_erase(ep->offset, ep->size);
}

static void erase_flash_store() {
    const PicoBoardProfile* prof = pico_current_board_profile();
    if (prof->flash_bytes <= PICO_FLASH_STORE_SIZE) return;
    PicoFlashEraseParam ep;
    ep.offset = prof->flash_bytes - PICO_FLASH_STORE_SIZE;
    ep.size = FLASH_SECTOR_SIZE;
    flash_safe_execute(flash_erase_cb, &ep, 500U);
}

extern "C" {

int _open(const char* path, int flags, int /*mode*/) {
    int slot = -1;
    for (int i = 0; i < 8; ++i) {
        if (!s_fds[i].open) { slot = i; break; }
    }
    if (slot < 0) return -1;
    int acc = flags & O_ACCMODE;
    int writable = (acc == O_WRONLY || acc == O_RDWR) ? 1 : 0;
    int fs = find_slot(path, 0);
    if (!writable) {
        if (fs < 0) return -1;
    } else {
        if (fs < 0) {
            if (!(flags & O_CREAT)) return -1;
            fs = alloc_slot(path, 0);
            if (fs < 0) return -1;
        } else if (flags & O_TRUNC) {
            s_files[fs].size = 0;
        }
    }
    s_fds[slot].open = 1;
    s_fds[slot].file_slot = fs;
    s_fds[slot].writable = writable;
    s_fds[slot].append = (flags & O_APPEND) ? 1 : 0;
    s_fds[slot].pos = s_fds[slot].append ? s_files[fs].size : 0;
    return PICO_FD_OFFSET + slot;
}

int _read(int fd, char* ptr, int len) {
    if (fd == 0) {
        return (ptr && len > 0) ? stdio_get_until(ptr, len, at_the_end_of_time) : 0;
    }
    int s = fd - PICO_FD_OFFSET;
    if (s < 0 || s >= 8 || !s_fds[s].open || !ptr || len <= 0) return 0;
    PicoMemFile& f = s_files[s_fds[s].file_slot];
    if (s_fds[s].pos >= f.size) return 0;
    size_t avail = f.size - s_fds[s].pos;
    size_t n = (static_cast<size_t>(len) < avail) ? static_cast<size_t>(len) : avail;
    memcpy(ptr, f.data + s_fds[s].pos, n);
    s_fds[s].pos += n;
    return static_cast<int>(n);
}

int _write(int fd, char* ptr, int len) {
    if (fd == 1 || fd == 2) {
        if (!ptr || len <= 0) return 0;
        stdio_put_string(ptr, len, false, true);
        return len;
    }
    int s = fd - PICO_FD_OFFSET;
    if (s < 0 || s >= 8 || !s_fds[s].open || !s_fds[s].writable || !ptr || len <= 0) return -1;
    PicoMemFile& f = s_files[s_fds[s].file_slot];
    if (s_fds[s].pos >= PICO_FS_MAX_BYTES) return 0;
    size_t room = PICO_FS_MAX_BYTES - s_fds[s].pos;
    size_t n = (static_cast<size_t>(len) < room) ? static_cast<size_t>(len) : room;
    memcpy(f.data + s_fds[s].pos, ptr, n);
    s_fds[s].pos += n;
    if (s_fds[s].pos > f.size) f.size = s_fds[s].pos;
    return static_cast<int>(n);
}

int _close(int fd) {
    int s = fd - PICO_FD_OFFSET;
    if (s >= 0 && s < 8) {
        s_fds[s].open = 0;
        return 0;
    }
    return -1;
}

int _lseek(int fd, int ptr, int dir) {
    int s = fd - PICO_FD_OFFSET;
    if (s < 0 || s >= 8 || !s_fds[s].open) return -1;
    PicoMemFile& f = s_files[s_fds[s].file_slot];
    long base = 0;
    if (dir == SEEK_CUR) base = static_cast<long>(s_fds[s].pos);
    else if (dir == SEEK_END) base = static_cast<long>(f.size);
    long np = base + ptr;
    if (np < 0) np = 0;
    if (static_cast<size_t>(np) > f.size) np = static_cast<long>(f.size);
    s_fds[s].pos = static_cast<size_t>(np);
    return static_cast<int>(np);
}

int _fstat(int fd, struct stat* st) {
    if ((fd == 0 || fd == 1 || fd == 2) && st) {
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFCHR;
        return 0;
    }
    int s = fd - PICO_FD_OFFSET;
    if (s < 0 || s >= 8 || !s_fds[s].open || !st) return -1;
    memset(st, 0, sizeof(*st));
    PicoMemFile& f = s_files[s_fds[s].file_slot];
    st->st_mode = f.is_dir ? S_IFDIR : S_IFREG;
    st->st_size = static_cast<off_t>(f.size);
    return 0;
}

int _stat(const char* path, struct stat* st) {
    int fs = find_slot(path, -1);
    if (fs < 0 || !st) return -1;
    memset(st, 0, sizeof(*st));
    st->st_mode = s_files[fs].is_dir ? S_IFDIR : S_IFREG;
    st->st_size = static_cast<off_t>(s_files[fs].size);
    return 0;
}

int _unlink(const char* path) {
    int fs = find_slot(path, -1);
    if (fs < 0) return -1;
    s_files[fs].used = 0;
    return 0;
}

int _link(const char* old_p, const char* new_p) {
    int src = find_slot(old_p, 0);
    if (src < 0 || !new_p || !new_p[0]) return -1;
    int dst = alloc_slot(new_p, 0);
    if (dst < 0) return -1;
    s_files[dst].size = s_files[src].size;
    if (s_files[dst].size > 0) {
        memcpy(s_files[dst].data, s_files[src].data, s_files[dst].size);
    }
    return 0;
}

int mkdir(const char* path, mode_t /*mode*/) {
    return (alloc_slot(path, 1) >= 0) ? 0 : -1;
}

} /* extern "C" */

#else

static int probe_spi_sd_card(const PicoBoardProfile* /*prof*/) {
    return s_sim_sd_flag ? 1 : 0;
}

static void erase_flash_store() {}

#endif

void pico_fs_set_sim_sd_present(int present) {
    s_sim_sd_flag = present ? 1 : 0;
}

int pico_fs_init(int prefer_sd, int sim_sd_present) {
    s_sim_sd_flag = sim_sd_present ? 1 : 0;
    memset(&s_fs_status, 0, sizeof(s_fs_status));
    memset(s_files, 0, sizeof(s_files));
    memset(s_fds, 0, sizeof(s_fds));

    const PicoBoardProfile* prof = pico_current_board_profile();
    const int sd_ok = prefer_sd ? probe_spi_sd_card(prof) : 0;
    s_fs_status.sd_present = sd_ok ? 1 : 0;
    s_fs_status.flash_ready = (prof->flash_bytes > PICO_FLASH_STORE_SIZE) ? 1 : 0;
    if (s_fs_status.flash_ready) {
        s_fs_status.flash_partition_offset = prof->flash_bytes - PICO_FLASH_STORE_SIZE;
        s_fs_status.flash_partition_size = PICO_FLASH_STORE_SIZE;
    }

    if (sd_ok) {
        s_fs_status.active_medium = PICO_FS_SD_SPI;
        s_fs_status.total_kb = 1024UL * 32UL;
        strcpy(s_fs_status.medium_label, "SD (SPI0 microSD)");
    } else if (s_fs_status.flash_ready) {
        s_fs_status.active_medium = PICO_FS_FLASH_KV;
        s_fs_status.total_kb = PICO_FLASH_STORE_SIZE / 1024UL;
        strcpy(s_fs_status.medium_label, "FLASH (QSPI KVStore)");
    } else {
        s_fs_status.active_medium = PICO_FS_RAM_SIM;
        s_fs_status.total_kb = (PICO_FS_MAX_FILES * PICO_FS_MAX_BYTES) / 1024UL;
        strcpy(s_fs_status.medium_label, "RAM (Volatile Store)");
    }

    alloc_slot("mail", 1);
    alloc_slot("files", 1);
    alloc_slot("logs", 1);
    return 1;
}

const PicoFsStatus* pico_fs_status() {
    oyk_u32 used = 0;
    for (int i = 0; i < PICO_FS_MAX_FILES; ++i) {
        if (s_files[i].used && !s_files[i].is_dir) used += static_cast<oyk_u32>(s_files[i].size);
    }
    s_fs_status.used_kb = (used + 1023UL) / 1024UL;
    return &s_fs_status;
}

int pico_fs_format_storage(const char* confirm_word) {
    if (!confirm_word || strcmp(confirm_word, "FORMAT") != 0) return 0;
    erase_flash_store();
    memset(s_files, 0, sizeof(s_files));
    alloc_slot("mail", 1);
    alloc_slot("files", 1);
    alloc_slot("logs", 1);
    return 1;
}

int pico_fs_exists(const char* path) {
    return (find_slot(path, -1) >= 0) ? 1 : 0;
}

int pico_fs_write_text_file(const char* path, const char* text) {
    int slot = alloc_slot(path, 0);
    if (slot < 0) return 0;
    size_t n = text ? strlen(text) : 0;
    if (n > PICO_FS_MAX_BYTES) n = PICO_FS_MAX_BYTES;
    if (n > 0) memcpy(s_files[slot].data, text, n);
    s_files[slot].size = n;
    return 1;
}

int pico_fs_read_text_file(const char* path, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return 0;
    int slot = find_slot(path, 0);
    if (slot < 0) return 0;
    size_t n = s_files[slot].size;
    if (n >= out_sz) n = out_sz - 1;
    if (n > 0) memcpy(out, s_files[slot].data, n);
    out[n] = '\0';
    return 1;
}
