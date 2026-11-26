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

    /* Verify shared oyk_gui schema access */
    MB_CHECK(oyk_gui_settings_tab_count() == 7, "gui_tabs_7");
    MB_CHECK(oyk_gui_node_menu_count() == 13, "gui_menu_13");

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
