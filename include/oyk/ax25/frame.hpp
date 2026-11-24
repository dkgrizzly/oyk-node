// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include "oyk/ax25/callsign.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace oyk::ax25 {

constexpr uint8_t PID_NO_LAYER3 = 0xF0;

enum class FrameType {
    I,     // Information
    RR,    // Receive Ready (S)
    RNR,   // Receive Not Ready (S)
    REJ,   // Reject (S)
    SABM,  // Set Asynchronous Balanced Mode (U)
    DISC,  // Disconnect (U)
    DM,    // Disconnected Mode (U)
    UA,    // Unnumbered Acknowledge (U)
    UI,    // Unnumbered Information (U)
    FRMR,  // Frame Reject (U)
    Unknown
};

struct Digipeater {
    Callsign call;
    bool repeated = false; // H-bit
};

struct Frame {
    Callsign destination;
    Callsign source;
    std::vector<Digipeater> digipeaters;

    // AX.25 v2.0 command/response bits
    bool is_command = true;
    bool is_response = false;

    uint8_t control = 0;
    bool has_pid = false;
    uint8_t pid = PID_NO_LAYER3;
    std::vector<uint8_t> info;

    FrameType type() const noexcept;
    uint8_t ns() const noexcept { return static_cast<uint8_t>((control >> 1) & 0x07); }
    uint8_t nr() const noexcept { return static_cast<uint8_t>((control >> 5) & 0x07); }
    bool pf() const noexcept { return (control & 0x10) != 0; }

    std::string type_name() const;
    std::string summary() const;

    // Decode a raw AX.25 frame (without HDLC flags or trailing FCS).
    static std::optional<Frame> decode(const uint8_t* data, std::size_t len);

    // Encode to a raw AX.25 frame (without trailing FCS).
    std::vector<uint8_t> encode() const;

    // Helper builders (AX.25 v2.0 modulo-8)
    static Frame make_sabm(const Callsign& dst, const Callsign& src, bool pf = true);
    static Frame make_disc(const Callsign& dst, const Callsign& src, bool pf = true);
    static Frame make_ua(const Callsign& dst, const Callsign& src, bool pf = true);
    static Frame make_dm(const Callsign& dst, const Callsign& src, bool pf = true);
    static Frame make_rr(const Callsign& dst, const Callsign& src, uint8_t nr,
                         bool is_cmd, bool pf = false);
    static Frame make_rnr(const Callsign& dst, const Callsign& src, uint8_t nr,
                          bool is_cmd, bool pf = false);
    static Frame make_rej(const Callsign& dst, const Callsign& src, uint8_t nr,
                          bool is_cmd, bool pf = false);
    static Frame make_i(const Callsign& dst, const Callsign& src, uint8_t ns,
                        uint8_t nr, const std::vector<uint8_t>& payload,
                        bool pf = false, uint8_t pid = PID_NO_LAYER3);
    static Frame make_ui(const Callsign& dst, const Callsign& src,
                         const std::vector<uint8_t>& payload,
                         uint8_t pid = PID_NO_LAYER3);
};

} // namespace oyk::ax25
