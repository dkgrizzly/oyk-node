/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * Classic Macintosh Entry Point (68k + PowerPC Fat Application)
 * Runs on System Software 6.0.8 through Mac OS 9.2.2.
 */

#include "mac_ui.hpp"
#include "mac_capture.hpp"

#include <string.h>

#include <MacTypes.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Memory.h>

static void mc_init_toolbox() {
    InitGraf(&qd.thePort);
    InitFonts();
    FlushEvents(everyEvent, 0);
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    MaxApplZone();
    MoreMasters();
    MoreMasters();
}

int main() {
    mc_init_toolbox();

    /* Heap-allocate heavy engine & driver structs (68k stack is limited!). */
    OykPortableNode* node = new OykPortableNode();
    OykMacNetState* net = new OykMacNetState();
    OykConfig* init_cfg = new OykConfig();
    if (!node || !net || !init_cfg) {
        if (node) delete node;
        if (net) delete net;
        if (init_cfg) delete init_cfg;
        oyk_mac_ui_show_fatal("Insufficient application memory to start OYK Node.");
        return 1;
    }

    int test_mode = oyk_mac_is_ui_test_mode();
    char err_buf[128];
    err_buf[0] = '\0';
    oyk_config_init_default(init_cfg);
    (void)oyk_config_load("NODE.CFG", init_cfg, err_buf, sizeof(err_buf));

    /* If UI test fixture mode, seed deterministic KR4OYK-7 interfaces & peers. */
    if (test_mode) {
        oyk_parse_callsign("KR4OYK-7", &init_cfg->callsign);
        oyk_parse_callsign("OYKNOD", &init_cfg->alias);
        init_cfg->iface_count = 2;
        memset(&init_cfg->ifaces[0], 0, sizeof(OykIfaceConfig));
        strcpy(init_cfg->ifaces[0].id, "axudp0");
        strcpy(init_cfg->ifaces[0].type, "axudp");
        strcpy(init_cfg->ifaces[0].host, "127.0.0.1");
        init_cfg->ifaces[0].port = 10093U;
        strcpy(init_cfg->ifaces[0].netmask, "255.0.0.0");

        memset(&init_cfg->ifaces[1], 0, sizeof(OykIfaceConfig));
        strcpy(init_cfg->ifaces[1].id, "kiss0");
        strcpy(init_cfg->ifaces[1].type, "kiss-serial");
        strcpy(init_cfg->ifaces[1].device, "Modem");
        init_cfg->ifaces[1].baud = 9600UL;

        init_cfg->peer_count = 1;
        memset(&init_cfg->peers[0], 0, sizeof(OykPeerConfig));
        strcpy(init_cfg->peers[0].id, "remote0");
        strcpy(init_cfg->peers[0].callsign, "R3MOT-7");
        strcpy(init_cfg->peers[0].interface_id, "axudp0");
        strcpy(init_cfg->peers[0].host, "12.34.56.78");
        init_cfg->peers[0].port = 10093U;
        init_cfg->peers[0].allow_inbound = 1;
        init_cfg->peers[0].allow_outbound = 1;
    }

    oyk_mac_net_init(net, node);
    node->init(init_cfg, oyk_mac_tx_callback, net, err_buf, sizeof(err_buf));
    delete init_cfg;
    oyk_mac_net_sync_ifaces(net, &node->config());

    int rc = oyk_mac_ui_run(node, net, test_mode);

    oyk_mac_net_shutdown(net);
    node->shutdown();
    delete net;
    delete node;
    return rc;
}
