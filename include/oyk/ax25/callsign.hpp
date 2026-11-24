#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace oyk::ax25 {

// Represents an AX.25 callsign with an optional SSID (0-15).
class Callsign {
public:
    Callsign() = default;
    Callsign(std::string base, uint8_t ssid = 0);

    // Parse formats like "N0CALL", "N0CALL-0", "w1aw-7".
    static std::optional<Callsign> parse(std::string_view text);

    // Decode a 7-byte AX.25 address field entry.
    // Sets `c_or_h_bit` to bit 7 of the SSID byte and `is_last` to bit 0.
    static std::optional<Callsign> decode_ax25(const uint8_t bytes[7],
                                               bool* c_or_h_bit = nullptr,
                                               bool* is_last = nullptr);

    // Encode into a 7-byte AX.25 address field entry.
    std::array<uint8_t, 7> encode_ax25(bool c_or_h_bit, bool is_last) const;

    const std::string& base() const noexcept { return base_; }
    uint8_t ssid() const noexcept { return ssid_; }
    bool valid() const noexcept { return !base_.empty() && ssid_ <= 15; }

    // Canonical string representation: "CALL" if ssid == 0, else "CALL-N".
    std::string str() const;

    bool operator==(const Callsign& other) const noexcept {
        return base_ == other.base_ && ssid_ == other.ssid_;
    }
    bool operator!=(const Callsign& other) const noexcept {
        return !(*this == other);
    }
    bool operator<(const Callsign& other) const noexcept {
        if (base_ != other.base_) return base_ < other.base_;
        return ssid_ < other.ssid_;
    }

private:
    std::string base_;
    uint8_t ssid_ = 0;
};

} // namespace oyk::ax25
