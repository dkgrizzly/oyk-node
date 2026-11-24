/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_YAPP_HPP
#define OYK_YAPP_HPP

/*
 * Portable C++98 / 16-bit-clean WA7MBL YAPP & F6FBB YAPP-C File Transfer Engine
 * for MS-DOS (OpenWatcom 8088/V20) and Windows 3.1 / 95..11 (Win16/Win32/Win64).
 *
 * Security invariants:
 *   - Rejects path traversal (../, ..\, /, \, drive letters, control bytes, reserved names).
 *   - Saves downloaded files into incoming/<CALLSIGN-SSID>/<filename>.
 *   - Never overwrites an existing file without explicit user confirmation.
 */

#include "oyk_portable.hpp"

#define OYK_YAPP_SOH  0x01U
#define OYK_YAPP_STX  0x02U
#define OYK_YAPP_ETX  0x03U
#define OYK_YAPP_EOT  0x04U
#define OYK_YAPP_ENQ  0x05U
#define OYK_YAPP_ACK  0x06U
#define OYK_YAPP_DLE  0x10U
#define OYK_YAPP_NAK  0x15U
#define OYK_YAPP_CAN  0x18U

enum OykYappState {
    OYK_YAPP_IDLE = 0,
    OYK_YAPP_SEND_WAIT_INIT,
    OYK_YAPP_SEND_WAIT_HDR,
    OYK_YAPP_SENDING,
    OYK_YAPP_SEND_WAIT_EOF,
    OYK_YAPP_SEND_WAIT_EOT,
    OYK_YAPP_RECV_WAIT_HDR,
    OYK_YAPP_RECV_OVERWRITE,
    OYK_YAPP_RECV_DATA,
    OYK_YAPP_RECV_WAIT_EOT,
    OYK_YAPP_COMPLETE,
    OYK_YAPP_ABORTED,
    OYK_YAPP_ERROR
};

typedef int (*OykYappSendFn)(const oyk_u8* data, size_t len, void* ctx);
/* Return 1 to allow overwrite, 0 to decline. */
typedef int (*OykYappOverwriteFn)(const char* target_path, const char* filename, void* ctx);

int oyk_yapp_is_safe_filename(const char* name);
void oyk_yapp_format_peer_dir(const OykCallsign* remote_call, char* out, size_t out_sz);
int oyk_yapp_resolve_incoming_path(const char* incoming_root,
                                   const OykCallsign* remote_call,
                                   const char* filename,
                                   char* out_path, size_t out_sz,
                                   char* out_dir, size_t out_dir_sz);

struct OykYappSession {
    char incoming_root[64];
    OykCallsign remote_call;
    OykYappSendFn send_fn;
    void* send_ctx;
    OykYappOverwriteFn overwrite_fn;
    void* overwrite_ctx;

    OykYappState state;
    int sending;
    int yappc;
    int pending_enq;

    char filename[128];
    char target_dir[160];
    char target_path[260];
    oyk_u32 total_bytes;
    oyk_u32 bytes_transferred;
    char status[128];

    FILE* fp;

    int parse_stage; /* 0=Type, 1=LenOrCode, 2=Payload, 3=Checksum */
    oyk_u8 pkt_type;
    oyk_u16 pkt_expected;
    oyk_u16 pkt_len;
    int pkt_expect_csum;
    oyk_u8 pkt_buf[264];
};

void oyk_yapp_init(OykYappSession* y, const char* incoming_root,
                   OykYappSendFn send_fn, void* send_ctx,
                   OykYappOverwriteFn overwrite_fn, void* overwrite_ctx);
void oyk_yapp_reset(OykYappSession* y);
void oyk_yapp_set_remote(OykYappSession* y, const OykCallsign* remote_call);
int oyk_yapp_is_active(const OykYappSession* y);

int oyk_yapp_start_send(OykYappSession* y, const char* local_path,
                        char* err_out, size_t err_sz);
void oyk_yapp_cancel(OykYappSession* y, const char* reason);
void oyk_yapp_resolve_overwrite(OykYappSession* y, int allow_overwrite);

/* Feed raw received bytes; returns count of plain non-YAPP bytes written to plain_out. */
size_t oyk_yapp_feed_bytes(OykYappSession* y, const oyk_u8* data, size_t len,
                           oyk_u8* plain_out, size_t plain_cap);

#endif /* OYK_YAPP_HPP */
