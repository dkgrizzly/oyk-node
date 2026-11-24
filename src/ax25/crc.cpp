// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/ax25/crc.hpp"

namespace oyk::ax25 {

uint16_t crc16_fcs(const uint8_t* data, std::size_t len) noexcept {
    uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc = static_cast<uint16_t>((crc >> 1) ^ 0x8408);
            } else {
                crc >>= 1;
            }
        }
    }
    return static_cast<uint16_t>(crc ^ 0xFFFF);
}

bool verify_fcs(const uint8_t* frame_with_fcs, std::size_t total_len) noexcept {
    if (!frame_with_fcs || total_len < 3) {
        return false;
    }
    const std::size_t payload_len = total_len - 2;
    const uint16_t computed = crc16_fcs(frame_with_fcs, payload_len);
    const uint16_t received = static_cast<uint16_t>(
        static_cast<uint16_t>(frame_with_fcs[payload_len]) |
        (static_cast<uint16_t>(frame_with_fcs[payload_len + 1]) << 8));
    return computed == received;
}

void append_fcs(std::vector<uint8_t>& buffer) {
    const uint16_t fcs = crc16_fcs(buffer.data(), buffer.size());
    buffer.push_back(static_cast<uint8_t>(fcs & 0xFF));
    buffer.push_back(static_cast<uint8_t>((fcs >> 8) & 0xFF));
}

} // namespace oyk::ax25
