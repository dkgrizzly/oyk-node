/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_gui.hpp"
#include "oyk_tui.hpp"
#include "oyk_term.hpp"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

static void test_menus_titles_tabs_and_tables() {
    int mc = oyk_gui_node_menu_count();
    assert(mc == 11);
    assert(oyk_gui_node_menu_item(0)->action == OYK_ACT_ALLOW_INCOMING);
    assert(oyk_gui_node_menu_item(1)->action == OYK_ACT_DENY_INCOMING);
    assert(oyk_gui_node_menu_item(2)->is_separator == 1);
    assert(oyk_gui_node_menu_item(3)->action == OYK_ACT_LOCAL_TERM);
    assert(oyk_gui_node_menu_item(6)->action == OYK_ACT_SETTINGS);
    assert(oyk_gui_node_menu_item(10)->action == OYK_ACT_EXIT);
    assert(oyk_gui_node_menu_item(11) == NULL);
    for (int i = 0; i < mc; ++i) {
        assert(oyk_gui_node_menu_item(i)->action != OYK_ACT_PEERS);
        assert(oyk_gui_node_menu_item(i)->action != OYK_ACT_THEME);
    }

    assert(oyk_gui_global_hotkey_allowed(OYK_SCR_MAIN, 0) == 1);
    assert(oyk_gui_global_hotkey_allowed(OYK_SCR_MAIN, 1) == 0);
    assert(oyk_gui_global_hotkey_allowed(OYK_SCR_SETTINGS, 0) == 0);
    assert(oyk_gui_global_hotkey_allowed(OYK_SCR_FILES, 0) == 0);
    assert(oyk_gui_global_hotkey_allowed(OYK_SCR_TERM_HELP, 0) == 0);

    assert(strcmp(oyk_gui_screen_title(OYK_SCR_SETTINGS), "Node Settings") == 0);
    assert(strcmp(oyk_gui_screen_title(OYK_SCR_TERMINATE), "Terminate Active Connection") == 0);
    assert(strcmp(oyk_gui_screen_title(OYK_SCR_THEME), "Theme Settings") == 0);
    assert(strcmp(oyk_gui_screen_title(OYK_SCR_EXIT_PEERS), "Auto-Learned Peers") == 0);
    assert(strcmp(oyk_gui_screen_title(OYK_SCR_MENU), "Node Menu") == 0);
    assert(strcmp(oyk_gui_screen_title(OYK_SCR_TERM_HELP), "Terminal Hotkey Help") == 0);
    assert(oyk_gui_vertical_text_offset(16, 8) == 4);
    assert(oyk_gui_vertical_text_offset(22, 16) == 3);
    assert(oyk_gui_control_height(8, 3) == 14);
    assert(strcmp(oyk_gui_format_learned_policy(OYK_LEARN_INHERIT), "inherit") == 0);
    assert(oyk_gui_cycle_learned_policy(OYK_LEARN_INHERIT, 1) == OYK_LEARN_NONE);
    assert(oyk_gui_cycle_learned_policy(OYK_LEARN_INHERIT, -1) == OYK_LEARN_BOTH);
    assert(strstr(oyk_gui_about_summary(), "AX.25") != NULL);

    assert(oyk_gui_settings_tab_count() == 8);
    assert(strcmp(oyk_gui_settings_tab_title(0), "Node") == 0);
    assert(strcmp(oyk_gui_settings_tab_title(1), "BBS") == 0);
    assert(strcmp(oyk_gui_settings_tab_title(2), "CHAT") == 0);
    assert(strcmp(oyk_gui_settings_tab_title(3), "Interfaces") == 0);
    assert(strcmp(oyk_gui_settings_tab_title(4), "Peers") == 0);
    assert(strcmp(oyk_gui_settings_tab_title(5), "Rig") == 0);
    assert(strcmp(oyk_gui_settings_tab_title(6), "Printer") == 0);
    assert(strcmp(oyk_gui_settings_tab_title(7), "Theme") == 0);

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

struct TestPreviewLog {
    int calls;
    char last_name[32];
    oyk_u32 last_bg;
    oyk_u32 last_fg;
};

static void on_test_theme_preview(const OykThemeConfig* pal, const char* active_name, void* ctx) {
    TestPreviewLog* pl = static_cast<TestPreviewLog*>(ctx);
    if (!pl || !pal) return;
    ++pl->calls;
    strcpy(pl->last_name, active_name ? active_name : "");
    pl->last_bg = pal->colors[OYK_TR_BG];
    pl->last_fg = pal->colors[OYK_TR_FG];
}

static void test_native_os_theme_and_full_screen_preview() {
    OykConfig cfg;
    oyk_config_init_default(&cfg);

    OykThemeConfig os_pal;
    oyk_theme_init_builtin(&os_pal, 1);
    strcpy(os_pal.name, "native");
    os_pal.is_dark_base = 0;
    os_pal.colors[OYK_TR_BG] = 0xEFEFEFUL;
    os_pal.colors[OYK_TR_FG] = 0x111111UL;
    os_pal.colors[OYK_TR_ACCENT] = 0x3366CCUL;

    OykGuiThemeCtrl tc;
    TestPreviewLog log;
    memset(&log, 0, sizeof(log));
    oyk_gui_theme_init(&tc, &cfg, 1, 0);
    assert(strcmp(tc.active_name, "native") == 0);
    oyk_gui_theme_set_preview_cb(&tc, on_test_theme_preview, &log);
    oyk_gui_theme_set_os_native(&tc, &os_pal);
    assert(log.calls == 1);
    assert(log.last_bg == 0xEFEFEFUL && log.last_fg == 0x111111UL);

    /* Cloning from native preserves OS-sourced colors */
    char err[96];
    assert(oyk_gui_theme_set_role_hex(&tc, OYK_TR_ACCENT, "#AA2244", err, sizeof(err)));
    assert(strcmp(tc.active_name, "custom-light") == 0);
    assert(log.calls == 2);
    assert(log.last_bg == 0xEFEFEFUL);

    /* Cycling touches every theme: custom-light -> native -> dark -> light */
    oyk_gui_theme_cycle(&tc, 1);
    assert(strcmp(tc.active_name, "native") == 0);
    assert(log.last_bg == 0xEFEFEFUL);

    oyk_gui_theme_cycle(&tc, 1);
    assert(strcmp(tc.active_name, "dark") == 0);
    assert(log.last_bg != 0xEFEFEFUL);

    oyk_gui_theme_cycle(&tc, 1);
    assert(strcmp(tc.active_name, "light") == 0);

    /* Revert restores initial "native" and fires whole-screen preview callback */
    int before = log.calls;
    oyk_gui_theme_revert(&tc);
    assert(log.calls == before + 1);
    assert(strcmp(tc.active_name, "native") == 0);
    assert(log.last_bg == 0xEFEFEFUL);
    printf("[PASS] test_native_os_theme_and_full_screen_preview\n");
}

struct StubTuiBuf {
    char ch[25][81];
    int  st[25][81];
};

static void stub_put_text(void* ctx, int row, int col, int max_w, int style, const char* text) {
    StubTuiBuf* b = static_cast<StubTuiBuf*>(ctx);
    if (!b || row < 0 || row >= 25 || col < 0 || col >= 80 || max_w <= 0) return;
    int len = text ? static_cast<int>(strlen(text)) : 0;
    for (int i = 0; i < max_w && col + i < 80; ++i) {
        b->ch[row][col + i] = (i < len) ? text[i] : ' ';
        b->st[row][col + i] = style;
    }
}

static void test_tui_buttons_and_jailed_file_mgr() {
    assert(strstr(oyk_term_hotkey_bar_text(1, 0), "Ctrl-A Z:Help") != NULL);
    assert(strstr(oyk_term_hotkey_bar_text(1, 0), "Tab/F9") != NULL);
    assert(strstr(oyk_term_hotkey_bar_text(0, 0), "Ctrl-A Z:Help") != NULL);

    StubTuiBuf tb;
    memset(&tb, 0, sizeof(tb));
    OykTuiBackend bk;
    memset(&bk, 0, sizeof(bk));
    bk.ctx = &tb;
    bk.put_text = stub_put_text;

    const OykTuiButton btns[3] = {
        { "< Back", 1 },
        { "Next >", 1 },
        { "Cancel", 1 }
    };
    assert(oyk_tui_button_width("< Back") == 12); /* " [ < Back ] " */
    oyk_tui_draw_button_row(&bk, 20, 4, 60, btns, 3, 1, "Hint");
    /* Verify exact 1-cell padding & 2-cell modal-fill gap between Back and Next */
    assert(strncmp(&tb.ch[20][4], " [ < Back ]    [ Next > ]    [ Cancel ] ", 40) == 0);
    assert(tb.st[20][4] == OYK_TUI_STYLE_DLG_BAR && tb.st[20][15] == OYK_TUI_STYLE_DLG_BAR);
    assert(tb.st[20][16] == OYK_TUI_STYLE_DLG_FILL && tb.st[20][17] == OYK_TUI_STYLE_DLG_FILL);
    assert(tb.st[20][18] == OYK_TUI_STYLE_DLG_SEL && tb.st[20][29] == OYK_TUI_STYLE_DLG_SEL);
    assert(tb.st[20][30] == OYK_TUI_STYLE_DLG_FILL && tb.st[20][31] == OYK_TUI_STYLE_DLG_FILL);
    assert(tb.st[20][32] == OYK_TUI_STYLE_DLG_BAR && tb.st[20][43] == OYK_TUI_STYLE_DLG_BAR);
    assert(oyk_tui_hit_button_row(20, 4, 60, btns, 3, 20, 18) == 1);
    assert(oyk_tui_hit_button_row(20, 4, 60, btns, 3, 20, 16) == -1);

    /* Dual-pane jailed File Manager checks */
    system("rm -rf /tmp/oyk_gui_fm_test");
    OykGuiFileMgr fm;
    oyk_gui_filemgr_init(&fm, "/tmp/oyk_gui_fm_test/inc", "/tmp/oyk_gui_fm_test/files");
    fm.active_pane = 1;
    assert(oyk_gui_filemgr_mkdir(&fm, "sub") == 1);
    fm.active_pane = 0;
    assert(oyk_gui_filemgr_mkdir(&fm, "W3TRM-1") == 1);

    /* Create local source file and Upload into incoming/W3TRM-1, then Approve -> files/sub */
    FILE* lp = fopen("/tmp/oyk_gui_fm_src.txt", "wb");
    assert(lp != NULL);
    fputs("Hello Packet BBS\n", lp);
    fclose(lp);

    oyk_gui_filemgr_refresh(&fm);
    /* Root must NEVER have ".." */
    for (int i = 0; i < fm.files.count; ++i) assert(strcmp(fm.files.entries[i].name, "..") != 0);
    for (int i = 0; i < fm.inc.count; ++i) assert(strcmp(fm.inc.entries[i].name, "..") != 0);

    fm.inc.sel = 0; /* W3TRM-1 */
    assert(oyk_gui_filemgr_activate(&fm, 0) == 1);
    assert(strcmp(fm.inc.rel, "W3TRM-1") == 0);
    /* Subdirectory MUST have ".." at index 0 */
    assert(fm.inc.count >= 1 && fm.inc.entries[0].is_parent == 1 && strcmp(fm.inc.entries[0].name, "..") == 0);

    int exists = 0;
    assert(oyk_gui_filemgr_upload(&fm, "/tmp/oyk_gui_fm_src.txt", 0, &exists) == 1);
    fm.active_pane = 1;
    fm.files.sel = 0; /* sub */
    assert(oyk_gui_filemgr_activate(&fm, 1) == 1);
    assert(strcmp(fm.files.rel, "sub") == 0);

    /* Move selected uploaded file from incoming/W3TRM-1 -> files/sub */
    fm.inc.sel = 1;
    assert(oyk_gui_filemgr_approve(&fm, 0, &exists) == 1);
    /* Preview & rename inside files/sub */
    fm.active_pane = 1;
    fm.files.sel = 1;
    char pbuf[128];
    int trunc = 0;
    assert(oyk_gui_filemgr_view(&fm, pbuf, sizeof(pbuf), &trunc) == 1);
    assert(strstr(pbuf, "Hello Packet BBS") != NULL);
    assert(oyk_gui_filemgr_rename(&fm, "notes.txt") == 1);
    fm.files.sel = 1;
    assert(oyk_gui_filemgr_delete(&fm) == 1);

    /* Jail guard: cannot go above root */
    assert(oyk_gui_filemgr_up(&fm, 1) == 1);
    assert(fm.files.rel[0] == '\0');
    assert(oyk_gui_filemgr_up(&fm, 1) == 0);
    char nrel[64], nerr[64];
    assert(oyk_bbs_enter_subdir("/tmp/oyk_gui_fm_test/files", "", "..", nrel, sizeof(nrel), nerr, sizeof(nerr)) == 0);
    assert(oyk_bbs_enter_subdir("/tmp/oyk_gui_fm_test/files", "sub", "../../etc", nrel, sizeof(nrel), nerr, sizeof(nerr)) == 0);

    remove("/tmp/oyk_gui_fm_src.txt");
    system("rm -rf /tmp/oyk_gui_fm_test");
    printf("[PASS] test_tui_buttons_and_jailed_file_mgr\n");
}

int main() {
    test_menus_titles_tabs_and_tables();
    test_banners_exit_prompt_and_validation();
    test_add_iface_wizard_and_theme_ctrl();
    test_native_os_theme_and_full_screen_preview();
    test_tui_buttons_and_jailed_file_mgr();
    printf("All oyk_gui middleware tests passed!\n");
    return 0;
}
