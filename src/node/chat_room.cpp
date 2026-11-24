// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/node/chat_room.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace oyk::node {

namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string sanitize_token(const std::string& s, std::size_t max_len) {
    std::string out;
    for (unsigned char c : s) {
        if (c >= 0x21 && c <= 0x7E) {
            out.push_back(static_cast<char>(c));
            if (out.size() >= max_len) break;
        }
    }
    return out;
}

} // namespace

void ChatRoom::configure(std::string chat_callsign, std::string default_topic, std::string welcome) {
    if (!chat_callsign.empty()) chat_callsign_ = std::move(chat_callsign);
    if (!default_topic.empty()) default_topic_ = std::move(default_topic);
    if (!welcome.empty()) welcome_ = std::move(welcome);
}

bool ChatRoom::has_participant(uint64_t session_id) const {
    return users_.count(session_id) > 0;
}

std::vector<ChatParticipant> ChatRoom::participants() const {
    std::vector<ChatParticipant> out;
    out.reserve(users_.size());
    for (const auto& [_, p] : users_) {
        out.push_back(p);
    }
    return out;
}

void ChatRoom::broadcast_to_topic(const std::string& topic,
                                  uint64_t exclude_sid,
                                  const std::string& text,
                                  std::vector<ChatDelivery>& out) const {
    const std::string target = to_lower(topic);
    for (const auto& [sid, p] : users_) {
        if (sid != exclude_sid && to_lower(p.topic) == target) {
            out.push_back(ChatDelivery{sid, text});
        }
    }
}

ChatActionResult ChatRoom::join(uint64_t session_id,
                                const std::string& callsign,
                                const std::string& interface_id,
                                bool direct_connect) {
    ChatActionResult res;
    ChatParticipant p;
    p.session_id = session_id;
    p.callsign = to_upper(callsign);
    p.interface_id = interface_id;
    p.topic = default_topic_;
    p.direct_connect = direct_connect;
    users_[session_id] = p;

    std::ostringstream oss;
    oss << "*** " << chat_callsign_ << " Packet Chat ***\r"
        << welcome_ << "\r"
        << "Topic: " << p.topic << " | Users online: " << users_.size() << "\r";
    res.deliveries.push_back(ChatDelivery{session_id, oss.str()});

    broadcast_to_topic(p.topic, session_id,
                       "*** " + p.callsign + " joined " + p.topic + "\r",
                       res.deliveries);
    return res;
}

ChatActionResult ChatRoom::leave(uint64_t session_id, const std::string& reason) {
    ChatActionResult res;
    auto it = users_.find(session_id);
    if (it == users_.end()) return res;

    ChatParticipant p = it->second;
    users_.erase(it);

    std::string note = "*** " + p.callsign + " left chat";
    if (!reason.empty()) note += " (" + reason + ")";
    note += "\r";
    broadcast_to_topic(p.topic, session_id, note, res.deliveries);
    res.left_chat = true;
    res.disconnect_session = p.direct_connect;
    return res;
}

ChatActionResult ChatRoom::handle_line(uint64_t session_id, const std::string& raw_line) {
    ChatActionResult res;
    auto it = users_.find(session_id);
    if (it == users_.end()) return res;

    std::string line = trim(raw_line);
    if (line.empty()) return res;

    ChatParticipant& self = it->second;

    if (line[0] == '/') {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        std::string low = to_lower(cmd);

        if (low == "/h" || low == "/help" || low == "/?") {
            std::ostringstream oss;
            oss << "Chat Commands:\r"
                << "  /u              - List users in chat\r"
                << "  /t              - List active topics\r"
                << "  /j <topic>      - Join/switch topic (default: " << default_topic_ << ")\r"
                << "  /m <call> <msg> - Send private message\r"
                << "  /n <name>       - Set display name\r"
                << "  /q              - Leave chat\r";
            res.deliveries.push_back(ChatDelivery{session_id, oss.str()});
            return res;
        }

        if (low == "/u" || low == "/who" || low == "/users") {
            std::ostringstream oss;
            oss << "Chat Users (" << users_.size() << "):\r";
            for (const auto& [_, u] : users_) {
                oss << "  " << u.callsign;
                if (!u.name.empty()) oss << " (" << u.name << ")";
                oss << " [" << u.topic << "] on " << u.interface_id << "\r";
            }
            res.deliveries.push_back(ChatDelivery{session_id, oss.str()});
            return res;
        }

        if (low == "/t" || low == "/topics") {
            std::map<std::string, int> counts;
            for (const auto& [_, u] : users_) {
                counts[u.topic]++;
            }
            std::ostringstream oss;
            oss << "Active Topics:\r";
            for (const auto& [t, cnt] : counts) {
                oss << "  " << t << " (" << cnt << ")\r";
            }
            res.deliveries.push_back(ChatDelivery{session_id, oss.str()});
            return res;
        }

        if (low == "/j" || low == "/join" || low == "/topic") {
            std::string new_topic;
            iss >> new_topic;
            new_topic = sanitize_token(new_topic, 16);
            if (new_topic.empty()) {
                res.deliveries.push_back(ChatDelivery{session_id, "Usage: /j <topic>\r"});
                return res;
            }
            std::string old_topic = self.topic;
            if (to_lower(old_topic) == to_lower(new_topic)) {
                res.deliveries.push_back(ChatDelivery{session_id, "Already in topic " + self.topic + ".\r"});
                return res;
            }
            broadcast_to_topic(old_topic, session_id,
                               "*** " + self.callsign + " moved to " + new_topic + "\r",
                               res.deliveries);
            self.topic = new_topic;
            res.deliveries.push_back(ChatDelivery{session_id, "*** Joined topic " + self.topic + "\r"});
            broadcast_to_topic(self.topic, session_id,
                               "*** " + self.callsign + " joined " + self.topic + "\r",
                               res.deliveries);
            return res;
        }

        if (low == "/m" || low == "/msg") {
            std::string dst_call;
            iss >> dst_call;
            std::string msg_text;
            std::getline(iss, msg_text);
            msg_text = trim(msg_text);
            dst_call = to_upper(dst_call);
            if (dst_call.empty() || msg_text.empty()) {
                res.deliveries.push_back(ChatDelivery{session_id, "Usage: /m <callsign> <message>\r"});
                return res;
            }
            uint64_t target_sid = 0;
            std::string target_call;
            for (const auto& [sid, u] : users_) {
                if (u.callsign == dst_call) {
                    target_sid = sid;
                    target_call = u.callsign;
                    break;
                }
            }
            if (target_sid == 0) {
                res.deliveries.push_back(ChatDelivery{session_id, "*** User " + dst_call + " is not in chat.\r"});
                return res;
            }
            res.deliveries.push_back(ChatDelivery{target_sid, "[PM from " + self.callsign + "] " + msg_text + "\r"});
            res.deliveries.push_back(ChatDelivery{session_id, "[PM to " + target_call + "] " + msg_text + "\r"});
            return res;
        }

        if (low == "/n" || low == "/name") {
            std::string new_name;
            std::getline(iss, new_name);
            new_name = trim(new_name);
            std::string clean;
            for (unsigned char c : new_name) {
                if (c >= 0x20 && c <= 0x7E) {
                    clean.push_back(static_cast<char>(c));
                    if (clean.size() >= 16) break;
                }
            }
            self.name = clean;
            res.deliveries.push_back(ChatDelivery{
                session_id,
                self.name.empty() ? "*** Display name cleared.\r"
                                  : ("*** Display name set to " + self.name + "\r")});
            return res;
        }

        if (low == "/q" || low == "/quit" || low == "/bye" || low == "/b") {
            bool direct = self.direct_connect;
            std::string topic = self.topic;
            std::string call = self.callsign;
            users_.erase(it);

            broadcast_to_topic(topic, session_id, "*** " + call + " left chat\r", res.deliveries);
            if (direct) {
                res.deliveries.push_back(ChatDelivery{session_id, "73 de " + chat_callsign_ + "\r"});
            } else {
                res.deliveries.push_back(ChatDelivery{session_id, "*** Left chat. Returning to node.\r"});
            }
            res.left_chat = true;
            res.disconnect_session = direct;
            return res;
        }

        res.deliveries.push_back(ChatDelivery{session_id, "Unknown chat command. Type /h for help.\r"});
        return res;
    }

    // Standard message broadcast to all other participants in the same topic
    std::string prefix = "<" + self.callsign;
    if (!self.name.empty()) prefix += " " + self.name;
    prefix += "> ";
    broadcast_to_topic(self.topic, session_id, prefix + line + "\r", res.deliveries);
    return res;
}

} // namespace oyk::node
