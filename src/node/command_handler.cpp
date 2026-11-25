// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/node/command_handler.hpp"
#include "oyk/node/node_engine.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace oyk::node {

namespace {

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::vector<std::string> split_tokens(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) {
        tokens.push_back(tok);
    }
    return tokens;
}

std::string format_time_hms(std::chrono::system_clock::time_point tp) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
    localtime_r(&tt, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%H:%M:%S");
    return oss.str();
}

std::string format_uptime(uint64_t secs) {
    uint64_t days = secs / 86400;
    secs %= 86400;
    uint64_t hours = secs / 3600;
    secs %= 3600;
    uint64_t mins = secs / 60;
    secs %= 60;
    std::ostringstream oss;
    if (days > 0) oss << days << "d ";
    oss << std::setw(2) << std::setfill('0') << hours << ":"
        << std::setw(2) << std::setfill('0') << mins << ":"
        << std::setw(2) << std::setfill('0') << secs;
    return oss.str();
}

} // namespace

void CommandHandler::register_external_command(const std::string& name,
                                               const std::string& description,
                                               ExternalCommandFn handler) {
    external_commands_[to_upper(name)] = ExternalCommand{description, std::move(handler)};
}

CommandResult CommandHandler::execute(NodeEngine& engine,
                                      uint64_t session_id,
                                      const std::string& raw_line) const {
    CommandResult res;
    auto tokens = split_tokens(raw_line);
    if (tokens.empty()) {
        return res;
    }

    const std::string cmd = to_upper(tokens[0]);

    if (cmd == "HELP" || cmd == "H" || cmd == "?") {
        std::ostringstream oss;
        oss << "Commands:\r"
            << "  INFO     (I)  - Node information\r"
            << "  PORTS    (P)  - List radio interfaces\r"
            << "  USERS    (U)  - List active connections\r"
            << "  MHEARD   (MH) - Recently heard callsigns\r"
            << "  BBS           - Enter packet Bulletin Board System\r"
            << "  CHAT     (CH) - Join packet chat conference\r"
            << "  CONNECT  (C)  - Connect out: C <port> <callsign>\r"
            << "  BYE      (B)  - Disconnect from node\r"
            << "  HELP     (?)  - Show this help\r";
        if (!external_commands_.empty()) {
            oss << "Applications:\r";
            for (const auto& [name, ext] : external_commands_) {
                oss << "  " << std::left << std::setw(8) << name << " - " << ext.description << "\r";
            }
        }
        res.response_text = oss.str();
        return res;
    }

    if (cmd == "INFO" || cmd == "I") {
        auto snap = engine.snapshot();
        std::ostringstream oss;
        oss << snap.node_callsign;
        if (!snap.node_alias.empty()) {
            oss << " (" << snap.node_alias << ")";
        }
        oss << " - " << snap.info_text << "\r"
            << "Uptime: " << format_uptime(snap.uptime_seconds)
            << " | Interfaces: " << snap.interfaces.size()
            << " | Active Connections: " << snap.total_active_connections << "\r";
        res.response_text = oss.str();
        return res;
    }

    if (cmd == "PORTS" || cmd == "P") {
        auto snap = engine.snapshot();
        std::ostringstream oss;
        oss << "Ports:\r";
        for (std::size_t i = 0; i < snap.interfaces.size(); ++i) {
            const auto& iface = snap.interfaces[i];
            oss << "  " << (i + 1) << ") "
                << std::left << std::setw(10) << iface.id
                << " [" << iface.type << "] "
                << iface.status
                << " | Conn: " << iface.active_connections
                << " | " << iface.description << "\r";
        }
        res.response_text = oss.str();
        return res;
    }

    if (cmd == "USERS" || cmd == "U") {
        auto snap = engine.snapshot();
        std::ostringstream oss;
        oss << "Active Connections (" << snap.active_sessions.size() << "):\r";
        for (const auto& s : snap.active_sessions) {
            oss << "  #" << s.id << " "
                << std::left << std::setw(10) << s.remote_call
                << " on " << std::setw(8) << s.interface_id
                << " [" << Session::state_name(s.state) << "]";
            if (s.bridged_session_id != 0) {
                oss << " <-> #" << s.bridged_session_id;
            }
            oss << "\r";
        }
        res.response_text = oss.str();
        return res;
    }

    if (cmd == "MHEARD" || cmd == "MH") {
        auto records = engine.mheard(15);
        std::ostringstream oss;
        if (records.empty()) {
            oss << "No stations heard yet.\r";
        } else {
            oss << "Callsign   Port       Pkts   Last Heard\r";
            for (const auto& r : records) {
                oss << std::left << std::setw(10) << r.callsign << " "
                    << std::setw(10) << r.interface_id << " "
                    << std::setw(6) << r.frame_count << " "
                    << format_time_hms(r.last_heard) << "\r";
            }
        }
        res.response_text = oss.str();
        return res;
    }

    if (cmd == "BBS") {
        res.enter_bbs = true;
        return res;
    }

    if (cmd == "CHAT" || cmd == "CH") {
        res.enter_chat = true;
        return res;
    }

    if (cmd == "CONNECT" || cmd == "C") {
        if (tokens.size() < 3) {
            res.response_text = "Usage: CONNECT <port> <callsign>\r";
            return res;
        }
        res.connect_request = true;
        res.connect_port = tokens[1];
        res.connect_callsign = to_upper(tokens[2]);
        return res;
    }

    if (cmd == "BYE" || cmd == "B" || cmd == "QUIT" || cmd == "Q") {
        res.response_text = "73 de " + engine.snapshot().node_callsign + "\r";
        res.disconnect = true;
        return res;
    }

    if (auto it = external_commands_.find(cmd); it != external_commands_.end()) {
        std::vector<std::string> args(tokens.begin() + 1, tokens.end());
        return it->second.handler(session_id, args);
    }

    res.response_text = "Unknown command '" + tokens[0] + "'. Type HELP for commands.\r";
    return res;
}

} // namespace oyk::node
