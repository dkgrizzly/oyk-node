// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include "oyk/ax25/callsign.hpp"
#include "oyk/ax25/frame.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace oyk::node {

enum class SessionState {
    Connecting,
    Connected,
    Disconnecting,
    Closed
};

enum class SessionDirection {
    Inbound,
    Outbound
};

enum class SessionMode {
    Normal,
    RestrictedBan,
    LocalTerminal
};

struct SessionParams {
    std::size_t paclen = 236;
    uint8_t maxframe = 4;
    uint32_t t1_ms = 3000;
    uint8_t n2_retries = 5;
    uint32_t idle_timeout_sec = 600;
};

struct SessionEvents {
    std::vector<ax25::Frame> tx_frames;
    std::vector<uint8_t> rx_data; // Raw received payload (used in bridged mode)
    std::vector<std::string> rx_lines; // Complete text lines (used in node shell mode)
    bool just_connected = false;
    bool just_closed = false;
    std::string close_reason;
};

// AX.25 v2.0 Modulo-8 connected-mode Layer 2 session state machine.
class Session {
public:
    Session(uint64_t id,
            std::string interface_id,
            ax25::Callsign local_call,
            ax25::Callsign remote_call,
            std::string peer_endpoint,
            SessionDirection direction,
            SessionParams params);

    uint64_t id() const noexcept { return id_; }
    const std::string& interface_id() const noexcept { return interface_id_; }
    const ax25::Callsign& local_call() const noexcept { return local_call_; }
    const ax25::Callsign& remote_call() const noexcept { return remote_call_; }
    const std::string& peer_endpoint() const noexcept { return peer_endpoint_; }
    void set_peer_endpoint(std::string ep) { peer_endpoint_ = std::move(ep); }

    SessionState state() const noexcept { return state_; }
    SessionDirection direction() const noexcept { return direction_; }
    bool is_active() const noexcept {
        return state_ == SessionState::Connecting || state_ == SessionState::Connected;
    }

    uint64_t bridged_session_id() const noexcept { return bridged_session_id_; }
    void set_bridged_session_id(uint64_t other_id) noexcept { bridged_session_id_ = other_id; }

    SessionMode mode() const noexcept { return mode_; }
    void set_local_terminal() noexcept { mode_ = SessionMode::LocalTerminal; }
    void set_restricted_ban(std::string ban_key, bool appeal_eligible) {
        mode_ = SessionMode::RestrictedBan;
        ban_key_ = std::move(ban_key);
        appeal_eligible_ = appeal_eligible;
    }
    const std::string& ban_key() const noexcept { return ban_key_; }
    bool appeal_eligible() const noexcept { return appeal_eligible_; }
    void set_appeal_eligible(bool v) noexcept { appeal_eligible_ = v; }

    std::chrono::system_clock::time_point connected_at() const noexcept { return connected_at_; }
    std::chrono::steady_clock::time_point last_activity() const noexcept { return last_activity_; }

    // Transition helpers
    SessionEvents accept_inbound_sabm();
    SessionEvents initiate_outbound_connect();
    SessionEvents initiate_disconnect(const std::string& reason = "Disconnected");
    SessionEvents queue_text(const std::string& text);
    SessionEvents queue_bytes(const std::vector<uint8_t>& data);
    SessionEvents on_frame(const ax25::Frame& frame);
    SessionEvents on_tick(std::chrono::steady_clock::time_point now);

    static std::string state_name(SessionState st);

private:
    struct SentFrame {
        uint8_t ns = 0;
        ax25::Frame frame;
    };

    void flush_tx_window(SessionEvents& out);
    void acknowledge_up_to(uint8_t nr);
    void retransmit_unacked(SessionEvents& out);
    void reset_t1();
    void stop_t1();
    void touch();
    static bool seq_in_window(uint8_t va, uint8_t nr, uint8_t vs) noexcept;

    uint64_t id_;
    std::string interface_id_;
    ax25::Callsign local_call_;
    ax25::Callsign remote_call_;
    std::string peer_endpoint_;
    SessionDirection direction_;
    SessionParams params_;

    SessionState state_ = SessionState::Connecting;
    SessionMode mode_ = SessionMode::Normal;
    std::string ban_key_;
    bool appeal_eligible_ = false;
    uint64_t bridged_session_id_ = 0;

    // AX.25 modulo-8 state variables
    uint8_t vs_ = 0; // Next N(S) to send
    uint8_t va_ = 0; // Lowest unacknowledged N(S)
    uint8_t vr_ = 0; // Next expected N(S) from remote
    bool peer_busy_ = false;

    std::deque<SentFrame> unacked_;
    std::deque<uint8_t> tx_queue_;
    std::string rx_line_buf_;

    bool t1_active_ = false;
    std::chrono::steady_clock::time_point t1_deadline_{};
    uint8_t retry_count_ = 0;

    std::chrono::system_clock::time_point connected_at_;
    std::chrono::steady_clock::time_point last_activity_;
};

} // namespace oyk::node
