/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_GSOS_NET_H
#define OYK_GSOS_NET_H

#include "gsos_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gsos_u16 gsos_version;
    int      has_marinetti;
    int      has_scc_serial;
    int      has_shr_640;
    char     backend_summary[96];
} OykGsosNetCaps;

typedef struct {
    OykGsosNetCaps caps;
    OykGsosNode   *node;
    int            initialized;
} OykGsosNetState;

void oyk_gsos_detect_caps(OykGsosNetCaps *out);
int  oyk_gsos_net_init(OykGsosNetState *net, OykGsosNode *node);
void oyk_gsos_net_poll(OykGsosNetState *net);
void oyk_gsos_net_shutdown(OykGsosNetState *net);

#ifdef __cplusplus
}
#endif

#endif /* OYK_GSOS_NET_H */
