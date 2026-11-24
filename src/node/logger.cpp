// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/node/logger.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace oyk::node {

namespace {

constexpr std::size_t MAX_CONSOLE_LINES = 200;

std::string trim(std::string_view sv) {
    std::size_t a = 0;
    while (a < sv.size() && std::isspace(static_cast<unsigned char>(sv[a]))) ++a;
    std::size_t b = sv.size();
    while (b > a && std::isspace(static_cast<unsigned char>(sv[b - 1]))) --b;
    return std::string(sv.substr(a, b - a));
}

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool parse_single_number(std::string_view tok, uint32_t& val) {
    if (tok.empty()) return false;
    try {
        std::string s(tok);
        std::size_t pos = 0;
        unsigned long parsed = 0;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
            for (std::size_t i = 2; i < s.size(); ++i) {
                if (s[i] != '0' && s[i] != '1') return false;
                parsed = (parsed << 1) | static_cast<unsigned long>(s[i] - '0');
            }
            val = static_cast<uint32_t>(parsed);
            return true;
        }
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            parsed = std::stoul(s.substr(2), &pos, 16);
            if (pos != s.size() - 2) return false;
            val = static_cast<uint32_t>(parsed);
            return true;
        }
        parsed = std::stoul(s, &pos, 10);
        if (pos != s.size()) return false;
        val = static_cast<uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_single_symbol(std::string_view tok, uint32_t& val) {
    std::string u = to_upper(std::string(tok));
    if (u == "SILENT" || u == "NONE") { val = LOG_SILENT; return true; }
    if (u == "ERROR" || u == "ERR")   { val = LOG_ERROR; return true; }
    if (u == "WARNING" || u == "WARN"){ val = LOG_WARNING; return true; }
    if (u == "INFO")                  { val = LOG_INFO; return true; }
    if (u == "KICK")                  { val = LOG_KICK; return true; }
    if (u == "BAN")                   { val = LOG_BAN; return true; }
    if (u == "APPEAL")                { val = LOG_APPEAL; return true; }
    if (u == "DEBUG")                 { val = LOG_DEBUG; return true; }
    if (u == "ALL") {
        val = LOG_ERROR | LOG_WARNING | LOG_INFO | LOG_KICK | LOG_BAN | LOG_APPEAL | LOG_DEBUG;
        return true;
    }
    return false;
}

} // namespace

bool parse_log_mask(std::string_view text, uint32_t& out) {
    std::string s = trim(text);
    if (s.empty()) return false;

    uint32_t single = 0;
    if (parse_single_number(s, single)) {
        out = single;
        return true;
    }

    // Split on '|' or ',' or '+'
    uint32_t mask = 0;
    bool any = false;
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t sep = s.find_first_of("|,+", start);
        std::string tok = trim(sep == std::string::npos
                                   ? std::string_view(s).substr(start)
                                   : std::string_view(s).substr(start, sep - start));
        if (!tok.empty()) {
            uint32_t v = 0;
            if (!parse_single_number(tok, v) && !parse_single_symbol(tok, v)) {
                return false;
            }
            mask |= v;
            any = true;
        }
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    if (!any) return false;
    out = mask;
    return true;
}

std::string log_level_name(LogLevel level) {
    switch (level) {
        case LOG_SILENT:  return "SILENT";
        case LOG_ERROR:   return "ERROR";
        case LOG_WARNING: return "WARN";
        case LOG_INFO:    return "INFO";
        case LOG_KICK:    return "KICK";
        case LOG_BAN:     return "BAN";
        case LOG_APPEAL:  return "APPEAL";
        case LOG_DEBUG:   return "DEBUG";
    }
    return "LOG";
}

std::string Logger::format_date_yyyymmdd(std::chrono::system_clock::time_point tp) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
    localtime_r(&tt, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y%m%d");
    return oss.str();
}

std::string Logger::format_timestamp_compact(std::chrono::system_clock::time_point tp) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
    localtime_r(&tt, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y%m%d-%H%M%S");
    return oss.str();
}

std::string Logger::format_timestamp_hms(std::chrono::system_clock::time_point tp) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
    localtime_r(&tt, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%H:%M:%S");
    return oss.str();
}

std::string Logger::format_timestamp_full(std::chrono::system_clock::time_point tp) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
    localtime_r(&tt, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

Logger::~Logger() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& t : targets_) {
        if (t.stream.is_open()) {
            t.stream.flush();
            t.stream.close();
        }
    }
}

bool Logger::configure(const LogConfig& config, std::string& error_out) {
    namespace fs = std::filesystem;
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& t : targets_) {
        if (t.stream.is_open()) {
            t.stream.flush();
            t.stream.close();
        }
    }
    targets_.clear();
    config_ = config;

    // Ensure defaults for node.log and appeals.log if not explicitly specified
    if (config_.log_files.find("node.log") == config_.log_files.end()) {
        config_.log_files["node.log"] = DEFAULT_NODE_LOG_MASK;
    }
    if (config_.log_files.find("appeals.log") == config_.log_files.end()) {
        config_.log_files["appeals.log"] = DEFAULT_APPEALS_LOG_MASK;
    }

    fs::path dir = config_.log_path.empty() ? fs::path("logs") : fs::path(config_.log_path);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        error_out = "Failed to create log directory '" + dir.string() + "': " + ec.message();
        return false;
    }

    const std::string today = format_date_yyyymmdd(std::chrono::system_clock::now());

    for (const auto& [name, mask] : config_.log_files) {
        if (mask == LOG_SILENT || name.empty()) continue;

        FileTarget target;
        target.filename = name;
        target.full_path = (dir / fs::path(name).filename()).string();
        target.mask = mask;

        // If an existing log file was last modified on an earlier day, rotate it now
        if (config_.rotate_daily && fs::exists(target.full_path, ec) && !ec) {
            auto ftime = fs::last_write_time(target.full_path, ec);
            if (!ec) {
                auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
                std::string file_date = format_date_yyyymmdd(sctp);
                if (file_date < today) {
                    target.active_date = file_date;
                    rotate_target_locked(target, today);
                }
            }
        }

        open_target_locked(target, today);
        if (!target.stream.is_open()) {
            error_out = "Failed to open log file: " + target.full_path;
            return false;
        }
        targets_.push_back(std::move(target));
    }

    configured_ = true;
    return true;
}

void Logger::open_target_locked(FileTarget& target, const std::string& today) {
    if (target.stream.is_open()) {
        target.stream.flush();
        target.stream.close();
    }
    target.stream.open(target.full_path, std::ios::out | std::ios::app);
    target.active_date = today;
}

void Logger::rotate_target_locked(FileTarget& target, const std::string& new_date) {
    namespace fs = std::filesystem;
    if (target.stream.is_open()) {
        target.stream.flush();
        target.stream.close();
    }

    std::error_code ec;
    if (fs::exists(target.full_path, ec) && !ec && fs::file_size(target.full_path, ec) > 0) {
        std::string suffix = target.active_date.empty() ? new_date : target.active_date;
        std::string rotated = target.full_path + "." + suffix;
        int seq = 1;
        while (fs::exists(rotated, ec) && !ec) {
            rotated = target.full_path + "." + suffix + "." + std::to_string(seq++);
        }
        fs::rename(target.full_path, rotated, ec);
    }

    open_target_locked(target, new_date);
}

void Logger::check_daily_rotation(std::chrono::system_clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!configured_ || !config_.rotate_daily) return;
    const std::string today = format_date_yyyymmdd(now);
    for (auto& target : targets_) {
        if (!target.active_date.empty() && target.active_date != today) {
            rotate_target_locked(target, today);
        }
    }
}

void Logger::push_console_locked(const std::string& line) {
    console_lines_.push_back(line);
    while (console_lines_.size() > MAX_CONSOLE_LINES) {
        console_lines_.pop_front();
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    log_at(level, message, std::chrono::system_clock::now());
}

void Logger::log_at(LogLevel level, const std::string& message,
                    std::chrono::system_clock::time_point now) {
    if (level == LOG_SILENT) return;

    std::lock_guard<std::mutex> lock(mutex_);
    const std::string today = format_date_yyyymmdd(now);

    if ((static_cast<uint32_t>(level) & config_.console_level) != 0) {
        push_console_locked("[" + format_timestamp_hms(now) + "] [" +
                            log_level_name(level) + "] " + message);
    }

    if (!configured_) return;

    const std::string file_line =
        "[" + format_timestamp_full(now) + "] [" + log_level_name(level) + "] " + message;

    for (auto& target : targets_) {
        if ((static_cast<uint32_t>(level) & target.mask) == 0) continue;
        if (config_.rotate_daily && !target.active_date.empty() && target.active_date != today) {
            rotate_target_locked(target, today);
        }
        if (target.stream.is_open()) {
            target.stream << file_line << "\n";
            target.stream.flush();
        }
    }
}

void Logger::log_appeal(const std::string& callsign,
                        const std::string& sanitized_message,
                        std::chrono::system_clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string today = format_date_yyyymmdd(now);
    const std::string appeal_time = format_timestamp_compact(now);

    // Strictly: callsign, time of appeal, and short base-ASCII appeal message
    const std::string appeal_line = callsign + " " + appeal_time + " " + sanitized_message;

    if ((static_cast<uint32_t>(LOG_APPEAL) & config_.console_level) != 0) {
        push_console_locked("[APPEAL] " + appeal_line);
    }

    if (!configured_) return;

    for (auto& target : targets_) {
        if ((static_cast<uint32_t>(LOG_APPEAL) & target.mask) == 0) continue;
        if (config_.rotate_daily && !target.active_date.empty() && target.active_date != today) {
            rotate_target_locked(target, today);
        }
        if (target.stream.is_open()) {
            target.stream << appeal_line << "\n";
            target.stream.flush();
        }
    }
}

std::vector<std::string> Logger::recent_console_logs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<std::string>(console_lines_.begin(), console_lines_.end());
}

uint32_t Logger::console_level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.console_level;
}

void Logger::set_console_level(uint32_t mask) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.console_level = mask;
}

} // namespace oyk::node
