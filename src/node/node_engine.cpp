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

    if (!logger_.configure(config_.logging, error_out)) {
        return false;
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
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (auto& [id, sess] : sessions_) {
            if (sess && sess->is_active()) {
                auto ev = sess->initiate_disconnect("Node shutting down");
                queue_session_frames(*sess, ev.tx_frames, pending);
            }
        }
        sessions_.clear();
        log_msg(LOG_INFO, "Node shutting down");
    }
    dispatch_tx(pending);

    if (timer_thread_.joinable()) {
        timer_thread_.join();
    }

    std::vector<radio::RadioInterface*> to_stop;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
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
    if (accept_incoming_ != allow) {
        accept_incoming_ = allow;
        log_msg(allow ? LOG_INFO : LOG_WARNING,
                std::string("Incoming connections ") + (allow ? "ALLOWED" : "DENIED"));
    }
}

bool NodeEngine::toggle_accept_incoming() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    accept_incoming_ = !accept_incoming_;
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
        if (!ban_list_.file_path().empty()) {
            if (!ban_list_.save_to_file()) {
                log_msg(LOG_ERROR, "Failed to save banlist to " + ban_list_.file_path());
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
        log_msg(LOG_BAN, msg);
        if (message_out) *message_out = msg;
    }
    dispatch_tx(pending);
    return true;
}

bool NodeEngine::unban_callsign(std::string_view callsign_spec, std::string* message_out) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    std::string removed_key;
    if (ban_list_.remove(callsign_spec, &removed_key)) {
        if (!ban_list_.file_path().empty()) {
            if (!ban_list_.save_to_file()) {
                log_msg(LOG_ERROR, "Failed to save banlist to " + ban_list_.file_path());
            }
        }
        std::string msg = "Unbanned callsign " + removed_key;
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

bool NodeEngine::is_own_callsign(const ax25::Callsign& call) const {
    return is_for_node(call);
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
    oss << " Packet Node\rType HELP or ? for commands.\r" << prompt_string();
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
    auto it = iface_by_id_.find(sess.interface_id());
    if (it == iface_by_id_.end() || !it->second) return;
    for (const auto& f : frames) {
        pending.push_back(PendingTx{it->second, f, sess.peer_endpoint()});
    }
}

void NodeEngine::dispatch_tx(const std::vector<PendingTx>& pending) {
    for (const auto& tx : pending) {
        if (tx.iface) {
            tx.iface->send_frame(tx.frame, tx.endpoint);
        }
    }
}

void NodeEngine::on_radio_frame(const ax25::Frame& frame, const radio::RadioFrameMeta& meta) {
    std::vector<PendingTx> pending;
    std::vector<PendingCommand> pending_cmds;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        const auto now_tp = current_time();

        log_msg(LOG_DEBUG, "RX [" + meta.interface_id + "] " + frame.summary());

        record_heard_locked(frame.source, meta.interface_id, meta.received_at);

        if (frame.type() == ax25::FrameType::UI) {
            return;
        }

        if (!is_for_node(frame.destination)) {
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
                process_session_events(*existing, ev, pending, pending_cmds);
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

                    log_msg(LOG_INFO, "Session #" + std::to_string(sid) + " connected: " +
                                      frame.source.str() + " on " + meta.interface_id);

                    auto banner_ev = raw->queue_text(welcome_banner());
                    queue_session_frames(*raw, banner_ev.tx_frames, pending);
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
            process_session_events(*existing, ev, pending, pending_cmds);
        }
    }
    dispatch_tx(pending);
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
                                        std::vector<PendingCommand>& pending_cmds) {
    queue_session_frames(sess, ev.tx_frames, pending);

    const uint64_t sid = sess.id();

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

    if (sess.mode() == SessionMode::RestrictedBan) {
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
        teardown_bridge_locked(sess, reason, pending);
        sessions_.erase(sid);
    }
}

void NodeEngine::execute_pending_commands(const std::vector<PendingCommand>& pending_cmds) {
    for (const auto& pc : pending_cmds) {
        CommandResult result = command_handler_.execute(*this, pc.session_id, pc.line);

        std::vector<PendingTx> pending;
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

            if (result.connect_request) {
                start_outbound_connect(sess, result.connect_port, result.connect_callsign, pending);
            } else {
                if (!result.response_text.empty()) {
                    auto out_ev = sess.queue_text(result.response_text);
                    queue_session_frames(sess, out_ev.tx_frames, pending);
                }
                if (result.disconnect) {
                    auto disc_ev = sess.initiate_disconnect("User disconnected (BYE)");
                    queue_session_frames(sess, disc_ev.tx_frames, pending);
                    log_msg(LOG_INFO, "Session #" + std::to_string(sess.id()) + " (" +
                                      sess.remote_call().str() + ") disconnected via BYE");
                } else {
                    auto prompt_ev = sess.queue_text(prompt_string());
                    queue_session_frames(sess, prompt_ev.tx_frames, pending);
                }
            }
        }
        dispatch_tx(pending);
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
                process_session_events(*it->second, ev, pending, dummy_cmds);
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
    }
}

NodeSnapshot NodeEngine::snapshot() const {
    NodeSnapshot snap;
    std::lock_guard<std::mutex> lock(state_mutex_);

    snap.node_callsign = config_.callsign.str();
    snap.node_alias = config_.alias.str();
    snap.info_text = config_.info_text;
    snap.accept_incoming = accept_incoming_;

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

radio::RadioInterface* NodeEngine::find_interface(const std::string& id) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = iface_by_id_.find(id);
    return (it != iface_by_id_.end()) ? it->second : nullptr;
}

} // namespace oyk::node
