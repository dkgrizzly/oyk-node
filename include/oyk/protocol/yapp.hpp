// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include "oyk/ax25/callsign.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace oyk::protocol {

// WA7MBL YAPP & F6FBB YAPP-C control bytes
constexpr uint8_t YAPP_SOH = 0x01;
constexpr uint8_t YAPP_STX = 0x02;
constexpr uint8_t YAPP_ETX = 0x03;
constexpr uint8_t YAPP_EOT = 0x04;
constexpr uint8_t YAPP_ENQ = 0x05;
constexpr uint8_t YAPP_ACK = 0x06;
constexpr uint8_t YAPP_DLE = 0x10;
constexpr uint8_t YAPP_NAK = 0x15;
constexpr uint8_t YAPP_CAN = 0x18;

enum class YappState {
    Idle = 0,
    // Sender states
    SendWaitInitAck,
    SendWaitHeaderAck,
    SendingData,
    SendWaitEofAck,
    SendWaitEotAck,
    // Receiver states
    RecvWaitHeader,
    RecvAwaitOverwrite,
    RecvData,
    RecvWaitEot,
    // Terminal states
    Complete,
    Aborted,
    Error
};

struct YappProgress {
    YappState state = YappState::Idle;
    bool sending = false;
    bool yappc_checksum = false;
    std::string filename;
    std::string target_path;
    uint64_t bytes_transferred = 0;
    uint64_t total_bytes = 0;
    std::string status_message;
};

// Validates a YAPP filename against path traversal (../, ..\, /, \, drive letters,
// control characters, leading dots, and reserved DOS/Windows device names).
bool is_safe_yapp_filename(std::string_view name);

// Formats a safe remote directory name with callsign and SSID (e.g., "N0CALL-11").
std::string format_yapp_peer_dir(const ax25::Callsign& remote_call);

// Resolves incoming/<CALLSIGN-SSID>/<filename> safely; returns empty path if unsafe.
std::filesystem::path resolve_yapp_incoming_path(const std::filesystem::path& incoming_root,
                                                 const ax25::Callsign& remote_call,
                                                 std::string_view filename);

// YAPP / YAPP-C bidirectional file transfer state machine.
// Feed raw bytes received from an AX.25 stream via feed_bytes(); any non-YAPP
// bytes while Idle are returned in plain_out so normal terminal text passes through.
// When ENQ 0x01 is detected while Idle, receive mode engages automatically.
class YappSession {
public:
    using SendFn = std::function<void(const std::vector<uint8_t>&)>;
    // Optional synchronous overwrite prompt callback.
    // If unset (nullptr), receiving an existing file pauses in YappState::RecvAwaitOverwrite
    // so a UI thread can prompt the user and call resolve_overwrite(bool).
    using OverwriteFn = std::function<bool(const std::filesystem::path& target_path,
                                           const std::string& filename)>;

    explicit YappSession(std::filesystem::path incoming_root = "incoming");
    ~YappSession();

    YappSession(const YappSession&) = delete;
    YappSession& operator=(const YappSession&) = delete;

    void set_incoming_root(std::filesystem::path root) { incoming_root_ = std::move(root); }
    const std::filesystem::path& incoming_root() const noexcept { return incoming_root_; }

    void set_remote_callsign(const ax25::Callsign& call) { remote_call_ = call; }
    void set_send_callback(SendFn fn) { send_fn_ = std::move(fn); }
    void set_overwrite_callback(OverwriteFn fn) { overwrite_fn_ = std::move(fn); }

    bool is_active() const noexcept;
    bool awaiting_overwrite() const noexcept { return state_ == YappState::RecvAwaitOverwrite; }
    YappProgress progress() const;

    // Reset back to Idle (closing any open file).
    void reset();

    // Start sending a local file to the connected peer.
    bool start_send(const std::filesystem::path& local_file, std::string& error_out);

    // Cancel an in-progress transfer and notify the peer.
    void cancel(const std::string& reason = "Cancelled by user");

    // Resolve a pending overwrite confirmation (when state == RecvAwaitOverwrite).
    void resolve_overwrite(bool allow_overwrite);

    // Feed raw received bytes from the AX.25 session.
    // Non-YAPP bytes received while Idle are appended to plain_out.
    void feed_bytes(const uint8_t* data, std::size_t len, std::vector<uint8_t>& plain_out);

private:
    void send_packet(const std::vector<uint8_t>& pkt);
    void send_two_byte(uint8_t b0, uint8_t b1);
    void send_cancel_pkt(const std::string& reason);
    void send_nak_pkt(const std::string& reason);
    void send_header_pkt();
    void send_next_data_chunks();
    void handle_yapp_byte(uint8_t b);
    void handle_complete_packet(uint8_t type, const std::vector<uint8_t>& payload);
    void begin_open_receive_file();
    void close_file();

    std::filesystem::path incoming_root_;
    ax25::Callsign remote_call_{"UNKNOWN", 0};
    SendFn send_fn_;
    OverwriteFn overwrite_fn_;

    YappState state_ = YappState::Idle;
    bool sending_ = false;
    bool yappc_ = false;
    bool pending_enq_ = false;

    std::string filename_;
    std::filesystem::path target_path_;
    uint64_t total_bytes_ = 0;
    uint64_t bytes_transferred_ = 0;
    std::string status_message_;

    std::FILE* fp_ = nullptr;

    // Incremental packet parser state
    enum class ParseStage { Type, LenOrCode, Payload, Checksum };
    ParseStage parse_stage_ = ParseStage::Type;
    uint8_t pkt_type_ = 0;
    std::size_t pkt_expected_ = 0;
    std::vector<uint8_t> pkt_buf_;
    bool pkt_expect_csum_ = false;
};

} // namespace oyk::protocol
