// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/radio/kiss_serial_interface.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sstream>
#include <termios.h>
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

speed_t baud_to_speed(int baud) {
    switch (baud) {
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default:     return B0;
    }
}

} // namespace

KissSerialInterface::KissSerialInterface(const RadioInterfaceConfig& config)
    : id_(config.id.empty() ? "kiss0" : config.id),
      description_(config.description.empty() ? "KISS Serial TNC" : config.description) {
    if (auto it = config.params.find("device"); it != config.params.end()) {
        device_ = it->second;
    }
    if (auto it = config.params.find("baud"); it != config.params.end() && !it->second.empty()) {
        try {
            baud_ = std::stoi(it->second);
        } catch (...) {
        }
    }
    if (auto p = parse_u8_param(config.params, "kiss_port"); p && *p <= 15) {
        kiss_port_ = *p;
    }
    txdelay_ = parse_u8_param(config.params, "txdelay");
    persistence_ = parse_u8_param(config.params, "persistence");
    slottime_ = parse_u8_param(config.params, "slottime");
    txtail_ = parse_u8_param(config.params, "txtail");
    fullduplex_ = parse_u8_param(config.params, "fullduplex");
}

KissSerialInterface::~KissSerialInterface() {
    stop();
}

std::string KissSerialInterface::status_summary() const {
    std::ostringstream oss;
    oss << (running_.load() ? "UP " : "DOWN ")
        << (device_.empty() ? "(unset)" : device_)
        << "@" << baud_;
    return oss.str();
}

bool KissSerialInterface::configure_serial(int fd, std::string& error_out) const {
    if (!isatty(fd)) {
        return true;
    }
    termios tio{};
    if (tcgetattr(fd, &tio) != 0) {
        error_out = "tcgetattr failed on " + device_ + ": " + std::strerror(errno);
        return false;
    }

    cfmakeraw(&tio);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~static_cast<tcflag_t>(PARENB | CSTOPB | CSIZE | CRTSCTS);
    tio.c_cflag |= CS8;
    tio.c_iflag &= ~static_cast<tcflag_t>(IXON | IXOFF | IXANY);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    speed_t spd = baud_to_speed(baud_);
    if (spd == B0) {
        error_out = "Unsupported baud rate: " + std::to_string(baud_);
        return false;
    }
    cfsetispeed(&tio, spd);
    cfsetospeed(&tio, spd);

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        error_out = "tcsetattr failed on " + device_ + ": " + std::strerror(errno);
        return false;
    }
    return true;
}

bool KissSerialInterface::start(FrameCallback on_frame, std::string& error_out) {
    if (running_.load()) return true;

    if (device_.empty()) {
        error_out = "KISS serial interface requires 'device' parameter (e.g. /dev/ttyUSB0)";
        return false;
    }

    if (pipe(wake_pipe_) != 0) {
        error_out = "Failed to create wake pipe: " + std::string(std::strerror(errno));
        return false;
    }
    fcntl(wake_pipe_[0], F_SETFL, O_NONBLOCK);
    fcntl(wake_pipe_[1], F_SETFL, O_NONBLOCK);

    serial_fd_ = open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serial_fd_ < 0) {
        error_out = "Failed to open serial device '" + device_ + "': " + std::strerror(errno);
        close(wake_pipe_[0]);
        close(wake_pipe_[1]);
        wake_pipe_[0] = wake_pipe_[1] = -1;
        return false;
    }

    if (!configure_serial(serial_fd_, error_out)) {
        close(serial_fd_);
        serial_fd_ = -1;
        close(wake_pipe_[0]);
        close(wake_pipe_[1]);
        wake_pipe_[0] = wake_pipe_[1] = -1;
        return false;
    }

    decoder_.reset();
    on_frame_ = std::move(on_frame);
    running_.store(true);
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.running = true;
    }

    send_init_params();
    rx_thread_ = std::thread(&KissSerialInterface::rx_loop, this);
    return true;
}

void KissSerialInterface::stop() {
    if (!running_.exchange(false)) return;

    if (wake_pipe_[1] >= 0) {
        uint8_t b = 1;
        [[maybe_unused]] ssize_t ignored = write(wake_pipe_[1], &b, 1);
    }
    if (rx_thread_.joinable()) {
        rx_thread_.join();
    }
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (serial_fd_ >= 0) {
            close(serial_fd_);
            serial_fd_ = -1;
        }
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

bool KissSerialInterface::write_all(const std::vector<uint8_t>& bytes) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (serial_fd_ < 0 || !running_.load()) return false;

    std::size_t written = 0;
    while (written < bytes.size()) {
        ssize_t n = write(serial_fd_, bytes.data() + written, bytes.size() - written);
        if (n > 0) {
            written += static_cast<std::size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pfd{};
            pfd.fd = serial_fd_;
            pfd.events = POLLOUT;
            int prc = poll(&pfd, 1, 200);
            if (prc <= 0) return false;
        } else {
            return false;
        }
    }
    return true;
}

void KissSerialInterface::send_init_params() {
    if (txdelay_)     write_all(kiss_encode_param(kiss_port_, KISS_CMD_TXDELAY, *txdelay_));
    if (persistence_) write_all(kiss_encode_param(kiss_port_, KISS_CMD_PERSISTENCE, *persistence_));
    if (slottime_)    write_all(kiss_encode_param(kiss_port_, KISS_CMD_SLOTTIME, *slottime_));
    if (txtail_)      write_all(kiss_encode_param(kiss_port_, KISS_CMD_TXTAIL, *txtail_));
    if (fullduplex_)  write_all(kiss_encode_param(kiss_port_, KISS_CMD_FULLDUPLEX, *fullduplex_));
}

void KissSerialInterface::rx_loop() {
    uint8_t buf[1024];

    while (running_.load()) {
        pollfd fds[2];
        fds[0].fd = serial_fd_;
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
            ssize_t n = read(serial_fd_, buf, sizeof(buf));
            if (n <= 0) {
                if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
                // Transient error or hangup on virtual PTY; avoid busy spin
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            auto frames = decoder_.feed(buf, static_cast<std::size_t>(n));
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
                    meta.peer_endpoint = device_;
                    meta.received_at = std::chrono::system_clock::now();
                    on_frame_(*decoded, meta);
                }
            }
        }
    }
}

bool KissSerialInterface::send_frame(const ax25::Frame& frame,
                                     const std::string& /*target_endpoint*/) {
    if (!running_.load()) return false;

    std::vector<uint8_t> raw = frame.encode();
    if (raw.empty()) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.tx_errors++;
        return false;
    }

    std::vector<uint8_t> encoded = kiss_encode(kiss_port_, raw, KISS_CMD_DATA);
    if (!write_all(encoded)) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.tx_errors++;
        return false;
    }

    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.tx_frames++;
    return true;
}

RadioInterfaceStats KissSerialInterface::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

} // namespace oyk::radio
