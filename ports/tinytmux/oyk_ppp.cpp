/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "oyk_ppp.hpp"

#include <stdio.h>
#include <string.h>

/* RFC 1662 constants */
#define PPP_FLAG   0x7EU
#define PPP_ESCAPE 0x7DU
#define PPP_XOR    0x20U
#define PPP_ADDR   0xFFU
#define PPP_CTRL   0x03U

/* LCP / IPCP codes */
#define PPP_CONF_REQ   1U
#define PPP_CONF_ACK   2U
#define PPP_CONF_NAK   3U
#define PPP_CONF_REJ   4U
#define PPP_TERM_REQ   5U
#define PPP_TERM_ACK   6U
#define PPP_ECHO_REQ   9U
#define PPP_ECHO_REP   10U

/* LCP option types */
#define LCP_OPT_MRU    1U
#define LCP_OPT_ACCM   2U
#define LCP_OPT_MAGIC  5U
#define LCP_OPT_PFC    7U
#define LCP_OPT_ACFC   8U

/* IPCP option types */
#define IPCP_OPT_COMP  2U
#define IPCP_OPT_ADDR  3U

static oyk_u16 read_be16(const oyk_u8* p) {
    return static_cast<oyk_u16>((static_cast<oyk_u16>(p[0]) << 8) | p[1]);
}

static void write_be16(oyk_u8* p, oyk_u16 v) {
    p[0] = static_cast<oyk_u8>((v >> 8) & 0xFFU);
    p[1] = static_cast<oyk_u8>(v & 0xFFU);
}

static oyk_u32 read_be32(const oyk_u8* p) {
    return (static_cast<oyk_u32>(p[0]) << 24) |
           (static_cast<oyk_u32>(p[1]) << 16) |
           (static_cast<oyk_u32>(p[2]) << 8)  |
           static_cast<oyk_u32>(p[3]);
}

static void write_be32(oyk_u8* p, oyk_u32 v) {
    p[0] = static_cast<oyk_u8>((v >> 24) & 0xFFU);
    p[1] = static_cast<oyk_u8>((v >> 16) & 0xFFU);
    p[2] = static_cast<oyk_u8>((v >> 8) & 0xFFU);
    p[3] = static_cast<oyk_u8>(v & 0xFFU);
}

oyk_u16 oyk_ppp_fcs16(const oyk_u8* data, size_t len) {
    /* RFC 1662 Appendix C: CRC-16/X-25 matches AX.25 oyk_crc16_fcs() */
    return oyk_crc16_fcs(data, len);
}

oyk_u16 oyk_ipv4_checksum(const oyk_u8* data, size_t len) {
    oyk_u32 sum = 0;
    size_t i = 0;
    while (i + 1 < len) {
        sum += read_be16(data + i);
        i += 2;
    }
    if (i < len) {
        sum += static_cast<oyk_u32>(data[i]) << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFUL) + (sum >> 16);
    }
    return static_cast<oyk_u16>(~sum & 0xFFFFU);
}

static int needs_escape(oyk_u8 b, oyk_u32 accm) {
    if (b == PPP_FLAG || b == PPP_ESCAPE) return 1;
    if (b < 0x20U && ((accm >> b) & 1UL) != 0UL) return 1;
    return 0;
}

static int append_stuffed(oyk_u8 b, oyk_u32 accm, oyk_u8* out, size_t cap, size_t* pos) {
    if (needs_escape(b, accm)) {
        if (*pos + 2 > cap) return 0;
        out[(*pos)++] = PPP_ESCAPE;
        out[(*pos)++] = static_cast<oyk_u8>(b ^ PPP_XOR);
        return 1;
    }
    if (*pos + 1 > cap) return 0;
    out[(*pos)++] = b;
    return 1;
}

size_t oyk_ppp_hdlc_encode(oyk_u16 protocol,
                           const oyk_u8* payload, size_t payload_len,
                           oyk_u32 tx_accm,
                           oyk_u8* out, size_t out_cap) {
    if (!out || out_cap < 10) return 0;

    /* Compute FCS over: FF 03 proto_hi proto_lo payload... */
    oyk_u8 hdr[4];
    hdr[0] = PPP_ADDR;
    hdr[1] = PPP_CTRL;
    hdr[2] = static_cast<oyk_u8>((protocol >> 8) & 0xFFU);
    hdr[3] = static_cast<oyk_u8>(protocol & 0xFFU);

    oyk_u16 crc = 0xFFFFU;
    for (size_t i = 0; i < 4; ++i) {
        crc ^= hdr[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 1U) crc = static_cast<oyk_u16>((crc >> 1) ^ 0x8408U);
            else crc = static_cast<oyk_u16>(crc >> 1);
        }
    }
    for (size_t i = 0; i < payload_len; ++i) {
        crc ^= payload[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 1U) crc = static_cast<oyk_u16>((crc >> 1) ^ 0x8408U);
            else crc = static_cast<oyk_u16>(crc >> 1);
        }
    }
    oyk_u16 fcs = static_cast<oyk_u16>(crc ^ 0xFFFFU);

    /* RFC 1662: LCP frames ALWAYS escape 0x00..0x1F (ACCM = 0xFFFFFFFF) */
    oyk_u32 eff_accm = (protocol == OYK_PPP_PROTO_LCP) ? 0xFFFFFFFFUL : tx_accm;

    size_t pos = 0;
    out[pos++] = PPP_FLAG;
    for (size_t i = 0; i < 4; ++i) {
        if (!append_stuffed(hdr[i], eff_accm, out, out_cap, &pos)) return 0;
    }
    for (size_t i = 0; i < payload_len; ++i) {
        if (!append_stuffed(payload[i], eff_accm, out, out_cap, &pos)) return 0;
    }
    if (!append_stuffed(static_cast<oyk_u8>(fcs & 0xFFU), eff_accm, out, out_cap, &pos)) return 0;
    if (!append_stuffed(static_cast<oyk_u8>((fcs >> 8) & 0xFFU), eff_accm, out, out_cap, &pos)) return 0;
    if (pos + 1 > out_cap) return 0;
    out[pos++] = PPP_FLAG;
    return pos;
}

OykPppLink::OykPppLink()
    : state_(OYK_PPP_DOWN),
      local_ip_(0), desired_peer_ip_(0), remote_ip_(0),
      axudp_port_(10093), magic_(0x4F594B01UL),
      tx_accm_(0xFFFFFFFFUL), ip_ident_(1),
      lcp_local_acked_(0), lcp_peer_acked_(0),
      ipcp_local_acked_(0), ipcp_peer_acked_(0),
      next_id_(1), last_req_ms_(0),
      tx_fn_(NULL), tx_ctx_(NULL),
      ax25_rx_cb_(NULL), ax25_rx_ctx_(NULL),
      raw_ip_rx_cb_(NULL), raw_ip_rx_ctx_(NULL),
      udp_rx_cb_(NULL), udp_rx_ctx_(NULL),
      rx_in_frame_(0), rx_escaped_(0), rx_overflow_(0), rx_len_(0),
      rx_frames_(0), tx_frames_(0), rx_fcs_errors_(0) {
}

void OykPppLink::init(oyk_u32 local_ip_net,
                      oyk_u32 peer_ip_net,
                      oyk_u16 axudp_listen_port,
                      oyk_u32 magic_number,
                      OykPppSerialTxFn tx_fn, void* tx_ctx) {
    state_ = OYK_PPP_DOWN;
    local_ip_ = local_ip_net;
    desired_peer_ip_ = peer_ip_net;
    remote_ip_ = peer_ip_net;
    axudp_port_ = axudp_listen_port ? axudp_listen_port : static_cast<oyk_u16>(10093);
    magic_ = magic_number ? magic_number : 0x4F594B01UL;
    tx_accm_ = 0xFFFFFFFFUL;
    ip_ident_ = 1;
    lcp_local_acked_ = 0;
    lcp_peer_acked_ = 0;
    ipcp_local_acked_ = 0;
    ipcp_peer_acked_ = 0;
    next_id_ = 1;
    last_req_ms_ = 0;
    tx_fn_ = tx_fn;
    tx_ctx_ = tx_ctx;
    rx_in_frame_ = 0;
    rx_escaped_ = 0;
    rx_overflow_ = 0;
    rx_len_ = 0;
    rx_frames_ = 0;
    tx_frames_ = 0;
    rx_fcs_errors_ = 0;
}

void OykPppLink::set_ax25_rx_callback(OykPppAx25RxFn cb, void* ctx) {
    ax25_rx_cb_ = cb;
    ax25_rx_ctx_ = ctx;
}

void OykPppLink::set_raw_ip_rx_callback(OykPppRawIpRxFn cb, void* ctx) {
    raw_ip_rx_cb_ = cb;
    raw_ip_rx_ctx_ = ctx;
}

void OykPppLink::set_udp_rx_callback(OykPppUdpRxFn cb, void* ctx) {
    udp_rx_cb_ = cb;
    udp_rx_ctx_ = ctx;
}

void OykPppLink::open(oyk_u32 now_ms) {
    state_ = OYK_PPP_LCP_OPENING;
    tx_accm_ = 0xFFFFFFFFUL;
    lcp_local_acked_ = 0;
    lcp_peer_acked_ = 0;
    ipcp_local_acked_ = 0;
    ipcp_peer_acked_ = 0;
    send_lcp_conf_req(now_ms);
}

void OykPppLink::close() {
    if (state_ != OYK_PPP_DOWN) {
        oyk_u8 pkt[4];
        pkt[0] = PPP_TERM_REQ;
        pkt[1] = next_id_++;
        write_be16(pkt + 2, 4);
        send_ppp_frame(OYK_PPP_PROTO_LCP, pkt, 4);
    }
    state_ = OYK_PPP_DOWN;
    lcp_local_acked_ = 0;
    lcp_peer_acked_ = 0;
    ipcp_local_acked_ = 0;
    ipcp_peer_acked_ = 0;
}

void OykPppLink::send_ppp_frame(oyk_u16 protocol, const oyk_u8* payload, size_t len) {
    if (!tx_fn_) return;
    static oyk_u8 wire[OYK_PPP_MAX_FRAME * 2 + 16];
    size_t n = oyk_ppp_hdlc_encode(protocol, payload, len, tx_accm_, wire, sizeof(wire));
    if (n > 0) {
        tx_fn_(wire, n, tx_ctx_);
        tx_frames_++;
    }
}

void OykPppLink::send_lcp_conf_req(oyk_u32 now_ms) {
    /* LCP Configure-Request: ACCM=0x00000000 + Magic Number */
    oyk_u8 pkt[16];
    pkt[0] = PPP_CONF_REQ;
    pkt[1] = next_id_++;
    write_be16(pkt + 2, 16);
    /* Option 2: ACCM (length 6) = 0x00000000 */
    pkt[4] = LCP_OPT_ACCM;
    pkt[5] = 6;
    write_be32(pkt + 6, 0x00000000UL);
    /* Option 5: Magic Number (length 6) */
    pkt[10] = LCP_OPT_MAGIC;
    pkt[11] = 6;
    write_be32(pkt + 12, magic_);

    last_req_ms_ = now_ms;
    send_ppp_frame(OYK_PPP_PROTO_LCP, pkt, 16);
}

void OykPppLink::send_ipcp_conf_req(oyk_u32 now_ms) {
    /* IPCP Configure-Request: Option 3 (IPv4 Address) */
    oyk_u8 pkt[10];
    pkt[0] = PPP_CONF_REQ;
    pkt[1] = next_id_++;
    write_be16(pkt + 2, 10);
    pkt[4] = IPCP_OPT_ADDR;
    pkt[5] = 6;
    memcpy(pkt + 6, &local_ip_, 4);

    last_req_ms_ = now_ms;
    send_ppp_frame(OYK_PPP_PROTO_IPCP, pkt, 10);
}

void OykPppLink::check_lcp_opened(oyk_u32 now_ms) {
    if (state_ == OYK_PPP_LCP_OPENING && lcp_local_acked_ && lcp_peer_acked_) {
        state_ = OYK_PPP_IPCP_OPENING;
        ipcp_local_acked_ = 0;
        ipcp_peer_acked_ = 0;
        send_ipcp_conf_req(now_ms);
    }
}

void OykPppLink::check_ipcp_opened() {
    if (state_ == OYK_PPP_IPCP_OPENING && ipcp_local_acked_ && ipcp_peer_acked_) {
        state_ = OYK_PPP_UP;
    }
}

void OykPppLink::tick(oyk_u32 now_ms) {
    if (state_ == OYK_PPP_LCP_OPENING && (now_ms - last_req_ms_) >= 2000UL) {
        send_lcp_conf_req(now_ms);
    } else if (state_ == OYK_PPP_IPCP_OPENING && (now_ms - last_req_ms_) >= 2000UL) {
        send_ipcp_conf_req(now_ms);
    }
}

void OykPppLink::feed_serial_bytes(const oyk_u8* data, size_t len, oyk_u32 now_ms) {
    if (!data) return;
    for (size_t i = 0; i < len; ++i) {
        oyk_u8 b = data[i];
        if (b == PPP_FLAG) {
            if (rx_in_frame_ && !rx_overflow_ && rx_len_ >= 4) {
                on_complete_hdlc_frame(rx_buf_, rx_len_, now_ms);
            }
            rx_in_frame_ = 1;
            rx_escaped_ = 0;
            rx_overflow_ = 0;
            rx_len_ = 0;
            continue;
        }
        if (!rx_in_frame_) continue;
        if (b == PPP_ESCAPE) {
            rx_escaped_ = 1;
            continue;
        }
        if (rx_escaped_) {
            b = static_cast<oyk_u8>(b ^ PPP_XOR);
            rx_escaped_ = 0;
        }
        if (rx_len_ < sizeof(rx_buf_)) {
            rx_buf_[rx_len_++] = b;
        } else {
            rx_overflow_ = 1;
        }
    }
}

void OykPppLink::on_complete_hdlc_frame(const oyk_u8* buf, size_t len, oyk_u32 now_ms) {
    if (len < 4) return;
    if (!oyk_verify_fcs(buf, len)) {
        rx_fcs_errors_++;
        return;
    }
    rx_frames_++;
    size_t body_len = len - 2; /* Strip 2-byte FCS */
    size_t off = 0;

    /* Optional Address (0xFF) + Control (0x03) header */
    if (body_len >= 2 && buf[0] == PPP_ADDR && buf[1] == PPP_CTRL) {
        off = 2;
    }
    if (off >= body_len) return;

    /* Protocol field: 1 byte if LSB==1 (PFC), else 2 bytes big-endian */
    oyk_u16 proto = 0;
    if ((buf[off] & 0x01U) != 0) {
        proto = buf[off++];
    } else {
        if (off + 2 > body_len) return;
        proto = read_be16(buf + off);
        off += 2;
    }

    const oyk_u8* payload = buf + off;
    size_t plen = body_len - off;

    if (proto == OYK_PPP_PROTO_LCP) {
        handle_lcp(payload, plen, now_ms);
    } else if (proto == OYK_PPP_PROTO_IPCP) {
        handle_ipcp(payload, plen, now_ms);
    } else if (proto == OYK_PPP_PROTO_IP && state_ == OYK_PPP_UP) {
        handle_ipv4(payload, plen);
    }
}

void OykPppLink::handle_lcp(const oyk_u8* buf, size_t len, oyk_u32 now_ms) {
    if (len < 4) return;
    oyk_u8 code = buf[0];
    oyk_u8 id = buf[1];
    oyk_u16 pkt_len = read_be16(buf + 2);
    if (pkt_len < 4 || pkt_len > len) return;

    if (code == PPP_CONF_REQ) {
        if (state_ == OYK_PPP_DOWN) {
            state_ = OYK_PPP_LCP_OPENING;
            send_lcp_conf_req(now_ms);
        }
        oyk_u8 rej[128];
        size_t rej_len = 4;
        oyk_u32 peer_accm = 0xFFFFFFFFUL;
        int has_accm = 0;

        size_t pos = 4;
        while (pos + 2 <= pkt_len) {
            oyk_u8 opt_t = buf[pos];
            oyk_u8 opt_l = buf[pos + 1];
            if (opt_l < 2 || pos + opt_l > pkt_len) break;
            if (opt_t == LCP_OPT_ACCM && opt_l == 6) {
                peer_accm = read_be32(buf + pos + 2);
                has_accm = 1;
            } else if (opt_t == LCP_OPT_MRU || opt_t == LCP_OPT_MAGIC ||
                       opt_t == LCP_OPT_PFC || opt_t == LCP_OPT_ACFC) {
                /* Accept standard LCP options */
            } else {
                /* Reject unsupported options (e.g., Auth/Callback) */
                if (rej_len + opt_l <= sizeof(rej)) {
                    memcpy(rej + rej_len, buf + pos, opt_l);
                    rej_len += opt_l;
                }
            }
            pos += opt_l;
        }

        if (rej_len > 4) {
            rej[0] = PPP_CONF_REJ;
            rej[1] = id;
            write_be16(rej + 2, static_cast<oyk_u16>(rej_len));
            send_ppp_frame(OYK_PPP_PROTO_LCP, rej, rej_len);
        } else {
            oyk_u8 ack[128];
            size_t alen = (pkt_len <= sizeof(ack)) ? pkt_len : sizeof(ack);
            memcpy(ack, buf, alen);
            ack[0] = PPP_CONF_ACK;
            write_be16(ack + 2, static_cast<oyk_u16>(alen));
            send_ppp_frame(OYK_PPP_PROTO_LCP, ack, alen);
            if (has_accm) tx_accm_ = peer_accm;
            lcp_peer_acked_ = 1;
            check_lcp_opened(now_ms);
        }
    } else if (code == PPP_CONF_ACK) {
        lcp_local_acked_ = 1;
        check_lcp_opened(now_ms);
    } else if (code == PPP_CONF_REJ || code == PPP_CONF_NAK) {
        /* Fallback: send minimal empty LCP Configure-Request */
        oyk_u8 min_req[4];
        min_req[0] = PPP_CONF_REQ;
        min_req[1] = next_id_++;
        write_be16(min_req + 2, 4);
        last_req_ms_ = now_ms;
        send_ppp_frame(OYK_PPP_PROTO_LCP, min_req, 4);
    } else if (code == PPP_ECHO_REQ && pkt_len >= 8) {
        oyk_u8 rep[64];
        size_t rlen = (pkt_len <= sizeof(rep)) ? pkt_len : sizeof(rep);
        memcpy(rep, buf, rlen);
        rep[0] = PPP_ECHO_REP;
        write_be32(rep + 4, magic_);
        send_ppp_frame(OYK_PPP_PROTO_LCP, rep, rlen);
    } else if (code == PPP_TERM_REQ) {
        oyk_u8 ack[4];
        ack[0] = PPP_TERM_ACK;
        ack[1] = id;
        write_be16(ack + 2, 4);
        send_ppp_frame(OYK_PPP_PROTO_LCP, ack, 4);
        state_ = OYK_PPP_DOWN;
        lcp_local_acked_ = 0;
        lcp_peer_acked_ = 0;
        ipcp_local_acked_ = 0;
        ipcp_peer_acked_ = 0;
    }
}

void OykPppLink::handle_ipcp(const oyk_u8* buf, size_t len, oyk_u32 now_ms) {
    if (len < 4) return;
    if (state_ == OYK_PPP_DOWN || state_ == OYK_PPP_LCP_OPENING) return;

    oyk_u8 code = buf[0];
    oyk_u8 id = buf[1];
    oyk_u16 pkt_len = read_be16(buf + 2);
    if (pkt_len < 4 || pkt_len > len) return;

    if (code == PPP_CONF_REQ) {
        oyk_u8 rej[64];
        size_t rej_len = 4;
        oyk_u8 nak[32];
        size_t nak_len = 4;
        oyk_u32 peer_req_ip = 0;
        int has_ip_opt = 0;

        size_t pos = 4;
        while (pos + 2 <= pkt_len) {
            oyk_u8 opt_t = buf[pos];
            oyk_u8 opt_l = buf[pos + 1];
            if (opt_l < 2 || pos + opt_l > pkt_len) break;
            if (opt_t == IPCP_OPT_ADDR && opt_l == 6) {
                memcpy(&peer_req_ip, buf + pos + 2, 4);
                has_ip_opt = 1;
                if (peer_req_ip == 0 && desired_peer_ip_ != 0) {
                    nak[nak_len++] = IPCP_OPT_ADDR;
                    nak[nak_len++] = 6;
                    memcpy(nak + nak_len, &desired_peer_ip_, 4);
                    nak_len += 4;
                }
            } else {
                if (rej_len + opt_l <= sizeof(rej)) {
                    memcpy(rej + rej_len, buf + pos, opt_l);
                    rej_len += opt_l;
                }
            }
            pos += opt_l;
        }

        if (rej_len > 4) {
            rej[0] = PPP_CONF_REJ;
            rej[1] = id;
            write_be16(rej + 2, static_cast<oyk_u16>(rej_len));
            send_ppp_frame(OYK_PPP_PROTO_IPCP, rej, rej_len);
        } else if (nak_len > 4) {
            nak[0] = PPP_CONF_NAK;
            nak[1] = id;
            write_be16(nak + 2, static_cast<oyk_u16>(nak_len));
            send_ppp_frame(OYK_PPP_PROTO_IPCP, nak, nak_len);
        } else {
            oyk_u8 ack[64];
            size_t alen = (pkt_len <= sizeof(ack)) ? pkt_len : sizeof(ack);
            memcpy(ack, buf, alen);
            ack[0] = PPP_CONF_ACK;
            write_be16(ack + 2, static_cast<oyk_u16>(alen));
            send_ppp_frame(OYK_PPP_PROTO_IPCP, ack, alen);
            if (has_ip_opt && peer_req_ip != 0) {
                remote_ip_ = peer_req_ip;
            }
            ipcp_peer_acked_ = 1;
            check_ipcp_opened();
        }
    } else if (code == PPP_CONF_ACK) {
        ipcp_local_acked_ = 1;
        check_ipcp_opened();
    } else if (code == PPP_CONF_NAK) {
        /* Peer suggested our local IPv4 address (client mode) */
        size_t pos = 4;
        while (pos + 2 <= pkt_len) {
            oyk_u8 opt_t = buf[pos];
            oyk_u8 opt_l = buf[pos + 1];
            if (opt_l < 2 || pos + opt_l > pkt_len) break;
            if (opt_t == IPCP_OPT_ADDR && opt_l == 6) {
                memcpy(&local_ip_, buf + pos + 2, 4);
            }
            pos += opt_l;
        }
        send_ipcp_conf_req(now_ms);
    }
}

void OykPppLink::handle_icmp_echo(const oyk_u8* pkt, size_t len, size_t ihl) {
    if (len < ihl + 8) return;
    if (pkt[ihl] != 8U) return; /* Only Echo Request (type 8) */
    if (len > 512) return;

    oyk_u8 rep[512];
    memcpy(rep, pkt, len);
    /* Swap src/dst IP */
    memcpy(rep + 12, pkt + 16, 4);
    memcpy(rep + 16, pkt + 12, 4);
    /* Recompute IPv4 header checksum */
    rep[10] = 0;
    rep[11] = 0;
    write_be16(rep + 10, oyk_ipv4_checksum(rep, ihl));
    /* Change ICMP type 8 -> 0 (Echo Reply) and recompute ICMP checksum */
    rep[ihl] = 0;
    rep[ihl + 2] = 0;
    rep[ihl + 3] = 0;
    write_be16(rep + ihl + 2, oyk_ipv4_checksum(rep + ihl, len - ihl));
    send_ip_packet(rep, len);
}

void OykPppLink::handle_ipv4(const oyk_u8* pkt, size_t len) {
    if (len < 20) return;
    oyk_u8 ver = static_cast<oyk_u8>(pkt[0] >> 4);
    size_t ihl = static_cast<size_t>(pkt[0] & 0x0FU) * 4;
    if (ver != 4U || ihl < 20 || ihl > len) return;

    oyk_u16 total_len = read_be16(pkt + 2);
    if (total_len < ihl || total_len > len) return;
    if (oyk_ipv4_checksum(pkt, ihl) != 0) return;

    if (raw_ip_rx_cb_) {
        raw_ip_rx_cb_(pkt, total_len, raw_ip_rx_ctx_);
    }

    oyk_u8 ip_proto = pkt[9];
    oyk_u32 src_ip = 0;
    memcpy(&src_ip, pkt + 12, 4);

    if (ip_proto == OYK_IP_PROTO_ICMP) {
        handle_icmp_echo(pkt, total_len, ihl);
        return;
    }

    if (ip_proto == OYK_IP_PROTO_AXIP && ax25_rx_cb_) {
        const oyk_u8* pay = pkt + ihl;
        size_t pay_len = total_len - ihl;
        if (pay_len >= 17 && oyk_verify_fcs(pay, pay_len)) {
            OykFrame f;
            if (oyk_ax25_decode(pay, pay_len - 2, &f)) {
                ax25_rx_cb_(1, &f, src_ip, 0, ax25_rx_ctx_);
            }
        }
        return;
    }

    if (ip_proto == OYK_IP_PROTO_UDP && total_len >= ihl + 8) {
        const oyk_u8* udp = pkt + ihl;
        oyk_u16 src_port = read_be16(udp);
        oyk_u16 dst_port = read_be16(udp + 2);
        oyk_u16 udp_len  = read_be16(udp + 4);
        if (udp_len < 8 || ihl + udp_len > total_len) return;
        const oyk_u8* pay = udp + 8;
        size_t pay_len = udp_len - 8;
        if (udp_rx_cb_) {
            udp_rx_cb_(src_ip, src_port, dst_port, pay, pay_len, udp_rx_ctx_);
        }
        if (dst_port == axudp_port_ && ax25_rx_cb_) {
            if (pay_len >= 17 && oyk_verify_fcs(pay, pay_len)) {
                OykFrame f;
                if (oyk_ax25_decode(pay, pay_len - 2, &f)) {
                    ax25_rx_cb_(0, &f, src_ip, src_port, ax25_rx_ctx_);
                }
            }
        }
    }
}

int OykPppLink::send_ip_packet(const oyk_u8* ip_pkt, size_t len) {
    if (state_ != OYK_PPP_UP || !ip_pkt || len < 20) return 0;
    send_ppp_frame(OYK_PPP_PROTO_IP, ip_pkt, len);
    return 1;
}

int OykPppLink::send_udp(oyk_u32 dst_ip_net,
                         oyk_u16 src_port_host,
                         oyk_u16 dst_port_host,
                         const oyk_u8* payload,
                         size_t len) {
    if (state_ != OYK_PPP_UP || (!payload && len > 0)) return 0;
    if (dst_ip_net == 0) dst_ip_net = remote_ip_;
    if (dst_ip_net == 0 || dst_port_host == 0) return 0;
    if (28U + len > 540U) return 0;

    oyk_u8 pkt[540];
    size_t udp_len = 8 + len;
    size_t total_len = 20 + udp_len;

    memset(pkt, 0, 20);
    pkt[0] = 0x45U;
    write_be16(pkt + 2, static_cast<oyk_u16>(total_len));
    write_be16(pkt + 4, ip_ident_++);
    pkt[8] = 64U;
    pkt[9] = OYK_IP_PROTO_UDP;
    memcpy(pkt + 12, &local_ip_, 4);
    memcpy(pkt + 16, &dst_ip_net, 4);
    write_be16(pkt + 10, oyk_ipv4_checksum(pkt, 20));

    write_be16(pkt + 20, src_port_host ? src_port_host : axudp_port_);
    write_be16(pkt + 22, dst_port_host);
    write_be16(pkt + 24, static_cast<oyk_u16>(udp_len));
    write_be16(pkt + 26, 0);
    if (len > 0) {
        memcpy(pkt + 28, payload, len);
    }

    return send_ip_packet(pkt, total_len);
}

int OykPppLink::send_axudp(const OykFrame* frame, oyk_u32 dst_ip_net, oyk_u16 dst_port_host) {
    if (state_ != OYK_PPP_UP || !frame) return 0;
    if (dst_ip_net == 0) dst_ip_net = remote_ip_;
    if (dst_port_host == 0) dst_port_host = axudp_port_;
    if (dst_ip_net == 0) return 0;

    oyk_u8 pkt[400];
    size_t ax_len = oyk_ax25_encode(frame, pkt + 28, sizeof(pkt) - 30);
    if (ax_len == 0) return 0;
    oyk_u16 fcs = oyk_crc16_fcs(pkt + 28, ax_len);
    pkt[28 + ax_len]     = static_cast<oyk_u8>(fcs & 0xFFU);
    pkt[28 + ax_len + 1] = static_cast<oyk_u8>((fcs >> 8) & 0xFFU);
    size_t udp_len = 8 + ax_len + 2;
    size_t total_len = 20 + udp_len;

    /* IPv4 Header (20 bytes) */
    memset(pkt, 0, 20);
    pkt[0] = 0x45U;
    write_be16(pkt + 2, static_cast<oyk_u16>(total_len));
    write_be16(pkt + 4, ip_ident_++);
    pkt[8] = 64U;
    pkt[9] = OYK_IP_PROTO_UDP;
    memcpy(pkt + 12, &local_ip_, 4);
    memcpy(pkt + 16, &dst_ip_net, 4);
    write_be16(pkt + 10, oyk_ipv4_checksum(pkt, 20));

    /* UDP Header (8 bytes, checksum 0 = optional in IPv4 RFC 768) */
    write_be16(pkt + 20, axudp_port_);
    write_be16(pkt + 22, dst_port_host);
    write_be16(pkt + 24, static_cast<oyk_u16>(udp_len));
    write_be16(pkt + 26, 0);

    return send_ip_packet(pkt, total_len);
}

int OykPppLink::send_axip(const OykFrame* frame, oyk_u32 dst_ip_net) {
    if (state_ != OYK_PPP_UP || !frame) return 0;
    if (dst_ip_net == 0) dst_ip_net = remote_ip_;
    if (dst_ip_net == 0) return 0;

    oyk_u8 pkt[400];
    size_t ax_len = oyk_ax25_encode(frame, pkt + 20, sizeof(pkt) - 22);
    if (ax_len == 0) return 0;
    oyk_u16 fcs = oyk_crc16_fcs(pkt + 20, ax_len);
    pkt[20 + ax_len]     = static_cast<oyk_u8>(fcs & 0xFFU);
    pkt[20 + ax_len + 1] = static_cast<oyk_u8>((fcs >> 8) & 0xFFU);
    size_t total_len = 20 + ax_len + 2;

    memset(pkt, 0, 20);
    pkt[0] = 0x45U;
    write_be16(pkt + 2, static_cast<oyk_u16>(total_len));
    write_be16(pkt + 4, ip_ident_++);
    pkt[8] = 64U;
    pkt[9] = OYK_IP_PROTO_AXIP;
    memcpy(pkt + 12, &local_ip_, 4);
    memcpy(pkt + 16, &dst_ip_net, 4);
    write_be16(pkt + 10, oyk_ipv4_checksum(pkt, 20));

    return send_ip_packet(pkt, total_len);
}

void OykPppLink::format_status(char* out, size_t out_sz) const {
    if (!out || out_sz == 0) return;
    if (state_ == OYK_PPP_DOWN) {
        strncpy(out, "PPP DOWN", out_sz - 1);
        out[out_sz - 1] = '\0';
    } else if (state_ == OYK_PPP_LCP_OPENING) {
        strncpy(out, "PPP LCP...", out_sz - 1);
        out[out_sz - 1] = '\0';
    } else if (state_ == OYK_PPP_IPCP_OPENING) {
        strncpy(out, "PPP IPCP...", out_sz - 1);
        out[out_sz - 1] = '\0';
    } else {
        const oyk_u8* ip = reinterpret_cast<const oyk_u8*>(&remote_ip_);
        char tmp[48];
        sprintf(tmp, "PPP UP %u.%u.%u.%u",
                static_cast<unsigned int>(ip[0]), static_cast<unsigned int>(ip[1]),
                static_cast<unsigned int>(ip[2]), static_cast<unsigned int>(ip[3]));
        strncpy(out, tmp, out_sz - 1);
        out[out_sz - 1] = '\0';
    }
}
