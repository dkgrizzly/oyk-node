/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_TINYTMUX_FS_HPP
#define OYK_TINYTMUX_FS_HPP

/*
 * MicroSD Mount, Newlib Syscall Retargeting, and Directory Hooks
 * for Teensy 4.1 / IMXRT1062 (TinyTMUX) via Teensyduino SD.h.
 */

#include "oyk_tinytmux_config.h"
#include "oyk_portable.hpp"

#ifdef __cplusplus
extern "C" {
#endif

int tinytmux_fs_mount();
void tinytmux_fs_unmount();
int tinytmux_fs_is_mounted();
const char* tinytmux_fs_status_label();

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
