// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/core/line_ending.hpp"

#include <fstream>
#include <sstream>

namespace oyk::core {

const char* line_ending_str(LineEnding eol) noexcept {
    switch (eol) {
        case LineEnding::LF:   return "\n";
        case LineEnding::CRLF: return "\r\n";
        case LineEnding::CR:   return "\r";
    }
    return "\n";
}

LineEndingInfo inspect_line_endings(std::string_view raw_bytes,
                                    LineEnding fallback) noexcept {
    LineEndingInfo info;
    info.first = fallback;
    info.found_any = false;
    info.mixed = false;

    for (std::size_t i = 0; i < raw_bytes.size(); ++i) {
        LineEnding cur;
        if (raw_bytes[i] == '\r') {
            if (i + 1 < raw_bytes.size() && raw_bytes[i + 1] == '\n') {
                cur = LineEnding::CRLF;
                ++i;
            } else {
                cur = LineEnding::CR;
            }
        } else if (raw_bytes[i] == '\n') {
            cur = LineEnding::LF;
        } else {
            continue;
        }

        if (!info.found_any) {
            info.first = cur;
            info.found_any = true;
        } else if (cur != info.first) {
            info.mixed = true;
        }
    }

    return info;
}

std::vector<std::string> split_lines_any_eol(std::string_view raw_bytes) {
    std::vector<std::string> lines;
    std::size_t start = 0;

    for (std::size_t i = 0; i < raw_bytes.size(); ++i) {
        if (raw_bytes[i] == '\r') {
            lines.emplace_back(raw_bytes.substr(start, i - start));
            if (i + 1 < raw_bytes.size() && raw_bytes[i + 1] == '\n') {
                ++i;
            }
            start = i + 1;
        } else if (raw_bytes[i] == '\n') {
            lines.emplace_back(raw_bytes.substr(start, i - start));
            start = i + 1;
        }
    }

    if (start < raw_bytes.size()) {
        lines.emplace_back(raw_bytes.substr(start));
    }
    return lines;
}

bool read_file_binary(const std::string& path, std::string& out_bytes) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    if (ifs.bad()) {
        return false;
    }
    out_bytes = oss.str();
    return true;
}

bool write_lines_binary(const std::string& path,
                        const std::vector<std::string>& lines,
                        LineEnding eol) {
    std::ofstream ofs(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return false;
    }
    const char* nl = line_ending_str(eol);
    for (const auto& line : lines) {
        ofs << line << nl;
    }
    ofs.flush();
    return ofs.good();
}

} // namespace oyk::core
