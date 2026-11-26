// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/node/node_config.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <pwd.h>
#include <sstream>
#include <unistd.h>

namespace oyk::node {

namespace {

std::string resolve_home_dir() {
    if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
        return std::string(home);
    }
    if (passwd* pw = getpwuid(getuid()); pw && pw->pw_dir && pw->pw_dir[0] != '\0') {
        return std::string(pw->pw_dir);
    }
    return {};
}

std::string expand_tilde(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    if (path.size() == 1 || path[1] == '/') {
        std::string home = resolve_home_dir();
        if (!home.empty()) {
            if (home.back() == '/' && path.size() > 1) {
                return home + path.substr(2);
            }
            return home + path.substr(1);
        }
    }
    return path;
}

std::string trim(std::string_view sv) {
    std::size_t start = 0;
    while (start < sv.size() && std::isspace(static_cast<unsigned char>(sv[start]))) {
        ++start;
    }
    std::size_t end = sv.size();
    while (end > start && std::isspace(static_cast<unsigned char>(sv[end - 1]))) {
        --end;
    }
    return std::string(sv.substr(start, end - start));
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool parse_bool(const std::string& v, bool fallback) {
    auto lv = to_lower(v);
    if (lv == "1" || lv == "true" || lv == "yes" || lv == "on") return true;
    if (lv == "0" || lv == "false" || lv == "no" || lv == "off") return false;
    return fallback;
}

} // namespace

std::string NodeConfig::resolved_banlist_path() const {
    namespace fs = std::filesystem;
    if (!banlist_path.empty()) {
        std::string expanded = expand_tilde(banlist_path);
        fs::path p(expanded);
        if (p.is_absolute() || source_path.empty()) {
            return p.string();
        }
        fs::path cfg_dir = fs::path(source_path).parent_path();
        if (cfg_dir.empty()) {
            return p.string();
        }
        return (cfg_dir / p).string();
    }

    if (!source_path.empty()) {
        fs::path cfg_dir = fs::path(source_path).parent_path();
        if (!cfg_dir.empty()) {
            return (cfg_dir / "banlist.cfg").string();
        }
    }
    return "./banlist.cfg";
}

ax25::Callsign NodeConfig::resolved_bbs_callsign() const {
    if (bbs.callsign.valid()) {
        return bbs.callsign;
    }
    return ax25::Callsign(callsign.base(), static_cast<uint8_t>(bbs.ssid & 0x0Fu));
}

std::string NodeConfig::resolved_bbs_mail_path() const {
    namespace fs = std::filesystem;
    std::string raw = bbs.mail_path.empty() ? "mail/" : expand_tilde(bbs.mail_path);
    fs::path p(raw);
    if (p.is_absolute() || source_path.empty()) {
        return p.string();
    }
    fs::path cfg_dir = fs::path(source_path).parent_path();
    if (cfg_dir.empty()) {
        return p.string();
    }
    return (cfg_dir / p).string();
}

std::string NodeConfig::resolved_bbs_files_path() const {
    namespace fs = std::filesystem;
    std::string raw = bbs.files_path.empty() ? "files/" : expand_tilde(bbs.files_path);
    fs::path p(raw);
    if (p.is_absolute() || source_path.empty()) {
        return p.string();
    }
    fs::path cfg_dir = fs::path(source_path).parent_path();
    if (cfg_dir.empty()) {
        return p.string();
    }
    return (cfg_dir / p).string();
}

ax25::Callsign NodeConfig::resolved_chat_callsign() const {
    if (chat.callsign.valid()) {
        return chat.callsign;
    }
    return ax25::Callsign(callsign.base(), static_cast<uint8_t>(chat.ssid & 0x0Fu));
}

std::optional<ax25::Callsign> NodeConfig::parse_callsign_with_default_ssid(
    std::string_view text, uint8_t default_ssid) {
    std::string trimmed = trim(text);
    auto parsed = ax25::Callsign::parse(trimmed);
    if (!parsed) {
        return std::nullopt;
    }
    if (trimmed.find('-') == std::string::npos) {
        return ax25::Callsign(parsed->base(), static_cast<uint8_t>(default_ssid & 0x0Fu));
    }
    return parsed;
}

NodeConfig NodeConfig::make_default() {
    NodeConfig cfg;
    radio::RadioInterfaceConfig axudp;
    axudp.id = "axudp0";
    axudp.type = "axudp";
    axudp.description = "Primary AXUDP Interface";
    axudp.params["bind_host"] = "0.0.0.0";
    axudp.params["bind_port"] = "10093";
    axudp.params["auto_learn"] = "true";
    cfg.interfaces.push_back(std::move(axudp));
    return cfg;
}

std::vector<std::string> NodeConfig::default_search_paths() {
    std::vector<std::string> paths;
    std::string home = resolve_home_dir();
    if (!home.empty()) {
        if (home.back() == '/') {
            paths.push_back(home + ".oyk/node.cfg");
        } else {
            paths.push_back(home + "/.oyk/node.cfg");
        }
    }
    paths.push_back("/etc/oyk/node.cfg");
    paths.push_back("./node.cfg");
    return paths;
}

std::optional<std::string> NodeConfig::find_default_config(
    const std::vector<std::string>& search_paths) {
    namespace fs = std::filesystem;
    for (const auto& path : search_paths) {
        std::error_code ec;
        if (!fs::is_regular_file(path, ec) || ec) {
            continue;
        }
        std::ifstream probe(path);
        if (probe.is_open()) {
            return path;
        }
    }
    return std::nullopt;
}

bool NodeConfig::load_from_file(const std::string& path, NodeConfig& out, std::string& error_out) {
    std::string raw_bytes;
    if (!core::read_file_binary(path, raw_bytes)) {
        error_out = "Unable to open config file: " + path;
        return false;
    }

    NodeConfig cfg;
    cfg.source_path = path;
    core::LineEndingInfo eol_info = core::inspect_line_endings(raw_bytes);
    cfg.line_ending = eol_info.first;
    cfg.has_detected_line_ending = eol_info.found_any;

    std::string current_section;
    radio::RadioInterfaceConfig* current_iface = nullptr;
    radio::PeerConfig* current_peer = nullptr;
    bool node_call_explicit_ssid = false;
    bool node_ssid_set = false;
    uint8_t node_ssid_val = DEFAULT_NODE_SSID;
    bool bbs_call_explicit_ssid = false;
    bool chat_call_explicit_ssid = false;

    auto lines = core::split_lines_any_eol(raw_bytes);
    int line_num = 0;
    for (std::string raw_line : lines) {
        ++line_num;
        // Strip inline comments starting with '#' or ';'
        auto hash_pos = raw_line.find('#');
        if (hash_pos != std::string::npos) {
            raw_line = raw_line.substr(0, hash_pos);
        }
        auto semi_pos = raw_line.find(';');
        if (semi_pos != std::string::npos) {
            raw_line = raw_line.substr(0, semi_pos);
        }

        std::string line = trim(raw_line);
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']') {
            std::string sec = trim(line.substr(1, line.size() - 2));
            std::string lower_sec = to_lower(sec);
            if (lower_sec == "node") {
                current_section = "node";
                current_iface = nullptr;
                current_peer = nullptr;
            } else if (lower_sec == "logfiles") {
                current_section = "logfiles";
                current_iface = nullptr;
                current_peer = nullptr;
            } else if (lower_sec == "bbs") {
                current_section = "bbs";
                current_iface = nullptr;
                current_peer = nullptr;
            } else if (lower_sec == "chat") {
                current_section = "chat";
                current_iface = nullptr;
                current_peer = nullptr;
            } else if (lower_sec == "rig" || lower_sec == "radio") {
                current_section = "rig";
                current_iface = nullptr;
                current_peer = nullptr;
            } else if (lower_sec == "printer") {
                current_section = "printer";
                current_iface = nullptr;
                current_peer = nullptr;
            } else if (lower_sec.rfind("interface", 0) == 0) {
                std::string rest = trim(sec.substr(9));
                // Allow "[interface axudp0]" or "[interface:axudp0]"
                if (!rest.empty() && rest.front() == ':') {
                    rest = trim(rest.substr(1));
                }
                if (rest.empty()) {
                    error_out = "Config line " + std::to_string(line_num) +
                                ": interface section requires an ID (e.g. [interface axudp0])";
                    return false;
                }
                current_section = "interface";
                radio::RadioInterfaceConfig iface;
                iface.id = rest;
                cfg.interfaces.push_back(std::move(iface));
                current_iface = &cfg.interfaces.back();
                current_peer = nullptr;
            } else if (lower_sec.rfind("peer", 0) == 0 &&
                       (lower_sec.size() == 4 || lower_sec[4] == ':' ||
                        std::isspace(static_cast<unsigned char>(lower_sec[4])))) {
                std::string rest = trim(sec.substr(4));
                if (!rest.empty() && rest.front() == ':') {
                    rest = trim(rest.substr(1));
                }
                if (rest.empty()) {
                    error_out = "Config line " + std::to_string(line_num) +
                                ": peer section requires an ID (e.g. [peer remote0])";
                    return false;
                }
                current_section = "peer";
                radio::PeerConfig peer;
                peer.id = rest;
                cfg.peers.push_back(std::move(peer));
                current_peer = &cfg.peers.back();
                current_iface = nullptr;
            } else {
                error_out = "Config line " + std::to_string(line_num) +
                            ": unknown section [" + sec + "]";
                return false;
            }
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) {
            error_out = "Config line " + std::to_string(line_num) + ": expected key = value";
            return false;
        }
        std::string raw_key = trim(line.substr(0, eq));
        std::string key = to_lower(raw_key);
        std::string val = trim(line.substr(eq + 1));

        if (current_section == "node" || current_section.empty()) {
            if (key == "callsign") {
                const uint8_t fallback_ssid = node_ssid_set ? node_ssid_val : DEFAULT_NODE_SSID;
                auto parsed = parse_callsign_with_default_ssid(val, fallback_ssid);
                if (!parsed) {
                    error_out = "Config line " + std::to_string(line_num) +
                                ": invalid callsign '" + val + "'";
                    return false;
                }
                cfg.callsign = *parsed;
                node_call_explicit_ssid = (val.find('-') != std::string::npos);
            } else if (key == "ssid") {
                node_ssid_val = static_cast<uint8_t>(std::clamp(std::stoi(val), 0, 15));
                node_ssid_set = true;
                if (!node_call_explicit_ssid) {
                    cfg.callsign = ax25::Callsign(cfg.callsign.base(), node_ssid_val);
                }
            } else if (key == "alias") {
                if (val.empty()) {
                    cfg.alias = ax25::Callsign{};
                } else {
                    auto parsed = ax25::Callsign::parse(val);
                    if (!parsed) {
                        error_out = "Config line " + std::to_string(line_num) +
                                    ": invalid alias '" + val + "'";
                        return false;
                    }
                    cfg.alias = *parsed;
                }
            } else if (key == "welcome" || key == "welcome_text") {
                cfg.welcome_text = val;
            } else if (key == "new_user_welcome" || key == "newuserwelcome") {
                cfg.new_user_welcome = val;
            } else if (key == "learned_peer_policy") {
                cfg.learned_peer_policy = parse_learned_peer_policy(val, radio::LearnedPeerPolicy::Inbound);
            } else if (key == "info" || key == "info_text") {
                cfg.info_text = val;
            } else if (key == "logpath") {
                cfg.logging.log_path = expand_tilde(val);
            } else if (key == "consolelevel") {
                uint32_t mask = 0;
                if (!parse_log_mask(val, mask)) {
                    error_out = "Config line " + std::to_string(line_num) +
                                ": invalid consolelevel '" + val + "'";
                    return false;
                }
                cfg.logging.console_level = mask;
            } else if (key == "log_rotate_daily") {
                cfg.logging.rotate_daily = parse_bool(val, true);
            } else if (key == "banlist") {
                cfg.banlist_path = val;
            } else if (key == "accept_incoming") {
                cfg.accept_incoming_default = parse_bool(val, true);
            } else if (key == "allow_flash_logging" || key == "allow_logging_to_flash") {
                cfg.allow_flash_logging = parse_bool(val, false);
            } else if (key == "paclen") {
                cfg.paclen = static_cast<std::size_t>(std::clamp(std::stoi(val), 32, 256));
            } else if (key == "maxframe") {
                cfg.maxframe = static_cast<uint8_t>(std::clamp(std::stoi(val), 1, 7));
            } else if (key == "t1_ms") {
                cfg.t1_ms = static_cast<uint32_t>(std::max(250, std::stoi(val)));
            } else if (key == "n2_retries") {
                cfg.n2_retries = static_cast<uint8_t>(std::clamp(std::stoi(val), 1, 20));
            } else if (key == "idle_timeout_sec") {
                cfg.idle_timeout_sec = static_cast<uint32_t>(std::max(0, std::stoi(val)));
            } else if (key == "beacon_interval_sec") {
                cfg.beacon_interval_sec = static_cast<uint32_t>(std::max(0, std::stoi(val)));
            } else if (key == "ban") {
                if (!val.empty()) {
                    cfg.banned_callsigns.push_back(val);
                }
            }
        } else if (current_section == "logfiles") {
            uint32_t mask = 0;
            if (!parse_log_mask(val, mask)) {
                error_out = "Config line " + std::to_string(line_num) +
                            ": invalid log level mask '" + val + "' for file '" + raw_key + "'";
                return false;
            }
            cfg.logging.log_files[raw_key] = mask;
        } else if (current_section == "bbs") {
            if (key == "enabled") {
                cfg.bbs.enabled = parse_bool(val, true);
            } else if (key == "ssid") {
                cfg.bbs.ssid = static_cast<uint8_t>(std::clamp(std::stoi(val), 0, 15));
                if (cfg.bbs.callsign.valid() && !bbs_call_explicit_ssid) {
                    cfg.bbs.callsign = ax25::Callsign(cfg.bbs.callsign.base(), cfg.bbs.ssid);
                }
            } else if (key == "callsign") {
                if (val.empty()) {
                    cfg.bbs.callsign = ax25::Callsign{};
                    bbs_call_explicit_ssid = false;
                } else {
                    auto parsed = parse_callsign_with_default_ssid(val, cfg.bbs.ssid);
                    if (!parsed) {
                        error_out = "Config line " + std::to_string(line_num) +
                                    ": invalid bbs callsign '" + val + "'";
                        return false;
                    }
                    cfg.bbs.callsign = *parsed;
                    bbs_call_explicit_ssid = (val.find('-') != std::string::npos);
                }
            } else if (key == "alias") {
                if (val.empty()) {
                    cfg.bbs.alias = ax25::Callsign{};
                } else {
                    auto parsed = ax25::Callsign::parse(val);
                    if (!parsed) {
                        error_out = "Config line " + std::to_string(line_num) +
                                    ": invalid bbs alias '" + val + "'";
                        return false;
                    }
                    cfg.bbs.alias = *parsed;
                }
            } else if (key == "mailpath" || key == "mail_path") {
                if (!val.empty()) cfg.bbs.mail_path = val;
            } else if (key == "files_enabled" || key == "filesenabled") {
                cfg.bbs.files_enabled = parse_bool(val, true);
            } else if (key == "files_case_insensitive" || key == "filescaseinsensitive") {
                cfg.bbs.files_case_insensitive = parse_bool(val, true);
            } else if (key == "filespath" || key == "files_path") {
                if (!val.empty()) cfg.bbs.files_path = val;
            } else if (key == "welcome") {
                cfg.bbs.welcome = val;
            } else if (key == "max_msg_bytes") {
                cfg.bbs.max_msg_bytes = static_cast<std::size_t>(std::clamp(std::stoi(val), 256, 65536));
            }
        } else if (current_section == "chat") {
            if (key == "enabled") {
                cfg.chat.enabled = parse_bool(val, true);
            } else if (key == "ssid") {
                cfg.chat.ssid = static_cast<uint8_t>(std::clamp(std::stoi(val), 0, 15));
                if (cfg.chat.callsign.valid() && !chat_call_explicit_ssid) {
                    cfg.chat.callsign = ax25::Callsign(cfg.chat.callsign.base(), cfg.chat.ssid);
                }
            } else if (key == "callsign") {
                if (val.empty()) {
                    cfg.chat.callsign = ax25::Callsign{};
                    chat_call_explicit_ssid = false;
                } else {
                    auto parsed = parse_callsign_with_default_ssid(val, cfg.chat.ssid);
                    if (!parsed) {
                        error_out = "Config line " + std::to_string(line_num) +
                                    ": invalid chat callsign '" + val + "'";
                        return false;
                    }
                    cfg.chat.callsign = *parsed;
                    chat_call_explicit_ssid = (val.find('-') != std::string::npos);
                }
            } else if (key == "alias") {
                if (val.empty()) {
                    cfg.chat.alias = ax25::Callsign{};
                } else {
                    auto parsed = ax25::Callsign::parse(val);
                    if (!parsed) {
                        error_out = "Config line " + std::to_string(line_num) +
                                    ": invalid chat alias '" + val + "'";
                        return false;
                    }
                    cfg.chat.alias = *parsed;
                }
            } else if (key == "topic" || key == "default_topic") {
                if (!val.empty()) cfg.chat.default_topic = val;
            } else if (key == "welcome") {
                cfg.chat.welcome = val;
            }
        } else if (current_section == "rig") {
            if (key == "enabled") {
                cfg.rig.enabled = parse_bool(val, false);
            } else if (key == "protocol" || key == "type") {
                cfg.rig.protocol = radio::rig_protocol_from_string(val);
            } else if (key == "host" || key == "address") {
                if (!val.empty()) cfg.rig.host = val;
            } else if (key == "port") {
                int pv = std::clamp(std::stoi(val), 0, 65535);
                cfg.rig.port = static_cast<uint16_t>(pv);
            } else if (key == "poll_interval_sec" || key == "keepalive_sec") {
                cfg.rig.poll_interval_sec = static_cast<uint32_t>(std::max(0, std::stoi(val)));
            } else if (key == "notify") {
                cfg.rig.notify = parse_bool(val, true);
            }
        } else if (current_section == "printer") {
            if (key == "enabled") {
                cfg.logging.printer.enabled = parse_bool(val, false);
            } else if (key == "device" || key == "port") {
                cfg.logging.printer.device = expand_tilde(val);
            } else if (key == "protocol" || key == "type") {
                cfg.logging.printer.protocol = val.empty() ? "generic" : to_lower(val);
            } else if (key == "baud") {
                cfg.logging.printer.baud = static_cast<uint32_t>(std::max(300, std::stoi(val)));
            } else if (key == "level" || key == "mask") {
                uint32_t mask = 0;
                if (!parse_log_mask(val, mask)) {
                    error_out = "Config line " + std::to_string(line_num) +
                                ": invalid printer level '" + val + "'";
                    return false;
                }
                cfg.logging.printer.level = mask;
            } else if (key == "condensed") {
                cfg.logging.printer.condensed = parse_bool(val, false);
            }
        } else if (current_section == "interface" && current_iface != nullptr) {
            if (key == "type") {
                current_iface->type = to_lower(val);
                if (current_iface->type == "telnet") current_iface->allow_outbound = false;
            } else if (key == "description") {
                current_iface->description = val;
            } else if (key == "allow_inbound" || key == "inbound") {
                current_iface->allow_inbound = parse_bool(val, true);
            } else if (key == "allow_outbound" || key == "outbound") {
                current_iface->allow_outbound = (current_iface->type == "telnet") ? false : parse_bool(val, true);
            } else if (key == "peer") {
                if (!val.empty()) {
                    current_iface->peers.push_back(val);
                }
            } else {
                current_iface->params[key] = val;
            }
        } else if (current_section == "peer" && current_peer != nullptr) {
            if (key == "callsign" || key == "call") {
                if (val.empty() || val == "*") {
                    current_peer->callsign.clear();
                } else if (auto parsed = ax25::Callsign::parse(val)) {
                    current_peer->callsign = parsed->str();
                } else {
                    std::string up = val;
                    std::transform(up.begin(), up.end(), up.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                    current_peer->callsign = up;
                }
            } else if (key == "description" || key == "desc") {
                current_peer->description = val;
            } else if (key == "interface" || key == "interface_id" || key == "iface") {
                current_peer->interface_id = val;
            } else if (key == "host" || key == "address") {
                auto colon = val.rfind(':');
                if (colon != std::string::npos && val.find(':') == colon && colon > 0 && colon + 1 < val.size()) {
                    current_peer->host = val.substr(0, colon);
                    if (current_peer->port == 0) {
                        try {
                            int pv = std::clamp(std::stoi(val.substr(colon + 1)), 0, 65535);
                            current_peer->port = static_cast<uint16_t>(pv);
                        } catch (...) {
                        }
                    }
                } else {
                    current_peer->host = val;
                }
            } else if (key == "port") {
                try {
                    int pv = std::clamp(std::stoi(val), 0, 65535);
                    current_peer->port = static_cast<uint16_t>(pv);
                } catch (...) {
                }
            } else if (key == "allow_inbound" || key == "inbound") {
                current_peer->allow_inbound = parse_bool(val, true);
            } else if (key == "allow_outbound" || key == "outbound") {
                current_peer->allow_outbound = parse_bool(val, true);
            }
        }
    }

    if (cfg.rig.port == 0 && cfg.rig.protocol != radio::RigProtocol::None) {
        cfg.rig.port = radio::rig_default_port(cfg.rig.protocol);
    }

    if (cfg.interfaces.empty()) {
        cfg.interfaces = make_default().interfaces;
    }

    for (auto& ic : cfg.interfaces) {
        if (ic.type == "telnet") {
            ic.allow_outbound = false;
        }
    }

    if (!radio::validate_iface_and_peer_addresses(cfg.interfaces, cfg.peers, error_out)) {
        return false;
    }

    // Migrate legacy inline peer = lines inside [interface <id>] into [peer <id>] entries
    for (auto& ic : cfg.interfaces) {
        uint16_t def_port = 0;
        auto pit = ic.params.find("bind_port");
        if (pit == ic.params.end()) pit = ic.params.find("port");
        if (pit != ic.params.end() && !pit->second.empty()) {
            try {
                int pv = std::clamp(std::stoi(pit->second), 0, 65535);
                def_port = static_cast<uint16_t>(pv);
            } catch (...) {
            }
        }
        if (def_port == 0 && ic.type == "axudp") def_port = 10093;
        for (const auto& raw_peer : ic.peers) {
            radio::PeerConfig migrated;
            if (parse_legacy_peer_spec(raw_peer, ic.id, def_port, cfg.peers, migrated)) {
                bool dup = false;
                for (const auto& ex : cfg.peers) {
                    if (to_lower(ex.interface_id) == to_lower(migrated.interface_id) &&
                        to_lower(ex.callsign) == to_lower(migrated.callsign) &&
                        ex.host == migrated.host && ex.port == migrated.port) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) {
                    cfg.peers.push_back(std::move(migrated));
                }
            }
        }
        ic.peers.clear();
    }

    out = std::move(cfg);
    return true;
}

bool NodeConfig::save_to_file(const std::string& path) const {
    const std::string target = path.empty() ? source_path : path;
    if (target.empty()) return false;

    std::vector<std::string> lines;
    lines.push_back("[node]");
    lines.push_back("callsign = " + callsign.str());
    if (alias.valid()) {
        lines.push_back("alias = " + alias.str());
    }
    lines.push_back("welcome = " + welcome_text);
    lines.push_back("info = " + info_text);
    lines.push_back("accept_incoming = " + std::string(accept_incoming_default ? "true" : "false"));
    lines.push_back("allow_flash_logging = " + std::string(allow_flash_logging ? "true" : "false"));
    lines.push_back("learned_peer_policy = " + learned_peer_policy_to_string(learned_peer_policy));
    if (!new_user_welcome.empty()) {
        lines.push_back("new_user_welcome = " + new_user_welcome);
    }
    lines.push_back("paclen = " + std::to_string(paclen));
    lines.push_back("maxframe = " + std::to_string(static_cast<int>(maxframe)));
    lines.push_back("t1_ms = " + std::to_string(t1_ms));
    lines.push_back("n2_retries = " + std::to_string(static_cast<int>(n2_retries)));
    lines.push_back("idle_timeout_sec = " + std::to_string(idle_timeout_sec));
    lines.push_back("beacon_interval_sec = " + std::to_string(beacon_interval_sec));
    lines.push_back("logpath = " + logging.log_path);
    {
        std::ostringstream oss;
        oss << "consolelevel = 0x" << std::hex << logging.console_level;
        lines.push_back(oss.str());
    }
    lines.push_back("log_rotate_daily = " + std::string(logging.rotate_daily ? "true" : "false"));
    if (!banlist_path.empty()) {
        lines.push_back("banlist = " + banlist_path);
    }

    if (!logging.log_files.empty()) {
        lines.push_back("");
        lines.push_back("[logfiles]");
        for (const auto& [fname, mask] : logging.log_files) {
            std::ostringstream oss;
            oss << fname << " = 0x" << std::hex << mask;
            lines.push_back(oss.str());
        }
    }

    lines.push_back("");
    lines.push_back("[bbs]");
    lines.push_back("enabled = " + std::string(bbs.enabled ? "true" : "false"));
    lines.push_back("ssid = " + std::to_string(static_cast<int>(bbs.ssid)));
    if (bbs.callsign.valid()) {
        lines.push_back("callsign = " + bbs.callsign.str());
    }
    if (bbs.alias.valid()) {
        lines.push_back("alias = " + bbs.alias.str());
    }
    lines.push_back("mailpath = " + bbs.mail_path);
    lines.push_back("files_enabled = " + std::string(bbs.files_enabled ? "true" : "false"));
    lines.push_back("files_case_insensitive = " + std::string(bbs.files_case_insensitive ? "true" : "false"));
    lines.push_back("filespath = " + bbs.files_path);
    lines.push_back("welcome = " + bbs.welcome);
    lines.push_back("max_msg_bytes = " + std::to_string(bbs.max_msg_bytes));

    lines.push_back("");
    lines.push_back("[chat]");
    lines.push_back("enabled = " + std::string(chat.enabled ? "true" : "false"));
    lines.push_back("ssid = " + std::to_string(static_cast<int>(chat.ssid)));
    if (chat.callsign.valid()) {
        lines.push_back("callsign = " + chat.callsign.str());
    }
    if (chat.alias.valid()) {
        lines.push_back("alias = " + chat.alias.str());
    }
    lines.push_back("topic = " + chat.default_topic);
    lines.push_back("welcome = " + chat.welcome);

    lines.push_back("");
    lines.push_back("[rig]");
    lines.push_back("enabled = " + std::string(rig.enabled ? "true" : "false"));
    lines.push_back("protocol = " + radio::rig_protocol_to_string(rig.protocol));
    lines.push_back("host = " + (rig.host.empty() ? std::string("127.0.0.1") : rig.host));
    lines.push_back("port = " + std::to_string(rig.port ? rig.port : radio::rig_default_port(rig.protocol)));
    lines.push_back("poll_interval_sec = " + std::to_string(rig.poll_interval_sec));
    lines.push_back("notify = " + std::string(rig.notify ? "true" : "false"));

    lines.push_back("");
    lines.push_back("[printer]");
    lines.push_back("enabled = " + std::string(logging.printer.enabled ? "true" : "false"));
    lines.push_back("device = " + logging.printer.device);
    lines.push_back("protocol = " + (logging.printer.protocol.empty() ? std::string("generic") : logging.printer.protocol));
    lines.push_back("baud = " + std::to_string(logging.printer.baud));
    {
        std::ostringstream oss;
        oss << "level = 0x" << std::hex << logging.printer.level;
        lines.push_back(oss.str());
    }
    lines.push_back("condensed = " + std::string(logging.printer.condensed ? "true" : "false"));

    for (const auto& iface : interfaces) {
        lines.push_back("");
        lines.push_back("[interface " + iface.id + "]");
        lines.push_back("type = " + iface.type);
        if (!iface.description.empty()) {
            lines.push_back("description = " + iface.description);
        }
        lines.push_back(std::string("allow_inbound = ") + (iface.allow_inbound ? "true" : "false"));
        const bool out_allowed = (iface.type == "telnet") ? false : iface.allow_outbound;
        lines.push_back(std::string("allow_outbound = ") + (out_allowed ? "true" : "false"));
        for (const auto& [k, v] : iface.params) {
            if (k == "allow_inbound" || k == "allow_outbound" || k == "inbound" || k == "outbound") continue;
            lines.push_back(k + " = " + v);
        }
    }

    for (const auto& peer : peers) {
        if (peer.auto_learned) continue;
        lines.push_back("");
        lines.push_back("[peer " + peer.id + "]");
        if (!peer.callsign.empty()) {
            lines.push_back("callsign = " + peer.callsign);
        }
        if (!peer.description.empty()) {
            lines.push_back("description = " + peer.description);
        }
        if (!peer.interface_id.empty()) {
            lines.push_back("interface = " + peer.interface_id);
        }
        if (!peer.host.empty()) {
            lines.push_back("host = " + peer.host);
        }
        if (peer.port > 0) {
            lines.push_back("port = " + std::to_string(peer.port));
        }
        lines.push_back("allow_inbound = " + std::string(peer.allow_inbound ? "true" : "false"));
        lines.push_back("allow_outbound = " + std::string(peer.allow_outbound ? "true" : "false"));
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p(target);
    if (p.has_parent_path() && !p.parent_path().empty()) {
        fs::create_directories(p.parent_path(), ec);
        if (ec) {
            return false;
        }
    }

    return core::write_lines_binary(target, lines, line_ending);
}

std::string derive_default_iface_id(const std::vector<radio::RadioInterfaceConfig>& existing,
                                    const std::string& iface_type) {
    const std::string lt = to_lower(trim(iface_type));
    std::string prefix = "iface";
    if (lt == "axudp") prefix = "axudp";
    else if (lt == "axip") prefix = "axip";
    else if (lt == "kiss-serial") prefix = "kiss";
    else if (lt == "kiss-tcp") prefix = "kisstcp";
    else if (lt == "telnet") prefix = "telnet";
    else if (lt == "ppp") prefix = "ppp";
    else if (lt == "usb-cdc-net") prefix = "usbnet";
    else if (lt == "usb-cdc-serial") prefix = "usbser";

    for (int n = 0; n < 1000; ++n) {
        const std::string cand = prefix + std::to_string(n);
        const std::string cand_l = to_lower(cand);
        bool used = false;
        for (const auto& ic : existing) {
            if (to_lower(ic.id) == cand_l) {
                used = true;
                break;
            }
        }
        if (!used) {
            return cand;
        }
    }
    return prefix;
}

std::string derive_default_peer_id(const std::vector<radio::PeerConfig>& existing,
                                   const std::string& callsign) {
    std::string base;
    for (unsigned char ch : callsign) {
        if (std::isalnum(ch) || ch == '-' || ch == '_') {
            base.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    if (base.empty()) base = "peer";
    if (base.size() > 12) base.resize(12);

    auto id_used = [&](const std::string& cand) {
        const std::string cl = to_lower(cand);
        for (const auto& p : existing) {
            if (to_lower(p.id) == cl) return true;
        }
        return false;
    };

    if (!id_used(base)) return base;
    for (int n = 1; n < 1000; ++n) {
        std::string suf = "-" + std::to_string(n);
        std::string cand = base;
        if (cand.size() + suf.size() > 15) {
            cand.resize(15 - suf.size());
        }
        cand += suf;
        if (!id_used(cand)) return cand;
    }
    return base;
}

radio::LearnedPeerPolicy parse_learned_peer_policy(
    std::string_view text,
    radio::LearnedPeerPolicy fallback) {
    const std::string s = to_lower(trim(text));
    if (s.empty() || s == "inherit" || s == "default") return radio::LearnedPeerPolicy::Inherit;
    if (s == "none" || s == "deny" || s == "off") return radio::LearnedPeerPolicy::None;
    if (s == "inbound" || s == "in") return radio::LearnedPeerPolicy::Inbound;
    if (s == "outbound" || s == "out") return radio::LearnedPeerPolicy::Outbound;
    if (s == "both" || s == "all" || s == "in+out") return radio::LearnedPeerPolicy::Both;
    return fallback;
}

std::string learned_peer_policy_to_string(radio::LearnedPeerPolicy policy) {
    switch (policy) {
        case radio::LearnedPeerPolicy::Inherit:  return "inherit";
        case radio::LearnedPeerPolicy::None:     return "none";
        case radio::LearnedPeerPolicy::Inbound:  return "inbound";
        case radio::LearnedPeerPolicy::Outbound: return "outbound";
        case radio::LearnedPeerPolicy::Both:     return "both";
    }
    return "inbound";
}

bool parse_legacy_peer_spec(const std::string& raw,
                            const std::string& iface_id,
                            uint16_t default_port,
                            const std::vector<radio::PeerConfig>& existing,
                            radio::PeerConfig& out) {
    const std::string spec = trim(raw);
    if (spec.empty()) return false;

    std::string call_s;
    std::string hp_s;
    auto at = spec.find('@');
    auto space = spec.find_first_of(" \t");
    if (at != std::string::npos) {
        call_s = trim(spec.substr(0, at));
        hp_s = trim(spec.substr(at + 1));
    } else if (space != std::string::npos) {
        call_s = trim(spec.substr(0, space));
        hp_s = trim(spec.substr(space + 1));
    } else {
        hp_s = spec;
    }

    std::string host_s = hp_s;
    uint16_t port = default_port;
    auto colon = hp_s.rfind(':');
    if (colon != std::string::npos && colon > 0 && colon + 1 < hp_s.size()) {
        host_s = hp_s.substr(0, colon);
        try {
            int pv = std::clamp(std::stoi(hp_s.substr(colon + 1)), 0, 65535);
            if (pv > 0) port = static_cast<uint16_t>(pv);
        } catch (...) {
        }
    }
    if (host_s.empty()) return false;

    out = radio::PeerConfig{};
    if (!call_s.empty() && call_s != "*") {
        if (auto parsed = ax25::Callsign::parse(call_s)) {
            out.callsign = parsed->str();
        } else {
            out.callsign = call_s;
        }
    }
    out.interface_id = iface_id;
    out.host = host_s;
    out.port = port;
    out.allow_inbound = true;
    out.allow_outbound = true;
    out.auto_learned = false;
    out.id = derive_default_peer_id(existing, out.callsign.empty() ? iface_id : out.callsign);
    return true;
}

} // namespace oyk::node

namespace oyk::radio {

bool is_all_ascii_digits(const std::string& text) noexcept {
    if (text.empty()) return false;
    for (unsigned char ch : text) {
        if (ch < '0' || ch > '9') return false;
    }
    return true;
}

bool parse_dmr_id(const std::string& text, uint32_t& id_out) noexcept {
    if (!is_all_ascii_digits(text)) return false;
    uint64_t v = 0;
    for (unsigned char ch : text) {
        v = v * 10u + static_cast<uint64_t>(ch - '0');
        if (v > 16777214ULL) return false;
    }
    if (v == 0ULL || v > 16777214ULL) return false;
    id_out = static_cast<uint32_t>(v);
    return true;
}

std::string dmr_id_to_ipv4(uint32_t dmr_id, uint8_t prefix_octet) {
    return std::to_string(static_cast<unsigned>(prefix_octet)) + "." +
           std::to_string(static_cast<unsigned>((dmr_id >> 16) & 0xFFu)) + "." +
           std::to_string(static_cast<unsigned>((dmr_id >> 8) & 0xFFu)) + "." +
           std::to_string(static_cast<unsigned>(dmr_id & 0xFFu));
}

bool resolve_host_or_dmr_id(const std::string& host_spec,
                            uint8_t prefix_octet,
                            std::string& resolved_out) {
    if (host_spec.empty()) return false;
    if (is_all_ascii_digits(host_spec)) {
        uint32_t dmr_id = 0;
        if (!parse_dmr_id(host_spec, dmr_id)) return false;
        resolved_out = dmr_id_to_ipv4(dmr_id, prefix_octet);
        return true;
    }
    resolved_out = host_spec;
    return true;
}

uint8_t dmr_prefix_for_iface(const RadioInterfaceConfig& ic) {
    if (ic.type != "axudp" && ic.type != "axip") return 12;
    auto nmit = ic.params.find("netmask");
    if (nmit == ic.params.end() || nmit->second != "255.0.0.0") return 12;
    auto bhit = ic.params.find("bind_host");
    if (bhit == ic.params.end() || bhit->second.empty()) return 12;
    std::string bh;
    if (!resolve_host_or_dmr_id(bhit->second, 12, bh)) return 12;
    in_addr ia{};
    if (inet_pton(AF_INET, bh.c_str(), &ia) == 1) {
        const uint8_t top = reinterpret_cast<const uint8_t*>(&ia.s_addr)[0];
        if (top != 0) return top;
    }
    return 12;
}

bool validate_iface_and_peer_addresses(const std::vector<RadioInterfaceConfig>& ifaces,
                                       const std::vector<PeerConfig>& peers,
                                       std::string& error_out) {
    for (const auto& ic : ifaces) {
        if (ic.type == "axudp" || ic.type == "axip") {
            if (auto it = ic.params.find("bind_host"); it != ic.params.end() && !it->second.empty()) {
                if (is_all_ascii_digits(it->second)) {
                    uint32_t id = 0;
                    if (!parse_dmr_id(it->second, id)) {
                        error_out = "Invalid DMR ID '" + it->second + "' on interface '" + ic.id +
                                    "' (allowed: 1..16777214).";
                        return false;
                    }
                }
            }
            if (auto nm = ic.params.find("netmask"); nm != ic.params.end() && !nm->second.empty()) {
                in_addr ma{};
                if (inet_pton(AF_INET, nm->second.c_str(), &ma) != 1) {
                    error_out = "Invalid IPv4 netmask '" + nm->second + "' on interface '" + ic.id + "'.";
                    return false;
                }
            }
        }
    }
    for (const auto& p : peers) {
        if (!p.host.empty() && is_all_ascii_digits(p.host)) {
            uint32_t id = 0;
            if (!parse_dmr_id(p.host, id)) {
                error_out = "Invalid DMR ID '" + p.host + "' on peer '" + p.id +
                            "' (allowed: 1..16777214).";
                return false;
            }
        }
    }
    return true;
}

} // namespace oyk::radio
