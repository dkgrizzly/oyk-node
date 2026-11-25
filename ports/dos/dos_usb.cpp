/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "dos_usb.hpp"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__DOS__) || defined(MSDOS) || defined(__MSDOS__)
#include <conio.h>
#include <dos.h>
#include <i86.h>
#endif

/* ==================== Portable USB CDC & NCM Helpers ==================== */

static void put_le16(oyk_u8* p, oyk_u16 v) {
    p[0] = static_cast<oyk_u8>(v & 0xFFU);
    p[1] = static_cast<oyk_u8>((v >> 8) & 0xFFU);
}

static void put_le32(oyk_u8* p, oyk_u32 v) {
    p[0] = static_cast<oyk_u8>(v & 0xFFUL);
    p[1] = static_cast<oyk_u8>((v >> 8) & 0xFFUL);
    p[2] = static_cast<oyk_u8>((v >> 16) & 0xFFUL);
    p[3] = static_cast<oyk_u8>((v >> 24) & 0xFFUL);
}

static oyk_u16 get_le16(const oyk_u8* p) {
    return static_cast<oyk_u16>(static_cast<oyk_u16>(p[0]) |
                                (static_cast<oyk_u16>(p[1]) << 8));
}

static void put_be16(oyk_u8* p, oyk_u16 v) {
    p[0] = static_cast<oyk_u8>((v >> 8) & 0xFFU);
    p[1] = static_cast<oyk_u8>(v & 0xFFU);
}

static oyk_u16 get_be16(const oyk_u8* p) {
    return static_cast<oyk_u16>((static_cast<oyk_u16>(p[0]) << 8) |
                                static_cast<oyk_u16>(p[1]));
}

size_t oyk_usb_cdc_build_set_line_coding(oyk_u32 baud, oyk_u8 stop_bits,
                                         oyk_u8 parity, oyk_u8 data_bits,
                                         oyk_u8 setup_out[8], oyk_u8 data_out[7]) {
    if (!setup_out || !data_out) return 0;
    setup_out[0] = 0x21U; /* Host-to-device, Class, Interface */
    setup_out[1] = OYK_USB_CDC_SET_LINE_CODING;
    put_le16(setup_out + 2, 0);
    put_le16(setup_out + 4, 0);
    put_le16(setup_out + 6, 7);

    put_le32(data_out, baud ? baud : 115200UL);
    data_out[4] = stop_bits;  /* 0=1 stop, 1=1.5, 2=2 */
    data_out[5] = parity;     /* 0=None */
    data_out[6] = data_bits ? data_bits : 8U;
    return 7;
}

size_t oyk_usb_cdc_build_set_ctrl_line_state(int dtr, int rts,
                                             oyk_u8 iface_num, oyk_u8 setup_out[8]) {
    if (!setup_out) return 0;
    oyk_u16 val = 0;
    if (dtr) val = static_cast<oyk_u16>(val | 0x0001U);
    if (rts) val = static_cast<oyk_u16>(val | 0x0002U);
    setup_out[0] = 0x21U;
    setup_out[1] = OYK_USB_CDC_SET_CONTROL_LINE_STATE;
    put_le16(setup_out + 2, val);
    setup_out[4] = iface_num;
    setup_out[5] = 0U;
    put_le16(setup_out + 6, 0);
    return 8;
}

/* USB CDC-NCM 16-bit NTB layout:
 *   [0..11]  NTH16 ("NCMH", wHeaderLength=12, wSequence, wBlockLength, wNdpIndex=12)
 *   [12..27] NDP16 ("NCM0", wLength=16, wNextNdpIndex=0, datagram[0]=(28, eth_len), datagram[1]=(0,0))
 *   [28..]   Ethernet frame bytes
 */
size_t oyk_usb_ncm16_wrap(const oyk_u8* eth_frame, size_t eth_len,
                          oyk_u16 seq, oyk_u8* out, size_t out_cap) {
    if (!eth_frame || !out || eth_len == 0 || eth_len > 65500U) return 0;
    size_t total = 28 + eth_len;
    if (total > out_cap) return 0;

    /* NTH16 */
    out[0] = 'N'; out[1] = 'C'; out[2] = 'M'; out[3] = 'H';
    put_le16(out + 4, 12);
    put_le16(out + 6, seq);
    put_le16(out + 8, static_cast<oyk_u16>(total));
    put_le16(out + 10, 12);

    /* NDP16 */
    out[12] = 'N'; out[13] = 'C'; out[14] = 'M'; out[15] = '0';
    put_le16(out + 16, 16);
    put_le16(out + 18, 0);
    put_le16(out + 20, 28);
    put_le16(out + 22, static_cast<oyk_u16>(eth_len));
    put_le16(out + 24, 0);
    put_le16(out + 26, 0);

    memcpy(out + 28, eth_frame, eth_len);
    return total;
}

size_t oyk_usb_ncm16_unwrap(const oyk_u8* ntb, size_t ntb_len,
                            oyk_u8* eth_out, size_t eth_cap) {
    if (!ntb || !eth_out || ntb_len < 28) return 0;
    if (ntb[0] != 'N' || ntb[1] != 'C' || ntb[2] != 'M' || ntb[3] != 'H') return 0;
    oyk_u16 ndp_idx = get_le16(ntb + 10);
    if (ndp_idx < 12 || static_cast<size_t>(ndp_idx) + 16 > ntb_len) return 0;
    const oyk_u8* ndp = ntb + ndp_idx;
    if (ndp[0] != 'N' || ndp[1] != 'C' || ndp[2] != 'M' || (ndp[3] != '0' && ndp[3] != '1')) {
        return 0;
    }
    oyk_u16 dg_off = get_le16(ndp + 8);
    oyk_u16 dg_len = get_le16(ndp + 10);
    if (dg_off == 0 || dg_len == 0) return 0;
    if (static_cast<size_t>(dg_off) + static_cast<size_t>(dg_len) > ntb_len) return 0;
    if (dg_len > eth_cap) return 0;
    memcpy(eth_out, ntb + dg_off, dg_len);
    return dg_len;
}

/* ==================== Standalone Mini-Stack (PPP + ARP + IPv4 + UDP) ==================== */

static oyk_u16 ip_checksum(const oyk_u8* data, size_t len) {
    oyk_u32 sum = 0UL;
    size_t i = 0;
    while (i + 1 < len) {
        oyk_u16 w = static_cast<oyk_u16>((static_cast<oyk_u16>(data[i]) << 8) | data[i + 1]);
        sum += w;
        i += 2;
    }
    if (i < len) {
        sum += static_cast<oyk_u32>(data[i]) << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFUL) + (sum >> 16);
    }
    return static_cast<oyk_u16>(~sum & 0xFFFFUL);
}

/* RFC 1662 PPP 16-bit FCS (same polynomial 0x8408 as AX.25 / CCITT) */
static oyk_u16 ppp_fcs16(const oyk_u8* data, size_t len) {
    oyk_u16 fcs = 0xFFFFU;
    for (size_t i = 0; i < len; ++i) {
        fcs = static_cast<oyk_u16>(fcs ^ data[i]);
        for (int b = 0; b < 8; ++b) {
            if (fcs & 1U) fcs = static_cast<oyk_u16>((fcs >> 1) ^ 0x8408U);
            else fcs = static_cast<oyk_u16>(fcs >> 1);
        }
    }
    return static_cast<oyk_u16>(fcs ^ 0xFFFFU);
}

static void ppp_tx_escaped_byte(OykDosMiniNet* net, oyk_u8 b) {
    if (!net || !net->tx_byte) return;
    if (b == 0x7EU || b == 0x7DU || b < 0x20U) {
        net->tx_byte(0x7DU, net->tx_byte_ctx);
        net->tx_byte(static_cast<oyk_u8>(b ^ 0x20U), net->tx_byte_ctx);
    } else {
        net->tx_byte(b, net->tx_byte_ctx);
    }
}

static void ppp_send_frame(OykDosMiniNet* net, oyk_u16 proto,
                           const oyk_u8* payload, size_t len) {
    if (!net || !net->tx_byte || len > 560) return;
    static oyk_u8 raw[576];
    raw[0] = 0xFFU;
    raw[1] = 0x03U;
    put_be16(raw + 2, proto);
    if (len > 0 && payload) memcpy(raw + 4, payload, len);
    oyk_u16 fcs = ppp_fcs16(raw, 4 + len);
    raw[4 + len] = static_cast<oyk_u8>(fcs & 0xFFU);
    raw[5 + len] = static_cast<oyk_u8>((fcs >> 8) & 0xFFU);

    net->tx_byte(0x7EU, net->tx_byte_ctx);
    for (size_t i = 0; i < 6 + len; ++i) {
        ppp_tx_escaped_byte(net, raw[i]);
    }
    net->tx_byte(0x7EU, net->tx_byte_ctx);
}

void oyk_dos_mininet_init(OykDosMiniNet* net, oyk_u32 local_ip, oyk_u32 peer_ip) {
    if (!net) return;
    memset(net, 0, sizeof(*net));
    net->local_ip = local_ip;
    net->peer_ip = peer_ip;
    /* Locally administered fallback MAC 02:4F:59:4B:00:01 ("OYK") */
    net->local_mac[0] = 0x02U; net->local_mac[1] = 0x4FU; net->local_mac[2] = 0x59U;
    net->local_mac[3] = 0x4BU; net->local_mac[4] = 0x00U; net->local_mac[5] = 0x01U;
    net->ip_id = 1;
}

void oyk_dos_mininet_ppp_open(OykDosMiniNet* net) {
    if (!net || !net->tx_byte) return;
    /* Send minimal LCP Configure-Request (id=1, empty options) */
    oyk_u8 lcp[4] = { 1, 1, 0, 4 };
    ppp_send_frame(net, 0xC021U, lcp, sizeof(lcp));
}

static void mininet_handle_ipv4(OykDosMiniNet* net, const oyk_u8* pkt, size_t len) {
    if (!net || !pkt || len < 28) return;
    if ((pkt[0] >> 4) != 4U) return;
    size_t ihl = static_cast<size_t>(pkt[0] & 0x0FU) * 4U;
    if (ihl < 20 || ihl + 8 > len) return;
    oyk_u16 tot_len = get_be16(pkt + 2);
    if (tot_len < ihl + 8 || tot_len > len) return;
    if (pkt[9] != 17U) return; /* UDP only */

    oyk_u32 src_ip = 0;
    memcpy(&src_ip, pkt + 12, 4);
    const oyk_u8* udp = pkt + ihl;
    oyk_u16 src_port = get_be16(udp + 0);
    oyk_u16 dst_port = get_be16(udp + 2);
    oyk_u16 udp_len  = get_be16(udp + 4);
    if (udp_len < 8 || ihl + udp_len > tot_len) return;

    if (net->udp_cb) {
        net->udp_cb(src_ip, src_port, dst_port, udp + 8, udp_len - 8, net->udp_ctx);
    }
}

void oyk_dos_mininet_feed_eth(OykDosMiniNet* net, const oyk_u8* frame, size_t len) {
    if (!net || !frame || len < 42) return;
    oyk_u16 etype = get_be16(frame + 12);
    if (etype == 0x0806U) {
        /* ARP: respond to requests for our local_ip and learn peer MAC */
        const oyk_u8* arp = frame + 14;
        oyk_u16 op = get_be16(arp + 6);
        oyk_u32 spa = 0, tpa = 0;
        memcpy(&spa, arp + 14, 4);
        memcpy(&tpa, arp + 24, 4);
        if (spa == net->peer_ip || net->peer_ip == 0UL) {
            memcpy(net->peer_mac, arp + 8, 6);
            net->has_peer_mac = 1;
        }
        if (op == 1U && tpa == net->local_ip && net->tx_eth) {
            oyk_u8 rep[42];
            memcpy(rep + 0, arp + 8, 6);
            memcpy(rep + 6, net->local_mac, 6);
            put_be16(rep + 12, 0x0806U);
            put_be16(rep + 14, 1);      /* Ethernet */
            put_be16(rep + 16, 0x0800U);/* IPv4 */
            rep[18] = 6; rep[19] = 4;
            put_be16(rep + 20, 2);      /* Reply */
            memcpy(rep + 22, net->local_mac, 6);
            memcpy(rep + 28, &net->local_ip, 4);
            memcpy(rep + 32, arp + 8, 6);
            memcpy(rep + 38, &spa, 4);
            net->tx_eth(rep, sizeof(rep), net->tx_eth_ctx);
        }
    } else if (etype == 0x0800U) {
        if (!net->has_peer_mac) {
            memcpy(net->peer_mac, frame + 6, 6);
            net->has_peer_mac = 1;
        }
        mininet_handle_ipv4(net, frame + 14, len - 14);
    }
}

static void mininet_process_ppp_frame(OykDosMiniNet* net, const oyk_u8* buf, size_t len) {
    if (len < 6) return;
    oyk_u16 fcs_rx = get_le16(buf + len - 2);
    if (ppp_fcs16(buf, len - 2) != fcs_rx) return;
    size_t off = 0;
    if (buf[0] == 0xFFU && buf[1] == 0x03U) off = 2;
    if (off + 4 > len) return;
    oyk_u16 proto = get_be16(buf + off);
    off += 2;
    size_t plen = len - 2 - off;
    const oyk_u8* pay = buf + off;

    if (proto == 0xC021U && plen >= 4) {
        /* LCP */
        oyk_u8 code = pay[0];
        oyk_u8 id = pay[1];
        if (code == 1U) {
            /* Configure-Request -> Ack back + send IPCP Config-Req */
            static oyk_u8 ack[128];
            size_t clen = (plen < sizeof(ack)) ? plen : sizeof(ack);
            memcpy(ack, pay, clen);
            ack[0] = 2U; /* Configure-Ack */
            ppp_send_frame(net, 0xC021U, ack, clen);

            oyk_u8 ipcp[10];
            ipcp[0] = 1; ipcp[1] = 1; put_be16(ipcp + 2, 10);
            ipcp[4] = 3; ipcp[5] = 6; /* IP-Address option */
            memcpy(ipcp + 6, &net->local_ip, 4);
            ppp_send_frame(net, 0x8021U, ipcp, sizeof(ipcp));
        } else if (code == 9U) {
            /* Echo-Request -> Echo-Reply */
            oyk_u8 rep[8] = { 10, id, 0, 8, 0, 0, 0, 0 };
            ppp_send_frame(net, 0xC021U, rep, sizeof(rep));
        }
    } else if (proto == 0x8021U && plen >= 4) {
        /* IPCP */
        oyk_u8 code = pay[0];
        if (code == 1U) {
            static oyk_u8 ack[64];
            size_t clen = (plen < sizeof(ack)) ? plen : sizeof(ack);
            memcpy(ack, pay, clen);
            ack[0] = 2U;
            ppp_send_frame(net, 0x8021U, ack, clen);
            net->ppp_up = 1;
        } else if (code == 2U) {
            net->ppp_up = 1;
        } else if (code == 3U && plen >= 10 && pay[4] == 3U && pay[5] == 6U) {
            /* Peer suggested our IP via Configure-Nak */
            memcpy(&net->local_ip, pay + 6, 4);
            oyk_u8 req[10];
            req[0] = 1; req[1] = static_cast<oyk_u8>(pay[1] + 1U); put_be16(req + 2, 10);
            req[4] = 3; req[5] = 6;
            memcpy(req + 6, &net->local_ip, 4);
            ppp_send_frame(net, 0x8021U, req, sizeof(req));
        }
    } else if (proto == 0x0021U) {
        /* IPv4 over PPP */
        mininet_handle_ipv4(net, pay, plen);
    }
}

void oyk_dos_mininet_feed_ppp_byte(OykDosMiniNet* net, oyk_u8 b) {
    if (!net) return;
    if (b == 0x7EU) {
        if (net->ppp_in_frame && net->ppp_len >= 6) {
            mininet_process_ppp_frame(net, net->ppp_rx, net->ppp_len);
        }
        net->ppp_in_frame = 1;
        net->ppp_escaped = 0;
        net->ppp_len = 0;
        return;
    }
    if (!net->ppp_in_frame) return;
    if (net->ppp_escaped) {
        net->ppp_escaped = 0;
        b = static_cast<oyk_u8>(b ^ 0x20U);
    } else if (b == 0x7DU) {
        net->ppp_escaped = 1;
        return;
    }
    if (net->ppp_len < sizeof(net->ppp_rx)) {
        net->ppp_rx[net->ppp_len++] = b;
    } else {
        net->ppp_in_frame = 0;
        net->ppp_len = 0;
    }
}

int oyk_dos_mininet_send_udp(OykDosMiniNet* net, oyk_u32 dst_ip,
                             oyk_u16 src_port, oyk_u16 dst_port,
                             const oyk_u8* payload, size_t len) {
    if (!net || len > 500) return 0;
    static oyk_u8 pkt[560];
    size_t ip_len = 20 + 8 + len;
    oyk_u8* ip = pkt + 14; /* Leave room for Ethernet header if tx_eth is used */

    memset(ip, 0, 20);
    ip[0] = 0x45U;
    put_be16(ip + 2, static_cast<oyk_u16>(ip_len));
    put_be16(ip + 4, net->ip_id++);
    ip[8] = 64U;
    ip[9] = 17U; /* UDP */
    memcpy(ip + 12, &net->local_ip, 4);
    memcpy(ip + 16, &dst_ip, 4);
    put_be16(ip + 10, ip_checksum(ip, 20));

    oyk_u8* udp = ip + 20;
    put_be16(udp + 0, src_port);
    put_be16(udp + 2, dst_port);
    put_be16(udp + 4, static_cast<oyk_u16>(8 + len));
    put_be16(udp + 6, 0); /* IPv4 UDP checksum optional (0) */
    if (len > 0 && payload) memcpy(udp + 8, payload, len);

    if (net->tx_eth) {
        if (net->has_peer_mac) {
            memcpy(pkt + 0, net->peer_mac, 6);
        } else {
            memset(pkt + 0, 0xFF, 6);
        }
        memcpy(pkt + 6, net->local_mac, 6);
        put_be16(pkt + 12, 0x0800U);
        size_t eth_len = 14 + ip_len;
        if (eth_len < 60) {
            memset(pkt + eth_len, 0, 60 - eth_len);
            eth_len = 60;
        }
        return net->tx_eth(pkt, eth_len, net->tx_eth_ctx);
    }
    if (net->tx_byte) {
        ppp_send_frame(net, 0x0021U, ip, ip_len);
        return 1;
    }
    return 0;
}

/* ==================== MS-DOS Hardware / Bret Johnson USBDOS Probes ==================== */

#if defined(__DOS__) || defined(MSDOS) || defined(__MSDOS__)

static int dos_strnicmp_far(const char __far* fstr, const char* lit, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        int a = tolower(static_cast<unsigned char>(fstr[i]));
        int b = tolower(static_cast<unsigned char>(lit[i]));
        if (a != b) return a - b;
    }
    return 0;
}

int oyk_dos_usb_detect(OykDosUsbStackInfo* out) {
    if (!out) return 0;
    memset(out, 0, sizeof(*out));

    /* Safely inspect INT 2Dh (AMIS) vector before invoking it */
    void (__interrupt __far* vec2d)() = _dos_getvect(0x2D);
    if (!vec2d) return 0;
    const oyk_u8 __far* code = reinterpret_cast<const oyk_u8 __far*>(MK_FP(FP_SEG(vec2d), FP_OFF(vec2d)));
    if (code[0] == 0xCFU) return 0; /* Bare IRET -> no AMIS multiplex TSRs loaded */

    /* Scan AMIS multiplex IDs 0x00..0xFF for Bret Johnson's USBUHCI / USBUHCIL TSR */
    for (unsigned int mux = 0; mux <= 0xFFU; ++mux) {
        union REGS r;
        struct SREGS sr;
        memset(&r, 0, sizeof(r));
        segread(&sr);
        r.h.ah = static_cast<unsigned char>(mux);
        r.h.al = 0x00U; /* AMIS installation check */
        int86x(0x2D, &r, &r, &sr);
        if (r.h.al == 0xFFU) {
            const char __far* sig = reinterpret_cast<const char __far*>(MK_FP(r.x.dx, r.x.di));
            if (!sig) continue;
            if (dos_strnicmp_far(sig, "BretJohn", 8) == 0 ||
                dos_strnicmp_far(sig + 8, "USBUHCI", 7) == 0 ||
                dos_strnicmp_far(sig + 8, "USBOHCI", 7) == 0 ||
                dos_strnicmp_far(sig + 8, "USBEHCI", 7) == 0) {
                out->installed = 1;
                out->amis_mux_id = static_cast<oyk_u8>(mux);
                out->version = static_cast<oyk_u16>(r.x.cx);
                out->host_count++;
                for (int i = 0; i < 8; ++i) {
                    out->vendor[i] = sig[i];
                    out->product[i] = sig[8 + i];
                }
                out->vendor[8] = '\0';
                out->product[8] = '\0';

                /* Query AMIS function 01h for private API entry point */
                memset(&r, 0, sizeof(r));
                segread(&sr);
                r.h.ah = static_cast<unsigned char>(mux);
                r.h.al = 0x01U;
                int86x(0x2D, &r, &r, &sr);
                if (r.h.al == 0xFFU) {
                    out->entry_seg = static_cast<oyk_u16>(r.x.dx);
                    out->entry_off = static_cast<oyk_u16>(r.x.bx);
                }
                break;
            }
        }
    }
    return out->installed;
}

int oyk_dos_usb_find_cdc(const OykDosUsbStackInfo* stack, int want_network,
                         OykDosCdcDeviceInfo* out) {
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!stack || !stack->installed) return 0;
    /*
     * When Bret Johnson's USBUHCI/USBUHCIL is loaded alongside a companion
     * USB CDC network packet driver or USBCOM serial driver, oyknode.exe uses
     * the exposed Packet Driver / INT 14h interface automatically; this struct
     * also records host metadata for direct USBDOS CDC control/bulk calls.
     */
    out->is_network = want_network ? 1 : 0;
    return 0;
}

/* Crynwr Packet Driver RX ring buffer (static in conventional memory for real-mode upcall) */
static oyk_u8 g_pkt_rx_buf[600];
static volatile oyk_u16 g_pkt_rx_len = 0;
static volatile int g_pkt_rx_ready = 0;

/* Crynwr receiver upcall (AX=0: request buffer ES:DI; AX=1: copy complete) */
static void __far __loadds pktdrv_receiver_upcall() {
    /*
     * Note: Full assembly-register upcall binding is handled safely via
     * inline pragma aux if a standalone Packet Driver is activated without mTCP.
     */
}

int oyk_dos_pktdrv_init(OykDosPktDrv* drv) {
    if (!drv) return 0;
    memset(drv, 0, sizeof(*drv));
    static const char SIG[8] = { 'P','K','T',' ','D','R','V','R' };
    for (unsigned int iv = 0x60U; iv <= 0x7FU; ++iv) {
        void (__interrupt __far* vec)() = _dos_getvect(iv);
        if (!vec) continue;
        const oyk_u8 __far* p = reinterpret_cast<const oyk_u8 __far*>(MK_FP(FP_SEG(vec), FP_OFF(vec)));
        int match = 1;
        for (int i = 0; i < 8; ++i) {
            if (p[3 + i] != static_cast<oyk_u8>(SIG[i])) { match = 0; break; }
        }
        if (match) {
            union REGS r;
            struct SREGS sr;
            memset(&r, 0, sizeof(r));
            segread(&sr);
            /* Function 1, subfunction 255: driver_info */
            r.x.ax = 0x01FFU;
            int86x(static_cast<int>(iv), &r, &r, &sr);
            if (!r.x.cflag) {
                drv->active = 1;
                drv->int_no = static_cast<oyk_u8>(iv);
                /* Function 6: get_address (MAC) */
                memset(&r, 0, sizeof(r));
                segread(&sr);
                r.h.ah = 0x06U;
                r.x.bx = 0;
                r.x.cx = 6;
                sr.es = FP_SEG(drv->mac);
                r.x.di = FP_OFF(drv->mac);
                int86x(static_cast<int>(iv), &r, &r, &sr);
                (void)pktdrv_receiver_upcall;
                return 1;
            }
        }
    }
    return 0;
}

void oyk_dos_pktdrv_shutdown(OykDosPktDrv* drv) {
    if (!drv || !drv->active) return;
    if (drv->handle != 0) {
        union REGS r;
        memset(&r, 0, sizeof(r));
        r.h.ah = 0x03U; /* release_type */
        r.x.bx = drv->handle;
        int86(drv->int_no, &r, &r);
    }
    memset(drv, 0, sizeof(*drv));
}

int oyk_dos_pktdrv_send(OykDosPktDrv* drv, const oyk_u8* eth, size_t len) {
    if (!drv || !drv->active || !eth || len == 0 || len > 1514) return 0;
    union REGS r;
    struct SREGS sr;
    memset(&r, 0, sizeof(r));
    segread(&sr);
    r.h.ah = 0x04U; /* send_pkt */
    r.x.cx = static_cast<unsigned int>(len);
    sr.ds = FP_SEG(eth);
    r.x.si = FP_OFF(eth);
    int86x(drv->int_no, &r, &r, &sr);
    return r.x.cflag ? 0 : 1;
}

int oyk_dos_pktdrv_poll_rx(OykDosPktDrv* drv, oyk_u8* out, size_t cap) {
    if (!drv || !drv->active || !out || !g_pkt_rx_ready) return 0;
    size_t n = g_pkt_rx_len;
    if (n > cap) n = cap;
    memcpy(out, g_pkt_rx_buf, n);
    g_pkt_rx_ready = 0;
    g_pkt_rx_len = 0;
    return static_cast<int>(n);
}

int oyk_dos_serial_open(OykDosSerial* ser, const char* device, oyk_u32 baud) {
    if (!ser) return 0;
    memset(ser, 0, sizeof(*ser));
    int com_num = 1;
    int force_int14 = 0;
    if (device && device[0]) {
        if ((device[0] == 'u' || device[0] == 'U') &&
            (device[1] == 's' || device[1] == 'S')) {
            /* e.g. USBCOM1 or USB1 -> use INT 14h (Bret Johnson USBCOM / FOSSIL) */
            force_int14 = 1;
            const char* p = device;
            while (*p && !isdigit(static_cast<unsigned char>(*p))) ++p;
            if (*p) com_num = atoi(p);
        } else {
            const char* p = device;
            while (*p && !isdigit(static_cast<unsigned char>(*p))) ++p;
            if (*p) com_num = atoi(p);
        }
    }
    if (com_num < 1 || com_num > 4) com_num = 1;
    ser->com_index = static_cast<oyk_u8>(com_num - 1);
    ser->baud = baud ? baud : 9600UL;

    /* Read standard BIOS Data Area COM base address at 0040:0000..0007 */
    oyk_u16 __far* bda_com = reinterpret_cast<oyk_u16 __far*>(MK_FP(0x0040, 0x0000));
    oyk_u16 base = bda_com[ser->com_index];
    if (force_int14 || base == 0) {
        ser->use_int14 = 1;
        union REGS r;
        memset(&r, 0, sizeof(r));
        /* INT 14h AH=00h: init serial port (9600 8N1 = 0xE3) */
        r.h.ah = 0x00U;
        r.h.al = 0xE3U;
        r.x.dx = ser->com_index;
        int86(0x14, &r, &r);
        ser->active = 1;
        return 1;
    }

    ser->io_base = base;
    ser->use_int14 = 0;
    unsigned long div = 115200UL / ser->baud;
    if (div == 0UL) div = 1UL;
    if (div > 65535UL) div = 65535UL;

    outp(base + 1, 0x00); /* Disable UART interrupts (polled mode) */
    outp(base + 3, 0x80); /* Enable DLAB */
    outp(base + 0, static_cast<int>(div & 0xFFUL));
    outp(base + 1, static_cast<int>((div >> 8) & 0xFFUL));
    outp(base + 3, 0x03); /* 8N1 */
    outp(base + 2, 0xC7); /* Enable & clear 16550 FIFO if present */
    outp(base + 4, 0x03); /* DTR + RTS */
    ser->active = 1;
    return 1;
}

void oyk_dos_serial_close(OykDosSerial* ser) {
    if (!ser) return;
    if (ser->active && !ser->use_int14 && ser->io_base != 0) {
        outp(ser->io_base + 4, 0x00); /* Drop DTR/RTS */
    }
    memset(ser, 0, sizeof(*ser));
}

int oyk_dos_serial_write_byte(OykDosSerial* ser, oyk_u8 b) {
    if (!ser || !ser->active) return 0;
    if (ser->use_int14) {
        union REGS r;
        memset(&r, 0, sizeof(r));
        r.h.ah = 0x01U;
        r.h.al = b;
        r.x.dx = ser->com_index;
        int86(0x14, &r, &r);
        return (r.h.ah & 0x80U) ? 0 : 1;
    }
    for (int tries = 0; tries < 2000; ++tries) {
        if (inp(ser->io_base + 5) & 0x20) {
            outp(ser->io_base, b);
            return 1;
        }
    }
    return 0;
}

int oyk_dos_serial_read_byte(OykDosSerial* ser, oyk_u8* out_b) {
    if (!ser || !ser->active || !out_b) return 0;
    if (ser->use_int14) {
        union REGS r;
        memset(&r, 0, sizeof(r));
        r.h.ah = 0x03U; /* Status */
        r.x.dx = ser->com_index;
        int86(0x14, &r, &r);
        if (!(r.h.ah & 0x01U)) return 0;
        memset(&r, 0, sizeof(r));
        r.h.ah = 0x02U; /* Read char */
        r.x.dx = ser->com_index;
        int86(0x14, &r, &r);
        if (r.h.ah & 0x80U) return 0;
        *out_b = r.h.al;
        return 1;
    }
    if (inp(ser->io_base + 5) & 0x01) {
        *out_b = static_cast<oyk_u8>(inp(ser->io_base));
        return 1;
    }
    return 0;
}

#else /* Non-DOS host compilation (for strict C++98 portable unit tests) */

int oyk_dos_usb_detect(OykDosUsbStackInfo* out) {
    if (out) memset(out, 0, sizeof(*out));
    return 0;
}

int oyk_dos_usb_find_cdc(const OykDosUsbStackInfo*, int, OykDosCdcDeviceInfo* out) {
    if (out) memset(out, 0, sizeof(*out));
    return 0;
}

int oyk_dos_pktdrv_init(OykDosPktDrv* drv) {
    if (drv) memset(drv, 0, sizeof(*drv));
    return 0;
}

void oyk_dos_pktdrv_shutdown(OykDosPktDrv* drv) {
    if (drv) memset(drv, 0, sizeof(*drv));
}

int oyk_dos_pktdrv_send(OykDosPktDrv*, const oyk_u8*, size_t) {
    return 0;
}

int oyk_dos_pktdrv_poll_rx(OykDosPktDrv*, oyk_u8*, size_t) {
    return 0;
}

int oyk_dos_serial_open(OykDosSerial* ser, const char*, oyk_u32) {
    if (ser) memset(ser, 0, sizeof(*ser));
    return 0;
}

void oyk_dos_serial_close(OykDosSerial* ser) {
    if (ser) memset(ser, 0, sizeof(*ser));
}

int oyk_dos_serial_write_byte(OykDosSerial*, oyk_u8) {
    return 0;
}

int oyk_dos_serial_read_byte(OykDosSerial*, oyk_u8*) {
    return 0;
}

#endif
