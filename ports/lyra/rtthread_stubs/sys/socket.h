/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_LYRA_SOCKET_STUB_H
#define OYK_LYRA_SOCKET_STUB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AF_INET    2
#define SOCK_DGRAM 2
#define INADDR_ANY ((uint32_t)0x00000000UL)

typedef uint32_t socklen_t;

struct in_addr {
    uint32_t s_addr;
};

struct sockaddr {
    unsigned short sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    unsigned short sin_family;
    uint16_t sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

uint16_t htons(uint16_t v);
uint16_t ntohs(uint16_t v);
int socket(int domain, int type, int protocol);
int bind(int s, const struct sockaddr* name, socklen_t namelen);
int sendto(int s, const void* dataptr, size_t size, int flags,
           const struct sockaddr* to, socklen_t tolen);
int recvfrom(int s, void* mem, size_t len, int flags,
             struct sockaddr* from, socklen_t* fromlen);
int closesocket(int s);

#ifdef __cplusplus
}
#endif

#endif /* OYK_LYRA_SOCKET_STUB_H */
