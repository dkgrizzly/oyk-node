#include "oyk/radio/kiss.hpp"

namespace oyk::radio {

namespace {
void push_escaped(std::vector<uint8_t>& out, uint8_t b) {
    if (b == KISS_FEND) {
        out.push_back(KISS_FESC);
        out.push_back(KISS_TFEND);
    } else if (b == KISS_FESC) {
        out.push_back(KISS_FESC);
        out.push_back(KISS_TFESC);
    } else {
        out.push_back(b);
    }
}
} // namespace

std::vector<uint8_t> kiss_encode(uint8_t port,
                                 const std::vector<uint8_t>& payload,
                                 uint8_t command) {
    std::vector<uint8_t> out;
    out.reserve(payload.size() + 8);
    out.push_back(KISS_FEND);
    uint8_t cmd_byte = static_cast<uint8_t>(((port & 0x0F) << 4) | (command & 0x0F));
    push_escaped(out, cmd_byte);
    for (uint8_t b : payload) {
        push_escaped(out, b);
    }
    out.push_back(KISS_FEND);
    return out;
}

std::vector<uint8_t> kiss_encode_param(uint8_t port, uint8_t command, uint8_t value) {
    return kiss_encode(port, std::vector<uint8_t>{value}, command);
}

void KissDecoder::reset() {
    in_frame_ = false;
    escaped_ = false;
    overflowed_ = false;
    buffer_.clear();
}

std::vector<KissFrame> KissDecoder::feed(const uint8_t* data, std::size_t len) {
    std::vector<KissFrame> frames;
    if (!data || len == 0) return frames;

    for (std::size_t i = 0; i < len; ++i) {
        uint8_t b = data[i];

        if (b == KISS_FEND) {
            if (in_frame_ && !overflowed_ && buffer_.size() >= 1) {
                KissFrame kf;
                uint8_t cmd_byte = buffer_[0];
                kf.port = static_cast<uint8_t>((cmd_byte >> 4) & 0x0F);
                kf.command = static_cast<uint8_t>(cmd_byte & 0x0F);
                kf.payload.assign(buffer_.begin() + 1, buffer_.end());
                frames.push_back(std::move(kf));
            }
            in_frame_ = true;
            escaped_ = false;
            overflowed_ = false;
            buffer_.clear();
            continue;
        }

        if (!in_frame_ || overflowed_) {
            continue;
        }

        if (escaped_) {
            escaped_ = false;
            if (b == KISS_TFEND) {
                b = KISS_FEND;
            } else if (b == KISS_TFESC) {
                b = KISS_FESC;
            }
            // Any other byte after FESC is passed through per KISS spec
        } else if (b == KISS_FESC) {
            escaped_ = true;
            continue;
        }

        if (buffer_.size() < MAX_FRAME_SIZE) {
            buffer_.push_back(b);
        } else {
            overflowed_ = true;
            buffer_.clear();
        }
    }

    return frames;
}

} // namespace oyk::radio
