/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "mac_capture.hpp"

#include <stdio.h>
#include <string.h>

#include <Multiverse.h>
#include <Quickdraw.h>
#include <Memory.h>
#include <Files.h>
#include <Events.h>

static short g_out_vref = 0;

static void mc_c2p(const char* src, Str255 dst) {
    if (!src) { dst[0] = 0; return; }
    size_t n = strlen(src);
    if (n > 255U) n = 255U;
    dst[0] = static_cast<unsigned char>(n);
    if (n > 0U) memcpy(dst + 1, src, n);
}

static short mc_find_volume(const char* vol_name) {
    Str255 target;
    mc_c2p(vol_name, target);
    for (short idx = 1; idx <= 12; ++idx) {
        ParamBlockRec pb;
        Str255 vname;
        memset(&pb, 0, sizeof(pb));
        vname[0] = 0;
        pb.volumeParam.ioNamePtr = vname;
        pb.volumeParam.ioVolIndex = idx;
        if (PBGetVInfoSync(&pb) != noErr) break;
        if (vname[0] == target[0] &&
            memcmp(vname + 1, target + 1, target[0]) == 0) {
            return pb.volumeParam.ioVRefNum;
        }
    }
    return 0;
}

static int mc_hfs_file_exists(short vref, const char* fname) {
    Str255 pname;
    mc_c2p(fname, pname);
    short ref = 0;
    if (HOpen(vref, 0, pname, fsRdPerm, &ref) == noErr) {
        FSClose(ref);
        return 1;
    }
    return 0;
}

static int mc_hfs_write_bytes(short vref, const char* fname,
                              OSType ftype, const void* data, long len) {
    Str255 pname;
    mc_c2p(fname, pname);
    HDelete(vref, 0, pname);
    HCreate(vref, 0, pname, 'OYKN', ftype);
    short ref = 0;
    if (HOpen(vref, 0, pname, fsWrPerm, &ref) != noErr) {
        return 0;
    }
    long count = len;
    OSErr err = FSWrite(ref, &count, data);
    FSClose(ref);
    return (err == noErr && count == len) ? 1 : 0;
}

static void mc_write_le16(unsigned char* p, unsigned short v) {
    p[0] = static_cast<unsigned char>(v & 0xFFU);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFFU);
}

static void mc_write_le32(unsigned char* p, unsigned long v) {
    p[0] = static_cast<unsigned char>(v & 0xFFUL);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFFUL);
    p[2] = static_cast<unsigned char>((v >> 16) & 0xFFUL);
    p[3] = static_cast<unsigned char>((v >> 24) & 0xFFUL);
}

void oyk_mac_mount_all_drives(unsigned long max_wait_ticks) {
    unsigned long start = TickCount();
    do {
        SystemTask();
        EventRecord ev;
        if (GetNextEvent(everyEvent, &ev) && ev.what == diskEvt) {
            ParamBlockRec dpb;
            memset(&dpb, 0, sizeof(dpb));
            dpb.volumeParam.ioVRefNum = static_cast<short>(ev.message & 0xFFFFL);
            (void)PBMountVol(&dpb);
        }
        DrvQEl* dq = reinterpret_cast<DrvQEl*>(LMGetDrvQHdr().qHead);
        while (dq) {
            ParamBlockRec pb;
            memset(&pb, 0, sizeof(pb));
            pb.volumeParam.ioVRefNum = dq->dQDrive;
            (void)PBMountVol(&pb);
            dq = reinterpret_cast<DrvQEl*>(dq->qLink);
        }
        for (short drv = 1; drv <= 4; ++drv) {
            ParamBlockRec pb;
            memset(&pb, 0, sizeof(pb));
            pb.volumeParam.ioVRefNum = drv;
            (void)PBMountVol(&pb);
        }
        g_out_vref = mc_find_volume("OYKOUT");
        if (g_out_vref != 0) {
            return;
        }
    } while (static_cast<unsigned long>(TickCount() - start) < max_wait_ticks);
}

int oyk_mac_is_ui_test_mode() {
    g_out_vref = mc_find_volume("OYKOUT");
    if (g_out_vref != 0 && mc_hfs_file_exists(g_out_vref, "UITEST.FLG")) {
        return 1;
    }
    if (mc_hfs_file_exists(0, "UITEST.FLG") || mc_hfs_file_exists(0, ":UITEST.FLG")) {
        /* Wait up to 480 ticks (Mini vMac 240-tick Sony insert delay) for OYKOUT volume. */
        oyk_mac_mount_all_drives(480UL);
        return 1;
    }
    return 0;
}

int oyk_mac_capture_screen_bmp(const char* hfs_filename, int has_color_qd) {
    if (!hfs_filename || !hfs_filename[0]) return 0;

    int w = 512;
    int h = 342;
    int depth = 1;
    int row_bytes = 64;
    const unsigned char* base = NULL;
    CTabHandle ctab = NULL;
    PixMapHandle pm = NULL;

    if (has_color_qd) {
        GDHandle gd = GetMainDevice();
        if (gd && (**gd).gdPMap) {
            pm = (**gd).gdPMap;
            HLock(reinterpret_cast<Handle>(pm));
            w = static_cast<int>((**pm).bounds.right - (**pm).bounds.left);
            h = static_cast<int>((**pm).bounds.bottom - (**pm).bounds.top);
            row_bytes = static_cast<int>((**pm).rowBytes & 0x3FFF);
            depth = static_cast<int>((**pm).pixelSize);
            base = reinterpret_cast<const unsigned char*>((**pm).baseAddr);
            ctab = (**pm).pmTable;
        }
    }

    if (!base) {
        w = static_cast<int>(qd.screenBits.bounds.right - qd.screenBits.bounds.left);
        h = static_cast<int>(qd.screenBits.bounds.bottom - qd.screenBits.bounds.top);
        row_bytes = static_cast<int>(qd.screenBits.rowBytes & 0x3FFF);
        depth = 1;
        base = reinterpret_cast<const unsigned char*>(qd.screenBits.baseAddr);
    }

    if (!base || w <= 0 || h <= 0 || w > 1280) {
        if (pm) HUnlock(reinterpret_cast<Handle>(pm));
        return 0;
    }

    unsigned long row_stride = static_cast<unsigned long>((w * 3 + 3) & ~3);
    unsigned char* row_buf = reinterpret_cast<unsigned char*>(NewPtrClear(static_cast<long>(row_stride)));
    if (!row_buf) {
        if (pm) HUnlock(reinterpret_cast<Handle>(pm));
        return 0;
    }

    short vref = (g_out_vref != 0) ? g_out_vref : 0;
    Str255 pname;
    mc_c2p(hfs_filename, pname);
    HDelete(vref, 0, pname);
    HCreate(vref, 0, pname, 'OYKN', 'BMP ');
    short ref = 0;
    if (HOpen(vref, 0, pname, fsWrPerm, &ref) != noErr) {
        DisposePtr(reinterpret_cast<Ptr>(row_buf));
        if (pm) HUnlock(reinterpret_cast<Handle>(pm));
        return 0;
    }

    unsigned long img_size = row_stride * static_cast<unsigned long>(h);
    unsigned long file_size = 54UL + img_size;

    unsigned char hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B';
    hdr[1] = 'M';
    mc_write_le32(hdr + 2, file_size);
    mc_write_le32(hdr + 10, 54UL);
    mc_write_le32(hdr + 14, 40UL);
    mc_write_le32(hdr + 18, static_cast<unsigned long>(w));
    mc_write_le32(hdr + 22, static_cast<unsigned long>(h));
    mc_write_le16(hdr + 26, 1U);
    mc_write_le16(hdr + 28, 24U);
    mc_write_le32(hdr + 34, img_size);
    long hcnt = static_cast<long>(sizeof(hdr));
    FSWrite(ref, &hcnt, hdr);

    for (int y = h - 1; y >= 0; --y) {
        const unsigned char* src = base + static_cast<long>(y) * static_cast<long>(row_bytes);
        memset(row_buf, 0, row_stride);
        for (int x = 0; x < w; ++x) {
            unsigned char r = 255;
            unsigned char g = 255;
            unsigned char b = 255;
            if (depth == 1) {
                int bit = (src[x >> 3] >> (7 - (x & 7))) & 1;
                if (bit != 0) { r = 0; g = 0; b = 0; }
            } else if (depth == 8 || depth == 4 || depth == 2) {
                unsigned int idx = 0;
                if (depth == 8) {
                    idx = src[x];
                } else if (depth == 4) {
                    idx = (x & 1) ? (src[x >> 1] & 0x0FU) : ((src[x >> 1] >> 4) & 0x0FU);
                } else {
                    int shift = (3 - (x & 3)) * 2;
                    idx = (src[x >> 2] >> shift) & 0x03U;
                }
                if (ctab && idx <= static_cast<unsigned int>((**ctab).ctSize)) {
                    const ColorSpec& cs = (**ctab).ctTable[idx];
                    r = static_cast<unsigned char>(cs.rgb.red >> 8);
                    g = static_cast<unsigned char>(cs.rgb.green >> 8);
                    b = static_cast<unsigned char>(cs.rgb.blue >> 8);
                } else {
                    unsigned char v = static_cast<unsigned char>(255U - idx);
                    r = v; g = v; b = v;
                }
            } else if (depth == 16) {
                unsigned short px = static_cast<unsigned short>(
                    (static_cast<unsigned short>(src[x * 2]) << 8) | src[x * 2 + 1]);
                r = static_cast<unsigned char>(((px >> 10) & 0x1FU) * 255U / 31U);
                g = static_cast<unsigned char>(((px >> 5) & 0x1FU) * 255U / 31U);
                b = static_cast<unsigned char>((px & 0x1FU) * 255U / 31U);
            } else if (depth == 32) {
                r = src[x * 4 + 1];
                g = src[x * 4 + 2];
                b = src[x * 4 + 3];
            }
            row_buf[x * 3 + 0] = b;
            row_buf[x * 3 + 1] = g;
            row_buf[x * 3 + 2] = r;
        }
        long rcnt = static_cast<long>(row_stride);
        FSWrite(ref, &rcnt, row_buf);
    }

    FSClose(ref);
    DisposePtr(reinterpret_cast<Ptr>(row_buf));
    if (pm) HUnlock(reinterpret_cast<Handle>(pm));
    FlushVol(NULL, vref);
    return 1;
}

void oyk_mac_finish_test_and_shutdown(int vrefnum) {
    static const char kDoneMarker[] = "OYK_TEST_COMPLETE_MARKER\n";
    short vref = (g_out_vref != 0) ? g_out_vref : 0;
    mc_hfs_write_bytes(vref, "DONE.FLG", 'TEXT', kDoneMarker,
                       static_cast<long>(sizeof(kDoneMarker) - 1U));
    FlushVol(NULL, vref);
    if (vrefnum != vref) {
        FlushVol(NULL, static_cast<short>(vrefnum));
    }
    ShutDwnPower();
}
