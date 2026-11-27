/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * Shared Strict ANSI C89 Network & Serial KISS Abstraction
 * Supports SVR4 sockets (Motorola 88K), SVR3.2 WIN/TCP (AT&T 3B2), and
 * OpenVMS UCX/TCPIP sockets, falling back gracefully when offline.
 */

#ifndef OYK_SYSV_NET_H
#define OYK_SYSV_NET_H

#include "sysv_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int            udp_fd;
    int            serial_fd;
    int            net_available;
    int            serial_available;
    sysv_u16       bound_port;
    unsigned long  rx_bytes;
    unsigned long  tx_bytes;
    char           last_error[64];
} OykSysvNet;

void oyk_sysv_net_init(OykSysvNet *net, const OykSysvConfig *cfg);
void oyk_sysv_net_shutdown(OykSysvNet *net);
int  oyk_sysv_net_open_udp(OykSysvNet *net, const char *bind_host, sysv_u16 port);
int  oyk_sysv_net_open_serial(OykSysvNet *net, const char *device, sysv_u32 baud);
int  oyk_sysv_net_send_udp(OykSysvNet *net, const char *host, sysv_u16 port,
                           const sysv_u8 *data, size_t len);
int  oyk_sysv_net_poll_udp(OykSysvNet *net, sysv_u8 *out, size_t cap, int timeout_ms);
int  oyk_sysv_net_send_serial_kiss(OykSysvNet *net, sysv_u8 kiss_port,
                                   const sysv_u8 *ax25_frame, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* OYK_SYSV_NET_H */
