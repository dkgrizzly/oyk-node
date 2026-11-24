#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace oyk::node {

// Severity bitmask flags
enum LogLevel : uint32_t {
    LOG_SILENT  = 0x00,
    LOG_ERROR   = 0x01,
    LOG_WARNING = 0x02,
    LOG_INFO    = 0x04,
    LOG_KICK    = 0x10,
    LOG_BAN     = 0x20,
    LOG_APPEAL  = 0x40,
    LOG_DEBUG   = 0x80
};

// Default masks
constexpr uint32_t DEFAULT_CONSOLE_LOG_MASK =
    LOG_ERROR | LOG_WARNING | LOG_INFO | LOG_KICK | LOG_BAN | LOG_APPEAL; // 0x77 (all except DEBUG)
constexpr uint32_t DEFAULT_NODE_LOG_MASK =
    LOG_ERROR | LOG_WARNING | LOG_INFO | LOG_KICK | LOG_BAN; // 0x37
constexpr uint32_t DEFAULT_APPEALS_LOG_MASK = LOG_APPEAL; // 0x40

struct LogConfig {
    std::string log_path = "logs/";
    uint32_t console_level = DEFAULT_CONSOLE_LOG_MASK;
    bool rotate_daily = true;
    // Map of filename (relative to log_path, e.g. "node.log") -> severity bitmask
    std::map<std::string, uint32_t> log_files;
};

// Parses a log level bitmask from decimal, hex (0x...), binary (0b...),
// or symbolic names separated by '|' or ',' (e.g. "ERROR|WARNING|INFO|KICK|BAN").
bool parse_log_mask(std::string_view text, uint32_t& out);

// Returns uppercase name for a single LogLevel flag.
std::string log_level_name(LogLevel level);

// Thread-safe severity-filtering logger supporting multiple log files under
// `logpath`, console ring buffer filtering via `consolelevel`, and daily rotation.
class Logger {
public:
    Logger() = default;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Configures log directory, console mask, and file targets.
    // Creates `log_path` directory if needed and rotates stale previous-day files.
    bool configure(const LogConfig& config, std::string& error_out);

    // Logs a standard message at `level`.
    void log(LogLevel level, const std::string& message);
    void log_at(LogLevel level, const std::string& message,
                std::chrono::system_clock::time_point now);

    // Logs an appeal strictly as: "<CALLSIGN> <YYYYMMDD-HHMMSS> <sanitized_message>"
    // to any file matching LOG_APPEAL (e.g. appeals.log) and to the local sysop console.
    void log_appeal(const std::string& callsign,
                    const std::string& sanitized_message,
                    std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

    // Checks if the calendar day has changed and rotates open log files if so.
    void check_daily_rotation(
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

    // Returns recent console-filtered log lines (for ncurses UI / headless stdout).
    std::vector<std::string> recent_console_logs() const;

    uint32_t console_level() const;
    void set_console_level(uint32_t mask);

    static std::string format_date_yyyymmdd(std::chrono::system_clock::time_point tp);
    static std::string format_timestamp_compact(std::chrono::system_clock::time_point tp);
    static std::string format_timestamp_hms(std::chrono::system_clock::time_point tp);
    static std::string format_timestamp_full(std::chrono::system_clock::time_point tp);

private:
    struct FileTarget {
        std::string filename;
        std::string full_path;
        uint32_t mask = 0;
        std::string active_date; // YYYYMMDD of currently open file
        std::ofstream stream;
    };

    void rotate_target_locked(FileTarget& target, const std::string& new_date);
    void open_target_locked(FileTarget& target, const std::string& today);
    void push_console_locked(const std::string& line);

    mutable std::mutex mutex_;
    LogConfig config_;
    std::vector<FileTarget> targets_;
    std::deque<std::string> console_lines_;
    bool configured_ = false;
};

} // namespace oyk::node
