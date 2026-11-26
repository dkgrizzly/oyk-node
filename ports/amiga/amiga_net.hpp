/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_AMIGA_NET_HPP
#define OYK_AMIGA_NET_HPP

#include "oyk_portable.hpp"

struct OykAmigaNetCaps {
    int  exec_version;
    int  has_bsdsocket;
    int  has_serial_device;
    int  wb_width;
    int  wb_height;
    int  wb_depth;
    int  wb_colors;
    char backend_summary[96];
};

struct OykAmigaNetState {
    OykAmigaNetCaps  caps;
    OykPortableNode* node;
    int              initialized;
    oyk_u32          tx_packets;
};

void oyk_amiga_detect_caps(OykAmigaNetCaps* out);
int  oyk_amiga_net_init(OykAmigaNetState* net, OykPortableNode* node);
void oyk_amiga_net_poll(OykAmigaNetState* net);
void oyk_amiga_net_shutdown(OykAmigaNetState* net);
int  oyk_amiga_tx_callback(int iface_idx,
                           const OykFrame* frame,
                           oyk_u32 peer_ip,
                           oyk_u16 peer_port,
                           oyk_u16 peer_handle,
                           void* ctx);

#endif /* OYK_AMIGA_NET_HPP */
