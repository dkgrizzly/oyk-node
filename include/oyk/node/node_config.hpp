// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include "oyk/ax25/callsign.hpp"
#include "oyk/core/line_ending.hpp"
#include "oyk/node/logger.hpp"
#include "oyk/radio/radio_interface.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace oyk::node {

struct ChatConfig {
    bool enabled = true;
    uint8_t ssid = 12;                  // Default SSID 12 -> <node base callsign>-12
    ax25::Callsign callsign;            // Optional explicit callsign override
    ax25::Callsign alias{"OYKCHT", 0};  // Optional chat alias
    std::string default_topic = "Lobby";
    std::string welcome = "Welcome to OYK Packet Chat! Type /h for help, /q to leave.";
};

struct NodeConfig {
    ax25::Callsign callsign{"N0CALL", 7};
    ax25::Callsign alias{"OYKNOD", 0};
    std::string info_text = "OYK C++ Modular Amateur Radio Packet Node";
    std::string source_path; // Path of loaded config file (empty if built-in defaults)
    core::LineEnding line_ending = core::platform_default_line_ending();
    bool has_detected_line_ending = false;

    LogConfig logging;
    ChatConfig chat;
    std::string banlist_path; // Optional override from banlist= in [node]

    bool accept_incoming_default = true;
    std::size_t paclen = 236;
    uint8_t maxframe = 4;
    uint32_t t1_ms = 3000;
    uint8_t n2_retries = 5;
    uint32_t idle_timeout_sec = 600;
    uint32_t beacon_interval_sec = 0; // 0 = disabled

    std::vector<std::string> banned_callsigns; // Legacy inline ban= support if present
    std::vector<radio::RadioInterfaceConfig> interfaces;

    // Resolves banlist.cfg path: explicit `banlist=` override if set,
    // otherwise `banlist.cfg` in the same directory as `source_path` (or `./banlist.cfg`).
    std::string resolved_banlist_path() const;

    // Resolves CHAT callsign: explicit chat.callsign if valid, else <callsign.base()>-<chat.ssid>.
    ax25::Callsign resolved_chat_callsign() const;

    static NodeConfig make_default();

    // Returns default config search paths in priority order:
    //   1. ~/.oyk/node.cfg
    //   2. /etc/oyk/node.cfg
    //   3. ./node.cfg
    static std::vector<std::string> default_search_paths();

    // Returns the first existing readable config file from `search_paths`.
    static std::optional<std::string> find_default_config(
        const std::vector<std::string>& search_paths = default_search_paths());

    static bool load_from_file(const std::string& path, NodeConfig& out, std::string& error_out);
    bool save_to_file(const std::string& path = "") const;
};

} // namespace oyk::node
