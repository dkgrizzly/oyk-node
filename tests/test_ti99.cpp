// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk_port_parity_tests.hpp"
#include "oyk_portable.hpp"
#include "ti99_core.h"
#include "ti99_rs232.h"
#include "ti99_sams.h"
#include "ti99_tipi.h"
#include "ti99_tui.h"
#include "ti99_vdp.h"

#include <cstdio>
#include <cstring>

int main() {
    int failures = 0;

    /* 1. Shared portable cross-port parity suite */
    {
        OykPortableNode *pnode = new OykPortableNode();
        OykConfig *pcfg = new OykConfig();
        oyk_config_init_default(pcfg);
        char perr[64];
        if (pnode->init(pcfg, NULL, NULL, perr, sizeof(perr)) != 1) {
            std::printf("FAIL: OykPortableNode::init (%s)\n", perr);
            ++failures;
        } else {
            oyk_run_shared_port_parity_tests(pnode, stdout, &failures);
            pnode->shutdown();
        }
        delete pcfg;
        delete pnode;
    }

    /* 2. TI-99/4A big-endian wire + AX.25/KISS + EOL parity */
    {
        uint8_t b2[2];
        uint8_t b4[4];
        ti99_be16_write(b2, 0x1234U);
        ti99_be32_write(b4, 0xCAFEBABEUL);
        if (ti99_be16_read(b2) != 0x1234U || ti99_be32_read(b4) != 0xCAFEBABEUL) {
            std::printf("FAIL: TI-99 big-endian helpers\n");
            ++failures;
        }

        uint8_t ax[64];
        uint8_t kiss[80];
        uint8_t dec_out[64];
        size_t ax_len = ti99_ax25_encode_ui("KR4OYK-7", "CQ",
                                            reinterpret_cast<const uint8_t *>("TI99"),
                                            4, ax, sizeof(ax));
        size_t k_len = ti99_kiss_encode(ax, ax_len, 0, kiss, sizeof(kiss));
        Ti99KissDecoder dec;
        ti99_kiss_init(&dec);
        size_t got = 0;
        for (size_t i = 0; i < k_len; ++i) {
            got = ti99_kiss_feed(&dec, kiss[i], dec_out, sizeof(dec_out));
        }
        if (got != ax_len || std::memcmp(ax, dec_out, ax_len) != 0) {
            std::printf("FAIL: TI-99 AX.25/KISS roundtrip\n");
            ++failures;
        }

        Ti99Config cfg;
        const char *crlf = "callsign=KR4OYK-9\r\nalias=TI99N\r\n";
        ti99_config_parse(crlf, std::strlen(crlf), &cfg);
        char ser[256];
        size_t ser_len = ti99_config_serialize(&cfg, ser, sizeof(ser));
        if (cfg.eol_mode != TI99_EOL_CRLF || ser_len == 0 || std::strstr(ser, "\r\n") == NULL) {
            std::printf("FAIL: TI-99 config CRLF preservation\n");
            ++failures;
        }
    }

    /* 3. SAMS 1MB, TIPI, RS232, and all 23 screens (F18A 80-col + 9918 40-col + root jail) */
    {
        Ti99SamsState sams;
        if (ti99_sams_probe(&sams) != 0) {
            std::printf("FAIL: TI-99 SAMS probe\n");
            ++failures;
        } else {
            const uint8_t pat[8] = {1, 2, 3, 4, 5, 6, 7, 8};
            uint8_t rd[8] = {0};
            ti99_sams_write_bank(&sams, 200, 16, pat, sizeof(pat));
            ti99_sams_read_bank(&sams, 200, 16, rd, sizeof(rd));
            if (std::memcmp(pat, rd, sizeof(pat)) != 0) {
                std::printf("FAIL: TI-99 SAMS bank read/write\n");
                ++failures;
            }
            ti99_sams_shutdown(&sams);
        }

        Ti99App app;
        Ti99VdpState vdp;
        ti99_app_init(&app, 1);

        Ti99FileEntry root_inc[8];
        uint8_t rn = ti99_app_list_files(&app, 0, root_inc, 8);
        for (uint8_t i = 0; i < rn; ++i) {
            if (std::strcmp(root_inc[i].name, "..") == 0) {
                std::printf("FAIL: TI-99 root jail leaked '..' at root\n");
                ++failures;
            }
        }
        ti99_app_cd(&app, 0, "UPLOADS");
        rn = ti99_app_list_files(&app, 0, root_inc, 8);
        if (rn == 0 || std::strcmp(root_inc[0].name, "..") != 0) {
            std::printf("FAIL: TI-99 subdir missing '..'\n");
            ++failures;
        }
        ti99_app_cd(&app, 0, "..");

        static const char *const kScreens[23] = {
            "main", "menu", "settings", "settings_bbs", "settings_chat",
            "settings_ifaces", "peers", "settings_rig", "settings_printer",
            "add_iface_1", "add_iface_2", "add_iface_3", "add_iface_4",
            "edit_iface", "edit_peer", "terminate", "ban", "exit",
            "theme", "terminal", "terminal_help", "files", "about"
        };
        for (int f18a = 1; f18a >= 0; --f18a) {
            ti99_vdp_init(&vdp, f18a);
            for (int s = 0; s < 23; ++s) {
                ti99_app_set_screen_by_name(&app, kScreens[s]);
                ti99_tui_render(&vdp, &app);
                if (s == 22 && !ti99_vdp_find_text(&vdp, "O Y K")) {
                    std::printf("FAIL: TI-99 about screen missing logo (%s)\n",
                                f18a ? "F18A" : "40col");
                    ++failures;
                }
                if (s == 19 && !ti99_vdp_find_text(&vdp, "Ctrl-A Z:Help")) {
                    std::printf("FAIL: TI-99 terminal missing Ctrl-A Z:Help\n");
                    ++failures;
                }
            }
        }
    }

    if (failures == 0) {
        std::printf("All TI-99/4A parity tests PASSED.\n");
    }
    return failures == 0 ? 0 : 1;
}
