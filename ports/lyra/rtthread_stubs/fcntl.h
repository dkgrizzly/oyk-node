/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_LYRA_FCNTL_STUB_H
#define OYK_LYRA_FCNTL_STUB_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef O_RDONLY
#define O_RDONLY   0x0000
#endif
#ifndef O_WRONLY
#define O_WRONLY   0x0001
#endif
#ifndef O_CREAT
#define O_CREAT    0x0200
#endif
#ifndef O_TRUNC
#define O_TRUNC    0x0400
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x4000
#endif
#ifndef F_SETFL
#define F_SETFL    4
#endif

int fcntl(int fd, int cmd, ...);

#ifdef __cplusplus
}
#endif

#endif /* OYK_LYRA_FCNTL_STUB_H */
