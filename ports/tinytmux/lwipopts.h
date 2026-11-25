/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * lwIP configuration for OYK Packet Node on IMXRT1060 (Teensy 4.1 / TinyTMUX).
 * lwIP is licensed under the 3-clause BSD license (GPL-3.0-or-later compatible).
 *
 * Enables:
 *   - 10/100 Ethernet (IMXRT1060 ENET + DP83825I RMII PHY)
 *   - PPPoS (RFC 1661/1662/1332 PPP over Serial for RadioFoundry firmware radios)
 *   - UDP (AXUDP), TCP (Incoming Telnet), and RAW IP (AXIP protocol 93 / RFC 1226)
 */

#ifndef OYK_TINYTMUX_LWIPOPTS_H
#define OYK_TINYTMUX_LWIPOPTS_H

/* Bare-metal polling / superloop mode (no RTOS required) */
#define NO_SYS                          1
#define LWIP_SOCKET                     0
#define LWIP_NETCONN                    0
#define SYS_LIGHTWEIGHT_PROT            0

/* Memory pools (sized comfortably for IMXRT1062 512KB OCRAM2 + 512KB FlexRAM) */
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        (32 * 1024)
#define MEMP_NUM_PBUF                   32
#define MEMP_NUM_UDP_PCB                12
#define MEMP_NUM_TCP_PCB                16
#define MEMP_NUM_TCP_PCB_LISTEN         4
#define MEMP_NUM_TCP_SEG                32
#define MEMP_NUM_RAW_PCB                4
#define MEMP_NUM_SYS_TIMEOUT            16
#define PBUF_POOL_SIZE                  40
#define PBUF_POOL_BUFSIZE               1536

/* Core IPv4 & Transport Protocols */
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_ICMP                       1
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define LWIP_RAW                        1   /* Required for AXIP (IP Protocol 93 / RFC 1226) */
#define LWIP_DHCP                       1
#define LWIP_AUTOIP                     0
#define LWIP_DNS                        1
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_NETIF_HOSTNAME             1

/* TCP tuning for incoming Telnet console */
#define TCP_MSS                         1460
#define TCP_WND                         (4 * TCP_MSS)
#define TCP_SND_BUF                     (4 * TCP_MSS)
#define TCP_SND_QUEUELEN                16

/* PPP over Serial (PPPoS) for RadioFoundry firmware radios */
#define PPP_SUPPORT                     1
#define PPPOS_SUPPORT                   1
#define PPPOE_SUPPORT                   0
#define PPPOL2TP_SUPPORT                0
#define PPP_IPV4_SUPPORT                1
#define PPP_IPV6_SUPPORT                0
#define PAP_SUPPORT                     1
#define CHAP_SUPPORT                    1
#define MSCHAP_SUPPORT                  0
#define CCP_SUPPORT                     0
#define VJ_SUPPORT                      0
#define PPP_NOTIFY_PHASE                1
#define PPP_INPROC_IRQ_SAFE             0
#define PPP_MAXIDLEFLAG                 100

/* Checksums */
#define CHECKSUM_GEN_IP                 1
#define CHECKSUM_GEN_UDP                1
#define CHECKSUM_GEN_TCP                1
#define CHECKSUM_CHECK_IP               1
#define CHECKSUM_CHECK_UDP              1
#define CHECKSUM_CHECK_TCP              1

/* Diagnostics disabled in release firmware */
#define LWIP_DEBUG                      0
#define LWIP_STATS                      0

#endif /* OYK_TINYTMUX_LWIPOPTS_H */
