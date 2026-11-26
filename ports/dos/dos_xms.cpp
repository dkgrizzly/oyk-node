/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "dos_xms.hpp"

#include <string.h>

#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
  #include <dos.h>
  #include <i86.h>
#else
  #include <stdlib.h>
#endif

#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)

#ifdef __WATCOMC__
/* 8086-safe EFLAGS probing:
 * - On 8086/8088/80186, FLAGS bits 12..15 are hardwired to 1.
 * - On 80286 real mode, FLAGS bits 12..14 (IOPL/NT) stay 0 when set.
 * - On 80386+, bits 12..14 can be set/cleared cleanly.
 */
static oyk_u16 dos_probe_flags_bits();
#pragma aux dos_probe_flags_bits = \
    "pushf" \
    "pop bx" \
    "mov ax, bx" \
    "and ax, 0FFFh" \
    "push ax" \
    "popf" \
    "pushf" \
    "pop cx" \
    "and cx, 0F000h" \
    "cmp cx, 0F000h" \
    "je not_386" \
    "or ax, 4000h" \
    "push ax" \
    "popf" \
    "pushf" \
    "pop cx" \
    "push bx" \
    "popf" \
    "and cx, 4000h" \
    "mov ax, cx" \
    "jmp done_386" \
    "not_386:" \
    "push bx" \
    "popf" \
    "xor ax, ax" \
    "done_386:" \
    value [ax] \
    modify [bx cx];

struct OykXmsMoveBlock {
    oyk_u32 length;
    oyk_u16 src_handle;
    oyk_u32 src_offset;
    oyk_u16 dst_handle;
    oyk_u32 dst_offset;
};

static oyk_u16 xms_query_free_kb(oyk_u32 far* entry_ptr) {
    oyk_u16 max_kb = 0;
    void (far *fn)() = reinterpret_cast<void (far *)()>(*entry_ptr);
    (void)fn;
    /* Call XMS AH=08h (Query Free Extended Memory) */
    __asm {
        mov ah, 08h
        les bx, entry_ptr
        call dword ptr es:[bx]
        mov max_kb, ax
    }
    return max_kb;
}

static int xms_alloc_kb(oyk_u32 far* entry_ptr, oyk_u16 kb, oyk_u16* out_handle) {
    oyk_u16 ok = 0;
    oyk_u16 h = 0;
    __asm {
        mov ah, 09h
        mov dx, kb
        les bx, entry_ptr
        call dword ptr es:[bx]
        mov ok, ax
        mov h, dx
    }
    if (ok == 1U) {
        if (out_handle) *out_handle = h;
        return 1;
    }
    return 0;
}

static void xms_free_handle(oyk_u32 far* entry_ptr, oyk_u16 h) {
    __asm {
        mov ah, 0Ah
        mov dx, h
        les bx, entry_ptr
        call dword ptr es:[bx]
    }
}

static int xms_move_block(oyk_u32 far* entry_ptr, OykXmsMoveBlock far* blk) {
    oyk_u16 ok = 0;
    oyk_u16 blk_seg = FP_SEG(blk);
    oyk_u16 blk_off = FP_OFF(blk);
    oyk_u32 entry_val = *entry_ptr;
    oyk_u16 entry_seg = static_cast<oyk_u16>((entry_val >> 16) & 0xFFFFUL);
    oyk_u16 entry_off = static_cast<oyk_u16>(entry_val & 0xFFFFUL);
    __asm {
        mov cx, blk_seg
        mov si, blk_off
        mov dx, entry_seg
        mov bx, entry_off
        push ds
        push bp
        push dx
        push bx
        mov bp, sp
        mov ax, cx
        mov ds, ax
        mov ah, 0Bh
        call dword ptr ss:[bp]
        add sp, 4
        pop bp
        pop ds
        mov ok, ax
    }
    return (ok == 1U) ? 1 : 0;
}
#endif

int oyk_dos_is_386_or_newer() {
#ifdef __WATCOMC__
    return (dos_probe_flags_bits() != 0U) ? 1 : 0;
#else
    return 1;
#endif
}

#else

int oyk_dos_is_386_or_newer() {
    return 1;
}

#endif

OykDosExtMem::OykDosExtMem()
    : backend_(OYK_EXTMEM_NONE),
      capacity_bytes_(0),
      xms_handle_(0),
      xms_entry_far_(0),
      host_buf_(NULL) {}

OykDosExtMem::~OykDosExtMem() {
    shutdown();
}

const char* OykDosExtMem::backend_name() const {
    if (backend_ == OYK_EXTMEM_XMS) return "XMS";
    if (backend_ == OYK_EXTMEM_EMS) return "EMS";
    if (backend_ == OYK_EXTMEM_HOST_SIM) return "SIM";
    return "NONE";
}

int OykDosExtMem::init_xms(oyk_u32 bytes_needed) {
#if (defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)) && defined(__WATCOMC__)
    union REGS r;
    struct SREGS sr;
    memset(&r, 0, sizeof(r));
    memset(&sr, 0, sizeof(sr));
    r.x.ax = 0x4300U;
    int86x(0x2F, &r, &r, &sr);
    if (r.h.al != 0x80U) return 0;

    memset(&r, 0, sizeof(r));
    memset(&sr, 0, sizeof(sr));
    r.x.ax = 0x4310U;
    int86x(0x2F, &r, &r, &sr);
    xms_entry_far_ = (static_cast<oyk_u32>(sr.es) << 16) | static_cast<oyk_u32>(r.x.bx);
    if (xms_entry_far_ == 0UL) return 0;

    oyk_u32 kb_req = (bytes_needed + 1023UL) / 1024UL;
    if (kb_req == 0UL) kb_req = 1UL;
    if (kb_req > 65535UL) kb_req = 65535UL;

    oyk_u16 free_kb = xms_query_free_kb(&xms_entry_far_);
    if (free_kb < static_cast<oyk_u16>(kb_req)) {
        xms_entry_far_ = 0;
        return 0;
    }

    oyk_u16 h = 0;
    if (!xms_alloc_kb(&xms_entry_far_, static_cast<oyk_u16>(kb_req), &h)) {
        xms_entry_far_ = 0;
        return 0;
    }
    xms_handle_ = h;
    capacity_bytes_ = static_cast<oyk_u32>(kb_req) * 1024UL;
    backend_ = OYK_EXTMEM_XMS;
    return 1;
#else
    (void)bytes_needed;
    return 0;
#endif
}

int OykDosExtMem::init(oyk_u32 bytes_needed) {
    shutdown();
    if (bytes_needed == 0UL) return 0;

#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
    if (init_xms(bytes_needed)) return 1;

    oyk_u32 ems_pages = (bytes_needed + 16383UL) / 16384UL;
    if (ems_pages == 0UL) ems_pages = 1UL;
    if (ems_pages <= 255UL && ems_.init(static_cast<oyk_u16>(ems_pages))) {
        capacity_bytes_ = ems_pages * 16384UL;
        backend_ = OYK_EXTMEM_EMS;
        return 1;
    }
    return 0;
#else
    host_buf_ = static_cast<oyk_u8*>(malloc(static_cast<size_t>(bytes_needed)));
    if (!host_buf_) return 0;
    memset(host_buf_, 0, static_cast<size_t>(bytes_needed));
    capacity_bytes_ = bytes_needed;
    backend_ = OYK_EXTMEM_HOST_SIM;
    return 1;
#endif
}

void OykDosExtMem::shutdown() {
#if (defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)) && defined(__WATCOMC__)
    if (backend_ == OYK_EXTMEM_XMS && xms_handle_ != 0U && xms_entry_far_ != 0UL) {
        xms_free_handle(&xms_entry_far_, xms_handle_);
    }
#endif
    xms_handle_ = 0;
    xms_entry_far_ = 0;
    ems_.shutdown();
    if (host_buf_) {
#if !(defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS))
        free(host_buf_);
#endif
        host_buf_ = NULL;
    }
    capacity_bytes_ = 0;
    backend_ = OYK_EXTMEM_NONE;
}

int OykDosExtMem::write_block(oyk_u32 dst_offset, const void* src, oyk_u16 len) {
    if (backend_ == OYK_EXTMEM_NONE || !src || len == 0U) return 0;
    if (dst_offset + static_cast<oyk_u32>(len) > capacity_bytes_) return 0;

    if (backend_ == OYK_EXTMEM_HOST_SIM) {
        memcpy(host_buf_ + dst_offset, src, len);
        return 1;
    }

    if (backend_ == OYK_EXTMEM_EMS) {
        const oyk_u8* p = static_cast<const oyk_u8*>(src);
        oyk_u16 rem = len;
        oyk_u32 cur = dst_offset;
        while (rem > 0U) {
            oyk_u16 pg = static_cast<oyk_u16>(cur / 16384UL);
            oyk_u16 off = static_cast<oyk_u16>(cur % 16384UL);
            oyk_u16 avail = static_cast<oyk_u16>(16384U - off);
            oyk_u16 chunk = (rem < avail) ? rem : avail;
            if (!ems_.write_page(pg, off, p, chunk)) return 0;
            p += chunk;
            cur += chunk;
            rem = static_cast<oyk_u16>(rem - chunk);
        }
        return 1;
    }

#if (defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)) && defined(__WATCOMC__)
    if (backend_ == OYK_EXTMEM_XMS) {
        /* XMS INT/func 0Bh requires even length and even XMS offsets if strictly aligned;
         * handle arbitrary offset/length via small read-modify-write for boundary bytes. */
        const oyk_u8* sptr = static_cast<const oyk_u8*>(src);
        oyk_u32 cur = dst_offset;
        oyk_u16 rem = len;

        if ((cur & 1UL) != 0UL) {
            oyk_u8 pair[2];
            OykXmsMoveBlock rb;
            rb.length = 2UL;
            rb.src_handle = xms_handle_;
            rb.src_offset = cur - 1UL;
            rb.dst_handle = 0U;
            rb.dst_offset = (static_cast<oyk_u32>(FP_SEG(pair)) << 16) | static_cast<oyk_u32>(FP_OFF(pair));
            if (!xms_move_block(&xms_entry_far_, &rb)) return 0;
            pair[1] = *sptr++;
            OykXmsMoveBlock wb;
            wb.length = 2UL;
            wb.src_handle = 0U;
            wb.src_offset = rb.dst_offset;
            wb.dst_handle = xms_handle_;
            wb.dst_offset = cur - 1UL;
            if (!xms_move_block(&xms_entry_far_, &wb)) return 0;
            ++cur;
            --rem;
        }

        oyk_u16 even_len = static_cast<oyk_u16>(rem & 0xFFFEU);
        if (even_len > 0U) {
            OykXmsMoveBlock blk;
            blk.length = even_len;
            blk.src_handle = 0U;
            blk.src_offset = (static_cast<oyk_u32>(FP_SEG(sptr)) << 16) | static_cast<oyk_u32>(FP_OFF(sptr));
            blk.dst_handle = xms_handle_;
            blk.dst_offset = cur;
            if (!xms_move_block(&xms_entry_far_, &blk)) return 0;
            sptr += even_len;
            cur += even_len;
            rem = static_cast<oyk_u16>(rem - even_len);
        }

        if (rem == 1U) {
            oyk_u8 pair[2];
            OykXmsMoveBlock rb;
            rb.length = 2UL;
            rb.src_handle = xms_handle_;
            rb.src_offset = cur;
            rb.dst_handle = 0U;
            rb.dst_offset = (static_cast<oyk_u32>(FP_SEG(pair)) << 16) | static_cast<oyk_u32>(FP_OFF(pair));
            if (!xms_move_block(&xms_entry_far_, &rb)) return 0;
            pair[0] = *sptr;
            OykXmsMoveBlock wb;
            wb.length = 2UL;
            wb.src_handle = 0U;
            wb.src_offset = rb.dst_offset;
            wb.dst_handle = xms_handle_;
            wb.dst_offset = cur;
            if (!xms_move_block(&xms_entry_far_, &wb)) return 0;
        }
        return 1;
    }
#endif
    return 0;
}

int OykDosExtMem::read_block(oyk_u32 src_offset, void* dst, oyk_u16 len) {
    if (backend_ == OYK_EXTMEM_NONE || !dst || len == 0U) return 0;
    if (src_offset + static_cast<oyk_u32>(len) > capacity_bytes_) return 0;

    if (backend_ == OYK_EXTMEM_HOST_SIM) {
        memcpy(dst, host_buf_ + src_offset, len);
        return 1;
    }

    if (backend_ == OYK_EXTMEM_EMS) {
        oyk_u8* p = static_cast<oyk_u8*>(dst);
        oyk_u16 rem = len;
        oyk_u32 cur = src_offset;
        while (rem > 0U) {
            oyk_u16 pg = static_cast<oyk_u16>(cur / 16384UL);
            oyk_u16 off = static_cast<oyk_u16>(cur % 16384UL);
            oyk_u16 avail = static_cast<oyk_u16>(16384U - off);
            oyk_u16 chunk = (rem < avail) ? rem : avail;
            if (!ems_.read_page(pg, off, p, chunk)) return 0;
            p += chunk;
            cur += chunk;
            rem = static_cast<oyk_u16>(rem - chunk);
        }
        return 1;
    }

#if (defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)) && defined(__WATCOMC__)
    if (backend_ == OYK_EXTMEM_XMS) {
        oyk_u8* dptr = static_cast<oyk_u8*>(dst);
        oyk_u32 cur = src_offset;
        oyk_u16 rem = len;

        if ((cur & 1UL) != 0UL) {
            oyk_u8 pair[2];
            OykXmsMoveBlock rb;
            rb.length = 2UL;
            rb.src_handle = xms_handle_;
            rb.src_offset = cur - 1UL;
            rb.dst_handle = 0U;
            rb.dst_offset = (static_cast<oyk_u32>(FP_SEG(pair)) << 16) | static_cast<oyk_u32>(FP_OFF(pair));
            if (!xms_move_block(&xms_entry_far_, &rb)) return 0;
            *dptr++ = pair[1];
            ++cur;
            --rem;
        }

        oyk_u16 even_len = static_cast<oyk_u16>(rem & 0xFFFEU);
        if (even_len > 0U) {
            OykXmsMoveBlock blk;
            blk.length = even_len;
            blk.src_handle = xms_handle_;
            blk.src_offset = cur;
            blk.dst_handle = 0U;
            blk.dst_offset = (static_cast<oyk_u32>(FP_SEG(dptr)) << 16) | static_cast<oyk_u32>(FP_OFF(dptr));
            if (!xms_move_block(&xms_entry_far_, &blk)) return 0;
            dptr += even_len;
            cur += even_len;
            rem = static_cast<oyk_u16>(rem - even_len);
        }

        if (rem == 1U) {
            oyk_u8 pair[2];
            OykXmsMoveBlock rb;
            rb.length = 2UL;
            rb.src_handle = xms_handle_;
            rb.src_offset = cur;
            rb.dst_handle = 0U;
            rb.dst_offset = (static_cast<oyk_u32>(FP_SEG(pair)) << 16) | static_cast<oyk_u32>(FP_OFF(pair));
            if (!xms_move_block(&xms_entry_far_, &rb)) return 0;
            *dptr = pair[0];
        }
        return 1;
    }
#endif
    return 0;
}
