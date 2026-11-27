/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "atari_capture.hpp"
#include "atari_policy.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ATARI__
#include <gem.h>
#include <osbind.h>
#endif

static void at_write_le16(unsigned char* p, unsigned short v) {
    p[0] = static_cast<unsigned char>(v & 0xFFU);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFFU);
}

static void at_write_le32(unsigned char* p, unsigned long v) {
    p[0] = static_cast<unsigned char>(v & 0xFFUL);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFFUL);
    p[2] = static_cast<unsigned char>((v >> 16) & 0xFFUL);
    p[3] = static_cast<unsigned char>((v >> 24) & 0xFFUL);
}

int oyk_atari_is_ui_test_mode() {
#ifdef __ATARI__
    FILE* f = fopen("C:\\UITEST.FLG", "r");
    if (!f) f = fopen("UITEST.FLG", "r");
    if (f) {
        fclose(f);
        return 1;
    }
#endif
    return 0;
}

int oyk_atari_capture_screen_bmp(const char* path, short vdi_handle, int w, int h, int colors) {
#ifdef __ATARI__
    if (!path || !path[0] || vdi_handle <= 0 || w <= 0 || h <= 0) return 0;

    int planes = oyk_atari_planes_from_colors(colors);
    unsigned char pal_r[16], pal_g[16], pal_b[16];
    memset(pal_r, 0, sizeof(pal_r));
    memset(pal_g, 0, sizeof(pal_g));
    memset(pal_b, 0, sizeof(pal_b));

    int pcnt = (planes <= 1) ? 2 : ((planes == 2) ? 4 : 16);
    for (int i = 0; i < pcnt; ++i) {
        short rgb[3] = {0, 0, 0};
        vq_color(vdi_handle, static_cast<short>(i), 0, rgb);
        oyk_u32 c24 = oyk_atari_vdi_to_rgb24(rgb[0], rgb[1], rgb[2]);
        pal_r[i] = static_cast<unsigned char>((c24 >> 16) & 0xFFUL);
        pal_g[i] = static_cast<unsigned char>((c24 >> 8) & 0xFFUL);
        pal_b[i] = static_cast<unsigned char>(c24 & 0xFFUL);
    }
    if (planes <= 1 &&
        pal_r[0] == pal_r[1] && pal_g[0] == pal_g[1] && pal_b[0] == pal_b[1]) {
        /* EmuTOS ST High reports pen 1 as 1000,1000,1000; bit=1 is foreground ink. */
        pal_r[1] = static_cast<unsigned char>(255U - pal_r[0]);
        pal_g[1] = static_cast<unsigned char>(255U - pal_g[0]);
        pal_b[1] = static_cast<unsigned char>(255U - pal_b[0]);
    }

    const unsigned char* phys = reinterpret_cast<const unsigned char*>(Physbase());
    if (!phys) return 0;

    FILE* fp = fopen(path, "wb");
    if (!fp) return 0;

    unsigned long row_stride = static_cast<unsigned long>((w * 3 + 3) & ~3);
    unsigned long img_size = row_stride * static_cast<unsigned long>(h);
    unsigned char* row_buf = static_cast<unsigned char*>(malloc(row_stride));
    if (!row_buf) {
        fclose(fp);
        return 0;
    }

    unsigned char hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B';
    hdr[1] = 'M';
    at_write_le32(hdr + 2, 54UL + img_size);
    at_write_le32(hdr + 10, 54UL);
    at_write_le32(hdr + 14, 40UL);
    at_write_le32(hdr + 18, static_cast<unsigned long>(w));
    at_write_le32(hdr + 22, static_cast<unsigned long>(h));
    at_write_le16(hdr + 26, 1U);
    at_write_le16(hdr + 28, 24U);
    at_write_le32(hdr + 34, img_size);
    fwrite(hdr, 1, sizeof(hdr), fp);

    /* Atari ST / TT / VDI screen layout: interleaved 16-bit big-endian bitplane groups. */
    int words_per_row = ((w + 15) / 16) * planes;
    int bytes_per_row = words_per_row * 2;
    for (int y = h - 1; y >= 0; --y) {
        const unsigned char* src = phys + static_cast<long>(y) * static_cast<long>(bytes_per_row);
        memset(row_buf, 0, row_stride);
        for (int x = 0; x < w; ++x) {
            int chunk = x >> 4;
            int bit = 15 - (x & 15);
            int hw_idx = 0;
            for (int p = 0; p < planes; ++p) {
                const unsigned char* wp = src + (chunk * planes + p) * 2;
                unsigned short word = static_cast<unsigned short>((static_cast<unsigned short>(wp[0]) << 8) | wp[1]);
                if ((word & (1U << bit)) != 0U) {
                    hw_idx |= (1 << p);
                }
            }
            int vdi_idx = oyk_atari_hw_to_vdi_idx(hw_idx, planes);
            row_buf[x * 3 + 0] = pal_b[vdi_idx];
            row_buf[x * 3 + 1] = pal_g[vdi_idx];
            row_buf[x * 3 + 2] = pal_r[vdi_idx];
        }
        fwrite(row_buf, 1, row_stride, fp);
    }

    free(row_buf);
    fclose(fp);
    return 1;
#else
    (void)path; (void)vdi_handle; (void)w; (void)h; (void)colors;
    return 0;
#endif
}

void oyk_atari_finish_test() {
#ifdef __ATARI__
    FILE* f = fopen("C:\\DONE.TXT", "w");
    if (!f) f = fopen("DONE.TXT", "w");
    if (f) {
        fprintf(f, "OYK_ATARI_TEST_COMPLETE\n");
        fclose(f);
    }
#endif
}
