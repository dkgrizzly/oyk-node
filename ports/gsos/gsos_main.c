/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * Apple IIGS GS/OS Native Application Entry Point (65C816 S16)
 */

#include "gsos_ui.h"
#include "gsos_capture.h"

#include <string.h>

static OykGsosNode     g_node;
static OykGsosNetState g_net;
static OykGsosConfig   g_init_cfg;

int main(void) {
    int test_mode = oyk_gsos_is_ui_test_mode();
    oyk_gsos_config_init_default(&g_init_cfg);
    oyk_gsos_node_init(&g_node, &g_init_cfg);
    oyk_gsos_net_init(&g_net, &g_node);

    {
        int rc = oyk_gsos_ui_run(&g_node, &g_net, test_mode);
        oyk_gsos_net_shutdown(&g_net);
        return rc;
    }
}
