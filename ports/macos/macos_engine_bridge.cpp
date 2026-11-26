/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "macos_engine_bridge.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#if !OYK_MACOS_USE_PORTABLE
#  include "oyk/node/node_engine.hpp"
#  include "oyk/ax25/crc.hpp"
#  include <chrono>
#  include <ctime>
#endif

static void mb_copy(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1U;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int mb_strieq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + 32);
        if (ca != cb) return 0;
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

#if OYK_MACOS_USE_PORTABLE

struct PortableTransportSlot {
    int fd;
    int is_udp;
    int is_serial;
    int iface_idx;
    OykKissDecoder kiss_dec;
};

struct OykMacOsEngine::Impl {
    OykPortableNode node;
    OykConfig cfg;
    char cfg_path[256];
    int running;
    PortableTransportSlot xports[OYK_MAX_INTERFACES];
    OykMacOsTermRxFn term_cb;
    void* term_ctx;
    time_t last_tick;

    Impl() : running(0), term_cb(NULL), term_ctx(NULL), last_tick(0) {
        cfg_path[0] = '\0';
        oyk_config_init_default(&cfg);
        for (int i = 0; i < OYK_MAX_INTERFACES; ++i) {
            xports[i].fd = -1;
            xports[i].is_udp = 0;
            xports[i].is_serial = 0;
            xports[i].iface_idx = i;
            oyk_kiss_decoder_init(&xports[i].kiss_dec);
        }
    }

    static void local_trampoline(oyk_u32 sid, int ev_type, const oyk_u8* data,
                                 size_t len, const char* reason, void* ctx) {
        Impl* self = static_cast<Impl*>(ctx);
        if (self && self->term_cb) {
            self->term_cb(sid, ev_type, data, len, reason, self->term_ctx);
        }
    }

    static int tx_trampoline(int iface_idx, const OykFrame* frame, oyk_u32 peer_ip,
                             oyk_u16 peer_port, oyk_u16 peer_handle, void* ctx) {
        (void)peer_handle;
        Impl* self = static_cast<Impl*>(ctx);
        if (!self || !frame || iface_idx < 0 || iface_idx >= OYK_MAX_INTERFACES) return 0;
        PortableTransportSlot& slot = self->xports[iface_idx];
        if (slot.fd < 0) return 0;
        oyk_u8 raw[512];
        size_t flen = oyk_ax25_encode(frame, raw, sizeof(raw));
        if (flen == 0) return 0;

        if (slot.is_udp) {
            struct sockaddr_in dst;
            memset(&dst, 0, sizeof(dst));
            dst.sin_family = AF_INET;
            dst.sin_addr.s_addr = peer_ip;
            dst.sin_port = htons(peer_port ? peer_port : 10093U);
            ssize_t wr = sendto(slot.fd, raw, flen, 0,
                                reinterpret_cast<const struct sockaddr*>(&dst), sizeof(dst));
            return (wr == static_cast<ssize_t>(flen)) ? 1 : 0;
        }
        if (slot.is_serial) {
            oyk_u8 kiss[768];
            size_t klen = oyk_kiss_encode(0, raw, flen, kiss, sizeof(kiss));
            if (klen == 0) return 0;
            ssize_t wr = write(slot.fd, kiss, klen);
            return (wr == static_cast<ssize_t>(klen)) ? 1 : 0;
        }
        return 0;
    }

    void close_xports() {
        for (int i = 0; i < OYK_MAX_INTERFACES; ++i) {
            if (xports[i].fd >= 0) {
                close(xports[i].fd);
                xports[i].fd = -1;
            }
            xports[i].is_udp = 0;
            xports[i].is_serial = 0;
        }
    }

    void open_xports() {
        close_xports();
        const OykConfig& nc = node.config();
        for (int i = 0; i < nc.iface_count && i < OYK_MAX_INTERFACES; ++i) {
            const OykIfaceConfig& ic = nc.ifaces[i];
            if (mb_strieq(ic.type, "axudp")) {
                int fd = socket(AF_INET, SOCK_DGRAM, 0);
                if (fd >= 0) {
                    int flags = fcntl(fd, F_GETFL, 0);
                    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
                    struct sockaddr_in addr;
                    memset(&addr, 0, sizeof(addr));
                    addr.sin_family = AF_INET;
                    addr.sin_addr.s_addr = ic.host[0] ? inet_addr(ic.host) : htonl(INADDR_ANY);
                    addr.sin_port = htons(ic.port ? ic.port : 10093U);
                    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
                        xports[i].fd = fd;
                        xports[i].is_udp = 1;
                        node.set_iface_status(i, 1, "UDP Listening");
                    } else {
                        close(fd);
                        node.set_iface_status(i, 0, "Bind Failed");
                    }
                }
            } else if (mb_strieq(ic.type, "kiss-serial") && ic.device[0]) {
                int fd = open(ic.device, O_RDWR | O_NOCTTY | O_NONBLOCK);
                if (fd >= 0) {
                    struct termios tio;
                    if (tcgetattr(fd, &tio) == 0) {
                        cfmakeraw(&tio);
                        speed_t spd = (ic.baud >= 38400UL) ? B38400 :
                                      (ic.baud >= 19200UL) ? B19200 : B9600;
                        cfsetispeed(&tio, spd);
                        cfsetospeed(&tio, spd);
                        tcsetattr(fd, TCSANOW, &tio);
                    }
                    xports[i].fd = fd;
                    xports[i].is_serial = 1;
                    oyk_kiss_decoder_init(&xports[i].kiss_dec);
                    node.set_iface_status(i, 1, "KISS Serial Up");
                } else {
                    node.set_iface_status(i, 0, "Serial Offline");
                }
            } else {
                node.set_iface_status(i, 1, "Ready");
            }
        }
    }
};

OykMacOsEngine::OykMacOsEngine() : impl_(new Impl()) {}

OykMacOsEngine::~OykMacOsEngine() {
    stop();
    delete impl_;
}

int OykMacOsEngine::init_and_start(const char* config_path, int seed_demo_data,
                                   char* err_out, size_t err_cap) {
    if (!impl_) return 0;
    stop();
    oyk_config_init_default(&impl_->cfg);
    if (config_path && config_path[0]) {
        mb_copy(impl_->cfg_path, sizeof(impl_->cfg_path), config_path);
        (void)oyk_config_load(config_path, &impl_->cfg, err_out, err_cap);
    }
    if (seed_demo_data) {
        oyk_parse_callsign("KR4OYK-7", &impl_->cfg.callsign);
        oyk_parse_callsign("OYKNOD", &impl_->cfg.alias);
        if (impl_->cfg.iface_count == 0) {
            impl_->cfg.iface_count = 1;
            memset(&impl_->cfg.ifaces[0], 0, sizeof(OykIfaceConfig));
            mb_copy(impl_->cfg.ifaces[0].id, 16, "axudp0");
            mb_copy(impl_->cfg.ifaces[0].type, 16, "axudp");
            mb_copy(impl_->cfg.ifaces[0].host, 64, "127.0.0.1");
            impl_->cfg.ifaces[0].port = 0U;
            mb_copy(impl_->cfg.ifaces[0].netmask, 24, "255.0.0.0");
        }
        if (impl_->cfg.peer_count == 0) {
            impl_->cfg.peer_count = 1;
            memset(&impl_->cfg.peers[0], 0, sizeof(OykPeerConfig));
            mb_copy(impl_->cfg.peers[0].id, 16, "remote0");
            mb_copy(impl_->cfg.peers[0].callsign, 16, "R3MOT-7");
            mb_copy(impl_->cfg.peers[0].interface_id, 16, impl_->cfg.ifaces[0].id);
            mb_copy(impl_->cfg.peers[0].host, 64, "12.34.56.78");
            impl_->cfg.peers[0].port = 10093U;
            impl_->cfg.peers[0].allow_inbound = 1;
            impl_->cfg.peers[0].allow_outbound = 1;
        }
    }
    if (!impl_->node.init(&impl_->cfg, &Impl::tx_trampoline, impl_, err_out, err_cap)) {
        return 0;
    }
    impl_->node.set_local_event_callback(&Impl::local_trampoline, impl_);
    impl_->open_xports();
    impl_->running = 1;
    impl_->last_tick = time(NULL);

    if (seed_demo_data) {
        OykFrame rx_f;
        memset(&rx_f, 0, sizeof(rx_f));
        oyk_parse_callsign("KR4OYK-7", &rx_f.dst);
        oyk_parse_callsign("W4ABC-7", &rx_f.src);
        rx_f.control = 0x3FU;
        impl_->node.on_rx_frame(0, &rx_f, 0x0C22384EU, 10093U, 0U);
        char bmsg[96];
        impl_->node.ban_callsign("W9SPAM-9", 1UL, "Flooding", "7d", bmsg, sizeof(bmsg));
        impl_->node.log_message(OYK_LOG_INFO, "macOS portable node initialized");
    }
    return 1;
}

void OykMacOsEngine::stop() {
    if (!impl_ || !impl_->running) return;
    impl_->close_xports();
    impl_->node.shutdown();
    impl_->running = 0;
}

void OykMacOsEngine::poll() {
    if (!impl_ || !impl_->running) return;
    oyk_u8 buf[1024];
    for (int i = 0; i < OYK_MAX_INTERFACES; ++i) {
        PortableTransportSlot& slot = impl_->xports[i];
        if (slot.fd < 0) continue;
        if (slot.is_udp) {
            struct sockaddr_in src;
            socklen_t slen = sizeof(src);
            ssize_t rd = recvfrom(slot.fd, buf, sizeof(buf), 0,
                                  reinterpret_cast<struct sockaddr*>(&src), &slen);
            if (rd > 0) {
                OykFrame f;
                if (oyk_ax25_decode(buf, static_cast<size_t>(rd), &f)) {
                    impl_->node.on_rx_frame(i, &f, src.sin_addr.s_addr,
                                            ntohs(src.sin_port), 0U);
                }
            }
        } else if (slot.is_serial) {
            ssize_t rd = read(slot.fd, buf, sizeof(buf));
            if (rd > 0) {
                for (ssize_t k = 0; k < rd; ++k) {
                    oyk_u8 kp = 0, kc = 0;
                    if (oyk_kiss_decoder_feed_byte(&slot.kiss_dec, buf[k], &kp, &kc) == 1 &&
                        kc == 0U && slot.kiss_dec.len >= 15U) {
                        OykFrame f;
                        if (oyk_ax25_decode(slot.kiss_dec.buf, slot.kiss_dec.len, &f)) {
                            impl_->node.on_rx_frame(i, &f, 0UL, 0U, 0U);
                        }
                    }
                }
            }
        }
    }
    time_t now = time(NULL);
    if (now != impl_->last_tick) {
        impl_->last_tick = now;
        impl_->node.tick();
    }
}

int OykMacOsEngine::is_running() const { return impl_ ? impl_->running : 0; }
int OykMacOsEngine::uses_portable_core() const { return 1; }
const char* OykMacOsEngine::config_path() const { return impl_ ? impl_->cfg_path : ""; }
const OykConfig& OykMacOsEngine::portable_config() const { return impl_->node.config(); }

void OykMacOsEngine::get_snapshot(OykMacOsSnapshot* out) const {
    if (!out || !impl_) return;
    memset(out, 0, sizeof(*out));
    const OykConfig& c = impl_->node.config();
    oyk_format_callsign(&c.callsign, out->node_callsign, sizeof(out->node_callsign));
    oyk_format_callsign(&c.alias, out->node_alias, sizeof(out->node_alias));
    out->uptime_seconds = impl_->node.uptime_seconds();
    out->accept_incoming = impl_->node.accept_incoming();
    out->manual_deny_incoming = impl_->node.manual_deny_incoming();
    out->active_connections = impl_->node.total_active_connections();
    out->iface_count = impl_->node.iface_count();
    for (int i = 0; i < out->iface_count && i < OYK_MAX_INTERFACES; ++i) {
        const OykIfaceRuntime& rt = impl_->node.iface(i);
        mb_copy(out->ifaces[i].id, sizeof(out->ifaces[i].id), rt.cfg.id);
        mb_copy(out->ifaces[i].type, sizeof(out->ifaces[i].type), rt.cfg.type);
        mb_copy(out->ifaces[i].status, sizeof(out->ifaces[i].status), rt.status);
        out->ifaces[i].running = rt.running;
        out->ifaces[i].active_conns = impl_->node.active_connections_on(i);
        out->ifaces[i].rx_frames = rt.rx_frames;
        out->ifaces[i].tx_frames = rt.tx_frames;
    }
    out->heard_count = impl_->node.new_heard_count();
    for (int i = 0; i < out->heard_count && i < OYK_MAX_NEW_HEARD; ++i) {
        const OykNewHeard& nh = impl_->node.new_heard(i);
        mb_copy(out->heard[i].callsign, sizeof(out->heard[i].callsign), nh.callsign);
        mb_copy(out->heard[i].iface_id, sizeof(out->heard[i].iface_id), nh.iface_id);
        mb_copy(out->heard[i].when_hms, sizeof(out->heard[i].when_hms), nh.time_hms);
    }
    for (int i = 0; i < impl_->node.max_sessions() && out->session_count < OYK_MAX_SESSIONS; ++i) {
        const OykSession& s = impl_->node.session_slot(i);
        if (s.state == OYK_SESS_FREE) continue;
        OykMacOsSessionSnap& ds = out->sessions[out->session_count++];
        ds.id = s.id;
        oyk_format_callsign(&s.remote_call, ds.callsign, sizeof(ds.callsign));
        mb_copy(ds.iface_id, sizeof(ds.iface_id),
                (s.iface_idx >= 0 && s.iface_idx < c.iface_count) ? c.ifaces[s.iface_idx].id : "local");
        mb_copy(ds.direction, sizeof(ds.direction), s.is_local_term ? "LOCAL" : "IN");
        mb_copy(ds.state, sizeof(ds.state), (s.state == OYK_SESS_CONNECTED) ? "CONNECTED" : "OPENING");
        mb_copy(ds.since_hms, sizeof(ds.since_hms), s.since_hms);
        ds.is_local = s.is_local_term;
    }
    out->ban_count = impl_->node.ban_count();
    if (out->ban_count > 16) out->ban_count = 16;
    for (int i = 0; i < out->ban_count; ++i) {
        const OykBanEntry& b = impl_->node.ban_entry(i);
        mb_copy(out->bans[i].callsign, sizeof(out->bans[i].callsign), b.key);
        mb_copy(out->bans[i].banned_at, sizeof(out->bans[i].banned_at), b.banned_at);
        mb_copy(out->bans[i].lift_at, sizeof(out->bans[i].lift_at), b.lift_at);
        out->bans[i].flags = b.flags;
        mb_copy(out->bans[i].reason, sizeof(out->bans[i].reason), b.reason);
    }
    int total_l = impl_->node.console_log_count();
    int start = (total_l > 24) ? (total_l - 24) : 0;
    out->log_count = total_l - start;
    for (int i = 0; i < out->log_count; ++i) {
        mb_copy(out->logs[i], sizeof(out->logs[i]), impl_->node.console_log_line(start + i));
    }
    out->pending_count = oyk_bbs_list_incoming("incoming", out->pending, 12);
    out->approved_count = oyk_bbs_list_approved("files", out->approved, 16);
    out->auto_learned_count = impl_->node.auto_learned_peer_count();
}

void OykMacOsEngine::set_accept_incoming(int allow) {
    if (impl_) impl_->node.set_accept_incoming(allow);
}

int OykMacOsEngine::apply_config(const OykConfig* new_cfg, int save_to_disk,
                                 char* err_out, size_t err_cap) {
    if (!impl_ || !new_cfg) return 0;
    OykConfigApplyResult res;
    int ok = impl_->node.apply_config(new_cfg, save_to_disk, &res);
    if (ok) {
        impl_->cfg = impl_->node.config();
        impl_->open_xports();
    } else if (err_out && err_cap > 0) {
        mb_copy(err_out, err_cap, res.error);
    }
    return ok;
}

int OykMacOsEngine::open_local_terminal(const char* port_id, const char* target_call,
                                        oyk_u32* sess_id_out, char* resolved_out, size_t rcap,
                                        char* err_out, size_t ecap) {
    if (!impl_) return 0;
    int idx = 0;
    const OykConfig& c = impl_->node.config();
    if (port_id && port_id[0]) {
        for (int i = 0; i < c.iface_count; ++i) {
            if (mb_strieq(c.ifaces[i].id, port_id)) { idx = i; break; }
        }
    }
    OykCallsign rcall;
    int is_loc = 0;
    oyk_u32 sid = 0;
    int ok = impl_->node.open_local_terminal(idx, target_call ? target_call : "BBS",
                                             &sid, &rcall, &is_loc, err_out, ecap);
    if (ok) {
        if (sess_id_out) *sess_id_out = sid;
        if (resolved_out) oyk_format_callsign(&rcall, resolved_out, rcap);
    }
    return ok;
}

int OykMacOsEngine::send_local_bytes(oyk_u32 session_id, const oyk_u8* data, size_t len) {
    return impl_ ? impl_->node.send_local_bytes(session_id, data, len) : 0;
}

int OykMacOsEngine::close_local_terminal(oyk_u32 session_id) {
    return impl_ ? impl_->node.close_local_terminal(session_id) : 0;
}

void OykMacOsEngine::set_term_rx_callback(OykMacOsTermRxFn cb, void* ctx) {
    if (!impl_) return;
    impl_->term_cb = cb;
    impl_->term_ctx = ctx;
}

int OykMacOsEngine::terminate_session(oyk_u32 session_id, char* msg_out, size_t cap) {
    return impl_ ? impl_->node.terminate_session(session_id, msg_out, cap) : 0;
}

int OykMacOsEngine::ban_callsign(const char* call_spec, const char* lift_spec,
                                 oyk_u32 flags, const char* reason,
                                 char* msg_out, size_t cap) {
    return impl_ ? impl_->node.ban_callsign(call_spec, flags, reason, lift_spec, msg_out, cap) : 0;
}

int OykMacOsEngine::unban_callsign(const char* call_spec, char* msg_out, size_t cap) {
    return impl_ ? impl_->node.unban_callsign(call_spec, msg_out, cap) : 0;
}

int OykMacOsEngine::persist_auto_learned(char* err_out, size_t cap) {
    return impl_ ? impl_->node.persist_auto_learned_peers(err_out, cap) : 0;
}

void OykMacOsEngine::discard_auto_learned() {
    if (impl_) impl_->node.discard_auto_learned_peers();
}

void OykMacOsEngine::log_message(oyk_u32 level, const char* text) {
    if (impl_) impl_->node.log_message(level, text);
}

#else /* !OYK_MACOS_USE_PORTABLE: C++20 oyk::node::NodeEngine (Intel / Apple Silicon) */

static void mb_tp_hms(std::chrono::system_clock::time_point tp, char* out, size_t cap) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tmb{};
    localtime_r(&tt, &tmb);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmb.tm_hour, tmb.tm_min, tmb.tm_sec);
    mb_copy(out, cap, buf);
}

struct OykMacOsEngine::Impl {
    std::unique_ptr<oyk::node::NodeEngine> engine;
    OykConfig cached_portable_cfg;
    char cfg_path[256];
    OykMacOsTermRxFn term_cb = nullptr;
    void* term_ctx = nullptr;

    Impl() {
        cfg_path[0] = '\0';
        oyk_config_init_default(&cached_portable_cfg);
    }
};

OykMacOsEngine::OykMacOsEngine() : impl_(new Impl()) {}

OykMacOsEngine::~OykMacOsEngine() {
    stop();
    delete impl_;
}

int OykMacOsEngine::init_and_start(const char* config_path, int seed_demo_data,
                                   char* err_out, size_t err_cap) {
    stop();
    oyk_config_init_default(&impl_->cached_portable_cfg);
    oyk::node::NodeConfig nc = oyk::node::NodeConfig::make_default();
    if (config_path && config_path[0]) {
        mb_copy(impl_->cfg_path, sizeof(impl_->cfg_path), config_path);
        std::string nerr;
        if (!oyk::node::NodeConfig::load_from_file(config_path, nc, nerr)) {
            nc = oyk::node::NodeConfig::make_default();
        }
        (void)oyk_config_load(config_path, &impl_->cached_portable_cfg, err_out, err_cap);
    }
    if (seed_demo_data) {
        nc.callsign = oyk::ax25::Callsign("KR4OYK", 7);
        nc.alias = oyk::ax25::Callsign("OYKNOD", 0);
        if (nc.interfaces.empty()) {
            oyk::radio::RadioInterfaceConfig ic;
            ic.id = "axudp0";
            ic.type = "axudp";
            ic.params["bind_host"] = "127.0.0.1";
            ic.params["bind_port"] = "0";
            ic.params["netmask"] = "255.0.0.0";
            nc.interfaces.push_back(ic);
        }
        if (nc.peers.empty()) {
            oyk::radio::PeerConfig pc;
            pc.id = "remote0";
            pc.callsign = "R3MOT-7";
            pc.interface_id = nc.interfaces[0].id;
            pc.host = "12.34.56.78";
            pc.port = 10093;
            nc.peers.push_back(pc);
        }
        oyk_parse_callsign("KR4OYK-7", &impl_->cached_portable_cfg.callsign);
        oyk_parse_callsign("OYKNOD", &impl_->cached_portable_cfg.alias);
    }
    impl_->engine = std::make_unique<oyk::node::NodeEngine>(std::move(nc));
    std::string serr;
    if (!impl_->engine->start(serr)) {
        mb_copy(err_out, err_cap, serr.c_str());
        return 0;
    }
    if (seed_demo_data) {
        std::string bmsg;
        impl_->engine->ban_callsign_with_lift_spec(
            "W9SPAM-9", "7d", &bmsg, false, 1U, "Flooding");
        impl_->engine->logger().info("macOS native C++20 NodeEngine started");
    }
    return 1;
}

void OykMacOsEngine::stop() {
    if (impl_ && impl_->engine) {
        impl_->engine->stop();
        impl_->engine.reset();
    }
}

void OykMacOsEngine::poll() {
    /* NodeEngine runs its own worker threads; nothing needed synchronously. */
}

int OykMacOsEngine::is_running() const {
    return (impl_ && impl_->engine && impl_->engine->is_running()) ? 1 : 0;
}

int OykMacOsEngine::uses_portable_core() const { return 0; }
const char* OykMacOsEngine::config_path() const { return impl_ ? impl_->cfg_path : ""; }
const OykConfig& OykMacOsEngine::portable_config() const { return impl_->cached_portable_cfg; }

void OykMacOsEngine::get_snapshot(OykMacOsSnapshot* out) const {
    if (!out || !impl_ || !impl_->engine) return;
    memset(out, 0, sizeof(*out));
    const oyk::node::NodeSnapshot snap = impl_->engine->snapshot();
    mb_copy(out->node_callsign, sizeof(out->node_callsign), snap.node_callsign.c_str());
    mb_copy(out->node_alias, sizeof(out->node_alias), snap.node_alias.c_str());
    out->uptime_seconds = static_cast<oyk_u32>(snap.uptime_seconds);
    out->accept_incoming = snap.accept_incoming ? 1 : 0;
    out->manual_deny_incoming = snap.manual_deny_incoming ? 1 : 0;
    out->active_connections = static_cast<int>(snap.total_active_connections);
    out->iface_count = static_cast<int>(snap.interfaces.size());
    if (out->iface_count > OYK_MAX_INTERFACES) out->iface_count = OYK_MAX_INTERFACES;
    for (int i = 0; i < out->iface_count; ++i) {
        const oyk::node::InterfaceSnapshot& is = snap.interfaces[static_cast<size_t>(i)];
        mb_copy(out->ifaces[i].id, sizeof(out->ifaces[i].id), is.id.c_str());
        mb_copy(out->ifaces[i].type, sizeof(out->ifaces[i].type), is.type.c_str());
        mb_copy(out->ifaces[i].status, sizeof(out->ifaces[i].status), is.status.c_str());
        out->ifaces[i].running = is.running ? 1 : 0;
        out->ifaces[i].active_conns = static_cast<int>(is.active_connections);
        out->ifaces[i].rx_frames = static_cast<oyk_u32>(is.rx_frames);
        out->ifaces[i].tx_frames = static_cast<oyk_u32>(is.tx_frames);
    }
    out->heard_count = static_cast<int>(snap.last_new_callsigns.size());
    if (out->heard_count > OYK_MAX_NEW_HEARD) out->heard_count = OYK_MAX_NEW_HEARD;
    for (int i = 0; i < out->heard_count; ++i) {
        const oyk::node::NewCallsignRecord& h = snap.last_new_callsigns[static_cast<size_t>(i)];
        mb_copy(out->heard[i].callsign, sizeof(out->heard[i].callsign), h.callsign.c_str());
        mb_copy(out->heard[i].iface_id, sizeof(out->heard[i].iface_id), h.interface_id.c_str());
        mb_tp_hms(h.first_heard, out->heard[i].when_hms, sizeof(out->heard[i].when_hms));
    }
    out->session_count = static_cast<int>(snap.active_sessions.size());
    if (out->session_count > OYK_MAX_SESSIONS) out->session_count = OYK_MAX_SESSIONS;
    for (int i = 0; i < out->session_count; ++i) {
        const oyk::node::SessionSnapshot& ss = snap.active_sessions[static_cast<size_t>(i)];
        out->sessions[i].id = static_cast<oyk_u32>(ss.id);
        mb_copy(out->sessions[i].callsign, sizeof(out->sessions[i].callsign), ss.remote_call.c_str());
        mb_copy(out->sessions[i].iface_id, sizeof(out->sessions[i].iface_id), ss.interface_id.c_str());
        mb_copy(out->sessions[i].direction, sizeof(out->sessions[i].direction),
                ss.direction == oyk::node::SessionDirection::Outbound ? "OUT" : "IN");
        mb_copy(out->sessions[i].state, sizeof(out->sessions[i].state), "CONNECTED");
        mb_tp_hms(ss.connected_at, out->sessions[i].since_hms, sizeof(out->sessions[i].since_hms));
    }
    out->ban_count = static_cast<int>(snap.ban_entries.size());
    if (out->ban_count > 16) out->ban_count = 16;
    for (int i = 0; i < out->ban_count; ++i) {
        const oyk::node::BanEntry& b = snap.ban_entries[static_cast<size_t>(i)];
        mb_copy(out->bans[i].callsign, sizeof(out->bans[i].callsign), b.callsign.c_str());
        mb_copy(out->bans[i].banned_at, sizeof(out->bans[i].banned_at), b.banned_at.c_str());
        mb_copy(out->bans[i].lift_at, sizeof(out->bans[i].lift_at), b.lift_at.c_str());
        out->bans[i].flags = b.flags;
        mb_copy(out->bans[i].reason, sizeof(out->bans[i].reason), b.reason.c_str());
    }
    int total_l = static_cast<int>(snap.recent_logs.size());
    int start = (total_l > 24) ? (total_l - 24) : 0;
    out->log_count = total_l - start;
    for (int i = 0; i < out->log_count; ++i) {
        mb_copy(out->logs[i], sizeof(out->logs[i]), snap.recent_logs[static_cast<size_t>(start + i)].c_str());
    }
    const auto pend = impl_->engine->list_pending_incoming_files();
    out->pending_count = static_cast<int>(pend.size());
    if (out->pending_count > 12) out->pending_count = 12;
    for (int i = 0; i < out->pending_count; ++i) {
        mb_copy(out->pending[i].uploader, sizeof(out->pending[i].uploader),
                pend[static_cast<size_t>(i)].uploader.c_str());
        mb_copy(out->pending[i].filename, sizeof(out->pending[i].filename),
                pend[static_cast<size_t>(i)].filename.c_str());
        out->pending[i].size = static_cast<oyk_u32>(pend[static_cast<size_t>(i)].size);
    }
    const auto appr = impl_->engine->list_approved_files();
    out->approved_count = static_cast<int>(appr.size());
    if (out->approved_count > 16) out->approved_count = 16;
    for (int i = 0; i < out->approved_count; ++i) {
        mb_copy(out->approved[i].rel_path, sizeof(out->approved[i].rel_path),
                appr[static_cast<size_t>(i)].rel_path.c_str());
        out->approved[i].size = static_cast<oyk_u32>(appr[static_cast<size_t>(i)].size);
    }
    out->auto_learned_count = static_cast<int>(impl_->engine->auto_learned_peer_count());
}

void OykMacOsEngine::set_accept_incoming(int allow) {
    if (impl_ && impl_->engine) impl_->engine->set_accept_incoming(allow != 0);
}

int OykMacOsEngine::apply_config(const OykConfig* new_cfg, int save_to_disk,
                                 char* err_out, size_t err_cap) {
    if (!impl_ || !impl_->engine || !new_cfg) return 0;
    impl_->cached_portable_cfg = *new_cfg;
    const char* tmp_p = impl_->cfg_path[0] ? impl_->cfg_path : "/tmp/oyk_macos_cfg.cfg";
    if (!oyk_config_save(new_cfg, tmp_p)) {
        mb_copy(err_out, err_cap, "Unable to save macOS config.");
        return 0;
    }
    oyk::node::NodeConfig nc;
    std::string lerr;
    if (!oyk::node::NodeConfig::load_from_file(tmp_p, nc, lerr)) {
        mb_copy(err_out, err_cap, lerr.c_str());
        return 0;
    }
    const auto res = impl_->engine->apply_config(std::move(nc), save_to_disk != 0);
    if (!res.applied) mb_copy(err_out, err_cap, res.error.c_str());
    return res.applied ? 1 : 0;
}

int OykMacOsEngine::open_local_terminal(const char* port_id, const char* target_call,
                                        oyk_u32* sess_id_out, char* resolved_out, size_t rcap,
                                        char* err_out, size_t ecap) {
    if (!impl_ || !impl_->engine) return 0;
    oyk::node::NodeEngine::LocalTerminalCallbacks cbs;
    Impl* self = impl_;
    cbs.on_connected = [self](uint64_t sid) {
        if (self->term_cb) self->term_cb(static_cast<oyk_u32>(sid), 1, NULL, 0, NULL, self->term_ctx);
    };
    cbs.on_rx_bytes = [self](uint64_t sid, const std::vector<uint8_t>& b) {
        if (self->term_cb) {
            self->term_cb(static_cast<oyk_u32>(sid), 2, b.data(), b.size(), NULL, self->term_ctx);
        }
    };
    cbs.on_closed = [self](uint64_t sid, const std::string& rsn) {
        if (self->term_cb) {
            self->term_cb(static_cast<oyk_u32>(sid), 3, NULL, 0, rsn.c_str(), self->term_ctx);
        }
    };
    uint64_t sid64 = 0;
    oyk::ax25::Callsign rcs;
    std::string nerr;
    if (!impl_->engine->open_local_terminal(port_id ? port_id : "",
                                            target_call ? target_call : "BBS",
                                            cbs, sid64, &rcs, nerr)) {
        mb_copy(err_out, ecap, nerr.c_str());
        return 0;
    }
    if (sess_id_out) *sess_id_out = static_cast<oyk_u32>(sid64);
    if (resolved_out) mb_copy(resolved_out, rcap, rcs.to_string().c_str());
    return 1;
}

int OykMacOsEngine::send_local_bytes(oyk_u32 session_id, const oyk_u8* data, size_t len) {
    if (!impl_ || !impl_->engine || !data || len == 0) return 0;
    std::vector<uint8_t> vec(data, data + len);
    return impl_->engine->send_local_terminal_bytes(session_id, vec) ? 1 : 0;
}

int OykMacOsEngine::close_local_terminal(oyk_u32 session_id) {
    if (!impl_ || !impl_->engine) return 0;
    return impl_->engine->close_local_terminal(session_id) ? 1 : 0;
}

void OykMacOsEngine::set_term_rx_callback(OykMacOsTermRxFn cb, void* ctx) {
    if (!impl_) return;
    impl_->term_cb = cb;
    impl_->term_ctx = ctx;
}

int OykMacOsEngine::terminate_session(oyk_u32 session_id, char* msg_out, size_t cap) {
    if (!impl_ || !impl_->engine) return 0;
    std::string msg;
    bool ok = impl_->engine->terminate_session(session_id, &msg);
    mb_copy(msg_out, cap, msg.c_str());
    return ok ? 1 : 0;
}

int OykMacOsEngine::ban_callsign(const char* call_spec, const char* lift_spec,
                                 oyk_u32 flags, const char* reason,
                                 char* msg_out, size_t cap) {
    if (!impl_ || !impl_->engine) return 0;
    std::string msg;
    bool ok = impl_->engine->ban_callsign_with_lift_spec(
        call_spec ? call_spec : "", lift_spec ? lift_spec : "permanent",
        &msg, true, flags, reason ? reason : "Banned by sysop.");
    mb_copy(msg_out, cap, msg.c_str());
    return ok ? 1 : 0;
}

int OykMacOsEngine::unban_callsign(const char* call_spec, char* msg_out, size_t cap) {
    if (!impl_ || !impl_->engine) return 0;
    std::string msg;
    bool ok = impl_->engine->unban_callsign(call_spec ? call_spec : "", &msg);
    mb_copy(msg_out, cap, msg.c_str());
    return ok ? 1 : 0;
}

int OykMacOsEngine::persist_auto_learned(char* err_out, size_t cap) {
    if (!impl_ || !impl_->engine) return 0;
    std::string err;
    bool ok = impl_->engine->persist_auto_learned_peers(&err);
    mb_copy(err_out, cap, err.c_str());
    return ok ? 1 : 0;
}

void OykMacOsEngine::discard_auto_learned() {
    if (impl_ && impl_->engine) impl_->engine->discard_auto_learned_peers();
}

void OykMacOsEngine::log_message(oyk_u32 level, const char* text) {
    (void)level;
    if (impl_ && impl_->engine && text) impl_->engine->logger().info(text);
}

#endif
