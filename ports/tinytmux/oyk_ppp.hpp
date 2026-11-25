/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_PPP_HPP
#define OYK_PPP_HPP

/*
 * Strict C++98 RFC 1661 / RFC 1662 / RFC 1332 PPP-over-Serial + IPv4 AXUDP/AXIP
 * transport engine for Teensy 4.1 / IMXRT1060 (TinyTMUX) <-> RadioFoundry radios.
 *
 * Features:
 *   - RFC 1662 Async HDLC framing, ACCM byte-stuffing, and PPP FCS-16 (CRC-16/X-25)
 *   - RFC 1661 LCP negotiation (MRU, ACCM, Magic Number, Echo-Request/Reply, Terminate)
 *   - RFC 1332 IPCP IPv4 negotiation (static, server-assigned, or peer-assigned IP)
 *   - IPv4 encapsulation/decapsulation for:
 *       * AXUDP (UDP + AX.25 + CRC-16 FCS) over PPP
 *       * AXIP  (IP protocol 93 / RFC 1226 + AX.25 + CRC-16 FCS) over PPP
 *       * ICMP Echo Reply (ping over PPP link)
 *   - Zero STL, zero exceptions, zero RTTI, bounded static buffers
 */

#include "../compat/oyk_portable.hpp"

/* PPP Protocol Identifiers */
#define OYK_PPP_PROTO_IP    0x0021U
#define OYK_PPP_PROTO_IPCP  0x8021U
#define OYK_PPP_PROTO_LCP   0xC021U

/* IPv4 Protocol Numbers */
#define OYK_IP_PROTO_ICMP   1U
#define OYK_IP_PROTO_UDP    17U
#define OYK_IP_PROTO_AXIP   93U /* RFC 1226 AX.25 over IP */

#define OYK_PPP_MAX_FRAME   1540

enum OykPppState {
    OYK_PPP_DOWN = 0,
    OYK_PPP_LCP_OPENING,
    OYK_PPP_IPCP_OPENING,
    OYK_PPP_UP
};

typedef size_t (*OykPppSerialTxFn)(const oyk_u8* data, size_t len, void* ctx);
typedef void (*OykPppAx25RxFn)(int is_axip,
                               const OykFrame* frame,
                               oyk_u32 src_ip_net,
                               oyk_u16 src_port_host,
                               void* ctx);
typedef void (*OykPppRawIpRxFn)(const oyk_u8* ip_pkt, size_t len, void* ctx);

/* RFC 1662 PPP FCS-16 helpers */
oyk_u16 oyk_ppp_fcs16(const oyk_u8* data, size_t len);
oyk_u16 oyk_ipv4_checksum(const oyk_u8* data, size_t len);

/* Encodes a PPP payload (protocol + body) into an RFC 1662 HDLC frame with 0x7E flags & FCS. */
size_t oyk_ppp_hdlc_encode(oyk_u16 protocol,
                           const oyk_u8* payload, size_t payload_len,
                           oyk_u32 tx_accm,
                           oyk_u8* out, size_t out_cap);

class OykPppLink {
public:
    OykPppLink();

    void init(oyk_u32 local_ip_net,
              oyk_u32 peer_ip_net,
              oyk_u16 axudp_listen_port,
              oyk_u32 magic_number,
              OykPppSerialTxFn tx_fn, void* tx_ctx);

    void set_ax25_rx_callback(OykPppAx25RxFn cb, void* ctx);
    void set_raw_ip_rx_callback(OykPppRawIpRxFn cb, void* ctx);

    /* Bring link up (begins LCP Configure-Request handshake) or cleanly terminate. */
    void open(oyk_u32 now_ms);
    void close();

    /* Feed incoming serial bytes from UART (or mock loopback). */
    void feed_serial_bytes(const oyk_u8* data, size_t len, oyk_u32 now_ms);

    /* Periodic retransmit / keepalive tick (call every ~100-250ms). */
    void tick(oyk_u32 now_ms);

    /* Send AX.25 frame over PPP as AXUDP (UDP) or AXIP (IP protocol 93). */
    int send_axudp(const OykFrame* frame, oyk_u32 dst_ip_net, oyk_u16 dst_port_host);
    int send_axip(const OykFrame* frame, oyk_u32 dst_ip_net);

    /* Send raw IPv4 packet over PPP (used by lwIP bridge or direct callers). */
    int send_ip_packet(const oyk_u8* ip_pkt, size_t len);

    /* State & telemetry queries */
    OykPppState state() const { return state_; }
    int is_up() const { return (state_ == OYK_PPP_UP) ? 1 : 0; }
    oyk_u32 local_ip() const { return local_ip_; }
    oyk_u32 remote_ip() const { return remote_ip_; }
    oyk_u16 axudp_port() const { return axudp_port_; }
    oyk_u32 rx_frames() const { return rx_frames_; }
    oyk_u32 tx_frames() const { return tx_frames_; }
    oyk_u32 rx_fcs_errors() const { return rx_fcs_errors_; }
    void format_status(char* out, size_t out_sz) const;

private:
    void send_ppp_frame(oyk_u16 protocol, const oyk_u8* payload, size_t len);
    void send_lcp_conf_req(oyk_u32 now_ms);
    void send_ipcp_conf_req(oyk_u32 now_ms);
    void on_complete_hdlc_frame(const oyk_u8* buf, size_t len, oyk_u32 now_ms);
    void handle_lcp(const oyk_u8* buf, size_t len, oyk_u32 now_ms);
    void handle_ipcp(const oyk_u8* buf, size_t len, oyk_u32 now_ms);
    void handle_ipv4(const oyk_u8* pkt, size_t len);
    void handle_icmp_echo(const oyk_u8* pkt, size_t len, size_t ihl);
    void check_lcp_opened(oyk_u32 now_ms);
    void check_ipcp_opened();

    OykPppState state_;
    oyk_u32 local_ip_;       /* Network byte order */
    oyk_u32 desired_peer_ip_;/* Optional IP to assign to peer if peer asks for 0.0.0.0 */
    oyk_u32 remote_ip_;      /* Negotiated peer IP (network byte order) */
    oyk_u16 axudp_port_;
    oyk_u32 magic_;
    oyk_u32 tx_accm_;
    oyk_u16 ip_ident_;

    int lcp_local_acked_;
    int lcp_peer_acked_;
    int ipcp_local_acked_;
    int ipcp_peer_acked_;
    oyk_u8 next_id_;
    oyk_u32 last_req_ms_;

    OykPppSerialTxFn tx_fn_;
    void* tx_ctx_;
    OykPppAx25RxFn ax25_rx_cb_;
    void* ax25_rx_ctx_;
    OykPppRawIpRxFn raw_ip_rx_cb_;
    void* raw_ip_rx_ctx_;

    /* Incremental HDLC RX decoder */
    int rx_in_frame_;
    int rx_escaped_;
    int rx_overflow_;
    size_t rx_len_;
    oyk_u8 rx_buf_[OYK_PPP_MAX_FRAME];

    oyk_u32 rx_frames_;
    oyk_u32 tx_frames_;
    oyk_u32 rx_fcs_errors_;
};

#endif /* OYK_PPP_HPP */
