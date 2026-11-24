#pragma once

#include "oyk/radio/kiss.hpp"
#include "oyk/radio/radio_interface.hpp"

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace oyk::radio {

// KISS over TCP/IP radio interface.
// Supports:
//   - mode = "client" (default): connects to a KISS TCP server (e.g. Direwolf port 8001)
//     with automatic reconnection.
//   - mode = "server": listens for incoming KISS TCP client connections.
class KissTcpInterface : public RadioInterface {
public:
    explicit KissTcpInterface(const RadioInterfaceConfig& config);
    ~KissTcpInterface() override;

    KissTcpInterface(const KissTcpInterface&) = delete;
    KissTcpInterface& operator=(const KissTcpInterface&) = delete;

    std::string id() const override { return id_; }
    std::string type() const override { return "kiss-tcp"; }
    std::string description() const override { return description_; }
    std::string status_summary() const override;

    bool start(FrameCallback on_frame, std::string& error_out) override;
    void stop() override;
    bool is_running() const override { return running_.load(); }

    bool send_frame(const ax25::Frame& frame,
                    const std::string& target_endpoint = "") override;

    RadioInterfaceStats stats() const override;

    bool is_connected() const noexcept { return connected_count_.load() > 0; }
    uint16_t bound_port() const noexcept { return bound_port_.load(); }

private:
    struct ClientConn {
        int fd = -1;
        std::string endpoint;
        KissDecoder decoder;
    };

    void client_loop();
    void server_loop();
    bool connect_nonblocking(int& out_fd);
    bool send_bytes_fd(int fd, const std::vector<uint8_t>& data);
    void send_init_params_fd(int fd);
    void handle_kiss_frames(const std::vector<KissFrame>& frames, const std::string& endpoint);
    bool wait_interruptible_ms(int ms);

    std::string id_;
    std::string description_;
    bool server_mode_ = false;
    std::string host_ = "127.0.0.1";
    uint16_t configured_port_ = 8001;
    uint8_t kiss_port_ = 0;
    int reconnect_ms_ = 3000;

    std::optional<uint8_t> txdelay_;
    std::optional<uint8_t> persistence_;
    std::optional<uint8_t> slottime_;
    std::optional<uint8_t> txtail_;
    std::optional<uint8_t> fullduplex_;

    std::atomic<bool> running_{false};
    std::atomic<int> connected_count_{0};
    std::atomic<uint16_t> bound_port_{0};

    int listen_fd_ = -1;
    int wake_pipe_[2] = {-1, -1};
    std::thread io_thread_;

    FrameCallback on_frame_;

    mutable std::mutex conn_mutex_;
    int client_fd_ = -1; // Used in client mode
    std::map<int, ClientConn> server_clients_; // Used in server mode

    mutable std::mutex stats_mutex_;
    RadioInterfaceStats stats_{};
};

} // namespace oyk::radio
