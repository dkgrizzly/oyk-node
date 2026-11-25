// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/node/node_config.hpp"

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
            } else if (lower_sec == "logfiles") {
                current_section = "logfiles";
                current_iface = nullptr;
            } else if (lower_sec == "bbs") {
                current_section = "bbs";
                current_iface = nullptr;
            } else if (lower_sec == "chat") {
                current_section = "chat";
                current_iface = nullptr;
            } else if (lower_sec == "rig" || lower_sec == "radio") {
                current_section = "rig";
                current_iface = nullptr;
            } else if (lower_sec == "printer") {
                current_section = "printer";
                current_iface = nullptr;
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
            } else if (key == "description") {
                current_iface->description = val;
            } else if (key == "peer") {
                if (!val.empty()) {
                    current_iface->peers.push_back(val);
                }
            } else {
                current_iface->params[key] = val;
            }
        }
    }

    if (cfg.rig.port == 0 && cfg.rig.protocol != radio::RigProtocol::None) {
        cfg.rig.port = radio::rig_default_port(cfg.rig.protocol);
    }

    if (cfg.interfaces.empty()) {
        cfg.interfaces = make_default().interfaces;
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
        for (const auto& [k, v] : iface.params) {
            lines.push_back(k + " = " + v);
        }
        for (const auto& peer : iface.peers) {
            lines.push_back("peer = " + peer);
        }
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

} // namespace oyk::node
