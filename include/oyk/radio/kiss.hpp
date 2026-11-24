// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace oyk::radio {

constexpr uint8_t KISS_FEND  = 0xC0;
constexpr uint8_t KISS_FESC  = 0xDB;
constexpr uint8_t KISS_TFEND = 0xDC;
constexpr uint8_t KISS_TFESC = 0xDD;

constexpr uint8_t KISS_CMD_DATA        = 0x00;
constexpr uint8_t KISS_CMD_TXDELAY     = 0x01;
constexpr uint8_t KISS_CMD_PERSISTENCE = 0x02;
constexpr uint8_t KISS_CMD_SLOTTIME    = 0x03;
constexpr uint8_t KISS_CMD_TXTAIL      = 0x04;
constexpr uint8_t KISS_CMD_FULLDUPLEX  = 0x05;
constexpr uint8_t KISS_CMD_SETHARDWARE = 0x06;

struct KissFrame {
    uint8_t port = 0;    // 0..15 (high nibble of command byte)
    uint8_t command = 0; // 0..15 (low nibble of command byte; 0 = data)
    std::vector<uint8_t> payload;
};

// Encodes a raw payload (e.g. AX.25 frame without FCS) into a KISS frame.
std::vector<uint8_t> kiss_encode(uint8_t port,
                                 const std::vector<uint8_t>& payload,
                                 uint8_t command = KISS_CMD_DATA);

// Encodes a 1-byte KISS TNC parameter command (TXDELAY, PERSISTENCE, etc.).
std::vector<uint8_t> kiss_encode_param(uint8_t port, uint8_t command, uint8_t value);

// Streaming KISS frame decoder suitable for both serial ports and TCP streams.
class KissDecoder {
public:
    static constexpr std::size_t MAX_FRAME_SIZE = 2048;

    void reset();

    // Feeds raw bytes from a serial/TCP stream and returns any complete decoded KISS frames.
    std::vector<KissFrame> feed(const uint8_t* data, std::size_t len);

private:
    bool in_frame_ = false;
    bool escaped_ = false;
    bool overflowed_ = false;
    std::vector<uint8_t> buffer_;
};

} // namespace oyk::radio
