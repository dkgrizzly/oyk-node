// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace oyk::node {

struct ChatDelivery {
    uint64_t session_id = 0;
    std::string text;
};

struct ChatActionResult {
    std::vector<ChatDelivery> deliveries;
    bool left_chat = false;
    bool disconnect_session = false;
};

struct ChatParticipant {
    uint64_t session_id = 0;
    std::string callsign;
    std::string interface_id;
    std::string topic = "Lobby";
    std::string name;
    bool direct_connect = false; // true = connected directly to CHAT SSID; false = entered via node CHAT cmd
};

// Multi-user amateur radio packet CHAT conference room.
// Supports multiple topics/channels, private messages, display handles, and
// cross-interface broadcasting across RF, AXUDP, Telnet, and Local Terminal.
class ChatRoom {
public:
    void configure(std::string chat_callsign, std::string default_topic, std::string welcome);

    ChatActionResult join(uint64_t session_id,
                          const std::string& callsign,
                          const std::string& interface_id,
                          bool direct_connect);

    ChatActionResult leave(uint64_t session_id, const std::string& reason = "");

    ChatActionResult handle_line(uint64_t session_id, const std::string& raw_line);

    bool has_participant(uint64_t session_id) const;
    std::vector<ChatParticipant> participants() const;

private:
    void broadcast_to_topic(const std::string& topic,
                            uint64_t exclude_sid,
                            const std::string& text,
                            std::vector<ChatDelivery>& out) const;

    std::string chat_callsign_ = "N0CALL-12";
    std::string default_topic_ = "Lobby";
    std::string welcome_ = "Welcome to OYK Packet Chat! Type /h for help, /q to leave.";
    std::map<uint64_t, ChatParticipant> users_;
};

} // namespace oyk::node
