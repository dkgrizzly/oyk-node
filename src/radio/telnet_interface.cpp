#include "oyk/radio/telnet_interface.hpp"

#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace oyk::radio {

namespace {

constexpr uint8_t TELNET_IAC  = 255;
constexpr uint8_t TELNET_DONT = 254;
constexpr uint8_t TELNET_DO   = 253;
constexpr uint8_t TELNET_WONT = 252;
constexpr uint8_t TELNET_WILL = 251;
constexpr uint8_t TELNET_SB   = 250;
constexpr uint8_t TELNET_SE   = 240;
constexpr uint8_t TELNET_OPT_SGA = 3;

std::string format_peer(const sockaddr_in& addr) {
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
}

} // namespace

TelnetInterface::TelnetInterface(const RadioInterfaceConfig& config)
    : id_(config.id.empty() ? "telnet0" : config.id),
      description_(config.description.empty() ? "Incoming Telnet Server" : config.description) {
    if (auto it = config.params.find("bind_host"); it != config.params.end() && !it->second.empty()) {
        bind_host_ = it->second;
    } else if (auto it2 = config.params.find("host"); it2 != config.params.end() && !it2->second.empty()) {
        bind_host_ = it2->second;
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

    if (auto it = config.params.find("max_clients"); it != config.params.end() && !it->second.empty()) {
        try {
            int m = std::stoi(it->second);
            if (m >= 1 && m <= 256) max_clients_ = static_cast<std::size_t>(m);
        } catch (...) {
        }
    }

    if (auto it = config.params.find("node_callsign"); it != config.params.end() && !it->second.empty()) {
        if (auto parsed = ax25::Callsign::parse(it->second)) {
            node_callsign_ = *parsed;
        }
    }

    if (auto it = config.params.find("banner"); it != config.params.end() && !it->second.empty()) {
        banner_ = it->second;
    } else {
        banner_ = "OYK Packet Node Telnet Access (" + node_callsign_.str() + ")";
    }
}

TelnetInterface::~TelnetInterface() {
    stop();
}

std::string TelnetInterface::status_summary() const {
    std::ostringstream oss;
    if (running_.load()) {
        oss << "UP " << bind_host_ << ":" << bound_port_.load() << " (in)";
    } else {
        oss << "DOWN " << bind_host_ << ":" << configured_port_;
    }
    return oss.str();
}

void TelnetInterface::wake_io() {
    if (wake_pipe_[1] >= 0) {
        uint8_t b = 1;
        [[maybe_unused]] ssize_t ignored = write(wake_pipe_[1], &b, 1);
    }
}

void TelnetInterface::send_raw_fd(int fd, const void* data, std::size_t len) {
    if (fd < 0 || !data || len == 0) return;
    const auto* ptr = static_cast<const uint8_t*>(data);
    std::size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, ptr + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLOUT;
            if (poll(&pfd, 1, 200) <= 0) break;
        } else {
            break;
        }
    }
}

void TelnetInterface::send_text_fd(int fd, const std::string& text) {
    send_raw_fd(fd, text.data(), text.size());
}

std::string TelnetInterface::convert_cr_to_crlf(const std::vector<uint8_t>& payload) {
    std::string out;
    out.reserve(payload.size() + 16);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        char c = static_cast<char>(payload[i]);
        if (c == '\r') {
            out.push_back('\r');
            if (i + 1 >= payload.size() || payload[i + 1] != '\n') {
                out.push_back('\n');
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

bool TelnetInterface::start(FrameCallback on_frame, std::string& error_out) {
    if (running_.load()) return true;

    if (pipe(wake_pipe_) != 0) {
        error_out = "Failed to create wake pipe: " + std::string(std::strerror(errno));
        return false;
    }
    fcntl(wake_pipe_[0], F_SETFL, O_NONBLOCK);
    fcntl(wake_pipe_[1], F_SETFL, O_NONBLOCK);

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        error_out = "Failed to create Telnet TCP socket: " + std::string(std::strerror(errno));
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
    if (bind_host_.empty() || bind_host_ == "0.0.0.0" || bind_host_ == "*") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, bind_host_.c_str(), &addr.sin_addr) != 1) {
        error_out = "Invalid Telnet bind_host: " + bind_host_;
        close(listen_fd_);
        listen_fd_ = -1;
        close(wake_pipe_[0]);
        close(wake_pipe_[1]);
        wake_pipe_[0] = wake_pipe_[1] = -1;
        return false;
    }

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        listen(listen_fd_, 16) < 0) {
        error_out = "Failed to bind/listen Telnet on " + bind_host_ + ":" +
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

    on_frame_ = std::move(on_frame);
    running_.store(true);
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.running = true;
    }

    io_thread_ = std::thread(&TelnetInterface::io_loop, this);
    return true;
}

void TelnetInterface::stop() {
    if (!running_.exchange(false)) return;

    wake_io();
    if (io_thread_.joinable()) {
        io_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [fd, _] : clients_by_fd_) {
            close(fd);
        }
        clients_by_fd_.clear();
        fd_by_endpoint_.clear();
        deferred_rx_.clear();
        client_count_.store(0);
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

void TelnetInterface::handle_accept() {
    sockaddr_in peer{};
    socklen_t peer_len = sizeof(peer);
    int cfd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len);
    if (cfd < 0) return;

    int one = 1;
    setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    fcntl(cfd, F_SETFL, O_NONBLOCK);

    std::lock_guard<std::mutex> lock(mutex_);
    if (clients_by_fd_.size() >= max_clients_) {
        send_text_fd(cfd, "*** Server busy (too many telnet connections).\r\n");
        close(cfd);
        return;
    }

    uint64_t cid = next_conn_id_++;
    TelnetClient client;
    client.conn_id = cid;
    client.fd = cfd;
    client.endpoint = "telnet:" + std::to_string(cid);
    client.remote_addr = format_peer(peer);
    client.stage = ClientStage::LoginPrompt;

    fd_by_endpoint_[client.endpoint] = cfd;
    clients_by_fd_[cfd] = std::move(client);
    client_count_.store(clients_by_fd_.size());

    send_text_fd(cfd, banner_ + "\r\nCallsign: ");
}

void TelnetInterface::process_clean_byte(TelnetClient& client, uint8_t b,
                                         std::vector<DeferredRx>& emit_out,
                                         bool& close_client) {
    // Normalize Telnet NVT CR-LF and CR-NUL to a single '\r'
    if (client.last_was_cr) {
        client.last_was_cr = false;
        if (b == '\n' || b == '\0') {
            return;
        }
    }

    if (b == '\r' || b == '\n') {
        if (b == '\r') client.last_was_cr = true;

        if (client.stage == ClientStage::LoginPrompt) {
            std::string entered = client.line_buf;
            client.line_buf.clear();

            auto parsed = ax25::Callsign::parse(entered);
            if (!parsed) {
                client.login_attempts++;
                if (client.login_attempts >= 3) {
                    send_text_fd(client.fd, "Too many invalid attempts. Goodbye.\r\n");
                    close_client = true;
                } else {
                    send_text_fd(client.fd, "Invalid callsign. Callsign: ");
                }
                return;
            }

            // Prevent duplicate simultaneous callsign on the same telnet interface
            for (const auto& [other_fd, other] : clients_by_fd_) {
                if (other_fd != client.fd &&
                    other.stage != ClientStage::LoginPrompt &&
                    other.callsign == *parsed) {
                    send_text_fd(client.fd, "*** Callsign already connected on this telnet port.\r\n");
                    close_client = true;
                    return;
                }
            }

            client.callsign = *parsed;
            client.stage = ClientStage::Connecting;
            client.vs = 0;
            client.vr = 0;

            ax25::Frame sabm = ax25::Frame::make_sabm(node_callsign_, client.callsign, true);
            emit_out.push_back(DeferredRx{std::move(sabm), client.endpoint});
            return;
        }

        if (client.stage == ClientStage::Connected) {
            std::string line = client.line_buf + "\r";
            client.line_buf.clear();

            constexpr std::size_t CHUNK_MAX = 200;
            std::size_t pos = 0;
            while (pos < line.size()) {
                std::size_t n = std::min(CHUNK_MAX, line.size() - pos);
                std::vector<uint8_t> payload(line.begin() + static_cast<std::ptrdiff_t>(pos),
                                             line.begin() + static_cast<std::ptrdiff_t>(pos + n));
                ax25::Frame iframe = ax25::Frame::make_i(
                    node_callsign_, client.callsign, client.vs, client.vr, payload, false);
                client.vs = static_cast<uint8_t>((client.vs + 1) & 0x07);
                emit_out.push_back(DeferredRx{std::move(iframe), client.endpoint});
                pos += n;
            }
        }
        return;
    }

    if (b == '\b' || b == 0x7F) {
        if (!client.line_buf.empty()) {
            client.line_buf.pop_back();
        }
        return;
    }

    if (b >= 0x20 && b < 0x7F) {
        if (client.line_buf.size() < 512) {
            client.line_buf.push_back(static_cast<char>(b));
        }
    }
}

void TelnetInterface::handle_client_bytes(int fd, const uint8_t* data, std::size_t len,
                                          std::vector<DeferredRx>& emit_out) {
    bool close_client = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = clients_by_fd_.find(fd);
        if (it == clients_by_fd_.end()) return;
        TelnetClient& client = it->second;

        for (std::size_t i = 0; i < len && !close_client; ++i) {
            uint8_t b = data[i];
            switch (client.iac_state) {
                case IacState::Normal:
                    if (b == TELNET_IAC) {
                        client.iac_state = IacState::Iac;
                    } else {
                        process_clean_byte(client, b, emit_out, close_client);
                    }
                    break;
                case IacState::Iac:
                    if (b == TELNET_IAC) {
                        client.iac_state = IacState::Normal;
                    } else if (b == TELNET_WILL || b == TELNET_WONT ||
                               b == TELNET_DO || b == TELNET_DONT) {
                        client.iac_cmd = b;
                        client.iac_state = IacState::IacOpt;
                    } else if (b == TELNET_SB) {
                        client.iac_state = IacState::Subneg;
                    } else {
                        client.iac_state = IacState::Normal;
                    }
                    break;
                case IacState::IacOpt: {
                    // Politely refuse options except Suppress Go Ahead
                    if (client.iac_cmd == TELNET_WILL) {
                        uint8_t resp[3] = {
                            TELNET_IAC,
                            (b == TELNET_OPT_SGA) ? TELNET_DO : TELNET_DONT,
                            b
                        };
                        send_raw_fd(client.fd, resp, 3);
                    } else if (client.iac_cmd == TELNET_DO) {
                        uint8_t resp[3] = {
                            TELNET_IAC,
                            (b == TELNET_OPT_SGA) ? TELNET_WILL : TELNET_WONT,
                            b
                        };
                        send_raw_fd(client.fd, resp, 3);
                    }
                    client.iac_state = IacState::Normal;
                    break;
                }
                case IacState::Subneg:
                    if (b == TELNET_IAC) {
                        client.iac_state = IacState::SubnegIac;
                    }
                    break;
                case IacState::SubnegIac:
                    if (b == TELNET_SE) {
                        client.iac_state = IacState::Normal;
                    } else if (b != TELNET_IAC) {
                        client.iac_state = IacState::Subneg;
                    }
                    break;
            }
        }

        if (close_client) {
            fd_by_endpoint_.erase(client.endpoint);
            close(fd);
            clients_by_fd_.erase(it);
            client_count_.store(clients_by_fd_.size());
        }
    }
}

void TelnetInterface::io_loop() {
    uint8_t buf[1024];

    while (running_.load()) {
        // First deliver any deferred synthetic RX frames (RR, UA, DM) outside mutex_
        std::vector<DeferredRx> to_deliver;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            while (!deferred_rx_.empty()) {
                to_deliver.push_back(std::move(deferred_rx_.front()));
                deferred_rx_.pop_front();
            }
        }
        for (const auto& item : to_deliver) {
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.rx_frames++;
            }
            if (on_frame_) {
                RadioFrameMeta meta;
                meta.interface_id = id_;
                meta.peer_endpoint = item.endpoint;
                meta.received_at = std::chrono::system_clock::now();
                on_frame_(item.frame, meta);
            }
        }

        std::vector<pollfd> fds;
        fds.push_back(pollfd{wake_pipe_[0], POLLIN, 0});
        fds.push_back(pollfd{listen_fd_, POLLIN, 0});

        std::vector<int> client_fds;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [fd, _] : clients_by_fd_) {
                client_fds.push_back(fd);
                fds.push_back(pollfd{fd, POLLIN, 0});
            }
        }

        int ret = poll(fds.data(), fds.size(), 100);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;

        if (fds[0].revents & POLLIN) {
            uint8_t drain[32];
            while (read(wake_pipe_[0], drain, sizeof(drain)) > 0) {}
            if (!running_.load()) break;
        }

        if (fds[1].revents & POLLIN) {
            handle_accept();
        }

        std::vector<DeferredRx> emit_frames;
        for (std::size_t i = 0; i < client_fds.size(); ++i) {
            if (!(fds[i + 2].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            int cfd = client_fds[i];
            ssize_t n = recv(cfd, buf, sizeof(buf), 0);
            if (n <= 0) {
                if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;

                // Client disconnected: emit DISC if session was established/connecting
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = clients_by_fd_.find(cfd);
                if (it != clients_by_fd_.end()) {
                    if (it->second.stage != ClientStage::LoginPrompt &&
                        it->second.callsign.valid()) {
                        emit_frames.push_back(DeferredRx{
                            ax25::Frame::make_disc(node_callsign_, it->second.callsign, true),
                            it->second.endpoint
                        });
                    }
                    fd_by_endpoint_.erase(it->second.endpoint);
                    close(cfd);
                    clients_by_fd_.erase(it);
                    client_count_.store(clients_by_fd_.size());
                }
                continue;
            }

            handle_client_bytes(cfd, buf, static_cast<std::size_t>(n), emit_frames);
        }

        for (const auto& item : emit_frames) {
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.rx_frames++;
            }
            if (on_frame_) {
                RadioFrameMeta meta;
                meta.interface_id = id_;
                meta.peer_endpoint = item.endpoint;
                meta.received_at = std::chrono::system_clock::now();
                on_frame_(item.frame, meta);
            }
        }
    }
}

bool TelnetInterface::send_frame(const ax25::Frame& frame,
                                 const std::string& target_endpoint) {
    if (!running_.load()) return false;

    const ax25::FrameType ft = frame.type();
    if (ft == ax25::FrameType::UI) {
        // Ignore periodic RF UI ID beacons on Telnet
        return true;
    }

    bool wake_needed = false;
    bool handled = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        int target_fd = -1;
        if (!target_endpoint.empty()) {
            if (auto it = fd_by_endpoint_.find(target_endpoint); it != fd_by_endpoint_.end()) {
                target_fd = it->second;
            }
        }
        if (target_fd < 0) {
            for (const auto& [fd, c] : clients_by_fd_) {
                if (c.stage != ClientStage::LoginPrompt && c.callsign == frame.destination) {
                    target_fd = fd;
                    break;
                }
            }
        }

        if (target_fd < 0) {
            // Outbound telnet connect is not supported (incoming-only for now):
            // immediately answer SABM with DM so caller does not stall waiting on T1.
            if (ft == ax25::FrameType::SABM) {
                deferred_rx_.push_back(DeferredRx{
                    ax25::Frame::make_dm(frame.source, frame.destination, frame.pf()),
                    ""
                });
                wake_needed = true;
                handled = true;
            }
        } else {
            auto it = clients_by_fd_.find(target_fd);
            if (it != clients_by_fd_.end()) {
                TelnetClient& client = it->second;
                handled = true;

                switch (ft) {
                    case ax25::FrameType::UA:
                        if (client.stage == ClientStage::Connecting) {
                            client.stage = ClientStage::Connected;
                        }
                        break;
                    case ax25::FrameType::DM:
                        send_text_fd(client.fd,
                                     "\r\n*** Connection rejected (banned or incoming disabled).\r\n");
                        fd_by_endpoint_.erase(client.endpoint);
                        close(client.fd);
                        clients_by_fd_.erase(it);
                        client_count_.store(clients_by_fd_.size());
                        break;
                    case ax25::FrameType::DISC:
                        send_text_fd(client.fd, "\r\n*** Disconnected.\r\n");
                        deferred_rx_.push_back(DeferredRx{
                            ax25::Frame::make_ua(frame.source, client.callsign, frame.pf()),
                            client.endpoint
                        });
                        wake_needed = true;
                        fd_by_endpoint_.erase(client.endpoint);
                        close(client.fd);
                        clients_by_fd_.erase(it);
                        client_count_.store(clients_by_fd_.size());
                        break;
                    case ax25::FrameType::I: {
                        std::string out = convert_cr_to_crlf(frame.info);
                        send_text_fd(client.fd, out);
                        client.vr = static_cast<uint8_t>((frame.ns() + 1) & 0x07);
                        deferred_rx_.push_back(DeferredRx{
                            ax25::Frame::make_rr(frame.source, client.callsign, client.vr, false, false),
                            client.endpoint
                        });
                        wake_needed = true;
                        break;
                    }
                    case ax25::FrameType::RR:
                    case ax25::FrameType::RNR:
                    case ax25::FrameType::REJ:
                        if (frame.is_command && frame.pf()) {
                            deferred_rx_.push_back(DeferredRx{
                                ax25::Frame::make_rr(frame.source, client.callsign, client.vr, false, true),
                                client.endpoint
                            });
                            wake_needed = true;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    if (wake_needed) {
        wake_io();
    }

    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (handled) {
        stats_.tx_frames++;
    } else {
        stats_.tx_errors++;
    }
    return handled;
}

RadioInterfaceStats TelnetInterface::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

} // namespace oyk::radio
