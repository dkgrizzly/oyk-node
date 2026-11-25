// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace oyk::node {

class NodeEngine;

struct CommandResult {
    std::string response_text; // Text sent back to remote user (uses \r line endings)
    bool disconnect = false;   // True if user issued BYE/QUIT
    bool enter_chat = false;   // True if user issued CHAT
    bool enter_bbs = false;    // True if user issued BBS
    bool connect_request = false;
    std::string connect_port;
    std::string connect_callsign;
};

// Processes remote packet node commands from connected stations.
// Extensible via register_external_command() for future standalone BBS/Chat executables.
class CommandHandler {
public:
    using ExternalCommandFn = std::function<CommandResult(
        uint64_t session_id,
        const std::vector<std::string>& args)>;

    struct ExternalCommand {
        std::string description;
        ExternalCommandFn handler;
    };

    CommandResult execute(NodeEngine& engine,
                          uint64_t session_id,
                          const std::string& raw_line) const;

    // Extension hook for future BBS / Chat executables
    void register_external_command(const std::string& name,
                                   const std::string& description,
                                   ExternalCommandFn handler);

private:
    std::map<std::string, ExternalCommand> external_commands_;
};

} // namespace oyk::node
