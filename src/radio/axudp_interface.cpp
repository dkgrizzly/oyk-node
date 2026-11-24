#include "oyk/radio/axudp_interface.hpp"

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
constexpr std::size_t MAX_AXUDP_PACKET = 2048;

bool parse_bool(const std::string& v, bool fallback) {
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return fallback;
}
} // namespace

AxudpInterface::AxudpInterface(const RadioInterfaceConfig& config)
    : id_(config.id.empty() ? "axudp0" : config.id),
      description_(config.description.empty() ? "AXUDP Port" : config.description) {
    if (auto it = config.params.find("bind_host"); it != config.params.end() && !it->second.empty()) {
        bind_host_ = it->second;
    }
    if (auto it = config.params.find("bind_port"); it != config.params.end() && !it->second.empty()) {
        try {
            int p = std::stoi(it->second);
            if (p >= 0 && p <= 65535) {
                configured_port_ = static_cast<uint16_t>(p);
            }
        } catch (...) {
        }
    }
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

AxudpInterface::~AxudpInterface() {
    stop();
}

std::string AxudpInterface::status_summary() const {
    std::ostringstream oss;
    if (running_.load()) {
        oss << "UP " << bind_host_ << ":" << bound_port_.load();
    } else {
        oss << "DOWN " << bind_host_ << ":" << configured_port_;
    }
    return oss.str();
}

bool AxudpInterface::resolve_endpoint(const std::string& host, uint16_t port, Endpoint& out) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* res = nullptr;
    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        return false;
    }

    std::memcpy(&out.addr, res->ai_addr, sizeof(sockaddr_in));
    out.text = format_sockaddr(out.addr);
    freeaddrinfo(res);
    return true;
}

bool AxudpInterface::parse_host_port(const std::string& spec, std::string& host, uint16_t& port) {
    auto colon = spec.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= spec.size()) {
        return false;
    }
    host = spec.substr(0, colon);
    try {
        int p = std::stoi(spec.substr(colon + 1));
        if (p <= 0 || p > 65535) {
            return false;
        }
        port = static_cast<uint16_t>(p);
        return true;
    } catch (...) {
        return false;
    }
}

std::string AxudpInterface::format_sockaddr(const sockaddr_in& addr) {
    char ip_buf[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &addr.sin_addr, ip_buf, sizeof(ip_buf));
    return std::string(ip_buf) + ":" + std::to_string(ntohs(addr.sin_port));
}

void AxudpInterface::parse_peer_spec(const std::string& raw) {
    // Trim whitespace
    std::size_t first = raw.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return;
    std::size_t last = raw.find_last_not_of(" \t\r\n");
    std::string spec = raw.substr(first, last - first + 1);

    // Supported formats:
    //   "CALLSIGN@host:port"
    //   "CALLSIGN host:port"
    //   "host:port" (broadcast / default peer)
    std::string call_str;
    std::string hp_str;

    auto at = spec.find('@');
    auto space = spec.find_first_of(" \t");
    if (at != std::string::npos) {
        call_str = spec.substr(0, at);
        hp_str = spec.substr(at + 1);
    } else if (space != std::string::npos) {
        call_str = spec.substr(0, space);
        std::size_t hp_start = spec.find_first_not_of(" \t", space);
        if (hp_start != std::string::npos) {
            hp_str = spec.substr(hp_start);
        }
    } else {
        hp_str = spec;
    }

    std::string host;
    uint16_t port = 0;
    if (!parse_host_port(hp_str, host, port)) {
        return;
    }
    add_peer(call_str, host, port);
}

bool AxudpInterface::add_peer(const std::string& callsign_or_empty,
                              const std::string& host,
                              uint16_t port) {
    Endpoint ep;
    if (!resolve_endpoint(host, port, ep)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(peer_mutex_);
    if (callsign_or_empty.empty() || callsign_or_empty == "*") {
        broadcast_peers_.push_back(ep);
        return true;
    }
    auto parsed = ax25::Callsign::parse(callsign_or_empty);
    if (!parsed) {
        return false;
    }
    callsign_peers_[parsed->str()] = ep;
    // Also add base callsign fallback if SSID was 0 and not already explicitly mapped
    if (parsed->ssid() == 0) {
        callsign_peers_[parsed->base()] = ep;
    }
    return true;
}

bool AxudpInterface::start(FrameCallback on_frame, std::string& error_out) {
    if (running_.load()) {
        return true;
    }

    if (pipe(wake_pipe_) != 0) {
        error_out = "Failed to create wake pipe: " + std::string(std::strerror(errno));
        return false;
    }
    fcntl(wake_pipe_[0], F_SETFL, O_NONBLOCK);
    fcntl(wake_pipe_[1], F_SETFL, O_NONBLOCK);

    sock_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd_ < 0) {
        error_out = "Failed to create UDP socket: " + std::string(std::strerror(errno));
        close(wake_pipe_[0]);
        close(wake_pipe_[1]);
        wake_pipe_[0] = wake_pipe_[1] = -1;
        return false;
    }

    int reuse = 1;
    setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(configured_port_);
    if (bind_host_.empty() || bind_host_ == "0.0.0.0" || bind_host_ == "*") {
        bind_addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, bind_host_.c_str(), &bind_addr.sin_addr) != 1) {
        Endpoint resolved;
        if (!resolve_endpoint(bind_host_, configured_port_, resolved)) {
            error_out = "Invalid bind_host: " + bind_host_;
            close(sock_fd_);
            sock_fd_ = -1;
            close(wake_pipe_[0]);
            close(wake_pipe_[1]);
            wake_pipe_[0] = wake_pipe_[1] = -1;
            return false;
        }
        bind_addr = resolved.addr;
    }

    if (bind(sock_fd_, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        error_out = "Failed to bind AXUDP socket on " + bind_host_ + ":" +
                    std::to_string(configured_port_) + ": " + std::strerror(errno);
        close(sock_fd_);
        sock_fd_ = -1;
        close(wake_pipe_[0]);
        close(wake_pipe_[1]);
        wake_pipe_[0] = wake_pipe_[1] = -1;
        return false;
    }

    sockaddr_in actual_addr{};
    socklen_t actual_len = sizeof(actual_addr);
    if (getsockname(sock_fd_, reinterpret_cast<sockaddr*>(&actual_addr), &actual_len) == 0) {
        bound_port_.store(ntohs(actual_addr.sin_port));
    } else {
        bound_port_.store(configured_port_);
    }

    on_frame_ = std::move(on_frame);
    running_.store(true);
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.running = true;
    }

    rx_thread_ = std::thread(&AxudpInterface::rx_loop, this);
    return true;
}

void AxudpInterface::stop() {
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

void AxudpInterface::rx_loop() {
    uint8_t buffer[MAX_AXUDP_PACKET];

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
            if (n <= 0) {
                continue;
            }

            const std::size_t packet_len = static_cast<std::size_t>(n);
            if (!ax25::verify_fcs(buffer, packet_len)) {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.rx_errors++;
                continue;
            }

            auto decoded = ax25::Frame::decode(buffer, packet_len - 2);
            if (!decoded) {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.rx_errors++;
                continue;
            }

            Endpoint sender_ep{sender, format_sockaddr(sender)};
            if (auto_learn_ && decoded->source.valid()) {
                std::lock_guard<std::mutex> lock(peer_mutex_);
                callsign_peers_[decoded->source.str()] = sender_ep;
            }

            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.rx_frames++;
            }

            if (on_frame_) {
                RadioFrameMeta meta;
                meta.interface_id = id_;
                meta.peer_endpoint = sender_ep.text;
                meta.received_at = std::chrono::system_clock::now();
                on_frame_(*decoded, meta);
            }
        }
    }
}

bool AxudpInterface::send_datagram(const std::vector<uint8_t>& datagram, const sockaddr_in& dest) {
    if (sock_fd_ < 0 || !running_.load()) {
        return false;
    }
    ssize_t sent = sendto(sock_fd_, datagram.data(), datagram.size(), 0,
                          reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
    if (sent != static_cast<ssize_t>(datagram.size())) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.tx_errors++;
        return false;
    }
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.tx_frames++;
    return true;
}

bool AxudpInterface::send_frame(const ax25::Frame& frame, const std::string& target_endpoint) {
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

    // 1. Explicit target endpoint (e.g. reply to active session's UDP source)
    if (!target_endpoint.empty()) {
        std::string host;
        uint16_t port = 0;
        Endpoint ep;
        if (parse_host_port(target_endpoint, host, port) && resolve_endpoint(host, port, ep)) {
            return send_datagram(raw, ep.addr);
        }
    }

    // 2. Determine next-hop callsign (first unrepeated digipeater, or destination)
    ax25::Callsign next_hop = frame.destination;
    for (const auto& digi : frame.digipeaters) {
        if (!digi.repeated) {
            next_hop = digi.call;
            break;
        }
    }

    std::vector<Endpoint> targets;
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
        if (send_datagram(raw, ep.addr)) {
            any_ok = true;
        }
    }
    return any_ok;
}

RadioInterfaceStats AxudpInterface::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

} // namespace oyk::radio
