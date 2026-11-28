/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "sysv_net.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#if defined(__VMS)
#include <types.h>
#include <socket.h>
#include <in.h>
#include <inet.h>
#include <netdb.h>
#include <unixio.h>
#elif defined(OYK_SYSV_SVR32)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/in.h>
#include <sys/inet.h>
#include <netdb.h>
#include <termio.h>
#include <poll.h>
extern int read();
extern int write();
extern int close();
#ifndef O_NONBLOCK
#define O_NONBLOCK O_NDELAY
#endif
#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <termios.h>
#include <sys/time.h>
#include <unistd.h>
#endif

static void net_close_fd(int *fd) {
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static void net_set_nonblock(int fd) {
    int flags;
    if (fd < 0) return;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

void oyk_sysv_net_init(OykSysvNet *net, const OykSysvConfig *cfg) {
    if (!net) return;
    memset(net, 0, sizeof(*net));
    net->udp_fd = -1;
    net->serial_fd = -1;
    if (cfg && cfg->iface_count > 0 && cfg->ifaces[0].port > 0U) {
        (void)oyk_sysv_net_open_udp(net, cfg->ifaces[0].host, cfg->ifaces[0].port);
    }
}

void oyk_sysv_net_shutdown(OykSysvNet *net) {
    if (!net) return;
    net_close_fd(&net->udp_fd);
    net_close_fd(&net->serial_fd);
    net->net_available = 0;
    net->serial_available = 0;
}

int oyk_sysv_net_open_udp(OykSysvNet *net, const char *bind_host, sysv_u16 port) {
    int fd;
    struct sockaddr_in sa;
    int yes;
    if (!net) return 0;
    net_close_fd(&net->udp_fd);
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        oyk_sysv_copy_str(net->last_error, sizeof(net->last_error), "UDP socket unavailable");
        net->net_available = 0;
        return 0;
    }
    yes = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (!bind_host || !bind_host[0] || strcmp(bind_host, "0.0.0.0") == 0) {
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        sa.sin_addr.s_addr = inet_addr(bind_host);
    }
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(fd);
        oyk_sysv_copy_str(net->last_error, sizeof(net->last_error), "UDP bind failed");
        net->net_available = 0;
        return 0;
    }
    net_set_nonblock(fd);
    net->udp_fd = fd;
    net->bound_port = port;
    net->net_available = 1;
    net->last_error[0] = '\0';
    return 1;
}

int oyk_sysv_net_open_serial(OykSysvNet *net, const char *device, sysv_u32 baud) {
    int fd;
    (void)baud;
    if (!net || !device || !device[0]) return 0;
    net_close_fd(&net->serial_fd);
    fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        oyk_sysv_copy_str(net->last_error, sizeof(net->last_error), "Serial device unavailable");
        net->serial_available = 0;
        return 0;
    }
#if defined(OYK_SYSV_SVR32)
    {
        struct termio tio;
        if (ioctl(fd, TCGETA, &tio) == 0) {
            tio.c_iflag = 0;
            tio.c_oflag = 0;
            tio.c_cflag = CS8 | CREAD | CLOCAL | B9600;
            tio.c_lflag = 0;
            tio.c_cc[VMIN] = 0;
            tio.c_cc[VTIME] = 0;
            (void)ioctl(fd, TCSETA, &tio);
        }
    }
#elif !defined(__VMS)
    {
        struct termios tio;
        if (tcgetattr(fd, &tio) == 0) {
            tio.c_iflag = 0;
            tio.c_oflag = 0;
            tio.c_cflag = CS8 | CREAD | CLOCAL | B9600;
            tio.c_lflag = 0;
            tio.c_cc[VMIN] = 0;
            tio.c_cc[VTIME] = 0;
            (void)tcsetattr(fd, TCSANOW, &tio);
        }
    }
#endif
    net->serial_fd = fd;
    net->serial_available = 1;
    return 1;
}

int oyk_sysv_net_send_udp(OykSysvNet *net, const char *host, sysv_u16 port,
                          const sysv_u8 *data, size_t len) {
    struct sockaddr_in dst;
    int sent;
    if (!net || net->udp_fd < 0 || !host || !data || len == 0U) return 0;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(port);
    dst.sin_addr.s_addr = inet_addr(host);
    sent = (int)sendto(net->udp_fd, (const char *)data, len, 0,
                       (struct sockaddr *)&dst, sizeof(dst));
    if (sent <= 0) return 0;
    net->tx_bytes += (unsigned long)sent;
    return sent;
}

int oyk_sysv_net_poll_udp(OykSysvNet *net, sysv_u8 *out, size_t cap, int timeout_ms) {
#if defined(OYK_SYSV_SVR32)
    struct pollfd pfd;
    int rc;
    int got;
    if (!net || net->udp_fd < 0 || !out || cap == 0U) return 0;
    pfd.fd = net->udp_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    rc = poll(&pfd, 1U, timeout_ms);
    if (rc <= 0 || !(pfd.revents & POLLIN)) return 0;
    got = (int)recv(net->udp_fd, (char *)out, cap, 0);
    if (got > 0) net->rx_bytes += (unsigned long)got;
    return (got > 0) ? got : 0;
#else
    fd_set rfds;
    struct timeval tv;
    int rc;
    int got;
    if (!net || net->udp_fd < 0 || !out || cap == 0U) return 0;
    FD_ZERO(&rfds);
    FD_SET(net->udp_fd, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (long)(timeout_ms % 1000) * 1000L;
    rc = select(net->udp_fd + 1, &rfds, NULL, NULL, &tv);
    if (rc <= 0 || !FD_ISSET(net->udp_fd, &rfds)) return 0;
    got = (int)recv(net->udp_fd, (char *)out, cap, 0);
    if (got > 0) net->rx_bytes += (unsigned long)got;
    return (got > 0) ? got : 0;
#endif
}

int oyk_sysv_net_send_serial_kiss(OykSysvNet *net, sysv_u8 kiss_port,
                                  const sysv_u8 *ax25_frame, size_t len) {
    sysv_u8 kbuf[600];
    size_t klen;
    int wr;
    if (!net || net->serial_fd < 0 || !ax25_frame || len == 0U) return 0;
    klen = oyk_sysv_kiss_encode(kiss_port, ax25_frame, len, kbuf, sizeof(kbuf));
    if (klen == 0U) return 0;
    wr = (int)write(net->serial_fd, (const char *)kbuf, klen);
    if (wr > 0) net->tx_bytes += (unsigned long)wr;
    return (wr > 0) ? wr : 0;
}
