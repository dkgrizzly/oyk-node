// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include "oyk/radio/radio_interface.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <thread>
#include <vector>

namespace oyk::radio {

constexpr int IPPROTO_AXIP = 93; // RFC 1226: IP Protocol 93 (AX.25)

// AXIP radio interface: encapsulates raw AX.25 frames + 2-byte CRC-16-CCITT FCS
// directly inside IPv4 packets with IP protocol 93 (RFC 1226).
// Note: requires root or CAP_NET_RAW on Linux.
class AxipInterface : public RadioInterface {
public:
    explicit AxipInterface(const RadioInterfaceConfig& config);
    ~AxipInterface() override;

    AxipInterface(const AxipInterface&) = delete;
    AxipInterface& operator=(const AxipInterface&) = delete;

    std::string id() const override { return id_; }
    std::string type() const override { return "axip"; }
    std::string description() const override { return description_; }
    std::string status_summary() const override;

    bool start(FrameCallback on_frame, std::string& error_out) override;
    void stop() override;
    bool is_running() const override { return running_.load(); }

    bool send_frame(const ax25::Frame& frame,
                    const std::string& target_endpoint = "") override;

    RadioInterfaceStats stats() const override;

    // Register a callsign -> IP/host route (or broadcast peer if callsign is empty/"*").
    bool add_peer(const std::string& callsign_or_empty, const std::string& host);
    void sync_peers(const std::vector<PeerConfig>& peers) override;

    // Parses an IPv4 raw socket packet and extracts the protocol-93 payload.
    // Exposed as a static helper for deterministic unit testing without CAP_NET_RAW.
    static bool extract_ipv4_axip_payload(const uint8_t* raw_ip_pkt,
                                          std::size_t total_len,
                                          const uint8_t*& payload_out,
                                          std::size_t& payload_len_out) noexcept;

private:
    struct PeerAddr {
        sockaddr_in addr{};
        std::string ip_text;
    };

    void rx_loop();
    static bool resolve_host(const std::string& host, PeerAddr& out);
    static std::string format_ip(const sockaddr_in& addr);
    void parse_peer_spec(const std::string& spec);
    bool ip_allowed_by_netmask(const sockaddr_in& addr) const noexcept;
    bool send_ip_payload(const std::vector<uint8_t>& payload, const sockaddr_in& dest);

    std::string id_;
    std::string description_;
    std::string bind_host_ = "0.0.0.0";
    std::string netmask_;
    uint32_t bind_ip_net_ = 0;
    uint32_t netmask_net_ = 0;
    uint8_t dmr_prefix_ = 12;
    bool auto_learn_ = true;

    std::atomic<bool> running_{false};
    int sock_fd_ = -1;
    int wake_pipe_[2] = {-1, -1};
    std::thread rx_thread_;

    FrameCallback on_frame_;

    bool add_peer_locked(const std::string& callsign_or_empty,
                         const std::string& host,
                         bool record_manual);

    mutable std::mutex peer_mutex_;
    std::map<std::string, PeerAddr> callsign_peers_;
    std::vector<PeerAddr> broadcast_peers_;
    std::map<std::string, PeerAddr> manual_callsign_peers_;
    std::vector<PeerAddr> manual_broadcast_peers_;

    mutable std::mutex stats_mutex_;
    RadioInterfaceStats stats_{};
};

} // namespace oyk::radio
