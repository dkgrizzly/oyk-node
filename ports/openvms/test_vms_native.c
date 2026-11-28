/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "vms_compat.h"
#include "sysv_core.h"
#include "sysv_tui.h"
#include "sysv_net.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    OykSysvApp app;
    OykSysvCanvas cv;
    sysv_u8 b2[2];
    sysv_u8 b4[4];
    sysv_u8 ax[64];
    sysv_u8 kiss[96];
    OykSysvFrame frm;
    sysv_u8 kport = 0;
    sysv_u8 kcmd = 0;
    size_t ax_len;
    size_t k_len;
    OykSysvKissDecoder kd;
    const char *screens[23] = {
        "main", "menu", "settings", "settings_bbs", "settings_chat",
        "settings_ifaces", "peers", "settings_rig", "settings_printer",
        "add_iface_1", "add_iface_2", "add_iface_3", "add_iface_4",
        "edit_iface", "edit_peer", "terminate", "ban", "exit",
        "theme", "terminal", "terminal_help", "files", "about"
    };
    int i;

    oyk_sysv_be16_write(b2, 0x4567U);
    oyk_sysv_be32_write(b4, 0x89ABCDEFUL);
    if (oyk_sysv_be16_read(b2) != 0x4567U || oyk_sysv_be32_read(b4) != 0x89ABCDEFUL) {
        return 1;
    }

    oyk_sysv_app_init(&app, OYK_VMS_ARCH_NAME, 1);
    memset(&frm, 0, sizeof(frm));
    oyk_sysv_parse_callsign("KR4OYK-7", &frm.src);
    oyk_sysv_parse_callsign("CQ", &frm.dst);
    frm.control = 0x03U;
    frm.pid = 0xF0U;
    memcpy(frm.info, "VMS", 3U);
    frm.info_len = 3U;
    ax_len = oyk_sysv_ax25_encode(&frm, ax, sizeof(ax));
    if (ax_len == 0) {
        return 2;
    }
    k_len = oyk_sysv_kiss_encode(0, ax, ax_len, kiss, sizeof(kiss));
    oyk_sysv_kiss_init(&kd);
    for (i = 0; i < (int)k_len; ++i) {
        int got = oyk_sysv_kiss_feed(&kd, kiss[i], &kport, &kcmd);
        if (i + 1 == (int)k_len && (got != 1 || kd.len != ax_len || memcmp(ax, kd.buf, ax_len) != 0)) {
            return 3;
        }
    }

    for (i = 0; i < 23; ++i) {
        if (!oyk_sysv_tui_setup_screen(&app, screens[i])) {
            return 4;
        }
        oyk_sysv_tui_render(&cv, &app, 24, 80);
        if (i == 22 && !oyk_sysv_canvas_find_text(&cv, "O Y K")) {
            return 5;
        }
    }

    printf("Native OpenVMS core & 23-screen suite PASSED (%s).\n", OYK_VMS_ARCH_NAME);
    return 0;
}
