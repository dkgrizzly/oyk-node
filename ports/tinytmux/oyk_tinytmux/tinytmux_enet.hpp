/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_TINYTMUX_ENET_HPP
#define OYK_TINYTMUX_ENET_HPP

/*
 * Teensy 4.1 10/100 Ethernet Driver (Teensyduino QNEthernet / NativeEthernet).
 */

#include "oyk_tinytmux_config.h"
#include "oyk_portable.hpp"

class TinyTmuxNetBridge;

int tinytmux_enet_init(const oyk_u8 mac[6]);
int tinytmux_enet_poll(TinyTmuxNetBridge* bridge, oyk_u32 now_ms);
int tinytmux_enet_send_axudp(oyk_u32 dst_ip_net, oyk_u16 src_port_host, oyk_u16 dst_port_host,
                             const oyk_u8* payload, size_t len);
int tinytmux_enet_send_axip(oyk_u32 dst_ip_net, const oyk_u8* payload, size_t len);
int tinytmux_enet_rigctl_tcp_exchange(oyk_u32 host_ip_net, oyk_u16 port_host,
                                      const char* cmd, size_t cmd_len,
                                      char* reply_out, size_t reply_cap);
oyk_u32 tinytmux_enet_current_ip();
int tinytmux_enet_link_is_up();
int tinytmux_enet_speed_is_100m();

#endif /* OYK_TINYTMUX_ENET_HPP */
