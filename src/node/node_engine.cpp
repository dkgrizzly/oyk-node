// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/node/node_engine.hpp"
#include "oyk/radio/interface_registry.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace oyk::node {

namespace {

constexpr std::size_t MAX_NEW_CALLSIGNS = 5;

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

} // namespace

NodeEngine::NodeEngine(NodeConfig config)
    : config_(std::move(config)),
      clock_fn_([]() { return std::chrono::system_clock::now(); }),
      accept_incoming_(config_.accept_incoming_default) {
    logger_.set_console_level(config_.logging.console_level);
}

NodeEngine::~NodeEngine() {
    stop();
}

void NodeEngine::set_clock(ClockFn clock_fn) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    clock_fn_ = clock_fn ? std::move(clock_fn)
                         : ClockFn([]() { return std::chrono::system_clock::now(); });
}

std::chrono::system_clock::time_point NodeEngine::current_time() const {
    return clock_fn_ ? clock_fn_() : std::chrono::system_clock::now();
}

void NodeEngine::log_msg(LogLevel level, const std::string& msg) {
    logger_.log_at(level, msg, current_time());
}

bool NodeEngine::add_interface(std::unique_ptr<radio::RadioInterface> iface, std::string& error_out) {
    if (!iface) {
        error_out = "Null radio interface";
        return false;
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    const std::string id = iface->id();
    if (iface_by_id_.count(id) != 0) {
        error_out = "Duplicate radio interface id: " + id;
        return false;
    }
    radio::RadioInterface* raw = iface.get();
    interfaces_.push_back(std::move(iface));
    iface_by_id_[id] = raw;
    return true;
}

bool NodeEngine::start(std::string& error_out) {
    if (running_.load()) {
        return true;
    }

    std::string log_warn;
    (void)logger_.configure(config_.logging, log_warn);
    if (!log_warn.empty()) {
        log_msg(LOG_WARNING, log_warn);
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        const std::string ban_path = config_.resolved_banlist_path();
        if (!ban_list_.load_from_file(ban_path, error_out)) {
            log_msg(LOG_ERROR, "Failed to load banlist: " + error_out);
            return false;
        }
        // First line ending from node.cfg takes precedence over mixed or unset EOLs
        if (config_.has_detected_line_ending) {
            ban_list_.apply_preferred_line_ending(config_.line_ending, true);
        }
        const auto now_tp = current_time();
        for (const auto& legacy_ban : config_.banned_callsigns) {
            if (auto key = BanList::normalize_key(legacy_ban)) {
                auto parsed = ax25::Callsign::parse(*key);
                if (parsed && !ban_list_.find_active(*parsed, now_tp)) {
                    BanEntry entry;
                    entry.key = *key;
                    entry.banned_at = now_tp;
                    entry.permanent = true;
                    entry.flags = BAN_FLAG_APPEAL_ALLOWED;
                    entry.reason = "Banned by node configuration.";
                    ban_list_.upsert(std::move(entry));
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (interfaces_.empty()) {
            auto& reg = radio::InterfaceRegistry::instance();
            for (const auto& raw_icfg : config_.interfaces) {
                radio::RadioInterfaceConfig icfg = raw_icfg;
                if (icfg.params.find("node_callsign") == icfg.params.end()) {
                    icfg.params["node_callsign"] = config_.callsign.str();
                }
                std::string create_err;
                auto iface = reg.create(icfg, create_err);
                if (!iface) {
                    error_out = "Interface '" + icfg.id + "': " + create_err;
                    log_msg(LOG_ERROR, error_out);
                    return false;
                }
                const std::string id = iface->id();
                if (iface_by_id_.count(id) != 0) {
                    error_out = "Duplicate interface id in config: " + id;
                    log_msg(LOG_ERROR, error_out);
                    return false;
                }
                iface_by_id_[id] = iface.get();
                interfaces_.push_back(std::move(iface));
            }
        }
    }

    std::vector<radio::RadioInterface*> started;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (auto& iface : interfaces_) {
            std::string start_err;
            auto cb = [this](const ax25::Frame& f, const radio::RadioFrameMeta& meta) {
                this->on_radio_frame(f, meta);
            };
            if (!iface->start(std::move(cb), start_err)) {
                for (auto* prev : started) {
                    prev->stop();
                }
                error_out = "Failed to start interface '" + iface->id() + "': " + start_err;
                log_msg(LOG_ERROR, error_out);
                return false;
            }
            started.push_back(iface.get());
            log_msg(LOG_INFO, "Interface " + iface->id() + " (" + iface->type() + ") started: " +
                              iface->status_summary());
        }
        started_at_ = std::chrono::steady_clock::now();
        last_beacon_at_ = started_at_;
        if (!config_.source_path.empty()) {
            log_msg(LOG_INFO, "Loaded config: " + config_.source_path);
        } else {
            log_msg(LOG_INFO, "No config file found; using built-in defaults");
        }
        log_msg(LOG_INFO, "Banlist: " + ban_list_.file_path() + " (" +
                          std::to_string(ban_list_.entries(current_time()).size()) + " active)");
        log_msg(LOG_INFO, "Node " + config_.callsign.str() +
                          (config_.alias.valid() ? " (" + config_.alias.str() + ")" : "") +
                          " online");
        if (config_.bbs.enabled) {
            const std::string bbs_call = config_.resolved_bbs_callsign().str();
            const std::string bbs_alias = config_.bbs.alias.valid() ? config_.bbs.alias.str() : "";
            const std::string mail_dir = config_.resolved_bbs_mail_path();
            const std::string files_dir = config_.resolved_bbs_files_path();
            std::string bbs_err;
            bbs_server_.set_files_enabled(config_.bbs.files_enabled);
            if (!bbs_server_.configure(bbs_call, bbs_alias, mail_dir, files_dir, "incoming",
                                       config_.bbs.welcome, config_.bbs.max_msg_bytes, bbs_err)) {
                log_msg(LOG_WARNING, "BBS startup warning: " + bbs_err);
            } else {
                log_msg(LOG_INFO, "BBS active on " + bbs_call +
                                  (!bbs_alias.empty() ? " (" + bbs_alias + ")" : "") +
                                  " (" + std::to_string(bbs_server_.active_message_count()) + " msgs in " +
                                  mail_dir + ", files: " + files_dir + ")");
            }
        }
        if (config_.chat.enabled) {
            const std::string chat_call = config_.resolved_chat_callsign().str();
            chat_room_.configure(chat_call, config_.chat.default_topic, config_.chat.welcome);
            log_msg(LOG_INFO, "CHAT conference active on " + chat_call +
                              (config_.chat.alias.valid() ? " (" + config_.chat.alias.str() + ")" : ""));
        }
    }

    running_.store(true);
    timer_thread_ = std::thread(&NodeEngine::timer_loop, this);
    return true;
}

void NodeEngine::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    std::vector<PendingTx> pending;
    bool had_active = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        accept_incoming_ = false;
        const std::string signoff = "\r*** Node shutting down. 73 de " + config_.callsign.str() + "\r";
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            Session* sess = it->second.get();
            if (sess && sess->is_active()) {
                had_active = true;
                sess->set_bridged_session_id(0);
                if (sess->state() == SessionState::Connected) {
                    auto msg_ev = sess->queue_text(signoff);
                    queue_session_frames(*sess, msg_ev.tx_frames, pending);
                }
                auto ev = sess->initiate_disconnect("Node shutting down");
                queue_session_frames(*sess, ev.tx_frames, pending);
                if (sess->state() == SessionState::Closed) {
                    it = sessions_.erase(it);
                    continue;
                }
            }
            ++it;
        }
        log_msg(LOG_INFO, "Node shutting down");
    }
    dispatch_tx(pending);

    // Give active peers a brief window to receive the 73 sign-off + DISC and acknowledge with UA
    if (had_active) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
        while (std::chrono::steady_clock::now() < deadline) {
            bool any_open = false;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                for (const auto& [id, sess] : sessions_) {
                    if (sess && sess->is_active()) {
                        any_open = true;
                        break;
                    }
                }
            }
            if (!any_open) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    if (timer_thread_.joinable()) {
        timer_thread_.join();
    }

    std::vector<radio::RadioInterface*> to_stop;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        sessions_.clear();
        local_callbacks_.clear();
        for (auto& iface : interfaces_) {
            to_stop.push_back(iface.get());
        }
    }
    for (auto* iface : to_stop) {
        iface->stop();
    }
}

void NodeEngine::set_accept_incoming(bool allow) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    manual_deny_incoming_ = !allow;
    if (accept_incoming_ != allow) {
        accept_incoming_ = allow;
        log_msg(allow ? LOG_INFO : LOG_WARNING,
                std::string("Incoming connections ") + (allow ? "ALLOWED" : "DENIED"));
    }
}

bool NodeEngine::toggle_accept_incoming() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    accept_incoming_ = !accept_incoming_;
    manual_deny_incoming_ = !accept_incoming_;
    log_msg(accept_incoming_ ? LOG_INFO : LOG_WARNING,
            std::string("Incoming connections ") + (accept_incoming_ ? "ALLOWED" : "DENIED"));
    return accept_incoming_;
}

bool NodeEngine::accept_incoming() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return accept_incoming_;
}

bool NodeEngine::terminate_session(uint64_t session_id, std::string* message_out) {
    std::vector<PendingTx> pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end() && it->second && it->second->is_active()) {
            Session& sess = *it->second;
            std::string call = sess.remote_call().str();
            std::string iface = sess.interface_id();
            teardown_bridge_locked(sess, "Connection terminated by operator", pending);
            auto ev = sess.initiate_disconnect("Terminated by operator");
            queue_session_frames(sess, ev.tx_frames, pending);
            if (sess.state() == SessionState::Closed) {
                sessions_.erase(it);
            }
            log_msg(LOG_KICK, "Terminated session #" + std::to_string(session_id) +
                              " (" + call + " on " + iface + ")");
            if (message_out) {
                *message_out = "Terminated #" + std::to_string(session_id) + " (" + call + ")";
            }
            found = true;
        } else if (message_out) {
            *message_out = "Session #" + std::to_string(session_id) + " not found";
        }
    }
    dispatch_tx(pending);
    return found;
}

bool NodeEngine::ban_callsign(std::string_view callsign_spec,
                              std::string* message_out,
                              bool disconnect_active,
                              uint32_t flags,
                              const std::string& reason,
                              std::optional<std::chrono::system_clock::time_point> lift_at) {
    auto key_opt = BanList::normalize_key(callsign_spec);
    if (!key_opt) {
        if (message_out) *message_out = "Invalid callsign format";
        return false;
    }

    std::vector<PendingTx> pending;
    std::size_t disconnected = 0;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        const auto now_tp = current_time();

        BanEntry entry;
        entry.key = *key_opt;
        entry.banned_at = now_tp;
        if (lift_at.has_value()) {
            entry.permanent = false;
            entry.lift_at = *lift_at;
        } else {
            entry.permanent = true;
        }
        entry.flags = flags;
        entry.reason = reason.empty() ? "Banned by sysop." : reason;

        ban_list_.upsert(entry);
        bool ban_saved = true;
        if (!ban_list_.file_path().empty()) {
            if (!ban_list_.save_to_file()) {
                ban_saved = false;
                log_msg(LOG_WARNING, "Banlist file '" + ban_list_.file_path() +
                                     "' not writable; ban kept in memory only.");
            }
        }

        if (disconnect_active) {
            for (auto it = sessions_.begin(); it != sessions_.end();) {
                Session& sess = *it->second;
                if (sess.is_active() && is_banned_locked(sess.remote_call())) {
                    teardown_bridge_locked(sess, "Callsign banned", pending);
                    auto ev = sess.initiate_disconnect("Callsign banned");
                    queue_session_frames(sess, ev.tx_frames, pending);
                    ++disconnected;
                    if (sess.state() == SessionState::Closed) {
                        it = sessions_.erase(it);
                        continue;
                    }
                }
                ++it;
            }
        }

        std::string msg = "Banned callsign " + entry.key + " [" + entry.format_flags_binary() +
                          ", " + entry.format_lift_at() + "]: " + entry.reason;
        if (disconnected > 0) {
            msg += " (disconnected " + std::to_string(disconnected) + " active)";
        }
        if (!ban_saved) {
            msg += " [in memory only; banlist.cfg not writable]";
        }
        log_msg(LOG_BAN, msg);
        if (message_out) *message_out = msg;
    }
    dispatch_tx(pending);
    return true;
}

bool NodeEngine::ban_callsign_with_lift_spec(std::string_view callsign_spec,
                                             std::string_view lift_spec,
                                             std::string* message_out,
                                             bool disconnect_active,
                                             uint32_t flags,
                                             const std::string& reason) {
    std::optional<std::chrono::system_clock::time_point> lift_at;
    std::string parse_err;
    if (!BanList::parse_lift_spec(lift_spec, current_time(), lift_at, &parse_err)) {
        if (message_out) *message_out = parse_err;
        return false;
    }
    return ban_callsign(callsign_spec, message_out, disconnect_active, flags, reason, lift_at);
}

bool NodeEngine::unban_callsign(std::string_view callsign_spec, std::string* message_out) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    std::string removed_key;
    if (ban_list_.remove(callsign_spec, &removed_key)) {
        bool ban_saved = true;
        if (!ban_list_.file_path().empty()) {
            if (!ban_list_.save_to_file()) {
                ban_saved = false;
                log_msg(LOG_WARNING, "Banlist file '" + ban_list_.file_path() +
                                     "' not writable; unban kept in memory only.");
            }
        }
        std::string msg = "Unbanned callsign " + removed_key;
        if (!ban_saved) {
            msg += " [in memory only; banlist.cfg not writable]";
        }
        log_msg(LOG_BAN, msg);
        if (message_out) *message_out = msg;
        return true;
    }
    auto key_opt = BanList::normalize_key(callsign_spec);
    if (!key_opt) {
        if (message_out) *message_out = "Invalid callsign format";
    } else if (message_out) {
        *message_out = *key_opt + " is not in ban list";
    }
    return false;
}

bool NodeEngine::is_banned(const ax25::Callsign& call) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return is_banned_locked(call);
}

bool NodeEngine::is_banned_locked(const ax25::Callsign& call) const {
    return ban_list_.is_banned(call, current_time());
}

bool NodeEngine::is_for_node(const ax25::Callsign& dst) const {
    if (dst == config_.callsign) return true;
    if (config_.alias.valid() && dst == config_.alias) return true;
    return false;
}

bool NodeEngine::is_for_bbs(const ax25::Callsign& dst) const {
    if (!config_.bbs.enabled) return false;
    if (dst == config_.resolved_bbs_callsign()) return true;
    if (config_.bbs.alias.valid() && dst == config_.bbs.alias) return true;
    return false;
}

bool NodeEngine::is_for_chat(const ax25::Callsign& dst) const {
    if (!config_.chat.enabled) return false;
    if (dst == config_.resolved_chat_callsign()) return true;
    if (config_.chat.alias.valid() && dst == config_.chat.alias) return true;
    return false;
}

bool NodeEngine::is_own_callsign(const ax25::Callsign& call) const {
    return is_for_node(call) || is_for_bbs(call) || is_for_chat(call);
}

void NodeEngine::record_heard_locked(const ax25::Callsign& src,
                                     const std::string& interface_id,
                                     std::chrono::system_clock::time_point now) {
    if (!src.valid() || is_own_callsign(src)) {
        return;
    }

    const std::string call_str = src.str();
    auto& mh = mheard_[call_str];
    mh.callsign = call_str;
    mh.interface_id = interface_id;
    mh.last_heard = now;
    mh.frame_count++;

    if (seen_callsigns_.insert(call_str).second) {
        last_new_callsigns_.push_front(NewCallsignRecord{call_str, interface_id, now});
        while (last_new_callsigns_.size() > MAX_NEW_CALLSIGNS) {
            last_new_callsigns_.pop_back();
        }
        log_msg(LOG_INFO, "New callsign heard: " + call_str + " on " + interface_id);
    }
}

std::string NodeEngine::welcome_banner() const {
    std::ostringstream oss;
    oss << "*** Connected to " << config_.callsign.str();
    if (config_.alias.valid()) {
        oss << " (" << config_.alias.str() << ")";
    }
    oss << " Packet Node\r";
    if (!config_.welcome_text.empty()) {
        oss << config_.welcome_text << "\r";
    }
    oss << prompt_string();
    return oss.str();
}

std::string NodeEngine::prompt_string() const {
    return config_.callsign.str() + "> ";
}

std::string NodeEngine::build_ban_notice(const BanEntry& entry, bool can_appeal) {
    std::ostringstream oss;
    oss << "*** RESTRICTED SESSION - CALLSIGN BANNED ***\r"
        << "Callsign : " << entry.key << "\r"
        << "Type     : " << (entry.hard_ban() ? "HARD BAN" : "SOFT BAN") << "\r"
        << "Banned   : " << entry.format_banned_at() << "\r"
        << "Lifts    : " << entry.format_lift_at() << "\r"
        << "Reason   : " << entry.reason << "\r";
    if (can_appeal) {
        oss << "You may submit ONE appeal (max " << MAX_APPEAL_LENGTH
            << " ASCII chars), or type QUIT to disconnect.\r"
            << "Appeal> ";
    } else if (entry.appeal_used()) {
        oss << "Appeal   : Already submitted.\rDisconnecting.\r";
    } else {
        oss << "Appeal   : Not permitted for this ban.\rDisconnecting.\r";
    }
    return oss.str();
}

void NodeEngine::queue_session_frames(Session& sess,
                                      const std::vector<ax25::Frame>& frames,
                                      std::vector<PendingTx>& pending) {
    if (sess.is_local_loopback()) return;
    auto it = iface_by_id_.find(sess.interface_id());
    if (it == iface_by_id_.end() || !it->second) return;
    for (const auto& f : frames) {
        pending.push_back(PendingTx{it->second, f, sess.peer_endpoint()});
    }
}

void NodeEngine::deliver_text_locked(Session& sess,
                                     const std::string& text,
                                     std::vector<PendingTx>& pending,
                                     std::vector<PendingLocalEvent>& pending_local) {
    if (text.empty()) return;
    if (sess.is_local_loopback()) {
        auto cbit = local_callbacks_.find(sess.id());
        if (cbit != local_callbacks_.end()) {
            PendingLocalEvent ple;
            ple.session_id = sess.id();
            ple.rx_bytes.assign(text.begin(), text.end());
            ple.callbacks = cbit->second;
            pending_local.push_back(std::move(ple));
        }
        return;
    }
    auto ev = sess.queue_text(text);
    queue_session_frames(sess, ev.tx_frames, pending);
}

void NodeEngine::apply_chat_result_locked(const ChatActionResult& res,
                                          uint64_t actor_sid,
                                          std::vector<PendingTx>& pending,
                                          std::vector<PendingLocalEvent>& pending_local) {
    for (const auto& d : res.deliveries) {
        auto it = sessions_.find(d.session_id);
        if (it != sessions_.end() && it->second && it->second->state() == SessionState::Connected) {
            deliver_text_locked(*it->second, d.text, pending, pending_local);
        }
    }
    if (res.left_chat) {
        auto it = sessions_.find(actor_sid);
        if (it != sessions_.end() && it->second) {
            Session& actor = *it->second;
            if (res.disconnect_session) {
                if (actor.is_local_loopback()) {
                    auto cbit = local_callbacks_.find(actor_sid);
                    if (cbit != local_callbacks_.end()) {
                        PendingLocalEvent ple;
                        ple.session_id = actor_sid;
                        ple.closed = true;
                        ple.close_reason = "Left chat";
                        ple.callbacks = cbit->second;
                        pending_local.push_back(std::move(ple));
                        local_callbacks_.erase(cbit);
                    }
                    sessions_.erase(it);
                } else {
                    auto disc_ev = actor.initiate_disconnect("Left chat");
                    queue_session_frames(actor, disc_ev.tx_frames, pending);
                    if (actor.state() == SessionState::Closed) {
                        sessions_.erase(it);
                    }
                }
            } else {
                actor.set_mode(SessionMode::Normal);
                deliver_text_locked(actor, prompt_string(), pending, pending_local);
            }
        }
    }
}

void NodeEngine::apply_bbs_result_locked(const BbsActionResult& res,
                                         uint64_t actor_sid,
                                         std::vector<PendingTx>& pending,
                                         std::vector<PendingLocalEvent>& pending_local) {
    if (!res.log_info.empty()) {
        log_msg(LOG_INFO, res.log_info);
    }
    auto it = sessions_.find(actor_sid);
    if (it == sessions_.end() || !it->second) return;
    Session& actor = *it->second;
    if (!res.response_text.empty() && actor.state() == SessionState::Connected) {
        deliver_text_locked(actor, res.response_text, pending, pending_local);
    }
    if (!res.tx_bytes.empty() && actor.state() == SessionState::Connected && !actor.is_local_loopback()) {
        auto bev = actor.queue_bytes(res.tx_bytes);
        queue_session_frames(actor, bev.tx_frames, pending);
    }
    if (res.left_bbs) {
        if (res.disconnect_session) {
            if (actor.is_local_loopback()) {
                auto cbit = local_callbacks_.find(actor_sid);
                if (cbit != local_callbacks_.end()) {
                    PendingLocalEvent ple;
                    ple.session_id = actor_sid;
                    ple.closed = true;
                    ple.close_reason = "Left BBS";
                    ple.callbacks = cbit->second;
                    pending_local.push_back(std::move(ple));
                    local_callbacks_.erase(cbit);
                }
                sessions_.erase(it);
            } else {
                auto disc_ev = actor.initiate_disconnect("Left BBS");
                queue_session_frames(actor, disc_ev.tx_frames, pending);
                if (actor.state() == SessionState::Closed) {
                    sessions_.erase(it);
                }
            }
        } else {
            actor.set_mode(SessionMode::Normal);
            deliver_text_locked(actor, prompt_string(), pending, pending_local);
        }
    }
}

void NodeEngine::dispatch_tx(const std::vector<PendingTx>& pending) {
    for (const auto& tx : pending) {
        if (tx.iface) {
            tx.iface->send_frame(tx.frame, tx.endpoint);
        }
    }
}

void NodeEngine::dispatch_local_events(const std::vector<PendingLocalEvent>& events) {
    for (const auto& ev : events) {
        if (ev.connected && ev.callbacks.on_connected) {
            ev.callbacks.on_connected(ev.session_id);
        }
        if (!ev.rx_bytes.empty() && ev.callbacks.on_rx_bytes) {
            ev.callbacks.on_rx_bytes(ev.session_id, ev.rx_bytes);
        }
        if (ev.closed && ev.callbacks.on_closed) {
            ev.callbacks.on_closed(ev.session_id, ev.close_reason);
        }
    }
}

bool NodeEngine::open_local_terminal(const std::string& port_spec,
                                     const std::string& target_call_str,
                                     LocalTerminalCallbacks callbacks,
                                     uint64_t& session_id_out,
                                     ax25::Callsign* resolved_call_out,
                                     std::string& error_out,
                                     bool* is_local_out) {
    std::vector<PendingTx> pending;
    std::vector<PendingLocalEvent> pending_local;
    bool is_local = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        std::string port_upper = to_upper(port_spec);
        std::string call_part = target_call_str;
        std::string endpoint;
        std::size_t at = target_call_str.find('@');
        if (at != std::string::npos) {
            call_part = target_call_str.substr(0, at);
            endpoint = target_call_str.substr(at + 1);
        }
        std::string call_upper = to_upper(call_part);

        bool target_is_local_keyword = call_upper.empty() || call_upper == "LOCAL" || call_upper == "NODE";
        bool target_is_chat_keyword = (call_upper == "CHAT");
        bool target_is_bbs_keyword = (call_upper == "BBS");
        std::optional<ax25::Callsign> parsed_target;
        if (!target_is_local_keyword && !target_is_chat_keyword && !target_is_bbs_keyword) {
            parsed_target = ax25::Callsign::parse(call_part);
        }

        bool direct_chat = false;
        bool direct_bbs = false;
        if (port_upper == "LOCAL" || target_is_local_keyword || target_is_chat_keyword ||
            target_is_bbs_keyword ||
            (parsed_target && (is_for_node(*parsed_target) || is_for_bbs(*parsed_target) ||
                               is_for_chat(*parsed_target)))) {
            is_local = true;
            if (target_is_bbs_keyword || (parsed_target && is_for_bbs(*parsed_target))) {
                if (!config_.bbs.enabled) {
                    error_out = "Local BBS is disabled in configuration";
                    return false;
                }
                direct_bbs = true;
                parsed_target = config_.resolved_bbs_callsign();
            } else if (target_is_chat_keyword || (parsed_target && is_for_chat(*parsed_target))) {
                if (!config_.chat.enabled) {
                    error_out = "Local CHAT is disabled in configuration";
                    return false;
                }
                direct_chat = true;
                parsed_target = config_.resolved_chat_callsign();
            } else {
                parsed_target = config_.callsign;
            }
        }

        if (is_local) {
            SessionParams sp{
                config_.paclen,
                config_.maxframe,
                config_.t1_ms,
                config_.n2_retries,
                config_.idle_timeout_sec
            };
            uint64_t sid = next_session_id_++;
            auto sess = std::make_unique<Session>(
                sid, "local", *parsed_target, config_.callsign, "local",
                SessionDirection::Inbound, sp);
            sess->set_local_loopback(true);
            (void)sess->accept_inbound_sabm();
            if (direct_bbs) {
                sess->set_mode(SessionMode::Bbs);
                sess->set_bbs_direct(true);
            } else if (direct_chat) {
                sess->set_mode(SessionMode::Chat);
                sess->set_chat_direct(true);
            } else {
                sess->set_mode(SessionMode::Normal);
            }
            Session* raw = sess.get();
            sessions_[sid] = std::move(sess);
            local_callbacks_[sid] = callbacks;

            PendingLocalEvent conn_ev;
            conn_ev.session_id = sid;
            conn_ev.connected = true;
            conn_ev.callbacks = callbacks;
            pending_local.push_back(std::move(conn_ev));

            if (direct_bbs) {
                auto bbs_res = bbs_server_.enter(sid, config_.callsign.str(), "local",
                                                 true, true, true, current_time());
                apply_bbs_result_locked(bbs_res, sid, pending, pending_local);
            } else if (direct_chat) {
                auto chat_res = chat_room_.join(sid, config_.callsign.str(), "local", true);
                apply_chat_result_locked(chat_res, sid, pending, pending_local);
            } else {
                deliver_text_locked(*raw, welcome_banner(), pending, pending_local);
            }

            session_id_out = sid;
            if (resolved_call_out) *resolved_call_out = *parsed_target;
            if (is_local_out) *is_local_out = true;
            log_msg(LOG_INFO, "Local terminal #" + std::to_string(sid) + " connected locally to " +
                              parsed_target->str());
        } else {
            std::string port_id = resolve_port_id_locked(port_spec);
            if (port_id.empty()) {
                error_out = "Unknown port '" + port_spec + "'";
                return false;
            }
            if (!parsed_target) {
                error_out = "Invalid callsign '" + call_part + "'";
                return false;
            }

            SessionParams sp{
                config_.paclen,
                config_.maxframe,
                config_.t1_ms,
                config_.n2_retries,
                config_.idle_timeout_sec
            };
            uint64_t sid = next_session_id_++;
            auto sess = std::make_unique<Session>(
                sid, port_id, config_.callsign, *parsed_target, endpoint,
                SessionDirection::Outbound, sp);
            sess->set_local_terminal();
            Session* raw = sess.get();
            sessions_[sid] = std::move(sess);
            local_callbacks_[sid] = std::move(callbacks);

            auto ev = raw->initiate_outbound_connect();
            queue_session_frames(*raw, ev.tx_frames, pending);

            session_id_out = sid;
            if (resolved_call_out) *resolved_call_out = *parsed_target;
            if (is_local_out) *is_local_out = false;
            log_msg(LOG_INFO, "Local terminal #" + std::to_string(sid) + " connecting to " +
                              parsed_target->str() + " on " + port_id);
        }
    }
    dispatch_tx(pending);
    dispatch_local_events(pending_local);
    return true;
}

bool NodeEngine::send_local_terminal_bytes(uint64_t session_id, const std::vector<uint8_t>& data) {
    std::vector<PendingTx> pending;
    std::vector<PendingLocalEvent> pending_local;
    std::vector<PendingCommand> pending_cmds;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end() || !it->second ||
            it->second->state() != SessionState::Connected) {
            return false;
        }
        Session& sess = *it->second;
        if (sess.is_local_loopback()) {
            auto lines = sess.feed_local_loopback_bytes(data);
            for (const auto& line : lines) {
                if (sess.mode() == SessionMode::Chat) {
                    auto cres = chat_room_.handle_line(session_id, line);
                    apply_chat_result_locked(cres, session_id, pending, pending_local);
                    if (sessions_.find(session_id) == sessions_.end()) break;
                } else if (sess.mode() == SessionMode::Bbs) {
                    auto bres = bbs_server_.handle_line(session_id, line, current_time());
                    apply_bbs_result_locked(bres, session_id, pending, pending_local);
                    if (sessions_.find(session_id) == sessions_.end()) break;
                } else {
                    pending_cmds.push_back(PendingCommand{session_id, line});
                }
            }
        } else if (sess.mode() == SessionMode::LocalTerminal) {
            auto ev = sess.queue_bytes(data);
            queue_session_frames(sess, ev.tx_frames, pending);
        } else {
            return false;
        }
    }
    dispatch_tx(pending);
    dispatch_local_events(pending_local);
    if (!pending_cmds.empty()) {
        execute_pending_commands(pending_cmds);
    }
    return true;
}

bool NodeEngine::close_local_terminal(uint64_t session_id) {
    std::vector<PendingTx> pending;
    std::vector<PendingLocalEvent> pending_local;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end() || !it->second) {
            local_callbacks_.erase(session_id);
            return false;
        }
        Session& sess = *it->second;
        if (bbs_server_.has_session(session_id)) {
            bbs_server_.leave(session_id);
        }
        if (chat_room_.has_participant(session_id)) {
            auto cres = chat_room_.leave(session_id, "Disconnected");
            for (const auto& d : cres.deliveries) {
                if (d.session_id == session_id) continue;
                auto dit = sessions_.find(d.session_id);
                if (dit != sessions_.end() && dit->second && dit->second->state() == SessionState::Connected) {
                    deliver_text_locked(*dit->second, d.text, pending, pending_local);
                }
            }
        }
        if (sess.is_local_loopback()) {
            auto cbit = local_callbacks_.find(session_id);
            if (cbit != local_callbacks_.end()) {
                PendingLocalEvent ple;
                ple.session_id = session_id;
                ple.closed = true;
                ple.close_reason = "Disconnected";
                ple.callbacks = cbit->second;
                pending_local.push_back(std::move(ple));
                local_callbacks_.erase(cbit);
            }
            sessions_.erase(it);
        } else {
            auto ev = sess.initiate_disconnect("Disconnected by local operator");
            queue_session_frames(sess, ev.tx_frames, pending);
            auto cbit = local_callbacks_.find(session_id);
            if (sess.state() == SessionState::Closed) {
                if (cbit != local_callbacks_.end()) {
                    PendingLocalEvent ple;
                    ple.session_id = session_id;
                    ple.closed = true;
                    ple.close_reason = "Disconnected";
                    ple.callbacks = cbit->second;
                    pending_local.push_back(std::move(ple));
                    local_callbacks_.erase(cbit);
                }
                sessions_.erase(it);
            }
        }
    }
    dispatch_tx(pending);
    dispatch_local_events(pending_local);
    return true;
}

void NodeEngine::on_radio_frame(const ax25::Frame& frame, const radio::RadioFrameMeta& meta) {
    std::vector<PendingTx> pending;
    std::vector<PendingCommand> pending_cmds;
    std::vector<PendingLocalEvent> pending_local;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        const auto now_tp = current_time();

        log_msg(LOG_DEBUG, "RX [" + meta.interface_id + "] " + frame.summary());

        record_heard_locked(frame.source, meta.interface_id, meta.received_at);

        if (frame.type() == ax25::FrameType::UI) {
            return;
        }

        if (!is_for_node(frame.destination) && !is_for_bbs(frame.destination) &&
            !is_for_chat(frame.destination)) {
            return;
        }

        Session* existing = nullptr;
        for (auto& [id, sess] : sessions_) {
            if (sess && sess->interface_id() == meta.interface_id &&
                sess->remote_call() == frame.source) {
                existing = sess.get();
                break;
            }
        }

        if (frame.type() == ax25::FrameType::SABM) {
            if (!existing && !accept_incoming_) {
                log_msg(LOG_WARNING, "Denied incoming connection from " + frame.source.str() +
                                     " on " + meta.interface_id + " (incoming disabled)");
                if (auto it = iface_by_id_.find(meta.interface_id); it != iface_by_id_.end()) {
                    pending.push_back(PendingTx{
                        it->second,
                        ax25::Frame::make_dm(frame.source, frame.destination, frame.pf()),
                        meta.peer_endpoint});
                }
            } else if (existing) {
                existing->set_peer_endpoint(meta.peer_endpoint);
                auto ev = existing->on_frame(frame);
                process_session_events(*existing, ev, pending, pending_cmds, pending_local);
            } else {
                const BanAccessDecision decision = ban_list_.evaluate_access(frame.source, now_tp);

                if (decision.action == BanAccessAction::RejectHardBan) {
                    log_msg(LOG_BAN, "Rejected SABM from hard-banned callsign " +
                                     frame.source.str() + " on " + meta.interface_id);
                    if (auto it = iface_by_id_.find(meta.interface_id); it != iface_by_id_.end()) {
                        pending.push_back(PendingTx{
                            it->second,
                            ax25::Frame::make_dm(frame.source, frame.destination, frame.pf()),
                            meta.peer_endpoint});
                    }
                } else if (decision.action == BanAccessAction::RejectDailyLimit) {
                    log_msg(LOG_BAN, "Rejected SABM from soft-banned callsign " +
                                     frame.source.str() + " on " + meta.interface_id +
                                     " (daily check limit reached)");
                    if (auto it = iface_by_id_.find(meta.interface_id); it != iface_by_id_.end()) {
                        pending.push_back(PendingTx{
                            it->second,
                            ax25::Frame::make_dm(frame.source, frame.destination, frame.pf()),
                            meta.peer_endpoint});
                    }
                } else if (decision.action == BanAccessAction::RestrictedNotify ||
                           decision.action == BanAccessAction::RestrictedStatus) {
                    const BanEntry& entry = *decision.entry;
                    const bool can_appeal = entry.appeal_allowed() && !entry.appeal_used();
                    const std::string today = Logger::format_date_yyyymmdd(now_tp);

                    if (decision.action == BanAccessAction::RestrictedNotify) {
                        ban_list_.mark_notified_and_checked(decision.ban_key, today);
                        if (!ban_list_.file_path().empty()) {
                            ban_list_.save_to_file();
                        }
                    } else {
                        ban_list_.mark_daily_checked(decision.ban_key, today);
                    }

                    SessionParams sp{
                        config_.paclen,
                        config_.maxframe,
                        config_.t1_ms,
                        config_.n2_retries,
                        std::min<uint32_t>(config_.idle_timeout_sec > 0 ? config_.idle_timeout_sec : 120u, 120u)
                    };
                    uint64_t sid = next_session_id_++;
                    auto sess = std::make_unique<Session>(
                        sid,
                        meta.interface_id,
                        frame.destination,
                        frame.source,
                        meta.peer_endpoint,
                        SessionDirection::Inbound,
                        sp);
                    sess->set_restricted_ban(decision.ban_key, can_appeal);
                    Session* raw = sess.get();
                    sessions_[sid] = std::move(sess);

                    auto ev = raw->accept_inbound_sabm();
                    queue_session_frames(*raw, ev.tx_frames, pending);

                    log_msg(LOG_BAN,
                            std::string(decision.action == BanAccessAction::RestrictedNotify
                                            ? "Restricted ban notice session #"
                                            : "Restricted soft-ban status session #") +
                                std::to_string(sid) + " for " + frame.source.str() +
                                " on " + meta.interface_id);

                    auto notice_ev = raw->queue_text(build_ban_notice(entry, can_appeal));
                    queue_session_frames(*raw, notice_ev.tx_frames, pending);

                    if (!can_appeal) {
                        auto disc_ev = raw->initiate_disconnect("Ban notice delivered");
                        queue_session_frames(*raw, disc_ev.tx_frames, pending);
                        if (raw->state() == SessionState::Closed) {
                            sessions_.erase(sid);
                        }
                    }
                } else {
                    SessionParams sp{
                        config_.paclen,
                        config_.maxframe,
                        config_.t1_ms,
                        config_.n2_retries,
                        config_.idle_timeout_sec
                    };
                    uint64_t sid = next_session_id_++;
                    auto sess = std::make_unique<Session>(
                        sid,
                        meta.interface_id,
                        frame.destination,
                        frame.source,
                        meta.peer_endpoint,
                        SessionDirection::Inbound,
                        sp);
                    Session* raw = sess.get();
                    sessions_[sid] = std::move(sess);

                    auto ev = raw->accept_inbound_sabm();
                    queue_session_frames(*raw, ev.tx_frames, pending);

                    if (is_for_bbs(frame.destination)) {
                        raw->set_mode(SessionMode::Bbs);
                        raw->set_bbs_direct(true);
                        log_msg(LOG_INFO, "BBS session #" + std::to_string(sid) + " connected: " +
                                          frame.source.str() + " on " + meta.interface_id);
                        bool is_sysop = (frame.source.base() == config_.callsign.base());
                        auto bres = bbs_server_.enter(sid, frame.source.str(), meta.interface_id,
                                                      true, is_sysop, false, now_tp);
                        apply_bbs_result_locked(bres, sid, pending, pending_local);
                    } else if (is_for_chat(frame.destination)) {
                        raw->set_mode(SessionMode::Chat);
                        raw->set_chat_direct(true);
                        log_msg(LOG_INFO, "CHAT session #" + std::to_string(sid) + " connected: " +
                                          frame.source.str() + " on " + meta.interface_id);
                        auto cres = chat_room_.join(sid, frame.source.str(), meta.interface_id, true);
                        apply_chat_result_locked(cres, sid, pending, pending_local);
                    } else {
                        log_msg(LOG_INFO, "Session #" + std::to_string(sid) + " connected: " +
                                          frame.source.str() + " on " + meta.interface_id);
                        auto banner_ev = raw->queue_text(welcome_banner());
                        queue_session_frames(*raw, banner_ev.tx_frames, pending);
                    }
                }
            }
        } else if (!existing) {
            if (frame.type() != ax25::FrameType::DM) {
                if (auto it = iface_by_id_.find(meta.interface_id); it != iface_by_id_.end()) {
                    pending.push_back(PendingTx{
                        it->second,
                        ax25::Frame::make_dm(frame.source, frame.destination, frame.pf()),
                        meta.peer_endpoint});
                }
            }
        } else {
            existing->set_peer_endpoint(meta.peer_endpoint);
            auto ev = existing->on_frame(frame);
            process_session_events(*existing, ev, pending, pending_cmds, pending_local);
        }
    }
    dispatch_tx(pending);
    dispatch_local_events(pending_local);
    if (!pending_cmds.empty()) {
        execute_pending_commands(pending_cmds);
    }
}

void NodeEngine::handle_restricted_ban_line_locked(Session& sess,
                                                   const std::string& line,
                                                   std::vector<PendingTx>& pending) {
    const std::string sanitized = BanList::sanitize_appeal_text(line);
    const std::string upper = to_upper(sanitized);

    if (!sess.appeal_eligible() || sanitized.empty() ||
        upper == "QUIT" || upper == "BYE" || upper == "CANCEL") {
        auto resp = sess.queue_text("No appeal submitted. Disconnecting.\r");
        queue_session_frames(sess, resp.tx_frames, pending);
        auto disc = sess.initiate_disconnect("Restricted session ended");
        queue_session_frames(sess, disc.tx_frames, pending);
        return;
    }

    sess.set_appeal_eligible(false);
    ban_list_.mark_appeal_used(sess.ban_key());
    if (!ban_list_.file_path().empty()) {
        ban_list_.save_to_file();
    }

    logger_.log_appeal(sess.remote_call().str(), sanitized, current_time());

    auto resp = sess.queue_text("Appeal recorded (" + std::to_string(sanitized.size()) +
                                " chars). Disconnecting.\r");
    queue_session_frames(sess, resp.tx_frames, pending);
    auto disc = sess.initiate_disconnect("Appeal submitted");
    queue_session_frames(sess, disc.tx_frames, pending);
}

void NodeEngine::process_session_events(Session& sess,
                                        SessionEvents& ev,
                                        std::vector<PendingTx>& pending,
                                        std::vector<PendingCommand>& pending_cmds,
                                        std::vector<PendingLocalEvent>& pending_local) {
    queue_session_frames(sess, ev.tx_frames, pending);

    const uint64_t sid = sess.id();

    if (sess.mode() == SessionMode::LocalTerminal) {
        if (ev.just_connected) {
            log_msg(LOG_INFO, "Local terminal #" + std::to_string(sid) + " connected to " +
                              sess.remote_call().str() + " on " + sess.interface_id());
        }
        if (ev.just_connected || !ev.rx_data.empty() || ev.just_closed) {
            auto cbit = local_callbacks_.find(sid);
            if (cbit != local_callbacks_.end()) {
                PendingLocalEvent ple;
                ple.session_id = sid;
                ple.connected = ev.just_connected;
                ple.closed = ev.just_closed;
                ple.close_reason = ev.close_reason.empty() ? "Disconnected" : ev.close_reason;
                ple.rx_bytes = ev.rx_data;
                ple.callbacks = cbit->second;
                pending_local.push_back(std::move(ple));
                if (ev.just_closed) {
                    local_callbacks_.erase(cbit);
                }
            }
        }
        if (ev.just_closed) {
            std::string reason = ev.close_reason.empty() ? "Closed" : ev.close_reason;
            log_msg(LOG_INFO, "Local terminal #" + std::to_string(sid) + " (" +
                              sess.remote_call().str() + ") closed: " + reason);
            sessions_.erase(sid);
        }
        return;
    }

    if (ev.just_connected && sess.direction() == SessionDirection::Outbound) {
        log_msg(LOG_INFO, "Outbound session #" + std::to_string(sid) + " connected to " +
                          sess.remote_call().str() + " on " + sess.interface_id());
        if (sess.bridged_session_id() != 0) {
            if (auto it = sessions_.find(sess.bridged_session_id()); it != sessions_.end()) {
                auto notice = it->second->queue_text(
                    "*** Connected to " + sess.remote_call().str() + "\r");
                queue_session_frames(*it->second, notice.tx_frames, pending);
            }
        }
    }

    if (sess.mode() == SessionMode::Chat) {
        for (const auto& line : ev.rx_lines) {
            auto cres = chat_room_.handle_line(sid, line);
            apply_chat_result_locked(cres, sid, pending, pending_local);
            if (sessions_.find(sid) == sessions_.end()) return;
        }
    } else if (sess.mode() == SessionMode::Bbs) {
        if (bbs_server_.is_yapp_active(sid) && !ev.rx_data.empty()) {
            auto bres = bbs_server_.handle_raw_bytes(sid, ev.rx_data);
            apply_bbs_result_locked(bres, sid, pending, pending_local);
            if (sessions_.find(sid) == sessions_.end()) return;
        } else {
            const auto now_tp = current_time();
            for (const auto& line : ev.rx_lines) {
                auto bres = bbs_server_.handle_line(sid, line, now_tp);
                apply_bbs_result_locked(bres, sid, pending, pending_local);
                if (sessions_.find(sid) == sessions_.end()) return;
                if (bbs_server_.is_yapp_active(sid)) break;
            }
        }
    } else if (sess.mode() == SessionMode::RestrictedBan) {
        for (const auto& line : ev.rx_lines) {
            handle_restricted_ban_line_locked(sess, line, pending);
            if (sess.state() != SessionState::Connected) {
                break;
            }
        }
    } else if (sess.bridged_session_id() != 0 && !ev.rx_data.empty()) {
        if (auto it = sessions_.find(sess.bridged_session_id());
            it != sessions_.end() && it->second && it->second->state() == SessionState::Connected) {
            auto fwd = it->second->queue_bytes(ev.rx_data);
            queue_session_frames(*it->second, fwd.tx_frames, pending);
        }
    } else if (sess.bridged_session_id() == 0) {
        for (const auto& line : ev.rx_lines) {
            pending_cmds.push_back(PendingCommand{sid, line});
        }
    }

    if (ev.just_closed) {
        std::string reason = ev.close_reason.empty() ? "Closed" : ev.close_reason;
        log_msg(LOG_INFO, "Session #" + std::to_string(sid) + " (" +
                          sess.remote_call().str() + ") closed: " + reason);
        if (bbs_server_.has_session(sid)) {
            bbs_server_.leave(sid);
        }
        if (chat_room_.has_participant(sid)) {
            auto cres = chat_room_.leave(sid, reason);
            for (const auto& d : cres.deliveries) {
                if (d.session_id == sid) continue;
                auto dit = sessions_.find(d.session_id);
                if (dit != sessions_.end() && dit->second && dit->second->state() == SessionState::Connected) {
                    deliver_text_locked(*dit->second, d.text, pending, pending_local);
                }
            }
        }
        teardown_bridge_locked(sess, reason, pending);
        sessions_.erase(sid);
    }
}

void NodeEngine::execute_pending_commands(const std::vector<PendingCommand>& pending_cmds) {
    for (const auto& pc : pending_cmds) {
        CommandResult result = command_handler_.execute(*this, pc.session_id, pc.line);

        std::vector<PendingTx> pending;
        std::vector<PendingLocalEvent> pending_local;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            auto it = sessions_.find(pc.session_id);
            if (it == sessions_.end() || !it->second ||
                it->second->state() != SessionState::Connected ||
                it->second->mode() != SessionMode::Normal ||
                it->second->bridged_session_id() != 0) {
                continue;
            }
            Session& sess = *it->second;

            if (result.enter_bbs) {
                if (!config_.bbs.enabled) {
                    deliver_text_locked(sess, "BBS is disabled on this node.\r" + prompt_string(),
                                        pending, pending_local);
                } else {
                    sess.set_mode(SessionMode::Bbs);
                    sess.set_bbs_direct(false);
                    bool is_sysop = sess.is_local_loopback() ||
                                    (sess.remote_call().base() == config_.callsign.base());
                    auto bres = bbs_server_.enter(sess.id(), sess.remote_call().str(),
                                                  sess.interface_id(), false, is_sysop,
                                                  sess.is_local_loopback(), current_time());
                    apply_bbs_result_locked(bres, sess.id(), pending, pending_local);
                }
            } else if (result.enter_chat) {
                if (!config_.chat.enabled) {
                    deliver_text_locked(sess, "CHAT is disabled on this node.\r" + prompt_string(),
                                        pending, pending_local);
                } else {
                    sess.set_mode(SessionMode::Chat);
                    sess.set_chat_direct(false);
                    auto cres = chat_room_.join(sess.id(), sess.remote_call().str(),
                                                sess.interface_id(), false);
                    apply_chat_result_locked(cres, sess.id(), pending, pending_local);
                }
            } else if (result.connect_request) {
                if (sess.is_local_loopback()) {
                    deliver_text_locked(sess, "Use Connect (F2 / Connect button) to connect to a remote node.\r" + prompt_string(),
                                        pending, pending_local);
                } else {
                    start_outbound_connect(sess, result.connect_port, result.connect_callsign, pending);
                }
            } else {
                if (!result.response_text.empty()) {
                    deliver_text_locked(sess, result.response_text, pending, pending_local);
                }
                if (result.disconnect) {
                    if (sess.is_local_loopback()) {
                        auto cbit = local_callbacks_.find(sess.id());
                        if (cbit != local_callbacks_.end()) {
                            PendingLocalEvent ple;
                            ple.session_id = sess.id();
                            ple.closed = true;
                            ple.close_reason = "User disconnected (BYE)";
                            ple.callbacks = cbit->second;
                            pending_local.push_back(std::move(ple));
                            local_callbacks_.erase(cbit);
                        }
                        sessions_.erase(it);
                    } else {
                        auto disc_ev = sess.initiate_disconnect("User disconnected (BYE)");
                        queue_session_frames(sess, disc_ev.tx_frames, pending);
                        log_msg(LOG_INFO, "Session #" + std::to_string(sess.id()) + " (" +
                                          sess.remote_call().str() + ") disconnected via BYE");
                    }
                } else {
                    deliver_text_locked(sess, prompt_string(), pending, pending_local);
                }
            }
        }
        dispatch_tx(pending);
        dispatch_local_events(pending_local);
    }
}

void NodeEngine::teardown_bridge_locked(Session& closed_sess,
                                        const std::string& reason,
                                        std::vector<PendingTx>& pending) {
    uint64_t partner_id = closed_sess.bridged_session_id();
    if (partner_id == 0) return;

    closed_sess.set_bridged_session_id(0);
    auto it = sessions_.find(partner_id);
    if (it == sessions_.end() || !it->second) return;

    Session& partner = *it->second;
    partner.set_bridged_session_id(0);

    if (partner.direction() == SessionDirection::Inbound && partner.is_active()) {
        auto notice = partner.queue_text(
            "\r*** Disconnected from " + closed_sess.remote_call().str() +
            " (" + reason + ")\r" + prompt_string());
        queue_session_frames(partner, notice.tx_frames, pending);
    } else if (partner.direction() == SessionDirection::Outbound && partner.is_active()) {
        auto disc = partner.initiate_disconnect("Bridged user disconnected");
        queue_session_frames(partner, disc.tx_frames, pending);
        if (partner.state() == SessionState::Closed) {
            sessions_.erase(it);
        }
    }
}

std::string NodeEngine::resolve_port_id_locked(const std::string& port_spec) const {
    if (iface_by_id_.count(port_spec) > 0) {
        return port_spec;
    }
    try {
        std::size_t idx = std::stoul(port_spec);
        if (idx >= 1 && idx <= interfaces_.size()) {
            return interfaces_[idx - 1]->id();
        }
    } catch (...) {
    }
    return {};
}

void NodeEngine::start_outbound_connect(Session& inbound_sess,
                                        const std::string& port_spec,
                                        const std::string& target_call_str,
                                        std::vector<PendingTx>& pending) {
    std::string port_id = resolve_port_id_locked(port_spec);
    if (port_id.empty()) {
        auto ev = inbound_sess.queue_text("Unknown port '" + port_spec + "'.\r" + prompt_string());
        queue_session_frames(inbound_sess, ev.tx_frames, pending);
        return;
    }

    auto target = ax25::Callsign::parse(target_call_str);
    if (!target) {
        auto ev = inbound_sess.queue_text("Invalid callsign '" + target_call_str + "'.\r" + prompt_string());
        queue_session_frames(inbound_sess, ev.tx_frames, pending);
        return;
    }

    if (is_own_callsign(*target)) {
        auto ev = inbound_sess.queue_text("Cannot connect to self.\r" + prompt_string());
        queue_session_frames(inbound_sess, ev.tx_frames, pending);
        return;
    }

    if (is_banned_locked(*target)) {
        auto ev = inbound_sess.queue_text("Target callsign is banned.\r" + prompt_string());
        queue_session_frames(inbound_sess, ev.tx_frames, pending);
        return;
    }

    SessionParams sp{
        config_.paclen,
        config_.maxframe,
        config_.t1_ms,
        config_.n2_retries,
        config_.idle_timeout_sec
    };
    uint64_t out_id = next_session_id_++;
    auto out_sess = std::make_unique<Session>(
        out_id,
        port_id,
        config_.callsign,
        *target,
        "",
        SessionDirection::Outbound,
        sp);
    out_sess->set_bridged_session_id(inbound_sess.id());
    inbound_sess.set_bridged_session_id(out_id);

    Session* raw_out = out_sess.get();
    sessions_[out_id] = std::move(out_sess);

    auto notice = inbound_sess.queue_text(
        "*** Connecting to " + target->str() + " via " + port_id + "...\r");
    queue_session_frames(inbound_sess, notice.tx_frames, pending);

    auto conn_ev = raw_out->initiate_outbound_connect();
    queue_session_frames(*raw_out, conn_ev.tx_frames, pending);

    log_msg(LOG_INFO, "Session #" + std::to_string(inbound_sess.id()) + " (" +
                      inbound_sess.remote_call().str() + ") connecting out to " +
                      target->str() + " on " + port_id + " (#" + std::to_string(out_id) + ")");
}

void NodeEngine::timer_loop() {
    int rotation_ticks = 0;
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!running_.load()) break;

        if (++rotation_ticks >= 10) {
            rotation_ticks = 0;
            logger_.check_daily_rotation(current_time());
        }

        std::vector<PendingTx> pending;
        std::vector<PendingLocalEvent> pending_local;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            auto now = std::chrono::steady_clock::now();

            std::vector<uint64_t> ids;
            ids.reserve(sessions_.size());
            for (const auto& [id, _] : sessions_) {
                ids.push_back(id);
            }

            std::vector<PendingCommand> dummy_cmds;
            for (uint64_t id : ids) {
                auto it = sessions_.find(id);
                if (it == sessions_.end() || !it->second) continue;
                auto ev = it->second->on_tick(now);
                process_session_events(*it->second, ev, pending, dummy_cmds, pending_local);
            }

            if (config_.beacon_interval_sec > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - last_beacon_at_).count();
                if (elapsed >= static_cast<int64_t>(config_.beacon_interval_sec)) {
                    last_beacon_at_ = now;
                    std::string id_text = config_.callsign.str() +
                                          (config_.alias.valid() ? " (" + config_.alias.str() + ")" : "") +
                                          " - " + config_.info_text;
                    std::vector<uint8_t> payload(id_text.begin(), id_text.end());
                    ax25::Frame ui = ax25::Frame::make_ui(
                        ax25::Callsign{"ID", 0}, config_.callsign, payload);
                    for (auto& iface : interfaces_) {
                        if (iface && iface->is_running()) {
                            pending.push_back(PendingTx{iface.get(), ui, ""});
                        }
                    }
                }
            }
        }
        dispatch_tx(pending);
        dispatch_local_events(pending_local);
    }
}

NodeSnapshot NodeEngine::snapshot() const {
    NodeSnapshot snap;
    std::lock_guard<std::mutex> lock(state_mutex_);

    snap.node_callsign = config_.callsign.str();
    snap.node_alias = config_.alias.str();
    snap.welcome_text = config_.welcome_text;
    snap.info_text = config_.info_text;
    snap.accept_incoming = accept_incoming_;
    snap.manual_deny_incoming = manual_deny_incoming_;

    if (running_.load()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - started_at_).count();
        snap.uptime_seconds = elapsed > 0 ? static_cast<uint64_t>(elapsed) : 0;
    }

    std::map<std::string, std::size_t> active_per_iface;
    for (const auto& [id, sess] : sessions_) {
        if (sess && sess->is_active()) {
            active_per_iface[sess->interface_id()]++;
            snap.total_active_connections++;
            snap.active_sessions.push_back(SessionSnapshot{
                sess->id(),
                sess->remote_call().str(),
                sess->interface_id(),
                sess->state(),
                sess->direction(),
                sess->mode(),
                sess->bridged_session_id(),
                sess->connected_at()
            });
        }
    }

    for (const auto& iface : interfaces_) {
        if (!iface) continue;
        auto st = iface->stats();
        InterfaceSnapshot is;
        is.id = iface->id();
        is.type = iface->type();
        is.description = iface->description();
        is.status = iface->status_summary();
        is.running = st.running;
        is.active_connections = active_per_iface[is.id];
        is.rx_frames = st.rx_frames;
        is.tx_frames = st.tx_frames;
        is.rx_errors = st.rx_errors;
        is.tx_errors = st.tx_errors;
        snap.interfaces.push_back(std::move(is));
    }

    snap.last_new_callsigns.assign(last_new_callsigns_.begin(), last_new_callsigns_.end());
    snap.ban_entries = ban_list_.entries(current_time());
    snap.banned_callsigns.reserve(snap.ban_entries.size());
    for (const auto& e : snap.ban_entries) {
        snap.banned_callsigns.push_back(e.key);
    }
    snap.recent_logs = logger_.recent_console_logs();
    return snap;
}

std::vector<MHeardRecord> NodeEngine::mheard(std::size_t max_entries) const {
    std::vector<MHeardRecord> out;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        out.reserve(mheard_.size());
        for (const auto& [_, rec] : mheard_) {
            out.push_back(rec);
        }
    }
    std::sort(out.begin(), out.end(), [](const MHeardRecord& a, const MHeardRecord& b) {
        return a.last_heard > b.last_heard;
    });
    if (out.size() > max_entries) {
        out.resize(max_entries);
    }
    return out;
}

std::string NodeEngine::bbs_files_path() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return bbs_server_.files_path();
}

std::string NodeEngine::bbs_incoming_path() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return bbs_server_.incoming_root();
}

std::vector<PendingIncomingFile> NodeEngine::list_pending_incoming_files() const {
    std::string inc;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        inc = bbs_server_.incoming_root();
    }
    return BbsServer::list_pending_incoming(inc);
}

bool NodeEngine::approve_incoming_file(const std::string& uploader,
                                       const std::string& filename,
                                       const std::string& dest_subdir,
                                       bool allow_overwrite,
                                       std::string& dest_path_out,
                                       bool& already_exists_out,
                                       std::string& error_out) {
    std::string inc, files_dir;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        inc = bbs_server_.incoming_root();
        files_dir = bbs_server_.files_path();
    }
    bool ok = BbsServer::approve_incoming_file(inc, files_dir, uploader, filename, dest_subdir,
                                               allow_overwrite, dest_path_out, already_exists_out, error_out);
    if (ok) {
        log_msg(LOG_INFO, "Sysop approved incoming file " + uploader + "/" + filename +
                          " -> " + dest_path_out);
    }
    return ok;
}

bool NodeEngine::delete_incoming_file(const std::string& uploader,
                                      const std::string& filename,
                                      std::string& error_out) {
    std::string inc;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        inc = bbs_server_.incoming_root();
    }
    bool ok = BbsServer::delete_incoming_file(inc, uploader, filename, error_out);
    if (ok) {
        log_msg(LOG_INFO, "Sysop deleted incoming file " + uploader + "/" + filename);
    }
    return ok;
}

radio::RadioInterface* NodeEngine::find_interface(const std::string& id) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = iface_by_id_.find(id);
    return (it != iface_by_id_.end()) ? it->second : nullptr;
}

NodeConfig NodeEngine::config() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return config_;
}

namespace {

bool iface_cfg_equal(const radio::RadioInterfaceConfig& a,
                     const radio::RadioInterfaceConfig& b) {
    return a.id == b.id &&
           a.type == b.type &&
           a.description == b.description &&
           a.params == b.params &&
           a.peers == b.peers;
}

bool is_supported_iface_type(const std::string& t) {
    return t == "axudp" || t == "axip" || t == "kiss-serial" ||
           t == "kiss-tcp" || t == "telnet";
}

} // namespace

ConfigApplyResult NodeEngine::apply_config(NodeConfig new_cfg, bool save_to_disk) {
    ConfigApplyResult result;

    if (!new_cfg.callsign.valid()) {
        result.error = "Invalid node callsign.";
        return result;
    }
    new_cfg.bbs.ssid = static_cast<uint8_t>(new_cfg.bbs.ssid & 0x0Fu);
    new_cfg.chat.ssid = static_cast<uint8_t>(new_cfg.chat.ssid & 0x0Fu);

    {
        std::set<std::string> seen_ids;
        for (const auto& ic : new_cfg.interfaces) {
            if (ic.id.empty()) {
                result.error = "Interface ID cannot be empty.";
                return result;
            }
            if (!seen_ids.insert(ic.id).second) {
                result.error = "Duplicate interface ID: " + ic.id;
                return result;
            }
            if (!is_supported_iface_type(ic.type)) {
                result.error = "Unsupported interface type '" + ic.type + "' for " + ic.id;
                return result;
            }
        }
    }

    NodeConfig old_cfg;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        old_cfg = config_;
        if (new_cfg.source_path.empty()) {
            new_cfg.source_path = old_cfg.source_path;
        }
        if (!new_cfg.has_detected_line_ending && old_cfg.has_detected_line_ending) {
            new_cfg.line_ending = old_cfg.line_ending;
            new_cfg.has_detected_line_ending = true;
        }
    }

    const std::string new_node_call = new_cfg.callsign.str();
    const std::string new_bbs_call  = new_cfg.resolved_bbs_callsign().str();
    const std::string new_chat_call = new_cfg.resolved_chat_callsign().str();

    const bool node_id_changed =
        (old_cfg.callsign != new_cfg.callsign) || (old_cfg.alias != new_cfg.alias);
    const bool bbs_id_changed =
        (old_cfg.resolved_bbs_callsign() != new_cfg.resolved_bbs_callsign()) ||
        (old_cfg.bbs.alias != new_cfg.bbs.alias);
    const bool chat_id_changed =
        (old_cfg.resolved_chat_callsign() != new_cfg.resolved_chat_callsign()) ||
        (old_cfg.chat.alias != new_cfg.chat.alias);
    const bool bbs_disabled  = (old_cfg.bbs.enabled && !new_cfg.bbs.enabled);
    const bool chat_disabled = (old_cfg.chat.enabled && !new_cfg.chat.enabled);
    const bool identity_restart = (node_id_changed || bbs_id_changed || chat_id_changed);

    result.identity_restarted = identity_restart;

    // Determine which interfaces need to be stopped/recreated
    std::map<std::string, radio::RadioInterfaceConfig> old_if_map;
    for (const auto& ic : old_cfg.interfaces) old_if_map[ic.id] = ic;
    std::map<std::string, radio::RadioInterfaceConfig> new_if_map;
    for (const auto& ic : new_cfg.interfaces) new_if_map[ic.id] = ic;

    std::set<std::string> ifaces_to_stop_ids;
    std::vector<radio::RadioInterfaceConfig> ifaces_to_start_cfgs;

    for (const auto& [id, oic] : old_if_map) {
        auto nit = new_if_map.find(id);
        bool telnet_needs_restart = (node_id_changed && oic.type == "telnet");
        if (nit == new_if_map.end() || !iface_cfg_equal(oic, nit->second) || telnet_needs_restart) {
            ifaces_to_stop_ids.insert(id);
        }
    }
    for (const auto& nic : new_cfg.interfaces) {
        auto oit = old_if_map.find(nic.id);
        bool telnet_needs_restart = (node_id_changed && nic.type == "telnet");
        if (oit == old_if_map.end() || !iface_cfg_equal(oit->second, nic) || telnet_needs_restart) {
            ifaces_to_start_cfgs.push_back(nic);
        }
    }

    const bool ifaces_changed = !ifaces_to_stop_ids.empty() || !ifaces_to_start_cfgs.empty();
    result.interfaces_reloaded = ifaces_changed;

    // Phase 1: Temporarily disable new incoming connections & gracefully sign off affected sessions
    std::vector<PendingTx> pending_tx;
    std::vector<PendingLocalEvent> pending_local;
    std::set<uint64_t> signed_off_ids;
    bool wait_for_remote_drain = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (identity_restart) {
            accept_incoming_ = false;
        }

        for (auto it = sessions_.begin(); it != sessions_.end();) {
            Session* sess = it->second.get();
            if (!sess || !sess->is_active()) {
                ++it;
                continue;
            }
            const bool is_bbs  = (sess->mode() == SessionMode::Bbs);
            const bool is_chat = (sess->mode() == SessionMode::Chat);
            const bool iface_stopping = (ifaces_to_stop_ids.count(sess->interface_id()) > 0);
            const bool must_signoff =
                identity_restart ||
                (bbs_disabled && is_bbs) ||
                (chat_disabled && is_chat) ||
                iface_stopping;

            if (!must_signoff) {
                ++it;
                continue;
            }

            std::string signoff;
            if (is_bbs) {
                if (new_cfg.bbs.enabled) {
                    signoff = "\r*** Node is restarting for configuration changes. "
                              "BBS will be accessible on " + new_bbs_call + " after restart. 73!\r";
                } else {
                    signoff = "\r*** Node is restarting for configuration changes. "
                              "BBS will not be available after this change. 73!\r";
                }
            } else if (is_chat) {
                if (new_cfg.chat.enabled) {
                    signoff = "\r*** Node is restarting for configuration changes. "
                              "CHAT will be accessible on " + new_chat_call + " after restart. 73!\r";
                } else {
                    signoff = "\r*** Node is restarting for configuration changes. "
                              "CHAT will not be available after this change. 73!\r";
                }
            } else {
                signoff = "\r*** Node is restarting for configuration changes. "
                          "Node will be accessible on " + new_node_call + " after restart. 73!\r";
            }

            const uint64_t sid = sess->id();
            bbs_server_.leave(sid);
            (void)chat_room_.leave(sid, "Node restarting for configuration changes");
            sess->set_bridged_session_id(0);

            ++result.signed_off_sessions;
            signed_off_ids.insert(sid);

            if (sess->is_local_loopback()) {
                auto cbit = local_callbacks_.find(sid);
                if (cbit != local_callbacks_.end()) {
                    PendingLocalEvent ple;
                    ple.session_id = sid;
                    ple.rx_bytes.assign(signoff.begin(), signoff.end());
                    ple.closed = true;
                    ple.close_reason = "Configuration change restart";
                    ple.callbacks = cbit->second;
                    pending_local.push_back(std::move(ple));
                    local_callbacks_.erase(cbit);
                }
                it = sessions_.erase(it);
                continue;
            }

            if (sess->state() == SessionState::Connected) {
                auto msg_ev = sess->queue_text(signoff);
                queue_session_frames(*sess, msg_ev.tx_frames, pending_tx);
            }
            auto disc_ev = sess->initiate_disconnect("Configuration change restart");
            queue_session_frames(*sess, disc_ev.tx_frames, pending_tx);
            if (sess->state() == SessionState::Closed) {
                it = sessions_.erase(it);
                continue;
            }
            wait_for_remote_drain = true;
            ++it;
        }
    }

    dispatch_tx(pending_tx);
    dispatch_local_events(pending_local);

    if (wait_for_remote_drain && running_.load()) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
        while (std::chrono::steady_clock::now() < deadline) {
            bool any_pending = false;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                for (uint64_t sid : signed_off_ids) {
                    auto it = sessions_.find(sid);
                    if (it != sessions_.end() && it->second && it->second->is_active()) {
                        any_pending = true;
                        break;
                    }
                }
            }
            if (!any_pending) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    // Remove any remaining disconnecting sessions that were signed off
    if (!signed_off_ids.empty()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (uint64_t sid : signed_off_ids) {
            sessions_.erase(sid);
            local_callbacks_.erase(sid);
        }
    }

    // Phase 2: Attempt to save config file (apply in memory even if read-only/unwritable)
    const std::string target_save_path =
        !new_cfg.source_path.empty() ? new_cfg.source_path : "./node.cfg";
    result.save_path = target_save_path;
    if (save_to_disk) {
        if (new_cfg.save_to_file(target_save_path)) {
            result.saved_to_disk = true;
            new_cfg.source_path = target_save_path;
        } else {
            result.saved_to_disk = false;
            result.warning = "Config file '" + target_save_path +
                             "' is not writable. Changes remain in memory only while running "
                             "and will be lost on exit.";
        }
    }

    // Phase 3: Stop removed/modified interfaces outside state_mutex_
    std::vector<std::unique_ptr<radio::RadioInterface>> removed_ifaces;
    if (!ifaces_to_stop_ids.empty()) {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            for (auto it = interfaces_.begin(); it != interfaces_.end();) {
                if (*it && ifaces_to_stop_ids.count((*it)->id()) > 0) {
                    iface_by_id_.erase((*it)->id());
                    removed_ifaces.push_back(std::move(*it));
                    it = interfaces_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& rif : removed_ifaces) {
            if (rif) rif->stop();
        }
    }

    // Start added/modified interfaces outside state_mutex_
    std::vector<std::unique_ptr<radio::RadioInterface>> new_ifaces;
    if (!ifaces_to_start_cfgs.empty() && running_.load()) {
        auto& reg = radio::InterfaceRegistry::instance();
        for (const auto& raw_icfg : ifaces_to_start_cfgs) {
            radio::RadioInterfaceConfig icfg = raw_icfg;
            if (icfg.params.find("node_callsign") == icfg.params.end() || icfg.type == "telnet") {
                icfg.params["node_callsign"] = new_cfg.callsign.str();
            }
            std::string create_err;
            auto iface = reg.create(icfg, create_err);
            if (!iface) {
                std::string w = "Interface '" + icfg.id + "': " + create_err;
                log_msg(LOG_ERROR, w);
                if (!result.warning.empty()) result.warning += " ";
                result.warning += w;
                continue;
            }
            std::string start_err;
            auto cb = [this](const ax25::Frame& f, const radio::RadioFrameMeta& meta) {
                this->on_radio_frame(f, meta);
            };
            if (!iface->start(std::move(cb), start_err)) {
                std::string w = "Failed to start interface '" + iface->id() + "': " + start_err;
                log_msg(LOG_ERROR, w);
                if (!result.warning.empty()) result.warning += " ";
                result.warning += w;
                continue;
            }
            log_msg(LOG_INFO, "Interface " + iface->id() + " (" + iface->type() + ") started: " +
                              iface->status_summary());
            new_ifaces.push_back(std::move(iface));
        }
    }

    // Phase 4: Commit updated configuration, reconfigure services, and restore incoming state
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (auto& nif : new_ifaces) {
            if (nif && iface_by_id_.count(nif->id()) == 0) {
                iface_by_id_[nif->id()] = nif.get();
                interfaces_.push_back(std::move(nif));
            }
        }

        config_ = std::move(new_cfg);

        // Reconfigure BBS
        bbs_server_.set_files_enabled(config_.bbs.files_enabled);
        if (config_.bbs.enabled) {
            const std::string bbs_call = config_.resolved_bbs_callsign().str();
            const std::string bbs_alias = config_.bbs.alias.valid() ? config_.bbs.alias.str() : "";
            const std::string mail_dir = config_.resolved_bbs_mail_path();
            const std::string files_dir = config_.resolved_bbs_files_path();
            std::string bbs_err;
            if (!bbs_server_.configure(bbs_call, bbs_alias, mail_dir, files_dir, "incoming",
                                       config_.bbs.welcome, config_.bbs.max_msg_bytes, bbs_err)) {
                log_msg(LOG_WARNING, "BBS reload warning: " + bbs_err);
            }
        }

        // Reconfigure CHAT
        if (config_.chat.enabled) {
            chat_room_.configure(config_.resolved_chat_callsign().str(),
                                 config_.chat.default_topic,
                                 config_.chat.welcome);
        }

        // Re-enable incoming connections ONLY if allowed by config AND not manually disabled by operator
        if (manual_deny_incoming_) {
            accept_incoming_ = false;
        } else {
            accept_incoming_ = config_.accept_incoming_default;
        }

        if (!result.warning.empty()) {
            log_msg(LOG_WARNING, result.warning);
        }
        log_msg(LOG_INFO, "Configuration applied (" + config_.callsign.str() +
                          ", BBS:" + (config_.bbs.enabled ? config_.resolved_bbs_callsign().str() : "OFF") +
                          (config_.bbs.enabled ? (config_.bbs.files_enabled ? " [Files:ON]" : " [Files:OFF]") : "") +
                          ", CHAT:" + (config_.chat.enabled ? config_.resolved_chat_callsign().str() : "OFF") +
                          ", Incoming:" + (accept_incoming_ ? "ALLOW" : "DENY") + ")");
    }

    result.applied = true;
    return result;
}

} // namespace oyk::node
