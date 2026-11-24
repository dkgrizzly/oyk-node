// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace oyk::ax25 {

// Computes standard AX.25 / HDLC / X.25 CRC-16-CCITT (reflected poly 0x8408,
// init 0xFFFF, final XOR 0xFFFF). Used as the 2-byte FCS in AXUDP frames.
uint16_t crc16_fcs(const uint8_t* data, std::size_t len) noexcept;

// Verifies that the trailing 2 little-endian bytes of `frame_with_fcs` match
// the CRC-16 FCS of the preceding bytes.
bool verify_fcs(const uint8_t* frame_with_fcs, std::size_t total_len) noexcept;

// Appends the 2-byte little-endian FCS to `buffer`.
void append_fcs(std::vector<uint8_t>& buffer);

} // namespace oyk::ax25
