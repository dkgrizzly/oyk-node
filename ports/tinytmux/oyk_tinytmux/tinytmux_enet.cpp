/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "tinytmux_enet.hpp"
#include "board_tinytmux.hpp"
#include "lwip_net_bridge.hpp"

#include <string.h>

static int s_enet_inited = 0;
static int s_link_up = 0;
static int s_link_100m = 0;
static oyk_u32 s_current_ip_net = 0;

#if defined(TEENSYDUINO)

#include <Arduino.h>

static TinyTmuxNetBridge* s_bridge_ref = NULL;

#if !defined(OYK_USE_NATIVE_ETHERNET)

#include <QNEthernet.h>
#include <lwip/ip_addr.h>
#include <lwip/pbuf.h>
#include <lwip/raw.h>

using namespace qindesign::network;

static EthernetUDP s_axudp_sock;
static EthernetUDP s_rf_sock;
static struct raw_pcb* s_axip_pcb = NULL;
static int s_sockets_bound = 0;
static oyk_u8 s_rx_buf[1536];

static oyk_u32 ip_to_net_u32(const IPAddress& ip) {
    return static_cast<oyk_u32>(ip[0]) |
           (static_cast<oyk_u32>(ip[1]) << 8) |
           (static_cast<oyk_u32>(ip[2]) << 16) |
           (static_cast<oyk_u32>(ip[3]) << 24);
}

static IPAddress net_u32_to_ip(oyk_u32 ip_net) {
    return IPAddress(static_cast<uint8_t>(ip_net & 0xFFU),
                     static_cast<uint8_t>((ip_net >> 8) & 0xFFU),
                     static_cast<uint8_t>((ip_net >> 16) & 0xFFU),
                     static_cast<uint8_t>((ip_net >> 24) & 0xFFU));
}

static u8_t axip_raw_recv_cb(void* /*arg*/, struct raw_pcb* /*pcb*/,
                              struct pbuf* p, const ip_addr_t* addr) {
    if (!p || !addr) return 0;
    if (p->tot_len >= 20 && s_bridge_ref) {
        u8_t ver_ihl = 0;
        pbuf_copy_partial(p, &ver_ihl, 1, 0);
        u16_t iphdr_len = static_cast<u16_t>((ver_ihl & 0x0FU) * 4U);
        if (iphdr_len >= 20U && p->tot_len > iphdr_len &&
            static_cast<size_t>(p->tot_len - iphdr_len) <= sizeof(s_rx_buf)) {
            u16_t pay_len = static_cast<u16_t>(p->tot_len - iphdr_len);
            pbuf_copy_partial(p, s_rx_buf, pay_len, iphdr_len);
            s_bridge_ref->on_ethernet_rx(1, ip_2_ip4(addr)->addr, 0, s_rx_buf, pay_len);
        }
    }
    pbuf_free(p);
    return 1;
}

static void bind_enet_sockets() {
    if (s_sockets_bound) return;
    s_axudp_sock.begin(10093);
    s_rf_sock.begin(OYK_RF_DEFAULT_PORT);
    s_axip_pcb = raw_new(93);
    if (s_axip_pcb) {
        raw_bind(s_axip_pcb, IP_ADDR_ANY);
        raw_recv(s_axip_pcb, axip_raw_recv_cb, NULL);
    }
    s_sockets_bound = 1;
}

int tinytmux_enet_init(const oyk_u8 mac[6]) {
    if (s_enet_inited) return 1;
    if (mac) {
        uint8_t mac_buf[6];
        memcpy(mac_buf, mac, 6);
        Ethernet.setMACAddress(mac_buf);
    }
    /* Non-blocking DHCP startup on QNEthernet */
    Ethernet.begin();
    bind_enet_sockets();
    s_enet_inited = 1;
    return 1;
}

int tinytmux_enet_poll(TinyTmuxNetBridge* bridge, oyk_u32 /*now_ms*/) {
    s_bridge_ref = bridge;
    if (!s_enet_inited) return 0;

    Ethernet.loop();
    int up = Ethernet.linkState() ? 1 : 0;
    int sp100 = (up && Ethernet.linkInfo().speed >= 100) ? 1 : 0;
    oyk_u32 cur_ip = ip_to_net_u32(Ethernet.localIP());
    tinytmux_board_set_link_led(sp100);

    if (up) {
        int pkt = s_axudp_sock.parsePacket();
        while (pkt > 0) {
            int n = s_axudp_sock.read(s_rx_buf, sizeof(s_rx_buf));
            if (n > 0 && bridge) {
                oyk_u32 sip = ip_to_net_u32(s_axudp_sock.remoteIP());
                oyk_u16 sport = static_cast<oyk_u16>(s_axudp_sock.remotePort());
                bridge->on_ethernet_rx(0, sip, sport, s_rx_buf, static_cast<size_t>(n));
            }
            pkt = s_axudp_sock.parsePacket();
        }

        int rfpkt = s_rf_sock.parsePacket();
        while (rfpkt > 0) {
            int n = s_rf_sock.read(s_rx_buf, sizeof(s_rx_buf));
            if (n > 0 && bridge) {
                oyk_u32 sip = ip_to_net_u32(s_rf_sock.remoteIP());
                oyk_u16 sport = static_cast<oyk_u16>(s_rf_sock.remotePort());
                bridge->on_ethernet_udp_rx(sip, sport, OYK_RF_DEFAULT_PORT,
                                           s_rx_buf, static_cast<size_t>(n));
            }
            rfpkt = s_rf_sock.parsePacket();
        }
    }

    if (bridge && (up != s_link_up || sp100 != s_link_100m || cur_ip != s_current_ip_net)) {
        s_link_up = up;
        s_link_100m = sp100;
        s_current_ip_net = cur_ip;
        bridge->set_ethernet_link_state(up, sp100, cur_ip);
    } else {
        s_link_up = up;
        s_link_100m = sp100;
        s_current_ip_net = cur_ip;
    }
    return 1;
}

int tinytmux_enet_send_axudp(oyk_u32 dst_ip_net, oyk_u16 src_port_host, oyk_u16 dst_port_host,
                             const oyk_u8* payload, size_t len) {
    if (!s_enet_inited || !s_sockets_bound || !payload || len == 0 || dst_ip_net == 0) return 0;
    EthernetUDP& sock = (src_port_host == OYK_RF_DEFAULT_PORT) ? s_rf_sock : s_axudp_sock;
    if (!sock.beginPacket(net_u32_to_ip(dst_ip_net), dst_port_host)) return 0;
    size_t wrote = sock.write(payload, len);
    if (wrote != len) return 0;
    return sock.endPacket() ? 1 : 0;
}

int tinytmux_enet_send_axip(oyk_u32 dst_ip_net, const oyk_u8* payload, size_t len) {
    if (!s_enet_inited || !s_axip_pcb || !payload || len == 0 || dst_ip_net == 0) return 0;
    struct pbuf* p = pbuf_alloc(PBUF_IP, static_cast<u16_t>(len), PBUF_RAM);
    if (!p) return 0;
    memcpy(p->payload, payload, len);
    ip_addr_t ip;
    IP4_ADDR(ip_2_ip4(&ip), dst_ip_net & 0xFFU, (dst_ip_net >> 8) & 0xFFU,
             (dst_ip_net >> 16) & 0xFFU, (dst_ip_net >> 24) & 0xFFU);
    err_t e = raw_sendto(s_axip_pcb, p, &ip);
    pbuf_free(p);
    return (e == ERR_OK) ? 1 : 0;
}

int tinytmux_enet_rigctl_tcp_exchange(oyk_u32 host_ip_net, oyk_u16 port_host,
                                      const char* cmd, size_t cmd_len,
                                      char* reply_out, size_t reply_cap) {
    if (!s_enet_inited || !s_link_up || host_ip_net == 0 || !cmd || cmd_len == 0) return 0;
    EthernetClient cli;
    cli.setConnectionTimeout(250);
    if (!cli.connect(net_u32_to_ip(host_ip_net), port_host)) return 0;
    cli.write(reinterpret_cast<const uint8_t*>(cmd), cmd_len);
    cli.flush();

    size_t got = 0;
    unsigned long start = millis();
    while ((millis() - start) < 250UL && (cli.connected() || cli.available() > 0)) {
        Ethernet.loop();
        while (cli.available() > 0 && got + 1 < reply_cap) {
            int ch = cli.read();
            if (ch < 0) break;
            reply_out[got++] = static_cast<char>(ch);
            if (ch == '\n') {
                start = 0UL;
                break;
            }
        }
        if (start == 0UL) break;
    }
    if (reply_out && reply_cap > 0) reply_out[got] = '\0';
    cli.stop();
    return (got > 0) ? 1 : 0;
}

#else

#include <NativeEthernet.h>
#include <NativeEthernetUdp.h>

static EthernetUDP s_axudp_sock;
static EthernetUDP s_rf_sock;
static uint8_t s_mac_copy[6];
static int s_dhcp_started = 0;
static oyk_u8 s_rx_buf[1536];

static oyk_u32 ip_to_net_u32(const IPAddress& ip) {
    return static_cast<oyk_u32>(ip[0]) |
           (static_cast<oyk_u32>(ip[1]) << 8) |
           (static_cast<oyk_u32>(ip[2]) << 16) |
           (static_cast<oyk_u32>(ip[3]) << 24);
}

static IPAddress net_u32_to_ip(oyk_u32 ip_net) {
    return IPAddress(static_cast<uint8_t>(ip_net & 0xFFU),
                     static_cast<uint8_t>((ip_net >> 8) & 0xFFU),
                     static_cast<uint8_t>((ip_net >> 16) & 0xFFU),
                     static_cast<uint8_t>((ip_net >> 24) & 0xFFU));
}

int tinytmux_enet_init(const oyk_u8 mac[6]) {
    if (mac) memcpy(s_mac_copy, mac, 6);
    s_dhcp_started = 0;
    s_enet_inited = 1;
    return 1;
}

int tinytmux_enet_poll(TinyTmuxNetBridge* bridge, oyk_u32 /*now_ms*/) {
    s_bridge_ref = bridge;
    if (!s_enet_inited) return 0;

    int up = (Ethernet.linkStatus() == LinkON) ? 1 : 0;
    if (up && !s_dhcp_started) {
        if (Ethernet.begin(s_mac_copy, 500, 250)) {
            s_axudp_sock.begin(10093);
            s_rf_sock.begin(OYK_RF_DEFAULT_PORT);
            s_dhcp_started = 1;
        }
    }
    int sp100 = up ? 1 : 0;
    oyk_u32 cur_ip = s_dhcp_started ? ip_to_net_u32(Ethernet.localIP()) : 0UL;
    tinytmux_board_set_link_led(sp100);

    if (up && s_dhcp_started) {
        Ethernet.maintain();
        int pkt = s_axudp_sock.parsePacket();
        while (pkt > 0) {
            int n = s_axudp_sock.read(s_rx_buf, sizeof(s_rx_buf));
            if (n > 0 && bridge) {
                bridge->on_ethernet_rx(0, ip_to_net_u32(s_axudp_sock.remoteIP()),
                                       static_cast<oyk_u16>(s_axudp_sock.remotePort()),
                                       s_rx_buf, static_cast<size_t>(n));
            }
            pkt = s_axudp_sock.parsePacket();
        }
        int rfpkt = s_rf_sock.parsePacket();
        while (rfpkt > 0) {
            int n = s_rf_sock.read(s_rx_buf, sizeof(s_rx_buf));
            if (n > 0 && bridge) {
                bridge->on_ethernet_udp_rx(ip_to_net_u32(s_rf_sock.remoteIP()),
                                           static_cast<oyk_u16>(s_rf_sock.remotePort()),
                                           OYK_RF_DEFAULT_PORT,
                                           s_rx_buf, static_cast<size_t>(n));
            }
            rfpkt = s_rf_sock.parsePacket();
        }
    }

    if (bridge && (up != s_link_up || sp100 != s_link_100m || cur_ip != s_current_ip_net)) {
        s_link_up = up;
        s_link_100m = sp100;
        s_current_ip_net = cur_ip;
        bridge->set_ethernet_link_state(up, sp100, cur_ip);
    } else {
        s_link_up = up;
        s_link_100m = sp100;
        s_current_ip_net = cur_ip;
    }
    return 1;
}

int tinytmux_enet_send_axudp(oyk_u32 dst_ip_net, oyk_u16 src_port_host, oyk_u16 dst_port_host,
                             const oyk_u8* payload, size_t len) {
    if (!s_dhcp_started || !payload || len == 0 || dst_ip_net == 0) return 0;
    EthernetUDP& sock = (src_port_host == OYK_RF_DEFAULT_PORT) ? s_rf_sock : s_axudp_sock;
    if (!sock.beginPacket(net_u32_to_ip(dst_ip_net), dst_port_host)) return 0;
    size_t wrote = sock.write(payload, len);
    if (wrote != len) return 0;
    return sock.endPacket() ? 1 : 0;
}

int tinytmux_enet_send_axip(oyk_u32 /*dst_ip_net*/, const oyk_u8* /*payload*/, size_t /*len*/) {
    /* Raw IP protocol 93 requires QNEthernet lwIP; NativeEthernet uses FNET BSD sockets. */
    return 0;
}

int tinytmux_enet_rigctl_tcp_exchange(oyk_u32 host_ip_net, oyk_u16 port_host,
                                      const char* cmd, size_t cmd_len,
                                      char* reply_out, size_t reply_cap) {
    if (!s_dhcp_started || !s_link_up || host_ip_net == 0 || !cmd || cmd_len == 0) return 0;
    EthernetClient cli;
    if (!cli.connect(net_u32_to_ip(host_ip_net), port_host)) return 0;
    cli.write(reinterpret_cast<const uint8_t*>(cmd), cmd_len);
    size_t got = 0;
    unsigned long start = millis();
    while ((millis() - start) < 250UL && (cli.connected() || cli.available() > 0)) {
        while (cli.available() > 0 && got + 1 < reply_cap) {
            int ch = cli.read();
            if (ch < 0) break;
            reply_out[got++] = static_cast<char>(ch);
            if (ch == '\n') {
                start = 0UL;
                break;
            }
        }
        if (start == 0UL) break;
    }
    if (reply_out && reply_cap > 0) reply_out[got] = '\0';
    cli.stop();
    return (got > 0) ? 1 : 0;
}

#endif

#else /* Host build / unit tests */

int tinytmux_enet_init(const oyk_u8 /*mac*/[6]) {
    s_enet_inited = 1;
    s_link_up = 0;
    s_link_100m = 0;
    s_current_ip_net = 0;
    return 1;
}

int tinytmux_enet_poll(TinyTmuxNetBridge* /*bridge*/, oyk_u32 /*now_ms*/) {
    return 1;
}

int tinytmux_enet_send_axudp(oyk_u32 /*dst_ip_net*/, oyk_u16 /*src_port_host*/,
                             oyk_u16 /*dst_port_host*/, const oyk_u8* /*payload*/, size_t /*len*/) {
    return 0;
}

int tinytmux_enet_send_axip(oyk_u32 /*dst_ip_net*/, const oyk_u8* /*payload*/, size_t /*len*/) {
    return 0;
}

int tinytmux_enet_rigctl_tcp_exchange(oyk_u32 /*host_ip_net*/, oyk_u16 /*port_host*/,
                                      const char* /*cmd*/, size_t /*cmd_len*/,
                                      char* /*reply_out*/, size_t /*reply_cap*/) {
    return 0;
}

#endif /* TEENSYDUINO */

oyk_u32 tinytmux_enet_current_ip() { return s_current_ip_net; }
int tinytmux_enet_link_is_up() { return s_link_up; }
int tinytmux_enet_speed_is_100m() { return s_link_100m; }
