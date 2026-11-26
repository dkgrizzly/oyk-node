/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "amiga_capture.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __AMIGA__
#include <exec/execbase.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/view.h>
#include <proto/exec.h>
#include <proto/graphics.h>
extern struct ExecBase* SysBase;
#endif

static void ac_write_le16(unsigned char* p, unsigned short v) {
    p[0] = static_cast<unsigned char>(v & 0xFFU);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFFU);
}

static void ac_write_le32(unsigned char* p, unsigned long v) {
    p[0] = static_cast<unsigned char>(v & 0xFFUL);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFFUL);
    p[2] = static_cast<unsigned char>((v >> 16) & 0xFFUL);
    p[3] = static_cast<unsigned char>((v >> 24) & 0xFFUL);
}

int oyk_amiga_is_ui_test_mode() {
#ifdef __AMIGA__
    FILE* f = fopen("DH0:UITEST.FLG", "r");
    if (!f) f = fopen("UITEST.FLG", "r");
    if (f) {
        fclose(f);
        return 1;
    }
#endif
    return 0;
}

int oyk_amiga_capture_screen_bmp(const char* path, struct Screen* scr) {
#ifdef __AMIGA__
    if (!path || !path[0] || !scr) return 0;

    int w = static_cast<int>(scr->Width);
    int h = static_cast<int>(scr->Height);
    if (w <= 0 || h <= 0 || w > 1280 || h > 1024) return 0;

    /* Preserve 2:1 non-interlaced Amiga PAL/NTSC hires CRT aspect ratio (640x256 -> 640x512). */
    int vscale = (w >= 512 && h <= 300) ? 2 : 1;
    int out_h = h * vscale;

    int depth = 2;
    int ks = SysBase ? static_cast<int>(SysBase->LibNode.lib_Version) : 34;
    if (ks >= 39) {
        depth = static_cast<int>(GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH));
    } else if (scr->RastPort.BitMap) {
        depth = static_cast<int>(scr->RastPort.BitMap->Depth);
    }
    if (depth < 1) depth = 1;
    if (depth > 8) depth = 8;

    int num_colors = 1 << depth;
    if (num_colors > 256) num_colors = 256;

    unsigned char pal_r[256], pal_g[256], pal_b[256];
    memset(pal_r, 0, sizeof(pal_r));
    memset(pal_g, 0, sizeof(pal_g));
    memset(pal_b, 0, sizeof(pal_b));

    struct ColorMap* cm = scr->ViewPort.ColorMap;
    if (cm) {
        if (ks >= 39) {
            ULONG rgb32[256 * 3];
            int count = (num_colors <= static_cast<int>(cm->Count)) ? num_colors : static_cast<int>(cm->Count);
            if (count > 0 && count <= 256) {
                GetRGB32(cm, 0, static_cast<ULONG>(count), rgb32);
                for (int i = 0; i < count; ++i) {
                    pal_r[i] = static_cast<unsigned char>((rgb32[i * 3 + 0] >> 24) & 0xFFUL);
                    pal_g[i] = static_cast<unsigned char>((rgb32[i * 3 + 1] >> 24) & 0xFFUL);
                    pal_b[i] = static_cast<unsigned char>((rgb32[i * 3 + 2] >> 24) & 0xFFUL);
                }
            }
        } else {
            int count = (num_colors <= static_cast<int>(cm->Count)) ? num_colors : static_cast<int>(cm->Count);
            for (int i = 0; i < count; ++i) {
                LONG rgb4 = GetRGB4(cm, i);
                if (rgb4 >= 0) {
                    pal_r[i] = static_cast<unsigned char>(((rgb4 >> 8) & 0x0F) * 17);
                    pal_g[i] = static_cast<unsigned char>(((rgb4 >> 4) & 0x0F) * 17);
                    pal_b[i] = static_cast<unsigned char>((rgb4 & 0x0F) * 17);
                }
            }
        }
    }

    FILE* fp = fopen(path, "wb");
    if (!fp) return 0;

    unsigned long row_stride = static_cast<unsigned long>((w * 3 + 3) & ~3);
    unsigned long img_size = row_stride * static_cast<unsigned long>(out_h);
    unsigned char* row_buf = static_cast<unsigned char*>(malloc(row_stride));
    if (!row_buf) {
        fclose(fp);
        return 0;
    }

    unsigned char hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B';
    hdr[1] = 'M';
    ac_write_le32(hdr + 2, 54UL + img_size);
    ac_write_le32(hdr + 10, 54UL);
    ac_write_le32(hdr + 14, 40UL);
    ac_write_le32(hdr + 18, static_cast<unsigned long>(w));
    ac_write_le32(hdr + 22, static_cast<unsigned long>(out_h));
    ac_write_le16(hdr + 26, 1U);
    ac_write_le16(hdr + 28, 24U);
    ac_write_le32(hdr + 34, img_size);
    fwrite(hdr, 1, sizeof(hdr), fp);

    struct BitMap* bm = scr->RastPort.BitMap;
    int is_planar = (bm && bm->Planes[0] != NULL && (ks < 39 || (GetBitMapAttr(bm, BMA_FLAGS) & BMF_STANDARD) != 0)) ? 1 : 0;
    int bpr = bm ? static_cast<int>(bm->BytesPerRow) : 0;

    for (int y = h - 1; y >= 0; --y) {
        memset(row_buf, 0, row_stride);
        if (is_planar && bpr > 0) {
            long row_off = static_cast<long>(y) * static_cast<long>(bpr);
            for (int x = 0; x < w; ++x) {
                int byte_idx = x >> 3;
                unsigned char bit_mask = static_cast<unsigned char>(0x80U >> (x & 7));
                unsigned int pen = 0;
                for (int p = 0; p < depth && p < 8; ++p) {
                    if (bm->Planes[p] && (bm->Planes[p][row_off + byte_idx] & bit_mask) != 0) {
                        pen |= (1U << p);
                    }
                }
                if (pen >= 256U) pen &= 255U;
                row_buf[x * 3 + 0] = pal_b[pen];
                row_buf[x * 3 + 1] = pal_g[pen];
                row_buf[x * 3 + 2] = pal_r[pen];
            }
        } else {
            for (int x = 0; x < w; ++x) {
                ULONG pen = ReadPixel(&scr->RastPort, x, y);
                if (pen < 256UL) {
                    row_buf[x * 3 + 0] = pal_b[pen];
                    row_buf[x * 3 + 1] = pal_g[pen];
                    row_buf[x * 3 + 2] = pal_r[pen];
                }
            }
        }
        for (int rep = 0; rep < vscale; ++rep) {
            fwrite(row_buf, 1, row_stride, fp);
        }
    }

    free(row_buf);
    fclose(fp);
    return 1;
#else
    (void)path;
    (void)scr;
    return 0;
#endif
}

void oyk_amiga_finish_test() {
#ifdef __AMIGA__
    FILE* f = fopen("DH0:DONE.TXT", "w");
    if (!f) f = fopen("DONE.TXT", "w");
    if (f) {
        fprintf(f, "OYK_AMIGA_TEST_COMPLETE\n");
        fclose(f);
    }
#endif
}
