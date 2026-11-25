// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace oyk::node {

enum class BbsMsgType : char {
    Personal = 'P',
    Bulletin = 'B',
    Traffic  = 'T'
};

enum class BbsMsgStatus : char {
    Unread   = 'N',
    Read     = 'Y',
    Bulletin = '$',
    Killed   = 'K'
};

struct BbsMessage {
    uint64_t id = 0;
    BbsMsgType type = BbsMsgType::Personal;
    BbsMsgStatus status = BbsMsgStatus::Unread;
    std::string from;     // Sender callsign (uppercase)
    std::string to;       // Recipient callsign or bulletin topic (uppercase)
    std::string at_bbs;   // Optional @BBS or distribution (uppercase)
    std::string date;     // YYYYMMDD-HHMMSS
    std::string subject;
    std::string body;
};

struct BbsActionResult {
    std::string response_text;
    bool left_bbs = false;
    bool disconnect_session = false;
};

// Classic Amateur Radio Packet Bulletin Board System (PBBS).
// Persists messages as human-readable M<id>.MSG files under mail_path.
class BbsServer {
public:
    bool configure(std::string bbs_callsign,
                   std::string bbs_alias,
                   std::string mail_path,
                   std::string welcome,
                   std::size_t max_msg_bytes,
                   std::string& error_out);

    BbsActionResult enter(uint64_t session_id,
                          const std::string& callsign,
                          const std::string& interface_id,
                          bool direct_connect,
                          bool is_sysop,
                          std::chrono::system_clock::time_point now);

    void leave(uint64_t session_id);

    BbsActionResult handle_line(uint64_t session_id,
                                const std::string& raw_line,
                                std::chrono::system_clock::time_point now);

    bool has_session(uint64_t session_id) const;
    std::size_t active_message_count() const;
    std::size_t unread_count_for(const std::string& callsign) const;
    const std::string& mail_path() const noexcept { return mail_path_; }

private:
    enum class ComposeStage {
        Command,
        Subject,
        Body
    };

    struct BbsSessionState {
        uint64_t session_id = 0;
        std::string callsign;      // Full uppercase callsign (e.g. W1AW-7)
        std::string base_callsign; // Base uppercase callsign (e.g. W1AW)
        std::string interface_id;
        bool direct_connect = false;
        bool is_sysop = false;

        ComposeStage stage = ComposeStage::Command;
        BbsMsgType draft_type = BbsMsgType::Personal;
        std::string draft_to;
        std::string draft_at;
        std::string draft_subject;
        std::string draft_body;
    };

    bool load_messages_from_disk(std::string& error_out);
    bool save_message_to_disk(const BbsMessage& msg) const;
    std::string message_file_path(uint64_t id) const;
    std::string prompt_string() const;

    static std::string base_call(const std::string& call);
    static bool call_matches(const std::string& target, const BbsSessionState& st);
    bool can_read(const BbsMessage& msg, const BbsSessionState& st) const;
    bool can_kill(const BbsMessage& msg, const BbsSessionState& st) const;

    std::string format_list(const std::vector<const BbsMessage*>& msgs) const;
    std::string format_message(const BbsMessage& msg) const;

    std::string bbs_callsign_ = "N0CALL-11";
    std::string bbs_alias_ = "OYKBBS";
    std::string mail_path_ = "mail";
    std::string welcome_ = "Welcome to OYK Packet BBS! Type H for help, B to exit.";
    std::size_t max_msg_bytes_ = 8192;

    uint64_t next_msg_id_ = 1;
    std::map<uint64_t, BbsMessage> messages_;
    std::map<uint64_t, BbsSessionState> sessions_;
};

} // namespace oyk::node
