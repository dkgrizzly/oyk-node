#pragma once

#include "oyk/ax25/callsign.hpp"
#include "oyk/core/line_ending.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oyk::node {

// Ban entry bitfield flags (4-bit representation e.g. 0b0000):
//   Bit 0 (0b0001 / 0x01): Ban can be appealed
//   Bit 1 (0b0010 / 0x02): User has been notified of the ban
//   Bit 2 (0b0100 / 0x04): User has already submitted their appeal
//   Bit 3 (0b1000 / 0x08): Hard ban (no further connections after initial notification)
constexpr uint32_t BAN_FLAG_APPEAL_ALLOWED = 0x01;
constexpr uint32_t BAN_FLAG_NOTIFIED       = 0x02;
constexpr uint32_t BAN_FLAG_APPEAL_USED    = 0x04;
constexpr uint32_t BAN_FLAG_HARD_BAN       = 0x08;

constexpr std::size_t MAX_APPEAL_LENGTH = 140;

struct BanEntry {
    std::string key; // Normalized "CALL" (all SSIDs) or "CALL-N" (specific SSID)
    std::chrono::system_clock::time_point banned_at{};
    bool permanent = true;
    std::chrono::system_clock::time_point lift_at{};
    uint32_t flags = 0;
    std::string reason;

    // Runtime-only daily check-in date (YYYYMMDD) for soft-banned users
    std::string last_check_date;

    bool appeal_allowed() const noexcept { return (flags & BAN_FLAG_APPEAL_ALLOWED) != 0; }
    bool notified() const noexcept       { return (flags & BAN_FLAG_NOTIFIED) != 0; }
    bool appeal_used() const noexcept    { return (flags & BAN_FLAG_APPEAL_USED) != 0; }
    bool hard_ban() const noexcept       { return (flags & BAN_FLAG_HARD_BAN) != 0; }

    bool is_expired(std::chrono::system_clock::time_point now) const noexcept {
        return !permanent && now >= lift_at;
    }

    std::string format_banned_at() const;
    std::string format_lift_at() const;
    std::string format_flags_binary() const;
};

enum class BanAccessAction {
    AllowNormal,       // Not banned (or ban has expired)
    RestrictedNotify,  // First notification (not yet notified) -> restricted session
    RestrictedStatus,  // Soft-banned user's once-per-day status/appeal check -> restricted session
    RejectHardBan,     // Hard-banned and already notified -> DM
    RejectDailyLimit   // Soft-banned and already checked in today -> DM
};

struct BanAccessDecision {
    BanAccessAction action = BanAccessAction::AllowNormal;
    std::string ban_key;
    std::optional<BanEntry> entry;
};

// Manages banlist.cfg entries, persistence, expiry, bitfield parsing, and appeal sanitization.
class BanList {
public:
    // Load banlist.cfg (missing file is treated as empty list and returns true).
    bool load_from_file(const std::string& path, std::string& error_out);

    // Save current banlist entries to `path` (or previously loaded path if `path` empty).
    bool save_to_file(const std::string& path = "") const;

    const std::string& file_path() const noexcept { return file_path_; }
    void set_file_path(std::string path) { file_path_ = std::move(path); }

    core::LineEnding line_ending() const noexcept { return line_ending_; }
    // Sets preferred line ending (e.g. from node.cfg). When `only_if_unset_or_mixed` is true,
    // preserves a clean uniform EOL read from banlist.cfg, while overriding mixed/absent EOLs.
    void apply_preferred_line_ending(core::LineEnding eol,
                                     bool only_if_unset_or_mixed = true) noexcept;

    // Normalize a callsign key ("CALL" for base / all SSIDs; "CALL-N" for explicit SSID > 0).
    static std::optional<std::string> normalize_key(std::string_view spec);

    // Parse flags from 0b..., 0x..., or decimal without prefix.
    static bool parse_flags(std::string_view text, uint32_t& out);

    // Parse/format ban timestamp "YYYYMMDD-HHMMSS" (local time).
    static bool parse_timestamp(std::string_view text,
                                std::chrono::system_clock::time_point& out);
    static std::string format_timestamp(std::chrono::system_clock::time_point tp);

    // Sanitizes an appeal message: keeps only base printable ASCII (0x20..0x7E,
    // no control characters), trims leading/trailing whitespace, truncates to 140 chars.
    static std::string sanitize_appeal_text(std::string_view raw);

    // Evaluates incoming connection access for `call` at time `now`.
    // Does NOT mutate state until mark_notified() / mark_daily_check() is called.
    BanAccessDecision evaluate_access(
        const ax25::Callsign& call,
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) const;

    // Returns active (non-expired) ban entry for `call`, if any.
    std::optional<BanEntry> find_active(
        const ax25::Callsign& call,
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) const;

    bool is_banned(
        const ax25::Callsign& call,
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) const;

    // Add or update a ban entry. Returns normalized key.
    bool upsert(BanEntry entry);

    // Remove a ban entry by callsign spec.
    bool remove(std::string_view callsign_spec, std::string* removed_key = nullptr);

    // Mutate flags/check-in date on an existing ban entry (returns true if updated).
    bool mark_notified_and_checked(const std::string& key, const std::string& yyyymmdd);
    bool mark_daily_checked(const std::string& key, const std::string& yyyymmdd);
    bool mark_appeal_used(const std::string& key);

    // Removes expired bans; returns count removed.
    std::size_t purge_expired(
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

    std::vector<BanEntry> entries(
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now(),
        bool include_expired = false) const;

private:
    const BanEntry* find_matching_entry(const ax25::Callsign& call,
                                        std::chrono::system_clock::time_point now) const;

    std::string file_path_;
    core::LineEnding line_ending_ = core::platform_default_line_ending();
    bool has_detected_eol_ = false;
    bool had_mixed_eol_ = false;
    std::map<std::string, BanEntry> entries_;
};

} // namespace oyk::node
