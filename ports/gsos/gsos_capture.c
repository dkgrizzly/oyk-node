/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "gsos_capture.h"

#include <string.h>

#ifdef __VBCC__
#include <OS/Types.h>
#include <OS/GSOS.h>

static GSString255 g_path_buf;
static GSString32  g_vol_out = { 7, "/OYKOUT" };
static CreateRecGS g_cr;
static OpenRecGS   g_op;
static IORecGS     g_io;
static RefNumRecGS g_cl;
static NameRecGS   g_ds;
static VolumeRecGS g_fl;
static unsigned char g_row_buf[3840]; /* Two 640 * 3 byte rows for fast 2x BMP output */

static void gs_fill_path(const char *p) {
    size_t n = p ? strlen(p) : 0U;
    if (n > 250U) n = 250U;
    g_path_buf.length = (Word)n;
    if (n > 0U) memcpy(g_path_buf.text, p, n);
}

static void gs_write_le16(unsigned char *p, unsigned short v) {
    p[0] = (unsigned char)(v & 0xFFU);
    p[1] = (unsigned char)((v >> 8) & 0xFFU);
}

static void gs_write_le32(unsigned char *p, unsigned long v) {
    p[0] = (unsigned char)(v & 0xFFUL);
    p[1] = (unsigned char)((v >> 8) & 0xFFUL);
    p[2] = (unsigned char)((v >> 16) & 0xFFUL);
    p[3] = (unsigned char)((v >> 24) & 0xFFUL);
}
#endif

int oyk_gsos_is_ui_test_mode(void) {
#ifdef __VBCC__
    gs_fill_path("/OYKOUT/UITEST.FLG");
    g_op.pCount = 3;
    g_op.pathname = &g_path_buf;
    g_op.requestAccess = 1;
    g_op.refNum = 0;
    OpenGS(&g_op);
    if (_toolErr == 0 && g_op.refNum != 0) {
        g_cl.pCount = 1;
        g_cl.refNum = g_op.refNum;
        CloseGS(&g_cl);
        return 1;
    }
    gs_fill_path("*:UITEST.FLG");
    g_op.pCount = 3;
    g_op.pathname = &g_path_buf;
    g_op.requestAccess = 1;
    g_op.refNum = 0;
    OpenGS(&g_op);
    if (_toolErr == 0 && g_op.refNum != 0) {
        g_cl.pCount = 1;
        g_cl.refNum = g_op.refNum;
        CloseGS(&g_cl);
        return 1;
    }
    return 0;
#else
    return 0;
#endif
}

int oyk_gsos_write_text_file(const char *gsos_path, const char *data, unsigned long len) {
#ifdef __VBCC__
    if (!gsos_path || !data) return 0;
    gs_fill_path(gsos_path);
    g_ds.pCount = 1;
    g_ds.pathname = &g_path_buf;
    DestroyGS(&g_ds);

    g_cr.pCount = 4;
    g_cr.pathname = &g_path_buf;
    g_cr.access = 0xE3;
    g_cr.fileType = 0x04;
    g_cr.auxType = 0UL;
    CreateGS(&g_cr);
    if (_toolErr != 0) return 0;

    g_op.pCount = 3;
    g_op.pathname = &g_path_buf;
    g_op.requestAccess = 2;
    g_op.refNum = 0;
    OpenGS(&g_op);
    if (_toolErr != 0 || g_op.refNum == 0) return 0;

    g_io.pCount = 4;
    g_io.refNum = g_op.refNum;
    g_io.dataBuffer = (Pointer)data;
    g_io.requestCount = len;
    WriteGS(&g_io);

    g_cl.pCount = 1;
    g_cl.refNum = g_op.refNum;
    CloseGS(&g_cl);

    g_fl.pCount = 1;
    g_fl.devName = &g_vol_out;
    FlushGS(&g_fl);
    return 1;
#else
    (void)gsos_path;
    (void)data;
    (void)len;
    return 0;
#endif
}

int oyk_gsos_capture_screen_bmp(const char *gsos_path) {
#ifdef __VBCC__
    /* Capture Apple IIGS Super Hi-Res 640x200 RAM as aspect-corrected 640x400 24-bit BMP. */
    const unsigned char *vram = (const unsigned char *)0xE12000UL;
    const unsigned char *scbs = (const unsigned char *)0xE19D00UL;
    const unsigned char *pals = (const unsigned char *)0xE19E00UL;
    unsigned char hdr[54];
    int y, b;
    unsigned long row_stride = 1920UL;
    unsigned long img_size = row_stride * 400UL;

    if (!gsos_path || !gsos_path[0]) return 0;
    gs_fill_path(gsos_path);
    g_ds.pCount = 1;
    g_ds.pathname = &g_path_buf;
    DestroyGS(&g_ds);

    g_cr.pCount = 4;
    g_cr.pathname = &g_path_buf;
    g_cr.access = 0xE3;
    g_cr.fileType = 0x06;
    g_cr.auxType = 0UL;
    CreateGS(&g_cr);
    if (_toolErr != 0) return 0;

    g_op.pCount = 3;
    g_op.pathname = &g_path_buf;
    g_op.requestAccess = 2;
    g_op.refNum = 0;
    OpenGS(&g_op);
    if (_toolErr != 0 || g_op.refNum == 0) return 0;

    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B';
    hdr[1] = 'M';
    gs_write_le32(hdr + 2, 54UL + img_size);
    gs_write_le32(hdr + 10, 54UL);
    gs_write_le32(hdr + 14, 40UL);
    gs_write_le32(hdr + 18, 640UL);
    gs_write_le32(hdr + 22, 400UL);
    gs_write_le16(hdr + 26, 1U);
    gs_write_le16(hdr + 28, 24U);
    gs_write_le32(hdr + 34, img_size);

    g_io.pCount = 4;
    g_io.refNum = g_op.refNum;
    g_io.dataBuffer = (Pointer)hdr;
    g_io.requestCount = 54UL;
    WriteGS(&g_io);

    /* Bottom-up BMP: repeat each SHR scanline twice to preserve 2:1 IIgs CRT aspect ratio. */
    for (y = 199; y >= 0; --y) {
        const unsigned char *line = vram + (unsigned long)y * 160UL;
        unsigned int pal_id = (unsigned int)(scbs[y] & 0x0FU);
        const unsigned char *pal = pals + (unsigned long)pal_id * 32UL;
        unsigned char *dst = g_row_buf;
        for (b = 0; b < 160; ++b) {
            unsigned char byte = line[b];
            int sub;
            static const unsigned int quad_base[4] = { 8U, 12U, 0U, 4U };
            for (sub = 0; sub < 4; ++sub) {
                unsigned int px = (unsigned int)((byte >> ((3 - sub) * 2)) & 0x03U);
                unsigned int ci = quad_base[sub] + px;
                unsigned int lo = pal[ci * 2U];
                unsigned int hi = pal[ci * 2U + 1U];
                unsigned int r4 = hi & 0x0FU;
                unsigned int g4 = (lo >> 4) & 0x0FU;
                unsigned int b4 = lo & 0x0FU;
                *dst++ = (unsigned char)(b4 * 17U);
                *dst++ = (unsigned char)(g4 * 17U);
                *dst++ = (unsigned char)(r4 * 17U);
            }
        }
        memcpy(g_row_buf + row_stride, g_row_buf, row_stride);
        g_io.dataBuffer = (Pointer)g_row_buf;
        g_io.requestCount = row_stride * 2UL;
        WriteGS(&g_io);
    }

    g_cl.pCount = 1;
    g_cl.refNum = g_op.refNum;
    CloseGS(&g_cl);

    g_fl.pCount = 1;
    g_fl.devName = &g_vol_out;
    FlushGS(&g_fl);
    return 1;
#else
    (void)gsos_path;
    return 0;
#endif
}

void oyk_gsos_finish_test(void) {
    static const char marker[] = "OYK_GSOS_TEST_COMPLETE\n";
    oyk_gsos_write_text_file("/OYKOUT/DONE.TXT", marker,
                             (unsigned long)(sizeof(marker) - 1U));
}
