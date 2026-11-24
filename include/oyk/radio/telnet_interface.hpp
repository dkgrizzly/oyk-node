#pragma once

#include "oyk/ax25/callsign.hpp"
#include "oyk/radio/radio_interface.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace oyk::radio {

// Incoming-only Telnet console interface.
// Accepts TCP telnet connections, negotiates minimal RFC 854 IAC options,
// prompts for an amateur radio callsign, and bridges the session into
// NodeEngine as a standard AX.25 connected-mode stream so all node features
// (MHEARD, last 5 new callsigns, allow/deny incoming, terminate, ban, and
// cross-port CONNECT) work uniformly.
class TelnetInterface : public RadioInterface {
public:
    explicit TelnetInterface(const RadioInterfaceConfig& config);
    ~TelnetInterface() override;

    TelnetInterface(const TelnetInterface&) = delete;
    TelnetInterface& operator=(const TelnetInterface&) = delete;

    std::string id() const override { return id_; }
    std::string type() const override { return "telnet"; }
    std::string description() const override { return description_; }
    std::string status_summary() const override;

    bool start(FrameCallback on_frame, std::string& error_out) override;
    void stop() override;
    bool is_running() const override { return running_.load(); }

    bool send_frame(const ax25::Frame& frame,
                    const std::string& target_endpoint = "") override;

    RadioInterfaceStats stats() const override;

    uint16_t bound_port() const noexcept { return bound_port_.load(); }

private:
    enum class ClientStage {
        LoginPrompt,
        Connecting,
        Connected
    };

    enum class IacState {
        Normal,
        Iac,
        IacOpt,
        Subneg,
        SubnegIac
    };

    struct TelnetClient {
        uint64_t conn_id = 0;
        int fd = -1;
        std::string endpoint; // "telnet:<conn_id>"
        std::string remote_addr;
        ClientStage stage = ClientStage::LoginPrompt;
        int login_attempts = 0;

        ax25::Callsign callsign;
        uint8_t vs = 0;
        uint8_t vr = 0;

        IacState iac_state = IacState::Normal;
        uint8_t iac_cmd = 0;
        bool last_was_cr = false;
        std::string line_buf;
    };

    struct DeferredRx {
        ax25::Frame frame;
        std::string endpoint;
    };

    void io_loop();
    void wake_io();
    void handle_accept();
    void handle_client_bytes(int fd, const uint8_t* data, std::size_t len,
                             std::vector<DeferredRx>& emit_out);
    void process_clean_byte(TelnetClient& client, uint8_t b,
                            std::vector<DeferredRx>& emit_out,
                            bool& close_client);
    void send_raw_fd(int fd, const void* data, std::size_t len);
    void send_text_fd(int fd, const std::string& text);
    static std::string convert_cr_to_crlf(const std::vector<uint8_t>& payload);

    std::string id_;
    std::string description_;
    std::string bind_host_ = "0.0.0.0";
    uint16_t configured_port_ = 8010;
    std::size_t max_clients_ = 32;
    ax25::Callsign node_callsign_{"N0CALL", 7};
    std::string banner_;

    std::atomic<bool> running_{false};
    std::atomic<uint16_t> bound_port_{0};
    std::atomic<std::size_t> client_count_{0};

    int listen_fd_ = -1;
    int wake_pipe_[2] = {-1, -1};
    std::thread io_thread_;

    FrameCallback on_frame_;

    mutable std::mutex mutex_;
    uint64_t next_conn_id_ = 1;
    std::map<int, TelnetClient> clients_by_fd_;
    std::map<std::string, int> fd_by_endpoint_;
    std::deque<DeferredRx> deferred_rx_;

    mutable std::mutex stats_mutex_;
    RadioInterfaceStats stats_{};
};

} // namespace oyk::radio
