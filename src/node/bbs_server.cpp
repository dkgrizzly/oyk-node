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
                          std::string welcome,
                          std::size_t max_msg_bytes,
                          std::string& error_out) {
    if (!bbs_callsign.empty()) bbs_callsign_ = std::move(bbs_callsign);
    bbs_alias_ = std::move(bbs_alias);
    mail_path_ = mail_path.empty() ? "mail" : std::move(mail_path);
    if (!welcome.empty()) welcome_ = std::move(welcome);
    max_msg_bytes_ = std::clamp<std::size_t>(max_msg_bytes, 256, 65536);

    namespace fs = std::filesystem;
    messages_.clear();
    next_msg_id_ = 1;
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
                                 std::chrono::system_clock::time_point /*now*/) {
    BbsSessionState st;
    st.session_id = session_id;
    st.callsign = to_upper(trim(callsign));
    st.base_callsign = base_call(st.callsign);
    st.interface_id = interface_id;
    st.direct_connect = direct_connect;
    st.is_sysop = is_sysop;
    sessions_[session_id] = st;

    const std::size_t unread = unread_count_for(st.callsign);
    const std::size_t total = active_message_count();

    std::ostringstream oss;
    oss << "[OYK-0.1-HMR$]\r"
        << "*** " << bbs_callsign_;
    if (!bbs_alias_.empty()) oss << " (" << bbs_alias_ << ")";
    oss << " Packet BBS ***\r"
        << welcome_ << "\r"
        << "Hello " << st.base_callsign
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
            << "  KM              - Kill all read personal messages to me\r"
            << "  I  / INFO       - BBS status and statistics\r"
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

    res.response_text = "Unknown BBS command '" + tokens[0] + "'. Type H for help.\r" + prompt_string();
    return res;
}

} // namespace oyk::node
