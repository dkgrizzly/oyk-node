#include "oyk/radio/kiss_tcp_interface.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace oyk::radio {

namespace {

std::optional<uint8_t> parse_u8_param(const std::map<std::string, std::string>& params,
                                      const std::string& key) {
    auto it = params.find(key);
    if (it == params.end() || it->second.empty()) return std::nullopt;
    try {
        int v = std::stoi(it->second);
        if (v >= 0 && v <= 255) return static_cast<uint8_t>(v);
    } catch (...) {
    }
    return std::nullopt;
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string format_peer(const sockaddr_in& addr) {
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
}

} // namespace

KissTcpInterface::KissTcpInterface(const RadioInterfaceConfig& config)
    : id_(config.id.empty() ? "kisstcp0" : config.id),
      description_(config.description.empty() ? "KISS TCP/IP" : config.description) {
    if (auto it = config.params.find("mode"); it != config.params.end()) {
        server_mode_ = (to_lower(it->second) == "server");
    }
    host_ = server_mode_ ? "0.0.0.0" : "127.0.0.1";

    if (auto it = config.params.find("host"); it != config.params.end() && !it->second.empty()) {
        host_ = it->second;
    } else if (auto it2 = config.params.find("bind_host"); it2 != config.params.end() && !it2->second.empty()) {
        host_ = it2->second;
    }

    auto parse_port = [&](const std::string& key) {
        if (auto it = config.params.find(key); it != config.params.end() && !it->second.empty()) {
            try {
                int p = std::stoi(it->second);
                if (p >= 0 && p <= 65535) configured_port_ = static_cast<uint16_t>(p);
            } catch (...) {
            }
        }
    };
    parse_port("port");
    parse_port("bind_port");

    if (auto p = parse_u8_param(config.params, "kiss_port"); p && *p <= 15) {
        kiss_port_ = *p;
    }
    if (auto it = config.params.find("reconnect_ms"); it != config.params.end() && !it->second.empty()) {
        try {
            reconnect_ms_ = std::max(100, std::stoi(it->second));
        } catch (...) {
        }
    }

    txdelay_ = parse_u8_param(config.params, "txdelay");
    persistence_ = parse_u8_param(config.params, "persistence");
    slottime_ = parse_u8_param(config.params, "slottime");
    txtail_ = parse_u8_param(config.params, "txtail");
    fullduplex_ = parse_u8_param(config.params, "fullduplex");
}

KissTcpInterface::~KissTcpInterface() {
    stop();
}

std::string KissTcpInterface::status_summary() const {
    std::ostringstream oss;
    if (!running_.load()) {
        oss << "DOWN " << host_ << ":" << configured_port_;
        return oss.str();
    }
    if (server_mode_) {
        oss << "SRV :" << bound_port_.load() << " (" << connected_count_.load() << " cli)";
    } else {
        oss << (connected_count_.load() > 0 ? "UP " : "WAIT ")
            << host_ << ":" << configured_port_;
    }
    return oss.str();
}

bool KissTcpInterface::start(FrameCallback on_frame, std::string& error_out) {
    if (running_.load()) return true;

    if (pipe(wake_pipe_) != 0) {
        error_out = "Failed to create wake pipe: " + std::string(std::strerror(errno));
        return false;
    }
    fcntl(wake_pipe_[0], F_SETFL, O_NONBLOCK);
    fcntl(wake_pipe_[1], F_SETFL, O_NONBLOCK);

    if (server_mode_) {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            error_out = "Failed to create KISS TCP server socket: " + std::string(std::strerror(errno));
            close(wake_pipe_[0]);
            close(wake_pipe_[1]);
            wake_pipe_[0] = wake_pipe_[1] = -1;
            return false;
        }
        int reuse = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(configured_port_);
        if (host_.empty() || host_ == "0.0.0.0" || host_ == "*") {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
            error_out = "Invalid KISS TCP bind host: " + host_;
            close(listen_fd_);
            listen_fd_ = -1;
            close(wake_pipe_[0]);
            close(wake_pipe_[1]);
            wake_pipe_[0] = wake_pipe_[1] = -1;
            return false;
        }

        if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
            listen(listen_fd_, 16) < 0) {
            error_out = "Failed to bind/listen KISS TCP on " + host_ + ":" +
                        std::to_string(configured_port_) + ": " + std::strerror(errno);
            close(listen_fd_);
            listen_fd_ = -1;
            close(wake_pipe_[0]);
            close(wake_pipe_[1]);
            wake_pipe_[0] = wake_pipe_[1] = -1;
            return false;
        }

        sockaddr_in actual{};
        socklen_t len = sizeof(actual);
        if (getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&actual), &len) == 0) {
            bound_port_.store(ntohs(actual.sin_port));
        } else {
            bound_port_.store(configured_port_);
        }
    }

    on_frame_ = std::move(on_frame);
    running_.store(true);
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.running = true;
    }

    io_thread_ = server_mode_ ? std::thread(&KissTcpInterface::server_loop, this)
                              : std::thread(&KissTcpInterface::client_loop, this);
    return true;
}

void KissTcpInterface::stop() {
    if (!running_.exchange(false)) return;

    if (wake_pipe_[1] >= 0) {
        uint8_t b = 1;
        [[maybe_unused]] ssize_t ignored = write(wake_pipe_[1], &b, 1);
    }
    if (io_thread_.joinable()) {
        io_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        if (client_fd_ >= 0) {
            close(client_fd_);
            client_fd_ = -1;
        }
        for (auto& [fd, _] : server_clients_) {
            close(fd);
        }
        server_clients_.clear();
        connected_count_.store(0);
    }

    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
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

bool KissTcpInterface::wait_interruptible_ms(int ms) {
    pollfd pfd{};
    pfd.fd = wake_pipe_[0];
    pfd.events = POLLIN;
    int rc = poll(&pfd, 1, ms);
    if (rc > 0 && (pfd.revents & POLLIN)) {
        uint8_t drain[16];
        while (read(wake_pipe_[0], drain, sizeof(drain)) > 0) {}
    }
    return running_.load();
}

bool KissTcpInterface::connect_nonblocking(int& out_fd) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    std::string port_str = std::to_string(configured_port_);
    if (getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        return false;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        freeaddrinfo(res);
        return false;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    fcntl(fd, F_SETFL, O_NONBLOCK);

    int rc = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (rc < 0 && errno != EINPROGRESS) {
        close(fd);
        return false;
    }

    if (rc < 0) {
        pollfd fds[2];
        fds[0].fd = fd;
        fds[0].events = POLLOUT;
        fds[0].revents = 0;
        fds[1].fd = wake_pipe_[0];
        fds[1].events = POLLIN;
        fds[1].revents = 0;

        int prc = poll(fds, 2, 2000);
        if (prc <= 0 || !running_.load() || (fds[1].revents & POLLIN)) {
            close(fd);
            return false;
        }
        int so_err = 0;
        socklen_t slen = sizeof(so_err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &slen) != 0 || so_err != 0) {
            close(fd);
            return false;
        }
    }

    out_fd = fd;
    return true;
}

bool KissTcpInterface::send_bytes_fd(int fd, const std::vector<uint8_t>& data) {
    if (fd < 0) return false;
    std::size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLOUT;
            if (poll(&pfd, 1, 200) <= 0) return false;
        } else {
            return false;
        }
    }
    return true;
}

void KissTcpInterface::send_init_params_fd(int fd) {
    if (txdelay_)     send_bytes_fd(fd, kiss_encode_param(kiss_port_, KISS_CMD_TXDELAY, *txdelay_));
    if (persistence_) send_bytes_fd(fd, kiss_encode_param(kiss_port_, KISS_CMD_PERSISTENCE, *persistence_));
    if (slottime_)    send_bytes_fd(fd, kiss_encode_param(kiss_port_, KISS_CMD_SLOTTIME, *slottime_));
    if (txtail_)      send_bytes_fd(fd, kiss_encode_param(kiss_port_, KISS_CMD_TXTAIL, *txtail_));
    if (fullduplex_)  send_bytes_fd(fd, kiss_encode_param(kiss_port_, KISS_CMD_FULLDUPLEX, *fullduplex_));
}

void KissTcpInterface::handle_kiss_frames(const std::vector<KissFrame>& frames,
                                          const std::string& endpoint) {
    for (const auto& kf : frames) {
        if (kf.command != KISS_CMD_DATA || kf.port != kiss_port_) {
            continue;
        }
        auto decoded = ax25::Frame::decode(kf.payload.data(), kf.payload.size());
        if (!decoded) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.rx_errors++;
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.rx_frames++;
        }
        if (on_frame_) {
            RadioFrameMeta meta;
            meta.interface_id = id_;
            meta.peer_endpoint = endpoint;
            meta.received_at = std::chrono::system_clock::now();
            on_frame_(*decoded, meta);
        }
    }
}

void KissTcpInterface::client_loop() {
    uint8_t buf[2048];
    const std::string ep = host_ + ":" + std::to_string(configured_port_);

    while (running_.load()) {
        int fd = -1;
        if (!connect_nonblocking(fd)) {
            if (!wait_interruptible_ms(reconnect_ms_)) break;
            continue;
        }

        KissDecoder decoder;
        {
            std::lock_guard<std::mutex> lock(conn_mutex_);
            client_fd_ = fd;
            connected_count_.store(1);
            send_init_params_fd(fd);
        }

        while (running_.load()) {
            pollfd fds[2];
            fds[0].fd = fd;
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

            if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
                ssize_t n = recv(fd, buf, sizeof(buf), 0);
                if (n <= 0) {
                    if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
                    break; // Disconnected; reconnect
                }
                auto frames = decoder.feed(buf, static_cast<std::size_t>(n));
                handle_kiss_frames(frames, ep);
            }
        }

        {
            std::lock_guard<std::mutex> lock(conn_mutex_);
            if (client_fd_ == fd) {
                close(client_fd_);
                client_fd_ = -1;
                connected_count_.store(0);
            }
        }

        if (running_.load()) {
            wait_interruptible_ms(reconnect_ms_);
        }
    }
}

void KissTcpInterface::server_loop() {
    uint8_t buf[2048];

    while (running_.load()) {
        std::vector<pollfd> fds;
        fds.push_back(pollfd{wake_pipe_[0], POLLIN, 0});
        fds.push_back(pollfd{listen_fd_, POLLIN, 0});

        std::vector<int> client_fds;
        {
            std::lock_guard<std::mutex> lock(conn_mutex_);
            for (const auto& [fd, _] : server_clients_) {
                client_fds.push_back(fd);
                fds.push_back(pollfd{fd, POLLIN, 0});
            }
        }

        int ret = poll(fds.data(), fds.size(), 200);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;

        if (fds[0].revents & POLLIN) {
            uint8_t drain[16];
            while (read(wake_pipe_[0], drain, sizeof(drain)) > 0) {}
            if (!running_.load()) break;
        }

        if (fds[1].revents & POLLIN) {
            sockaddr_in peer{};
            socklen_t peer_len = sizeof(peer);
            int cfd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len);
            if (cfd >= 0) {
                int one = 1;
                setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                fcntl(cfd, F_SETFL, O_NONBLOCK);
                std::lock_guard<std::mutex> lock(conn_mutex_);
                ClientConn cc;
                cc.fd = cfd;
                cc.endpoint = format_peer(peer);
                server_clients_[cfd] = std::move(cc);
                connected_count_.store(static_cast<int>(server_clients_.size()));
                send_init_params_fd(cfd);
            }
        }

        for (std::size_t i = 0; i < client_fds.size(); ++i) {
            if (!(fds[i + 2].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            int cfd = client_fds[i];
            ssize_t n = recv(cfd, buf, sizeof(buf), 0);
            if (n <= 0) {
                if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
                std::lock_guard<std::mutex> lock(conn_mutex_);
                auto it = server_clients_.find(cfd);
                if (it != server_clients_.end()) {
                    close(it->first);
                    server_clients_.erase(it);
                    connected_count_.store(static_cast<int>(server_clients_.size()));
                }
                continue;
            }

            std::vector<KissFrame> frames;
            std::string ep;
            {
                std::lock_guard<std::mutex> lock(conn_mutex_);
                auto it = server_clients_.find(cfd);
                if (it != server_clients_.end()) {
                    frames = it->second.decoder.feed(buf, static_cast<std::size_t>(n));
                    ep = it->second.endpoint;
                }
            }
            if (!frames.empty()) {
                handle_kiss_frames(frames, ep);
            }
        }
    }
}

bool KissTcpInterface::send_frame(const ax25::Frame& frame,
                                  const std::string& target_endpoint) {
    if (!running_.load()) return false;

    std::vector<uint8_t> raw = frame.encode();
    if (raw.empty()) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.tx_errors++;
        return false;
    }
    std::vector<uint8_t> encoded = kiss_encode(kiss_port_, raw, KISS_CMD_DATA);

    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        if (!server_mode_) {
            ok = (client_fd_ >= 0) && send_bytes_fd(client_fd_, encoded);
        } else {
            bool matched_target = false;
            if (!target_endpoint.empty()) {
                for (const auto& [fd, cc] : server_clients_) {
                    if (cc.endpoint == target_endpoint) {
                        matched_target = true;
                        ok = send_bytes_fd(fd, encoded);
                        break;
                    }
                }
            }
            if (!matched_target) {
                for (const auto& [fd, _] : server_clients_) {
                    if (send_bytes_fd(fd, encoded)) {
                        ok = true;
                    }
                }
            }
        }
    }

    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (ok) {
        stats_.tx_frames++;
    } else {
        stats_.tx_errors++;
    }
    return ok;
}

RadioInterfaceStats KissTcpInterface::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

} // namespace oyk::radio
