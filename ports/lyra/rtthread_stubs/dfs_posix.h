/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_LYRA_DFS_POSIX_STUB_H
#define OYK_LYRA_DFS_POSIX_STUB_H

#include <stddef.h>
#include "fcntl.h"

#ifdef __cplusplus
extern "C" {
#endif

struct stat {
    unsigned long st_mode;
    unsigned long st_size;
};

int open(const char* file, int flags, ...);
int read(int fd, void* buf, size_t len);
int write(int fd, const void* buf, size_t len);
int close(int fd);
int stat(const char* file, struct stat* buf);

#ifdef __cplusplus
}
#endif

#endif /* OYK_LYRA_DFS_POSIX_STUB_H */
