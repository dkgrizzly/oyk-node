// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include "oyk/radio/radio_interface.hpp"

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <thread>
#include <vector>

namespace oyk::radio {

// AXUDP radio interface: carries raw AX.25 frames + 2-byte CRC-16-CCITT FCS
// inside UDP datagrams (compatible with LinBPQ, BPQ32, Direwolf, JNOS, ax25ipd).
class AxudpInterface : public RadioInterface {
public:
    explicit AxudpInterface(const RadioInterfaceConfig& config);
    ~AxudpInterface() override;

    AxudpInterface(const AxudpInterface&) = delete;
    AxudpInterface& operator=(const AxudpInterface&) = delete;

    std::string id() const override { return id_; }
    std::string type() const override { return "axudp"; }
    std::string description() const override { return description_; }
    std::string status_summary() const override;

    bool start(FrameCallback on_frame, std::string& error_out) override;
    void stop() override;
    bool is_running() const override { return running_.load(); }

    bool send_frame(const ax25::Frame& frame,
                    const std::string& target_endpoint = "") override;

    RadioInterfaceStats stats() const override;

    // Useful when bind_port == 0 (ephemeral port in tests) or for diagnostics.
    uint16_t bound_port() const noexcept { return bound_port_.load(); }

    // Manually register a callsign -> host:port route (or broadcast peer if callsign empty).
    bool add_peer(const std::string& callsign_or_empty,
                  const std::string& host,
                  uint16_t port);
    void sync_peers(const std::vector<PeerConfig>& peers) override;

private:
    struct Endpoint {
        sockaddr_in addr{};
        std::string text; // "a.b.c.d:port"
    };

    void rx_loop();
    static bool resolve_endpoint(const std::string& host, uint16_t port, Endpoint& out);
    static bool parse_host_port(const std::string& spec, std::string& host, uint16_t& port);
    static std::string format_sockaddr(const sockaddr_in& addr);
    void parse_peer_spec(const std::string& spec);
    bool ip_allowed_by_netmask(const sockaddr_in& addr) const noexcept;
    bool send_datagram(const std::vector<uint8_t>& datagram, const sockaddr_in& dest);

    std::string id_;
    std::string description_;
    std::string bind_host_ = "0.0.0.0";
    std::string netmask_;
    uint32_t bind_ip_net_ = 0;
    uint32_t netmask_net_ = 0;
    uint8_t dmr_prefix_ = 12;
    uint16_t configured_port_ = 10093;
    bool auto_learn_ = true;

    std::atomic<bool> running_{false};
    std::atomic<uint16_t> bound_port_{0};

    int sock_fd_ = -1;
    int wake_pipe_[2] = {-1, -1};
    std::thread rx_thread_;

    FrameCallback on_frame_;

    bool add_peer_locked(const std::string& callsign_or_empty,
                         const std::string& host,
                         uint16_t port,
                         bool record_manual);

    mutable std::mutex peer_mutex_;
    std::map<std::string, Endpoint> callsign_peers_; // canonical callsign / base -> endpoint
    std::vector<Endpoint> broadcast_peers_;
    std::map<std::string, Endpoint> manual_callsign_peers_;
    std::vector<Endpoint> manual_broadcast_peers_;

    mutable std::mutex stats_mutex_;
    RadioInterfaceStats stats_{};
};

} // namespace oyk::radio
