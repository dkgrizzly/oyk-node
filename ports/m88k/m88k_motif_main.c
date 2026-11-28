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
    int i;
    int rc;

    app = (OykSysvApp *)malloc(sizeof(OykSysvApp));
    if (!app) return 1;
    oyk_sysv_app_init(app, "Motorola 88K SVR4 (OSF/Motif 1.2)", 1);

    for (i = 1; i < argc; ++i) {
        if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) && i + 1 < argc) {
            oyk_sysv_config_load_file(argv[++i], &app->node.cfg);
        }
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
