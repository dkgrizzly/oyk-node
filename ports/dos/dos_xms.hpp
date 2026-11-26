/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_DOS_XMS_HPP
#define OYK_DOS_XMS_HPP

/*
 * MS-DOS 386+ CPU detector and Extended Memory (XMS -> LIM EMS 4.0) allocator
 * for off-screen graphical video / scrollback buffers in OpenWatcom 16-bit
 * Large-model C++98 builds. Falls back to heap RAM simulation on POSIX hosts.
 */

#include "../compat/oyk_portable.hpp"
#include "ems.hpp"

#define OYK_EXTMEM_NONE     0
#define OYK_EXTMEM_XMS      1
#define OYK_EXTMEM_EMS      2
#define OYK_EXTMEM_HOST_SIM 3

/* Returns 1 on 80386 or newer (and on non-DOS hosts), 0 on 8086/80186/80286. */
int oyk_dos_is_386_or_newer();

class OykDosExtMem {
public:
    OykDosExtMem();
    ~OykDosExtMem();

    int init(oyk_u32 bytes_needed);
    void shutdown();

    int available() const { return backend_ != OYK_EXTMEM_NONE; }
    int backend() const { return backend_; }
    const char* backend_name() const;
    oyk_u32 capacity_bytes() const { return capacity_bytes_; }

    int write_block(oyk_u32 dst_offset, const void* src, oyk_u16 len);
    int read_block(oyk_u32 src_offset, void* dst, oyk_u16 len);

private:
    OykDosExtMem(const OykDosExtMem&);
    OykDosExtMem& operator=(const OykDosExtMem&);

    int init_xms(oyk_u32 bytes_needed);

    int backend_;
    oyk_u32 capacity_bytes_;
    oyk_u16 xms_handle_;
    oyk_u32 xms_entry_far_;
    OykEms4 ems_;
    oyk_u8* host_buf_;
};

#endif /* OYK_DOS_XMS_HPP */
