/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "os2_net.hpp"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifdef __OS2__
#define INCL_DOSMODULEMGR
#define INCL_DOSDEVICES
#define INCL_DOSDEVIOCTL
#define INCL_DOSERRORS
#include <os2.h>

typedef int (*OykOs2SockInitFn)(void);
typedef int (*OykOs2SocketFn)(int, int, int);
typedef int (*OykOs2CloseFn)(int);
typedef int (*OykOs2BindFn)(int, const void*, int);
typedef int (*OykOs2SendtoFn)(int, const void*, int, int, const void*, int);
typedef int (*OykOs2RecvfromFn)(int, void*, int, int, void*, int*);

static HMODULE          g_so32 = NULLHANDLE;
static OykOs2SockInitFn g_sock_init = 0;
static OykOs2SocketFn   g_socket = 0;
static OykOs2CloseFn    g_soclose = 0;
static OykOs2BindFn     g_bind = 0;
static OykOs2SendtoFn   g_sendto = 0;
static OykOs2RecvfromFn g_recvfrom = 0;
#endif

int oyk_os2_is_valid_com_port(const char* dev) {
    if (!dev || dev[0] == '\0') return 0;
    if ((dev[0] == 'C' || dev[0] == 'c') &&
        (dev[1] == 'O' || dev[1] == 'o') &&
        (dev[2] == 'M' || dev[2] == 'm') &&
        dev[3] >= '1' && dev[3] <= '4' && dev[4] == '\0') {
        return 1;
    }
    return 0;
}

void oyk_os2_net_init(OykOs2NetState* st) {
    if (!st) return;
    memset(st, 0, sizeof(*st));
    st->udp_sock = -1;
    st->com_handle = -1;

#ifdef __OS2__
    {
        char fail[128];
        if (DosLoadModule(fail, sizeof(fail), (PSZ)"SO32DLL", &g_so32) == NO_ERROR) {
            DosQueryProcAddr(g_so32, 0L, (PSZ)"SOCK_INIT", (PFN*)&g_sock_init);
            DosQueryProcAddr(g_so32, 0L, (PSZ)"SOCKET",    (PFN*)&g_socket);
            DosQueryProcAddr(g_so32, 0L, (PSZ)"SOCLOSE",   (PFN*)&g_soclose);
            DosQueryProcAddr(g_so32, 0L, (PSZ)"BIND",      (PFN*)&g_bind);
            DosQueryProcAddr(g_so32, 0L, (PSZ)"SENDTO",    (PFN*)&g_sendto);
            DosQueryProcAddr(g_so32, 0L, (PSZ)"RECVFROM",  (PFN*)&g_recvfrom);
            if (g_sock_init && g_socket && g_soclose && g_bind) {
                g_sock_init();
                st->tcpip_loaded = 1;
            }
        }
        if (!st->tcpip_loaded) {
            strcpy(st->last_error, "IBM TCP/IP SO32DLL absent; running serial/offline");
        }
    }
#else
    strcpy(st->last_error, "OS/2 host mock mode (SO32DLL/TCP32DLL dynamic loader)");
#endif
}

void oyk_os2_net_shutdown(OykOs2NetState* st) {
    if (!st) return;
#ifdef __OS2__
    if (st->udp_sock >= 0 && g_soclose) {
        g_soclose(st->udp_sock);
    }
    if (st->com_handle >= 0) {
        DosClose((HFILE)st->com_handle);
    }
    if (g_so32 != NULLHANDLE) {
        DosFreeModule(g_so32);
        g_so32 = NULLHANDLE;
    }
#endif
    st->udp_sock = -1;
    st->com_handle = -1;
    st->tcpip_loaded = 0;
}

int oyk_os2_net_open_udp(OykOs2NetState* st, oyk_u16 port) {
    if (!st) return -1;
    st->bound_port = port;
#ifdef __OS2__
    if (!st->tcpip_loaded || !g_socket || !g_bind) return -1;
    {
        unsigned char sin[16];
        int fd = g_socket(2 /* AF_INET */, 2 /* SOCK_DGRAM */, 0);
        if (fd < 0) return -1;
        memset(sin, 0, sizeof(sin));
        sin[0] = 2; /* AF_INET LE */
        sin[2] = (unsigned char)((port >> 8) & 0xFFU);
        sin[3] = (unsigned char)(port & 0xFFU);
        if (g_bind(fd, sin, sizeof(sin)) < 0) {
            if (g_soclose) g_soclose(fd);
            return -1;
        }
        st->udp_sock = fd;
        return 0;
    }
#else
    st->udp_sock = 1;
    return 0;
#endif
}

int oyk_os2_net_open_serial(OykOs2NetState* st, const char* dev, oyk_u32 baud) {
    if (!st || !oyk_os2_is_valid_com_port(dev)) {
        if (st) strcpy(st->last_error, "Invalid OS/2 COM device (expected COM1..COM4)");
        return -1;
    }
    strncpy(st->com_name, dev, sizeof(st->com_name) - 1U);
    st->com_name[sizeof(st->com_name) - 1U] = '\0';
#ifdef __OS2__
    {
        HFILE hf = NULLHANDLE;
        ULONG action = 0;
        ULONG b = baud ? baud : 9600UL;
        BYTE lc[4] = { 8, 0, 0, 0 }; /* 8N1 */
        APIRET rc = DosOpen((PSZ)st->com_name, &hf, &action, 0UL, FILE_NORMAL,
                            OPEN_ACTION_OPEN_IF_EXISTS,
                            OPEN_ACCESS_READWRITE | OPEN_SHARE_DENYNONE, NULL);
        if (rc != NO_ERROR) {
            strcpy(st->last_error, "DosOpen COM failed");
            return -1;
        }
        DosDevIOCtl(hf, IOCTL_ASYNC, 0x0041UL /* ASYNC_SETBAUDRATE */, &b, sizeof(b), NULL, NULL, 0, NULL);
        DosDevIOCtl(hf, IOCTL_ASYNC, 0x0042UL /* ASYNC_SETLINECTRL */, lc, sizeof(lc), NULL, NULL, 0, NULL);
        st->com_handle = (int)hf;
        return 0;
    }
#else
    (void)baud;
    st->com_handle = 2;
    return 0;
#endif
}

int oyk_os2_net_send_udp(OykOs2NetState* st, const char* host, oyk_u16 port,
                         const oyk_u8* data, size_t len) {
    if (!st || !host || !data || len == 0 || len > sizeof(st->mock_buf)) return -1;
    (void)port;
#ifdef __OS2__
    if (!st->tcpip_loaded || st->udp_sock < 0 || !g_sendto) return -1;
    {
        unsigned char sin[16];
        int b[4];
        memset(sin, 0, sizeof(sin));
        sin[0] = 2;
        sin[2] = (unsigned char)((port >> 8) & 0xFFU);
        sin[3] = (unsigned char)(port & 0xFFU);
        if (sscanf(host, "%d.%d.%d.%d", &b[0], &b[1], &b[2], &b[3]) != 4) return -1;
        sin[4] = (unsigned char)b[0];
        sin[5] = (unsigned char)b[1];
        sin[6] = (unsigned char)b[2];
        sin[7] = (unsigned char)b[3];
        int rc = g_sendto(st->udp_sock, data, (int)len, 0, sin, sizeof(sin));
        if (rc > 0) st->tx_bytes += (unsigned long)rc;
        return rc;
    }
#else
    memcpy(st->mock_buf, data, len);
    st->mock_len = len;
    st->tx_bytes += (unsigned long)len;
    return (int)len;
#endif
}

int oyk_os2_net_poll_udp(OykOs2NetState* st, oyk_u8* out, size_t cap) {
    if (!st || !out || cap == 0) return -1;
#ifdef __OS2__
    if (!st->tcpip_loaded || st->udp_sock < 0 || !g_recvfrom) return -1;
    {
        unsigned char from[16];
        int flen = sizeof(from);
        int rc = g_recvfrom(st->udp_sock, out, (int)cap, 0, from, &flen);
        if (rc > 0) st->rx_bytes += (unsigned long)rc;
        return rc;
    }
#else
    if (st->mock_len == 0) return 0;
    size_t n = st->mock_len < cap ? st->mock_len : cap;
    memcpy(out, st->mock_buf, n);
    st->mock_len = 0;
    st->rx_bytes += (unsigned long)n;
    return (int)n;
#endif
}
