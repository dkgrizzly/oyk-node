// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/node/session.hpp"

#include <algorithm>

namespace oyk::node {

Session::Session(uint64_t id,
                 std::string interface_id,
                 ax25::Callsign local_call,
                 ax25::Callsign remote_call,
                 std::string peer_endpoint,
                 SessionDirection direction,
                 SessionParams params)
    : id_(id),
      interface_id_(std::move(interface_id)),
      local_call_(std::move(local_call)),
      remote_call_(std::move(remote_call)),
      peer_endpoint_(std::move(peer_endpoint)),
      direction_(direction),
      params_(params),
      connected_at_(std::chrono::system_clock::now()),
      last_activity_(std::chrono::steady_clock::now()) {
    if (params_.paclen == 0 || params_.paclen > 256) {
        params_.paclen = 236;
    }
    if (params_.maxframe == 0 || params_.maxframe > 7) {
        params_.maxframe = 4;
    }
}

std::string Session::state_name(SessionState st) {
    switch (st) {
        case SessionState::Connecting:    return "CONNECTING";
        case SessionState::Connected:     return "CONNECTED";
        case SessionState::Disconnecting: return "DISCONNECTING";
        case SessionState::Closed:        return "CLOSED";
    }
    return "UNKNOWN";
}

void Session::touch() {
    last_activity_ = std::chrono::steady_clock::now();
}

void Session::reset_t1() {
    t1_active_ = true;
    t1_deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(params_.t1_ms);
}

void Session::stop_t1() {
    t1_active_ = false;
    retry_count_ = 0;
}

bool Session::seq_in_window(uint8_t va, uint8_t nr, uint8_t vs) noexcept {
    // Checks if va <= nr <= vs in modulo-8 arithmetic
    uint8_t window = static_cast<uint8_t>((vs - va) & 0x07);
    uint8_t offset = static_cast<uint8_t>((nr - va) & 0x07);
    return offset <= window;
}

SessionEvents Session::accept_inbound_sabm() {
    SessionEvents ev;
    state_ = SessionState::Connected;
    vs_ = 0;
    va_ = 0;
    vr_ = 0;
    peer_busy_ = false;
    unacked_.clear();
    tx_queue_.clear();
    rx_line_buf_.clear();
    stop_t1();
    touch();
    ev.tx_frames.push_back(ax25::Frame::make_ua(remote_call_, local_call_, true));
    ev.just_connected = true;
    return ev;
}

SessionEvents Session::initiate_outbound_connect() {
    SessionEvents ev;
    state_ = SessionState::Connecting;
    vs_ = 0;
    va_ = 0;
    vr_ = 0;
    peer_busy_ = false;
    unacked_.clear();
    retry_count_ = 0;
    reset_t1();
    touch();
    ev.tx_frames.push_back(ax25::Frame::make_sabm(remote_call_, local_call_, true));
    return ev;
}

SessionEvents Session::initiate_disconnect(const std::string& reason) {
    SessionEvents ev;
    if (state_ == SessionState::Closed) {
        return ev;
    }
    if (state_ == SessionState::Connecting) {
        state_ = SessionState::Closed;
        stop_t1();
        ev.tx_frames.push_back(ax25::Frame::make_disc(remote_call_, local_call_, true));
        ev.just_closed = true;
        ev.close_reason = reason;
        return ev;
    }
    state_ = SessionState::Disconnecting;
    tx_queue_.clear();
    unacked_.clear();
    retry_count_ = 0;
    reset_t1();
    touch();
    ev.tx_frames.push_back(ax25::Frame::make_disc(remote_call_, local_call_, true));
    return ev;
}

SessionEvents Session::queue_text(const std::string& text) {
    std::vector<uint8_t> bytes(text.begin(), text.end());
    return queue_bytes(bytes);
}

SessionEvents Session::queue_bytes(const std::vector<uint8_t>& data) {
    SessionEvents ev;
    if (state_ != SessionState::Connected) {
        return ev;
    }
    tx_queue_.insert(tx_queue_.end(), data.begin(), data.end());
    flush_tx_window(ev);
    return ev;
}

void Session::flush_tx_window(SessionEvents& out) {
    if (state_ != SessionState::Connected || peer_busy_) {
        return;
    }
    while (!tx_queue_.empty() && unacked_.size() < params_.maxframe) {
        std::size_t chunk_len = std::min(tx_queue_.size(), params_.paclen);
        std::vector<uint8_t> chunk;
        chunk.reserve(chunk_len);
        for (std::size_t i = 0; i < chunk_len; ++i) {
            chunk.push_back(tx_queue_.front());
            tx_queue_.pop_front();
        }
        ax25::Frame iframe = ax25::Frame::make_i(remote_call_, local_call_, vs_, vr_, chunk, false);
        unacked_.push_back(SentFrame{vs_, iframe});
        vs_ = static_cast<uint8_t>((vs_ + 1) & 0x07);
        out.tx_frames.push_back(std::move(iframe));
        if (!t1_active_) {
            reset_t1();
        }
    }
}

void Session::acknowledge_up_to(uint8_t nr) {
    if (!seq_in_window(va_, nr, vs_)) {
        return;
    }
    while (!unacked_.empty() && va_ != nr) {
        unacked_.pop_front();
        va_ = static_cast<uint8_t>((va_ + 1) & 0x07);
    }
    if (unacked_.empty()) {
        stop_t1();
    } else {
        retry_count_ = 0;
        reset_t1();
    }
}

void Session::retransmit_unacked(SessionEvents& out) {
    for (auto& sf : unacked_) {
        // Refresh N(R) with our latest V(R)
        sf.frame.control = static_cast<uint8_t>((sf.frame.control & 0x1F) | ((vr_ & 0x07) << 5));
        out.tx_frames.push_back(sf.frame);
    }
    if (!unacked_.empty()) {
        reset_t1();
    }
}

SessionEvents Session::on_frame(const ax25::Frame& frame) {
    SessionEvents ev;
    touch();

    const ax25::FrameType ft = frame.type();

    if (state_ == SessionState::Closed) {
        if (ft != ax25::FrameType::DM && ft != ax25::FrameType::UI) {
            ev.tx_frames.push_back(ax25::Frame::make_dm(remote_call_, local_call_, frame.pf()));
        }
        return ev;
    }

    if (state_ == SessionState::Connecting) {
        if (ft == ax25::FrameType::UA) {
            state_ = SessionState::Connected;
            stop_t1();
            ev.just_connected = true;
            flush_tx_window(ev);
        } else if (ft == ax25::FrameType::DM) {
            state_ = SessionState::Closed;
            stop_t1();
            ev.just_closed = true;
            ev.close_reason = "Connection refused (DM)";
        } else if (ft == ax25::FrameType::DISC) {
            state_ = SessionState::Closed;
            stop_t1();
            ev.tx_frames.push_back(ax25::Frame::make_dm(remote_call_, local_call_, frame.pf()));
            ev.just_closed = true;
            ev.close_reason = "Disconnected by remote";
        }
        return ev;
    }

    if (state_ == SessionState::Disconnecting) {
        if (ft == ax25::FrameType::UA || ft == ax25::FrameType::DM) {
            state_ = SessionState::Closed;
            stop_t1();
            ev.just_closed = true;
            ev.close_reason = "Disconnected";
        } else if (ft == ax25::FrameType::DISC) {
            state_ = SessionState::Closed;
            stop_t1();
            ev.tx_frames.push_back(ax25::Frame::make_ua(remote_call_, local_call_, frame.pf()));
            ev.just_closed = true;
            ev.close_reason = "Disconnected";
        }
        return ev;
    }

    // state_ == SessionState::Connected
    switch (ft) {
        case ax25::FrameType::SABM: {
            // Link reset by remote station
            vs_ = va_ = vr_ = 0;
            peer_busy_ = false;
            unacked_.clear();
            tx_queue_.clear();
            rx_line_buf_.clear();
            stop_t1();
            ev.tx_frames.push_back(ax25::Frame::make_ua(remote_call_, local_call_, frame.pf()));
            break;
        }
        case ax25::FrameType::DISC: {
            state_ = SessionState::Closed;
            stop_t1();
            ev.tx_frames.push_back(ax25::Frame::make_ua(remote_call_, local_call_, frame.pf()));
            ev.just_closed = true;
            ev.close_reason = "Disconnected by remote";
            break;
        }
        case ax25::FrameType::DM:
        case ax25::FrameType::FRMR: {
            state_ = SessionState::Closed;
            stop_t1();
            ev.just_closed = true;
            ev.close_reason = (ft == ax25::FrameType::DM) ? "Link reset (DM)" : "Protocol error (FRMR)";
            break;
        }
        case ax25::FrameType::I: {
            acknowledge_up_to(frame.nr());
            if (frame.ns() == vr_) {
                vr_ = static_cast<uint8_t>((vr_ + 1) & 0x07);
                ev.rx_data.insert(ev.rx_data.end(), frame.info.begin(), frame.info.end());

                if (bridged_session_id_ == 0 && mode_ != SessionMode::LocalTerminal) {
                    for (uint8_t b : frame.info) {
                        char c = static_cast<char>(b);
                        if (c == '\r' || c == '\n') {
                            if (!rx_line_buf_.empty() || c == '\r') {
                                ev.rx_lines.push_back(rx_line_buf_);
                                rx_line_buf_.clear();
                            }
                        } else if (c == '\b' || b == 0x7F) {
                            if (!rx_line_buf_.empty()) {
                                rx_line_buf_.pop_back();
                            }
                        } else if (b >= 0x20 && b < 0x7F) {
                            if (rx_line_buf_.size() < 512) {
                                rx_line_buf_.push_back(c);
                            }
                        }
                    }
                }
                ev.tx_frames.push_back(ax25::Frame::make_rr(remote_call_, local_call_, vr_, false, frame.pf()));
            } else {
                // Distinguish duplicate (already received) vs out-of-sequence ahead
                uint8_t diff = static_cast<uint8_t>((frame.ns() - vr_) & 0x07);
                if (diff < 4) {
                    ev.tx_frames.push_back(ax25::Frame::make_rej(remote_call_, local_call_, vr_, false, frame.pf()));
                } else {
                    ev.tx_frames.push_back(ax25::Frame::make_rr(remote_call_, local_call_, vr_, false, frame.pf()));
                }
            }
            flush_tx_window(ev);
            break;
        }
        case ax25::FrameType::RR: {
            peer_busy_ = false;
            acknowledge_up_to(frame.nr());
            if (frame.is_command && frame.pf()) {
                ev.tx_frames.push_back(ax25::Frame::make_rr(remote_call_, local_call_, vr_, false, true));
            }
            flush_tx_window(ev);
            break;
        }
        case ax25::FrameType::RNR: {
            peer_busy_ = true;
            acknowledge_up_to(frame.nr());
            if (frame.is_command && frame.pf()) {
                ev.tx_frames.push_back(ax25::Frame::make_rr(remote_call_, local_call_, vr_, false, true));
            }
            break;
        }
        case ax25::FrameType::REJ: {
            peer_busy_ = false;
            acknowledge_up_to(frame.nr());
            retransmit_unacked(ev);
            flush_tx_window(ev);
            break;
        }
        default:
            break;
    }

    return ev;
}

std::vector<std::string> Session::feed_local_loopback_bytes(const std::vector<uint8_t>& data) {
    touch();
    std::vector<std::string> lines;
    for (uint8_t b : data) {
        char c = static_cast<char>(b);
        if (c == '\r' || c == '\n') {
            if (!rx_line_buf_.empty() || c == '\r') {
                lines.push_back(rx_line_buf_);
                rx_line_buf_.clear();
            }
        } else if (c == '\b' || b == 0x7F) {
            if (!rx_line_buf_.empty()) rx_line_buf_.pop_back();
        } else if (b >= 0x20 && b < 0x7F) {
            if (rx_line_buf_.size() < 512) rx_line_buf_.push_back(c);
        }
    }
    return lines;
}

SessionEvents Session::on_tick(std::chrono::steady_clock::time_point now) {
    SessionEvents ev;
    if (state_ == SessionState::Closed) {
        return ev;
    }

    // Idle timeout check (only when connected and not waiting for ack)
    if (state_ == SessionState::Connected && params_.idle_timeout_sec > 0 && !t1_active_) {
        auto idle = std::chrono::duration_cast<std::chrono::seconds>(now - last_activity_).count();
        if (idle >= static_cast<int64_t>(params_.idle_timeout_sec)) {
            return initiate_disconnect("Idle timeout");
        }
    }

    if (t1_active_ && now >= t1_deadline_) {
        ++retry_count_;
        if (retry_count_ > params_.n2_retries) {
            state_ = SessionState::Closed;
            stop_t1();
            ev.tx_frames.push_back(ax25::Frame::make_dm(remote_call_, local_call_, false));
            ev.just_closed = true;
            ev.close_reason = "Retry limit (N2) exceeded";
            return ev;
        }

        if (state_ == SessionState::Connecting) {
            ev.tx_frames.push_back(ax25::Frame::make_sabm(remote_call_, local_call_, true));
            reset_t1();
        } else if (state_ == SessionState::Disconnecting) {
            ev.tx_frames.push_back(ax25::Frame::make_disc(remote_call_, local_call_, true));
            reset_t1();
        } else if (state_ == SessionState::Connected) {
            if (!unacked_.empty()) {
                retransmit_unacked(ev);
            } else {
                ev.tx_frames.push_back(ax25::Frame::make_rr(remote_call_, local_call_, vr_, true, true));
                reset_t1();
            }
        }
    }

    return ev;
}

} // namespace oyk::node
