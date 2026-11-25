// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/ui/gtk2_ui.hpp"
#include "oyk/protocol/yapp.hpp"

#include <gtk/gtk.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace oyk::ui {

namespace {

std::string format_hms(std::chrono::system_clock::time_point tp) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
    localtime_r(&tt, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%H:%M:%S");
    return oss.str();
}

std::string format_duration(uint64_t secs) {
    uint64_t h = secs / 3600;
    uint64_t m = (secs % 3600) / 60;
    uint64_t s = secs % 60;
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << h << ":"
        << std::setw(2) << std::setfill('0') << m << ":"
        << std::setw(2) << std::setfill('0') << s;
    return oss.str();
}

std::string escape_markup(const std::string& text) {
    gchar* esc = g_markup_escape_text(text.c_str(), -1);
    std::string out(esc ? esc : "");
    g_free(esc);
    return out;
}

void add_text_column(GtkTreeView* tv, const char* title, int col_idx, int min_width = -1) {
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(
        title, renderer, "text", col_idx, static_cast<const char*>(nullptr));
    gtk_tree_view_column_set_resizable(col, TRUE);
    if (min_width > 0) {
        gtk_tree_view_column_set_min_width(col, min_width);
    }
    gtk_tree_view_append_column(tv, col);
}

struct GtkAppState {
    node::NodeEngine* engine = nullptr;
    std::atomic<bool>* stop_flag = nullptr;

    GtkWidget* window = nullptr;
    GtkWidget* header_info_label = nullptr;
    GtkWidget* incoming_badge_label = nullptr;
    GtkWidget* sessions_frame = nullptr;
    GtkWidget* status_label = nullptr;

    GtkListStore* iface_store = nullptr;
    GtkListStore* heard_store = nullptr;
    GtkListStore* session_store = nullptr;
    GtkWidget* session_tree = nullptr;

    GtkWidget* log_view = nullptr;
    GtkTextBuffer* log_buffer = nullptr;
    std::string last_rendered_logs;
    GtkWidget* term_window = nullptr;
};

void set_status_text(GtkAppState* st, const std::string& msg) {
    if (st && st->status_label) {
        gtk_label_set_text(GTK_LABEL(st->status_label), msg.c_str());
    }
}

void refresh_ui(GtkAppState* st) {
    node::NodeSnapshot snap = st->engine->snapshot();

    // Header summary
    std::ostringstream hdr;
    hdr << "<b>OYK PACKET NODE: " << escape_markup(snap.node_callsign);
    if (!snap.node_alias.empty()) {
        hdr << " (" << escape_markup(snap.node_alias) << ")";
    }
    hdr << "</b>   |   Uptime: " << format_duration(snap.uptime_seconds)
        << "   |   Active Connections: <b>" << snap.total_active_connections << "</b>";
    gtk_label_set_markup(GTK_LABEL(st->header_info_label), hdr.str().c_str());

    if (snap.accept_incoming) {
        gtk_label_set_markup(
            GTK_LABEL(st->incoming_badge_label),
            "<span background=\"#1e7e34\" foreground=\"#ffffff\" weight=\"bold\">  INCOMING: ALLOW  </span>");
    } else {
        gtk_label_set_markup(
            GTK_LABEL(st->incoming_badge_label),
            "<span background=\"#b71c1c\" foreground=\"#ffffff\" weight=\"bold\">  INCOMING: DENY  </span>");
    }

    // Radio Interfaces table
    gtk_list_store_clear(st->iface_store);
    for (const auto& iface : snap.interfaces) {
        GtkTreeIter iter;
        std::string rxtx = std::to_string(iface.rx_frames) + "/" + std::to_string(iface.tx_frames);
        std::string conn_str = std::to_string(iface.active_connections);
        gtk_list_store_append(st->iface_store, &iter);
        gtk_list_store_set(st->iface_store, &iter,
                           0, iface.id.c_str(),
                           1, iface.type.c_str(),
                           2, conn_str.c_str(),
                           3, iface.status.c_str(),
                           4, rxtx.c_str(),
                           -1);
    }

    // Last 5 New Callsigns Heard table
    gtk_list_store_clear(st->heard_store);
    for (std::size_t i = 0; i < snap.last_new_callsigns.size(); ++i) {
        const auto& rec = snap.last_new_callsigns[i];
        GtkTreeIter iter;
        std::string idx = std::to_string(i + 1) + ".";
        std::string hms = format_hms(rec.first_heard);
        gtk_list_store_append(st->heard_store, &iter);
        gtk_list_store_set(st->heard_store, &iter,
                           0, idx.c_str(),
                           1, rec.callsign.c_str(),
                           2, rec.interface_id.c_str(),
                           3, hms.c_str(),
                           -1);
    }

    // Active Connections frame title & table (preserve selection by session ID if possible)
    uint64_t selected_sid = 0;
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(st->session_tree));
    GtkTreeModel* model = nullptr;
    GtkTreeIter sel_iter;
    if (gtk_tree_selection_get_selected(sel, &model, &sel_iter)) {
        guint64 raw_sid = 0;
        gtk_tree_model_get(model, &sel_iter, 7, &raw_sid, -1);
        selected_sid = static_cast<uint64_t>(raw_sid);
    }

    std::ostringstream sf_title;
    sf_title << "Active Connections (" << snap.active_sessions.size()
             << ")  |  Banned Callsigns: " << snap.ban_entries.size();
    gtk_frame_set_label(GTK_FRAME(st->sessions_frame), sf_title.str().c_str());

    gtk_list_store_clear(st->session_store);
    for (const auto& s : snap.active_sessions) {
        GtkTreeIter iter;
        std::string id_str = "#" + std::to_string(s.id);
        std::string dir_str = (s.direction == node::SessionDirection::Inbound) ? "INBOUND" : "OUTBOUND";
        std::string st_str = node::Session::state_name(s.state);
        std::string since_str = format_hms(s.connected_at);
        std::string role = (s.mode == node::SessionMode::RestrictedBan)
                               ? "BAN NOTICE"
                               : (s.mode == node::SessionMode::LocalTerminal)
                                     ? "LOCAL TERM"
                                     : (s.mode == node::SessionMode::Chat)
                                           ? "CHAT"
                                           : (s.mode == node::SessionMode::Bbs)
                                                 ? "BBS"
                                                 : (s.bridged_session_id ? ("<-> #" + std::to_string(s.bridged_session_id))
                                                                         : "NODE SHELL");
        gtk_list_store_append(st->session_store, &iter);
        gtk_list_store_set(st->session_store, &iter,
                           0, id_str.c_str(),
                           1, s.remote_call.c_str(),
                           2, s.interface_id.c_str(),
                           3, dir_str.c_str(),
                           4, st_str.c_str(),
                           5, since_str.c_str(),
                           6, role.c_str(),
                           7, static_cast<guint64>(s.id),
                           -1);
        if (s.id == selected_sid) {
            gtk_tree_selection_select_iter(sel, &iter);
        }
    }

    // Activity Log
    std::ostringstream log_oss;
    for (const auto& line : snap.recent_logs) {
        log_oss << line << "\n";
    }
    std::string combined = log_oss.str();
    if (combined != st->last_rendered_logs) {
        st->last_rendered_logs = combined;
        gtk_text_buffer_set_text(st->log_buffer, combined.c_str(), -1);
        GtkTextIter end_iter;
        gtk_text_buffer_get_end_iter(st->log_buffer, &end_iter);
        GtkTextMark* mark = gtk_text_buffer_create_mark(st->log_buffer, nullptr, &end_iter, FALSE);
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(st->log_view), mark, 0.0, FALSE, 0.0, 1.0);
        gtk_text_buffer_delete_mark(st->log_buffer, mark);
    }
}

gboolean on_timer_tick(gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);
    if (st->stop_flag && st->stop_flag->load()) {
        gtk_main_quit();
        return FALSE;
    }
    refresh_ui(st);
    return TRUE;
}

void on_allow_incoming_clicked(GtkWidget*, gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);
    st->engine->set_accept_incoming(true);
    set_status_text(st, "New incoming connections are now ALLOWED.");
    refresh_ui(st);
}

void on_deny_incoming_clicked(GtkWidget*, gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);
    st->engine->set_accept_incoming(false);
    set_status_text(st, "New incoming connections are now DENIED.");
    refresh_ui(st);
}

struct GtkTermWindowState {
    GtkAppState* app = nullptr;
    node::NodeEngine* engine = nullptr;
    GtkWidget* win = nullptr;
    GtkWidget* port_combo = nullptr;
    GtkWidget* call_entry = nullptr;
    GtkWidget* btn_connect = nullptr;
    GtkWidget* btn_disc = nullptr;
    GtkWidget* btn_upload = nullptr;
    GtkWidget* btn_abort = nullptr;
    GtkWidget* text_view = nullptr;
    GtkTextBuffer* text_buf = nullptr;
    GtkWidget* input_entry = nullptr;
    GtkWidget* progress_bar = nullptr;
    guint timer_id = 0;

    std::mutex mtx;
    uint64_t sid = 0;
    bool connected = false;
    bool connecting = false;
    bool is_local = false;
    std::string port_id;
    ax25::Callsign remote_call{"UNKNOWN", 0};
    std::string pending_text;
    protocol::YappSession yapp{"incoming"};
    std::string last_yapp_status;
    bool overwrite_prompt_open = false;
};

void gtk_term_append_text_ui(GtkTermWindowState* ts, const std::string& text) {
    if (!ts || !ts->text_buf || text.empty()) return;
    GtkTextIter end_iter;
    gtk_text_buffer_get_end_iter(ts->text_buf, &end_iter);
    gtk_text_buffer_insert(ts->text_buf, &end_iter, text.c_str(), -1);
    gtk_text_buffer_get_end_iter(ts->text_buf, &end_iter);
    GtkTextMark* mark = gtk_text_buffer_create_mark(ts->text_buf, nullptr, &end_iter, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(ts->text_view), mark, 0.0, FALSE, 0.0, 1.0);
    gtk_text_buffer_delete_mark(ts->text_buf, mark);
}

gboolean on_gtk_term_tick(gpointer user_data) {
    auto* ts = static_cast<GtkTermWindowState*>(user_data);
    std::string chunk;
    protocol::YappProgress yp;
    bool is_conn = false;
    bool is_connecting = false;
    bool is_loc = false;
    std::string overwrite_path;

    {
        std::lock_guard<std::mutex> lk(ts->mtx);
        if (!ts->pending_text.empty()) {
            chunk.swap(ts->pending_text);
        }
        yp = ts->yapp.progress();
        is_conn = ts->connected;
        is_connecting = ts->connecting;
        is_loc = ts->is_local;
        if (!is_loc && ts->yapp.awaiting_overwrite() && !ts->overwrite_prompt_open) {
            overwrite_path = yp.target_path;
            ts->overwrite_prompt_open = true;
        }
    }

    if (!chunk.empty()) {
        gtk_term_append_text_ui(ts, chunk);
    }

    gtk_widget_set_sensitive(ts->btn_connect, (!is_conn && !is_connecting) ? TRUE : FALSE);
    gtk_widget_set_sensitive(ts->btn_disc, (is_conn || is_connecting) ? TRUE : FALSE);
    gtk_widget_set_sensitive(ts->btn_upload, (is_conn && !is_loc && !ts->yapp.is_active()) ? TRUE : FALSE);
    gtk_widget_set_sensitive(ts->btn_abort, (!is_loc && ts->yapp.is_active()) ? TRUE : FALSE);

    if (yp.state == protocol::YappState::Idle) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ts->progress_bar), 0.0);
        std::string status_txt = is_conn
            ? ("Connected to " + ts->remote_call.str() + (is_loc ? " (LOCAL - YAPP disabled)" : " (YAPP ready)"))
            : (is_connecting ? "Connecting..." : "Disconnected");
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ts->progress_bar), status_txt.c_str());
    } else {
        double frac = (yp.total_bytes > 0)
                          ? std::min(1.0, static_cast<double>(yp.bytes_transferred) / static_cast<double>(yp.total_bytes))
                          : (yp.state == protocol::YappState::Complete ? 1.0 : 0.0);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ts->progress_bar), frac);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ts->progress_bar), yp.status_message.c_str());
    }

    if (!overwrite_path.empty()) {
        GtkWidget* md = gtk_message_dialog_new(
            GTK_WINDOW(ts->win),
            static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_YES_NO,
            "File already exists:\n%s\n\nOverwrite existing file?",
            overwrite_path.c_str());
        gtk_window_set_title(GTK_WINDOW(md), "YAPP Overwrite Confirmation");
        gint resp = gtk_dialog_run(GTK_DIALOG(md));
        gtk_widget_destroy(md);

        std::lock_guard<std::mutex> lk(ts->mtx);
        ts->overwrite_prompt_open = false;
        ts->yapp.resolve_overwrite(resp == GTK_RESPONSE_YES);
        auto yp2 = ts->yapp.progress();
        if (!yp2.status_message.empty() && yp2.status_message != ts->last_yapp_status) {
            ts->last_yapp_status = yp2.status_message;
            ts->pending_text += "*** " + yp2.status_message + "\n";
        }
    }

    return TRUE;
}

void on_gtk_term_connect(GtkWidget*, gpointer user_data) {
    auto* ts = static_cast<GtkTermWindowState*>(user_data);
    gchar* port_raw = gtk_combo_box_get_active_text(GTK_COMBO_BOX(ts->port_combo));
    std::string port = port_raw ? port_raw : "";
    if (port_raw) g_free(port_raw);
    std::string target = gtk_entry_get_text(GTK_ENTRY(ts->call_entry));
    if (port.empty()) {
        gtk_term_append_text_ui(ts, "*** Select a port.\n");
        return;
    }

    node::NodeEngine::LocalTerminalCallbacks cbs;
    cbs.on_connected = [ts](uint64_t sid) {
        std::lock_guard<std::mutex> lk(ts->mtx);
        if (ts->sid != sid) return;
        ts->connecting = false;
        ts->connected = true;
        ts->pending_text += "*** Connected to " + ts->remote_call.str() + " via " + ts->port_id + "\n";
    };
    cbs.on_rx_bytes = [ts](uint64_t sid, const std::vector<uint8_t>& bytes) {
        std::lock_guard<std::mutex> lk(ts->mtx);
        if (ts->sid != sid) return;
        if (ts->is_local) {
            for (uint8_t b : bytes) {
                if (b == '\r') ts->pending_text.push_back('\n');
                else if (b == '\n' || (b >= 0x20 && b < 0x7F)) ts->pending_text.push_back(static_cast<char>(b));
            }
            return;
        }
        std::vector<uint8_t> plain;
        ts->yapp.feed_bytes(bytes.data(), bytes.size(), plain);
        for (uint8_t b : plain) {
            if (b == '\r') ts->pending_text.push_back('\n');
            else if (b == '\n' || (b >= 0x20 && b < 0x7F)) ts->pending_text.push_back(static_cast<char>(b));
        }
        auto yp = ts->yapp.progress();
        if (!yp.status_message.empty() && yp.status_message != ts->last_yapp_status) {
            ts->last_yapp_status = yp.status_message;
            ts->pending_text += "*** " + yp.status_message + "\n";
        }
    };
    cbs.on_closed = [ts](uint64_t sid, const std::string& reason) {
        std::lock_guard<std::mutex> lk(ts->mtx);
        if (ts->sid != sid) return;
        ts->sid = 0;
        ts->connecting = false;
        ts->connected = false;
        ts->is_local = false;
        ts->yapp.reset();
        ts->pending_text += "*** Disconnected (" + reason + ")\n";
    };

    uint64_t sid = 0;
    ax25::Callsign resolved;
    bool is_local = false;
    std::string err;
    if (ts->engine->open_local_terminal(port, target, std::move(cbs), sid, &resolved, err, &is_local)) {
        std::lock_guard<std::mutex> lk(ts->mtx);
        ts->sid = sid;
        ts->is_local = is_local;
        ts->connecting = !is_local;
        ts->connected = is_local;
        ts->port_id = is_local ? "local" : port;
        ts->remote_call = resolved;
        ts->yapp.set_remote_callsign(resolved);
        if (is_local) {
            ts->pending_text += "*** Connected locally to " + resolved.str() + " (YAPP disabled)\n";
        } else {
            ts->pending_text += "*** Connecting to " + resolved.str() + " on " + port + "...\n";
        }
    } else {
        gtk_term_append_text_ui(ts, "*** Connect error: " + err + "\n");
    }
}

void on_gtk_term_disconnect(GtkWidget*, gpointer user_data) {
    auto* ts = static_cast<GtkTermWindowState*>(user_data);
    uint64_t sid = 0;
    {
        std::lock_guard<std::mutex> lk(ts->mtx);
        sid = ts->sid;
        ts->yapp.cancel("Disconnected");
    }
    if (sid != 0) ts->engine->close_local_terminal(sid);
}

void on_gtk_term_upload(GtkWidget*, gpointer user_data) {
    auto* ts = static_cast<GtkTermWindowState*>(user_data);
    GtkWidget* fc = gtk_file_chooser_dialog_new(
        "YAPP Upload - Select File",
        GTK_WINDOW(ts->win),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
        static_cast<const char*>(nullptr));
    if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
        gchar* fname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
        if (fname) {
            std::lock_guard<std::mutex> lk(ts->mtx);
            std::string err;
            if (!ts->yapp.start_send(fname, err)) {
                ts->pending_text += "*** YAPP upload error: " + err + "\n";
            } else {
                ts->last_yapp_status = ts->yapp.progress().status_message;
                ts->pending_text += "*** " + ts->last_yapp_status + "\n";
            }
            g_free(fname);
        }
    }
    gtk_widget_destroy(fc);
}

void on_gtk_term_abort_yapp(GtkWidget*, gpointer user_data) {
    auto* ts = static_cast<GtkTermWindowState*>(user_data);
    std::lock_guard<std::mutex> lk(ts->mtx);
    ts->yapp.cancel("Cancelled by user");
    ts->pending_text += "*** YAPP transfer cancelled.\n";
}

void on_gtk_term_send_line(GtkWidget*, gpointer user_data) {
    auto* ts = static_cast<GtkTermWindowState*>(user_data);
    std::string line = gtk_entry_get_text(GTK_ENTRY(ts->input_entry));
    gtk_entry_set_text(GTK_ENTRY(ts->input_entry), "");
    uint64_t sid = 0;
    bool is_conn = false;
    {
        std::lock_guard<std::mutex> lk(ts->mtx);
        sid = ts->sid;
        is_conn = ts->connected;
    }
    gtk_term_append_text_ui(ts, "> " + line + "\n");
    if (is_conn && sid != 0) {
        std::string wire = line + "\r";
        std::vector<uint8_t> bytes(wire.begin(), wire.end());
        ts->engine->send_local_terminal_bytes(sid, bytes);
    } else {
        gtk_term_append_text_ui(ts, "*** Not connected.\n");
    }
}

void on_gtk_term_destroy(GtkWidget*, gpointer user_data) {
    auto* ts = static_cast<GtkTermWindowState*>(user_data);
    if (ts->timer_id != 0) {
        g_source_remove(ts->timer_id);
        ts->timer_id = 0;
    }
    uint64_t sid = 0;
    {
        std::lock_guard<std::mutex> lk(ts->mtx);
        sid = ts->sid;
        ts->sid = 0;
        ts->yapp.reset();
    }
    if (sid != 0) ts->engine->close_local_terminal(sid);
    if (ts->app && ts->app->term_window == ts->win) {
        ts->app->term_window = nullptr;
    }
    delete ts;
}

void on_local_terminal_clicked(GtkWidget*, gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);
    if (st->term_window != nullptr) {
        gtk_window_present(GTK_WINDOW(st->term_window));
        return;
    }
    auto* ts = new GtkTermWindowState();
    ts->app = st;
    ts->engine = st->engine;
    ts->yapp.set_send_callback([ts](const std::vector<uint8_t>& pkt) {
        if (ts->sid != 0) {
            ts->engine->send_local_terminal_bytes(ts->sid, pkt);
        }
    });

    ts->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    st->term_window = ts->win;
    gtk_window_set_title(GTK_WINDOW(ts->win), "OYK Local Packet Terminal (YAPP)");
    gtk_window_set_resizable(GTK_WINDOW(ts->win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(ts->win), 720, 480);
    g_signal_connect(ts->win, "destroy", G_CALLBACK(on_gtk_term_destroy), ts);

    GtkWidget* vbox = gtk_vbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
    gtk_container_add(GTK_CONTAINER(ts->win), vbox);

    GtkWidget* top_bar = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(top_bar), gtk_label_new("Port:"), FALSE, FALSE, 2);
    ts->port_combo = gtk_combo_box_new_text();
    gtk_combo_box_append_text(GTK_COMBO_BOX(ts->port_combo), "local");
    auto snap = st->engine->snapshot();
    for (const auto& iface : snap.interfaces) {
        gtk_combo_box_append_text(GTK_COMBO_BOX(ts->port_combo), iface.id.c_str());
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(ts->port_combo), 0);
    gtk_box_pack_start(GTK_BOX(top_bar), ts->port_combo, FALSE, FALSE, 2);

    gtk_box_pack_start(GTK_BOX(top_bar), gtk_label_new("Callsign:"), FALSE, FALSE, 2);
    ts->call_entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(ts->call_entry), 10);
    gtk_box_pack_start(GTK_BOX(top_bar), ts->call_entry, TRUE, TRUE, 2);

    ts->btn_connect = gtk_button_new_with_label("Connect");
    ts->btn_disc = gtk_button_new_with_label("Disconnect");
    ts->btn_upload = gtk_button_new_with_label("YAPP Upload...");
    ts->btn_abort = gtk_button_new_with_label("Abort YAPP");
    g_signal_connect(ts->btn_connect, "clicked", G_CALLBACK(on_gtk_term_connect), ts);
    g_signal_connect(ts->btn_disc, "clicked", G_CALLBACK(on_gtk_term_disconnect), ts);
    g_signal_connect(ts->btn_upload, "clicked", G_CALLBACK(on_gtk_term_upload), ts);
    g_signal_connect(ts->btn_abort, "clicked", G_CALLBACK(on_gtk_term_abort_yapp), ts);
    g_signal_connect(ts->call_entry, "activate", G_CALLBACK(on_gtk_term_connect), ts);

    gtk_box_pack_start(GTK_BOX(top_bar), ts->btn_connect, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(top_bar), ts->btn_disc, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(top_bar), ts->btn_upload, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(top_bar), ts->btn_abort, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), top_bar, FALSE, FALSE, 2);

    ts->text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(ts->text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ts->text_view), GTK_WRAP_WORD_CHAR);
    PangoFontDescription* mono = pango_font_description_from_string("Monospace 9");
    gtk_widget_modify_font(ts->text_view, mono);
    pango_font_description_free(mono);
    ts->text_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ts->text_view));

    GtkWidget* sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sc), ts->text_view);
    gtk_box_pack_start(GTK_BOX(vbox), sc, TRUE, TRUE, 2);

    ts->progress_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(vbox), ts->progress_bar, FALSE, FALSE, 2);

    GtkWidget* in_bar = gtk_hbox_new(FALSE, 4);
    ts->input_entry = gtk_entry_new();
    GtkWidget* btn_send = gtk_button_new_with_label("Send");
    g_signal_connect(ts->input_entry, "activate", G_CALLBACK(on_gtk_term_send_line), ts);
    g_signal_connect(btn_send, "clicked", G_CALLBACK(on_gtk_term_send_line), ts);
    gtk_box_pack_start(GTK_BOX(in_bar), ts->input_entry, TRUE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(in_bar), btn_send, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), in_bar, FALSE, FALSE, 2);

    gtk_term_append_text_ui(
        ts,
        "*** Local Packet Terminal (YAPP / YAPP-C enabled)\n"
        "*** Incoming files save to incoming/<CALLSIGN-SSID>/<filename>\n"
        "*** Enter target callsign (or CALLSIGN@ip:port) and click Connect.\n");

    ts->timer_id = g_timeout_add(50, on_gtk_term_tick, ts);
    gtk_widget_show_all(ts->win);
}

void on_terminate_clicked(GtkWidget*, gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);
    auto snap = st->engine->snapshot();
    if (snap.active_sessions.empty()) {
        set_status_text(st, "No active connections to terminate.");
        return;
    }

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Terminate Connection",
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "Terminate", GTK_RESPONSE_ACCEPT,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        static_cast<const char*>(nullptr));

    GtkWidget* vbox = GTK_DIALOG(dlg)->vbox;
    GtkWidget* lbl = gtk_label_new("Select an active connection to terminate:");
    gtk_misc_set_alignment(GTK_MISC(lbl), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 6);

    GtkWidget* combo = gtk_combo_box_new_text();
    std::vector<uint64_t> ids;
    for (const auto& s : snap.active_sessions) {
        std::string item = "#" + std::to_string(s.id) + "  " + s.remote_call +
                           " on " + s.interface_id + " [" + node::Session::state_name(s.state) + "]";
        gtk_combo_box_append_text(GTK_COMBO_BOX(combo), item.c_str());
        ids.push_back(s.id);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_box_pack_start(GTK_BOX(vbox), combo, FALSE, FALSE, 6);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        gint idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
        if (idx >= 0 && static_cast<std::size_t>(idx) < ids.size()) {
            std::string msg;
            st->engine->terminate_session(ids[static_cast<std::size_t>(idx)], &msg);
            set_status_text(st, msg);
        }
    }
    gtk_widget_destroy(dlg);
    refresh_ui(st);
}

void populate_ban_dialog_store(node::NodeEngine* engine, GtkListStore* store) {
    gtk_list_store_clear(store);
    auto snap = engine->snapshot();
    for (const auto& e : snap.ban_entries) {
        GtkTreeIter iter;
        std::string type_s = e.hard_ban() ? "HARD" : "SOFT";
        std::string flags_s = e.format_flags_binary();
        std::string lifts_s = e.format_lift_at();
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           0, e.key.c_str(),
                           1, type_s.c_str(),
                           2, flags_s.c_str(),
                           3, lifts_s.c_str(),
                           4, e.reason.c_str(),
                           -1);
    }
}

void on_ban_clicked(GtkWidget*, gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Ban / Unban Callsign (banlist.cfg)",
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "Ban Callsign", 1,
        "Unban Selected", 2,
        GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
        static_cast<const char*>(nullptr));
    gtk_window_set_default_size(GTK_WINDOW(dlg), 560, 380);

    GtkWidget* vbox = GTK_DIALOG(dlg)->vbox;

    GtkWidget* table = gtk_table_new(4, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 4);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 6);

    GtkWidget* call_lbl = gtk_label_new("Callsign (e.g. B4DCAL or B4DCAL-7):");
    gtk_misc_set_alignment(GTK_MISC(call_lbl), 0.0f, 0.5f);
    GtkWidget* call_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(call_entry), 9);

    GtkWidget* lift_lbl = gtk_label_new("Lift (permanent, 30m, 12h, 7d, YYYY-MM-DD):");
    gtk_misc_set_alignment(GTK_MISC(lift_lbl), 0.0f, 0.5f);
    GtkWidget* lift_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(lift_entry), "permanent");

    GtkWidget* reason_lbl = gtk_label_new("Reason:");
    gtk_misc_set_alignment(GTK_MISC(reason_lbl), 0.0f, 0.5f);
    GtkWidget* reason_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(reason_entry), "Banned by sysop.");

    GtkWidget* chk_hard = gtk_check_button_new_with_label("Hard Ban (block after initial notice)");
    GtkWidget* chk_appeal = gtk_check_button_new_with_label("Appeal Allowed");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_appeal), TRUE);

    gtk_table_attach(GTK_TABLE(table), call_lbl, 0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), call_entry, 1, 2, 0, 1,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), lift_lbl, 0, 1, 1, 2, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), lift_entry, 1, 2, 1, 2,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), reason_lbl, 0, 1, 2, 3, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), reason_entry, 1, 2, 2, 3,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), chk_hard, 0, 1, 3, 4, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), chk_appeal, 1, 2, 3, 4, GTK_FILL, GTK_FILL, 0, 0);

    gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 4);

    GtkListStore* ban_store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING,
                                                 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* ban_tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ban_store));
    add_text_column(GTK_TREE_VIEW(ban_tv), "Callsign", 0, 90);
    add_text_column(GTK_TREE_VIEW(ban_tv), "Type", 1, 60);
    add_text_column(GTK_TREE_VIEW(ban_tv), "Flags", 2, 70);
    add_text_column(GTK_TREE_VIEW(ban_tv), "Lifts", 3, 110);
    add_text_column(GTK_TREE_VIEW(ban_tv), "Reason", 4, 180);

    GtkWidget* sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sc), ban_tv);
    gtk_box_pack_start(GTK_BOX(vbox), sc, TRUE, TRUE, 4);

    GtkWidget* fb_lbl = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(fb_lbl), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), fb_lbl, FALSE, FALSE, 4);

    populate_ban_dialog_store(st->engine, ban_store);
    gtk_widget_show_all(dlg);

    while (true) {
        gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
        if (resp == 1) {
            std::string call = gtk_entry_get_text(GTK_ENTRY(call_entry));
            std::string lift = gtk_entry_get_text(GTK_ENTRY(lift_entry));
            std::string reason = gtk_entry_get_text(GTK_ENTRY(reason_entry));
            uint32_t flags = 0;
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_hard))) {
                flags |= node::BAN_FLAG_HARD_BAN;
            }
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_appeal))) {
                flags |= node::BAN_FLAG_APPEAL_ALLOWED;
            }
            std::string msg;
            if (st->engine->ban_callsign_with_lift_spec(call, lift, &msg, true, flags, reason)) {
                gtk_entry_set_text(GTK_ENTRY(call_entry), "");
                gtk_entry_set_text(GTK_ENTRY(lift_entry), "permanent");
                gtk_label_set_text(GTK_LABEL(fb_lbl), msg.c_str());
                set_status_text(st, msg);
                populate_ban_dialog_store(st->engine, ban_store);
                refresh_ui(st);
            } else {
                gtk_label_set_text(GTK_LABEL(fb_lbl), ("Error: " + msg).c_str());
            }
        } else if (resp == 2) {
            GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(ban_tv));
            GtkTreeModel* model = nullptr;
            GtkTreeIter iter;
            if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
                gchar* key = nullptr;
                gtk_tree_model_get(model, &iter, 0, &key, -1);
                if (key) {
                    GtkWidget* confirm = gtk_message_dialog_new(
                        GTK_WINDOW(dlg),
                        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                        GTK_MESSAGE_QUESTION,
                        GTK_BUTTONS_YES_NO,
                        "Unban callsign %s?", key);
                    gtk_window_set_title(GTK_WINDOW(confirm), "Confirm Unban");
                    gint cresp = gtk_dialog_run(GTK_DIALOG(confirm));
                    gtk_widget_destroy(confirm);
                    if (cresp == GTK_RESPONSE_YES) {
                        std::string msg;
                        st->engine->unban_callsign(key, &msg);
                        gtk_label_set_text(GTK_LABEL(fb_lbl), msg.c_str());
                        set_status_text(st, msg);
                        populate_ban_dialog_store(st->engine, ban_store);
                        refresh_ui(st);
                    }
                    g_free(key);
                }
            }
        } else {
            break;
        }
    }

    g_object_unref(ban_store);
    gtk_widget_destroy(dlg);
}

void populate_incoming_store(node::NodeEngine* engine, GtkListStore* store) {
    gtk_list_store_clear(store);
    auto items = engine->list_pending_incoming_files();
    for (const auto& pf : items) {
        GtkTreeIter iter;
        std::string sz_str = std::to_string(pf.size_bytes) + " B";
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           0, pf.uploader.c_str(),
                           1, pf.filename.c_str(),
                           2, sz_str.c_str(),
                           -1);
    }
}

void populate_approved_store(node::NodeEngine* engine, GtkListStore* store) {
    gtk_list_store_clear(store);
    auto items = engine->list_approved_files();
    for (const auto& af : items) {
        GtkTreeIter iter;
        std::string sz_str = std::to_string(af.size_bytes) + " B";
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           0, af.rel_path.c_str(),
                           1, sz_str.c_str(),
                           -1);
    }
}

void on_approve_files_clicked(GtkWidget*, gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Manage BBS Files Area",
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "Refresh", 3,
        GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
        static_cast<const char*>(nullptr));
    gtk_window_set_default_size(GTK_WINDOW(dlg), 600, 420);

    GtkWidget* vbox = GTK_DIALOG(dlg)->vbox;

    std::string hdr_txt = "Incoming: " + st->engine->bbs_incoming_path() +
                          "   |   BBS Files Area: " + st->engine->bbs_files_path();
    GtkWidget* hdr_lbl = gtk_label_new(hdr_txt.c_str());
    gtk_misc_set_alignment(GTK_MISC(hdr_lbl), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), hdr_lbl, FALSE, FALSE, 4);

    GtkWidget* nb = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), nb, TRUE, TRUE, 4);

    // Tab 1: Pending Incoming
    GtkWidget* p_vbox = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(p_vbox), 6);
    GtkListStore* inc_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* inc_tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(inc_store));
    add_text_column(GTK_TREE_VIEW(inc_tv), "Uploader", 0, 120);
    add_text_column(GTK_TREE_VIEW(inc_tv), "Filename", 1, 260);
    add_text_column(GTK_TREE_VIEW(inc_tv), "Size", 2, 90);

    GtkWidget* inc_sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(inc_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(inc_sc), inc_tv);
    gtk_box_pack_start(GTK_BOX(p_vbox), inc_sc, TRUE, TRUE, 0);

    GtkWidget* sub_hbox = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(sub_hbox),
                       gtk_label_new("Destination subfolder (blank = root):"),
                       FALSE, FALSE, 2);
    GtkWidget* sub_entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(sub_hbox), sub_entry, TRUE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(p_vbox), sub_hbox, FALSE, FALSE, 0);

    GtkWidget* p_btns = gtk_hbox_new(FALSE, 6);
    GtkWidget* btn_approve = gtk_button_new_with_label("Approve & Move");
    GtkWidget* btn_del_inc = gtk_button_new_with_label("Delete / Reject");
    gtk_box_pack_start(GTK_BOX(p_btns), btn_approve, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(p_btns), btn_del_inc, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(p_vbox), p_btns, FALSE, FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), p_vbox, gtk_label_new("Pending Incoming"));

    // Tab 2: Approved Files
    GtkWidget* a_vbox = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(a_vbox), 6);
    GtkListStore* app_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* app_tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app_store));
    add_text_column(GTK_TREE_VIEW(app_tv), "Approved File Path", 0, 380);
    add_text_column(GTK_TREE_VIEW(app_tv), "Size", 1, 90);

    GtkWidget* app_sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(app_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(app_sc), app_tv);
    gtk_box_pack_start(GTK_BOX(a_vbox), app_sc, TRUE, TRUE, 0);

    GtkWidget* a_btns = gtk_hbox_new(FALSE, 6);
    GtkWidget* btn_rem_app = gtk_button_new_with_label("Remove Selected File");
    gtk_box_pack_start(GTK_BOX(a_btns), btn_rem_app, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(a_vbox), a_btns, FALSE, FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), a_vbox, gtk_label_new("Approved Files"));

    GtkWidget* fb_lbl = gtk_label_new("Manage pending uploads and approved BBS files.");
    gtk_misc_set_alignment(GTK_MISC(fb_lbl), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), fb_lbl, FALSE, FALSE, 4);

    struct FilesDlgCtx {
        GtkAppState* app;
        GtkWidget* dlg;
        GtkListStore* inc_store;
        GtkWidget* inc_tv;
        GtkWidget* sub_entry;
        GtkListStore* app_store;
        GtkWidget* app_tv;
        GtkWidget* fb_lbl;
    } fctx{ st, dlg, inc_store, inc_tv, sub_entry, app_store, app_tv, fb_lbl };

    auto cb_approve = +[](GtkWidget*, gpointer data) {
        auto* c = static_cast<FilesDlgCtx*>(data);
        GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(c->inc_tv));
        GtkTreeModel* model = nullptr;
        GtkTreeIter iter;
        if (!gtk_tree_selection_get_selected(sel, &model, &iter)) {
            gtk_label_set_text(GTK_LABEL(c->fb_lbl), "Select a pending file first.");
            return;
        }
        gchar* uploader_raw = nullptr;
        gchar* fname_raw = nullptr;
        gtk_tree_model_get(model, &iter, 0, &uploader_raw, 1, &fname_raw, -1);
        std::string uploader = uploader_raw ? uploader_raw : "";
        std::string fname = fname_raw ? fname_raw : "";
        if (uploader_raw) g_free(uploader_raw);
        if (fname_raw) g_free(fname_raw);

        std::string subdir = gtk_entry_get_text(GTK_ENTRY(c->sub_entry));
        std::string dest_path, err;
        bool exists = false;
        bool ok = c->app->engine->approve_incoming_file(uploader, fname, subdir,
                                                        false, dest_path, exists, err);
        if (!ok && exists) {
            GtkWidget* md = gtk_message_dialog_new(
                GTK_WINDOW(c->dlg),
                static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
                "File already exists in BBS files area:\n%s\n\nOverwrite existing file?",
                dest_path.c_str());
            gtk_window_set_title(GTK_WINDOW(md), "Confirm Overwrite");
            gint ow = gtk_dialog_run(GTK_DIALOG(md));
            gtk_widget_destroy(md);
            if (ow == GTK_RESPONSE_YES) {
                ok = c->app->engine->approve_incoming_file(uploader, fname, subdir,
                                                           true, dest_path, exists, err);
            } else {
                gtk_label_set_text(GTK_LABEL(c->fb_lbl), "Overwrite declined; file remains in incoming/.");
                return;
            }
        }
        if (ok) {
            std::string msg = "Approved " + uploader + "/" + fname + " -> " + dest_path;
            gtk_label_set_text(GTK_LABEL(c->fb_lbl), msg.c_str());
            set_status_text(c->app, msg);
            populate_incoming_store(c->app->engine, c->inc_store);
            populate_approved_store(c->app->engine, c->app_store);
            refresh_ui(c->app);
        } else {
            gtk_label_set_text(GTK_LABEL(c->fb_lbl), ("Error: " + err).c_str());
        }
    };

    auto cb_del_inc = +[](GtkWidget*, gpointer data) {
        auto* c = static_cast<FilesDlgCtx*>(data);
        GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(c->inc_tv));
        GtkTreeModel* model = nullptr;
        GtkTreeIter iter;
        if (!gtk_tree_selection_get_selected(sel, &model, &iter)) {
            gtk_label_set_text(GTK_LABEL(c->fb_lbl), "Select a pending file first.");
            return;
        }
        gchar* uploader_raw = nullptr;
        gchar* fname_raw = nullptr;
        gtk_tree_model_get(model, &iter, 0, &uploader_raw, 1, &fname_raw, -1);
        std::string uploader = uploader_raw ? uploader_raw : "";
        std::string fname = fname_raw ? fname_raw : "";
        if (uploader_raw) g_free(uploader_raw);
        if (fname_raw) g_free(fname_raw);

        GtkWidget* confirm = gtk_message_dialog_new(
            GTK_WINDOW(c->dlg),
            static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
            "Delete incoming file %s/%s?", uploader.c_str(), fname.c_str());
        gtk_window_set_title(GTK_WINDOW(confirm), "Confirm Delete");
        gint cresp = gtk_dialog_run(GTK_DIALOG(confirm));
        gtk_widget_destroy(confirm);
        if (cresp == GTK_RESPONSE_YES) {
            std::string err;
            if (c->app->engine->delete_incoming_file(uploader, fname, err)) {
                std::string msg = "Deleted incoming file " + uploader + "/" + fname;
                gtk_label_set_text(GTK_LABEL(c->fb_lbl), msg.c_str());
                set_status_text(c->app, msg);
                populate_incoming_store(c->app->engine, c->inc_store);
                refresh_ui(c->app);
            } else {
                gtk_label_set_text(GTK_LABEL(c->fb_lbl), ("Error: " + err).c_str());
            }
        }
    };

    auto cb_rem_app = +[](GtkWidget*, gpointer data) {
        auto* c = static_cast<FilesDlgCtx*>(data);
        GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(c->app_tv));
        GtkTreeModel* model = nullptr;
        GtkTreeIter iter;
        if (!gtk_tree_selection_get_selected(sel, &model, &iter)) {
            gtk_label_set_text(GTK_LABEL(c->fb_lbl), "Select an approved file first.");
            return;
        }
        gchar* rel_raw = nullptr;
        gtk_tree_model_get(model, &iter, 0, &rel_raw, -1);
        std::string rel = rel_raw ? rel_raw : "";
        if (rel_raw) g_free(rel_raw);

        GtkWidget* confirm = gtk_message_dialog_new(
            GTK_WINDOW(c->dlg),
            static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
            "Remove approved file /%s from the BBS files area?", rel.c_str());
        gtk_window_set_title(GTK_WINDOW(confirm), "Confirm Remove");
        gint cresp = gtk_dialog_run(GTK_DIALOG(confirm));
        gtk_widget_destroy(confirm);
        if (cresp == GTK_RESPONSE_YES) {
            std::string err;
            if (c->app->engine->delete_approved_file(rel, err)) {
                std::string msg = "Removed approved file /" + rel;
                gtk_label_set_text(GTK_LABEL(c->fb_lbl), msg.c_str());
                set_status_text(c->app, msg);
                populate_approved_store(c->app->engine, c->app_store);
                refresh_ui(c->app);
            } else {
                gtk_label_set_text(GTK_LABEL(c->fb_lbl), ("Error: " + err).c_str());
            }
        }
    };

    g_signal_connect(btn_approve, "clicked", G_CALLBACK(cb_approve), &fctx);
    g_signal_connect(btn_del_inc, "clicked", G_CALLBACK(cb_del_inc), &fctx);
    g_signal_connect(btn_rem_app, "clicked", G_CALLBACK(cb_rem_app), &fctx);

    populate_incoming_store(st->engine, inc_store);
    populate_approved_store(st->engine, app_store);
    gtk_widget_show_all(dlg);

    while (true) {
        gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
        if (resp == 3) {
            populate_incoming_store(st->engine, inc_store);
            populate_approved_store(st->engine, app_store);
            gtk_label_set_text(GTK_LABEL(fb_lbl), "Refreshed pending and approved file lists.");
            continue;
        }
        break;
    }

    g_object_unref(inc_store);
    g_object_unref(app_store);
    gtk_widget_destroy(dlg);
}

void on_rig_control_clicked(GtkWidget*, gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);
    auto& rig = st->engine->rig_controller();
    auto rc = rig.config();

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Radio / Rig Control",
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
        static_cast<const char*>(nullptr));
    gtk_window_set_default_size(GTK_WINDOW(dlg), 500, 340);

    GtkWidget* vbox = GTK_DIALOG(dlg)->vbox;
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

    std::string ep_str = "Protocol: " + radio::rig_protocol_to_string(rc.protocol) +
                         "   |   Endpoint: " + rc.host + ":" + std::to_string(rc.port);
    GtkWidget* lbl_hdr = gtk_label_new(ep_str.c_str());
    gtk_misc_set_alignment(GTK_MISC(lbl_hdr), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_hdr, FALSE, FALSE, 4);

    GtkWidget* lbl_state = gtk_label_new("Querying radio...");
    gtk_misc_set_alignment(GTK_MISC(lbl_state), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_state, FALSE, FALSE, 4);

    GtkWidget* lbl_fb = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(lbl_fb), 0.0f, 0.5f);

    struct RigDlgCtx {
        GtkAppState* app = nullptr;
        GtkWidget* lbl_state = nullptr;
        GtkWidget* lbl_fb = nullptr;
        GtkWidget* e_freq = nullptr;
        GtkWidget* e_mode = nullptr;
        GtkWidget* e_pb = nullptr;
        GtkWidget* e_vfo = nullptr;
        GtkWidget* sp_zone = nullptr;
        GtkWidget* sp_pos = nullptr;
    } rctx;
    rctx.app = st;
    rctx.lbl_state = lbl_state;
    rctx.lbl_fb = lbl_fb;

    auto update_view = +[](RigDlgCtx* c) {
        auto& rg = c->app->engine->rig_controller();
        auto rcfg = rg.config();
        auto rs = rg.status();
        std::ostringstream oss;
        if (!rcfg.enabled || rcfg.protocol == radio::RigProtocol::None) {
            oss << "Rig control is disabled. Configure it in Node -> Settings -> Rig.";
        } else if (rcfg.protocol == radio::RigProtocol::RadioFoundry) {
            oss << "Status: " << (rs.reachable ? "ONLINE" : "NO REPLY")
                << "\nZone: " << rs.zone << "   Pos: " << rs.pos
                << "   Name: " << (rs.channel_name.empty() ? "(unknown)" : rs.channel_name)
                << "\nFreq: " << rs.freq_hz << " Hz";
            if (rs.is_dmr) {
                oss << "   |   DMR CC:" << rs.color_code
                    << " TS:" << rs.timeslot << " TG:" << rs.talkgroup;
            }
            oss << "\nNotifications: " << (rs.notifications_active ? "ACTIVE" : "OFF");
            if (!rs.raw_status.empty()) oss << "\nRaw: " << rs.raw_status;
            if (c->sp_zone && rs.zone >= 0) gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->sp_zone), rs.zone);
            if (c->sp_pos && rs.pos >= 0) gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->sp_pos), rs.pos);
        } else {
            oss << "Status: " << (rs.reachable ? "ONLINE" : "NO REPLY")
                << "\nFrequency: " << rs.freq_hz << " Hz"
                << "\nMode: " << (rs.mode.empty() ? "(unknown)" : rs.mode)
                << " (" << rs.passband_hz << " Hz)"
                << "   |   PTT: " << (rs.ptt > 0 ? "TX" : (rs.ptt == 0 ? "RX" : "UNKNOWN"))
                << "   |   VFO: " << (rs.vfo.empty() ? "(unknown)" : rs.vfo);
            if (c->e_freq && rs.freq_hz > 0) gtk_entry_set_text(GTK_ENTRY(c->e_freq), std::to_string(rs.freq_hz).c_str());
            if (c->e_mode && !rs.mode.empty()) gtk_entry_set_text(GTK_ENTRY(c->e_mode), rs.mode.c_str());
            if (c->e_pb) gtk_entry_set_text(GTK_ENTRY(c->e_pb), std::to_string(rs.passband_hz).c_str());
            if (c->e_vfo && !rs.vfo.empty()) gtk_entry_set_text(GTK_ENTRY(c->e_vfo), rs.vfo.c_str());
        }
        gtk_label_set_text(GTK_LABEL(c->lbl_state), oss.str().c_str());
    };

    if (rc.protocol == radio::RigProtocol::RadioFoundry) {
        GtkWidget* tbl = gtk_table_new(3, 4, FALSE);
        gtk_table_set_row_spacings(GTK_TABLE(tbl), 6);
        gtk_table_set_col_spacings(GTK_TABLE(tbl), 6);

        rctx.sp_zone = gtk_spin_button_new_with_range(0.0, 999.0, 1.0);
        rctx.sp_pos = gtk_spin_button_new_with_range(0.0, 999.0, 1.0);
        GtkWidget* btn_sel = gtk_button_new_with_label("Select Zone & Pos");
        GtkWidget* btn_ch_dn = gtk_button_new_with_label("Channel -1");
        GtkWidget* btn_ch_up = gtk_button_new_with_label("Channel +1");
        GtkWidget* btn_z_dn = gtk_button_new_with_label("Zone -1");
        GtkWidget* btn_z_up = gtk_button_new_with_label("Zone +1");
        GtkWidget* btn_ref = gtk_button_new_with_label("Query (select)");
        GtkWidget* btn_stopn = gtk_button_new_with_label("Stop Notify");

        gtk_table_attach_defaults(GTK_TABLE(tbl), gtk_label_new("Zone (0-based):"), 0, 1, 0, 1);
        gtk_table_attach_defaults(GTK_TABLE(tbl), rctx.sp_zone, 1, 2, 0, 1);
        gtk_table_attach_defaults(GTK_TABLE(tbl), gtk_label_new("Pos (0-based):"), 2, 3, 0, 1);
        gtk_table_attach_defaults(GTK_TABLE(tbl), rctx.sp_pos, 3, 4, 0, 1);
        gtk_table_attach_defaults(GTK_TABLE(tbl), btn_sel, 0, 2, 1, 2);
        gtk_table_attach_defaults(GTK_TABLE(tbl), btn_ref, 2, 3, 1, 2);
        gtk_table_attach_defaults(GTK_TABLE(tbl), btn_stopn, 3, 4, 1, 2);
        gtk_table_attach_defaults(GTK_TABLE(tbl), btn_ch_dn, 0, 1, 2, 3);
        gtk_table_attach_defaults(GTK_TABLE(tbl), btn_ch_up, 1, 2, 2, 3);
        gtk_table_attach_defaults(GTK_TABLE(tbl), btn_z_dn, 2, 3, 2, 3);
        gtk_table_attach_defaults(GTK_TABLE(tbl), btn_z_up, 3, 4, 2, 3);
        gtk_box_pack_start(GTK_BOX(vbox), tbl, FALSE, FALSE, 8);

        g_signal_connect(btn_sel, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* c = static_cast<RigDlgCtx*>(d);
            int z = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(c->sp_zone));
            int p = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(c->sp_pos));
            std::string err;
            if (c->app->engine->rig_controller().select_zone_channel(z, p, err))
                gtk_label_set_text(GTK_LABEL(c->lbl_fb), "Selected zone & channel.");
            else gtk_label_set_text(GTK_LABEL(c->lbl_fb), err.c_str());
            auto st2 = c->app->engine->rig_controller().status();
            (void)st2;
        }), &rctx);
        g_signal_connect(btn_ref, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* c = static_cast<RigDlgCtx*>(d);
            std::string err;
            if (c->app->engine->rig_controller().refresh(err))
                gtk_label_set_text(GTK_LABEL(c->lbl_fb), "Refreshed.");
            else gtk_label_set_text(GTK_LABEL(c->lbl_fb), err.c_str());
        }), &rctx);
        g_signal_connect(btn_stopn, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* c = static_cast<RigDlgCtx*>(d);
            std::string err;
            if (c->app->engine->rig_controller().stop_notifications(err))
                gtk_label_set_text(GTK_LABEL(c->lbl_fb), "Notifications stopped.");
            else gtk_label_set_text(GTK_LABEL(c->lbl_fb), err.c_str());
        }), &rctx);
        g_signal_connect(btn_ch_dn, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* c = static_cast<RigDlgCtx*>(d);
            std::string err;
            if (c->app->engine->rig_controller().step_channel(-1, err))
                gtk_label_set_text(GTK_LABEL(c->lbl_fb), "Stepped channel -1.");
            else gtk_label_set_text(GTK_LABEL(c->lbl_fb), err.c_str());
        }), &rctx);
        g_signal_connect(btn_ch_up, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* c = static_cast<RigDlgCtx*>(d);
            std::string err;
            if (c->app->engine->rig_controller().step_channel(1, err))
                gtk_label_set_text(GTK_LABEL(c->lbl_fb), "Stepped channel +1.");
            else gtk_label_set_text(GTK_LABEL(c->lbl_fb), err.c_str());
        }), &rctx);
        g_signal_connect(btn_z_dn, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* c = static_cast<RigDlgCtx*>(d);
            std::string err;
            if (c->app->engine->rig_controller().step_zone(-1, err))
                gtk_label_set_text(GTK_LABEL(c->lbl_fb), "Stepped zone -1.");
            else gtk_label_set_text(GTK_LABEL(c->lbl_fb), err.c_str());
        }), &rctx);
        g_signal_connect(btn_z_up, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* c = static_cast<RigDlgCtx*>(d);
            std::string err;
            if (c->app->engine->rig_controller().step_zone(1, err))
                gtk_label_set_text(GTK_LABEL(c->lbl_fb), "Stepped zone +1.");
            else gtk_label_set_text(GTK_LABEL(c->lbl_fb), err.c_str());
        }), &rctx);

        for (GtkWidget* b : { btn_sel, btn_ref, btn_stopn, btn_ch_dn, btn_ch_up, btn_z_dn, btn_z_up }) {
            g_signal_connect_after(b, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
                auto* c = static_cast<RigDlgCtx*>(d);
                auto& rg = c->app->engine->rig_controller();
                auto rs = rg.status();
                std::ostringstream oss;
                oss << "Status: " << (rs.reachable ? "ONLINE" : "NO REPLY")
                    << "\nZone: " << rs.zone << "   Pos: " << rs.pos
                    << "   Name: " << (rs.channel_name.empty() ? "(unknown)" : rs.channel_name)
                    << "\nFreq: " << rs.freq_hz << " Hz";
                if (rs.is_dmr) {
                    oss << "   |   DMR CC:" << rs.color_code
                        << " TS:" << rs.timeslot << " TG:" << rs.talkgroup;
                }
                oss << "\nNotifications: " << (rs.notifications_active ? "ACTIVE" : "OFF");
                if (!rs.raw_status.empty()) oss << "\nRaw: " << rs.raw_status;
                gtk_label_set_text(GTK_LABEL(c->lbl_state), oss.str().c_str());
                if (rs.zone >= 0) gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->sp_zone), rs.zone);
                if (rs.pos >= 0) gtk_spin_button_set_value(GTK_SPIN_BUTTON(c->sp_pos), rs.pos);
            }), &rctx);
        }
    } else {
        GtkWidget* tbl = gtk_table_new(4, 3, FALSE);
        gtk_table_set_row_spacings(GTK_TABLE(tbl), 6);
        gtk_table_set_col_spacings(GTK_TABLE(tbl), 6);

        rctx.e_freq = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(rctx.e_freq), "145050000");
        rctx.e_mode = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(rctx.e_mode), "FM");
        rctx.e_pb = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(rctx.e_pb), "0");
        rctx.e_vfo = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(rctx.e_vfo), "VFOA");

        GtkWidget* btn_f = gtk_button_new_with_label("Set Freq");
        GtkWidget* btn_m = gtk_button_new_with_label("Set Mode");
        GtkWidget* btn_v = gtk_button_new_with_label("Set VFO");
        GtkWidget* btn_ptt = gtk_button_new_with_label("Toggle PTT");
        GtkWidget* btn_ref = gtk_button_new_with_label("Refresh");

        gtk_table_attach_defaults(GTK_TABLE(tbl), gtk_label_new("Freq (Hz):"), 0, 1, 0, 1);
        gtk_table_attach_defaults(GTK_TABLE(tbl), rctx.e_freq, 1, 2, 0, 1);
        gtk_table_attach_defaults(GTK_TABLE(tbl), btn_f, 2, 3, 0, 1);
        gtk_table_attach_defaults(GTK_TABLE(tbl), gtk_label_new("Mode / PB:"), 0, 1, 1, 2);
        GtkWidget* mbox = gtk_hbox_new(FALSE, 4);
        gtk_box_pack_start(GTK_BOX(mbox), rctx.e_mode, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(mbox), rctx.e_pb, TRUE, TRUE, 0);
        gtk_table_attach_defaults(GTK_TABLE(tbl), mbox, 1, 2, 1, 2);
        gtk_table_attach_defaults(GTK_TABLE(tbl), btn_m, 2, 3, 1, 2);
        gtk_table_attach_defaults(GTK_TABLE(tbl), gtk_label_new("VFO:"), 0, 1, 2, 3);
        gtk_table_attach_defaults(GTK_TABLE(tbl), rctx.e_vfo, 1, 2, 2, 3);
        gtk_table_attach_defaults(GTK_TABLE(tbl), btn_v, 2, 3, 2, 3);
        gtk_table_attach_defaults(GTK_TABLE(tbl), btn_ptt, 0, 2, 3, 4);
        gtk_table_attach_defaults(GTK_TABLE(tbl), btn_ref, 2, 3, 3, 4);
        gtk_box_pack_start(GTK_BOX(vbox), tbl, FALSE, FALSE, 8);

        g_signal_connect(btn_f, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* c = static_cast<RigDlgCtx*>(d);
            std::string err;
            try {
                uint64_t hz = std::stoull(gtk_entry_get_text(GTK_ENTRY(c->e_freq)));
                if (c->app->engine->rig_controller().set_frequency(hz, err))
                    gtk_label_set_text(GTK_LABEL(c->lbl_fb), "Frequency updated.");
                else gtk_label_set_text(GTK_LABEL(c->lbl_fb), err.c_str());
            } catch (...) { gtk_label_set_text(GTK_LABEL(c->lbl_fb), "Invalid frequency."); }
        }), &rctx);
        g_signal_connect(btn_m, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* c = static_cast<RigDlgCtx*>(d);
            std::string err;
            try {
                std::string m = gtk_entry_get_text(GTK_ENTRY(c->e_mode));
                int pb = std::stoi(gtk_entry_get_text(GTK_ENTRY(c->e_pb)));
                if (c->app->engine->rig_controller().set_mode(m, pb, err))
                    gtk_label_set_text(GTK_LABEL(c->lbl_fb), "Mode updated.");
                else gtk_label_set_text(GTK_LABEL(c->lbl_fb), err.c_str());
            } catch (...) { gtk_label_set_text(GTK_LABEL(c->lbl_fb), "Invalid passband."); }
        }), &rctx);
        g_signal_connect(btn_v, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* c = static_cast<RigDlgCtx*>(d);
            std::string err;
            std::string v = gtk_entry_get_text(GTK_ENTRY(c->e_vfo));
            if (c->app->engine->rig_controller().set_vfo(v, err))
                gtk_label_set_text(GTK_LABEL(c->lbl_fb), "VFO updated.");
            else gtk_label_set_text(GTK_LABEL(c->lbl_fb), err.c_str());
        }), &rctx);
        g_signal_connect(btn_ptt, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* c = static_cast<RigDlgCtx*>(d);
            auto rs = c->app->engine->rig_controller().status();
            std::string err;
            bool next_tx = !(rs.ptt > 0);
            if (c->app->engine->rig_controller().set_ptt(next_tx, err))
                gtk_label_set_text(GTK_LABEL(c->lbl_fb), next_tx ? "PTT set to TX." : "PTT set to RX.");
            else gtk_label_set_text(GTK_LABEL(c->lbl_fb), err.c_str());
        }), &rctx);
        g_signal_connect(btn_ref, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* c = static_cast<RigDlgCtx*>(d);
            std::string err;
            if (c->app->engine->rig_controller().refresh(err))
                gtk_label_set_text(GTK_LABEL(c->lbl_fb), "Refreshed.");
            else gtk_label_set_text(GTK_LABEL(c->lbl_fb), err.c_str());
        }), &rctx);

        for (GtkWidget* b : { btn_f, btn_m, btn_v, btn_ptt, btn_ref }) {
            g_signal_connect_after(b, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
                auto* c = static_cast<RigDlgCtx*>(d);
                auto rs = c->app->engine->rig_controller().status();
                std::ostringstream oss;
                oss << "Status: " << (rs.reachable ? "ONLINE" : "NO REPLY")
                    << "\nFrequency: " << rs.freq_hz << " Hz"
                    << "\nMode: " << (rs.mode.empty() ? "(unknown)" : rs.mode)
                    << " (" << rs.passband_hz << " Hz)"
                    << "   |   PTT: " << (rs.ptt > 0 ? "TX" : (rs.ptt == 0 ? "RX" : "UNKNOWN"))
                    << "   |   VFO: " << (rs.vfo.empty() ? "(unknown)" : rs.vfo);
                gtk_label_set_text(GTK_LABEL(c->lbl_state), oss.str().c_str());
            }), &rctx);
        }
    }

    gtk_box_pack_start(GTK_BOX(vbox), lbl_fb, FALSE, FALSE, 4);

    if (rc.enabled && rc.protocol != radio::RigProtocol::None) {
        std::string err;
        if (!rig.refresh(err)) gtk_label_set_text(GTK_LABEL(lbl_fb), err.c_str());
    }
    update_view(&rctx);

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

std::string gtk_iface_summary(const radio::RadioInterfaceConfig& ic) {
    auto gp = [&](const char* k) -> std::string {
        auto it = ic.params.find(k);
        return (it != ic.params.end()) ? it->second : "";
    };
    if (ic.type == "axudp" || ic.type == "telnet") {
        std::string h = gp("bind_host");
        return (h.empty() ? "0.0.0.0" : h) + ":" + gp("bind_port");
    }
    if (ic.type == "axip") {
        std::string h = gp("bind_host");
        return (h.empty() ? "0.0.0.0" : h) + " (IP proto 93)";
    }
    if (ic.type == "kiss-serial") {
        return gp("device") + " @" + gp("baud");
    }
    if (ic.type == "kiss-tcp") {
        return gp("mode") + " " + gp("host") + ":" + gp("port");
    }
    return {};
}

struct GtkSettingsCtx {
    GtkWidget* dlg = nullptr;
    GtkListStore* if_store = nullptr;
    GtkWidget* if_tree = nullptr;
    std::vector<radio::RadioInterfaceConfig>* ifaces = nullptr;
};

void repopulate_gtk_ifaces(GtkSettingsCtx* ctx) {
    gtk_list_store_clear(ctx->if_store);
    for (std::size_t i = 0; i < ctx->ifaces->size(); ++i) {
        const auto& ic = (*ctx->ifaces)[i];
        GtkTreeIter it;
        gtk_list_store_append(ctx->if_store, &it);
        gtk_list_store_set(ctx->if_store, &it,
                           0, ic.id.c_str(),
                           1, ic.type.c_str(),
                           2, gtk_iface_summary(ic).c_str(),
                           3, ic.description.c_str(),
                           4, static_cast<gint>(i),
                           -1);
    }
}

void on_settings_add_iface_clicked(GtkWidget*, gpointer user_data) {
    auto* ctx = static_cast<GtkSettingsCtx*>(user_data);
    GtkWidget* adlg = gtk_dialog_new_with_buttons(
        "Add Interface Mapping",
        GTK_WINDOW(ctx->dlg),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_ADD, GTK_RESPONSE_ACCEPT,
        static_cast<const char*>(nullptr));
    gtk_window_set_default_size(GTK_WINDOW(adlg), 420, 280);

    GtkWidget* tbl = gtk_table_new(8, 2, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(tbl), 8);
    gtk_table_set_row_spacings(GTK_TABLE(tbl), 5);
    gtk_table_set_col_spacings(GTK_TABLE(tbl), 8);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(adlg)->vbox), tbl, TRUE, TRUE, 4);

    auto add_row = [&](int r, const char* lbl, GtkWidget* w) {
        GtkWidget* l = gtk_label_new(lbl);
        gtk_misc_set_alignment(GTK_MISC(l), 0.0f, 0.5f);
        gtk_table_attach(GTK_TABLE(tbl), l, 0, 1, r, r + 1, GTK_FILL, GTK_FILL, 0, 0);
        gtk_table_attach(GTK_TABLE(tbl), w, 1, 2, r, r + 1,
                         static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);
    };

    GtkWidget* e_id = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_id), "axudp1");
    add_row(0, "Interface ID:", e_id);

    GtkWidget* cb_type = gtk_combo_box_new_text();
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_type), "axudp");
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_type), "axip");
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_type), "kiss-serial");
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_type), "kiss-tcp");
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_type), "telnet");
    gtk_combo_box_set_active(GTK_COMBO_BOX(cb_type), 0);
    add_row(1, "Type:", cb_type);

    GtkWidget* e_desc = gtk_entry_new();
    add_row(2, "Description:", e_desc);

    GtkWidget* e_host = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_host), "0.0.0.0");
    add_row(3, "Host / Serial Device:", e_host);

    GtkWidget* e_port = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_port), "10093");
    add_row(4, "Port / Baud Rate:", e_port);

    GtkWidget* cb_mode = gtk_combo_box_new_text();
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_mode), "client");
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_mode), "server");
    gtk_combo_box_set_active(GTK_COMBO_BOX(cb_mode), 0);
    add_row(5, "KISS-TCP Mode:", cb_mode);

    GtkWidget* e_peer = gtk_entry_new();
    add_row(6, "Peer (optional):", e_peer);

    gtk_widget_show_all(adlg);
    while (gtk_dialog_run(GTK_DIALOG(adlg)) == GTK_RESPONSE_ACCEPT) {
        std::string id = gtk_entry_get_text(GTK_ENTRY(e_id));
        gchar* t_raw = gtk_combo_box_get_active_text(GTK_COMBO_BOX(cb_type));
        std::string type_s = t_raw ? t_raw : "axudp";
        if (t_raw) g_free(t_raw);
        if (id.empty()) {
            GtkWidget* err = gtk_message_dialog_new(
                GTK_WINDOW(adlg), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                "Interface ID cannot be empty.");
            gtk_dialog_run(GTK_DIALOG(err));
            gtk_widget_destroy(err);
            continue;
        }
        bool dup = false;
        for (const auto& ex : *ctx->ifaces) {
            if (ex.id == id) { dup = true; break; }
        }
        if (dup) {
            GtkWidget* err = gtk_message_dialog_new(
                GTK_WINDOW(adlg), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                "An interface with ID '%s' already exists.", id.c_str());
            gtk_dialog_run(GTK_DIALOG(err));
            gtk_widget_destroy(err);
            continue;
        }
        radio::RadioInterfaceConfig ic;
        ic.id = id;
        ic.type = type_s;
        ic.description = gtk_entry_get_text(GTK_ENTRY(e_desc));
        std::string host = gtk_entry_get_text(GTK_ENTRY(e_host));
        std::string port = gtk_entry_get_text(GTK_ENTRY(e_port));
        std::string peer = gtk_entry_get_text(GTK_ENTRY(e_peer));
        gchar* m_raw = gtk_combo_box_get_active_text(GTK_COMBO_BOX(cb_mode));
        std::string mode = m_raw ? m_raw : "client";
        if (m_raw) g_free(m_raw);

        if (type_s == "axudp") {
            ic.params["bind_host"] = host.empty() ? "0.0.0.0" : host;
            ic.params["bind_port"] = port.empty() ? "10093" : port;
            ic.params["auto_learn"] = "true";
            if (!peer.empty()) ic.peers.push_back(peer);
        } else if (type_s == "axip") {
            ic.params["bind_host"] = host.empty() ? "0.0.0.0" : host;
            ic.params["auto_learn"] = "true";
            if (!peer.empty()) ic.peers.push_back(peer);
        } else if (type_s == "kiss-serial") {
            ic.params["device"] = host.empty() ? "/dev/ttyUSB0" : host;
            ic.params["baud"] = port.empty() ? "9600" : port;
            ic.params["kiss_port"] = "0";
        } else if (type_s == "kiss-tcp") {
            ic.params["mode"] = mode;
            ic.params["host"] = host.empty() ? "127.0.0.1" : host;
            ic.params["port"] = port.empty() ? "8001" : port;
            ic.params["kiss_port"] = "0";
        } else if (type_s == "telnet") {
            ic.params["bind_host"] = host.empty() ? "0.0.0.0" : host;
            ic.params["bind_port"] = port.empty() ? "8010" : port;
        }
        ctx->ifaces->push_back(std::move(ic));
        repopulate_gtk_ifaces(ctx);
        break;
    }
    gtk_widget_destroy(adlg);
}

void on_settings_rem_iface_clicked(GtkWidget*, gpointer user_data) {
    auto* ctx = static_cast<GtkSettingsCtx*>(user_data);
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(ctx->if_tree));
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;
    gint idx = -1;
    gtk_tree_model_get(model, &iter, 4, &idx, -1);
    if (idx >= 0 && static_cast<std::size_t>(idx) < ctx->ifaces->size()) {
        ctx->ifaces->erase(ctx->ifaces->begin() + idx);
        repopulate_gtk_ifaces(ctx);
    }
}

void on_settings_clicked(GtkWidget*, gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);
    node::NodeConfig draft = st->engine->config();

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Node Settings",
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        "Save & Apply", GTK_RESPONSE_ACCEPT,
        static_cast<const char*>(nullptr));
    gtk_window_set_default_size(GTK_WINDOW(dlg), 560, 400);

    GtkWidget* nb = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), nb, TRUE, TRUE, 6);

    auto make_table = [](int rows) -> GtkWidget* {
        GtkWidget* t = gtk_table_new(rows, 2, FALSE);
        gtk_container_set_border_width(GTK_CONTAINER(t), 10);
        gtk_table_set_row_spacings(GTK_TABLE(t), 6);
        gtk_table_set_col_spacings(GTK_TABLE(t), 8);
        return t;
    };
    auto attach_row = [](GtkWidget* tbl, int r, const char* lbl, GtkWidget* w) {
        GtkWidget* l = gtk_label_new(lbl);
        gtk_misc_set_alignment(GTK_MISC(l), 0.0f, 0.5f);
        gtk_table_attach(GTK_TABLE(tbl), l, 0, 1, r, r + 1, GTK_FILL, GTK_FILL, 0, 0);
        gtk_table_attach(GTK_TABLE(tbl), w, 1, 2, r, r + 1,
                         static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);
    };

    // Tab 1: Node
    GtkWidget* t_node = make_table(5);
    GtkWidget* e_call = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_call), draft.callsign.str().c_str());
    attach_row(t_node, 0, "Node Callsign (CALL-SSID):", e_call);

    GtkWidget* e_alias = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_alias), draft.alias.valid() ? draft.alias.str().c_str() : "");
    attach_row(t_node, 1, "Node Alias:", e_alias);

    GtkWidget* e_welcome = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_welcome), draft.welcome_text.c_str());
    attach_row(t_node, 2, "Welcome Message:", e_welcome);

    GtkWidget* e_info = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_info), draft.info_text.c_str());
    attach_row(t_node, 3, "Info Message:", e_info);

    GtkWidget* chk_accept = gtk_check_button_new_with_label("Allow incoming connections by default");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_accept), draft.accept_incoming_default ? TRUE : FALSE);
    gtk_table_attach(GTK_TABLE(t_node), chk_accept, 0, 2, 4, 5, GTK_FILL, GTK_FILL, 0, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), t_node, gtk_label_new("Node"));

    // Tab 2: BBS
    GtkWidget* t_bbs = make_table(9);
    GtkWidget* chk_bbs = gtk_check_button_new_with_label("Enable Packet BBS");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_bbs), draft.bbs.enabled ? TRUE : FALSE);
    gtk_table_attach(GTK_TABLE(t_bbs), chk_bbs, 0, 2, 0, 1, GTK_FILL, GTK_FILL, 0, 0);

    GtkWidget* sp_bbs_ssid = gtk_spin_button_new_with_range(0.0, 15.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp_bbs_ssid), static_cast<gdouble>(draft.bbs.ssid));
    attach_row(t_bbs, 1, "BBS SSID (0-15):", sp_bbs_ssid);

    GtkWidget* e_bbs_call = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_bbs_call), draft.bbs.callsign.valid() ? draft.bbs.callsign.str().c_str() : "");
    attach_row(t_bbs, 2, "Callsign Override (optional):", e_bbs_call);

    GtkWidget* e_bbs_alias = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_bbs_alias), draft.bbs.alias.valid() ? draft.bbs.alias.str().c_str() : "");
    attach_row(t_bbs, 3, "BBS Alias:", e_bbs_alias);

    GtkWidget* chk_files = gtk_check_button_new_with_label("Enable BBS Files Area (FILES/GET/UPLOAD)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_files), draft.bbs.files_enabled ? TRUE : FALSE);
    gtk_table_attach(GTK_TABLE(t_bbs), chk_files, 0, 2, 4, 5, GTK_FILL, GTK_FILL, 0, 0);

    GtkWidget* chk_files_ci = gtk_check_button_new_with_label(
        "Case-insensitive file matching (exact -> UPPER -> lower -> glob)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_files_ci),
                                 draft.bbs.files_case_insensitive ? TRUE : FALSE);
    gtk_table_attach(GTK_TABLE(t_bbs), chk_files_ci, 0, 2, 5, 6, GTK_FILL, GTK_FILL, 0, 0);

    GtkWidget* e_files_path = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_files_path), draft.bbs.files_path.c_str());
    attach_row(t_bbs, 6, "Files Area Root Path:", e_files_path);

    GtkWidget* e_mail_path = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_mail_path), draft.bbs.mail_path.c_str());
    attach_row(t_bbs, 7, "Mail Storage Path:", e_mail_path);

    GtkWidget* e_bbs_welcome = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_bbs_welcome), draft.bbs.welcome.c_str());
    attach_row(t_bbs, 8, "BBS Welcome Message:", e_bbs_welcome);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), t_bbs, gtk_label_new("BBS"));

    // Tab 3: CHAT
    GtkWidget* t_chat = make_table(6);
    GtkWidget* chk_chat = gtk_check_button_new_with_label("Enable Multi-User CHAT Conference");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_chat), draft.chat.enabled ? TRUE : FALSE);
    gtk_table_attach(GTK_TABLE(t_chat), chk_chat, 0, 2, 0, 1, GTK_FILL, GTK_FILL, 0, 0);

    GtkWidget* sp_chat_ssid = gtk_spin_button_new_with_range(0.0, 15.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp_chat_ssid), static_cast<gdouble>(draft.chat.ssid));
    attach_row(t_chat, 1, "CHAT SSID (0-15):", sp_chat_ssid);

    GtkWidget* e_chat_call = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_chat_call), draft.chat.callsign.valid() ? draft.chat.callsign.str().c_str() : "");
    attach_row(t_chat, 2, "Callsign Override (optional):", e_chat_call);

    GtkWidget* e_chat_alias = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_chat_alias), draft.chat.alias.valid() ? draft.chat.alias.str().c_str() : "");
    attach_row(t_chat, 3, "CHAT Alias:", e_chat_alias);

    GtkWidget* e_chat_topic = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_chat_topic), draft.chat.default_topic.c_str());
    attach_row(t_chat, 4, "Default Topic:", e_chat_topic);

    GtkWidget* e_chat_welcome = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_chat_welcome), draft.chat.welcome.c_str());
    attach_row(t_chat, 5, "CHAT Welcome Message:", e_chat_welcome);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), t_chat, gtk_label_new("CHAT"));

    // Tab 4: Interfaces
    GtkWidget* if_vbox = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(if_vbox), 8);
    GtkListStore* if_store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING,
                                                G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    GtkWidget* if_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(if_store));
    add_text_column(GTK_TREE_VIEW(if_tree), "ID", 0, 85);
    add_text_column(GTK_TREE_VIEW(if_tree), "Type", 1, 80);
    add_text_column(GTK_TREE_VIEW(if_tree), "Endpoint / Device", 2, 160);
    add_text_column(GTK_TREE_VIEW(if_tree), "Description", 3, 140);
    GtkWidget* if_sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(if_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(if_sc), if_tree);
    gtk_box_pack_start(GTK_BOX(if_vbox), if_sc, TRUE, TRUE, 0);

    GtkWidget* if_btns = gtk_hbox_new(FALSE, 6);
    GtkWidget* btn_add_if = gtk_button_new_with_label("Add Interface...");
    GtkWidget* btn_rem_if = gtk_button_new_with_label("Remove Selected");
    gtk_box_pack_start(GTK_BOX(if_btns), btn_add_if, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(if_btns), btn_rem_if, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(if_vbox), if_btns, FALSE, FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), if_vbox, gtk_label_new("Interfaces"));

    // Tab 5: Rig Control
    GtkWidget* t_rig = make_table(6);
    GtkWidget* chk_rig = gtk_check_button_new_with_label("Enable Radio / Rig Control");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_rig), draft.rig.enabled ? TRUE : FALSE);
    gtk_table_attach(GTK_TABLE(t_rig), chk_rig, 0, 2, 0, 1, GTK_FILL, GTK_FILL, 0, 0);

    GtkWidget* cb_rig_proto = gtk_combo_box_new_text();
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_rig_proto), "rigctl");
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_rig_proto), "radiofoundry");
    gtk_combo_box_set_active(GTK_COMBO_BOX(cb_rig_proto),
                             draft.rig.protocol == radio::RigProtocol::RadioFoundry ? 1 : 0);
    attach_row(t_rig, 1, "Protocol:", cb_rig_proto);

    GtkWidget* e_rig_host = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_rig_host), draft.rig.host.c_str());
    attach_row(t_rig, 2, "Host / Radio IP:", e_rig_host);

    GtkWidget* e_rig_port = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_rig_port), std::to_string(draft.rig.port).c_str());
    attach_row(t_rig, 3, "Port (4532 / 4191):", e_rig_port);

    GtkWidget* sp_rig_poll = gtk_spin_button_new_with_range(0.0, 3600.0, 5.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp_rig_poll), static_cast<gdouble>(draft.rig.poll_interval_sec));
    attach_row(t_rig, 4, "Poll / Keepalive (sec):", sp_rig_poll);

    GtkWidget* chk_rig_notify = gtk_check_button_new_with_label("Receive RadioFoundry UDP 4191 change notifications");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_rig_notify), draft.rig.notify ? TRUE : FALSE);
    gtk_table_attach(GTK_TABLE(t_rig), chk_rig_notify, 0, 2, 5, 6, GTK_FILL, GTK_FILL, 0, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), t_rig, gtk_label_new("Rig"));

    GtkSettingsCtx sctx;
    sctx.dlg = dlg;
    sctx.if_store = if_store;
    sctx.if_tree = if_tree;
    sctx.ifaces = &draft.interfaces;
    repopulate_gtk_ifaces(&sctx);
    g_signal_connect(btn_add_if, "clicked", G_CALLBACK(on_settings_add_iface_clicked), &sctx);
    g_signal_connect(btn_rem_if, "clicked", G_CALLBACK(on_settings_rem_iface_clicked), &sctx);

    gtk_widget_show_all(dlg);

    while (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        node::NodeConfig next = draft;

        auto show_err = [&](const std::string& msg) {
            GtkWidget* ed = gtk_message_dialog_new(
                GTK_WINDOW(dlg), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                "%s", msg.c_str());
            gtk_window_set_title(GTK_WINDOW(ed), "Configuration Error");
            gtk_dialog_run(GTK_DIALOG(ed));
            gtk_widget_destroy(ed);
        };

        auto parsed_call = node::NodeConfig::parse_callsign_with_default_ssid(
            gtk_entry_get_text(GTK_ENTRY(e_call)));
        if (!parsed_call) {
            show_err("Invalid node callsign.");
            continue;
        }
        next.callsign = *parsed_call;

        std::string alias_s = gtk_entry_get_text(GTK_ENTRY(e_alias));
        if (alias_s.empty()) {
            next.alias = ax25::Callsign{};
        } else if (auto pa = ax25::Callsign::parse(alias_s)) {
            next.alias = *pa;
        } else {
            show_err("Invalid node alias.");
            continue;
        }
        next.welcome_text = gtk_entry_get_text(GTK_ENTRY(e_welcome));
        next.info_text = gtk_entry_get_text(GTK_ENTRY(e_info));
        next.accept_incoming_default = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_accept)) != FALSE;

        next.bbs.enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_bbs)) != FALSE;
        next.bbs.ssid = static_cast<uint8_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(sp_bbs_ssid)));
        std::string bc_s = gtk_entry_get_text(GTK_ENTRY(e_bbs_call));
        if (bc_s.empty()) {
            next.bbs.callsign = ax25::Callsign{};
        } else if (auto pbc = node::NodeConfig::parse_callsign_with_default_ssid(bc_s, next.bbs.ssid)) {
            next.bbs.callsign = *pbc;
        } else {
            show_err("Invalid BBS callsign override.");
            continue;
        }
        std::string ba_s = gtk_entry_get_text(GTK_ENTRY(e_bbs_alias));
        if (ba_s.empty()) {
            next.bbs.alias = ax25::Callsign{};
        } else if (auto pba = ax25::Callsign::parse(ba_s)) {
            next.bbs.alias = *pba;
        } else {
            show_err("Invalid BBS alias.");
            continue;
        }
        next.bbs.files_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_files)) != FALSE;
        next.bbs.files_case_insensitive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_files_ci)) != FALSE;
        std::string fp_s = gtk_entry_get_text(GTK_ENTRY(e_files_path));
        if (!fp_s.empty()) next.bbs.files_path = fp_s;
        std::string mp_s = gtk_entry_get_text(GTK_ENTRY(e_mail_path));
        if (!mp_s.empty()) next.bbs.mail_path = mp_s;
        next.bbs.welcome = gtk_entry_get_text(GTK_ENTRY(e_bbs_welcome));

        next.chat.enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_chat)) != FALSE;
        next.chat.ssid = static_cast<uint8_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(sp_chat_ssid)));
        std::string cc_s = gtk_entry_get_text(GTK_ENTRY(e_chat_call));
        if (cc_s.empty()) {
            next.chat.callsign = ax25::Callsign{};
        } else if (auto pcc = node::NodeConfig::parse_callsign_with_default_ssid(cc_s, next.chat.ssid)) {
            next.chat.callsign = *pcc;
        } else {
            show_err("Invalid CHAT callsign override.");
            continue;
        }
        std::string ca_s = gtk_entry_get_text(GTK_ENTRY(e_chat_alias));
        if (ca_s.empty()) {
            next.chat.alias = ax25::Callsign{};
        } else if (auto pca = ax25::Callsign::parse(ca_s)) {
            next.chat.alias = *pca;
        } else {
            show_err("Invalid CHAT alias.");
            continue;
        }
        std::string ct_s = gtk_entry_get_text(GTK_ENTRY(e_chat_topic));
        if (!ct_s.empty()) next.chat.default_topic = ct_s;
        next.chat.welcome = gtk_entry_get_text(GTK_ENTRY(e_chat_welcome));

        next.rig.enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_rig)) != FALSE;
        next.rig.protocol = (gtk_combo_box_get_active(GTK_COMBO_BOX(cb_rig_proto)) == 1)
                                ? radio::RigProtocol::RadioFoundry
                                : radio::RigProtocol::Rigctl;
        std::string rh_s = gtk_entry_get_text(GTK_ENTRY(e_rig_host));
        if (!rh_s.empty()) next.rig.host = rh_s;
        try {
            int rp = std::stoi(gtk_entry_get_text(GTK_ENTRY(e_rig_port)));
            if (rp < 1 || rp > 65535) {
                show_err("Rig port must be 1..65535.");
                continue;
            }
            next.rig.port = static_cast<uint16_t>(rp);
        } catch (...) {
            next.rig.port = radio::rig_default_port(next.rig.protocol);
        }
        next.rig.poll_interval_sec = static_cast<uint32_t>(
            gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(sp_rig_poll)));
        next.rig.notify = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_rig_notify)) != FALSE;

        auto res = st->engine->apply_config(next, true);
        if (!res.applied) {
            show_err(res.error);
            continue;
        }
        if (!res.saved_to_disk) {
            GtkWidget* wd = gtk_message_dialog_new(
                GTK_WINDOW(dlg), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                "%s",
                res.warning.empty()
                    ? "The configuration file could not be written. Changes have been applied "
                      "in memory only and will be lost when the node exits."
                    : res.warning.c_str());
            gtk_window_set_title(GTK_WINDOW(wd), "Read-Only Configuration");
            gtk_dialog_run(GTK_DIALOG(wd));
            gtk_widget_destroy(wd);
            set_status_text(st, "Settings applied IN MEMORY ONLY (config file not writable).");
        } else {
            std::string msg = "Settings saved to " + res.save_path + " and applied.";
            if (res.signed_off_sessions > 0) {
                msg += " Signed off " + std::to_string(res.signed_off_sessions) + " session(s).";
            }
            set_status_text(st, msg);
        }
        refresh_ui(st);
        break;
    }

    g_object_unref(if_store);
    gtk_widget_destroy(dlg);
}

bool confirm_and_exit(GtkAppState* st) {
    auto snap = st->engine->snapshot();
    std::string msg = (snap.total_active_connections > 0)
                          ? ("Exit node? (" + std::to_string(snap.total_active_connections) + " active)")
                          : "Exit node?";
    GtkWidget* dlg = gtk_message_dialog_new(
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "%s", msg.c_str());
    gtk_window_set_title(GTK_WINDOW(dlg), "Exit");
    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    if (resp == GTK_RESPONSE_YES) {
        gtk_main_quit();
        return true;
    }
    return false;
}

void on_exit_clicked(GtkWidget*, gpointer user_data) {
    confirm_and_exit(static_cast<GtkAppState*>(user_data));
}

gboolean on_window_delete(GtkWidget*, GdkEvent*, gpointer user_data) {
    // Return FALSE only if confirmed to allow close; TRUE cancels close
    return confirm_and_exit(static_cast<GtkAppState*>(user_data)) ? FALSE : TRUE;
}

} // namespace

Gtk2UI::Gtk2UI(node::NodeEngine& engine,
               std::atomic<bool>* external_stop_flag,
               std::string initial_geometry)
    : engine_(engine),
      external_stop_flag_(external_stop_flag),
      initial_geometry_(std::move(initial_geometry)) {}

bool Gtk2UI::run(int& argc, char**& argv) {
    if (!gtk_init_check(&argc, &argv)) {
        return false;
    }

    GtkAppState st;
    st.engine = &engine_;
    st.stop_flag = external_stop_flag_;

    st.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(st.window), "OYK Amateur Radio Packet Node");

    int init_w = 920;
    int init_h = 640;
    WindowGeometry geom;
    bool have_geom = !initial_geometry_.empty() && parse_x11_geometry(initial_geometry_, geom);
    if (have_geom && geom.has_size) {
        init_w = geom.width;
        init_h = geom.height;
    }
    gtk_window_set_default_size(GTK_WINDOW(st.window), init_w, init_h);
    g_signal_connect(st.window, "delete-event", G_CALLBACK(on_window_delete), &st);

    GtkWidget* root_vbox = gtk_vbox_new(FALSE, 4);
    gtk_container_add(GTK_CONTAINER(st.window), root_vbox);

    // Menu bar (matching Windows, ncurses, and DOS menus)
    GtkWidget* menubar = gtk_menu_bar_new();
    GtkWidget* node_menu = gtk_menu_new();
    GtkWidget* node_root = gtk_menu_item_new_with_mnemonic("_Node");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(node_root), node_menu);

    GtkWidget* mi_allow = gtk_menu_item_new_with_mnemonic("_Allow Incoming Connections");
    GtkWidget* mi_deny  = gtk_menu_item_new_with_mnemonic("_Deny Incoming Connections");
    GtkWidget* mi_sep1  = gtk_separator_menu_item_new();
    GtkWidget* mi_lterm = gtk_menu_item_new_with_mnemonic("_Local Terminal...");
    GtkWidget* mi_files = gtk_menu_item_new_with_mnemonic("Manage _Files Area...");
    GtkWidget* mi_rig   = gtk_menu_item_new_with_mnemonic("_Radio Control...");
    GtkWidget* mi_sett  = gtk_menu_item_new_with_mnemonic("_Settings...");
    GtkWidget* mi_term  = gtk_menu_item_new_with_mnemonic("_Terminate Connection...");
    GtkWidget* mi_ban   = gtk_menu_item_new_with_mnemonic("_Ban / Unban Callsign...");
    GtkWidget* mi_sep2  = gtk_separator_menu_item_new();
    GtkWidget* mi_exit  = gtk_menu_item_new_with_mnemonic("E_xit");

    g_signal_connect(mi_allow, "activate", G_CALLBACK(on_allow_incoming_clicked), &st);
    g_signal_connect(mi_deny,  "activate", G_CALLBACK(on_deny_incoming_clicked), &st);
    g_signal_connect(mi_lterm, "activate", G_CALLBACK(on_local_terminal_clicked), &st);
    g_signal_connect(mi_files, "activate", G_CALLBACK(on_approve_files_clicked), &st);
    g_signal_connect(mi_rig,   "activate", G_CALLBACK(on_rig_control_clicked), &st);
    g_signal_connect(mi_sett,  "activate", G_CALLBACK(on_settings_clicked), &st);
    g_signal_connect(mi_term,  "activate", G_CALLBACK(on_terminate_clicked), &st);
    g_signal_connect(mi_ban,   "activate", G_CALLBACK(on_ban_clicked), &st);
    g_signal_connect(mi_exit,  "activate", G_CALLBACK(on_exit_clicked), &st);

    gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), mi_allow);
    gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), mi_deny);
    gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), mi_sep1);
    gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), mi_lterm);
    gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), mi_files);
    gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), mi_rig);
    gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), mi_sett);
    gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), mi_term);
    gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), mi_ban);
    gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), mi_sep2);
    gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), mi_exit);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), node_root);
    gtk_box_pack_start(GTK_BOX(root_vbox), menubar, FALSE, FALSE, 0);

    // Top Header Status Bar
    GtkWidget* hdr_hbox = gtk_hbox_new(FALSE, 8);
    gtk_container_set_border_width(GTK_CONTAINER(hdr_hbox), 6);
    st.header_info_label = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(st.header_info_label), 0.0f, 0.5f);
    st.incoming_badge_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(hdr_hbox), st.header_info_label, TRUE, TRUE, 4);
    gtk_box_pack_end(GTK_BOX(hdr_hbox), st.incoming_badge_label, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(root_vbox), hdr_hbox, FALSE, FALSE, 0);

    // Upper split: Interfaces (left) + Last 5 New Callsigns Heard (right)
    GtkWidget* top_hpaned = gtk_hpaned_new();
    gtk_paned_set_position(GTK_PANED(top_hpaned), (init_w * 56) / 100);

    // Left: Radio Interfaces
    GtkWidget* iface_frame = gtk_frame_new("Radio Interfaces (Active Connections)");
    st.iface_store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                        G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* iface_tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(st.iface_store));
    add_text_column(GTK_TREE_VIEW(iface_tv), "Port", 0, 80);
    add_text_column(GTK_TREE_VIEW(iface_tv), "Type", 1, 75);
    add_text_column(GTK_TREE_VIEW(iface_tv), "Conn", 2, 55);
    add_text_column(GTK_TREE_VIEW(iface_tv), "Status", 3, 170);
    add_text_column(GTK_TREE_VIEW(iface_tv), "RX/TX", 4, 80);
    GtkWidget* iface_sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(iface_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(iface_sc), iface_tv);
    gtk_container_add(GTK_CONTAINER(iface_frame), iface_sc);
    gtk_paned_pack1(GTK_PANED(top_hpaned), iface_frame, TRUE, FALSE);

    // Right: Last 5 New Callsigns Heard
    GtkWidget* heard_frame = gtk_frame_new("Last 5 New Callsigns Heard");
    st.heard_store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* heard_tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(st.heard_store));
    add_text_column(GTK_TREE_VIEW(heard_tv), "#", 0, 32);
    add_text_column(GTK_TREE_VIEW(heard_tv), "Callsign", 1, 95);
    add_text_column(GTK_TREE_VIEW(heard_tv), "Port", 2, 85);
    add_text_column(GTK_TREE_VIEW(heard_tv), "Heard", 3, 80);
    GtkWidget* heard_sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(heard_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(heard_sc), heard_tv);
    gtk_container_add(GTK_CONTAINER(heard_frame), heard_sc);
    gtk_paned_pack2(GTK_PANED(top_hpaned), heard_frame, TRUE, FALSE);

    gtk_widget_set_size_request(top_hpaned, -1, 100);
    gtk_box_pack_start(GTK_BOX(root_vbox), top_hpaned, FALSE, TRUE, 2);

    // Middle: Active Connections
    st.sessions_frame = gtk_frame_new("Active Connections (0)  |  Banned Callsigns: 0");
    st.session_store = gtk_list_store_new(8, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                          G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                          G_TYPE_STRING, G_TYPE_UINT64);
    st.session_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(st.session_store));
    add_text_column(GTK_TREE_VIEW(st.session_tree), "ID", 0, 55);
    add_text_column(GTK_TREE_VIEW(st.session_tree), "Callsign", 1, 100);
    add_text_column(GTK_TREE_VIEW(st.session_tree), "Port", 2, 90);
    add_text_column(GTK_TREE_VIEW(st.session_tree), "Dir", 3, 85);
    add_text_column(GTK_TREE_VIEW(st.session_tree), "State", 4, 105);
    add_text_column(GTK_TREE_VIEW(st.session_tree), "Since", 5, 85);
    add_text_column(GTK_TREE_VIEW(st.session_tree), "Role / Bridge", 6, 130);
    GtkWidget* sess_sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sess_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sess_sc), st.session_tree);
    gtk_container_add(GTK_CONTAINER(st.sessions_frame), sess_sc);
    gtk_widget_set_size_request(st.sessions_frame, -1, 90);
    gtk_box_pack_start(GTK_BOX(root_vbox), st.sessions_frame, TRUE, TRUE, 2);

    // Lower: Node Activity Log
    GtkWidget* log_frame = gtk_frame_new("Node Activity Log");
    st.log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(st.log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(st.log_view), FALSE);
    PangoFontDescription* mono = pango_font_description_from_string("Monospace 9");
    gtk_widget_modify_font(st.log_view, mono);
    pango_font_description_free(mono);
    st.log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(st.log_view));

    GtkWidget* log_sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(log_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(log_sc), st.log_view);
    gtk_container_add(GTK_CONTAINER(log_frame), log_sc);
    gtk_widget_set_size_request(log_frame, -1, 80);
    gtk_box_pack_start(GTK_BOX(root_vbox), log_frame, TRUE, TRUE, 2);

    // Status message line (menu bar at top; no redundant bottom buttons)
    st.status_label = gtk_label_new("Ready.");
    gtk_misc_set_alignment(GTK_MISC(st.status_label), 0.0f, 0.5f);
    gtk_misc_set_padding(GTK_MISC(st.status_label), 6, 4);
    gtk_box_pack_start(GTK_BOX(root_vbox), st.status_label, FALSE, FALSE, 0);

    refresh_ui(&st);
    guint timer_id = g_timeout_add(250, on_timer_tick, &st);

    if (have_geom) {
        gtk_window_parse_geometry(GTK_WINDOW(st.window), initial_geometry_.c_str());
    }

    gtk_widget_show_all(st.window);
    gtk_main();

    g_source_remove(timer_id);
    if (st.term_window != nullptr) {
        gtk_widget_destroy(st.term_window);
        st.term_window = nullptr;
    }
    g_object_unref(st.iface_store);
    g_object_unref(st.heard_store);
    g_object_unref(st.session_store);
    return true;
}

} // namespace oyk::ui
