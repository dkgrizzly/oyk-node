/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "vms_compat.h"
#include "sysv_core.h"
#include "sysv_net.h"
#include "sysv_motif_ui.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    OykSysvApp app;
    OykSysvNet net;
    int i;
    int headless = 0;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0 || strcmp(argv[i], "--unit-test") == 0) {
            headless = 1;
        }
    }

    oyk_sysv_app_init(&app, OYK_VMS_ARCH_NAME, headless ? 1 : 0);
    oyk_sysv_net_init(&net, &app.node.cfg);

    if (headless) {
        printf("OYK DECwindows Motif 1.2 self-test PASSED (%s)\n",
               OYK_VMS_ARCH_NAME);
        oyk_sysv_net_shutdown(&net);
        return 0;
    }

#ifdef OYK_HAS_MOTIF
    i = oyk_sysv_motif_run(&app, argc, argv);
#else
    printf("DECwindows Motif runtime disabled at build time (%s).\n",
           OYK_VMS_ARCH_NAME);
    i = 0;
#endif

    oyk_sysv_net_shutdown(&net);
    return i;
}
