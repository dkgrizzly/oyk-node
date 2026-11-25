// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/protocol/yapp.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <system_error>

namespace oyk::protocol {

namespace {

constexpr std::size_t YAPP_TX_CHUNK = 128;

bool is_reserved_dos_name(std::string_view name) {
    std::string stem;
    for (char c : name) {
        if (c == '.') break;
        stem.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") {
        return true;
    }
    if (stem.size() == 4 &&
        (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) &&
        stem[3] >= '1' && stem[3] <= '9') {
        return true;
    }
    return false;
}

uint8_t yapp_checksum(const uint8_t* data, std::size_t len) {
    uint32_t sum = 0;
    for (std::size_t i = 0; i < len; ++i) {
        sum += data[i];
    }
    return static_cast<uint8_t>(sum & 0xFFu);
}

} // namespace

bool is_safe_yapp_filename(std::string_view name) {
    if (name.empty() || name.size() > 128) return false;
    if (name.front() == '.' || name.back() == '.' || name == "." || name == "..") return false;
    if (name.find("..") != std::string_view::npos) return false;
    for (char ch : name) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x20u || c >= 0x7Fu) return false;
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' ||
            ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
            return false;
        }
    }
    if (is_reserved_dos_name(name)) return false;
    return true;
}

std::string format_yapp_peer_dir(const ax25::Callsign& remote_call) {
    std::string base;
    for (char ch : remote_call.base()) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) {
            base.push_back(static_cast<char>(std::toupper(c)));
        }
    }
    if (base.empty()) base = "UNKNOWN";
    return base + "-" + std::to_string(static_cast<unsigned>(remote_call.ssid() & 0x0Fu));
}

std::filesystem::path resolve_yapp_incoming_path(const std::filesystem::path& incoming_root,
                                                 const ax25::Callsign& remote_call,
                                                 std::string_view filename) {
    if (!is_safe_yapp_filename(filename)) {
        return {};
    }
    std::filesystem::path root = incoming_root.empty() ? std::filesystem::path("incoming") : incoming_root;
    return root / format_yapp_peer_dir(remote_call) / std::string(filename);
}

YappSession::YappSession(std::filesystem::path incoming_root)
    : incoming_root_(incoming_root.empty() ? std::filesystem::path("incoming") : std::move(incoming_root)) {}

YappSession::~YappSession() {
    close_file();
}

void YappSession::close_file() {
    if (fp_) {
        std::fclose(fp_);
        fp_ = nullptr;
    }
}

bool YappSession::is_active() const noexcept {
    return state_ != YappState::Idle &&
           state_ != YappState::Complete &&
           state_ != YappState::Aborted &&
           state_ != YappState::Error;
}

YappProgress YappSession::progress() const {
    YappProgress p;
    p.state = state_;
    p.sending = sending_;
    p.yappc_checksum = yappc_;
    p.filename = filename_;
    p.target_path = target_path_.string();
    p.bytes_transferred = bytes_transferred_;
    p.total_bytes = total_bytes_;
    p.status_message = status_message_;
    return p;
}

void YappSession::reset() {
    close_file();
    state_ = YappState::Idle;
    sending_ = false;
    yappc_ = false;
    pending_enq_ = false;
    filename_.clear();
    target_path_.clear();
    total_bytes_ = 0;
    bytes_transferred_ = 0;
    status_message_.clear();
    parse_stage_ = ParseStage::Type;
    pkt_type_ = 0;
    pkt_expected_ = 0;
    pkt_buf_.clear();
    pkt_expect_csum_ = false;
}

void YappSession::send_packet(const std::vector<uint8_t>& pkt) {
    if (send_fn_ && !pkt.empty()) {
        send_fn_(pkt);
    }
}

void YappSession::send_two_byte(uint8_t b0, uint8_t b1) {
    send_packet({b0, b1});
}

void YappSession::send_cancel_pkt(const std::string& reason) {
    std::size_t n = std::min<std::size_t>(reason.size(), 200);
    std::vector<uint8_t> pkt;
    pkt.reserve(2 + n);
    pkt.push_back(YAPP_CAN);
    pkt.push_back(static_cast<uint8_t>(n));
    pkt.insert(pkt.end(), reason.begin(), reason.begin() + static_cast<std::ptrdiff_t>(n));
    send_packet(pkt);
}

void YappSession::send_nak_pkt(const std::string& reason) {
    std::size_t n = std::min<std::size_t>(reason.size(), 200);
    std::vector<uint8_t> pkt;
    pkt.reserve(2 + n);
    pkt.push_back(YAPP_NAK);
    pkt.push_back(static_cast<uint8_t>(n));
    pkt.insert(pkt.end(), reason.begin(), reason.begin() + static_cast<std::ptrdiff_t>(n));
    send_packet(pkt);
}

bool YappSession::start_send(const std::filesystem::path& local_file, std::string& error_out) {
    if (is_active()) {
        error_out = "A YAPP transfer is already in progress";
        return false;
    }
    reset();

    std::error_code ec;
    if (!std::filesystem::is_regular_file(local_file, ec)) {
        error_out = "Selected path is not a regular file";
        return false;
    }
    std::string base = local_file.filename().string();
    while (!base.empty() && base.back() == '.') {
        base.pop_back();
    }
    if (!is_safe_yapp_filename(base)) {
        error_out = "File name contains characters not supported by YAPP";
        return false;
    }

    fp_ = std::fopen(local_file.string().c_str(), "rb");
    if (!fp_) {
        error_out = "Unable to open file for reading";
        return false;
    }

    total_bytes_ = std::filesystem::file_size(local_file, ec);
    if (ec) total_bytes_ = 0;

    filename_ = base;
    target_path_ = local_file;
    sending_ = true;
    state_ = YappState::SendWaitInitAck;
    status_message_ = "YAPP: Sending init for " + filename_ + "...";
    send_two_byte(YAPP_ENQ, 0x01);
    return true;
}

void YappSession::cancel(const std::string& reason) {
    if (!is_active()) return;
    close_file();
    send_cancel_pkt(reason);
    state_ = YappState::Aborted;
    status_message_ = "YAPP cancelled: " + reason;
}

void YappSession::send_header_pkt() {
    std::string size_str = std::to_string(total_bytes_);
    // Include 8-hex-char date field to advertise YAPP-C checksum capability
    std::string date_str = "00000000";

    std::vector<uint8_t> payload;
    payload.insert(payload.end(), filename_.begin(), filename_.end());
    payload.push_back(0x00);
    payload.insert(payload.end(), size_str.begin(), size_str.end());
    payload.push_back(0x00);
    payload.insert(payload.end(), date_str.begin(), date_str.end());
    payload.push_back(0x00);

    std::vector<uint8_t> pkt;
    pkt.reserve(2 + payload.size());
    pkt.push_back(YAPP_SOH);
    pkt.push_back(static_cast<uint8_t>(payload.size() & 0xFFu));
    pkt.insert(pkt.end(), payload.begin(), payload.end());
    send_packet(pkt);
}

void YappSession::send_next_data_chunks() {
    if (!fp_ || state_ != YappState::SendingData) return;

    uint8_t buf[YAPP_TX_CHUNK];
    // Stream all chunks over the reliable AX.25 connected-mode link
    while (true) {
        std::size_t n = std::fread(buf, 1, sizeof(buf), fp_);
        if (n == 0) break;

        std::vector<uint8_t> pkt;
        pkt.reserve(2 + n + (yappc_ ? 1 : 0));
        pkt.push_back(YAPP_STX);
        pkt.push_back(static_cast<uint8_t>(n & 0xFFu));
        pkt.insert(pkt.end(), buf, buf + n);
        if (yappc_) {
            pkt.push_back(yapp_checksum(buf, n));
        }
        send_packet(pkt);
        bytes_transferred_ += n;
    }

    close_file();
    state_ = YappState::SendWaitEofAck;
    status_message_ = "YAPP: Sent " + std::to_string(bytes_transferred_) + " bytes; awaiting EOF ack...";
    send_two_byte(YAPP_ETX, 0x01);
}

void YappSession::begin_open_receive_file() {
    std::error_code ec;
    std::filesystem::create_directories(target_path_.parent_path(), ec);
    if (ec) {
        send_cancel_pkt("Unable to create incoming directory");
        state_ = YappState::Error;
        status_message_ = "YAPP error: cannot create directory " + target_path_.parent_path().string();
        return;
    }

    fp_ = std::fopen(target_path_.string().c_str(), "wb");
    if (!fp_) {
        send_cancel_pkt("Unable to create destination file");
        state_ = YappState::Error;
        status_message_ = "YAPP error: cannot open " + target_path_.string() + " for writing";
        return;
    }

    bytes_transferred_ = 0;
    state_ = YappState::RecvData;
    status_message_ = "YAPP: Receiving " + filename_ + " -> " + target_path_.string();
    send_two_byte(YAPP_ACK, yappc_ ? 0x06 : 0x02);
}

void YappSession::resolve_overwrite(bool allow_overwrite) {
    if (state_ != YappState::RecvAwaitOverwrite) return;
    if (!allow_overwrite) {
        send_cancel_pkt("File exists; overwrite declined");
        state_ = YappState::Aborted;
        status_message_ = "YAPP: Skipped existing file " + target_path_.string();
        return;
    }
    begin_open_receive_file();
}

void YappSession::feed_bytes(const uint8_t* data, std::size_t len, std::vector<uint8_t>& plain_out) {
    for (std::size_t i = 0; i < len; ++i) {
        uint8_t b = data[i];
        if (!is_active()) {
            // Auto-detect incoming YAPP Send Init (ENQ 0x01)
            if (pending_enq_) {
                if (b == 0x01) {
                    pending_enq_ = false;
                    reset();
                    sending_ = false;
                    state_ = YappState::RecvWaitHeader;
                    status_message_ = "YAPP: Incoming transfer initiated; waiting for header...";
                    send_two_byte(YAPP_ACK, 0x01);
                    continue;
                } else if (b == YAPP_ENQ) {
                    plain_out.push_back(YAPP_ENQ);
                    continue;
                } else {
                    pending_enq_ = false;
                    plain_out.push_back(YAPP_ENQ);
                    plain_out.push_back(b);
                    continue;
                }
            }
            if (b == YAPP_ENQ) {
                pending_enq_ = true;
                continue;
            }
            plain_out.push_back(b);
            continue;
        }
        handle_yapp_byte(b);
    }
}

void YappSession::handle_yapp_byte(uint8_t b) {
    switch (parse_stage_) {
        case ParseStage::Type:
            pkt_type_ = b;
            pkt_buf_.clear();
            pkt_expected_ = 0;
            pkt_expect_csum_ = false;
            if (b == YAPP_ENQ || b == YAPP_ACK || b == YAPP_ETX || b == YAPP_EOT ||
                b == YAPP_SOH || b == YAPP_STX || b == YAPP_NAK || b == YAPP_CAN || b == YAPP_DLE) {
                parse_stage_ = ParseStage::LenOrCode;
            }
            break;

        case ParseStage::LenOrCode:
            if (pkt_type_ == YAPP_ENQ || pkt_type_ == YAPP_ACK ||
                pkt_type_ == YAPP_ETX || pkt_type_ == YAPP_EOT) {
                pkt_buf_.push_back(b);
                parse_stage_ = ParseStage::Type;
                handle_complete_packet(pkt_type_, pkt_buf_);
            } else {
                // Length-prefixed packet (SOH, STX, NAK, CAN, DLE); 0 means 256 for STX/SOH
                pkt_expected_ = (b == 0 && (pkt_type_ == YAPP_STX || pkt_type_ == YAPP_SOH)) ? 256u : b;
                pkt_expect_csum_ = (pkt_type_ == YAPP_STX && yappc_);
                if (pkt_expected_ == 0) {
                    if (pkt_expect_csum_) {
                        parse_stage_ = ParseStage::Checksum;
                    } else {
                        parse_stage_ = ParseStage::Type;
                        handle_complete_packet(pkt_type_, pkt_buf_);
                    }
                } else {
                    parse_stage_ = ParseStage::Payload;
                }
            }
            break;

        case ParseStage::Payload:
            pkt_buf_.push_back(b);
            if (pkt_buf_.size() >= pkt_expected_) {
                if (pkt_expect_csum_) {
                    parse_stage_ = ParseStage::Checksum;
                } else {
                    parse_stage_ = ParseStage::Type;
                    handle_complete_packet(pkt_type_, pkt_buf_);
                }
            }
            break;

        case ParseStage::Checksum: {
            uint8_t calc = yapp_checksum(pkt_buf_.data(), pkt_buf_.size());
            parse_stage_ = ParseStage::Type;
            if (calc != b) {
                close_file();
                send_cancel_pkt("YAPP-C checksum mismatch");
                state_ = YappState::Error;
                status_message_ = "YAPP error: checksum mismatch";
            } else {
                handle_complete_packet(pkt_type_, pkt_buf_);
            }
            break;
        }
    }
}

void YappSession::handle_complete_packet(uint8_t type, const std::vector<uint8_t>& payload) {
    if (type == YAPP_CAN) {
        std::string reason(payload.begin(), payload.end());
        close_file();
        send_two_byte(YAPP_ACK, 0x05);
        state_ = YappState::Aborted;
        status_message_ = "YAPP cancelled by peer" + (reason.empty() ? "" : (": " + reason));
        return;
    }
    if (type == YAPP_NAK) {
        std::string reason(payload.begin(), payload.end());
        close_file();
        state_ = YappState::Error;
        status_message_ = "YAPP rejected by peer" + (reason.empty() ? "" : (": " + reason));
        return;
    }
    if (type == YAPP_DLE) {
        return;
    }

    switch (state_) {
        case YappState::SendWaitInitAck:
            if (type == YAPP_ACK && !payload.empty() && payload[0] == 0x01) {
                state_ = YappState::SendWaitHeaderAck;
                status_message_ = "YAPP: Sending header for " + filename_ + "...";
                send_header_pkt();
            }
            break;

        case YappState::SendWaitHeaderAck:
            if (type == YAPP_ACK && !payload.empty() &&
                (payload[0] == 0x02 || payload[0] == 0x06)) {
                yappc_ = (payload[0] == 0x06);
                state_ = YappState::SendingData;
                status_message_ = std::string("YAPP") + (yappc_ ? "-C" : "") + ": Sending " + filename_ + "...";
                send_next_data_chunks();
            }
            break;

        case YappState::SendWaitEofAck:
            if (type == YAPP_ACK && !payload.empty() && payload[0] == 0x03) {
                state_ = YappState::SendWaitEotAck;
                send_two_byte(YAPP_EOT, 0x01);
            }
            break;

        case YappState::SendWaitEotAck:
            if (type == YAPP_ACK && !payload.empty() && payload[0] == 0x04) {
                state_ = YappState::Complete;
                status_message_ = "YAPP upload complete: " + filename_ +
                                  " (" + std::to_string(bytes_transferred_) + " bytes)";
            }
            break;

        case YappState::RecvWaitHeader:
            if (type == YAPP_ENQ && !payload.empty() && payload[0] == 0x01) {
                // Retransmitted Send Init
                send_two_byte(YAPP_ACK, 0x01);
            } else if (type == YAPP_EOT && !payload.empty() && payload[0] == 0x01) {
                send_two_byte(YAPP_ACK, 0x04);
                state_ = YappState::Complete;
                status_message_ = "YAPP transfer complete.";
            } else if (type == YAPP_SOH) {
                // Parse NUL-separated fields: filename\0size\0[date\0]
                std::vector<std::string> fields;
                std::size_t pos = 0;
                while (pos < payload.size()) {
                    std::size_t nul = pos;
                    while (nul < payload.size() && payload[nul] != 0x00) ++nul;
                    fields.emplace_back(reinterpret_cast<const char*>(payload.data() + pos), nul - pos);
                    pos = (nul < payload.size()) ? (nul + 1) : payload.size();
                }

                std::string fname = fields.empty() ? "" : fields[0];
                // Strip trailing periods from legacy DOS/Win16 clients (e.g. "LICENSE." -> "LICENSE")
                // only after ensuring no path traversal or leading period is present.
                if (!fname.empty() && fname.front() != '.' &&
                    fname.find("..") == std::string::npos &&
                    fname.find('/') == std::string::npos &&
                    fname.find('\\') == std::string::npos) {
                    while (!fname.empty() && fname.back() == '.') {
                        fname.pop_back();
                    }
                }
                if (!is_safe_yapp_filename(fname)) {
                    send_cancel_pkt("Invalid filename (path traversal rejected)");
                    state_ = YappState::Error;
                    status_message_ = "YAPP security error: rejected unsafe filename '" + fname + "'";
                    return;
                }

                filename_ = fname;
                total_bytes_ = 0;
                if (fields.size() >= 2 && !fields[1].empty()) {
                    try {
                        total_bytes_ = std::stoull(fields[1]);
                    } catch (...) {
                        total_bytes_ = 0;
                    }
                }
                // F6FBB YAPP-C convention: 3rd field (date) signals YAPP-C checksum support
                yappc_ = (fields.size() >= 3 && !fields[2].empty());

                target_path_ = resolve_yapp_incoming_path(incoming_root_, remote_call_, filename_);
                if (target_path_.empty()) {
                    send_cancel_pkt("Invalid destination path");
                    state_ = YappState::Error;
                    status_message_ = "YAPP error: unable to resolve destination path";
                    return;
                }

                std::error_code ec;
                if (std::filesystem::exists(target_path_, ec)) {
                    if (overwrite_fn_) {
                        if (!overwrite_fn_(target_path_, filename_)) {
                            send_cancel_pkt("File exists; overwrite declined");
                            state_ = YappState::Aborted;
                            status_message_ = "YAPP: Skipped existing file " + target_path_.string();
                            return;
                        }
                        begin_open_receive_file();
                    } else {
                        state_ = YappState::RecvAwaitOverwrite;
                        status_message_ = "YAPP: File exists (" + target_path_.string() + "); confirm overwrite";
                    }
                    return;
                }

                begin_open_receive_file();
            }
            break;

        case YappState::RecvData:
            if (type == YAPP_STX) {
                if (fp_ && !payload.empty()) {
                    std::size_t wrote = std::fwrite(payload.data(), 1, payload.size(), fp_);
                    if (wrote != payload.size()) {
                        close_file();
                        send_cancel_pkt("Disk write error");
                        state_ = YappState::Error;
                        status_message_ = "YAPP error: disk write failed";
                        return;
                    }
                    bytes_transferred_ += wrote;
                }
            } else if (type == YAPP_ETX && !payload.empty() && payload[0] == 0x01) {
                close_file();
                state_ = YappState::RecvWaitEot;
                status_message_ = "YAPP: Saved " + target_path_.string() +
                                  " (" + std::to_string(bytes_transferred_) + " bytes)";
                send_two_byte(YAPP_ACK, 0x03);
            }
            break;

        case YappState::RecvWaitEot:
            if (type == YAPP_EOT && !payload.empty() && payload[0] == 0x01) {
                send_two_byte(YAPP_ACK, 0x04);
                state_ = YappState::Complete;
                status_message_ = "YAPP download complete: " + target_path_.string() +
                                  " (" + std::to_string(bytes_transferred_) + " bytes)";
            } else if (type == YAPP_SOH) {
                // Multi-file batch: process next header
                state_ = YappState::RecvWaitHeader;
                handle_complete_packet(type, payload);
            }
            break;

        default:
            break;
    }
}

} // namespace oyk::protocol
