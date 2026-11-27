/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_LYRA_DFS_FS_STUB_H
#define OYK_LYRA_DFS_FS_STUB_H

#ifdef __cplusplus
extern "C" {
#endif

int dfs_mount(const char* device_name, const char* path, const char* filesystemtype,
              unsigned long rwflag, const void* data);
int dfs_mkfs(const char* fs_name, const char* device_name);

#ifdef __cplusplus
}
#endif

#endif /* OYK_LYRA_DFS_FS_STUB_H */
