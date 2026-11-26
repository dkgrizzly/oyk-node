/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "gsos_net.h"

#include <stdio.h>
#include <string.h>

#ifdef __VBCC__
#include <OS/Types.h>
#include <OS/Locator.h>
#include <OS/GSOS.h>
#endif

void oyk_gsos_detect_caps(OykGsosNetCaps *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->has_scc_serial = 1; /* Apple IIgs built-in Zilog 8530 SCC (.A / .B) */
    out->has_shr_640 = 1;    /* Super Hi-Res 640x200 hardware */

#ifdef __VBCC__
    {
        VersionRecGS vrec;
        Pointer marinetti_ptr;
        vrec.pCount = 1;
        vrec.version = 0;
        GetVersionGS(&vrec);
        out->gsos_version = (gsos_u16)vrec.version;
        if (!TLStatus(0)) {
            TLStartUp();
        }
        /* Marinetti TCP/IP link layer installs as User Toolset #54 (0x36).
         * A valid Tool Function Pointer Table stores function count (>= 16) in word 0. */
        marinetti_ptr = GetTSPtr((Word)0x0036, (Word)0x8000, 0L);
        if (marinetti_ptr != NULL && _toolErr == 0 &&
            ((const unsigned short *)marinetti_ptr)[0] >= 16U) {
            out->has_marinetti = 1;
        } else {
            out->has_marinetti = 0;
        }
    }
#else
    out->gsos_version = 0x0601U;
    out->has_marinetti = 0;
#endif

    sprintf(out->backend_summary, "SCC Serial (.A/.B); %s",
            out->has_marinetti ? "Marinetti TCP/IP" : "Marinetti absent");
}

int oyk_gsos_net_init(OykGsosNetState *net, OykGsosNode *node) {
    if (!net) return 0;
    memset(net, 0, sizeof(*net));
    oyk_gsos_detect_caps(&net->caps);
    net->node = node;
    net->initialized = 1;
    return 1;
}

void oyk_gsos_net_poll(OykGsosNetState *net) {
    (void)net;
}

void oyk_gsos_net_shutdown(OykGsosNetState *net) {
    if (net) net->initialized = 0;
}
