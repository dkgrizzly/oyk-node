/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef _OYK_PICO_LWIPOPTS_H
#define _OYK_PICO_LWIPOPTS_H

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define LWIP_RAW                    1
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_ICMP                   1
#define LWIP_DHCP                   1
#define LWIP_DNS                    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_IGMP                   0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    16384
#define MEMP_NUM_PBUF               16
#define MEMP_NUM_UDP_PCB            8
#define MEMP_NUM_TCP_PCB            8
#define MEMP_NUM_TCP_SEG            16
#define PBUF_POOL_SIZE              16
#define TCP_MSS                     1460
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_SND_BUF                 (4 * TCP_MSS)
#define LWIP_TIMEVAL_PRIVATE        0

#endif /* _OYK_PICO_LWIPOPTS_H */
