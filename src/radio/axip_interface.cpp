// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/radio/axip_interface.hpp"

#include "oyk/ax25/callsign.hpp"
#include "oyk/ax25/crc.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace oyk::radio {

namespace {

constexpr std::size_t MAX_AXIP_PACKET = 4096;

bool parse_bool(const std::string& v, bool fallback) {
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return fallback;
}

} // namespace

AxipInterface::AxipInterface(const RadioInterfaceConfig& config)
    : id_(config.id.empty() ? "axip0" : config.id),
      description_(config.description.empty() ? "AXIP (IP Proto 93)" : config.description) {
    if (auto it = config.params.find("bind_host"); it != config.params.end() && !it->second.empty()) {
        std::string resolved_bh;
        if (resolve_host_or_dmr_id(it->second, 12, resolved_bh)) {
            bind_host_ = resolved_bh;
        } else {
            bind_host_ = it->second;
        }
    }
    if (auto it = config.params.find("netmask"); it != config.params.end() && !it->second.empty()) {
        netmask_ = it->second;
        in_addr ma{};
        if (inet_pton(AF_INET, netmask_.c_str(), &ma) == 1) {
            netmask_net_ = ma.s_addr;
        }
    }
    if (!bind_host_.empty() && bind_host_ != "0.0.0.0" && bind_host_ != "*") {
        in_addr ba{};
        if (inet_pton(AF_INET, bind_host_.c_str(), &ba) == 1) {
            bind_ip_net_ = ba.s_addr;
        }
    }
    dmr_prefix_ = dmr_prefix_for_iface(config);
    if (auto it = config.params.find("auto_learn"); it != config.params.end()) {
        auto_learn_ = parse_bool(it->second, true);
    }

    for (const auto& peer_spec : config.peers) {
        parse_peer_spec(peer_spec);
    }
    if (auto it = config.params.find("peers"); it != config.params.end() && !it->second.empty()) {
        std::stringstream ss(it->second);
        std::string item;
        while (std::getline(ss, item, ',')) {
            parse_peer_spec(item);
        }
    }
}

AxipInterface::~AxipInterface() {
    stop();
}

std::string AxipInterface::status_summary() const {
    return (running_.load() ? "UP ip93 " : "DOWN ip93 ") + bind_host_;
}

bool AxipInterface::extract_ipv4_axip_payload(const uint8_t* raw_ip_pkt,
                                              std::size_t total_len,
                                              const uint8_t*& payload_out,
                                              std::size_t& payload_len_out) noexcept {
    if (!raw_ip_pkt || total_len < 20) {
        return false;
    }
    const uint8_t version = static_cast<uint8_t>((raw_ip_pkt[0] >> 4) & 0x0F);
    if (version != 4) {
        return false;
    }
    const std::size_t ihl = static_cast<std::size_t>(raw_ip_pkt[0] & 0x0F) * 4;
    if (ihl < 20 || ihl >= total_len) {
        return false;
    }
    if (raw_ip_pkt[9] != static_cast<uint8_t>(IPPROTO_AXIP)) {
        return false;
    }
    // Honor IPv4 total_length field if smaller than recv buffer length
    const std::size_t ip_total_len = static_cast<std::size_t>(
        (static_cast<uint16_t>(raw_ip_pkt[2]) << 8) | raw_ip_pkt[3]);
    std::size_t effective_len = total_len;
    if (ip_total_len >= ihl && ip_total_len < effective_len) {
        effective_len = ip_total_len;
    }
    if (effective_len <= ihl) {
        return false;
    }

    payload_out = raw_ip_pkt + ihl;
    payload_len_out = effective_len - ihl;
    return true;
}

bool AxipInterface::resolve_host(const std::string& host, PeerAddr& out) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;

    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
        // Fallback without socktype filter
        hints.ai_socktype = 0;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
            return false;
        }
    }

    std::memcpy(&out.addr, res->ai_addr, sizeof(sockaddr_in));
    out.addr.sin_port = 0;
    out.ip_text = format_ip(out.addr);
    freeaddrinfo(res);
    return true;
}

std::string AxipInterface::format_ip(const sockaddr_in& addr) {
    char ip_buf[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &addr.sin_addr, ip_buf, sizeof(ip_buf));
    return std::string(ip_buf);
}

void AxipInterface::parse_peer_spec(const std::string& raw) {
    std::size_t first = raw.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return;
    std::size_t last = raw.find_last_not_of(" \t\r\n");
    std::string spec = raw.substr(first, last - first + 1);

    // Formats: "CALLSIGN@host", "CALLSIGN host", or "host"
    std::string call_str;
    std::string host_str;

    auto at = spec.find('@');
    auto space = spec.find_first_of(" \t");
    if (at != std::string::npos) {
        call_str = spec.substr(0, at);
        host_str = spec.substr(at + 1);
    } else if (space != std::string::npos) {
        call_str = spec.substr(0, space);
        std::size_t h_start = spec.find_first_not_of(" \t", space);
        if (h_start != std::string::npos) {
            host_str = spec.substr(h_start);
        }
    } else {
        host_str = spec;
    }

    if (!host_str.empty()) {
        add_peer_locked(call_str, host_str, false);
    }
}

bool AxipInterface::ip_allowed_by_netmask(const sockaddr_in& addr) const noexcept {
    if (netmask_net_ == 0 || bind_ip_net_ == 0) return true;
    return (addr.sin_addr.s_addr & netmask_net_) == (bind_ip_net_ & netmask_net_);
}

bool AxipInterface::add_peer_locked(const std::string& callsign_or_empty,
                                    const std::string& host,
                                    bool record_manual) {
    std::string resolved_host;
    if (!resolve_host_or_dmr_id(host, dmr_prefix_, resolved_host)) {
        return false;
    }
    PeerAddr peer;
    if (!resolve_host(resolved_host, peer)) {
        return false;
    }
    if (!ip_allowed_by_netmask(peer.addr)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(peer_mutex_);
    if (callsign_or_empty.empty() || callsign_or_empty == "*") {
        broadcast_peers_.push_back(peer);
        if (record_manual) manual_broadcast_peers_.push_back(peer);
        return true;
    }
    auto parsed = ax25::Callsign::parse(callsign_or_empty);
    if (!parsed) {
        return false;
    }
    callsign_peers_[parsed->str()] = peer;
    if (record_manual) manual_callsign_peers_[parsed->str()] = peer;
    if (parsed->ssid() == 0) {
        callsign_peers_[parsed->base()] = peer;
        if (record_manual) manual_callsign_peers_[parsed->base()] = peer;
    }
    return true;
}

bool AxipInterface::add_peer(const std::string& callsign_or_empty, const std::string& host) {
    return add_peer_locked(callsign_or_empty, host, true);
}

void AxipInterface::sync_peers(const std::vector<PeerConfig>& peers) {
    {
        std::lock_guard<std::mutex> lock(peer_mutex_);
        callsign_peers_ = manual_callsign_peers_;
        broadcast_peers_ = manual_broadcast_peers_;
    }
    for (const auto& p : peers) {
        if (!p.interface_id.empty() && p.interface_id != id_) continue;
        if (p.host.empty()) continue;
        (void)add_peer_locked(p.callsign, p.host, false);
    }
}

bool AxipInterface::start(FrameCallback on_frame, std::string& error_out) {
    if (running_.load()) {
        return true;
    }

    if (pipe(wake_pipe_) != 0) {
        error_out = "Failed to create wake pipe: " + std::string(std::strerror(errno));
        return false;
    }
    fcntl(wake_pipe_[0], F_SETFL, O_NONBLOCK);
    fcntl(wake_pipe_[1], F_SETFL, O_NONBLOCK);

    sock_fd_ = socket(AF_INET, SOCK_RAW, IPPROTO_AXIP);
    if (sock_fd_ < 0) {
        int saved = errno;
        if (saved == EPERM || saved == EACCES) {
            error_out = "AXIP (IP protocol 93) requires CAP_NET_RAW or root privileges "
                        "(e.g. 'sudo setcap cap_net_raw+ep oyk-node')";
        } else {
            error_out = "Failed to create AXIP raw socket (proto 93): " +
                        std::string(std::strerror(saved));
        }
        close(wake_pipe_[0]);
        close(wake_pipe_[1]);
        wake_pipe_[0] = wake_pipe_[1] = -1;
        return false;
    }

    if (!bind_host_.empty() && bind_host_ != "0.0.0.0" && bind_host_ != "*") {
        PeerAddr bind_peer;
        if (!resolve_host(bind_host_, bind_peer)) {
            error_out = "Invalid AXIP bind_host: " + bind_host_;
            close(sock_fd_);
            sock_fd_ = -1;
            close(wake_pipe_[0]);
            close(wake_pipe_[1]);
            wake_pipe_[0] = wake_pipe_[1] = -1;
            return false;
        }
        if (bind(sock_fd_, reinterpret_cast<sockaddr*>(&bind_peer.addr), sizeof(bind_peer.addr)) < 0) {
            error_out = "Failed to bind AXIP socket on " + bind_host_ + ": " + std::strerror(errno);
            close(sock_fd_);
            sock_fd_ = -1;
            close(wake_pipe_[0]);
            close(wake_pipe_[1]);
            wake_pipe_[0] = wake_pipe_[1] = -1;
            return false;
        }
    }

    on_frame_ = std::move(on_frame);
    running_.store(true);
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.running = true;
    }

    rx_thread_ = std::thread(&AxipInterface::rx_loop, this);
    return true;
}

void AxipInterface::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (wake_pipe_[1] >= 0) {
        uint8_t byte = 1;
        [[maybe_unused]] ssize_t ignored = write(wake_pipe_[1], &byte, 1);
    }
    if (rx_thread_.joinable()) {
        rx_thread_.join();
    }
    if (sock_fd_ >= 0) {
        close(sock_fd_);
        sock_fd_ = -1;
    }
    if (wake_pipe_[0] >= 0) {
        close(wake_pipe_[0]);
        wake_pipe_[0] = -1;
    }
    if (wake_pipe_[1] >= 0) {
        close(wake_pipe_[1]);
        wake_pipe_[1] = -1;
    }
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.running = false;
    }
}

void AxipInterface::rx_loop() {
    uint8_t buffer[MAX_AXIP_PACKET];

    while (running_.load()) {
        pollfd fds[2];
        fds[0].fd = sock_fd_;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = wake_pipe_[0];
        fds[1].events = POLLIN;
        fds[1].revents = 0;

        int ret = poll(fds, 2, 200);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;

        if (fds[1].revents & POLLIN) {
            uint8_t drain[16];
            while (read(wake_pipe_[0], drain, sizeof(drain)) > 0) {}
            if (!running_.load()) break;
        }

        if (fds[0].revents & POLLIN) {
            sockaddr_in sender{};
            socklen_t sender_len = sizeof(sender);
            ssize_t n = recvfrom(sock_fd_, buffer, sizeof(buffer), 0,
                                 reinterpret_cast<sockaddr*>(&sender), &sender_len);
            if (n <= 0) continue;
            if (!ip_allowed_by_netmask(sender)) {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.rx_errors++;
                continue;
            }

            const uint8_t* payload = nullptr;
            std::size_t payload_len = 0;
            if (!extract_ipv4_axip_payload(buffer, static_cast<std::size_t>(n),
                                           payload, payload_len)) {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.rx_errors++;
                continue;
            }

            if (!ax25::verify_fcs(payload, payload_len)) {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.rx_errors++;
                continue;
            }

            auto decoded = ax25::Frame::decode(payload, payload_len - 2);
            if (!decoded) {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.rx_errors++;
                continue;
            }

            sender.sin_port = 0;
            PeerAddr sender_peer{sender, format_ip(sender)};
            if (auto_learn_ && decoded->source.valid()) {
                std::lock_guard<std::mutex> lock(peer_mutex_);
                callsign_peers_[decoded->source.str()] = sender_peer;
            }

            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.rx_frames++;
            }

            if (on_frame_) {
                RadioFrameMeta meta;
                meta.interface_id = id_;
                meta.peer_endpoint = sender_peer.ip_text;
                meta.received_at = std::chrono::system_clock::now();
                on_frame_(*decoded, meta);
            }
        }
    }
}

bool AxipInterface::send_ip_payload(const std::vector<uint8_t>& payload, const sockaddr_in& dest) {
    if (sock_fd_ < 0 || !running_.load()) {
        return false;
    }
    if (!ip_allowed_by_netmask(dest)) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.tx_errors++;
        return false;
    }
    ssize_t sent = sendto(sock_fd_, payload.data(), payload.size(), 0,
                          reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
    if (sent != static_cast<ssize_t>(payload.size())) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.tx_errors++;
        return false;
    }
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.tx_frames++;
    return true;
}

bool AxipInterface::send_frame(const ax25::Frame& frame, const std::string& target_endpoint) {
    if (!running_.load() || sock_fd_ < 0) {
        return false;
    }

    std::vector<uint8_t> raw = frame.encode();
    if (raw.empty()) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.tx_errors++;
        return false;
    }
    ax25::append_fcs(raw);

    if (!target_endpoint.empty()) {
        PeerAddr ep;
        std::string resolved_ep;
        if (resolve_host_or_dmr_id(target_endpoint, dmr_prefix_, resolved_ep) &&
            resolve_host(resolved_ep, ep)) {
            return send_ip_payload(raw, ep.addr);
        }
    }

    ax25::Callsign next_hop = frame.destination;
    for (const auto& digi : frame.digipeaters) {
        if (!digi.repeated) {
            next_hop = digi.call;
            break;
        }
    }

    std::vector<PeerAddr> targets;
    {
        std::lock_guard<std::mutex> lock(peer_mutex_);
        if (frame.type() != ax25::FrameType::UI) {
            if (auto it = callsign_peers_.find(next_hop.str()); it != callsign_peers_.end()) {
                targets.push_back(it->second);
            } else if (auto it2 = callsign_peers_.find(next_hop.base()); it2 != callsign_peers_.end()) {
                targets.push_back(it2->second);
            }
        }
        if (targets.empty()) {
            targets = broadcast_peers_;
        }
    }

    if (targets.empty()) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.tx_errors++;
        return false;
    }

    bool any_ok = false;
    for (const auto& ep : targets) {
        if (send_ip_payload(raw, ep.addr)) {
            any_ok = true;
        }
    }
    return any_ok;
}

RadioInterfaceStats AxipInterface::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

} // namespace oyk::radio
