/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_MAC_NET_HPP
#define OYK_MAC_NET_HPP

/*
 * Classic Macintosh (System 6.0.8 - Mac OS 9.2.2) Network & Serial Driver Layer
 *
 * Runtime-detects available Macintosh communications drivers:
 *   1. Macintosh Serial Driver (.AIn/.AOut Modem port, .BIn/.BOut Printer port)
 *      for KISS TNC serial frames and transceiver CAT control.
 *   2. MacTCP (.IPP driver) for UDP/IP (AXUDP) and TCP/IP (KISS-TCP / Telnet).
 *   3. Open Transport ('otvr' Gestalt / CFM) on System 7.5+ / PPC / Mac OS 8-9,
 *      with automatic fallback to MacTCP and Serial-only KISS when absent.
 *
 * Strict C++98, zero STL, zero exceptions, heap-safe for 68k System 6.
 */

#include "oyk_portable.hpp"

enum OykMacNetBackend {
    OYK_MAC_NET_SERIAL_ONLY = 0,
    OYK_MAC_NET_MACTCP      = 1,
    OYK_MAC_NET_OPENTPT     = 2
};

struct OykMacNetCaps {
    int sys_version_bcd;     /* e.g. 0x0608, 0x0755, 0x0922 */
    int has_gestalt;
    int has_color_qd;
    int has_wait_next_event;
    int has_std_file_7;
    int has_serial;
    int has_mactcp;
    int has_open_transport;
    int active_backend;      /* OykMacNetBackend */
    oyk_u32 local_ip_net;    /* local IPv4 if MacTCP/OT active */
    char backend_summary[96];
};

struct OykMacSerialPort {
    int open;
    short in_ref;
    short out_ref;
    char dev_name[32];
    oyk_u32 baud;
    OykKissDecoder kiss_dec;
};

struct OykMacUdpSlot {
    int active;
    int iface_idx;
    oyk_u16 bind_port;
    void* stream_ptr; /* MacTCP StreamPtr or OT EndpointRef */
    int ot_mode;
};

struct OykMacTcpSlot {
    int active;
    int iface_idx;
    int is_kiss_tcp;
    int is_listener;
    int connected;
    oyk_u16 peer_handle;
    oyk_u16 local_port;
    oyk_u16 remote_port;
    oyk_u32 remote_ip;
    void* stream_ptr;
};

struct OykMacNetState {
    OykMacNetCaps caps;
    short ipp_refnum;
    int ipp_open;
    OykMacSerialPort serial_ports[2]; /* 0 = Modem (.AIn/.AOut), 1 = Printer (.BIn/.BOut) */
    OykMacUdpSlot udp_slots[OYK_MAX_INTERFACES];
    OykMacTcpSlot tcp_slots[OYK_MAX_SESSIONS];
    oyk_u8 io_buf[2048];
    OykPortableNode* node;
};

void oyk_mac_detect_caps(OykMacNetCaps* caps);

int  oyk_mac_net_init(OykMacNetState* st, OykPortableNode* node);
void oyk_mac_net_shutdown(OykMacNetState* st);
int  oyk_mac_net_sync_ifaces(OykMacNetState* st, const OykConfig* cfg);
void oyk_mac_net_poll(OykMacNetState* st);

int  oyk_mac_tx_callback(int iface_idx,
                         const OykFrame* frame,
                         oyk_u32 peer_ip,
                         oyk_u16 peer_port,
                         oyk_u16 peer_handle,
                         void* user_ctx);

/* Serial port helpers (mapping "Modem" / "COM1" / ".AIn" -> A, "Printer" / "COM2" / ".BIn" -> B) */
int  oyk_mac_serial_open(OykMacNetState* st, const char* dev_name, oyk_u32 baud);
void oyk_mac_serial_close_all(OykMacNetState* st);
int  oyk_mac_serial_write(OykMacNetState* st, const char* dev_name,
                          const oyk_u8* data, size_t len);
size_t oyk_mac_serial_read(OykMacNetState* st, const char* dev_name,
                           oyk_u8* out, size_t cap);

#endif /* OYK_MAC_NET_HPP */
