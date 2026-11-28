/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */
#include "ti99_core.h"
#include "ti99_rs232.h"
#include "ti99_sams.h"
#include "ti99_tipi.h"
#include "ti99_tui.h"
#include "ti99_vdp.h"

#include <stdio.h>
#include <string.h>

static int run_unit_tests(void)
{
    Ti99App app;
    Ti99SamsState sams;
    Ti99VdpState vdp;
    Ti99TipiState tipi;
    Ti99Rs232State rs;
    Ti99KissDecoder dec;
    uint8_t ax[64];
    uint8_t kiss[80];
    uint8_t out[64];
    size_t ax_len;
    size_t k_len;
    uint8_t page_wr[16];
    uint8_t page_rd[16];
    const char *screens[23] = {
        "main", "menu", "settings", "settings_bbs", "settings_chat",
        "settings_ifaces", "peers", "settings_rig", "settings_printer",
        "add_iface_1", "add_iface_2", "add_iface_3", "add_iface_4",
        "edit_iface", "edit_peer", "terminate", "ban", "exit",
        "theme", "terminal", "terminal_help", "files", "about"
    };
    int i;

    ti99_app_init(&app, 1);
    if (ti99_sams_probe(&sams) != 0) return 1;
    memset(page_wr, 0xA5, sizeof(page_wr));
    if (ti99_sams_write_bank(&sams, 42, 0, page_wr, sizeof(page_wr)) != 0) return 2;
    memset(page_rd, 0, sizeof(page_rd));
    if (ti99_sams_read_bank(&sams, 42, 0, page_rd, sizeof(page_rd)) != 0) return 3;
    if (memcmp(page_wr, page_rd, sizeof(page_wr)) != 0) return 4;

    ax_len = ti99_ax25_encode_ui("KR4OYK-7", "CQ", (const uint8_t *)"TI99", 4, ax, sizeof(ax));
    if (ax_len == 0) return 5;
    k_len = ti99_kiss_encode(ax, ax_len, 0, kiss, sizeof(kiss));
    ti99_kiss_init(&dec);
    for (i = 0; i < (int)k_len; ++i) {
        size_t got = ti99_kiss_feed(&dec, kiss[i], out, sizeof(out));
        if (i + 1 == (int)k_len && got != ax_len) return 6;
    }

    if (ti99_tipi_probe(&tipi) != 0) return 7;
    if (ti99_tipi_open_udp(&tipi, "127.0.0.1", 10093) != 0) return 8;
    if (ti99_tipi_send(&tipi, ax, ax_len) != (int)ax_len) return 9;
    if (ti99_tipi_recv(&tipi, out, sizeof(out)) != (int)ax_len) return 10;
    ti99_tipi_close(&tipi);

    if (ti99_rs232_init(&rs, 9600) != 0) return 11;
    if (ti99_rs232_write_byte(&rs, 0xC0) != 0) return 12;
    {
        uint8_t b = 0;
        if (ti99_rs232_read_byte(&rs, &b) != 1 || b != 0xC0) return 13;
    }
    ti99_rs232_close(&rs);

    /* Check F18A 80-col and TMS9918A 40-col fallback across all 23 canonical screens */
    for (int f18a = 1; f18a >= 0; --f18a) {
        ti99_vdp_init(&vdp, f18a);
        for (i = 0; i < 23; ++i) {
            if (ti99_app_set_screen_by_name(&app, screens[i]) != 0) return 14;
            ti99_tui_render(&vdp, &app);
            if (i == 22 && !ti99_vdp_find_text(&vdp, "O Y K")) return 15;
        }
    }

    /* Check hotkey discipline: closed-menu single-letter suppression, Ctrl-N, modal preemption, Esc */
    ti99_app_init(&app, 0);
    if (ti99_app_handle_key(&app, 's') == 0 || ti99_app_handle_key(&app, 'm') == 0 ||
        ti99_app_handle_key(&app, 'q') == 0 || app.screen != TI99_SCR_MAIN) return 16;
    if (ti99_app_handle_key(&app, TI99_KEY_CTRL_N) != 0 || app.screen != TI99_SCR_MENU) return 17;
    if (ti99_app_handle_key(&app, 'c') == 0 || app.screen != TI99_SCR_MENU) return 18;
    if (ti99_app_handle_key(&app, 's') != 0 || app.screen != TI99_SCR_SETTINGS) return 19;
    if (ti99_app_handle_key(&app, TI99_KEY_CTRL_N) == 0 ||
        ti99_app_handle_key(&app, TI99_KEY_F10) == 0 ||
        ti99_app_handle_key(&app, TI99_KEY_F9) == 0 ||
        app.screen != TI99_SCR_SETTINGS) return 20;
    if (ti99_app_handle_key(&app, '8') != 0 || app.screen != TI99_SCR_THEME || app.settings_tab != 7) return 21;
    if (ti99_app_handle_key(&app, TI99_KEY_ESC) != 0 || app.screen != TI99_SCR_MAIN) return 22;

    ti99_sams_shutdown(&sams);
    return 0;
}

int main(int argc, char **argv)
{
    Ti99App app;
    Ti99VdpState vdp;
    int i;
    int unit_test = 0;
    int headless = 0;
    int col40 = 0;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--unit-test") == 0) unit_test = 1;
        else if (strcmp(argv[i], "--headless") == 0) headless = 1;
        else if (strcmp(argv[i], "--40col") == 0) col40 = 1;
    }
    if (unit_test) {
        int rc = run_unit_tests();
        if (rc == 0) {
            printf("OYK TI-99/4A self-test PASSED\n");
        }
        return rc;
    }

    ti99_app_init(&app, 1);
    ti99_vdp_init(&vdp, col40 ? 0 : 1);
    ti99_tui_render(&vdp, &app);
    if (!headless) {
        int r;
        for (r = 0; r < (int)vdp.rows; ++r) {
            printf("%.*s\n", (int)vdp.cols, (const char *)vdp.screen[r]);
        }
    }
    return 0;
}
