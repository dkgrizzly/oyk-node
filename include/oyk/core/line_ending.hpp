// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace oyk::core {

enum class LineEnding {
    LF,    // "\n"   (Unix / Linux)
    CRLF,  // "\r\n" (MS-DOS / Windows)
    CR     // "\r"   (Classic Mac / legacy)
};

// Returns CRLF on MS-DOS and Windows targets, LF on POSIX targets.
constexpr LineEnding platform_default_line_ending() noexcept {
#if defined(_WIN32) || defined(_WIN64) || defined(_WIN16) || defined(__WINDOWS__) || \
    defined(__DOS__) || defined(__MSDOS__) || defined(_MSDOS)
    return LineEnding::CRLF;
#else
    return LineEnding::LF;
#endif
}

// Returns the byte sequence for `eol` ("\n", "\r\n", or "\r").
const char* line_ending_str(LineEnding eol) noexcept;

struct LineEndingInfo {
    LineEnding first = platform_default_line_ending();
    bool found_any = false;
    bool mixed = false;
};

// Scans raw file bytes and identifies the first line ending encountered
// (which takes precedence over any mixed line endings later in the file).
LineEndingInfo inspect_line_endings(std::string_view raw_bytes,
                                    LineEnding fallback = platform_default_line_ending()) noexcept;

// Splits raw text accepting CRLF, CR, and LF line endings uniformly.
std::vector<std::string> split_lines_any_eol(std::string_view raw_bytes);

// Reads an entire file in binary mode (preserving exact CR/LF bytes).
bool read_file_binary(const std::string& path, std::string& out_bytes);

// Writes `lines` joined with `eol` (including trailing `eol` after each line) in binary mode.
bool write_lines_binary(const std::string& path,
                        const std::vector<std::string>& lines,
                        LineEnding eol);

} // namespace oyk::core
