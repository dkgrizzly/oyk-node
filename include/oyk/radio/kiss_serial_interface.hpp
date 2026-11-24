#pragma once

#include "oyk/radio/kiss.hpp"
#include "oyk/radio/radio_interface.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace oyk::radio {

// KISS over Serial TTY/PTY radio interface (hardware TNCs, Mobilinkd, Direwolf PTYs).
class KissSerialInterface : public RadioInterface {
public:
    explicit KissSerialInterface(const RadioInterfaceConfig& config);
    ~KissSerialInterface() override;

    KissSerialInterface(const KissSerialInterface&) = delete;
    KissSerialInterface& operator=(const KissSerialInterface&) = delete;

    std::string id() const override { return id_; }
    std::string type() const override { return "kiss-serial"; }
    std::string description() const override { return description_; }
    std::string status_summary() const override;

    bool start(FrameCallback on_frame, std::string& error_out) override;
    void stop() override;
    bool is_running() const override { return running_.load(); }

    bool send_frame(const ax25::Frame& frame,
                    const std::string& target_endpoint = "") override;

    RadioInterfaceStats stats() const override;

private:
    bool configure_serial(int fd, std::string& error_out) const;
    bool write_all(const std::vector<uint8_t>& bytes);
    void send_init_params();
    void rx_loop();

    std::string id_;
    std::string description_;
    std::string device_;
    int baud_ = 9600;
    uint8_t kiss_port_ = 0;

    std::optional<uint8_t> txdelay_;
    std::optional<uint8_t> persistence_;
    std::optional<uint8_t> slottime_;
    std::optional<uint8_t> txtail_;
    std::optional<uint8_t> fullduplex_;

    std::atomic<bool> running_{false};
    int serial_fd_ = -1;
    int wake_pipe_[2] = {-1, -1};
    std::thread rx_thread_;

    FrameCallback on_frame_;
    KissDecoder decoder_;

    mutable std::mutex write_mutex_;
    mutable std::mutex stats_mutex_;
    RadioInterfaceStats stats_{};
};

} // namespace oyk::radio
