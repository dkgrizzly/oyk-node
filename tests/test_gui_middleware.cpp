/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_gui.hpp"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_menus_titles_tabs_and_tables() {
    int mc = oyk_gui_node_menu_count();
    assert(mc == 13);
    assert(oyk_gui_node_menu_item(0)->action == OYK_ACT_ALLOW_INCOMING);
    assert(oyk_gui_node_menu_item(1)->action == OYK_ACT_DENY_INCOMING);
    assert(oyk_gui_node_menu_item(2)->is_separator == 1);
    assert(oyk_gui_node_menu_item(3)->action == OYK_ACT_LOCAL_TERM);
    assert(oyk_gui_node_menu_item(6)->action == OYK_ACT_SETTINGS);
    assert(oyk_gui_node_menu_item(12)->action == OYK_ACT_EXIT);
    assert(oyk_gui_node_menu_item(13) == NULL);

    assert(strcmp(oyk_gui_screen_title(OYK_SCR_SETTINGS), "Node Settings") == 0);
    assert(strcmp(oyk_gui_screen_title(OYK_SCR_TERMINATE), "Terminate Active Connection") == 0);
    assert(strcmp(oyk_gui_screen_title(OYK_SCR_THEME), "Theme Settings") == 0);
    assert(strcmp(oyk_gui_screen_title(OYK_SCR_EXIT_PEERS), "Auto-Learned Peers") == 0);

    assert(oyk_gui_settings_tab_count() == 7);
    assert(strcmp(oyk_gui_settings_tab_title(0), "Node") == 0);
    assert(strcmp(oyk_gui_settings_tab_title(1), "BBS") == 0);
    assert(strcmp(oyk_gui_settings_tab_title(2), "CHAT") == 0);
    assert(strcmp(oyk_gui_settings_tab_title(3), "Interfaces") == 0);
    assert(strcmp(oyk_gui_settings_tab_title(4), "Peers") == 0);
    assert(strcmp(oyk_gui_settings_tab_title(5), "Rig") == 0);
    assert(strcmp(oyk_gui_settings_tab_title(6), "Printer") == 0);

    const char* tcols[4];
    assert(oyk_gui_terminate_columns(tcols, 4) == 4);
    assert(strcmp(tcols[0], "ID") == 0);
    assert(strcmp(tcols[1], "Callsign") == 0);
    assert(strcmp(tcols[2], "Port") == 0);
    assert(strcmp(tcols[3], "State") == 0);
    printf("[PASS] test_menus_titles_tabs_and_tables\n");
}

static void test_banners_exit_prompt_and_validation() {
    char buf[160];
    oyk_gui_format_console_banner("KR4OYK-7", "OYKNOD", 1, 2, 1, buf, sizeof(buf));
    assert(strstr(buf, "KR4OYK-7 (OYKNOD)") != NULL);
    assert(strstr(buf, "[INCOMING: ALLOW]") != NULL);
    assert(strstr(buf, "Conn: 2") != NULL);

    oyk_gui_format_console_header("KR4OYK-7", "OYKNOD", 3661UL, 1, 0, 1, buf, sizeof(buf));
    assert(strstr(buf, "1h 1m 1s") != NULL);
    assert(strstr(buf, "[IN:DENY]") != NULL);

    OykGuiExitPrompt ep;
    oyk_gui_build_exit_prompt(3, 2, &ep);
    assert(ep.has_auto_learned == 1);
    assert(strcmp(ep.title, "Auto-Learned Peers") == 0);
    assert(strstr(ep.body, "3 auto-learned") != NULL);

    oyk_gui_build_exit_prompt(0, 2, &ep);
    assert(ep.has_auto_learned == 0);
    assert(strstr(ep.body, "2 active") != NULL);

    OykPeerConfig pc;
    memset(&pc, 0, sizeof(pc));
    char err[96];
    assert(!oyk_gui_validate_peer_draft(&pc, err, sizeof(err)));
    strcpy(pc.id, "peer1");
    strcpy(pc.callsign, "W3TRM-7");
    assert(oyk_gui_validate_peer_draft(&pc, err, sizeof(err)));

    oyk_u16 p = 0;
    assert(oyk_gui_parse_port_str("10093", &p) && p == 10093);
    assert(!oyk_gui_parse_port_str("0", &p));
    assert(!oyk_gui_parse_port_str("70000", &p));
    printf("[PASS] test_banners_exit_prompt_and_validation\n");
}

static void test_add_iface_wizard_and_theme_ctrl() {
    OykConfig cfg;
    oyk_config_init_default(&cfg);

    OykGuiAddIfaceWizard w;
    oyk_gui_wiz_init(&w, &cfg, OYK_DRV_ALL);
    assert(w.step == 0);
    assert(oyk_gui_wiz_driver_count(OYK_DRV_ALL) == 5);
    assert(strcmp(w.draft.type, "axudp") == 0);
    assert(strcmp(w.draft.id, "axudp1") == 0);

    assert(oyk_gui_wiz_advance(&w, &cfg));
    assert(w.step == 1);
    strcpy(w.draft.id, "axudp0"); /* duplicate default iface */
    assert(!oyk_gui_wiz_advance(&w, &cfg));
    assert(strstr(w.error, "already exists") != NULL);

    strcpy(w.draft.id, "axudp1");
    assert(oyk_gui_wiz_advance(&w, &cfg));
    assert(w.step == 2);

    strcpy(w.draft.netmask, "bad.mask");
    assert(!oyk_gui_wiz_advance(&w, &cfg));
    strcpy(w.draft.netmask, "255.0.0.0");
    assert(oyk_gui_wiz_advance(&w, &cfg));
    assert(w.step == 3);

    char sum[512];
    oyk_gui_wiz_format_summary(&w, sum, sizeof(sum), 0);
    assert(strstr(sum, "axudp1") != NULL);
    assert(strstr(sum, "10093") != NULL);

    OykGuiThemeCtrl tc;
    oyk_gui_theme_init(&tc, &cfg, 0, 1);
    assert(strcmp(tc.active_name, "dark") == 0);
    char terr[96];
    assert(!oyk_gui_theme_set_role_hex(&tc, OYK_TR_ACCENT, "not-hex", terr, sizeof(terr)));
    assert(oyk_gui_theme_set_role_hex(&tc, OYK_TR_ACCENT, "#112233", terr, sizeof(terr)));
    assert(strncmp(tc.active_name, "custom-", 7) == 0);
    assert(cfg.theme_count == 1);

    OykThemeConfig pal;
    oyk_gui_theme_resolve(&tc, &pal);
    assert(pal.colors[OYK_TR_ACCENT] == 0x00112233UL);
    assert(oyk_gui_theme_delete_custom(&tc));
    assert(cfg.theme_count == 0);
    printf("[PASS] test_add_iface_wizard_and_theme_ctrl\n");
}

int main() {
    test_menus_titles_tabs_and_tables();
    test_banners_exit_prompt_and_validation();
    test_add_iface_wizard_and_theme_ctrl();
    printf("All oyk_gui middleware tests passed!\n");
    return 0;
}
