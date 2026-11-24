#include "ems.hpp"

#include <string.h>

#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS) || defined(__WATCOMC__)
#include <dos.h>
#include <i86.h>
#endif

OykEms4::OykEms4()
    : handle_(-1), version_bcd_(0), page_frame_seg_(0), allocated_pages_(0) {}

OykEms4::~OykEms4() {
    shutdown();
}

int OykEms4::init(oyk_u16 num_pages) {
#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
    if (num_pages == 0) return 0;

    /* Safe LIM EMS detection: inspect INT 67h handler's device name at offset 0x000A */
    void (__interrupt __far *vec)() = _dos_getvect(0x67);
    if (!vec) return 0;

    oyk_u16 seg = FP_SEG(vec);
    const char __far* sig = (const char __far*)MK_FP(seg, 0x000A);
    static const char expected[8] = {'E','M','M','X','X','X','X','0'};
    for (int i = 0; i < 8; ++i) {
        if (sig[i] != expected[i]) return 0;
    }

    union REGS r;

    /* AH=40h: Get EMM status */
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x40;
    int86(0x67, &r, &r);
    if (r.h.ah != 0) return 0;

    /* AH=46h: Get EMM version (require >= 4.0 = 0x40) */
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x46;
    int86(0x67, &r, &r);
    if (r.h.ah != 0 || r.h.al < 0x40) return 0;
    version_bcd_ = r.h.al;

    /* AH=41h: Get Page Frame Segment */
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x41;
    int86(0x67, &r, &r);
    if (r.h.ah != 0) return 0;
    page_frame_seg_ = r.x.bx;

    /* AH=42h: Check available unallocated pages */
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x42;
    int86(0x67, &r, &r);
    if (r.h.ah != 0 || r.x.bx < num_pages) return 0;

    /* AH=43h: Allocate logical pages */
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x43;
    r.x.bx = num_pages;
    int86(0x67, &r, &r);
    if (r.h.ah != 0) return 0;

    handle_ = static_cast<int>(r.x.dx);
    allocated_pages_ = num_pages;
    return 1;
#else
    (void)num_pages;
    return 0;
#endif
}

void OykEms4::shutdown() {
#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
    if (handle_ >= 0) {
        union REGS r;
        memset(&r, 0, sizeof(r));
        r.h.ah = 0x45; /* Deallocate handle & pages */
        r.x.dx = static_cast<oyk_u16>(handle_);
        int86(0x67, &r, &r);
        handle_ = -1;
        allocated_pages_ = 0;
    }
#endif
}

int OykEms4::map_page0(oyk_u16 logical_page) {
#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
    if (handle_ < 0 || logical_page >= allocated_pages_) return 0;
    union REGS r;
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x44;
    r.h.al = 0; /* Physical page 0 */
    r.x.bx = logical_page;
    r.x.dx = static_cast<oyk_u16>(handle_);
    int86(0x67, &r, &r);
    return (r.h.ah == 0) ? 1 : 0;
#else
    (void)logical_page;
    return 0;
#endif
}

int OykEms4::write_page(oyk_u16 logical_page, oyk_u16 offset, const void* src, oyk_u16 len) {
#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
    if (!src || static_cast<unsigned long>(offset) + len > 16384UL) return 0;
    if (!map_page0(logical_page)) return 0;
    oyk_u8 __far* dst = (oyk_u8 __far*)MK_FP(page_frame_seg_, offset);
    _fmemcpy(dst, src, len);
    return 1;
#else
    (void)logical_page; (void)offset; (void)src; (void)len;
    return 0;
#endif
}

int OykEms4::read_page(oyk_u16 logical_page, oyk_u16 offset, void* dst, oyk_u16 len) {
#if defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
    if (!dst || static_cast<unsigned long>(offset) + len > 16384UL) return 0;
    if (!map_page0(logical_page)) return 0;
    const oyk_u8 __far* src = (const oyk_u8 __far*)MK_FP(page_frame_seg_, offset);
    _fmemcpy(dst, src, len);
    return 1;
#else
    (void)logical_page; (void)offset; (void)dst; (void)len;
    return 0;
#endif
}
