/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "sysv_core.h"
#include "sysv_motif_ui.h"
#include "sysv_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    OykSysvApp *app;
    OykSysvNet *net;
    const char *env_test;
    const char *cfg_path;
    int i;
    int rc;
    int seed_demo;

    env_test = getenv("OYK_UI_TEST_MODE");
    seed_demo = (env_test && strcmp(env_test, "1") == 0) ? 1 : 0;
    cfg_path = NULL;
    for (i = 1; i < argc; ++i) {
        if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) && i + 1 < argc) {
            cfg_path = argv[++i];
        } else if (strcmp(argv[i], "--unit-test") == 0) {
            seed_demo = 1;
        }
    }

    app = (OykSysvApp *)malloc(sizeof(OykSysvApp));
    if (!app) return 1;
    oyk_sysv_app_init(app, "Motorola 88K SVR4 (OSF/Motif 1.2)", seed_demo);
    if (cfg_path) {
        (void)oyk_sysv_config_load_file(cfg_path, &app->node.cfg);
    } else if (!seed_demo) {
        (void)oyk_sysv_config_load_file("node.cfg", &app->node.cfg);
    }

    net = (OykSysvNet *)malloc(sizeof(OykSysvNet));
    if (net) oyk_sysv_net_init(net, &app->node.cfg);

    rc = oyk_sysv_motif_run(app, argc, argv);

    if (net) {
        oyk_sysv_net_shutdown(net);
        free(net);
    }
    free(app);
    return rc;
}
