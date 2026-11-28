/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#define OYK_MACOS_USE_PORTABLE 1
#include "macos_engine_bridge.hpp"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define MB_CHECK(cond, label) \
    do { \
        if (!(cond)) { \
            ++g_failures; \
            fprintf(stderr, "FAIL: %s (line %d)\n", (label), __LINE__); \
        } \
    } while (0)

int main() {
    OykMacOsEngine* eng = new OykMacOsEngine();
    MB_CHECK(eng != NULL, "alloc_engine");
    MB_CHECK(eng->uses_portable_core() == 1, "portable_core_active");

    char err[128];
    err[0] = '\0';
    MB_CHECK(eng->init_and_start(NULL, 1, err, sizeof(err)) == 1, "init_and_start_demo");
    MB_CHECK(eng->is_running() == 1, "engine_is_running");

    OykMacOsSnapshot* snap = new OykMacOsSnapshot();
    eng->get_snapshot(snap);
    MB_CHECK(strcmp(snap->node_callsign, "KR4OYK-7") == 0, "snap_callsign");
    MB_CHECK(snap->iface_count >= 1, "snap_iface_count");
    MB_CHECK(snap->ban_count >= 1, "snap_ban_seeded");

    /* Toggle incoming connections */
    eng->set_accept_incoming(0);
    eng->get_snapshot(snap);
    MB_CHECK(snap->accept_incoming == 0, "deny_incoming");
    eng->set_accept_incoming(1);

    /* Ban & Unban cycle */
    char msg[96];
    MB_CHECK(eng->ban_callsign("K1BAD-9", "30m", 1UL, "Test", msg, sizeof(msg)) == 1, "ban_callsign");
    eng->get_snapshot(snap);
    MB_CHECK(snap->ban_count >= 2, "ban_added");
    MB_CHECK(eng->unban_callsign("K1BAD-9", msg, sizeof(msg)) == 1, "unban_callsign");

    /* Open & close local terminal + send BBS bytes */
    oyk_u32 sid = 0;
    char resolved[24];
    MB_CHECK(eng->open_local_terminal("axudp0", "BBS", &sid, resolved, sizeof(resolved),
                                      err, sizeof(err)) == 1, "open_local_term");
    const oyk_u8 cmd[] = { 'H', '\r' };
    MB_CHECK(eng->send_local_bytes(sid, cmd, sizeof(cmd)) == 1, "send_local_bytes");
    eng->poll();
    MB_CHECK(eng->close_local_terminal(sid) == 1, "close_local_term");

    /* Verify shared oyk_gui schema access & modal preemption */
    MB_CHECK(oyk_gui_settings_tab_count() == 8, "gui_tabs_8");
    MB_CHECK(oyk_gui_node_menu_count() == 11, "gui_menu_11");
    MB_CHECK(oyk_gui_is_modal_screen(OYK_SCR_SETTINGS) == 1 &&
             oyk_gui_global_hotkey_allowed(OYK_SCR_SETTINGS, 0) == 0 &&
             oyk_gui_global_hotkey_allowed(OYK_SCR_MAIN, 0) == 1, "macos_modal_preemption");
    for (int mi = 0; mi < oyk_gui_node_menu_count(); ++mi) {
        const OykGuiMenuItem* mitem = oyk_gui_node_menu_item(mi);
        MB_CHECK(!mitem || (mitem->action != OYK_ACT_PEERS && mitem->action != OYK_ACT_THEME),
                 "macos_menu_no_dup_peers_theme");
    }

    /* Verify macOS OS-native default theme + whole-screen live preview across all themes */
    OykConfig tcfg = eng->portable_config();
    OykThemeConfig os_light;
    OykThemeConfig os_dark;
    oyk_macos_build_os_native_theme(0, NULL, &os_light);
    oyk_macos_build_os_native_theme(1, NULL, &os_dark);
    MB_CHECK(strcmp(os_light.name, "native") == 0 && os_light.is_dark_base == 0, "macos_os_light_native");
    MB_CHECK(strcmp(os_dark.name, "native") == 0 && os_dark.is_dark_base == 1, "macos_os_dark_native");
    MB_CHECK(os_light.colors[OYK_TR_BG] != os_dark.colors[OYK_TR_BG], "macos_os_light_vs_dark");

    struct RepaintTrack {
        oyk_u32 main_bg;
        oyk_u32 modal_bg;
        int repaints;
    } track = { 0, 0, 0 };

    struct PreviewShim {
        static void on_preview(const OykThemeConfig* pal, const char*, void* ctx) {
            RepaintTrack* tr = static_cast<RepaintTrack*>(ctx);
            if (!tr || !pal) return;
            tr->main_bg = pal->colors[OYK_TR_BG];
            tr->modal_bg = pal->colors[OYK_TR_BG];
            ++tr->repaints;
        }
    };

    OykGuiThemeCtrl tctrl;
    oyk_gui_theme_init(&tctrl, &tcfg, 1, 0);
    MB_CHECK(strcmp(tctrl.active_name, "native") == 0, "macos_default_native");
    oyk_gui_theme_set_preview_cb(&tctrl, PreviewShim::on_preview, &track);
    oyk_gui_theme_set_os_native(&tctrl, &os_light);
    MB_CHECK(track.repaints == 1 && track.main_bg == os_light.colors[OYK_TR_BG] &&
             track.modal_bg == os_light.colors[OYK_TR_BG], "macos_native_initial_repaint");

    OykThemeConfig dt;
    OykThemeConfig lt;
    oyk_theme_init_builtin(&dt, 0);
    oyk_theme_init_builtin(&lt, 1);

    oyk_gui_theme_select(&tctrl, "dark");
    MB_CHECK(track.repaints == 2 && track.main_bg == dt.colors[OYK_TR_BG] &&
             track.modal_bg == dt.colors[OYK_TR_BG], "macos_dark_whole_screen_repaint");
    oyk_gui_theme_select(&tctrl, "light");
    MB_CHECK(track.repaints == 3 && track.main_bg == lt.colors[OYK_TR_BG] &&
             track.modal_bg == lt.colors[OYK_TR_BG], "macos_light_whole_screen_repaint");
    char terr[64];
    MB_CHECK(oyk_gui_theme_set_role_rgb(&tctrl, OYK_TR_BG, 0x243040UL, terr, sizeof(terr)) == 1,
             "macos_custom_clone_edit");
    MB_CHECK(track.repaints == 4 && track.main_bg == 0x243040UL && track.modal_bg == 0x243040UL,
             "macos_custom_whole_screen_repaint");
    oyk_gui_theme_revert(&tctrl);
    MB_CHECK(track.repaints == 5 && strcmp(tctrl.active_name, "native") == 0 &&
             track.main_bg == os_light.colors[OYK_TR_BG], "macos_revert_native_repaint");

    eng->stop();
    MB_CHECK(eng->is_running() == 0, "engine_stopped");

    delete snap;
    delete eng;

    if (g_failures == 0) {
        printf("All macOS bridge tests passed.\n");
        return 0;
    }
    return 1;
}
