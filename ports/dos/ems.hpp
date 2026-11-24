#ifndef OYK_DOS_EMS_HPP
#define OYK_DOS_EMS_HPP

/*
 * Optional LIM EMS 4.0 (INT 67h) support for IBM XT (8088 / NEC V20) with 640KB RAM.
 * Gracefully disables itself if no EMS 4.0 driver/board is installed.
 */

#include "../compat/oyk_portable.hpp"

class OykEms4 {
public:
    OykEms4();
    ~OykEms4();

    /* Detects LIM EMS 4.0 driver and allocates `num_pages` (16KB each). */
    int init(oyk_u16 num_pages);
    void shutdown();

    int available() const { return handle_ >= 0; }
    oyk_u8 version_bcd() const { return version_bcd_; }
    oyk_u16 page_frame_seg() const { return page_frame_seg_; }
    oyk_u16 allocated_pages() const { return allocated_pages_; }

    /* Maps `logical_page` into physical page 0 and copies data in/out. */
    int write_page(oyk_u16 logical_page, oyk_u16 offset, const void* src, oyk_u16 len);
    int read_page(oyk_u16 logical_page, oyk_u16 offset, void* dst, oyk_u16 len);

private:
    int map_page0(oyk_u16 logical_page);

    int handle_;
    oyk_u8 version_bcd_;
    oyk_u16 page_frame_seg_;
    oyk_u16 allocated_pages_;
};

#endif /* OYK_DOS_EMS_HPP */
