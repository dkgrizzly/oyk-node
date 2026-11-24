// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/ax25/callsign.hpp"

#include <algorithm>
#include <cctype>

namespace oyk::ax25 {

namespace {

bool is_valid_base(std::string_view s) {
    if (s.empty() || s.size() > 6) {
        return false;
    }
    for (unsigned char c : s) {
        if (!std::isalnum(c)) {
            return false;
        }
    }
    return true;
}

std::string to_upper_ascii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(static_cast<char>(std::toupper(c)));
    }
    return out;
}

} // namespace

Callsign::Callsign(std::string base, uint8_t ssid)
    : base_(to_upper_ascii(base)), ssid_(ssid) {
    if (!is_valid_base(base_) || ssid_ > 15) {
        base_.clear();
        ssid_ = 0;
    }
}

std::optional<Callsign> Callsign::parse(std::string_view text) {
    // Trim leading/trailing whitespace
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return std::nullopt;
    }

    auto dash = text.find('-');
    std::string_view base_part = (dash == std::string_view::npos) ? text : text.substr(0, dash);
    uint8_t ssid = 0;

    if (dash != std::string_view::npos) {
        std::string_view ssid_part = text.substr(dash + 1);
        if (ssid_part.empty() || ssid_part.size() > 2) {
            return std::nullopt;
        }
        int val = 0;
        for (unsigned char c : ssid_part) {
            if (!std::isdigit(c)) {
                return std::nullopt;
            }
            val = val * 10 + (c - '0');
        }
        if (val < 0 || val > 15) {
            return std::nullopt;
        }
        ssid = static_cast<uint8_t>(val);
    }

    std::string upper_base = to_upper_ascii(base_part);
    if (!is_valid_base(upper_base)) {
        return std::nullopt;
    }
    return Callsign(std::move(upper_base), ssid);
}

std::optional<Callsign> Callsign::decode_ax25(const uint8_t bytes[7],
                                              bool* c_or_h_bit,
                                              bool* is_last) {
    std::string base;
    base.reserve(6);
    for (int i = 0; i < 6; ++i) {
        char ch = static_cast<char>((bytes[i] >> 1) & 0x7F);
        base.push_back(ch);
    }
    // Strip trailing padding spaces
    while (!base.empty() && base.back() == ' ') {
        base.pop_back();
    }

    std::string upper_base = to_upper_ascii(base);
    if (!is_valid_base(upper_base)) {
        return std::nullopt;
    }

    uint8_t ssid_byte = bytes[6];
    uint8_t ssid = static_cast<uint8_t>((ssid_byte >> 1) & 0x0F);
    if (c_or_h_bit) {
        *c_or_h_bit = (ssid_byte & 0x80) != 0;
    }
    if (is_last) {
        *is_last = (ssid_byte & 0x01) != 0;
    }

    return Callsign(std::move(upper_base), ssid);
}

std::array<uint8_t, 7> Callsign::encode_ax25(bool c_or_h_bit, bool is_last) const {
    std::array<uint8_t, 7> out{};
    for (std::size_t i = 0; i < 6; ++i) {
        char ch = (i < base_.size()) ? base_[i] : ' ';
        out[i] = static_cast<uint8_t>(static_cast<uint8_t>(ch) << 1);
    }
    uint8_t ssid_byte = 0x60; // Reserved bits RR = 11 (AX.25 v2.0)
    if (c_or_h_bit) {
        ssid_byte |= 0x80;
    }
    ssid_byte |= static_cast<uint8_t>((ssid_ & 0x0F) << 1);
    if (is_last) {
        ssid_byte |= 0x01;
    }
    out[6] = ssid_byte;
    return out;
}

std::string Callsign::str() const {
    if (base_.empty()) {
        return {};
    }
    if (ssid_ == 0) {
        return base_;
    }
    return base_ + "-" + std::to_string(ssid_);
}

} // namespace oyk::ax25
