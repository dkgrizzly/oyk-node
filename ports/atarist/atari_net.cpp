/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "atari_net.hpp"

#include <stdio.h>
#include <string.h>

#ifdef __ATARI__
#include <osbind.h>

typedef struct {
    unsigned long tag;
    unsigned long val;
} AtariCookie;

static AtariCookie* g_probe_jar = NULL;

static long read_cookie_jar_ptr(void) {
    unsigned long** p = reinterpret_cast<unsigned long**>(0x000005A0UL);
    g_probe_jar = reinterpret_cast<AtariCookie*>(*p);
    return 0L;
}

static int at_find_cookie(unsigned long tag, unsigned long* out_val) {
    Supexec(read_cookie_jar_ptr);
    if (!g_probe_jar) return 0;
    for (int i = 0; i < 64 && g_probe_jar[i].tag != 0UL; ++i) {
        if (g_probe_jar[i].tag == tag) {
            if (out_val) *out_val = g_probe_jar[i].val;
            return 1;
        }
    }
    return 0;
}
#endif

void oyk_atari_detect_caps(OykAtariNetCaps* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->has_rs232 = 1; /* Atari ST/TT/Falcon BIOS Aux/MFP RS-232 is always present */

#ifdef __ATARI__
    unsigned long mch = 0UL;
    if (at_find_cookie(0x5F4D4348UL /* '_MCH' */, &mch)) {
        out->machine_cookie = static_cast<oyk_u32>(mch);
    }
    if (at_find_cookie(0x4D694E54UL /* 'MiNT' */, NULL)) {
        out->has_mintnet = 1;
    }
    if (at_find_cookie(0x5354694BUL /* 'STiK' */, NULL)) {
        out->has_sting = 1;
    }
    /* Query BIOS Bcostat(1) output status safely (non-blocking) */
    (void)Bcostat(1);
#else
    out->machine_cookie = 0UL;
#endif

    const char* tcp = out->has_mintnet ? "MiNTnet"
                    : (out->has_sting ? "STinG/STiK" : "no STinG/MiNTnet");
    sprintf(out->backend_summary, "RS-232 AUX; %s", tcp);
}

int oyk_atari_net_init(OykAtariNetState* net, OykPortableNode* node) {
    if (!net) return 0;
    memset(net, 0, sizeof(*net));
    oyk_atari_detect_caps(&net->caps);
    net->node = node;
    net->initialized = 1;
    return 1;
}

void oyk_atari_net_poll(OykAtariNetState* net) {
    (void)net;
}

void oyk_atari_net_shutdown(OykAtariNetState* net) {
    if (net) net->initialized = 0;
}

int oyk_atari_tx_callback(int iface_idx,
                          const OykFrame* frame,
                          oyk_u32 peer_ip,
                          oyk_u16 peer_port,
                          oyk_u16 peer_handle,
                          void* ctx) {
    (void)iface_idx;
    (void)peer_ip;
    (void)peer_port;
    (void)peer_handle;
    OykAtariNetState* net = static_cast<OykAtariNetState*>(ctx);
    if (net) ++net->tx_packets;
    return (frame != NULL) ? 1 : 0;
}
