/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_ATARI_NET_HPP
#define OYK_ATARI_NET_HPP

#include "oyk_portable.hpp"

struct OykAtariNetCaps {
    oyk_u32 machine_cookie; /* _MCH cookie (0=ST, 0x10000=STE, 0x20000=TT, 0x30000=Falcon) */
    int     has_mintnet;    /* 'MiNT' cookie */
    int     has_sting;      /* 'STiK' cookie */
    int     has_rs232;      /* BIOS AUX (device 1: RS-232 MFP/SCC) */
    char    backend_summary[96];
};

struct OykAtariNetState {
    OykAtariNetCaps  caps;
    OykPortableNode* node;
    int              initialized;
    oyk_u32          tx_packets;
};

void oyk_atari_detect_caps(OykAtariNetCaps* out);
int  oyk_atari_net_init(OykAtariNetState* net, OykPortableNode* node);
void oyk_atari_net_poll(OykAtariNetState* net);
void oyk_atari_net_shutdown(OykAtariNetState* net);
int  oyk_atari_tx_callback(int iface_idx,
                           const OykFrame* frame,
                           oyk_u32 peer_ip,
                           oyk_u16 peer_port,
                           oyk_u16 peer_handle,
                           void* ctx);

#endif /* OYK_ATARI_NET_HPP */
