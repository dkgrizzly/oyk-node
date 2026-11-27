/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include <stdio.h>
#include <string.h>

#include "gsos_core.h"
#include "gsos_net.h"
#include "gsos_ui.h"

static int g_fail = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
            ++g_fail; \
        } \
    } while (0)

int main() {
    OykGsosConfig cfg;
    OykGsosNode node;
    OykGsosNetState net;
    OykGsosCallsign cs;
    OykGsosAddIfaceWiz wiz;
    OykGsosThemeConfig pal;
    OykGsosTermSpan spans[4];
    OykGsosTermEolParser eolp;
    char buf[512];
    char out_eol[64];
    int bg_ch = 0, fg_ch = 0;

    CHECK(oyk_gsos_node_menu_count() == 13);
    CHECK(oyk_gsos_settings_tab_count() == 7);
    CHECK(strcmp(oyk_gsos_table_column(OYK_GSOS_TBL_TERMINATE, 0), "ID") == 0);
    CHECK(strcmp(oyk_gsos_table_column(OYK_GSOS_TBL_TERMINATE, 1), "Callsign") == 0);
    CHECK(strcmp(oyk_gsos_table_column(OYK_GSOS_TBL_TERMINATE, 2), "Port") == 0);
    CHECK(strcmp(oyk_gsos_table_column(OYK_GSOS_TBL_TERMINATE, 3), "State") == 0);

    CHECK(oyk_gsos_parse_callsign("KR4OYK-7", &cs) == 1);
    CHECK(strcmp(cs.base, "KR4OYK") == 0 && cs.ssid == 7);
    oyk_gsos_format_callsign(&cs, buf, sizeof(buf));
    CHECK(strcmp(buf, "KR4OYK-7") == 0);

    oyk_gsos_config_init_default(&cfg);
    CHECK(cfg.accept_incoming == 1);
    CHECK(cfg.line_ending == OYK_GSOS_EOL_CR);
    CHECK(strcmp(cfg.active_theme, "native") == 0);

    /* Config CR/CRLF/LF detection & preservation */
    const char* cr_cfg = "[node]\rcallsign = W4ABC-7\ralias = TEST\raccept_incoming = false\rtheme = dark\r";
    CHECK(oyk_gsos_config_parse(cr_cfg, strlen(cr_cfg), &cfg) == 1);
    CHECK(cfg.line_ending == OYK_GSOS_EOL_CR);
    CHECK(cfg.accept_incoming == 0);
    CHECK(strcmp(cfg.active_theme, "dark") == 0);
    size_t slen = oyk_gsos_config_serialize(&cfg, buf, sizeof(buf));
    CHECK(slen > 0U && strchr(buf, '\r') != NULL && strchr(buf, '\n') == NULL);

    /* Host headless whole-screen theme preview state transitions */
    oyk_gsos_config_init_default(&cfg);
    oyk_gsos_node_init(&node, &cfg);
    oyk_gsos_net_init(&net, &node);
    oyk_gsos_query_os_native_theme(&pal);
    CHECK(strcmp(pal.name, "native") == 0 && pal.is_dark_base == 0);
    CHECK(oyk_gsos_ui_run_theme_preview_self_test(&node, &net, &bg_ch, &fg_ch) == 1);
    CHECK(bg_ch >= 4 && fg_ch >= 4);

    /* Wizard + console header + theme cycle + terminal helpers */
    oyk_gsos_wiz_init(&wiz, &cfg);
    CHECK(oyk_gsos_wiz_driver_count() == 5);
    for (int di = 0; di < 5; ++di) {
        CHECK(oyk_gsos_wiz_driver_name(di)[0] != '\0');
        CHECK(oyk_gsos_wiz_driver_desc(di)[0] != '\0');
    }
    oyk_gsos_wiz_select_driver(&wiz, &cfg, 2);
    CHECK(strcmp(wiz.draft.type, "kiss-serial") == 0);
    CHECK(wiz.step == 0);
    CHECK(oyk_gsos_wiz_advance(&wiz, &cfg) == 1 && wiz.step == 1);
    CHECK(oyk_gsos_wiz_advance(&wiz, &cfg) == 1 && wiz.step == 2);
    CHECK(oyk_gsos_wiz_advance(&wiz, &cfg) == 1 && wiz.step == 3);
    oyk_gsos_wiz_format_summary(&wiz, buf, sizeof(buf));
    CHECK(strstr(buf, "kiss-serial") != NULL);
    CHECK(strcmp(oyk_gsos_format_learned_policy(0), "inherit") == 0);
    CHECK(oyk_gsos_cycle_learned_policy(0, 1) == 1);
    oyk_gsos_format_console_header("KR4OYK-7", "OYKNOD", 1, buf, sizeof(buf));
    CHECK(strstr(buf, "OYK PACKET NODE:") != NULL);
    CHECK(strcmp(oyk_gsos_format_incoming_pill(1), "[INCOMING: ALLOW]") == 0);
    CHECK(strcmp(oyk_gsos_format_incoming_pill(0), "[INCOMING: DENY]") == 0);
    OykGsosThemeCtrl tctrl;
    oyk_gsos_theme_init(&tctrl, &cfg, 1, 0);
    oyk_gsos_theme_cycle(&tctrl, 1);
    CHECK(strcmp(tctrl.active_name, "native") != 0);
    oyk_gsos_theme_revert(&tctrl);
    CHECK(strcmp(tctrl.active_name, "native") == 0);

    CHECK(oyk_gsos_term_decode_ctrl_a('?') == OYK_GSOS_TCMD_HELP);
    CHECK(oyk_gsos_term_decode_ctrl_a('s') == OYK_GSOS_TCMD_UPLOAD);
    CHECK(oyk_gsos_term_wrap_line("HELLO APPLE IIGS GSOS", 21, 12, spans, 4) >= 2);
    memset(&eolp, 0, sizeof(eolp));
    CHECK(oyk_gsos_term_eol_feed(&eolp, "A\r\n", 3, out_eol, sizeof(out_eol)) == 2);
    CHECK(eolp.detected == OYK_GSOS_EOL_CRLF);

    printf("oyk_gsos_tests: failures=%d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
