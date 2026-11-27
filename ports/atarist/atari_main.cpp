/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * Atari ST / STE / TT / Falcon GEM Entry Point (OYKNODE.PRG)
 */

#include "atari_policy.hpp"
#include "atari_net.hpp"
#include "atari_ui.hpp"
#include "atari_capture.hpp"

#include <string.h>

int main(int argc, char** argv) {
    int ui_test = 0;
    const char* cfg_path = "NODE.CFG";
    oyk_atari_parse_cli_flags(argc, argv, &ui_test, &cfg_path);
    if (!ui_test && oyk_atari_is_ui_test_mode()) {
        ui_test = 1;
    }

    OykPortableNode* node = new OykPortableNode();
    OykAtariNetState* net = new OykAtariNetState();
    OykConfig* init_cfg = new OykConfig();
    if (!node || !net || !init_cfg) {
        if (node) delete node;
        if (net) delete net;
        if (init_cfg) delete init_cfg;
        oyk_atari_ui_show_fatal("Insufficient ST/TT RAM starting OYK Node.");
        return 1;
    }

    char err_buf[128];
    err_buf[0] = '\0';
    oyk_config_init_default(init_cfg);
    (void)oyk_config_load(cfg_path, init_cfg, err_buf, sizeof(err_buf));

    oyk_atari_net_init(net, node);
    node->init(init_cfg, oyk_atari_tx_callback, net, err_buf, sizeof(err_buf));
    delete init_cfg;

    int rc = oyk_atari_ui_run(node, net, ui_test);

    oyk_atari_net_shutdown(net);
    node->shutdown();
    delete net;
    delete node;
    return rc;
}
