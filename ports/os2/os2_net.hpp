/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_OS2_NET_HPP
#define OYK_OS2_NET_HPP

#include "oyk_portable.hpp"

struct OykOs2NetState {
    int tcpip_loaded;
    int udp_sock;
    int com_handle;
    oyk_u16 bound_port;
    char com_name[16];
    char last_error[96];
    unsigned long rx_bytes;
    unsigned long tx_bytes;
    oyk_u8 mock_buf[512];
    size_t mock_len;
};

int  oyk_os2_is_valid_com_port(const char* dev);
void oyk_os2_net_init(OykOs2NetState* st);
void oyk_os2_net_shutdown(OykOs2NetState* st);
int  oyk_os2_net_open_udp(OykOs2NetState* st, oyk_u16 port);
int  oyk_os2_net_open_serial(OykOs2NetState* st, const char* dev, oyk_u32 baud);
int  oyk_os2_net_send_udp(OykOs2NetState* st, const char* host, oyk_u16 port,
                          const oyk_u8* data, size_t len);
int  oyk_os2_net_poll_udp(OykOs2NetState* st, oyk_u8* out, size_t cap);

#endif /* OYK_OS2_NET_HPP */
