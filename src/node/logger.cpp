// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/node/logger.hpp"
#include "oyk_printer.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#if defined(__linux__) && defined(__has_include)
#  if __has_include(<linux/ppdev.h>)
#    if __has_include(<linux/parport.h>)
#      include <linux/parport.h>
#    endif
#    include <linux/ppdev.h>
#    define OYK_HAVE_LINUX_PPDEV 1
#  endif
#endif

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

speed_t baud_to_speed(uint32_t baud) {
    switch (baud) {
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B9600;
    }
}

bool write_all_fd(int fd, const unsigned char* data, std::size_t len) {
    std::size_t off = 0;
    int retries = 0;
    while (off < len) {
        ssize_t n = ::write(fd, data + off, len - off);
        if (n > 0) {
            off += static_cast<std::size_t>(n);
            retries = 0;
            continue;
        }
        if (n < 0 && (errno == EINTR || (errno == EAGAIN && retries++ < 50))) {
            continue;
        }
        return false;
    }
    return true;
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
    close_printer_locked();
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

    close_printer_locked();
    for (auto& t : targets_) {
        if (t.stream.is_open()) {
            t.stream.flush();
            t.stream.close();
        }
    }
    targets_.clear();
    config_ = config;

    std::string prn_warn;
    open_printer_locked(prn_warn);
    if (!prn_warn.empty()) {
        error_out = prn_warn;
        push_console_locked("[WARN] " + prn_warn);
    }

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
        std::string dir_warn = "Log directory '" + dir.string() + "' is not writable (" + ec.message() +
                               "); logging to console memory only.";
        if (!error_out.empty()) error_out += " ";
        error_out += dir_warn;
        configured_ = true;
        push_console_locked("[WARN] " + dir_warn);
        return true;
    }

    const std::string today = format_date_yyyymmdd(std::chrono::system_clock::now());
    std::vector<std::string> failed_files;

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
            failed_files.push_back(target.full_path);
            continue;
        }
        targets_.push_back(std::move(target));
    }

    configured_ = true;
    if (!failed_files.empty()) {
        std::string file_warn = "Log file(s) not writable (read-only filesystem?): " + failed_files.front() +
                                "; continuing with in-memory console logging.";
        if (!error_out.empty()) error_out += " ";
        error_out += file_warn;
        push_console_locked("[WARN] " + file_warn);
    }
    return true;
}

bool Logger::reconfigure_printer(const PrinterConfig& printer, std::string& error_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.printer = printer;
    open_printer_locked(error_out);
    if (!error_out.empty()) {
        push_console_locked("[WARN] " + error_out);
    }
    return error_out.empty();
}

void Logger::close_printer_locked() {
    if (printer_fd_ >= 0) {
#ifdef OYK_HAVE_LINUX_PPDEV
        if (printer_ppdev_claimed_) {
            (void)::ioctl(printer_fd_, PPRELEASE);
        }
#endif
        ::close(printer_fd_);
        printer_fd_ = -1;
    }
    printer_ppdev_claimed_ = false;
    printer_proto_ = OYK_PRN_NONE;
}

void Logger::open_printer_locked(std::string& error_out) {
    close_printer_locked();
    if (!config_.printer.enabled || config_.printer.device.empty()) {
        return;
    }

    int proto = oyk_printer_parse_protocol(config_.printer.protocol.c_str());
    if (proto == OYK_PRN_NONE) {
        if (config_.printer.protocol.empty()) {
            proto = OYK_PRN_GENERIC;
        } else {
            error_out = "Printer protocol '" + config_.printer.protocol + "' disabled or unknown.";
            return;
        }
    }

    const std::string& dev = config_.printer.device;
    const bool is_parport = (dev.rfind("/dev/parport", 0) == 0);
    const int flags = (is_parport ? O_RDWR : O_WRONLY) | O_NOCTTY | O_NONBLOCK;
    int fd = ::open(dev.c_str(), flags);
    if (fd < 0) {
        error_out = "Printer device '" + dev + "' unavailable (" + std::strerror(errno) + ").";
        return;
    }

#ifdef OYK_HAVE_LINUX_PPDEV
    if (is_parport) {
        if (::ioctl(fd, PPCLAIM) == 0) {
            printer_ppdev_claimed_ = true;
#ifdef IEEE1284_MODE_COMPAT
            int mode = IEEE1284_MODE_COMPAT;
            (void)::ioctl(fd, PPSETMODE, &mode);
#endif
        } else {
            error_out = "Printer ppdev claim failed on '" + dev + "' (" + std::strerror(errno) + ").";
            ::close(fd);
            return;
        }
    }
#endif

    if (::isatty(fd)) {
        struct termios tio{};
        if (::tcgetattr(fd, &tio) == 0) {
            ::cfmakeraw(&tio);
            tio.c_cflag |= (CLOCAL | CREAD | CS8);
            tio.c_cflag &= ~(PARENB | CSTOPB);
            const speed_t spd = baud_to_speed(config_.printer.baud);
            (void)::cfsetispeed(&tio, spd);
            (void)::cfsetospeed(&tio, spd);
            (void)::tcsetattr(fd, TCSANOW, &tio);
        }
    }

    int fl = ::fcntl(fd, F_GETFL, 0);
    if (fl >= 0 && (fl & O_NONBLOCK) != 0) {
        (void)::fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    }

    printer_fd_ = fd;
    printer_proto_ = proto;

    unsigned char init_buf[32];
    std::size_t init_len = oyk_printer_build_init(printer_proto_, config_.printer.condensed ? 1 : 0,
                                                  init_buf, sizeof(init_buf));
    if (init_len > 0 && !write_all_fd(printer_fd_, init_buf, init_len)) {
        error_out = "Printer init write failed on '" + dev + "'.";
        close_printer_locked();
    }
}

void Logger::write_printer_locked(LogLevel level, const std::string& line) {
    if (printer_fd_ < 0 || !config_.printer.enabled) return;
    if ((static_cast<uint32_t>(level) & config_.printer.level) == 0) return;
    unsigned char buf[512];
    std::size_t n = oyk_printer_format_line(printer_proto_, static_cast<unsigned long>(level),
                                            line.c_str(), buf, sizeof(buf));
    if (n > 0 && !write_all_fd(printer_fd_, buf, n)) {
        push_console_locked("[WARN] Printer write failed; disabling active printer handle.");
        close_printer_locked();
    }
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

    write_printer_locked(level, file_line);
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

    write_printer_locked(LOG_APPEAL, appeal_line);
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
