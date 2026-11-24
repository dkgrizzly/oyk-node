// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/ax25/frame.hpp"

#include <sstream>

namespace oyk::ax25 {

FrameType Frame::type() const noexcept {
    if ((control & 0x01) == 0) {
        return FrameType::I;
    }
    if ((control & 0x03) == 0x01) {
        switch (control & 0x0F) {
            case 0x01: return FrameType::RR;
            case 0x05: return FrameType::RNR;
            case 0x09: return FrameType::REJ;
            default:   return FrameType::Unknown;
        }
    }
    // U-frame: mask out P/F bit (0x10)
    switch (control & 0xEF) {
        case 0x2F: return FrameType::SABM;
        case 0x43: return FrameType::DISC;
        case 0x0F: return FrameType::DM;
        case 0x63: return FrameType::UA;
        case 0x03: return FrameType::UI;
        case 0x87: return FrameType::FRMR;
        default:   return FrameType::Unknown;
    }
}

std::string Frame::type_name() const {
    switch (type()) {
        case FrameType::I:       return "I";
        case FrameType::RR:      return "RR";
        case FrameType::RNR:     return "RNR";
        case FrameType::REJ:     return "REJ";
        case FrameType::SABM:    return "SABM";
        case FrameType::DISC:    return "DISC";
        case FrameType::DM:      return "DM";
        case FrameType::UA:      return "UA";
        case FrameType::UI:      return "UI";
        case FrameType::FRMR:    return "FRMR";
        case FrameType::Unknown: return "UNK";
    }
    return "UNK";
}

std::string Frame::summary() const {
    std::ostringstream oss;
    oss << source.str() << ">" << destination.str();
    for (const auto& digi : digipeaters) {
        oss << "," << digi.call.str() << (digi.repeated ? "*" : "");
    }
    oss << " [" << type_name();
    auto t = type();
    if (t == FrameType::I) {
        oss << " S=" << static_cast<int>(ns())
            << " R=" << static_cast<int>(nr())
            << " len=" << info.size();
    } else if (t == FrameType::RR || t == FrameType::RNR || t == FrameType::REJ) {
        oss << " R=" << static_cast<int>(nr());
    } else if (t == FrameType::UI) {
        oss << " len=" << info.size();
    }
    if (pf()) {
        oss << (is_command ? " P" : " F");
    }
    oss << "]";
    return oss.str();
}

std::optional<Frame> Frame::decode(const uint8_t* data, std::size_t len) {
    // Minimum AX.25 frame: 7 (dst) + 7 (src) + 1 (ctrl) = 15 bytes
    if (!data || len < 15) {
        return std::nullopt;
    }

    Frame frame;
    bool dst_c = false;
    bool dst_last = false;
    auto dst = Callsign::decode_ax25(data, &dst_c, &dst_last);
    if (!dst || dst_last) {
        // Destination cannot be the last address; must have at least source.
        return std::nullopt;
    }
    frame.destination = std::move(*dst);

    bool src_c = false;
    bool is_last = false;
    auto src = Callsign::decode_ax25(data + 7, &src_c, &is_last);
    if (!src) {
        return std::nullopt;
    }
    frame.source = std::move(*src);
    frame.is_command = dst_c && !src_c;
    frame.is_response = !dst_c && src_c;

    std::size_t offset = 14;
    // Up to 8 digipeaters per AX.25 spec
    while (!is_last) {
        if (frame.digipeaters.size() >= 8 || offset + 7 > len) {
            return std::nullopt;
        }
        bool repeated = false;
        auto digi = Callsign::decode_ax25(data + offset, &repeated, &is_last);
        if (!digi) {
            return std::nullopt;
        }
        frame.digipeaters.push_back(Digipeater{std::move(*digi), repeated});
        offset += 7;
    }

    if (offset >= len) {
        return std::nullopt;
    }

    frame.control = data[offset++];
    FrameType ft = frame.type();

    if (ft == FrameType::I || ft == FrameType::UI) {
        if (offset < len) {
            frame.has_pid = true;
            frame.pid = data[offset++];
            frame.info.assign(data + offset, data + len);
        }
    } else if (offset < len) {
        // E.g. FRMR info bytes
        frame.info.assign(data + offset, data + len);
    }

    return frame;
}

std::vector<uint8_t> Frame::encode() const {
    std::vector<uint8_t> out;
    if (!destination.valid() || !source.valid()) {
        return out;
    }

    const bool has_digis = !digipeaters.empty();
    const bool dst_c = is_command && !is_response;
    const bool src_c = is_response && !is_command;

    out.reserve(16 + digipeaters.size() * 7 + info.size());

    auto dst_bytes = destination.encode_ax25(dst_c, false);
    out.insert(out.end(), dst_bytes.begin(), dst_bytes.end());

    auto src_bytes = source.encode_ax25(src_c, !has_digis);
    out.insert(out.end(), src_bytes.begin(), src_bytes.end());

    for (std::size_t i = 0; i < digipeaters.size(); ++i) {
        const bool last = (i + 1 == digipeaters.size());
        auto digi_bytes = digipeaters[i].call.encode_ax25(digipeaters[i].repeated, last);
        out.insert(out.end(), digi_bytes.begin(), digi_bytes.end());
    }

    out.push_back(control);
    if (has_pid) {
        out.push_back(pid);
    }
    if (!info.empty()) {
        out.insert(out.end(), info.begin(), info.end());
    }
    return out;
}

Frame Frame::make_sabm(const Callsign& dst, const Callsign& src, bool pf) {
    Frame f;
    f.destination = dst;
    f.source = src;
    f.is_command = true;
    f.is_response = false;
    f.control = static_cast<uint8_t>(0x2F | (pf ? 0x10 : 0x00));
    return f;
}

Frame Frame::make_disc(const Callsign& dst, const Callsign& src, bool pf) {
    Frame f;
    f.destination = dst;
    f.source = src;
    f.is_command = true;
    f.is_response = false;
    f.control = static_cast<uint8_t>(0x43 | (pf ? 0x10 : 0x00));
    return f;
}

Frame Frame::make_ua(const Callsign& dst, const Callsign& src, bool pf) {
    Frame f;
    f.destination = dst;
    f.source = src;
    f.is_command = false;
    f.is_response = true;
    f.control = static_cast<uint8_t>(0x63 | (pf ? 0x10 : 0x00));
    return f;
}

Frame Frame::make_dm(const Callsign& dst, const Callsign& src, bool pf) {
    Frame f;
    f.destination = dst;
    f.source = src;
    f.is_command = false;
    f.is_response = true;
    f.control = static_cast<uint8_t>(0x0F | (pf ? 0x10 : 0x00));
    return f;
}

Frame Frame::make_rr(const Callsign& dst, const Callsign& src, uint8_t nr,
                     bool is_cmd, bool pf) {
    Frame f;
    f.destination = dst;
    f.source = src;
    f.is_command = is_cmd;
    f.is_response = !is_cmd;
    f.control = static_cast<uint8_t>(0x01 | ((nr & 0x07) << 5) | (pf ? 0x10 : 0x00));
    return f;
}

Frame Frame::make_rnr(const Callsign& dst, const Callsign& src, uint8_t nr,
                      bool is_cmd, bool pf) {
    Frame f;
    f.destination = dst;
    f.source = src;
    f.is_command = is_cmd;
    f.is_response = !is_cmd;
    f.control = static_cast<uint8_t>(0x05 | ((nr & 0x07) << 5) | (pf ? 0x10 : 0x00));
    return f;
}

Frame Frame::make_rej(const Callsign& dst, const Callsign& src, uint8_t nr,
                      bool is_cmd, bool pf) {
    Frame f;
    f.destination = dst;
    f.source = src;
    f.is_command = is_cmd;
    f.is_response = !is_cmd;
    f.control = static_cast<uint8_t>(0x09 | ((nr & 0x07) << 5) | (pf ? 0x10 : 0x00));
    return f;
}

Frame Frame::make_i(const Callsign& dst, const Callsign& src, uint8_t ns,
                    uint8_t nr, const std::vector<uint8_t>& payload,
                    bool pf, uint8_t pid) {
    Frame f;
    f.destination = dst;
    f.source = src;
    f.is_command = true;
    f.is_response = false;
    f.control = static_cast<uint8_t>(((ns & 0x07) << 1) | ((nr & 0x07) << 5) | (pf ? 0x10 : 0x00));
    f.has_pid = true;
    f.pid = pid;
    f.info = payload;
    return f;
}

Frame Frame::make_ui(const Callsign& dst, const Callsign& src,
                     const std::vector<uint8_t>& payload, uint8_t pid) {
    Frame f;
    f.destination = dst;
    f.source = src;
    f.is_command = true;
    f.is_response = false;
    f.control = 0x03;
    f.has_pid = true;
    f.pid = pid;
    f.info = payload;
    return f;
}

} // namespace oyk::ax25
