// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/node/bbs_server.hpp"
#include "oyk/node/logger.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace oyk::node {

namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string sanitize_ascii_line(const std::string& raw, std::size_t max_len) {
    std::string out;
    out.reserve(std::min(raw.size(), max_len));
    for (unsigned char c : raw) {
        if (c >= 0x20 && c <= 0x7E) {
            out.push_back(static_cast<char>(c));
            if (out.size() >= max_len) break;
        }
    }
    return out;
}

std::string sanitize_token(const std::string& raw, std::size_t max_len) {
    std::string out;
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') {
            out.push_back(static_cast<char>(std::toupper(c)));
            if (out.size() >= max_len) break;
        }
    }
    return out;
}

std::string format_compact_timestamp(std::chrono::system_clock::time_point tp) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
    localtime_r(&tt, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y%m%d-%H%M%S");
    return oss.str();
}

std::string format_short_date(const std::string& compact) {
    // Convert YYYYMMDD-HHMMSS -> MM/DD HH:MM for compact 80-col listing
    if (compact.size() >= 13) {
        return compact.substr(4, 2) + "/" + compact.substr(6, 2) + " " +
               compact.substr(9, 2) + ":" + compact.substr(11, 2);
    }
    return compact;
}

std::vector<std::string> split_ws(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> out;
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

bool parse_u64(const std::string& s, uint64_t& out) {
    if (s.empty()) return false;
    uint64_t acc = 0;
    for (unsigned char c : s) {
        if (!std::isdigit(c)) return false;
        if (acc > 1000000000ULL) return false;
        acc = acc * 10ULL + static_cast<uint64_t>(c - '0');
    }
    out = acc;
    return acc > 0;
}

bool parse_send_target(const std::vector<std::string>& tokens,
                       std::size_t start_idx,
                       std::string& to_out,
                       std::string& at_out) {
    to_out.clear();
    at_out.clear();
    if (start_idx >= tokens.size()) return false;

    std::string first = tokens[start_idx];
    auto at_pos = first.find('@');
    if (at_pos != std::string::npos) {
        to_out = sanitize_token(first.substr(0, at_pos), 12);
        at_out = sanitize_token(first.substr(at_pos + 1), 16);
    } else {
        to_out = sanitize_token(first, 12);
        if (start_idx + 1 < tokens.size()) {
            std::string next = tokens[start_idx + 1];
            if (next == "@" && start_idx + 2 < tokens.size()) {
                at_out = sanitize_token(tokens[start_idx + 2], 16);
            } else if (!next.empty() && next[0] == '@') {
                at_out = sanitize_token(next.substr(1), 16);
            }
        }
    }
    return !to_out.empty();
}

} // namespace

std::string BbsServer::base_call(const std::string& call) {
    std::string up = to_upper(trim(call));
    auto dash = up.find('-');
    if (dash != std::string::npos) {
        return up.substr(0, dash);
    }
    return up;
}

bool BbsServer::call_matches(const std::string& target, const BbsSessionState& st) {
    std::string up = to_upper(trim(target));
    if (up.empty()) return false;
    if (up == st.callsign || up == st.base_callsign) return true;
    return base_call(up) == st.base_callsign && up.find('-') == std::string::npos;
}

bool BbsServer::can_read(const BbsMessage& msg, const BbsSessionState& st) const {
    if (msg.status == BbsMsgStatus::Killed) return false;
    if (msg.type != BbsMsgType::Personal) return true;
    if (st.is_sysop) return true;
    return call_matches(msg.to, st) || call_matches(msg.from, st);
}

bool BbsServer::can_kill(const BbsMessage& msg, const BbsSessionState& st) const {
    if (msg.status == BbsMsgStatus::Killed) return false;
    if (st.is_sysop) return true;
    if (call_matches(msg.from, st)) return true;
    if (msg.type == BbsMsgType::Personal && call_matches(msg.to, st)) return true;
    return false;
}

std::string BbsServer::prompt_string() const {
    return "de " + bbs_callsign_ + "> ";
}

std::string BbsServer::message_file_path(uint64_t id) const {
    namespace fs = std::filesystem;
    return (fs::path(mail_path_) / ("M" + std::to_string(id) + ".MSG")).string();
}

bool BbsServer::configure(std::string bbs_callsign,
                          std::string bbs_alias,
                          std::string mail_path,
                          std::string files_path,
                          std::string incoming_root,
                          std::string welcome,
                          std::size_t max_msg_bytes,
                          std::string& error_out) {
    const std::string new_mail_path = mail_path.empty() ? "mail" : std::move(mail_path);
    const bool path_changed = (!loaded_once_ || new_mail_path != mail_path_);
    if (!bbs_callsign.empty()) bbs_callsign_ = std::move(bbs_callsign);
    bbs_alias_ = std::move(bbs_alias);
    mail_path_ = new_mail_path;
    files_path_ = files_path.empty() ? "files" : std::move(files_path);
    incoming_root_ = incoming_root.empty() ? "incoming" : std::move(incoming_root);
    if (!welcome.empty()) welcome_ = std::move(welcome);
    max_msg_bytes_ = std::clamp<std::size_t>(max_msg_bytes, 256, 65536);

    if (!path_changed) {
        return true;
    }

    namespace fs = std::filesystem;
    messages_.clear();
    next_msg_id_ = 1;
    loaded_once_ = true;
    std::error_code ec;
    if (!fs::exists(mail_path_, ec)) {
        return true;
    }
    return load_messages_from_disk(error_out);
}

bool BbsServer::load_messages_from_disk(std::string& error_out) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(mail_path_, ec)) {
        if (ec) {
            error_out = "Failed to scan BBS directory '" + mail_path_ + "': " + ec.message();
            return false;
        }
        if (!entry.is_regular_file()) continue;
        std::string fname = to_upper(entry.path().filename().string());
        if (fname.size() < 6 || fname.front() != 'M' ||
            fname.rfind(".MSG") != fname.size() - 4) {
            continue;
        }
        uint64_t file_id = 0;
        if (!parse_u64(fname.substr(1, fname.size() - 5), file_id)) continue;

        std::ifstream ifs(entry.path());
        if (!ifs.is_open()) continue;

        BbsMessage msg;
        msg.id = file_id;
        std::string line;
        bool in_body = false;
        std::ostringstream body_oss;
        while (std::getline(ifs, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!in_body) {
                if (line == "---") {
                    in_body = true;
                    continue;
                }
                auto colon = line.find(':');
                if (colon == std::string::npos) continue;
                std::string k = to_upper(trim(line.substr(0, colon)));
                std::string v = trim(line.substr(colon + 1));
                if (k == "ID") {
                    uint64_t parsed_id = 0;
                    if (parse_u64(v, parsed_id)) msg.id = parsed_id;
                } else if (k == "TYPE" && !v.empty()) {
                    char tc = static_cast<char>(std::toupper(static_cast<unsigned char>(v[0])));
                    if (tc == 'B') msg.type = BbsMsgType::Bulletin;
                    else if (tc == 'T') msg.type = BbsMsgType::Traffic;
                    else msg.type = BbsMsgType::Personal;
                } else if (k == "STATUS" && !v.empty()) {
                    char sc = static_cast<char>(std::toupper(static_cast<unsigned char>(v[0])));
                    if (sc == 'Y') msg.status = BbsMsgStatus::Read;
                    else if (sc == '$') msg.status = BbsMsgStatus::Bulletin;
                    else if (sc == 'K') msg.status = BbsMsgStatus::Killed;
                    else msg.status = BbsMsgStatus::Unread;
                } else if (k == "FROM") {
                    msg.from = to_upper(v);
                } else if (k == "TO") {
                    msg.to = to_upper(v);
                } else if (k == "AT") {
                    msg.at_bbs = to_upper(v);
                } else if (k == "DATE") {
                    msg.date = v;
                } else if (k == "SUBJECT") {
                    msg.subject = v;
                }
            } else {
                body_oss << line << "\n";
            }
        }
        msg.body = body_oss.str();
        if (msg.id >= next_msg_id_) {
            next_msg_id_ = msg.id + 1;
        }
        if (msg.status != BbsMsgStatus::Killed && !msg.from.empty() && !msg.to.empty()) {
            messages_[msg.id] = std::move(msg);
        }
    }
    return true;
}

bool BbsServer::save_message_to_disk(const BbsMessage& msg) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(mail_path_, ec);
    if (ec) return false;
    std::ofstream ofs(message_file_path(msg.id), std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs << "ID: " << msg.id << "\n"
        << "TYPE: " << static_cast<char>(msg.type) << "\n"
        << "STATUS: " << static_cast<char>(msg.status) << "\n"
        << "FROM: " << msg.from << "\n"
        << "TO: " << msg.to << "\n"
        << "AT: " << msg.at_bbs << "\n"
        << "DATE: " << msg.date << "\n"
        << "SUBJECT: " << msg.subject << "\n"
        << "---\n"
        << msg.body;
    return ofs.good();
}

bool BbsServer::has_session(uint64_t session_id) const {
    return sessions_.count(session_id) > 0;
}

bool BbsServer::is_yapp_active(uint64_t session_id) const {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return false;
    return it->second.yapp_active;
}

namespace {

bool path_is_within_root(const std::filesystem::path& root_canon,
                         const std::filesystem::path& cand_canon) {
    auto rit = root_canon.begin();
    auto cit = cand_canon.begin();
    for (; rit != root_canon.end(); ++rit, ++cit) {
        if (cit == cand_canon.end() || *rit != *cit) {
            return false;
        }
    }
    return true;
}

} // namespace

bool BbsServer::resolve_safe_files_path(const std::string& files_root,
                                        const std::string& current_rel_dir,
                                        const std::string& user_arg,
                                        bool allow_parent_in_root,
                                        std::string& resolved_abs_out,
                                        std::string& resolved_rel_out,
                                        std::string& error_out) {
    namespace fs = std::filesystem;
    resolved_abs_out.clear();
    resolved_rel_out.clear();

    for (unsigned char c : user_arg) {
        if (c < 0x20 || c >= 0x7F) {
            error_out = "Access denied: invalid characters in path.";
            return false;
        }
        if (c == ':') {
            error_out = "Access denied: drive specifiers are not permitted.";
            return false;
        }
        if (c == '\\') {
            error_out = "Access denied: backslashes are not permitted in BBS paths.";
            return false;
        }
    }

    std::vector<std::string> stack;
    auto push_rel = [&](const std::string& rel) {
        std::size_t i = 0;
        while (i < rel.size()) {
            while (i < rel.size() && rel[i] == '/') ++i;
            std::size_t j = i;
            while (j < rel.size() && rel[j] != '/') ++j;
            if (j > i) {
                std::string seg = rel.substr(i, j - i);
                if (seg != "." && seg != "..") stack.push_back(seg);
            }
            i = j;
        }
    };

    std::string arg = trim(user_arg);
    if (!arg.empty() && arg.front() == '/') {
        // Virtual root of the BBS files area (never the host filesystem root)
        stack.clear();
    } else {
        push_rel(current_rel_dir);
    }

    std::size_t i = 0;
    while (i < arg.size()) {
        while (i < arg.size() && arg[i] == '/') ++i;
        std::size_t j = i;
        while (j < arg.size() && arg[j] != '/') ++j;
        if (j > i) {
            std::string seg = arg.substr(i, j - i);
            if (seg == ".") {
                // Current directory inside files root
            } else if (seg == "..") {
                if (!allow_parent_in_root) {
                    error_out = "Access denied: directory traversal is not permitted.";
                    return false;
                }
                if (stack.empty()) {
                    error_out = "Access denied: cannot navigate above BBS files area root.";
                    return false;
                }
                stack.pop_back();
            } else {
                if (!protocol::is_safe_yapp_filename(seg)) {
                    error_out = "Access denied: invalid path component '" + seg + "'.";
                    return false;
                }
                stack.push_back(seg);
            }
        }
        i = j;
    }

    std::error_code ec;
    fs::path root_abs = fs::absolute(fs::path(files_root.empty() ? "files" : files_root), ec);
    if (ec) {
        error_out = "Unable to resolve BBS files area root.";
        return false;
    }
    fs::path root_canon = fs::weakly_canonical(root_abs, ec);
    if (ec) root_canon = root_abs.lexically_normal();

    fs::path cand = root_canon;
    std::string rel_joined;
    for (const auto& seg : stack) {
        cand /= seg;
        if (!rel_joined.empty()) rel_joined.push_back('/');
        rel_joined += seg;
    }

    fs::path cand_canon = fs::weakly_canonical(cand, ec);
    if (ec) {
        error_out = "Access denied: unable to validate target path.";
        return false;
    }
    if (!path_is_within_root(root_canon, cand_canon)) {
        error_out = "Access denied: target escapes BBS files area root.";
        return false;
    }

    resolved_abs_out = cand_canon.string();
    resolved_rel_out = rel_joined;
    return true;
}

bool BbsServer::resolve_files_path_with_case(const std::string& files_root,
                                             const std::string& current_rel_dir,
                                             const std::string& user_arg,
                                             bool allow_parent_in_root,
                                             bool case_insensitive,
                                             std::string& resolved_abs_out,
                                             std::string& resolved_rel_out,
                                             std::string& error_out) {
    namespace fs = std::filesystem;
    if (!resolve_safe_files_path(files_root, current_rel_dir, user_arg, allow_parent_in_root,
                                 resolved_abs_out, resolved_rel_out, error_out)) {
        return false;
    }

    // 1. Exact case-sensitive match first
    std::error_code ec;
    if (resolved_rel_out.empty() || fs::exists(resolved_abs_out, ec)) {
        return true;
    }
    if (!case_insensitive) {
        return true;
    }

    auto to_lower_str = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };

    // 2. Forced UPPERCASE match
    {
        std::string up_arg = to_upper(resolved_rel_out);
        std::string cand_abs, cand_rel, dummy_err;
        if (resolve_safe_files_path(files_root, "", up_arg, false, cand_abs, cand_rel, dummy_err) &&
            fs::exists(cand_abs, ec) && !ec) {
            resolved_abs_out = std::move(cand_abs);
            resolved_rel_out = std::move(cand_rel);
            return true;
        }
    }

    // 3. Forced lowercase match
    {
        std::string low_arg = to_lower_str(resolved_rel_out);
        std::string cand_abs, cand_rel, dummy_err;
        if (resolve_safe_files_path(files_root, "", low_arg, false, cand_abs, cand_rel, dummy_err) &&
            fs::exists(cand_abs, ec) && !ec) {
            resolved_abs_out = std::move(cand_abs);
            resolved_rel_out = std::move(cand_rel);
            return true;
        }
    }

    // 4. Case-insensitive glob / directory scan segment-by-segment inside files_root
    fs::path root_abs = fs::absolute(fs::path(files_root.empty() ? "files" : files_root), ec);
    if (ec) return true;
    fs::path root_canon = fs::weakly_canonical(root_abs, ec);
    if (ec) root_canon = root_abs.lexically_normal();

    std::vector<std::string> segs;
    {
        std::size_t pos = 0;
        while (pos < resolved_rel_out.size()) {
            std::size_t slash = resolved_rel_out.find('/', pos);
            if (slash == std::string::npos) {
                segs.push_back(resolved_rel_out.substr(pos));
                break;
            }
            if (slash > pos) segs.push_back(resolved_rel_out.substr(pos, slash - pos));
            pos = slash + 1;
        }
    }

    fs::path cur = root_canon;
    std::string matched_rel;
    bool all_matched = !segs.empty();
    for (const auto& seg : segs) {
        if (!fs::is_directory(cur, ec) || ec) {
            all_matched = false;
            break;
        }
        std::string want_low = to_lower_str(seg);
        std::vector<std::string> matches;
        for (const auto& ent : fs::directory_iterator(cur, ec)) {
            if (ec) break;
            std::string ename = ent.path().filename().string();
            if (to_lower_str(ename) == want_low) {
                matches.push_back(ename);
            }
        }
        if (matches.empty()) {
            all_matched = false;
            break;
        }
        std::sort(matches.begin(), matches.end());
        fs::path next = cur / matches.front();
        fs::path next_canon = fs::weakly_canonical(next, ec);
        if (ec || !path_is_within_root(root_canon, next_canon)) {
            all_matched = false;
            break;
        }
        cur = next_canon;
        if (!matched_rel.empty()) matched_rel.push_back('/');
        matched_rel += matches.front();
    }

    if (all_matched && fs::exists(cur, ec) && !ec) {
        resolved_abs_out = cur.string();
        resolved_rel_out = matched_rel;
    }
    return true;
}

std::vector<PendingIncomingFile> BbsServer::list_pending_incoming(const std::string& incoming_root) {
    namespace fs = std::filesystem;
    std::vector<PendingIncomingFile> out;
    std::error_code ec;
    fs::path root(incoming_root.empty() ? "incoming" : incoming_root);
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        return out;
    }

    for (const auto& dir_ent : fs::directory_iterator(root, ec)) {
        if (ec) break;
        if (!dir_ent.is_directory(ec) || ec) continue;
        std::string uploader = dir_ent.path().filename().string();
        if (!protocol::is_safe_yapp_filename(uploader)) continue;

        for (const auto& file_ent : fs::directory_iterator(dir_ent.path(), ec)) {
            if (ec) break;
            if (!file_ent.is_regular_file(ec) || ec) continue;
            std::string fname = file_ent.path().filename().string();
            if (!protocol::is_safe_yapp_filename(fname)) continue;
            PendingIncomingFile pf;
            pf.uploader = uploader;
            pf.filename = fname;
            pf.full_path = file_ent.path().string();
            pf.size_bytes = file_ent.file_size(ec);
            if (ec) pf.size_bytes = 0;
            out.push_back(std::move(pf));
        }
    }

    std::sort(out.begin(), out.end(), [](const PendingIncomingFile& a, const PendingIncomingFile& b) {
        if (a.uploader != b.uploader) return a.uploader < b.uploader;
        return a.filename < b.filename;
    });
    return out;
}

bool BbsServer::approve_incoming_file(const std::string& incoming_root,
                                      const std::string& files_root,
                                      const std::string& uploader,
                                      const std::string& filename,
                                      const std::string& dest_subdir,
                                      bool allow_overwrite,
                                      std::string& dest_path_out,
                                      bool& already_exists_out,
                                      std::string& error_out) {
    namespace fs = std::filesystem;
    dest_path_out.clear();
    already_exists_out = false;

    if (!protocol::is_safe_yapp_filename(uploader) || !protocol::is_safe_yapp_filename(filename)) {
        error_out = "Invalid uploader or filename.";
        return false;
    }

    std::error_code ec;
    fs::path inc_root = fs::weakly_canonical(
        fs::absolute(fs::path(incoming_root.empty() ? "incoming" : incoming_root), ec), ec);
    fs::path src_path = fs::weakly_canonical(inc_root / uploader / filename, ec);
    if (ec || !path_is_within_root(inc_root, src_path) || !fs::is_regular_file(src_path, ec)) {
        error_out = "Incoming file not found or invalid.";
        return false;
    }

    std::string dest_dir_abs;
    std::string dest_dir_rel;
    if (!resolve_safe_files_path(files_root, "", dest_subdir, false,
                                 dest_dir_abs, dest_dir_rel, error_out)) {
        return false;
    }

    fs::create_directories(dest_dir_abs, ec);
    if (ec) {
        error_out = "Unable to create files area directory: " + ec.message();
        return false;
    }

    fs::path root_canon = fs::weakly_canonical(
        fs::absolute(fs::path(files_root.empty() ? "files" : files_root), ec), ec);
    fs::path dest_file = fs::weakly_canonical(fs::path(dest_dir_abs) / filename, ec);
    if (ec || !path_is_within_root(root_canon, dest_file)) {
        error_out = "Access denied: destination escapes BBS files area.";
        return false;
    }

    if (fs::exists(dest_file, ec)) {
        if (!allow_overwrite) {
            already_exists_out = true;
            dest_path_out = dest_file.string();
            error_out = "File already exists in files area: " + dest_file.string();
            return false;
        }
        fs::remove(dest_file, ec);
        if (ec) {
            error_out = "Unable to overwrite existing file: " + ec.message();
            return false;
        }
    }

    fs::rename(src_path, dest_file, ec);
    if (ec) {
        ec.clear();
        fs::copy_file(src_path, dest_file, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error_out = "Failed to move file to files area: " + ec.message();
            return false;
        }
        fs::remove(src_path, ec);
    }

    dest_path_out = dest_file.string();
    return true;
}

bool BbsServer::delete_incoming_file(const std::string& incoming_root,
                                     const std::string& uploader,
                                     const std::string& filename,
                                     std::string& error_out) {
    namespace fs = std::filesystem;
    if (!protocol::is_safe_yapp_filename(uploader) || !protocol::is_safe_yapp_filename(filename)) {
        error_out = "Invalid uploader or filename.";
        return false;
    }
    std::error_code ec;
    fs::path inc_root = fs::weakly_canonical(
        fs::absolute(fs::path(incoming_root.empty() ? "incoming" : incoming_root), ec), ec);
    fs::path src_path = fs::weakly_canonical(inc_root / uploader / filename, ec);
    if (ec || !path_is_within_root(inc_root, src_path) || !fs::is_regular_file(src_path, ec)) {
        error_out = "Incoming file not found.";
        return false;
    }
    if (!fs::remove(src_path, ec) || ec) {
        error_out = "Failed to delete incoming file: " + ec.message();
        return false;
    }
    return true;
}

std::vector<ApprovedFileEntry> BbsServer::list_approved_files(const std::string& files_root) {
    namespace fs = std::filesystem;
    std::vector<ApprovedFileEntry> out;
    std::error_code ec;
    fs::path root_abs = fs::absolute(fs::path(files_root.empty() ? "files" : files_root), ec);
    if (ec || !fs::exists(root_abs, ec) || !fs::is_directory(root_abs, ec)) {
        return out;
    }
    fs::path root_canon = fs::weakly_canonical(root_abs, ec);
    if (ec) root_canon = root_abs.lexically_normal();

    auto walk = [&](auto& self, const fs::path& dir, const std::string& rel_prefix, int depth) -> void {
        if (depth > 8) return;
        std::error_code lec;
        for (const auto& ent : fs::directory_iterator(dir, lec)) {
            if (lec) break;
            std::string name = ent.path().filename().string();
            if (name.empty() || name == "." || name == "..") continue;
            fs::path cand_canon = fs::weakly_canonical(ent.path(), lec);
            if (lec || !path_is_within_root(root_canon, cand_canon)) continue;
            std::string rel = rel_prefix.empty() ? name : (rel_prefix + "/" + name);
            if (ent.is_directory(lec) && !lec && !ent.is_symlink(lec)) {
                self(self, ent.path(), rel, depth + 1);
            } else if (ent.is_regular_file(lec) && !lec) {
                ApprovedFileEntry afe;
                afe.rel_path = rel;
                afe.full_path = cand_canon.string();
                afe.size_bytes = ent.file_size(lec);
                if (lec) afe.size_bytes = 0;
                out.push_back(std::move(afe));
            }
        }
    };
    walk(walk, root_canon, "", 0);

    std::sort(out.begin(), out.end(), [](const ApprovedFileEntry& a, const ApprovedFileEntry& b) {
        return a.rel_path < b.rel_path;
    });
    return out;
}

bool BbsServer::delete_approved_file(const std::string& files_root,
                                     const std::string& rel_path,
                                     std::string& error_out) {
    namespace fs = std::filesystem;
    if (rel_path.empty() || rel_path.find("..") != std::string::npos ||
        rel_path.find('\\') != std::string::npos || rel_path.find(':') != std::string::npos) {
        error_out = "Access denied: invalid file path.";
        return false;
    }

    std::error_code ec;
    fs::path root_abs = fs::absolute(fs::path(files_root.empty() ? "files" : files_root), ec);
    if (ec) {
        error_out = "Unable to resolve BBS files area root.";
        return false;
    }
    fs::path root_canon = fs::weakly_canonical(root_abs, ec);
    if (ec) root_canon = root_abs.lexically_normal();

    fs::path cand = root_canon;
    std::size_t pos = 0;
    int seg_count = 0;
    while (pos < rel_path.size()) {
        while (pos < rel_path.size() && rel_path[pos] == '/') ++pos;
        if (pos >= rel_path.size()) break;
        std::size_t slash = rel_path.find('/', pos);
        std::string seg = (slash == std::string::npos) ? rel_path.substr(pos) : rel_path.substr(pos, slash - pos);
        if (seg.empty() || seg == "." || seg == "..") {
            error_out = "Access denied: invalid path component.";
            return false;
        }
        cand /= seg;
        ++seg_count;
        pos = (slash == std::string::npos) ? rel_path.size() : (slash + 1);
    }
    if (seg_count == 0) {
        error_out = "No file specified.";
        return false;
    }

    fs::path target_canon = fs::weakly_canonical(cand, ec);
    if (ec || !path_is_within_root(root_canon, target_canon) || target_canon == root_canon) {
        error_out = "Access denied: target escapes BBS files area.";
        return false;
    }
    if (!fs::is_regular_file(target_canon, ec) || ec) {
        error_out = "Approved file not found.";
        return false;
    }
    if (!fs::remove(target_canon, ec) || ec) {
        error_out = "Failed to remove approved file: " + ec.message();
        return false;
    }
    return true;
}

std::size_t BbsServer::active_message_count() const {
    std::size_t n = 0;
    for (const auto& [_, m] : messages_) {
        if (m.status != BbsMsgStatus::Killed) ++n;
    }
    return n;
}

std::size_t BbsServer::unread_count_for(const std::string& callsign) const {
    BbsSessionState probe;
    probe.callsign = to_upper(trim(callsign));
    probe.base_callsign = base_call(probe.callsign);
    std::size_t n = 0;
    for (const auto& [_, m] : messages_) {
        if (m.type == BbsMsgType::Personal &&
            m.status == BbsMsgStatus::Unread &&
            call_matches(m.to, probe)) {
            ++n;
        }
    }
    return n;
}

BbsActionResult BbsServer::enter(uint64_t session_id,
                                 const std::string& callsign,
                                 const std::string& interface_id,
                                 bool direct_connect,
                                 bool is_sysop,
                                 bool is_local_loopback,
                                 std::chrono::system_clock::time_point /*now*/) {
    BbsSessionState st;
    st.session_id = session_id;
    st.callsign = to_upper(trim(callsign));
    st.base_callsign = base_call(st.callsign);
    if (auto parsed = ax25::Callsign::parse(st.callsign)) {
        st.parsed_call = *parsed;
    } else {
        st.parsed_call = ax25::Callsign("UNKNOWN", 0);
    }
    st.interface_id = interface_id;
    st.direct_connect = direct_connect;
    st.is_sysop = is_sysop;
    st.is_local_loopback = is_local_loopback;
    sessions_[session_id] = std::move(st);

    const BbsSessionState& ref = sessions_[session_id];
    const std::size_t unread = unread_count_for(ref.callsign);
    const std::size_t total = active_message_count();

    std::ostringstream oss;
    oss << "[OYK-0.1-HMR$]\r"
        << "*** " << bbs_callsign_;
    if (!bbs_alias_.empty()) oss << " (" << bbs_alias_ << ")";
    oss << " Packet BBS ***\r"
        << welcome_ << "\r"
        << "Hello " << ref.base_callsign
        << ". Active msgs: " << total
        << " | Unread personal mail: " << unread << "\r";
    if (unread > 0) {
        oss << "*** You have " << unread << " unread personal message(s). Type LN or RM to read.\r";
    }
    oss << prompt_string();

    BbsActionResult res;
    res.response_text = oss.str();
    return res;
}

void BbsServer::leave(uint64_t session_id) {
    sessions_.erase(session_id);
}

std::string BbsServer::format_files_listing(const std::string& abs_dir,
                                            const std::string& rel_dir) const {
    namespace fs = std::filesystem;
    std::ostringstream oss;
    oss << "Files Area: /" << rel_dir << "\r";
    std::error_code ec;
    if (!fs::exists(abs_dir, ec) || !fs::is_directory(abs_dir, ec)) {
        oss << "(empty - no approved files yet)\r";
        return oss.str();
    }

    fs::path root_canon = fs::weakly_canonical(fs::absolute(fs::path(files_path_), ec), ec);

    struct FileRow {
        std::string name;
        bool is_dir = false;
        uint64_t size = 0;
    };
    std::vector<FileRow> rows;
    for (const auto& entry : fs::directory_iterator(abs_dir, ec)) {
        if (ec) break;
        std::string fname = entry.path().filename().string();
        if (!protocol::is_safe_yapp_filename(fname)) continue;
        fs::path ent_canon = fs::weakly_canonical(entry.path(), ec);
        if (ec || !path_is_within_root(root_canon, ent_canon)) continue;
        FileRow r;
        r.name = fname;
        r.is_dir = entry.is_directory(ec);
        if (!r.is_dir) {
            if (!entry.is_regular_file(ec)) continue;
            r.size = entry.file_size(ec);
            if (ec) r.size = 0;
        }
        rows.push_back(std::move(r));
    }

    std::sort(rows.begin(), rows.end(), [](const FileRow& a, const FileRow& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return a.name < b.name;
    });

    if (rows.empty()) {
        oss << "(no files in this directory)\r";
        return oss.str();
    }

    oss << "Name                             Size\r"
        << "-------------------------------- ----------\r";
    for (const auto& r : rows) {
        oss << std::left << std::setw(32) << r.name.substr(0, 32) << " ";
        if (r.is_dir) {
            oss << "<DIR>\r";
        } else {
            oss << std::right << std::setw(10) << r.size << "\r";
        }
    }
    oss << "Use: CD <dir>, TYPE <file>, GET <file> (YAPP download), UPLOAD (YAPP upload)\r";
    return oss.str();
}

BbsActionResult BbsServer::handle_raw_bytes(uint64_t session_id,
                                            const std::vector<uint8_t>& data) {
    BbsActionResult res;
    auto it = sessions_.find(session_id);
    if (it == sessions_.end() || !it->second.yapp_active || !it->second.yapp) return res;
    BbsSessionState& st = it->second;

    std::vector<uint8_t> tx_accum;
    st.yapp->set_send_callback([&tx_accum](const std::vector<uint8_t>& pkt) {
        tx_accum.insert(tx_accum.end(), pkt.begin(), pkt.end());
    });

    std::vector<uint8_t> plain;
    st.yapp->feed_bytes(data.data(), data.size(), plain);
    res.tx_bytes = std::move(tx_accum);

    auto prog = st.yapp->progress();
    if (prog.state == protocol::YappState::Complete ||
        prog.state == protocol::YappState::Aborted ||
        prog.state == protocol::YappState::Error) {
        st.yapp_active = false;
        bool was_upload = st.yapp_expect_upload;
        st.yapp_expect_upload = false;

        std::ostringstream oss;
        oss << "\r*** " << prog.status_message << "\r";
        if (was_upload && prog.state == protocol::YappState::Complete) {
            oss << "*** File stored in incoming queue pending sysop approval.\r";
            res.log_info = "BBS incoming upload from " + st.callsign + ": " + prog.target_path +
                           " (" + std::to_string(prog.bytes_transferred) +
                           " bytes) - pending sysop approval";
        }
        oss << prompt_string();
        res.response_text = oss.str();
        st.yapp->reset();
    } else if (!st.yapp->is_active() && !plain.empty()) {
        // User typed Ctrl-X / abort text while waiting to begin upload
        for (uint8_t b : plain) {
            if (b == protocol::YAPP_CAN || b == 0x03) {
                st.yapp_active = false;
                st.yapp_expect_upload = false;
                st.yapp->reset();
                res.response_text = "\r*** YAPP upload cancelled.\r" + prompt_string();
                break;
            }
        }
    }
    return res;
}

std::string BbsServer::format_list(const std::vector<const BbsMessage*>& msgs) const {
    if (msgs.empty()) {
        return "No matching messages.\r";
    }
    std::ostringstream oss;
    oss << "Msg#  TS Size  To       @BBS     From     Date        Subject\r"
        << "----- -- ----- -------- -------- -------- ----------- --------------------\r";
    for (const BbsMessage* m : msgs) {
        std::string ts;
        ts.push_back(static_cast<char>(m->type));
        ts.push_back(static_cast<char>(m->status));
        std::string subj = m->subject.size() > 24 ? m->subject.substr(0, 24) : m->subject;
        oss << std::left
            << std::setw(5) << m->id << " "
            << std::setw(2) << ts << " "
            << std::right << std::setw(5) << m->body.size() << " "
            << std::left
            << std::setw(8) << m->to.substr(0, 8) << " "
            << std::setw(8) << m->at_bbs.substr(0, 8) << " "
            << std::setw(8) << m->from.substr(0, 8) << " "
            << std::setw(11) << format_short_date(m->date) << " "
            << subj << "\r";
    }
    return oss.str();
}

std::string BbsServer::format_message(const BbsMessage& msg) const {
    std::ostringstream oss;
    oss << "------------------------------------------------------------------------\r"
        << "Msg #" << msg.id
        << " [" << static_cast<char>(msg.type) << static_cast<char>(msg.status) << "]"
        << "  Date: " << msg.date << "\r"
        << "From : " << msg.from
        << "   To: " << msg.to;
    if (!msg.at_bbs.empty()) oss << " @ " << msg.at_bbs;
    oss << "\r"
        << "Subj : " << msg.subject << "\r"
        << "------------------------------------------------------------------------\r";
    std::istringstream iss(msg.body);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        oss << line << "\r";
    }
    oss << "------------------------------------------------------------------------\r";
    return oss.str();
}

BbsActionResult BbsServer::handle_line(uint64_t session_id,
                                       const std::string& raw_line,
                                       std::chrono::system_clock::time_point now) {
    BbsActionResult res;
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return res;
    BbsSessionState& st = it->second;

    // 1. Multi-line composition: Subject prompt
    if (st.stage == ComposeStage::Subject) {
        std::string subj = sanitize_ascii_line(trim(raw_line), 64);
        if (to_upper(subj) == "/AB" || to_upper(subj) == "/ABORT") {
            st.stage = ComposeStage::Command;
            res.response_text = "Message aborted.\r" + prompt_string();
            return res;
        }
        st.draft_subject = subj.empty() ? "(no subject)" : subj;
        st.draft_body.clear();
        st.stage = ComposeStage::Body;
        res.response_text = "Enter message text. End with /EX or Ctrl-Z on a new line (/AB to abort):\r";
        return res;
    }

    // 2. Multi-line composition: Message body
    if (st.stage == ComposeStage::Body) {
        std::string trimmed = trim(raw_line);
        std::string up = to_upper(trimmed);
        if (up == "/AB" || up == "/ABORT") {
            st.stage = ComposeStage::Command;
            st.draft_body.clear();
            res.response_text = "Message aborted.\r" + prompt_string();
            return res;
        }
        if (up == "/EX" || trimmed == "." || raw_line == "\x1A") {
            BbsMessage msg;
            msg.id = next_msg_id_++;
            msg.type = st.draft_type;
            msg.status = (msg.type == BbsMsgType::Bulletin) ? BbsMsgStatus::Bulletin : BbsMsgStatus::Unread;
            msg.from = st.base_callsign;
            msg.to = st.draft_to;
            msg.at_bbs = st.draft_at;
            msg.date = format_compact_timestamp(now);
            msg.subject = st.draft_subject;
            msg.body = st.draft_body;

            messages_[msg.id] = msg;
            bool ok = save_message_to_disk(msg);
            st.stage = ComposeStage::Command;
            st.draft_body.clear();

            std::ostringstream oss;
            if (ok) {
                oss << "Message #" << msg.id << " saved (" << msg.body.size()
                    << " bytes) for " << msg.to;
                if (!msg.at_bbs.empty()) oss << " @ " << msg.at_bbs;
                oss << ".\r";
            } else {
                oss << "Message #" << msg.id << " stored in memory (warning: disk write failed).\r";
            }
            oss << prompt_string();
            res.response_text = oss.str();
            return res;
        }

        std::string clean_line = sanitize_ascii_line(raw_line, 240);
        if (st.draft_body.size() + clean_line.size() + 1 > max_msg_bytes_) {
            res.response_text = "*** Maximum message size (" + std::to_string(max_msg_bytes_) +
                                " bytes) reached. Type /EX to send or /AB to abort.\r";
            return res;
        }
        st.draft_body += clean_line;
        st.draft_body.push_back('\n');
        return res;
    }

    // 3. Normal BBS command mode
    std::string line = trim(raw_line);
    if (line.empty()) {
        res.response_text = prompt_string();
        return res;
    }

    auto tokens = split_ws(line);
    if (tokens.empty()) {
        res.response_text = prompt_string();
        return res;
    }

    const std::string cmd = to_upper(tokens[0]);

    // Help
    if (cmd == "H" || cmd == "HELP" || cmd == "?") {
        std::ostringstream oss;
        oss << "OYK Packet BBS Commands:\r"
            << "  L  / LL <n>     - List recent messages (last 20 or <n>)\r"
            << "  LM / LN         - List messages to me / unread personal mail\r"
            << "  LB / LP         - List bulletins / personal messages\r"
            << "  L> <to>         - List messages to <callsign/topic>\r"
            << "  L< <from>       - List messages from <callsign>\r"
            << "  L@ <bbs>        - List messages routed @<bbs>\r"
            << "  LC              - List bulletin categories\r"
            << "  LS <text>       - Search messages by subject/to/from\r"
            << "  R <n> [n2...]   - Read message number(s)\r"
            << "  RM / RN         - Read all unread personal messages\r"
            << "  SP <to> [@bbs]  - Send personal message\r"
            << "  SB <top> [@dst] - Send bulletin (e.g. SB ALL)\r"
            << "  ST <to> [@bbs]  - Send NTS traffic message\r"
            << "  SR <n>          - Reply to message #<n>\r"
            << "  K <n> [n2...]   - Kill (delete) message(s)\r"
            << "  KM              - Kill all read personal messages to me\r";
        if (files_enabled_) {
            oss << "  FILES / DIR [d] - List files in BBS files area\r"
                << "  CD <dir> / PWD  - Navigate BBS files area subdirectories\r"
                << "  TYPE <file>     - View text file from BBS files area\r"
                << "  GET / D <file>  - Download file via YAPP / YAPP-C\r"
                << "  UPLOAD / YU     - Upload file via YAPP (queued in incoming/)\r";
        } else {
            oss << "  (Files Area is currently disabled)\r";
        }
        oss << "  I  / INFO       - BBS status and statistics\r"
            << "  B  / BYE / Q    - Exit BBS\r"
            << prompt_string();
        res.response_text = oss.str();
        return res;
    }

    // Info / Stats
    if (cmd == "I" || cmd == "INFO" || cmd == "STAT" || cmd == "STATS") {
        std::size_t pers = 0, bull = 0, traf = 0;
        for (const auto& [_, m] : messages_) {
            if (m.status == BbsMsgStatus::Killed) continue;
            if (m.type == BbsMsgType::Personal) ++pers;
            else if (m.type == BbsMsgType::Bulletin) ++bull;
            else if (m.type == BbsMsgType::Traffic) ++traf;
        }
        std::ostringstream oss;
        oss << bbs_callsign_;
        if (!bbs_alias_.empty()) oss << " (" << bbs_alias_ << ")";
        oss << " - OYK Packet BBS [OYK-0.1-HMR$]\r"
            << "Active messages: " << (pers + bull + traf)
            << " (Personal: " << pers << ", Bulletins: " << bull << ", Traffic: " << traf << ")\r"
            << "Your unread personal mail: " << unread_count_for(st.callsign) << "\r"
            << "Files area: " << (files_enabled_ ? "ENABLED" : "DISABLED") << "\r"
            << "Max message size: " << max_msg_bytes_ << " bytes\r"
            << prompt_string();
        res.response_text = oss.str();
        return res;
    }

    // Exit BBS
    if (cmd == "B" || cmd == "BYE" || cmd == "Q" || cmd == "QUIT") {
        bool direct = st.direct_connect;
        sessions_.erase(it);
        res.left_bbs = true;
        res.disconnect_session = direct;
        if (direct) {
            res.response_text = "73 de " + bbs_callsign_ + "\r";
        } else {
            res.response_text = "*** Leaving BBS. Returning to node.\r";
        }
        return res;
    }

    // Listing commands
    if (cmd == "L" || cmd == "LL" || cmd == "LM" || cmd == "LN" || cmd == "LB" ||
        cmd == "LP" || cmd == "L>" || cmd == "L<" || cmd == "L@" || cmd == "LS" ||
        cmd == "LC") {
        if (cmd == "LC") {
            std::map<std::string, std::size_t> cats;
            for (const auto& [_, m] : messages_) {
                if (m.status != BbsMsgStatus::Killed && m.type == BbsMsgType::Bulletin) {
                    cats[m.to]++;
                }
            }
            std::ostringstream oss;
            if (cats.empty()) {
                oss << "No bulletin categories yet.\r";
            } else {
                oss << "Bulletin Categories:\r";
                for (const auto& [cat, cnt] : cats) {
                    oss << "  " << std::left << std::setw(12) << cat << " (" << cnt << ")\r";
                }
            }
            oss << prompt_string();
            res.response_text = oss.str();
            return res;
        }

        std::string filter_arg;
        if (cmd == "L>" || cmd == "L<" || cmd == "L@" || cmd == "LS") {
            if (tokens.size() < 2) {
                res.response_text = "Usage: " + cmd + " <pattern>\r" + prompt_string();
                return res;
            }
            filter_arg = to_upper(tokens[1]);
        }

        std::size_t limit = 20;
        if (cmd == "LL") {
            uint64_t n = 20;
            if (tokens.size() >= 2 && parse_u64(tokens[1], n)) {
                limit = static_cast<std::size_t>(std::min<uint64_t>(n, 200));
            }
        } else if (cmd != "L") {
            limit = 200;
        }

        std::vector<const BbsMessage*> matches;
        for (auto mit = messages_.rbegin(); mit != messages_.rend(); ++mit) {
            const BbsMessage& m = mit->second;
            if (!can_read(m, st)) continue;

            bool keep = false;
            if (cmd == "L" || cmd == "LL") {
                keep = true;
            } else if (cmd == "LM") {
                keep = call_matches(m.to, st);
            } else if (cmd == "LN") {
                keep = (m.type == BbsMsgType::Personal && m.status == BbsMsgStatus::Unread &&
                        call_matches(m.to, st));
            } else if (cmd == "LB") {
                keep = (m.type == BbsMsgType::Bulletin);
            } else if (cmd == "LP") {
                keep = (m.type == BbsMsgType::Personal);
            } else if (cmd == "L>") {
                keep = (m.to == filter_arg || base_call(m.to) == filter_arg);
            } else if (cmd == "L<") {
                keep = (m.from == filter_arg || base_call(m.from) == filter_arg);
            } else if (cmd == "L@") {
                keep = (m.at_bbs == filter_arg);
            } else if (cmd == "LS") {
                std::string hay = to_upper(m.subject + " " + m.to + " " + m.from + " " + m.at_bbs);
                keep = (hay.find(filter_arg) != std::string::npos);
            }

            if (keep) {
                matches.push_back(&m);
                if (matches.size() >= limit) break;
            }
        }
        res.response_text = format_list(matches) + prompt_string();
        return res;
    }

    // Read unread personal messages (RM / RN)
    if (cmd == "RM" || cmd == "RN") {
        std::ostringstream oss;
        int count = 0;
        for (auto& [_, m] : messages_) {
            if (m.type == BbsMsgType::Personal &&
                m.status == BbsMsgStatus::Unread &&
                call_matches(m.to, st)) {
                oss << format_message(m);
                m.status = BbsMsgStatus::Read;
                save_message_to_disk(m);
                ++count;
            }
        }
        if (count == 0) {
            oss << "No unread personal messages.\r";
        }
        oss << prompt_string();
        res.response_text = oss.str();
        return res;
    }

    // Read specific message(s): R <num> [num2 ...]
    if (cmd == "R" || cmd == "READ") {
        if (tokens.size() < 2) {
            res.response_text = "Usage: R <msg#> [msg# ...]\r" + prompt_string();
            return res;
        }
        std::ostringstream oss;
        for (std::size_t i = 1; i < tokens.size(); ++i) {
            uint64_t mid = 0;
            if (!parse_u64(tokens[i], mid)) {
                oss << "Invalid message number: " << tokens[i] << "\r";
                continue;
            }
            auto mit = messages_.find(mid);
            if (mit == messages_.end() || !can_read(mit->second, st)) {
                oss << "Message #" << mid << " not found or access denied.\r";
                continue;
            }
            oss << format_message(mit->second);
            if (mit->second.type == BbsMsgType::Personal &&
                mit->second.status == BbsMsgStatus::Unread &&
                call_matches(mit->second.to, st)) {
                mit->second.status = BbsMsgStatus::Read;
                save_message_to_disk(mit->second);
            }
        }
        oss << prompt_string();
        res.response_text = oss.str();
        return res;
    }

    // Send Personal / Bulletin / Traffic: SP, SB, ST, S
    if (cmd == "SP" || cmd == "S" || cmd == "SB" || cmd == "ST") {
        std::string to_str, at_str;
        if (!parse_send_target(tokens, 1, to_str, at_str)) {
            res.response_text = "Usage: " + cmd + " <to> [@ <bbs>]\r" + prompt_string();
            return res;
        }
        if (cmd == "SB") st.draft_type = BbsMsgType::Bulletin;
        else if (cmd == "ST") st.draft_type = BbsMsgType::Traffic;
        else st.draft_type = BbsMsgType::Personal;

        st.draft_to = to_str;
        st.draft_at = at_str;
        st.draft_subject.clear();
        st.draft_body.clear();
        st.stage = ComposeStage::Subject;
        res.response_text = "Enter title (subject) for " + st.draft_to + " (/AB to abort): ";
        return res;
    }

    // Reply: SR <num>
    if (cmd == "SR") {
        uint64_t mid = 0;
        if (tokens.size() < 2 || !parse_u64(tokens[1], mid)) {
            res.response_text = "Usage: SR <msg#>\r" + prompt_string();
            return res;
        }
        auto mit = messages_.find(mid);
        if (mit == messages_.end() || !can_read(mit->second, st)) {
            res.response_text = "Message #" + std::to_string(mid) + " not found or access denied.\r" + prompt_string();
            return res;
        }
        st.draft_type = BbsMsgType::Personal;
        st.draft_to = mit->second.from;
        st.draft_at.clear();
        std::string orig_subj = mit->second.subject;
        if (to_upper(orig_subj).rfind("RE:", 0) == 0) {
            st.draft_subject = orig_subj;
        } else {
            st.draft_subject = "Re: " + orig_subj;
            if (st.draft_subject.size() > 64) st.draft_subject.resize(64);
        }
        st.draft_body.clear();
        st.stage = ComposeStage::Body;
        res.response_text = "Replying to #" + std::to_string(mid) + " (" + st.draft_to +
                            ") Subject: " + st.draft_subject + "\r"
                            "Enter message text. End with /EX or Ctrl-Z on a new line (/AB to abort):\r";
        return res;
    }

    // Kill all read personal messages to me: KM
    if (cmd == "KM") {
        int killed = 0;
        for (auto& [_, m] : messages_) {
            if (m.type == BbsMsgType::Personal &&
                m.status == BbsMsgStatus::Read &&
                call_matches(m.to, st)) {
                m.status = BbsMsgStatus::Killed;
                save_message_to_disk(m);
                ++killed;
            }
        }
        res.response_text = "Killed " + std::to_string(killed) + " read personal message(s).\r" + prompt_string();
        return res;
    }

    // Kill specific message(s): K <num> [num2 ...]
    if (cmd == "K" || cmd == "KILL") {
        if (tokens.size() < 2) {
            res.response_text = "Usage: K <msg#> [msg# ...]\r" + prompt_string();
            return res;
        }
        std::ostringstream oss;
        for (std::size_t i = 1; i < tokens.size(); ++i) {
            uint64_t mid = 0;
            if (!parse_u64(tokens[i], mid)) {
                oss << "Invalid message number: " << tokens[i] << "\r";
                continue;
            }
            auto mit = messages_.find(mid);
            if (mit == messages_.end() || mit->second.status == BbsMsgStatus::Killed) {
                oss << "Message #" << mid << " not found.\r";
                continue;
            }
            if (!can_kill(mit->second, st)) {
                oss << "Not authorized to kill message #" << mid << ".\r";
                continue;
            }
            mit->second.status = BbsMsgStatus::Killed;
            save_message_to_disk(mit->second);
            oss << "Message #" << mid << " killed.\r";
        }
        oss << prompt_string();
        res.response_text = oss.str();
        return res;
    }

    // Files Area feature gate
    if (cmd == "F" || cmd == "FILES" || cmd == "DIR" || cmd == "PWD" || cmd == "CD" ||
        cmd == "TYPE" || cmd == "CAT" || cmd == "D" || cmd == "GET" ||
        cmd == "DOWNLOAD" || cmd == "YAPP" || cmd == "U" || cmd == "UPLOAD" || cmd == "YU") {
        if (!files_enabled_) {
            res.response_text = "*** BBS Files Area is currently disabled.\r" + prompt_string();
            return res;
        }
    }

    // Files Area: list directory (F / FILES / DIR [subdir])
    if (cmd == "F" || cmd == "FILES" || cmd == "DIR") {
        std::string arg_path = (tokens.size() >= 2) ? tokens[1] : "";
        std::string abs_dir, rel_dir, perr;
        if (!resolve_files_path_with_case(files_path_, st.cur_files_rel, arg_path, false,
                                          files_case_insensitive_, abs_dir, rel_dir, perr)) {
            res.response_text = perr + "\r" + prompt_string();
            return res;
        }
        res.response_text = format_files_listing(abs_dir, rel_dir) + prompt_string();
        return res;
    }

    // Files Area: print current virtual directory (PWD)
    if (cmd == "PWD") {
        res.response_text = "Files Area: /" + st.cur_files_rel + "\r" + prompt_string();
        return res;
    }

    // Files Area: change directory (CD [dir])
    if (cmd == "CD") {
        if (tokens.size() < 2 || tokens[1] == "/") {
            st.cur_files_rel.clear();
            res.response_text = "Files Area: /\r" + prompt_string();
            return res;
        }
        std::string abs_dir, rel_dir, perr;
        if (!resolve_files_path_with_case(files_path_, st.cur_files_rel, tokens[1], true,
                                          files_case_insensitive_, abs_dir, rel_dir, perr)) {
            res.response_text = perr + "\r" + prompt_string();
            return res;
        }
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(abs_dir, ec) || !fs::is_directory(abs_dir, ec)) {
            res.response_text = "Directory not found: /" + rel_dir + "\r" + prompt_string();
            return res;
        }
        st.cur_files_rel = rel_dir;
        res.response_text = "Files Area: /" + st.cur_files_rel + "\r" + prompt_string();
        return res;
    }

    // Files Area: display text file (TYPE <file>)
    if (cmd == "TYPE" || cmd == "CAT") {
        if (tokens.size() < 2) {
            res.response_text = "Usage: TYPE <filename>\r" + prompt_string();
            return res;
        }
        std::string abs_file, rel_file, perr;
        if (!resolve_files_path_with_case(files_path_, st.cur_files_rel, tokens[1], false,
                                          files_case_insensitive_, abs_file, rel_file, perr)) {
            res.response_text = perr + "\r" + prompt_string();
            return res;
        }
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_regular_file(abs_file, ec)) {
            res.response_text = "File not found: /" + rel_file + "\r" + prompt_string();
            return res;
        }
        std::ifstream ifs(abs_file, std::ios::binary);
        if (!ifs.is_open()) {
            res.response_text = "Unable to open file.\r" + prompt_string();
            return res;
        }
        std::ostringstream oss;
        oss << "--- /" << rel_file << " ---\r";
        std::size_t shown = 0;
        constexpr std::size_t MAX_TYPE_BYTES = 8192;
        char ch = 0;
        while (ifs.get(ch) && shown < MAX_TYPE_BYTES) {
            unsigned char u = static_cast<unsigned char>(ch);
            if (u == '\n') {
                oss << '\r';
                ++shown;
            } else if (u == '\r') {
                // Skip raw CR to normalize to AX.25 CR
            } else if (u == '\t' || (u >= 0x20 && u <= 0x7E)) {
                oss << ch;
                ++shown;
            }
        }
        if (ifs.good()) {
            oss << "\r*** (truncated at " << MAX_TYPE_BYTES
                << " bytes; use GET " << tokens[1] << " for full YAPP download)";
        }
        oss << "\r--- EOF ---\r" << prompt_string();
        res.response_text = oss.str();
        return res;
    }

    // Files Area: YAPP Download (D / GET / DOWNLOAD / YAPP <file>)
    if (cmd == "D" || cmd == "GET" || cmd == "DOWNLOAD" || cmd == "YAPP") {
        if (tokens.size() < 2) {
            res.response_text = "Usage: GET <filename>\r" + prompt_string();
            return res;
        }
        if (st.is_local_loopback) {
            res.response_text = "*** YAPP file transfer is disallowed when both sides are local.\r" +
                                prompt_string();
            return res;
        }
        std::string abs_file, rel_file, perr;
        if (!resolve_files_path_with_case(files_path_, st.cur_files_rel, tokens[1], false,
                                          files_case_insensitive_, abs_file, rel_file, perr)) {
            res.response_text = perr + "\r" + prompt_string();
            return res;
        }
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_regular_file(abs_file, ec)) {
            res.response_text = "File not found: /" + rel_file + "\r" + prompt_string();
            return res;
        }
        if (!st.yapp) {
            st.yapp = std::make_unique<protocol::YappSession>(incoming_root_);
        }
        st.yapp->reset();
        st.yapp->set_incoming_root(incoming_root_);
        st.yapp->set_remote_callsign(st.parsed_call);

        std::vector<uint8_t> tx_accum;
        st.yapp->set_send_callback([&tx_accum](const std::vector<uint8_t>& pkt) {
            tx_accum.insert(tx_accum.end(), pkt.begin(), pkt.end());
        });
        std::string yerr;
        if (!st.yapp->start_send(abs_file, yerr)) {
            res.response_text = "*** YAPP download error: " + yerr + "\r" + prompt_string();
            return res;
        }
        st.yapp_active = true;
        st.yapp_expect_upload = false;
        res.tx_bytes = std::move(tx_accum);
        res.log_info = "BBS YAPP download started by " + st.callsign + ": /" + rel_file;
        return res;
    }

    // Files Area: YAPP Upload into incoming/<CALLSIGN-SSID>/<filename> (U / UPLOAD / YU)
    if (cmd == "U" || cmd == "UPLOAD" || cmd == "YU") {
        if (st.is_local_loopback) {
            res.response_text = "*** YAPP file transfer is disallowed when both sides are local.\r" +
                                prompt_string();
            return res;
        }
        if (!st.yapp) {
            st.yapp = std::make_unique<protocol::YappSession>(incoming_root_);
        }
        st.yapp->reset();
        st.yapp->set_incoming_root(incoming_root_);
        st.yapp->set_remote_callsign(st.parsed_call);
        // Never allow remote uploads to overwrite an existing file in incoming/
        st.yapp->set_overwrite_callback([](const std::filesystem::path&, const std::string&) {
            return false;
        });
        st.yapp_active = true;
        st.yapp_expect_upload = true;
        res.response_text = "*** Ready for YAPP upload into incoming/" +
                            protocol::format_yapp_peer_dir(st.parsed_call) +
                            "/. Start YAPP send now (Ctrl-X to cancel).\r";
        return res;
    }

    res.response_text = "Unknown BBS command '" + tokens[0] + "'. Type H for help.\r" + prompt_string();
    return res;
}

} // namespace oyk::node
