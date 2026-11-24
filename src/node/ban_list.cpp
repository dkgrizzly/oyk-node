#include "oyk/node/ban_list.hpp"
#include "oyk/node/logger.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace oyk::node {

namespace {

std::string trim(std::string_view sv) {
    std::size_t a = 0;
    while (a < sv.size() && std::isspace(static_cast<unsigned char>(sv[a]))) ++a;
    std::size_t b = sv.size();
    while (b > a && std::isspace(static_cast<unsigned char>(sv[b - 1]))) --b;
    return std::string(sv.substr(a, b - a));
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

std::string BanEntry::format_banned_at() const {
    return BanList::format_timestamp(banned_at);
}

std::string BanEntry::format_lift_at() const {
    return permanent ? "permanent" : BanList::format_timestamp(lift_at);
}

std::string BanEntry::format_flags_binary() const {
    std::string out = "0b";
    // Format at least 4 bits (e.g. 0b0000), or more if higher bits are set
    int width = 4;
    if (flags > 0x0F) width = 8;
    for (int i = width - 1; i >= 0; --i) {
        out.push_back(((flags >> i) & 1U) ? '1' : '0');
    }
    return out;
}

std::optional<std::string> BanList::normalize_key(std::string_view spec) {
    std::string s = trim(spec);
    if (s.empty()) return std::nullopt;

    bool has_dash = (s.find('-') != std::string::npos);
    auto parsed = ax25::Callsign::parse(s);
    if (!parsed) return std::nullopt;

    if (has_dash && parsed->ssid() > 0) {
        return parsed->str();
    }
    return parsed->base();
}

bool BanList::parse_flags(std::string_view text, uint32_t& out) {
    std::string s = trim(text);
    if (s.empty()) return false;

    try {
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
            uint32_t val = 0;
            for (std::size_t i = 2; i < s.size(); ++i) {
                if (s[i] != '0' && s[i] != '1') return false;
                val = (val << 1) | static_cast<uint32_t>(s[i] - '0');
            }
            out = val;
            return true;
        }
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            std::size_t pos = 0;
            unsigned long val = std::stoul(s.substr(2), &pos, 16);
            if (pos != s.size() - 2) return false;
            out = static_cast<uint32_t>(val);
            return true;
        }
        std::size_t pos = 0;
        unsigned long val = std::stoul(s, &pos, 10);
        if (pos != s.size()) return false;
        out = static_cast<uint32_t>(val);
        return true;
    } catch (...) {
        return false;
    }
}

bool BanList::parse_timestamp(std::string_view text,
                              std::chrono::system_clock::time_point& out) {
    std::string s = trim(text);
    // Expected exact format: YYYYMMDD-HHMMSS (15 chars)
    if (s.size() != 15 || s[8] != '-') {
        return false;
    }
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i == 8) continue;
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }

    int year  = std::stoi(s.substr(0, 4));
    int month = std::stoi(s.substr(4, 2));
    int day   = std::stoi(s.substr(6, 2));
    int hour  = std::stoi(s.substr(9, 2));
    int min   = std::stoi(s.substr(11, 2));
    int sec   = std::stoi(s.substr(13, 2));

    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 60) {
        return false;
    }

    std::tm tm_val{};
    tm_val.tm_year = year - 1900;
    tm_val.tm_mon = month - 1;
    tm_val.tm_mday = day;
    tm_val.tm_hour = hour;
    tm_val.tm_min = min;
    tm_val.tm_sec = sec;
    tm_val.tm_isdst = -1;

    std::time_t tt = std::mktime(&tm_val);
    if (tt == static_cast<std::time_t>(-1)) {
        return false;
    }
    out = std::chrono::system_clock::from_time_t(tt);
    return true;
}

std::string BanList::format_timestamp(std::chrono::system_clock::time_point tp) {
    return Logger::format_timestamp_compact(tp);
}

std::string BanList::sanitize_appeal_text(std::string_view raw) {
    std::string clean;
    clean.reserve(std::min(raw.size(), MAX_APPEAL_LENGTH));
    for (unsigned char c : raw) {
        // Keep only base printable ASCII (0x20..0x7E); strip all control chars & non-ASCII
        if (c >= 0x20 && c <= 0x7E) {
            clean.push_back(static_cast<char>(c));
        }
    }
    clean = trim(clean);
    if (clean.size() > MAX_APPEAL_LENGTH) {
        clean.resize(MAX_APPEAL_LENGTH);
    }
    return clean;
}

void BanList::apply_preferred_line_ending(core::LineEnding eol,
                                          bool only_if_unset_or_mixed) noexcept {
    if (!only_if_unset_or_mixed || !has_detected_eol_ || had_mixed_eol_) {
        line_ending_ = eol;
    }
}

bool BanList::load_from_file(const std::string& path, std::string& error_out) {
    file_path_ = path;
    entries_.clear();
    has_detected_eol_ = false;
    had_mixed_eol_ = false;

    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return true;
    }

    std::string raw_bytes;
    if (!core::read_file_binary(path, raw_bytes)) {
        error_out = "Unable to open banlist file: " + path;
        return false;
    }

    core::LineEndingInfo eol_info = core::inspect_line_endings(raw_bytes, line_ending_);
    line_ending_ = eol_info.first;
    has_detected_eol_ = eol_info.found_any;
    had_mixed_eol_ = eol_info.mixed;

    auto lines = core::split_lines_any_eol(raw_bytes);
    int line_num = 0;
    for (const std::string& raw_line : lines) {
        ++line_num;
        std::string line = trim(raw_line);
        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) {
            error_out = path + ":" + std::to_string(line_num) + ": expected CALLSIGN=ban_time,lift_time,flags,reason";
            return false;
        }

        auto key_opt = normalize_key(line.substr(0, eq));
        if (!key_opt) {
            error_out = path + ":" + std::to_string(line_num) + ": invalid callsign '" +
                        trim(line.substr(0, eq)) + "'";
            return false;
        }

        std::string rhs = trim(line.substr(eq + 1));
        auto c1 = rhs.find(',');
        auto c2 = (c1 == std::string::npos) ? std::string::npos : rhs.find(',', c1 + 1);
        auto c3 = (c2 == std::string::npos) ? std::string::npos : rhs.find(',', c2 + 1);
        if (c1 == std::string::npos || c2 == std::string::npos || c3 == std::string::npos) {
            error_out = path + ":" + std::to_string(line_num) +
                        ": expected 4 comma-separated fields (ban_time,lift_time,flags,reason)";
            return false;
        }

        std::string f_ban_time  = trim(rhs.substr(0, c1));
        std::string f_lift_time = trim(rhs.substr(c1 + 1, c2 - c1 - 1));
        std::string f_flags     = trim(rhs.substr(c2 + 1, c3 - c2 - 1));
        std::string f_reason    = trim(rhs.substr(c3 + 1));

        BanEntry entry;
        entry.key = *key_opt;
        if (!parse_timestamp(f_ban_time, entry.banned_at)) {
            error_out = path + ":" + std::to_string(line_num) +
                        ": invalid ban timestamp '" + f_ban_time + "' (expected YYYYMMDD-HHMMSS)";
            return false;
        }

        if (to_lower(f_lift_time) == "permanent") {
            entry.permanent = true;
        } else {
            entry.permanent = false;
            if (!parse_timestamp(f_lift_time, entry.lift_at)) {
                error_out = path + ":" + std::to_string(line_num) +
                            ": invalid lift timestamp '" + f_lift_time + "' (expected YYYYMMDD-HHMMSS or permanent)";
                return false;
            }
        }

        if (!parse_flags(f_flags, entry.flags)) {
            error_out = path + ":" + std::to_string(line_num) +
                        ": invalid flags bitfield '" + f_flags + "'";
            return false;
        }

        entry.reason = f_reason.empty() ? "Banned by sysop." : f_reason;
        entries_[entry.key] = std::move(entry);
    }

    return true;
}

bool BanList::save_to_file(const std::string& path) const {
    const std::string target = path.empty() ? file_path_ : path;
    if (target.empty()) return false;

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p(target);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path(), ec);
    }

    const std::string tmp_path = target + ".tmp";
    std::vector<std::string> lines;
    lines.push_back("# OYK Packet Node Ban List");
    lines.push_back("# Format: CALLSIGN[-SSID]=YYYYMMDD-HHMMSS,YYYYMMDD-HHMMSS|permanent,flags,reason");
    lines.push_back("# Flags bitfield (accepts 0b..., 0x..., or decimal):");
    lines.push_back("#   bit 0 (0b0001 / 0x01): Appeal allowed");
    lines.push_back("#   bit 1 (0b0010 / 0x02): User has been notified");
    lines.push_back("#   bit 2 (0b0100 / 0x04): Appeal already used");
    lines.push_back("#   bit 3 (0b1000 / 0x08): Hard ban (no connections after initial notification)");
    lines.push_back("");

    for (const auto& [_, e] : entries_) {
        lines.push_back(e.key + "=" +
                        e.format_banned_at() + "," +
                        e.format_lift_at() + "," +
                        e.format_flags_binary() + "," +
                        e.reason);
    }

    if (!core::write_lines_binary(tmp_path, lines, line_ending_)) {
        fs::remove(tmp_path, ec);
        return false;
    }

    fs::rename(tmp_path, target, ec);
    if (ec) {
        fs::remove(tmp_path, ec);
        return false;
    }
    return true;
}

const BanEntry* BanList::find_matching_entry(const ax25::Callsign& call,
                                             std::chrono::system_clock::time_point now) const {
    // Check exact CALL-SSID first, then base CALL
    if (auto it = entries_.find(call.str()); it != entries_.end()) {
        if (!it->second.is_expired(now)) {
            return &it->second;
        }
    }
    if (auto it = entries_.find(call.base()); it != entries_.end()) {
        if (!it->second.is_expired(now)) {
            return &it->second;
        }
    }
    return nullptr;
}

std::optional<BanEntry> BanList::find_active(const ax25::Callsign& call,
                                             std::chrono::system_clock::time_point now) const {
    const BanEntry* e = find_matching_entry(call, now);
    if (!e) return std::nullopt;
    return *e;
}

bool BanList::is_banned(const ax25::Callsign& call,
                        std::chrono::system_clock::time_point now) const {
    return find_matching_entry(call, now) != nullptr;
}

BanAccessDecision BanList::evaluate_access(const ax25::Callsign& call,
                                           std::chrono::system_clock::time_point now) const {
    BanAccessDecision dec;
    const BanEntry* e = find_matching_entry(call, now);
    if (!e) {
        dec.action = BanAccessAction::AllowNormal;
        return dec;
    }

    dec.ban_key = e->key;
    dec.entry = *e;

    if (!e->notified()) {
        dec.action = BanAccessAction::RestrictedNotify;
        return dec;
    }

    if (e->hard_ban()) {
        dec.action = BanAccessAction::RejectHardBan;
        return dec;
    }

    const std::string today = Logger::format_date_yyyymmdd(now);
    if (e->last_check_date == today) {
        dec.action = BanAccessAction::RejectDailyLimit;
    } else {
        dec.action = BanAccessAction::RestrictedStatus;
    }
    return dec;
}

bool BanList::upsert(BanEntry entry) {
    auto key_opt = normalize_key(entry.key);
    if (!key_opt) return false;
    entry.key = *key_opt;
    if (entry.reason.empty()) {
        entry.reason = "Banned by sysop.";
    }
    entries_[entry.key] = std::move(entry);
    return true;
}

bool BanList::remove(std::string_view callsign_spec, std::string* removed_key) {
    auto key_opt = normalize_key(callsign_spec);
    if (!key_opt) return false;
    if (entries_.erase(*key_opt) > 0) {
        if (removed_key) *removed_key = *key_opt;
        return true;
    }
    return false;
}

bool BanList::mark_notified_and_checked(const std::string& key, const std::string& yyyymmdd) {
    auto it = entries_.find(key);
    if (it == entries_.end()) return false;
    it->second.flags |= BAN_FLAG_NOTIFIED;
    it->second.last_check_date = yyyymmdd;
    return true;
}

bool BanList::mark_daily_checked(const std::string& key, const std::string& yyyymmdd) {
    auto it = entries_.find(key);
    if (it == entries_.end()) return false;
    it->second.last_check_date = yyyymmdd;
    return true;
}

bool BanList::mark_appeal_used(const std::string& key) {
    auto it = entries_.find(key);
    if (it == entries_.end()) return false;
    it->second.flags |= BAN_FLAG_APPEAL_USED;
    return true;
}

std::size_t BanList::purge_expired(std::chrono::system_clock::time_point now) {
    std::size_t removed = 0;
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.is_expired(now)) {
            it = entries_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

std::vector<BanEntry> BanList::entries(std::chrono::system_clock::time_point now,
                                       bool include_expired) const {
    std::vector<BanEntry> out;
    out.reserve(entries_.size());
    for (const auto& [_, e] : entries_) {
        if (include_expired || !e.is_expired(now)) {
            out.push_back(e);
        }
    }
    return out;
}

} // namespace oyk::node
