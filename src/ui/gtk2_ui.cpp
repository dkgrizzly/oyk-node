// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder

#include "oyk/ui/gtk2_ui.hpp"
#include "oyk/ui/theme.hpp"
#include "oyk/ax25/crc.hpp"
#include "oyk/protocol/yapp.hpp"
#include "oyk_term.hpp"
#include "oyk_gui.hpp"
#include "oyk_logo_data.hpp"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace oyk::ui {

namespace {

struct GtkUiTestCtrl {
    bool enabled = false;
    std::string cap_dir;
    int main_step = 0;
    GtkWidget* main_win = nullptr;
};
static GtkUiTestCtrl g_gtk_test;

void gtk_capture_window_png(GtkWidget* widget, const std::string& tag) {
    if (!g_gtk_test.enabled || g_gtk_test.cap_dir.empty() || !widget || !tag[0]) return;
    while (gtk_events_pending()) gtk_main_iteration();
    if (widget->window) {
        if (g_gtk_test.main_win && g_gtk_test.main_win->window && widget != g_gtk_test.main_win) {
            GdkRectangle mr{};
            GdkRectangle dr{};
            gdk_window_get_frame_extents(g_gtk_test.main_win->window, &mr);
            gdk_window_get_frame_extents(widget->window, &dr);
            const gint cx = std::max<gint>(24, mr.x + (mr.width - dr.width) / 2);
            const gint cy = std::max<gint>(24, mr.y + (mr.height - dr.height) / 2);
            gtk_window_move(GTK_WINDOW(widget), cx, cy);
            gtk_window_present(GTK_WINDOW(widget));
        }
        gdk_window_process_all_updates();
        gdk_flush();
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(120000);
        gdk_window_process_all_updates();
        gdk_flush();
        while (gtk_events_pending()) gtk_main_iteration();

        GdkWindow* root = gdk_get_default_root_window();
        gint rw = 0, rh = 0;
        if (root) gdk_drawable_get_size(GDK_DRAWABLE(root), &rw, &rh);
        GdkRectangle dlg_r{};
        gdk_window_get_frame_extents(widget->window, &dlg_r);
        GdkRectangle cap_r = dlg_r;
        if (g_gtk_test.main_win && g_gtk_test.main_win->window && widget != g_gtk_test.main_win) {
            GdkRectangle main_r{};
            gdk_window_get_frame_extents(g_gtk_test.main_win->window, &main_r);
            gdk_rectangle_union(&dlg_r, &main_r, &cap_r);
        }
        GdkPixbuf* pb = nullptr;
        if (root && rw > 0 && rh > 0 && cap_r.width > 0 && cap_r.height > 0) {
            const gint x0 = std::max<gint>(0, cap_r.x);
            const gint y0 = std::max<gint>(0, cap_r.y);
            const gint cw = std::min<gint>(rw - x0, cap_r.x + cap_r.width - x0);
            const gint ch = std::min<gint>(rh - y0, cap_r.y + cap_r.height - y0);
            if (cw > 0 && ch > 0) {
                pb = gdk_pixbuf_get_from_drawable(
                    nullptr, GDK_DRAWABLE(root),
                    gdk_drawable_get_colormap(root), x0, y0, 0, 0, cw, ch);
            }
        }
        if (!pb) {
            gint w = 0, h = 0;
            gdk_drawable_get_size(GDK_DRAWABLE(widget->window), &w, &h);
            if (w > 0 && h > 0) {
                pb = gdk_pixbuf_get_from_drawable(
                    nullptr, GDK_DRAWABLE(widget->window),
                    gtk_widget_get_colormap(widget), 0, 0, 0, 0, w, h);
            }
        }
        if (pb) {
            const std::string out = g_gtk_test.cap_dir + "/gtk_" + tag + ".png";
            gdk_pixbuf_save(pb, out.c_str(), "png", nullptr, nullptr);
            g_object_unref(pb);
        }
    }
}

struct GtkSimpleCapCtx {
    GtkWidget* dlg;
    std::string tag;
    gint resp;
    int ticks;
};

gboolean on_gtk_simple_dlg_cap(gpointer data) {
    auto* c = static_cast<GtkSimpleCapCtx*>(data);
    if (++c->ticks == 1) {
        gtk_capture_window_png(c->dlg, c->tag);
        gtk_dialog_response(GTK_DIALOG(c->dlg), c->resp);
        return FALSE;
    }
    return TRUE;
}

void gtk_inject_demo_sabm(node::NodeEngine* engine) {
    const auto cfg = engine->config();
    uint16_t udp_port = 0;
    for (const auto& ic : cfg.interfaces) {
        if (ic.type == "axudp") {
            auto it = ic.params.find("bind_port");
            if (it != ic.params.end()) udp_port = static_cast<uint16_t>(std::atoi(it->second.c_str()));
            break;
        }
    }
    if (udp_port == 0) return;
    auto sabm = ax25::Frame::make_sabm(cfg.callsign, ax25::Callsign("W3TRM", 1));
    auto raw = sabm.encode();
    ax25::append_fcs(raw);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        dst.sin_port = htons(udp_port);
        (void)sendto(fd, raw.data(), raw.size(), 0,
                     reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
        close(fd);
    }
}

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

struct GtkAppState;
void gtk_attach_esc_close(GtkWidget* dlg, gint response_id);

struct GtkAppState {
    node::NodeEngine* engine = nullptr;
    std::atomic<bool>* stop_flag = nullptr;

    GtkWidget* window = nullptr;
    GtkWidget* menubar = nullptr;
    GtkWidget* node_root = nullptr;
    GtkWidget* node_menu = nullptr;
    int modal_open = 0;
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

    std::string pill = "  ";
    pill += oyk_gui_format_incoming_pill(snap.accept_incoming ? 1 : 0, 0) + 1;
    if (!pill.empty() && pill.back() == ']') pill.pop_back();
    if (snap.accept_incoming) {
        std::string mk = "<span background=\"#1e7e34\" foreground=\"#ffffff\" weight=\"bold\">" + pill + "  </span>";
        gtk_label_set_markup(GTK_LABEL(st->incoming_badge_label), mk.c_str());
    } else {
        std::string mk = "<span background=\"#b71c1c\" foreground=\"#ffffff\" weight=\"bold\">" + pill + "  </span>";
        gtk_label_set_markup(GTK_LABEL(st->incoming_badge_label), mk.c_str());
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
    GtkWidget* btn_help = nullptr;
    GtkWidget* text_view = nullptr;
    GtkTextBuffer* text_buf = nullptr;
    GtkWidget* input_entry = nullptr;
    GtkWidget* hotkey_bar = nullptr;
    GtkWidget* progress_bar = nullptr;
    guint timer_id = 0;
    bool ctrl_a_pending = false;

    std::mutex mtx;
    uint64_t sid = 0;
    bool connected = false;
    bool connecting = false;
    bool is_local = false;
    bool opening_local = false;
    OykTermEolParser eol{};
    std::string port_id;
    ax25::Callsign remote_call{"UNKNOWN", 0};
    std::string pending_text;
    protocol::YappSession yapp{"incoming"};
    std::string last_yapp_status;
    bool overwrite_prompt_open = false;

    GtkTermWindowState() {
        oyk_term_eol_init(&eol, OYK_TERM_EOL_CR, 1);
    }
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

static bool gtk_term_buf_at_bol(GtkTextBuffer* buf) {
    if (!buf) return true;
    GtkTextIter end_iter;
    gtk_text_buffer_get_end_iter(buf, &end_iter);
    if (gtk_text_iter_get_offset(&end_iter) == 0) return true;
    GtkTextIter prev = end_iter;
    gtk_text_iter_backward_char(&prev);
    return gtk_text_iter_get_char(&prev) == '\n';
}

gboolean on_gtk_term_tick(gpointer user_data) {
    auto* ts = static_cast<GtkTermWindowState*>(user_data);
    std::string chunk;
    protocol::YappProgress yp;
    bool is_conn = false;
    bool is_connecting = false;
    bool is_loc = false;
    std::string overwrite_path;
    std::string eol_tag = "CR";

    {
        std::lock_guard<std::mutex> lk(ts->mtx);
        if (!ts->pending_text.empty()) {
            chunk.swap(ts->pending_text);
        }
        yp = ts->yapp.progress();
        is_conn = ts->connected;
        is_connecting = ts->connecting;
        is_loc = ts->is_local;
        eol_tag = oyk_term_eol_label(&ts->eol);
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
        std::string status_txt;
        if (ts->ctrl_a_pending) {
            status_txt = "CTRL+A: Z:Help C:Conn L:Loc B:BBS T:Chat D:Disc S:YAPP K:Abrt E:EOL P:Prt W:Clr X:Close";
        } else {
            status_txt = is_conn
                ? ("Connected to " + ts->remote_call.str() + (is_loc ? " (LOCAL)" : " (YAPP ready)") + " [EOL:" + eol_tag + "]")
                : (is_connecting ? ("Connecting... [EOL:" + eol_tag + "]") : ("Disconnected [EOL:" + eol_tag + "] | Ctrl+A Z:Help"));
        }
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

    {
        std::lock_guard<std::mutex> lk(ts->mtx);
        ts->sid = 0;
        ts->connecting = false;
        ts->connected = false;
        ts->is_local = false;
        ts->opening_local = true;
        oyk_term_eol_init(&ts->eol, OYK_TERM_EOL_CR, 1);
    }

    auto feed_rx_normalized = [](GtkTermWindowState* st, const uint8_t* ptr, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            uint8_t b = ptr[i];
            int act = oyk_term_eol_feed_byte(&st->eol, b);
            if (act == OYK_TERM_ACT_NEWLINE) {
                st->pending_text.push_back('\n');
            } else if (act == OYK_TERM_ACT_BS) {
                if (!st->pending_text.empty() && st->pending_text.back() != '\n') {
                    st->pending_text.pop_back();
                }
            } else if (act == OYK_TERM_ACT_CHAR) {
                st->pending_text.push_back((b == '\t') ? ' ' : static_cast<char>(b));
            }
        }
    };

    node::NodeEngine::LocalTerminalCallbacks cbs;
    cbs.on_connected = [ts](uint64_t sid) {
        std::lock_guard<std::mutex> lk(ts->mtx);
        if (ts->sid != sid) {
            if (ts->opening_local && ts->sid == 0 && sid != 0) ts->sid = sid;
            else return;
        }
        ts->connecting = false;
        ts->connected = true;
        if (!ts->opening_local) {
            ts->pending_text += "*** Connected to " + ts->remote_call.str() + " via " + ts->port_id + "\n";
        }
    };
    cbs.on_rx_bytes = [ts, feed_rx_normalized](uint64_t sid, const std::vector<uint8_t>& bytes) {
        std::lock_guard<std::mutex> lk(ts->mtx);
        if (ts->sid != sid) {
            if (ts->opening_local && ts->sid == 0 && sid != 0) ts->sid = sid;
            else return;
        }
        if (ts->is_local || ts->opening_local) {
            feed_rx_normalized(ts, bytes.data(), bytes.size());
            return;
        }
        std::vector<uint8_t> plain;
        ts->yapp.feed_bytes(bytes.data(), bytes.size(), plain);
        if (!plain.empty()) {
            feed_rx_normalized(ts, plain.data(), plain.size());
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
        ts->opening_local = false;
        ts->yapp.reset();
        ts->pending_text += "\n*** Disconnected (" + reason + ")\n";
    };

    uint64_t sid = 0;
    ax25::Callsign resolved;
    bool is_local = false;
    std::string err;
    if (ts->engine->open_local_terminal(port, target, std::move(cbs), sid, &resolved, err, &is_local)) {
        std::lock_guard<std::mutex> lk(ts->mtx);
        ts->opening_local = false;
        ts->sid = sid;
        ts->is_local = is_local;
        ts->connecting = !is_local;
        ts->connected = is_local;
        ts->port_id = is_local ? "local" : port;
        ts->remote_call = resolved;
        ts->yapp.set_remote_callsign(resolved);
        if (!is_local) {
            ts->pending_text += "*** Connecting to " + resolved.str() + " on " + port + "...\n";
        }
    } else {
        {
            std::lock_guard<std::mutex> lk(ts->mtx);
            ts->opening_local = false;
        }
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

void exec_gtk_term_cmd(GtkTermWindowState* ts, int cmd) {
    if (!ts) return;
    switch (cmd) {
        case OYK_TCMD_CONNECT: {
            const char* tgt = gtk_entry_get_text(GTK_ENTRY(ts->call_entry));
            if (tgt && tgt[0]) on_gtk_term_connect(nullptr, ts);
            else gtk_widget_grab_focus(ts->call_entry);
            break;
        }
        case OYK_TCMD_LOCAL:
            gtk_combo_box_set_active(GTK_COMBO_BOX(ts->port_combo), 0);
            gtk_entry_set_text(GTK_ENTRY(ts->call_entry), "LOCAL");
            on_gtk_term_connect(nullptr, ts);
            break;
        case OYK_TCMD_BBS:
            gtk_combo_box_set_active(GTK_COMBO_BOX(ts->port_combo), 0);
            gtk_entry_set_text(GTK_ENTRY(ts->call_entry), "BBS");
            on_gtk_term_connect(nullptr, ts);
            break;
        case OYK_TCMD_CHAT:
            gtk_combo_box_set_active(GTK_COMBO_BOX(ts->port_combo), 0);
            gtk_entry_set_text(GTK_ENTRY(ts->call_entry), "CHAT");
            on_gtk_term_connect(nullptr, ts);
            break;
        case OYK_TCMD_DISCONNECT:
            on_gtk_term_disconnect(nullptr, ts);
            break;
        case OYK_TCMD_YAPP_SEND:
            on_gtk_term_upload(nullptr, ts);
            break;
        case OYK_TCMD_YAPP_ABORT:
            on_gtk_term_abort_yapp(nullptr, ts);
            break;
        case OYK_TCMD_CYCLE_EOL: {
            std::string msg;
            {
                std::lock_guard<std::mutex> lk(ts->mtx);
                oyk_term_eol_cycle_mode(&ts->eol);
                msg = std::string("*** Terminal EOL set to ") + oyk_term_eol_label(&ts->eol) + ".\n";
            }
            gtk_term_append_text_ui(ts, msg);
            break;
        }
        case OYK_TCMD_PORTS: {
            auto snap = ts->engine->snapshot();
            std::string out = "*** Configured Interfaces:\n";
            for (const auto& iface : snap.interfaces) {
                out += "  " + iface.id + " [" + iface.type + "] " + iface.status + "\n";
            }
            gtk_term_append_text_ui(ts, out);
            break;
        }
        case OYK_TCMD_CLEAR:
            gtk_text_buffer_set_text(ts->text_buf, "", 0);
            break;
        case OYK_TCMD_REDRAW:
            gtk_widget_queue_draw(ts->win);
            break;
        case OYK_TCMD_CONSOLE:
            gtk_widget_destroy(ts->win);
            break;
        case OYK_TCMD_SEND_CTRL_A: {
            uint64_t sid = 0;
            bool is_conn = false;
            {
                std::lock_guard<std::mutex> lk(ts->mtx);
                sid = ts->sid;
                is_conn = ts->connected;
            }
            if (is_conn && sid != 0) {
                ts->engine->send_local_terminal_bytes(sid, std::vector<uint8_t>{0x01});
            } else {
                gtk_term_append_text_ui(ts, "*** Not connected. Press Ctrl+A Z for help.\n");
            }
            break;
        }
        default:
            break;
    }
}

int show_gtk_term_help_dialog(GtkTermWindowState* ts) {
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Terminal Hotkey Help",
        GTK_WINDOW(ts->win),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
        static_cast<const char*>(nullptr));

    std::string body = "Command prefix: Ctrl+A (press Ctrl+A, then hotkey)\n";
    body += "----------------------------------------------------------------\n";
    const int cnt = oyk_term_help_count();
    for (int i = 0; i < cnt; ++i) {
        const OykTermHelpEntry* e = oyk_term_help_entry(i);
        if (!e) continue;
        char line[96];
        std::snprintf(line, sizeof(line), "%-9s  %-36s  %s\n",
                      e->key_label, e->action, e->alt_key);
        body += line;
    }
    body += "----------------------------------------------------------------\n";
    body += "Press a hotkey to run it, or Esc / Enter / Close";

    GtkWidget* lbl = gtk_label_new(body.c_str());
    gtk_misc_set_alignment(GTK_MISC(lbl), 0.0f, 0.0f);
    gtk_misc_set_padding(GTK_MISC(lbl), 10, 8);
    PangoFontDescription* mono = pango_font_description_from_string("Monospace 9");
    gtk_widget_modify_font(lbl, mono);
    pango_font_description_free(mono);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), lbl, TRUE, TRUE, 4);

    g_signal_connect(dlg, "key-press-event",
                     G_CALLBACK(+[](GtkWidget* d, GdkEventKey* kev, gpointer) -> gboolean {
                         if (!kev || kev->is_modifier) return FALSE;
                         if (kev->keyval == GDK_Escape || kev->keyval == GDK_Return ||
                             kev->keyval == GDK_KP_Enter || kev->keyval == GDK_space) {
                             gtk_dialog_response(GTK_DIALOG(d), GTK_RESPONSE_CLOSE);
                             return TRUE;
                         }
                         guint32 uc = gdk_keyval_to_unicode(kev->keyval);
                         int cmd = oyk_term_decode_ctrl_a_cmd(static_cast<int>(uc));
                         if (cmd != OYK_TCMD_NONE && cmd != OYK_TCMD_HELP && cmd != OYK_TCMD_CANCEL) {
                             gtk_dialog_response(GTK_DIALOG(d), 1000 + cmd);
                             return TRUE;
                         }
                         return FALSE;
                     }), nullptr);

    gtk_widget_show_all(dlg);
    GtkSimpleCapCtx cap_ctx{dlg, "terminal_help", GTK_RESPONSE_CLOSE, 0};
    if (g_gtk_test.enabled) g_timeout_add(160, on_gtk_simple_dlg_cap, &cap_ctx);
    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    return (resp >= 1000) ? static_cast<int>(resp - 1000) : OYK_TCMD_NONE;
}

void on_gtk_term_help_clicked(GtkWidget*, gpointer user_data) {
    auto* ts = static_cast<GtkTermWindowState*>(user_data);
    int cmd = show_gtk_term_help_dialog(ts);
    if (cmd != OYK_TCMD_NONE) exec_gtk_term_cmd(ts, cmd);
}

gboolean on_gtk_term_key_press(GtkWidget*, GdkEventKey* kev, gpointer user_data) {
    auto* ts = static_cast<GtkTermWindowState*>(user_data);
    if (!kev || kev->is_modifier) return FALSE;
    guint32 uc = gdk_keyval_to_unicode(kev->keyval);
    if (ts->ctrl_a_pending) {
        ts->ctrl_a_pending = false;
        if (ts->hotkey_bar) {
            gtk_label_set_text(GTK_LABEL(ts->hotkey_bar), oyk_term_hotkey_bar_text(0, 0));
        }
        int key = (kev->keyval == GDK_Escape) ? 27
                : (((kev->state & GDK_CONTROL_MASK) && (uc == 'a' || uc == 'A')) ? 0x01
                : static_cast<int>(uc));
        int cmd = oyk_term_decode_ctrl_a_cmd(key);
        if (cmd == OYK_TCMD_HELP) cmd = show_gtk_term_help_dialog(ts);
        if (cmd != OYK_TCMD_NONE && cmd != OYK_TCMD_CANCEL) {
            exec_gtk_term_cmd(ts, cmd);
        }
        return TRUE;
    }
    if ((kev->state & GDK_CONTROL_MASK) && (uc == 'a' || uc == 'A')) {
        ts->ctrl_a_pending = true;
        if (ts->hotkey_bar) {
            gtk_label_set_text(GTK_LABEL(ts->hotkey_bar), oyk_term_hotkey_bar_text(0, 1));
        }
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ts->progress_bar), "CTRL-A...");
        return TRUE;
    }
    if ((kev->state & GDK_CONTROL_MASK) && (uc == 'l' || uc == 'L')) {
        exec_gtk_term_cmd(ts, OYK_TCMD_REDRAW);
        return TRUE;
    }
    return FALSE;
}

void on_gtk_term_send_line(GtkWidget*, gpointer user_data) {
    auto* ts = static_cast<GtkTermWindowState*>(user_data);
    std::string line = gtk_entry_get_text(GTK_ENTRY(ts->input_entry));
    gtk_entry_set_text(GTK_ENTRY(ts->input_entry), "");

    uint64_t sid = 0;
    bool is_conn = false;
    std::string wire = line;
    {
        std::lock_guard<std::mutex> lk(ts->mtx);
        sid = ts->sid;
        is_conn = ts->connected;
        std::size_t eol_len = 0;
        const char* eol_s = oyk_term_eol_tx_bytes(&ts->eol, &eol_len);
        wire.append(eol_s, eol_len);
    }
    gtk_term_append_text_ui(ts, (gtk_term_buf_at_bol(ts->text_buf) ? "> " : "\n> ") + line + "\n");
    if (is_conn && sid != 0) {
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
    if (st && st->modal_open > 0) return;
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
    g_object_set_data(G_OBJECT(ts->win), "oyk_ts", ts);
    gtk_window_set_title(GTK_WINDOW(ts->win), oyk_gui_screen_title(OYK_SCR_TERMINAL));
    gtk_window_set_transient_for(GTK_WINDOW(ts->win), GTK_WINDOW(st->window));
    gtk_window_set_position(GTK_WINDOW(ts->win), GTK_WIN_POS_CENTER_ON_PARENT);
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
    ts->btn_help = gtk_button_new_with_label("Help");
    g_signal_connect(ts->btn_connect, "clicked", G_CALLBACK(on_gtk_term_connect), ts);
    g_signal_connect(ts->btn_disc, "clicked", G_CALLBACK(on_gtk_term_disconnect), ts);
    g_signal_connect(ts->btn_upload, "clicked", G_CALLBACK(on_gtk_term_upload), ts);
    g_signal_connect(ts->btn_abort, "clicked", G_CALLBACK(on_gtk_term_abort_yapp), ts);
    g_signal_connect(ts->btn_help, "clicked", G_CALLBACK(on_gtk_term_help_clicked), ts);
    g_signal_connect(ts->call_entry, "activate", G_CALLBACK(on_gtk_term_connect), ts);

    gtk_box_pack_start(GTK_BOX(top_bar), ts->btn_connect, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(top_bar), ts->btn_disc, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(top_bar), ts->btn_upload, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(top_bar), ts->btn_abort, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(top_bar), ts->btn_help, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), top_bar, FALSE, FALSE, 2);

    ts->text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(ts->text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ts->text_view), GTK_WRAP_WORD_CHAR);
    PangoFontDescription* mono = pango_font_description_from_string("Monospace 9");
    gtk_widget_modify_font(ts->text_view, mono);
    pango_font_description_free(mono);
    {
        GdkColor tbg{}, tfg{};
        if (gdk_color_parse("#101824", &tbg)) {
            gtk_widget_modify_base(ts->text_view, GTK_STATE_NORMAL, &tbg);
        }
        if (gdk_color_parse("#E2ECF8", &tfg)) {
            gtk_widget_modify_text(ts->text_view, GTK_STATE_NORMAL, &tfg);
        }
    }
    ts->text_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ts->text_view));

    GtkWidget* sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sc), ts->text_view);
    gtk_box_pack_start(GTK_BOX(vbox), sc, TRUE, TRUE, 2);

    GtkWidget* hk_row = gtk_hbox_new(FALSE, 6);
    GtkWidget* hk_ev = gtk_event_box_new();
    ts->hotkey_bar = gtk_label_new(oyk_term_hotkey_bar_text(0, 0));
    gtk_misc_set_alignment(GTK_MISC(ts->hotkey_bar), 0.0f, 0.5f);
    gtk_misc_set_padding(GTK_MISC(ts->hotkey_bar), 6, 3);
    {
        GdkColor hbg{}, hfg{};
        if (gdk_color_parse("#1D2C42", &hbg)) {
            gtk_widget_modify_bg(hk_ev, GTK_STATE_NORMAL, &hbg);
        }
        if (gdk_color_parse("#D9E8FF", &hfg)) {
            gtk_widget_modify_fg(ts->hotkey_bar, GTK_STATE_NORMAL, &hfg);
        }
    }
    gtk_container_add(GTK_CONTAINER(hk_ev), ts->hotkey_bar);
    gtk_box_pack_start(GTK_BOX(hk_row), hk_ev, TRUE, TRUE, 0);
    ts->progress_bar = gtk_progress_bar_new();
    gtk_widget_set_size_request(ts->progress_bar, 150, -1);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ts->progress_bar), "YAPP: Idle");
    gtk_box_pack_start(GTK_BOX(hk_row), ts->progress_bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hk_row, FALSE, FALSE, 2);

    GtkWidget* in_bar = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(in_bar), gtk_label_new(">"), FALSE, FALSE, 2);
    ts->input_entry = gtk_entry_new();
    GtkWidget* btn_send = gtk_button_new_with_label("Send");
    g_signal_connect(ts->input_entry, "activate", G_CALLBACK(on_gtk_term_send_line), ts);
    g_signal_connect(btn_send, "clicked", G_CALLBACK(on_gtk_term_send_line), ts);
    g_signal_connect(ts->win, "key-press-event", G_CALLBACK(on_gtk_term_key_press), ts);
    g_signal_connect(ts->input_entry, "key-press-event", G_CALLBACK(on_gtk_term_key_press), ts);
    g_signal_connect(ts->call_entry, "key-press-event", G_CALLBACK(on_gtk_term_key_press), ts);
    g_signal_connect(ts->text_view, "key-press-event", G_CALLBACK(on_gtk_term_key_press), ts);
    gtk_box_pack_start(GTK_BOX(in_bar), ts->input_entry, TRUE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(in_bar), btn_send, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), in_bar, FALSE, FALSE, 2);

    gtk_term_append_text_ui(
        ts,
        "*** Local Packet Terminal (YAPP / YAPP-C enabled)\n"
        "*** Incoming files save to incoming/<CALLSIGN-SSID>/<filename>\n"
        "*** Press Ctrl-A Z (or click Help) for hotkeys; typed / commands go to remote BBS/CHAT.\n");

    ts->timer_id = g_timeout_add(50, on_gtk_term_tick, ts);
    if (g_gtk_test.enabled) {
        gtk_term_append_text_ui(
            ts,
            "*** Connected to KR4OYK-1 BBS\n"
            "Welcome to KR4OYK-1 Packet BBS (OYKNOD)\n"
            "Type L to list messages, R # to read, B to bye.\n"
            "KR4OYK-1> L\n"
            "Msg#  Size  From     To       Subject\n"
            "1     248   W3TRM-1  ALL      Welcome to OYK Packet Node\n"
            "KR4OYK-1> ");
    }
    gtk_widget_show_all(ts->win);
}

void on_terminate_clicked(GtkWidget*, gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);
    if (st && st->modal_open > 0) return;
    auto snap = st->engine->snapshot();
    if (snap.active_sessions.empty()) {
        set_status_text(st, "No active connections to terminate.");
        return;
    }
    struct ModalGuard { GtkAppState* a; ModalGuard(GtkAppState* x) : a(x) { if (a) ++a->modal_open; } ~ModalGuard() { if (a && a->modal_open > 0) --a->modal_open; } } mg(st);

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        oyk_gui_screen_title(OYK_SCR_TERMINATE),
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "Terminate", GTK_RESPONSE_ACCEPT,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        static_cast<const char*>(nullptr));
    gtk_window_set_default_size(GTK_WINDOW(dlg), 460, 260);
    gtk_attach_esc_close(dlg, GTK_RESPONSE_CANCEL);

    GtkWidget* vbox = GTK_DIALOG(dlg)->vbox;
    GtkWidget* lbl = gtk_label_new("Select an active connection to terminate:");
    gtk_misc_set_alignment(GTK_MISC(lbl), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 6);

    GtkListStore* store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING,
                                             G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64);
    for (const auto& s : snap.active_sessions) {
        GtkTreeIter iter;
        std::string id_s = "#" + std::to_string(s.id);
        std::string st_s = node::Session::state_name(s.state);
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           0, id_s.c_str(),
                           1, s.remote_call.c_str(),
                           2, s.interface_id.c_str(),
                           3, st_s.c_str(),
                           4, static_cast<guint64>(s.id),
                           -1);
    }

    GtkWidget* tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    const char* tcols[4];
    const int tc = oyk_gui_terminate_columns(tcols, 4);
    const int twidths[4] = { 55, 110, 100, 120 };
    for (int ci = 0; ci < tc; ++ci) {
        add_text_column(GTK_TREE_VIEW(tv), tcols[ci], ci, twidths[ci]);
    }
    g_signal_connect(tv, "row-activated",
                     G_CALLBACK(+[](GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer d) {
                         gtk_dialog_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
                     }),
                     dlg);

    GtkTreePath* first_path = gtk_tree_path_new_first();
    gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(tv)), first_path);
    gtk_tree_path_free(first_path);

    GtkWidget* sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sc), tv);
    gtk_box_pack_start(GTK_BOX(vbox), sc, TRUE, TRUE, 4);
    gtk_widget_show_all(dlg);

    GtkSimpleCapCtx cap_ctx{dlg, "terminate", GTK_RESPONSE_CANCEL, 0};
    guint cap_tid = g_gtk_test.enabled ? g_timeout_add(160, on_gtk_simple_dlg_cap, &cap_ctx) : 0;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tv));
        GtkTreeModel* model = nullptr;
        GtkTreeIter iter;
        if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
            guint64 sid = 0;
            gtk_tree_model_get(model, &iter, 4, &sid, -1);
            std::string msg;
            st->engine->terminate_session(static_cast<uint64_t>(sid), &msg);
            set_status_text(st, msg);
        }
    }
    if (cap_tid && cap_ctx.ticks == 0) g_source_remove(cap_tid);
    g_object_unref(store);
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
    if (st && st->modal_open > 0) return;
    struct ModalGuard { GtkAppState* a; ModalGuard(GtkAppState* x) : a(x) { if (a) ++a->modal_open; } ~ModalGuard() { if (a && a->modal_open > 0) --a->modal_open; } } mg(st);

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Ban / Unban Callsign (banlist.cfg)",
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
        static_cast<const char*>(nullptr));
    gtk_window_set_default_size(GTK_WINDOW(dlg), 560, 400);
    gtk_attach_esc_close(dlg, GTK_RESPONSE_CLOSE);

    GtkWidget* vbox = GTK_DIALOG(dlg)->vbox;

    GtkWidget* table = gtk_table_new(4, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 5);
    gtk_table_set_col_spacings(GTK_TABLE(table), 8);
    gtk_container_set_border_width(GTK_CONTAINER(table), 6);

    GtkWidget* call_lbl = gtk_label_new("Callsign:");
    gtk_misc_set_alignment(GTK_MISC(call_lbl), 0.0f, 0.5f);
    GtkWidget* call_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(call_entry), 9);

    GtkWidget* flags_hbox = gtk_hbox_new(FALSE, 10);
    GtkWidget* chk_hard = gtk_check_button_new_with_label("Hard Ban");
    GtkWidget* chk_appeal = gtk_check_button_new_with_label("Appeal Allowed");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_appeal), TRUE);
    gtk_box_pack_start(GTK_BOX(flags_hbox), chk_hard, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(flags_hbox), chk_appeal, FALSE, FALSE, 0);

    GtkWidget* lift_lbl = gtk_label_new("Lift At:");
    gtk_misc_set_alignment(GTK_MISC(lift_lbl), 0.0f, 0.5f);
    GtkWidget* lift_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(lift_entry), "permanent");
    GtkWidget* lift_hint = gtk_label_new("(permanent, 30m, 12h, 7d, YYYY-MM-DD)");
    gtk_misc_set_alignment(GTK_MISC(lift_hint), 0.0f, 0.5f);

    GtkWidget* reason_lbl = gtk_label_new("Reason:");
    gtk_misc_set_alignment(GTK_MISC(reason_lbl), 0.0f, 0.5f);
    GtkWidget* reason_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(reason_entry), "Banned by sysop.");

    GtkWidget* btn_hbox = gtk_hbox_new(FALSE, 8);
    GtkWidget* btn_ban = gtk_button_new_with_mnemonic("_Ban Callsign");
    GtkWidget* btn_unban = gtk_button_new_with_mnemonic("_Unban Selected");
    gtk_box_pack_start(GTK_BOX(btn_hbox), btn_ban, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_hbox), btn_unban, FALSE, FALSE, 0);

    gtk_table_attach(GTK_TABLE(table), call_lbl, 0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), call_entry, 1, 2, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), flags_hbox, 2, 3, 0, 1,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);

    gtk_table_attach(GTK_TABLE(table), lift_lbl, 0, 1, 1, 2, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), lift_entry, 1, 2, 1, 2, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), lift_hint, 2, 3, 1, 2,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);

    gtk_table_attach(GTK_TABLE(table), reason_lbl, 0, 1, 2, 3, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), reason_entry, 1, 3, 2, 3,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);

    gtk_table_attach(GTK_TABLE(table), btn_hbox, 1, 3, 3, 4, GTK_FILL, GTK_FILL, 0, 0);

    gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 4);

    GtkWidget* list_hdr = gtk_label_new("Active Bans:");
    gtk_misc_set_alignment(GTK_MISC(list_hdr), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), list_hdr, FALSE, FALSE, 2);

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

    struct GtkBanCtx {
        GtkAppState* st;
        GtkWidget* dlg;
        GtkWidget* call_entry;
        GtkWidget* lift_entry;
        GtkWidget* reason_entry;
        GtkWidget* chk_hard;
        GtkWidget* chk_appeal;
        GtkWidget* ban_tv;
        GtkListStore* ban_store;
        GtkWidget* fb_lbl;
    } bctx{st, dlg, call_entry, lift_entry, reason_entry, chk_hard, chk_appeal, ban_tv, ban_store, fb_lbl};

    g_signal_connect(btn_ban, "clicked",
                     G_CALLBACK(+[](GtkWidget*, gpointer p) {
                         auto* c = static_cast<GtkBanCtx*>(p);
                         std::string call = gtk_entry_get_text(GTK_ENTRY(c->call_entry));
                         std::string lift = gtk_entry_get_text(GTK_ENTRY(c->lift_entry));
                         std::string reason = gtk_entry_get_text(GTK_ENTRY(c->reason_entry));
                         uint32_t flags = 0;
                         if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(c->chk_hard))) {
                             flags |= node::BAN_FLAG_HARD_BAN;
                         }
                         if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(c->chk_appeal))) {
                             flags |= node::BAN_FLAG_APPEAL_ALLOWED;
                         }
                         std::string msg;
                         if (c->st->engine->ban_callsign_with_lift_spec(call, lift, &msg, true, flags, reason)) {
                             gtk_entry_set_text(GTK_ENTRY(c->call_entry), "");
                             gtk_entry_set_text(GTK_ENTRY(c->lift_entry), "permanent");
                             gtk_label_set_text(GTK_LABEL(c->fb_lbl), msg.c_str());
                             set_status_text(c->st, msg);
                             populate_ban_dialog_store(c->st->engine, c->ban_store);
                             refresh_ui(c->st);
                         } else {
                             gtk_label_set_text(GTK_LABEL(c->fb_lbl), ("Error: " + msg).c_str());
                         }
                     }),
                     &bctx);

    g_signal_connect(btn_unban, "clicked",
                     G_CALLBACK(+[](GtkWidget*, gpointer p) {
                         auto* c = static_cast<GtkBanCtx*>(p);
                         GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(c->ban_tv));
                         GtkTreeModel* model = nullptr;
                         GtkTreeIter iter;
                         if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
                             gchar* key = nullptr;
                             gtk_tree_model_get(model, &iter, 0, &key, -1);
                             if (key) {
                                 GtkWidget* confirm = gtk_message_dialog_new(
                                     GTK_WINDOW(c->dlg),
                                     static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                                     GTK_MESSAGE_QUESTION,
                                     GTK_BUTTONS_YES_NO,
                                     "Unban callsign %s?", key);
                                 gtk_window_set_title(GTK_WINDOW(confirm), "Confirm Unban");
                                 gint cresp = gtk_dialog_run(GTK_DIALOG(confirm));
                                 gtk_widget_destroy(confirm);
                                 if (cresp == GTK_RESPONSE_YES) {
                                     std::string msg;
                                     c->st->engine->unban_callsign(key, &msg);
                                     gtk_label_set_text(GTK_LABEL(c->fb_lbl), msg.c_str());
                                     set_status_text(c->st, msg);
                                     populate_ban_dialog_store(c->st->engine, c->ban_store);
                                     refresh_ui(c->st);
                                 }
                                 g_free(key);
                             }
                         }
                     }),
                     &bctx);

    populate_ban_dialog_store(st->engine, ban_store);
    gtk_widget_show_all(dlg);

    GtkSimpleCapCtx cap_ctx{dlg, "ban", GTK_RESPONSE_CLOSE, 0};
    if (g_gtk_test.enabled) g_timeout_add(160, on_gtk_simple_dlg_cap, &cap_ctx);
    (void)gtk_dialog_run(GTK_DIALOG(dlg));

    g_object_unref(ban_store);
    gtk_widget_destroy(dlg);
}

static std::string gtk_prompt_input_text(GtkWidget* parent, const char* title,
                                         const char* label, const char* defval) {
    GtkWidget* md = gtk_dialog_new_with_buttons(
        title, GTK_WINDOW(parent),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_OK, GTK_RESPONSE_OK,
        static_cast<const char*>(nullptr));
    GtkWidget* vb = GTK_DIALOG(md)->vbox;
    GtkWidget* lbl = gtk_label_new(label);
    gtk_misc_set_alignment(GTK_MISC(lbl), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vb), lbl, FALSE, FALSE, 4);
    GtkWidget* ent = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(ent), defval ? defval : "");
    gtk_entry_set_activates_default(GTK_ENTRY(ent), TRUE);
    gtk_box_pack_start(GTK_BOX(vb), ent, FALSE, FALSE, 4);
    gtk_dialog_set_default_response(GTK_DIALOG(md), GTK_RESPONSE_OK);
    gtk_widget_show_all(md);
    std::string out;
    if (gtk_dialog_run(GTK_DIALOG(md)) == GTK_RESPONSE_OK) {
        out = gtk_entry_get_text(GTK_ENTRY(ent));
    }
    gtk_widget_destroy(md);
    return out;
}

void on_approve_files_clicked(GtkWidget*, gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);
    if (st && st->modal_open > 0) return;
    struct ModalGuard { GtkAppState* a; ModalGuard(GtkAppState* x) : a(x) { if (a) ++a->modal_open; } ~ModalGuard() { if (a && a->modal_open > 0) --a->modal_open; } } mg(st);

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        oyk_gui_screen_title(OYK_SCR_FILES),
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "Refresh", 3,
        GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
        static_cast<const char*>(nullptr));
    gtk_window_set_default_size(GTK_WINDOW(dlg), 780, 470);
    gtk_attach_esc_close(dlg, GTK_RESPONSE_CLOSE);

    GtkWidget* vbox = GTK_DIALOG(dlg)->vbox;
    GtkWidget* hdr_lbl = gtk_label_new("Dual-Pane Jailed File Manager  |  Incoming (left)  |  Files (right)");
    gtk_misc_set_alignment(GTK_MISC(hdr_lbl), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), hdr_lbl, FALSE, FALSE, 4);

    GtkWidget* paned = gtk_hpaned_new();
    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 4);

    // Left pane: Incoming
    GtkWidget* inc_box = gtk_vbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(inc_box), 4);
    GtkWidget* inc_path_lbl = gtk_label_new("Incoming: /");
    gtk_misc_set_alignment(GTK_MISC(inc_path_lbl), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(inc_box), inc_path_lbl, FALSE, FALSE, 2);

    GtkListStore* inc_store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING,
                                                 G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
    GtkWidget* inc_tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(inc_store));
    add_text_column(GTK_TREE_VIEW(inc_tv), "Name", 0, 240);
    add_text_column(GTK_TREE_VIEW(inc_tv), "Size", 2, 90);
    GtkWidget* inc_sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(inc_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(inc_sc), inc_tv);
    gtk_box_pack_start(GTK_BOX(inc_box), inc_sc, TRUE, TRUE, 0);
    gtk_paned_pack1(GTK_PANED(paned), inc_box, TRUE, FALSE);

    // Right pane: Files
    GtkWidget* files_box = gtk_vbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(files_box), 4);
    GtkWidget* files_path_lbl = gtk_label_new("Files: /");
    gtk_misc_set_alignment(GTK_MISC(files_path_lbl), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(files_box), files_path_lbl, FALSE, FALSE, 2);

    GtkListStore* files_store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING,
                                                   G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
    GtkWidget* files_tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(files_store));
    add_text_column(GTK_TREE_VIEW(files_tv), "Name", 0, 240);
    add_text_column(GTK_TREE_VIEW(files_tv), "Size", 2, 90);
    GtkWidget* files_sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(files_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(files_sc), files_tv);
    gtk_box_pack_start(GTK_BOX(files_box), files_sc, TRUE, TRUE, 0);
    gtk_paned_pack2(GTK_PANED(paned), files_box, TRUE, FALSE);

    // Action bar
    GtkWidget* act_bar = gtk_hbox_new(FALSE, 6);
    GtkWidget* btn_upload = gtk_button_new_with_label("Upload...");
    GtkWidget* btn_approve = gtk_button_new_with_label("Approve ->");
    GtkWidget* btn_rename = gtk_button_new_with_label("Rename");
    GtkWidget* btn_mkdir = gtk_button_new_with_label("New Dir");
    GtkWidget* btn_del = gtk_button_new_with_label("Delete");
    GtkWidget* btn_view = gtk_button_new_with_label("View");
    gtk_box_pack_start(GTK_BOX(act_bar), btn_upload, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(act_bar), btn_approve, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(act_bar), btn_rename, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(act_bar), btn_mkdir, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(act_bar), btn_del, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(act_bar), btn_view, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), act_bar, FALSE, FALSE, 2);

    GtkWidget* fb_lbl = gtk_label_new("Double-click folders to navigate. Select item and use actions above.");
    gtk_misc_set_alignment(GTK_MISC(fb_lbl), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), fb_lbl, FALSE, FALSE, 4);

    struct FilesDlgCtx {
        GtkAppState* app;
        GtkWidget* dlg;
        GtkListStore* inc_store;
        GtkWidget* inc_tv;
        GtkWidget* inc_path_lbl;
        std::string inc_rel;
        GtkListStore* files_store;
        GtkWidget* files_tv;
        GtkWidget* files_path_lbl;
        std::string files_rel;
        int active_pane; /* 0 = Incoming, 1 = Files */
        GtkWidget* fb_lbl;
    } fctx{ st, dlg, inc_store, inc_tv, inc_path_lbl, "", files_store, files_tv, files_path_lbl, "", 0, fb_lbl };

    auto refresh_pane = +[](FilesDlgCtx* c, int pane_idx) {
        bool is_inc = (pane_idx == 0);
        GtkListStore* store = is_inc ? c->inc_store : c->files_store;
        GtkWidget* tv = is_inc ? c->inc_tv : c->files_tv;
        GtkWidget* lbl = is_inc ? c->inc_path_lbl : c->files_path_lbl;
        std::string& rel = is_inc ? c->inc_rel : c->files_rel;
        std::vector<node::BbsDirEntry> entries;
        std::string norm_rel, err;
        (void)c->app->engine->list_bbs_dir(is_inc, rel, entries, norm_rel, err);
        rel = norm_rel;
        std::string title = std::string(is_inc ? "Incoming: /" : "Files: /") + rel;
        if (c->active_pane == pane_idx) title = "* " + title;
        gtk_label_set_text(GTK_LABEL(lbl), title.c_str());
        gtk_list_store_clear(store);
        for (const auto& e : entries) {
            GtkTreeIter iter;
            std::string disp = e.name;
            std::string sz = e.is_parent ? "<UP>"
                           : (e.is_dir ? "<DIR>" : (std::to_string(e.size_bytes) + " B"));
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter,
                               0, disp.c_str(),
                               1, e.name.c_str(),
                               2, sz.c_str(),
                               3, e.is_dir ? TRUE : FALSE,
                               4, e.is_parent ? TRUE : FALSE,
                               -1);
        }
        GtkTreeIter first;
        if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &first)) {
            GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tv));
            gtk_tree_selection_select_iter(sel, &first);
        }
    };

    auto refresh_both = +[](FilesDlgCtx* c,
                            void (*rp)(FilesDlgCtx*, int)) {
        rp(c, 0);
        rp(c, 1);
    };

    auto get_sel_entry = +[](FilesDlgCtx* c, int pane_idx,
                             std::string& name_out, bool& is_dir_out, bool& is_parent_out) -> bool {
        GtkWidget* tv = (pane_idx == 0) ? c->inc_tv : c->files_tv;
        GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tv));
        GtkTreeModel* model = nullptr;
        GtkTreeIter iter;
        if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return false;
        gchar* raw = nullptr;
        gboolean d = FALSE, p = FALSE;
        gtk_tree_model_get(model, &iter, 1, &raw, 3, &d, 4, &p, -1);
        name_out = raw ? raw : "";
        if (raw) g_free(raw);
        is_dir_out = (d != FALSE);
        is_parent_out = (p != FALSE);
        return true;
    };

    struct DlgCallWrap {
        FilesDlgCtx* c;
        decltype(refresh_pane) rp;
        decltype(get_sel_entry) gs;
    } cw{ &fctx, refresh_pane, get_sel_entry };

    auto on_inc_cursor = +[](GtkTreeView*, gpointer d) {
        auto* w = static_cast<DlgCallWrap*>(d);
        w->c->active_pane = 0;
        w->rp(w->c, 0);
        w->rp(w->c, 1);
    };
    auto on_files_cursor = +[](GtkTreeView*, gpointer d) {
        auto* w = static_cast<DlgCallWrap*>(d);
        w->c->active_pane = 1;
        w->rp(w->c, 0);
        w->rp(w->c, 1);
    };
    g_signal_connect(inc_tv, "cursor-changed", G_CALLBACK(on_inc_cursor), &cw);
    g_signal_connect(files_tv, "cursor-changed", G_CALLBACK(on_files_cursor), &cw);

    auto open_preview_dlg = +[](GtkWidget* parent, const std::string& title, const std::string& body) {
        GtkWidget* pv = gtk_dialog_new_with_buttons(
            ("Preview: " + title).c_str(), GTK_WINDOW(parent),
            static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
            GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE, static_cast<const char*>(nullptr));
        gtk_window_set_default_size(GTK_WINDOW(pv), 560, 360);
        GtkWidget* tv = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
        PangoFontDescription* m = pango_font_description_from_string("Monospace 9");
        gtk_widget_modify_font(tv, m);
        pango_font_description_free(m);
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)), body.c_str(), -1);
        GtkWidget* sc = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_add(GTK_CONTAINER(sc), tv);
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pv)->vbox), sc, TRUE, TRUE, 4);
        gtk_widget_show_all(pv);
        (void)gtk_dialog_run(GTK_DIALOG(pv));
        gtk_widget_destroy(pv);
    };
    struct RowActWrap {
        DlgCallWrap* cw;
        decltype(open_preview_dlg) pv;
    } rw0{ &cw, open_preview_dlg }, rw1{ &cw, open_preview_dlg };

    auto on_inc_act = +[](GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer d) {
        auto* w = static_cast<RowActWrap*>(d);
        w->cw->c->active_pane = 0;
        std::string name; bool is_dir = false, is_par = false;
        if (!w->cw->gs(w->cw->c, 0, name, is_dir, is_par)) return;
        if (is_dir || is_par) {
            std::string next_rel, err;
            if (w->cw->c->app->engine->enter_bbs_subdir(true, w->cw->c->inc_rel, name, next_rel, err)) {
                w->cw->c->inc_rel = next_rel;
                w->cw->rp(w->cw->c, 0);
            } else {
                gtk_label_set_text(GTK_LABEL(w->cw->c->fb_lbl), err.c_str());
            }
        } else {
            std::string body, err; bool trunc = false;
            if (w->cw->c->app->engine->read_bbs_text_preview(true, w->cw->c->inc_rel, name, body, trunc, err)) {
                w->pv(w->cw->c->dlg, name, body);
            } else {
                gtk_label_set_text(GTK_LABEL(w->cw->c->fb_lbl), err.c_str());
            }
        }
    };
    auto on_files_act = +[](GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer d) {
        auto* w = static_cast<RowActWrap*>(d);
        w->cw->c->active_pane = 1;
        std::string name; bool is_dir = false, is_par = false;
        if (!w->cw->gs(w->cw->c, 1, name, is_dir, is_par)) return;
        if (is_dir || is_par) {
            std::string next_rel, err;
            if (w->cw->c->app->engine->enter_bbs_subdir(false, w->cw->c->files_rel, name, next_rel, err)) {
                w->cw->c->files_rel = next_rel;
                w->cw->rp(w->cw->c, 1);
            } else {
                gtk_label_set_text(GTK_LABEL(w->cw->c->fb_lbl), err.c_str());
            }
        } else {
            std::string body, err; bool trunc = false;
            if (w->cw->c->app->engine->read_bbs_text_preview(false, w->cw->c->files_rel, name, body, trunc, err)) {
                w->pv(w->cw->c->dlg, name, body);
            } else {
                gtk_label_set_text(GTK_LABEL(w->cw->c->fb_lbl), err.c_str());
            }
        }
    };
    g_signal_connect(inc_tv, "row-activated", G_CALLBACK(on_inc_act), &rw0);
    g_signal_connect(files_tv, "row-activated", G_CALLBACK(on_files_act), &rw1);

    auto cb_upload = +[](GtkWidget*, gpointer d) {
        auto* w = static_cast<DlgCallWrap*>(d);
        GtkWidget* fc = gtk_file_chooser_dialog_new(
            "Select Local File to Upload", GTK_WINDOW(w->c->dlg),
            GTK_FILE_CHOOSER_ACTION_OPEN,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
            GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
            static_cast<const char*>(nullptr));
        if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
            char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
            if (fn) {
                bool is_inc = (w->c->active_pane == 0);
                const std::string& rel = is_inc ? w->c->inc_rel : w->c->files_rel;
                std::string dst, err; bool ex = false;
                if (w->c->app->engine->import_local_file_to_bbs(fn, is_inc, rel, true, dst, ex, err)) {
                    gtk_label_set_text(GTK_LABEL(w->c->fb_lbl), ("Uploaded -> " + dst).c_str());
                    w->rp(w->c, w->c->active_pane);
                } else {
                    gtk_label_set_text(GTK_LABEL(w->c->fb_lbl), ("Upload error: " + err).c_str());
                }
                g_free(fn);
            }
        }
        gtk_widget_destroy(fc);
    };
    auto cb_approve = +[](GtkWidget*, gpointer d) {
        auto* w = static_cast<DlgCallWrap*>(d);
        std::string name; bool is_dir = false, is_par = false;
        if (!w->gs(w->c, 0, name, is_dir, is_par) || is_dir || is_par) {
            gtk_label_set_text(GTK_LABEL(w->c->fb_lbl), "Select a file in the Incoming pane.");
            return;
        }
        std::string inc_file_rel = w->c->inc_rel.empty() ? name : (w->c->inc_rel + "/" + name);
        std::string dst, err; bool ex = false;
        bool ok = w->c->app->engine->approve_incoming_rel_file(inc_file_rel, w->c->files_rel,
                                                              false, dst, ex, err);
        if (!ok && ex) {
            GtkWidget* md = gtk_message_dialog_new(
                GTK_WINDOW(w->c->dlg),
                static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
                "File already exists in destination:\n%s\n\nOverwrite?", dst.c_str());
            gint ow = gtk_dialog_run(GTK_DIALOG(md));
            gtk_widget_destroy(md);
            if (ow == GTK_RESPONSE_YES) {
                ok = w->c->app->engine->approve_incoming_rel_file(inc_file_rel, w->c->files_rel,
                                                                  true, dst, ex, err);
            }
        }
        if (ok) {
            gtk_label_set_text(GTK_LABEL(w->c->fb_lbl), ("Approved -> " + dst).c_str());
            w->rp(w->c, 0);
            w->rp(w->c, 1);
            refresh_ui(w->c->app);
        } else {
            gtk_label_set_text(GTK_LABEL(w->c->fb_lbl), ("Approve error: " + err).c_str());
        }
    };
    auto cb_rename = +[](GtkWidget*, gpointer d) {
        auto* w = static_cast<DlgCallWrap*>(d);
        int pane = w->c->active_pane;
        std::string name; bool is_dir = false, is_par = false;
        if (!w->gs(w->c, pane, name, is_dir, is_par) || is_par) return;
        std::string nn = gtk_prompt_input_text(w->c->dlg, "Rename Entry", "New name:", name.c_str());
        if (nn.empty()) return;
        std::string err;
        bool is_inc = (pane == 0);
        const std::string& rel = is_inc ? w->c->inc_rel : w->c->files_rel;
        if (w->c->app->engine->rename_bbs_entry(is_inc, rel, name, nn, err)) {
            gtk_label_set_text(GTK_LABEL(w->c->fb_lbl), ("Renamed -> " + nn).c_str());
            w->rp(w->c, pane);
        } else {
            gtk_label_set_text(GTK_LABEL(w->c->fb_lbl), ("Rename error: " + err).c_str());
        }
    };
    auto cb_mkdir = +[](GtkWidget*, gpointer d) {
        auto* w = static_cast<DlgCallWrap*>(d);
        int pane = w->c->active_pane;
        std::string dn = gtk_prompt_input_text(w->c->dlg, "New Directory", "Directory name:", "");
        if (dn.empty()) return;
        std::string err;
        bool is_inc = (pane == 0);
        const std::string& rel = is_inc ? w->c->inc_rel : w->c->files_rel;
        if (w->c->app->engine->mkdir_bbs_dir(is_inc, rel, dn, err)) {
            gtk_label_set_text(GTK_LABEL(w->c->fb_lbl), ("Created folder " + dn).c_str());
            w->rp(w->c, pane);
        } else {
            gtk_label_set_text(GTK_LABEL(w->c->fb_lbl), ("New Dir error: " + err).c_str());
        }
    };
    auto cb_del = +[](GtkWidget*, gpointer d) {
        auto* w = static_cast<DlgCallWrap*>(d);
        int pane = w->c->active_pane;
        std::string name; bool is_dir = false, is_par = false;
        if (!w->gs(w->c, pane, name, is_dir, is_par) || is_par) return;
        GtkWidget* md = gtk_message_dialog_new(
            GTK_WINDOW(w->c->dlg),
            static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
            "Delete '%s'?", name.c_str());
        gint ok = gtk_dialog_run(GTK_DIALOG(md));
        gtk_widget_destroy(md);
        if (ok != GTK_RESPONSE_YES) return;
        std::string err;
        bool is_inc = (pane == 0);
        const std::string& rel = is_inc ? w->c->inc_rel : w->c->files_rel;
        if (w->c->app->engine->delete_bbs_entry(is_inc, rel, name, err)) {
            gtk_label_set_text(GTK_LABEL(w->c->fb_lbl), ("Deleted " + name).c_str());
            w->rp(w->c, pane);
            refresh_ui(w->c->app);
        } else {
            gtk_label_set_text(GTK_LABEL(w->c->fb_lbl), ("Delete error: " + err).c_str());
        }
    };
    auto cb_view = +[](GtkWidget*, gpointer d) {
        auto* w = static_cast<RowActWrap*>(d);
        int pane = w->cw->c->active_pane;
        std::string name; bool is_dir = false, is_par = false;
        if (!w->cw->gs(w->cw->c, pane, name, is_dir, is_par) || is_dir || is_par) return;
        bool is_inc = (pane == 0);
        const std::string& rel = is_inc ? w->cw->c->inc_rel : w->cw->c->files_rel;
        std::string body, err; bool trunc = false;
        if (w->cw->c->app->engine->read_bbs_text_preview(is_inc, rel, name, body, trunc, err)) {
            w->pv(w->cw->c->dlg, name, body);
        } else {
            gtk_label_set_text(GTK_LABEL(w->cw->c->fb_lbl), err.c_str());
        }
    };
    g_signal_connect(btn_upload, "clicked", G_CALLBACK(cb_upload), &cw);
    g_signal_connect(btn_approve, "clicked", G_CALLBACK(cb_approve), &cw);
    g_signal_connect(btn_rename, "clicked", G_CALLBACK(cb_rename), &cw);
    g_signal_connect(btn_mkdir, "clicked", G_CALLBACK(cb_mkdir), &cw);
    g_signal_connect(btn_del, "clicked", G_CALLBACK(cb_del), &cw);
    g_signal_connect(btn_view, "clicked", G_CALLBACK(cb_view), &rw0);

    refresh_both(&fctx, refresh_pane);
    gtk_widget_show_all(dlg);

    GtkSimpleCapCtx cap_ctx{dlg, "files", GTK_RESPONSE_CLOSE, 0};
    if (g_gtk_test.enabled) g_timeout_add(160, on_gtk_simple_dlg_cap, &cap_ctx);

    while (true) {
        gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
        if (resp == 3) {
            refresh_both(&fctx, refresh_pane);
            gtk_label_set_text(GTK_LABEL(fb_lbl), "Refreshed Incoming and Files panes.");
            continue;
        }
        break;
    }

    g_object_unref(inc_store);
    g_object_unref(files_store);
    gtk_widget_destroy(dlg);
}

void on_rig_control_clicked(GtkWidget*, gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);
    if (st && st->modal_open > 0) return;
    struct ModalGuard { GtkAppState* a; ModalGuard(GtkAppState* x) : a(x) { if (a) ++a->modal_open; } ~ModalGuard() { if (a && a->modal_open > 0) --a->modal_open; } } mg(st);
    auto& rig = st->engine->rig_controller();
    auto rc = rig.config();

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Radio / Rig Control",
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
        static_cast<const char*>(nullptr));
    gtk_window_set_default_size(GTK_WINDOW(dlg), 500, 340);
    gtk_attach_esc_close(dlg, GTK_RESPONSE_CLOSE);

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
    GtkListStore* peer_store = nullptr;
    GtkWidget* peer_tree = nullptr;
    std::vector<radio::PeerConfig>* peers = nullptr;
};

bool gtk_iface_ci_eq(const std::string& a, const std::string& b);

static std::string gtk_peer_acl_str(const radio::PeerConfig& p) {
    return std::string(p.allow_inbound ? "IN" : "--") + "/" +
           std::string(p.allow_outbound ? "OUT" : "---");
}

static void repopulate_gtk_peers(GtkSettingsCtx* ctx) {
    gtk_list_store_clear(ctx->peer_store);
    for (std::size_t i = 0; i < ctx->peers->size(); ++i) {
        const auto& p = (*ctx->peers)[i];
        std::string ep = p.host;
        if (!ep.empty() && p.port > 0) ep += ":" + std::to_string(p.port);
        if (ep.empty()) ep = "(any)";
        GtkTreeIter iter;
        gtk_list_store_append(ctx->peer_store, &iter);
        gtk_list_store_set(ctx->peer_store, &iter,
                           0, p.id.c_str(),
                           1, p.callsign.c_str(),
                           2, p.interface_id.c_str(),
                           3, ep.c_str(),
                           4, gtk_peer_acl_str(p).c_str(),
                           5, p.auto_learned ? "AUTO" : "",
                           6, static_cast<gint>(i),
                           -1);
    }
}

static void run_gtk_edit_peer_dialog(GtkSettingsCtx* ctx, int edit_idx) {
    const bool is_add = (edit_idx < 0 || static_cast<std::size_t>(edit_idx) >= ctx->peers->size());
    radio::PeerConfig src;
    if (!is_add) {
        src = (*ctx->peers)[static_cast<std::size_t>(edit_idx)];
    } else {
        src.allow_inbound = true;
        src.allow_outbound = true;
        src.auto_learned = false;
        for (const auto& ic : *ctx->ifaces) {
            if (ic.type != "telnet" && ic.type != "local") {
                src.interface_id = ic.id;
                break;
            }
        }
    }

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        is_add ? "Add Peer" : "Edit Peer",
        GTK_WINDOW(ctx->dlg),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
        static_cast<const char*>(nullptr));
    gtk_window_set_default_size(GTK_WINDOW(dlg), 420, -1);

    GtkWidget* tbl = gtk_table_new(9, 2, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(tbl), 10);
    gtk_table_set_row_spacings(GTK_TABLE(tbl), 6);
    gtk_table_set_col_spacings(GTK_TABLE(tbl), 8);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), tbl, TRUE, TRUE, 0);

    auto attach = [&](int r, const char* lbl, GtkWidget* w) {
        GtkWidget* l = gtk_label_new(lbl);
        gtk_misc_set_alignment(GTK_MISC(l), 0.0f, 0.5f);
        gtk_table_attach(GTK_TABLE(tbl), l, 0, 1, r, r + 1, GTK_FILL, GTK_FILL, 0, 0);
        gtk_table_attach(GTK_TABLE(tbl), w, 1, 2, r, r + 1,
                         static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);
    };

    GtkWidget* e_id = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_id), src.id.c_str());
    attach(0, "Peer ID:", e_id);

    GtkWidget* e_call = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_call), src.callsign.c_str());
    attach(1, "Callsign:", e_call);

    GtkWidget* e_desc = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_desc), src.description.c_str());
    attach(2, "Description:", e_desc);

    GtkWidget* cb_iface = gtk_combo_box_new_text();
    std::vector<std::string> iface_opts;
    int active_if = -1;
    for (const auto& ic : *ctx->ifaces) {
        if (ic.type == "telnet" || ic.type == "local") continue;
        iface_opts.push_back(ic.id);
        gtk_combo_box_append_text(GTK_COMBO_BOX(cb_iface), ic.id.c_str());
        if (ic.id == src.interface_id) active_if = static_cast<int>(iface_opts.size()) - 1;
    }
    if (active_if < 0 && !src.interface_id.empty()) {
        iface_opts.push_back(src.interface_id);
        gtk_combo_box_append_text(GTK_COMBO_BOX(cb_iface), src.interface_id.c_str());
        active_if = static_cast<int>(iface_opts.size()) - 1;
    }
    if (active_if < 0 && !iface_opts.empty()) active_if = 0;
    if (active_if >= 0) gtk_combo_box_set_active(GTK_COMBO_BOX(cb_iface), active_if);
    attach(3, "Interface:", cb_iface);

    GtkWidget* e_host = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_host), src.host.c_str());
    attach(4, "Host / DMR ID:", e_host);

    GtkWidget* e_port = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_port), (src.port > 0) ? std::to_string(src.port).c_str() : "");
    attach(5, "Port (0=default):", e_port);

    GtkWidget* chk_in = gtk_check_button_new_with_label("Allow inbound connections from this peer");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_in), src.allow_inbound ? TRUE : FALSE);
    gtk_table_attach(GTK_TABLE(tbl), chk_in, 0, 2, 6, 7, GTK_FILL, GTK_FILL, 0, 0);

    GtkWidget* chk_out = gtk_check_button_new_with_label("Allow outbound connections to this peer");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_out), src.allow_outbound ? TRUE : FALSE);
    gtk_table_attach(GTK_TABLE(tbl), chk_out, 0, 2, 7, 8, GTK_FILL, GTK_FILL, 0, 0);

    GtkWidget* lbl_err = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(lbl_err), 0.0f, 0.5f);
    gtk_table_attach(GTK_TABLE(tbl), lbl_err, 0, 2, 8, 9, GTK_FILL, GTK_FILL, 0, 0);

    gtk_widget_show_all(dlg);
    gtk_widget_hide(lbl_err);

    GtkSimpleCapCtx cap_ctx{dlg, "edit_peer", GTK_RESPONSE_CANCEL, 0};
    if (g_gtk_test.enabled) g_timeout_add(160, on_gtk_simple_dlg_cap, &cap_ctx);
    while (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        std::string call_s = gtk_entry_get_text(GTK_ENTRY(e_call));
        auto parsed = ax25::Callsign::parse(call_s);
        if (!parsed) {
            gtk_label_set_markup(GTK_LABEL(lbl_err), "<span foreground=\"#cc0000\"><b>Invalid AX.25 callsign.</b></span>");
            gtk_widget_show(lbl_err);
            continue;
        }
        std::string id_s = gtk_entry_get_text(GTK_ENTRY(e_id));
        if (id_s.empty()) {
            id_s = node::derive_default_peer_id(*ctx->peers, parsed->str());
        }
        bool dup = false;
        for (std::size_t i = 0; i < ctx->peers->size(); ++i) {
            if (!is_add && i == static_cast<std::size_t>(edit_idx)) continue;
            if (gtk_iface_ci_eq((*ctx->peers)[i].id, id_s)) { dup = true; break; }
        }
        if (dup) {
            std::string mk = "<span foreground=\"#cc0000\"><b>Peer ID '" + escape_markup(id_s) + "' already exists.</b></span>";
            gtk_label_set_markup(GTK_LABEL(lbl_err), mk.c_str());
            gtk_widget_show(lbl_err);
            continue;
        }
        uint16_t pnum = 0;
        std::string port_s = gtk_entry_get_text(GTK_ENTRY(e_port));
        if (!port_s.empty()) {
            try {
                int pv = std::stoi(port_s);
                if (pv < 0 || pv > 65535) {
                    gtk_label_set_markup(GTK_LABEL(lbl_err), "<span foreground=\"#cc0000\"><b>Port must be 0..65535.</b></span>");
                    gtk_widget_show(lbl_err);
                    continue;
                }
                pnum = static_cast<uint16_t>(pv);
            } catch (...) {
                gtk_label_set_markup(GTK_LABEL(lbl_err), "<span foreground=\"#cc0000\"><b>Invalid port number.</b></span>");
                gtk_widget_show(lbl_err);
                continue;
            }
        }
        std::string host_s = gtk_entry_get_text(GTK_ENTRY(e_host));
        if (!host_s.empty() && radio::is_all_ascii_digits(host_s)) {
            uint32_t did = 0;
            if (!radio::parse_dmr_id(host_s, did)) {
                gtk_label_set_markup(GTK_LABEL(lbl_err), "<span foreground=\"#cc0000\"><b>Invalid DMR ID (allowed 1..16777214).</b></span>");
                gtk_widget_show(lbl_err);
                continue;
            }
        }
        gchar* iface_raw = gtk_combo_box_get_active_text(GTK_COMBO_BOX(cb_iface));
        std::string iface_s = iface_raw ? iface_raw : "";
        if (iface_raw) g_free(iface_raw);

        radio::PeerConfig next_p;
        next_p.id = id_s;
        next_p.callsign = parsed->str();
        next_p.description = gtk_entry_get_text(GTK_ENTRY(e_desc));
        next_p.interface_id = iface_s;
        next_p.host = host_s;
        next_p.port = pnum;
        next_p.allow_inbound = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_in)) != FALSE;
        next_p.allow_outbound = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_out)) != FALSE;
        next_p.auto_learned = false;

        if (is_add) {
            ctx->peers->push_back(std::move(next_p));
        } else {
            (*ctx->peers)[static_cast<std::size_t>(edit_idx)] = std::move(next_p);
        }
        repopulate_gtk_peers(ctx);
        break;
    }
    gtk_widget_destroy(dlg);
}

void on_settings_add_peer_clicked(GtkWidget*, gpointer user_data) {
    run_gtk_edit_peer_dialog(static_cast<GtkSettingsCtx*>(user_data), -1);
}

void on_settings_edit_peer_clicked(GtkWidget*, gpointer user_data) {
    auto* ctx = static_cast<GtkSettingsCtx*>(user_data);
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(ctx->peer_tree));
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;
    gint idx = -1;
    gtk_tree_model_get(model, &iter, 6, &idx, -1);
    if (idx >= 0 && static_cast<std::size_t>(idx) < ctx->peers->size()) {
        run_gtk_edit_peer_dialog(ctx, idx);
    }
}

void on_settings_peer_row_activated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer user_data) {
    on_settings_edit_peer_clicked(nullptr, user_data);
}

void on_settings_rem_peer_clicked(GtkWidget*, gpointer user_data) {
    auto* ctx = static_cast<GtkSettingsCtx*>(user_data);
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(ctx->peer_tree));
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;
    gint idx = -1;
    gtk_tree_model_get(model, &iter, 6, &idx, -1);
    if (idx >= 0 && static_cast<std::size_t>(idx) < ctx->peers->size()) {
        ctx->peers->erase(ctx->peers->begin() + idx);
        repopulate_gtk_peers(ctx);
    }
}

void on_settings_keep_auto_peers_clicked(GtkWidget*, gpointer user_data) {
    auto* ctx = static_cast<GtkSettingsCtx*>(user_data);
    for (auto& p : *ctx->peers) {
        p.auto_learned = false;
    }
    repopulate_gtk_peers(ctx);
}

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

struct GtkIfaceDriverOpt {
    const char* type;
    const char* desc;
    const char* def_host;
    const char* def_port;
};

static const GtkIfaceDriverOpt GTK_IFACE_DRIVERS[5] = {
    { "axudp",       "AX.25 over UDP (BPQ / AXUDP)",     "0.0.0.0",      "10093" },
    { "axip",        "AX.25 over IP (Protocol 93)",      "0.0.0.0",      "0" },
    { "kiss-serial", "KISS TNC over Serial Port",        "/dev/ttyUSB0", "9600" },
    { "kiss-tcp",    "KISS TNC over TCP (Direwolf)",     "127.0.0.1",    "8001" },
    { "telnet",      "Plaintext Telnet User Port",       "0.0.0.0",      "8010" }
};

bool gtk_iface_ci_eq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool gtk_is_valid_ipv4(const std::string& s) {
    struct in_addr a{};
    return inet_pton(AF_INET, s.c_str(), &a) == 1;
}

radio::RadioInterfaceConfig gtk_build_iface_config(
    int drv_idx,
    const std::string& id,
    const std::string& desc,
    const std::string& host,
    const std::string& port,
    const std::string& mode,
    const std::string& kport,
    const std::string& netmask,
    const std::string& peer,
    bool allow_in,
    bool allow_out,
    bool allow_same,
    const std::string& learn_pol,
    const radio::RadioInterfaceConfig* prev) {
    radio::RadioInterfaceConfig ic;
    const std::string type_s = GTK_IFACE_DRIVERS[drv_idx].type;
    ic.id = id;
    ic.type = type_s;
    ic.description = desc;
    ic.allow_inbound = allow_in;
    ic.allow_outbound = (type_s == "telnet") ? false : allow_out;
    ic.allow_same_interface = (type_s == "telnet") ? false : allow_same;
    if (type_s == "axudp") {
        ic.params["bind_host"] = host.empty() ? "0.0.0.0" : host;
        ic.params["bind_port"] = port.empty() ? "10093" : port;
        if (!netmask.empty()) ic.params["netmask"] = netmask;
        ic.params["auto_learn"] = "true";
        if (!peer.empty()) ic.peers.push_back(peer);
    } else if (type_s == "axip") {
        ic.params["bind_host"] = host.empty() ? "0.0.0.0" : host;
        if (!netmask.empty()) ic.params["netmask"] = netmask;
        ic.params["auto_learn"] = "true";
        if (!peer.empty()) ic.peers.push_back(peer);
    } else if (type_s == "kiss-serial") {
        ic.params["device"] = host.empty() ? "/dev/ttyUSB0" : host;
        ic.params["baud"] = port.empty() ? "9600" : port;
        ic.params["kiss_port"] = kport.empty() ? "0" : kport;
    } else if (type_s == "kiss-tcp") {
        ic.params["mode"] = mode;
        ic.params["host"] = host.empty() ? "127.0.0.1" : host;
        ic.params["port"] = port.empty() ? "8001" : port;
        ic.params["kiss_port"] = kport.empty() ? "0" : kport;
    } else {
        ic.params["bind_host"] = host.empty() ? "0.0.0.0" : host;
        ic.params["bind_port"] = port.empty() ? "8010" : port;
    }
    if (type_s != "telnet" && !learn_pol.empty() && learn_pol != "inherit") {
        ic.params["learned_peer_policy"] = learn_pol;
    }
    if (prev && (type_s == "axudp" || type_s == "axip")) {
        for (std::size_t p = 1; p < prev->peers.size(); ++p) {
            ic.peers.push_back(prev->peers[p]);
        }
    }
    return ic;
}

void run_gtk_add_iface_wizard(GtkSettingsCtx* ctx) {
    enum { WIZ_RESP_BACK = 1, WIZ_RESP_NEXT = 2 };

    GtkWidget* dlg = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dlg), oyk_gui_screen_title(OYK_SCR_ADD_IFACE));
    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(ctx->dlg));
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dlg), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 480, 410);

    gtk_dialog_add_button(GTK_DIALOG(dlg), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    GtkWidget* btn_back = gtk_dialog_add_button(GTK_DIALOG(dlg), "< _Back", WIZ_RESP_BACK);
    GtkWidget* btn_next = gtk_dialog_add_button(GTK_DIALOG(dlg), "_Next >", WIZ_RESP_NEXT);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), WIZ_RESP_NEXT);

    GtkWidget* outer = gtk_vbox_new(FALSE, 8);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 10);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), outer, TRUE, TRUE, 0);

    /* Top header: prompt label on left + compact progress bar in top-right corner */
    GtkWidget* hdr_row = gtk_hbox_new(FALSE, 8);
    gtk_box_pack_start(GTK_BOX(outer), hdr_row, FALSE, FALSE, 0);

    GtkWidget* lbl_prompt = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(lbl_prompt), 0.0f, 0.5f);
    gtk_label_set_line_wrap(GTK_LABEL(lbl_prompt), TRUE);
    gtk_box_pack_start(GTK_BOX(hdr_row), lbl_prompt, TRUE, TRUE, 0);

    GtkWidget* pbar = gtk_progress_bar_new();
    gtk_widget_set_size_request(pbar, 118, 18);
    gtk_box_pack_end(GTK_BOX(hdr_row), pbar, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(outer), gtk_hseparator_new(), FALSE, FALSE, 0);

    GtkWidget* nb = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(nb), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(nb), FALSE);
    gtk_box_pack_start(GTK_BOX(outer), nb, TRUE, TRUE, 0);

    /* Page 0: Driver Selection List */
    GtkListStore* pstore = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    const int n_drv = oyk_gui_wiz_driver_count(OYK_DRV_ALL);
    for (int i = 0; i < n_drv; ++i) {
        GtkTreeIter it;
        gtk_list_store_append(pstore, &it);
        gtk_list_store_set(pstore, &it,
                           0, oyk_gui_wiz_driver_type(OYK_DRV_ALL, i),
                           1, oyk_gui_wiz_driver_desc(OYK_DRV_ALL, i),
                           2, i, -1);
    }
    GtkWidget* ptree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(pstore));
    g_object_unref(pstore);
    gtk_tree_view_insert_column_with_attributes(
        GTK_TREE_VIEW(ptree), -1, "Driver", gtk_cell_renderer_text_new(), "text", 0, nullptr);
    gtk_tree_view_insert_column_with_attributes(
        GTK_TREE_VIEW(ptree), -1, "Description", gtk_cell_renderer_text_new(), "text", 1, nullptr);

    g_signal_connect(ptree, "row-activated",
                     G_CALLBACK(+[](GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer d) {
                         gtk_dialog_response(GTK_DIALOG(d), WIZ_RESP_NEXT);
                     }),
                     dlg);
    GtkWidget* psw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(psw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(psw), ptree);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), psw, nullptr);

    /* Page 1: Interface ID & Description */
    GtkWidget* tbl1 = gtk_table_new(3, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(tbl1), 8);
    gtk_table_set_col_spacings(GTK_TABLE(tbl1), 10);
    GtkWidget* lbl_id_drv = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(lbl_id_drv), 0.0f, 0.5f);
    gtk_table_attach(GTK_TABLE(tbl1), gtk_label_new("Selected Driver:"), 0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(tbl1), lbl_id_drv, 1, 2, 0, 1,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);
    GtkWidget* e_id = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(e_id), TRUE);
    gtk_table_attach(GTK_TABLE(tbl1), gtk_label_new("Interface ID:"), 0, 1, 1, 2, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(tbl1), e_id, 1, 2, 1, 2,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);
    GtkWidget* e_desc = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(e_desc), TRUE);
    gtk_table_attach(GTK_TABLE(tbl1), gtk_label_new("Description (opt):"), 0, 1, 2, 3, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(tbl1), e_desc, 1, 2, 2, 3,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), tbl1, nullptr);

    /* Page 2: Driver Settings */
    GtkWidget* tbl2 = gtk_table_new(10, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(tbl2), 6);
    gtk_table_set_col_spacings(GTK_TABLE(tbl2), 10);
    GtkWidget* l_mode = gtk_label_new("KISS-TCP Mode:");
    gtk_misc_set_alignment(GTK_MISC(l_mode), 0.0f, 0.5f);
    GtkWidget* cb_mode = gtk_combo_box_new_text();
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_mode), "client");
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_mode), "server");
    gtk_combo_box_set_active(GTK_COMBO_BOX(cb_mode), 0);
    gtk_table_attach(GTK_TABLE(tbl2), l_mode, 0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(tbl2), cb_mode, 1, 2, 0, 1,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);

    GtkWidget* l_host = gtk_label_new("Host / Device:");
    gtk_misc_set_alignment(GTK_MISC(l_host), 0.0f, 0.5f);
    GtkWidget* e_host = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(e_host), TRUE);
    gtk_table_attach(GTK_TABLE(tbl2), l_host, 0, 1, 1, 2, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(tbl2), e_host, 1, 2, 1, 2,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);

    GtkWidget* l_port = gtk_label_new("Port / Baud:");
    gtk_misc_set_alignment(GTK_MISC(l_port), 0.0f, 0.5f);
    GtkWidget* e_port = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(e_port), TRUE);
    gtk_table_attach(GTK_TABLE(tbl2), l_port, 0, 1, 2, 3, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(tbl2), e_port, 1, 2, 2, 3,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);

    GtkWidget* l_netmask = gtk_label_new("Netmask (opt):");
    gtk_misc_set_alignment(GTK_MISC(l_netmask), 0.0f, 0.5f);
    GtkWidget* e_netmask = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(e_netmask), TRUE);
    gtk_table_attach(GTK_TABLE(tbl2), l_netmask, 0, 1, 3, 4, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(tbl2), e_netmask, 1, 2, 3, 4,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);

    GtkWidget* l_kport = gtk_label_new("KISS Port (0-15):");
    gtk_misc_set_alignment(GTK_MISC(l_kport), 0.0f, 0.5f);
    GtkWidget* e_kport = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_kport), "0");
    gtk_entry_set_activates_default(GTK_ENTRY(e_kport), TRUE);
    gtk_table_attach(GTK_TABLE(tbl2), l_kport, 0, 1, 4, 5, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(tbl2), e_kport, 1, 2, 4, 5,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);

    GtkWidget* l_peer = gtk_label_new("Static Peer (opt):");
    gtk_misc_set_alignment(GTK_MISC(l_peer), 0.0f, 0.5f);
    GtkWidget* e_peer = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(e_peer), TRUE);
    gtk_table_attach(GTK_TABLE(tbl2), l_peer, 0, 1, 5, 6, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(tbl2), e_peer, 1, 2, 5, 6,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);

    GtkWidget* chk_in = gtk_check_button_new_with_label("Allow inbound connections");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_in), TRUE);
    gtk_table_attach(GTK_TABLE(tbl2), chk_in, 0, 2, 6, 7, GTK_FILL, GTK_FILL, 0, 0);

    GtkWidget* chk_out = gtk_check_button_new_with_label("Allow outbound connections");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_out), TRUE);
    gtk_table_attach(GTK_TABLE(tbl2), chk_out, 0, 2, 7, 8, GTK_FILL, GTK_FILL, 0, 0);

    GtkWidget* chk_same = gtk_check_button_new_with_label("Allow same-interface forwarding");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_same), FALSE);
    gtk_table_attach(GTK_TABLE(tbl2), chk_same, 0, 2, 8, 9, GTK_FILL, GTK_FILL, 0, 0);

    GtkWidget* l_lpol = gtk_label_new("Learned Peer Policy:");
    gtk_misc_set_alignment(GTK_MISC(l_lpol), 0.0f, 0.5f);
    GtkWidget* cb_lpol = gtk_combo_box_new_text();
    for (const char* p : {"inherit", "none", "inbound", "outbound", "both"}) {
        gtk_combo_box_append_text(GTK_COMBO_BOX(cb_lpol), p);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(cb_lpol), 0);
    gtk_table_attach(GTK_TABLE(tbl2), l_lpol, 0, 1, 9, 10, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(tbl2), cb_lpol, 1, 2, 9, 10,
                     static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), tbl2, nullptr);

    /* Page 3: Review Summary */
    GtkWidget* sum_tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(sum_tv), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(sum_tv), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(sum_tv), GTK_WRAP_WORD_CHAR);
    GtkWidget* sum_sw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sum_sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sum_sw), sum_tv);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), sum_sw, nullptr);

    GtkWidget* lbl_err = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(lbl_err), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(outer), lbl_err, FALSE, FALSE, 0);

    gtk_widget_show_all(dlg);

    int step = 0;
    int drv_idx = 0;
    int last_drv = -1;

    GtkTreePath* psel_p = gtk_tree_path_new_from_indices(0, -1);
    gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(ptree)), psel_p);
    gtk_tree_path_free(psel_p);

    auto set_err = [&](const std::string& msg) {
        if (msg.empty()) {
            gtk_label_set_text(GTK_LABEL(lbl_err), "");
            gtk_widget_hide(lbl_err);
        } else {
            std::string mk = "<span foreground=\"#cc0000\"><b>" + escape_markup(msg) + "</b></span>";
            gtk_label_set_markup(GTK_LABEL(lbl_err), mk.c_str());
            gtk_widget_show(lbl_err);
        }
    };

    auto sync_wizard_ui = [&]() {
        std::string ptxt = "Step " + std::to_string(step + 1) + " of 4";
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pbar), static_cast<gdouble>(step + 1) / 4.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(pbar), ptxt.c_str());
        gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), step);
        gtk_widget_set_sensitive(btn_back, step > 0);
        gtk_button_set_label(GTK_BUTTON(btn_next), (step == 3) ? "_Finish" : "_Next >");

        const std::string t = GTK_IFACE_DRIVERS[drv_idx].type;
        if (step == 0) {
            gtk_label_set_markup(GTK_LABEL(lbl_prompt), "<b>Select the driver for the new interface:</b>");
        } else if (step == 1) {
            gtk_label_set_markup(GTK_LABEL(lbl_prompt),
                                 "<b>Enter a unique interface ID and optional description:</b>");
            gtk_label_set_text(GTK_LABEL(lbl_id_drv), (t + " - " + GTK_IFACE_DRIVERS[drv_idx].desc).c_str());
            gtk_widget_grab_focus(e_id);
        } else if (step == 2) {
            if (t == "axudp") {
                gtk_label_set_markup(GTK_LABEL(lbl_prompt), "<b>Configure AXUDP bind host/DMR ID, UDP port, netmask, and peer:</b>");
                gtk_label_set_text(GTK_LABEL(l_host), "Bind Host / DMR:");
                gtk_label_set_text(GTK_LABEL(l_port), "UDP Port:");
            } else if (t == "axip") {
                gtk_label_set_markup(GTK_LABEL(lbl_prompt), "<b>Configure AXIP bind host/DMR ID, netmask, and static peer:</b>");
                gtk_label_set_text(GTK_LABEL(l_host), "Bind Host / DMR:");
            } else if (t == "kiss-serial") {
                gtk_label_set_markup(GTK_LABEL(lbl_prompt), "<b>Configure serial device, baud rate, and KISS port:</b>");
                gtk_label_set_text(GTK_LABEL(l_host), "Serial Device:");
                gtk_label_set_text(GTK_LABEL(l_port), "Baud Rate:");
            } else if (t == "kiss-tcp") {
                gtk_label_set_markup(GTK_LABEL(lbl_prompt), "<b>Configure KISS-TCP mode, host, TCP port, and KISS port:</b>");
                gtk_label_set_text(GTK_LABEL(l_host), "Host:");
                gtk_label_set_text(GTK_LABEL(l_port), "TCP Port:");
            } else {
                gtk_label_set_markup(GTK_LABEL(lbl_prompt), "<b>Configure Telnet bind host and TCP port:</b>");
                gtk_label_set_text(GTK_LABEL(l_host), "Bind Host:");
                gtk_label_set_text(GTK_LABEL(l_port), "TCP Port:");
            }
            const bool is_ktcp = (t == "kiss-tcp");
            const bool is_kser = (t == "kiss-serial");
            const bool is_tel  = (t == "telnet");
            const bool has_port = (t != "axip");
            const bool has_peer = (t == "axudp" || t == "axip");
            gtk_widget_set_visible(l_mode, is_ktcp);
            gtk_widget_set_visible(cb_mode, is_ktcp);
            gtk_widget_set_visible(l_port, has_port);
            gtk_widget_set_visible(e_port, has_port);
            gtk_widget_set_visible(l_netmask, has_peer);
            gtk_widget_set_visible(e_netmask, has_peer);
            gtk_widget_set_visible(l_kport, is_kser || is_ktcp);
            gtk_widget_set_visible(e_kport, is_kser || is_ktcp);
            gtk_widget_set_visible(l_peer, has_peer);
            gtk_widget_set_visible(e_peer, has_peer);
            gtk_widget_set_visible(l_lpol, !is_tel);
            gtk_widget_set_visible(cb_lpol, !is_tel);
            if (is_tel) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_out), FALSE);
                gtk_widget_set_sensitive(chk_out, FALSE);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_same), FALSE);
                gtk_widget_set_sensitive(chk_same, FALSE);
            } else {
                gtk_widget_set_sensitive(chk_out, TRUE);
                gtk_widget_set_sensitive(chk_same, TRUE);
            }
            gtk_widget_grab_focus(e_host);
        } else {
            gtk_label_set_markup(GTK_LABEL(lbl_prompt),
                                 "<b>Review interface settings before committing:</b>");
            std::ostringstream oss;
            oss << "Driver:        " << t << "\n"
                << "Interface ID:  " << gtk_entry_get_text(GTK_ENTRY(e_id)) << "\n";
            std::string d_s = gtk_entry_get_text(GTK_ENTRY(e_desc));
            oss << "Description:   " << (d_s.empty() ? "(none)" : d_s) << "\n";
            std::string h_s = gtk_entry_get_text(GTK_ENTRY(e_host));
            std::string p_s = gtk_entry_get_text(GTK_ENTRY(e_port));
            std::string nm_s = gtk_entry_get_text(GTK_ENTRY(e_netmask));
            if (t == "kiss-serial") {
                oss << "Serial Device: " << h_s << "\n"
                    << "Baud Rate:     " << p_s << "\n"
                    << "KISS Port:     " << gtk_entry_get_text(GTK_ENTRY(e_kport)) << "\n";
            } else if (t == "kiss-tcp") {
                gchar* mr = gtk_combo_box_get_active_text(GTK_COMBO_BOX(cb_mode));
                oss << "Mode:          " << (mr ? mr : "client") << "\n"
                    << "Endpoint:      " << h_s << ":" << p_s << "\n"
                    << "KISS Port:     " << gtk_entry_get_text(GTK_ENTRY(e_kport)) << "\n";
                if (mr) g_free(mr);
            } else {
                oss << "Bind Host:     " << h_s << (nm_s.empty() ? "" : (" / " + nm_s)) << "\n";
                if (t != "axip") oss << "Port:          " << p_s << "\n";
                if (t == "axudp" || t == "axip") {
                    std::string pr = gtk_entry_get_text(GTK_ENTRY(e_peer));
                    oss << "Static Peer:   " << (pr.empty() ? "(auto-learn)" : pr) << "\n";
                }
            }
            const bool a_in = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_in)) != FALSE;
            const bool a_out = (t != "telnet") && (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_out)) != FALSE);
            const bool a_same = (t != "telnet") && (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_same)) != FALSE);
            gchar* lpr = gtk_combo_box_get_active_text(GTK_COMBO_BOX(cb_lpol));
            oss << "Allow Inbound: " << (a_in ? "true" : "false") << "\n"
                << "Allow Outbound:" << (a_out ? "true" : (t == "telnet" ? "false (disabled)" : "false")) << "\n"
                << "Same-Iface Fwd:" << (a_same ? "true" : (t == "telnet" ? "false (disabled)" : "false")) << "\n";
            if (t != "telnet") oss << "Learned Policy:" << (lpr ? lpr : "inherit") << "\n";
            if (lpr) g_free(lpr);
            gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(sum_tv)),
                                     oss.str().c_str(), -1);
        }
    };

    set_err("");
    sync_wizard_ui();

    struct WizCapCtx { GtkWidget* dlg; int ticks; } wiz_cap{dlg, 0};
    if (g_gtk_test.enabled) {
        g_timeout_add(160, +[](gpointer p) -> gboolean {
            auto* c = static_cast<WizCapCtx*>(p);
            ++c->ticks;
            if (c->ticks == 1) {
                gtk_capture_window_png(c->dlg, "add_iface_1");
                gtk_dialog_response(GTK_DIALOG(c->dlg), WIZ_RESP_NEXT);
                return TRUE;
            }
            if (c->ticks == 2) {
                gtk_capture_window_png(c->dlg, "add_iface_2");
                gtk_dialog_response(GTK_DIALOG(c->dlg), WIZ_RESP_NEXT);
                return TRUE;
            }
            if (c->ticks == 3) {
                gtk_capture_window_png(c->dlg, "add_iface_3");
                gtk_dialog_response(GTK_DIALOG(c->dlg), WIZ_RESP_NEXT);
                return TRUE;
            }
            gtk_capture_window_png(c->dlg, "add_iface_4");
            gtk_dialog_response(GTK_DIALOG(c->dlg), GTK_RESPONSE_CANCEL);
            return FALSE;
        }, &wiz_cap);
    }

    while (true) {
        gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
        if (resp == GTK_RESPONSE_CANCEL || resp == GTK_RESPONSE_DELETE_EVENT) {
            break;
        }
        if (resp == WIZ_RESP_BACK) {
            if (step > 0) {
                --step;
                set_err("");
                sync_wizard_ui();
            }
            continue;
        }
        if (resp != WIZ_RESP_NEXT) continue;

        set_err("");
        if (step == 0) {
            GtkTreeSelection* psel = gtk_tree_view_get_selection(GTK_TREE_VIEW(ptree));
            GtkTreeModel* pm = nullptr;
            GtkTreeIter pit;
            if (gtk_tree_selection_get_selected(psel, &pm, &pit)) {
                gtk_tree_model_get(pm, &pit, 2, &drv_idx, -1);
            }
            if (drv_idx < 0 || drv_idx >= 5) drv_idx = 0;
            if (drv_idx != last_drv) {
                std::string auto_id = node::derive_default_iface_id(*ctx->ifaces, GTK_IFACE_DRIVERS[drv_idx].type);
                gtk_entry_set_text(GTK_ENTRY(e_id), auto_id.c_str());
                gtk_entry_set_text(GTK_ENTRY(e_desc), "");
                gtk_entry_set_text(GTK_ENTRY(e_host), GTK_IFACE_DRIVERS[drv_idx].def_host);
                gtk_entry_set_text(GTK_ENTRY(e_port), GTK_IFACE_DRIVERS[drv_idx].def_port);
                gtk_entry_set_text(GTK_ENTRY(e_kport), "0");
                gtk_entry_set_text(GTK_ENTRY(e_netmask), "");
                gtk_entry_set_text(GTK_ENTRY(e_peer), "");
                gtk_combo_box_set_active(GTK_COMBO_BOX(cb_mode), 0);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_in), TRUE);
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_out),
                                             (GTK_IFACE_DRIVERS[drv_idx].type != std::string("telnet")) ? TRUE : FALSE);
                gtk_combo_box_set_active(GTK_COMBO_BOX(cb_lpol), 0);
                last_drv = drv_idx;
            }
            step = 1;
            sync_wizard_ui();
        } else if (step == 1) {
            std::string id = gtk_entry_get_text(GTK_ENTRY(e_id));
            if (id.empty()) {
                set_err("Interface ID cannot be empty.");
                continue;
            }
            bool dup = false;
            for (std::size_t i = 0; i < ctx->ifaces->size(); ++i) {
                if (gtk_iface_ci_eq((*ctx->ifaces)[i].id, id)) { dup = true; break; }
            }
            if (dup) {
                set_err("An interface with ID '" + id + "' already exists.");
                continue;
            }
            step = 2;
            sync_wizard_ui();
        } else if (step == 2) {
            std::string h_s = gtk_entry_get_text(GTK_ENTRY(e_host));
            if (h_s.empty()) {
                set_err("Host / Serial Device cannot be empty.");
                continue;
            }
            const std::string cur_t = GTK_IFACE_DRIVERS[drv_idx].type;
            if ((cur_t == "axudp" || cur_t == "axip") && radio::is_all_ascii_digits(h_s)) {
                uint32_t did = 0;
                if (!radio::parse_dmr_id(h_s, did)) {
                    set_err("Invalid DMR ID (allowed 1..16777214).");
                    continue;
                }
            }
            std::string nm_s = gtk_entry_get_text(GTK_ENTRY(e_netmask));
            if ((cur_t == "axudp" || cur_t == "axip") && !nm_s.empty() && !gtk_is_valid_ipv4(nm_s)) {
                set_err("Invalid IPv4 netmask (e.g. 255.0.0.0).");
                continue;
            }
            step = 3;
            sync_wizard_ui();
        } else {
            gchar* m_raw = gtk_combo_box_get_active_text(GTK_COMBO_BOX(cb_mode));
            std::string mode = m_raw ? m_raw : "client";
            if (m_raw) g_free(m_raw);
            gchar* lp_raw = gtk_combo_box_get_active_text(GTK_COMBO_BOX(cb_lpol));
            std::string lpol = lp_raw ? lp_raw : "inherit";
            if (lp_raw) g_free(lp_raw);
            radio::RadioInterfaceConfig ic = gtk_build_iface_config(
                drv_idx,
                gtk_entry_get_text(GTK_ENTRY(e_id)),
                gtk_entry_get_text(GTK_ENTRY(e_desc)),
                gtk_entry_get_text(GTK_ENTRY(e_host)),
                gtk_entry_get_text(GTK_ENTRY(e_port)),
                mode,
                gtk_entry_get_text(GTK_ENTRY(e_kport)),
                gtk_entry_get_text(GTK_ENTRY(e_netmask)),
                gtk_entry_get_text(GTK_ENTRY(e_peer)),
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_in)) != FALSE,
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_out)) != FALSE,
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_same)) != FALSE,
                lpol,
                nullptr);
            ctx->ifaces->push_back(std::move(ic));
            repopulate_gtk_ifaces(ctx);
            break;
        }
    }
    gtk_widget_destroy(dlg);
}

struct GtkEditIfaceWidgets {
    GtkWidget* cb_drv = nullptr;
    GtkWidget* l_mode = nullptr;
    GtkWidget* cb_mode = nullptr;
    GtkWidget* l_host = nullptr;
    GtkWidget* e_host = nullptr;
    GtkWidget* l_port = nullptr;
    GtkWidget* e_port = nullptr;
    GtkWidget* l_netmask = nullptr;
    GtkWidget* e_netmask = nullptr;
    GtkWidget* l_kport = nullptr;
    GtkWidget* e_kport = nullptr;
    GtkWidget* l_peer = nullptr;
    GtkWidget* e_peer = nullptr;
    GtkWidget* chk_in = nullptr;
    GtkWidget* chk_out = nullptr;
    GtkWidget* chk_same = nullptr;
    GtkWidget* l_lpol = nullptr;
    GtkWidget* cb_lpol = nullptr;
    int drv_idx = 0;
    bool apply_defaults_on_change = false;
};

void sync_gtk_edit_iface_fields(GtkEditIfaceWidgets* w) {
    int d = gtk_combo_box_get_active(GTK_COMBO_BOX(w->cb_drv));
    if (d < 0 || d >= 5) d = 0;
    const bool drv_changed = (d != w->drv_idx);
    w->drv_idx = d;
    const std::string t = GTK_IFACE_DRIVERS[d].type;
    if (drv_changed && w->apply_defaults_on_change) {
        gtk_entry_set_text(GTK_ENTRY(w->e_host), GTK_IFACE_DRIVERS[d].def_host);
        gtk_entry_set_text(GTK_ENTRY(w->e_port), GTK_IFACE_DRIVERS[d].def_port);
        gtk_entry_set_text(GTK_ENTRY(w->e_netmask), "");
        gtk_entry_set_text(GTK_ENTRY(w->e_kport), "0");
        gtk_entry_set_text(GTK_ENTRY(w->e_peer), "");
        gtk_combo_box_set_active(GTK_COMBO_BOX(w->cb_mode), 0);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->chk_out), (t != "telnet") ? TRUE : FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->chk_same), FALSE);
    }

    if (t == "kiss-serial") {
        gtk_label_set_text(GTK_LABEL(w->l_host), "Serial Device:");
        gtk_label_set_text(GTK_LABEL(w->l_port), "Baud Rate:");
    } else if (t == "kiss-tcp") {
        gtk_label_set_text(GTK_LABEL(w->l_host), "Host:");
        gtk_label_set_text(GTK_LABEL(w->l_port), "TCP Port:");
    } else if (t == "telnet") {
        gtk_label_set_text(GTK_LABEL(w->l_host), "Bind Host:");
        gtk_label_set_text(GTK_LABEL(w->l_port), "TCP Port:");
    } else if (t == "axip") {
        gtk_label_set_text(GTK_LABEL(w->l_host), "Bind Host / DMR:");
    } else {
        gtk_label_set_text(GTK_LABEL(w->l_host), "Bind Host / DMR:");
        gtk_label_set_text(GTK_LABEL(w->l_port), "UDP Port:");
    }

    const bool is_ktcp = (t == "kiss-tcp");
    const bool is_kser = (t == "kiss-serial");
    const bool is_tel  = (t == "telnet");
    const bool has_port = (t != "axip");
    const bool has_peer = (t == "axudp" || t == "axip");
    gtk_widget_set_visible(w->l_mode, is_ktcp);
    gtk_widget_set_visible(w->cb_mode, is_ktcp);
    gtk_widget_set_visible(w->l_port, has_port);
    gtk_widget_set_visible(w->e_port, has_port);
    gtk_widget_set_visible(w->l_netmask, has_peer);
    gtk_widget_set_visible(w->e_netmask, has_peer);
    gtk_widget_set_visible(w->l_kport, is_kser || is_ktcp);
    gtk_widget_set_visible(w->e_kport, is_kser || is_ktcp);
    gtk_widget_set_visible(w->l_peer, has_peer);
    gtk_widget_set_visible(w->e_peer, has_peer);
    gtk_widget_set_visible(w->l_lpol, !is_tel);
    gtk_widget_set_visible(w->cb_lpol, !is_tel);
    if (is_tel) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->chk_out), FALSE);
        gtk_widget_set_sensitive(w->chk_out, FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->chk_same), FALSE);
        gtk_widget_set_sensitive(w->chk_same, FALSE);
    } else {
        gtk_widget_set_sensitive(w->chk_out, TRUE);
        gtk_widget_set_sensitive(w->chk_same, TRUE);
    }
}

void run_gtk_edit_iface_dialog(GtkSettingsCtx* ctx, int edit_idx) {
    if (edit_idx < 0 || static_cast<std::size_t>(edit_idx) >= ctx->ifaces->size()) return;

    const radio::RadioInterfaceConfig& src = (*ctx->ifaces)[static_cast<std::size_t>(edit_idx)];
    auto get_p = [&](const char* k, const char* fallback) -> std::string {
        auto it = src.params.find(k);
        return (it != src.params.end() && !it->second.empty()) ? it->second : fallback;
    };

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Edit Interface",
        GTK_WINDOW(ctx->dlg),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT,
        static_cast<const char*>(nullptr));
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 470, 420);

    GtkWidget* outer = gtk_vbox_new(FALSE, 8);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 10);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), outer, TRUE, TRUE, 0);

    GtkWidget* lbl_prompt = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(lbl_prompt), "<b>Edit interface settings:</b>");
    gtk_misc_set_alignment(GTK_MISC(lbl_prompt), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(outer), lbl_prompt, FALSE, FALSE, 0);

    GtkWidget* tbl = gtk_table_new(13, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(tbl), 6);
    gtk_table_set_col_spacings(GTK_TABLE(tbl), 10);
    gtk_box_pack_start(GTK_BOX(outer), tbl, TRUE, TRUE, 0);

    GtkEditIfaceWidgets ew;
    ew.cb_drv = gtk_combo_box_new_text();
    for (int i = 0; i < 5; ++i) {
        gtk_combo_box_append_text(GTK_COMBO_BOX(ew.cb_drv), GTK_IFACE_DRIVERS[i].type);
        if (gtk_iface_ci_eq(src.type, GTK_IFACE_DRIVERS[i].type)) ew.drv_idx = i;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(ew.cb_drv), ew.drv_idx);

    GtkWidget* e_id = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_id), src.id.c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(e_id), TRUE);

    GtkWidget* e_desc = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_desc), src.description.c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(e_desc), TRUE);

    ew.l_mode = gtk_label_new("KISS-TCP Mode:");
    gtk_misc_set_alignment(GTK_MISC(ew.l_mode), 0.0f, 0.5f);
    ew.cb_mode = gtk_combo_box_new_text();
    gtk_combo_box_append_text(GTK_COMBO_BOX(ew.cb_mode), "client");
    gtk_combo_box_append_text(GTK_COMBO_BOX(ew.cb_mode), "server");
    gtk_combo_box_set_active(GTK_COMBO_BOX(ew.cb_mode), 0);

    ew.l_host = gtk_label_new("Host / Device:");
    gtk_misc_set_alignment(GTK_MISC(ew.l_host), 0.0f, 0.5f);
    ew.e_host = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(ew.e_host), TRUE);

    ew.l_port = gtk_label_new("Port / Baud:");
    gtk_misc_set_alignment(GTK_MISC(ew.l_port), 0.0f, 0.5f);
    ew.e_port = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(ew.e_port), TRUE);

    ew.l_netmask = gtk_label_new("Netmask (opt):");
    gtk_misc_set_alignment(GTK_MISC(ew.l_netmask), 0.0f, 0.5f);
    ew.e_netmask = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(ew.e_netmask), TRUE);

    ew.l_kport = gtk_label_new("KISS Port (0-15):");
    gtk_misc_set_alignment(GTK_MISC(ew.l_kport), 0.0f, 0.5f);
    ew.e_kport = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(ew.e_kport), "0");
    gtk_entry_set_activates_default(GTK_ENTRY(ew.e_kport), TRUE);

    ew.l_peer = gtk_label_new("Static Peer (opt):");
    gtk_misc_set_alignment(GTK_MISC(ew.l_peer), 0.0f, 0.5f);
    ew.e_peer = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(ew.e_peer), TRUE);

    ew.chk_in = gtk_check_button_new_with_label("Allow inbound connections");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ew.chk_in), src.allow_inbound ? TRUE : FALSE);

    ew.chk_out = gtk_check_button_new_with_label("Allow outbound connections");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ew.chk_out),
                                 (ew.drv_idx == 4) ? FALSE : (src.allow_outbound ? TRUE : FALSE));

    ew.chk_same = gtk_check_button_new_with_label("Allow same-interface forwarding");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ew.chk_same),
                                 (ew.drv_idx == 4) ? FALSE : (src.allow_same_interface ? TRUE : FALSE));

    ew.l_lpol = gtk_label_new("Learned Peer Policy:");
    gtk_misc_set_alignment(GTK_MISC(ew.l_lpol), 0.0f, 0.5f);
    ew.cb_lpol = gtk_combo_box_new_text();
    const std::string cur_lpol = get_p("learned_peer_policy", "inherit");
    int lp_sel = 0;
    const char* lp_opts[5] = {"inherit", "none", "inbound", "outbound", "both"};
    for (int i = 0; i < 5; ++i) {
        gtk_combo_box_append_text(GTK_COMBO_BOX(ew.cb_lpol), lp_opts[i]);
        if (cur_lpol == lp_opts[i]) lp_sel = i;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(ew.cb_lpol), lp_sel);

    if (ew.drv_idx == 2) {
        gtk_entry_set_text(GTK_ENTRY(ew.e_host), get_p("device", "/dev/ttyUSB0").c_str());
        gtk_entry_set_text(GTK_ENTRY(ew.e_port), get_p("baud", "9600").c_str());
        gtk_entry_set_text(GTK_ENTRY(ew.e_kport), get_p("kiss_port", "0").c_str());
    } else if (ew.drv_idx == 3) {
        std::string m = get_p("mode", "client");
        gtk_combo_box_set_active(GTK_COMBO_BOX(ew.cb_mode), (m == "server") ? 1 : 0);
        gtk_entry_set_text(GTK_ENTRY(ew.e_host), get_p("host", "127.0.0.1").c_str());
        gtk_entry_set_text(GTK_ENTRY(ew.e_port), get_p("port", "8001").c_str());
        gtk_entry_set_text(GTK_ENTRY(ew.e_kport), get_p("kiss_port", "0").c_str());
    } else {
        gtk_entry_set_text(GTK_ENTRY(ew.e_host), get_p("bind_host", "0.0.0.0").c_str());
        if (ew.drv_idx == 0) gtk_entry_set_text(GTK_ENTRY(ew.e_port), get_p("bind_port", "10093").c_str());
        else if (ew.drv_idx == 4) gtk_entry_set_text(GTK_ENTRY(ew.e_port), get_p("bind_port", "8010").c_str());
        else gtk_entry_set_text(GTK_ENTRY(ew.e_port), "0");
        gtk_entry_set_text(GTK_ENTRY(ew.e_netmask), get_p("netmask", "").c_str());
        if (!src.peers.empty() && (ew.drv_idx == 0 || ew.drv_idx == 1)) {
            gtk_entry_set_text(GTK_ENTRY(ew.e_peer), src.peers[0].c_str());
        }
    }

    auto attach_w = [&](int row, GtkWidget* l, GtkWidget* w) {
        gtk_misc_set_alignment(GTK_MISC(l), 0.0f, 0.5f);
        gtk_table_attach(GTK_TABLE(tbl), l, 0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
        gtk_table_attach(GTK_TABLE(tbl), w, 1, 2, row, row + 1,
                         static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);
    };
    attach_w(0, gtk_label_new("Driver:"), ew.cb_drv);
    attach_w(1, gtk_label_new("Interface ID:"), e_id);
    attach_w(2, gtk_label_new("Description (opt):"), e_desc);
    attach_w(3, ew.l_mode, ew.cb_mode);
    attach_w(4, ew.l_host, ew.e_host);
    attach_w(5, ew.l_port, ew.e_port);
    attach_w(6, ew.l_netmask, ew.e_netmask);
    attach_w(7, ew.l_kport, ew.e_kport);
    attach_w(8, ew.l_peer, ew.e_peer);
    gtk_table_attach(GTK_TABLE(tbl), ew.chk_in, 0, 2, 9, 10, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(tbl), ew.chk_out, 0, 2, 10, 11, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(tbl), ew.chk_same, 0, 2, 11, 12, GTK_FILL, GTK_FILL, 0, 0);
    attach_w(12, ew.l_lpol, ew.cb_lpol);

    GtkWidget* lbl_err = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(lbl_err), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(outer), lbl_err, FALSE, FALSE, 0);

    gtk_widget_show_all(dlg);
    gtk_widget_hide(lbl_err);
    sync_gtk_edit_iface_fields(&ew);
    ew.apply_defaults_on_change = true;
    g_signal_connect(ew.cb_drv, "changed",
                     G_CALLBACK(+[](GtkComboBox*, gpointer d) {
                         sync_gtk_edit_iface_fields(static_cast<GtkEditIfaceWidgets*>(d));
                     }),
                     &ew);

    GtkSimpleCapCtx cap_ctx{dlg, "edit_iface", GTK_RESPONSE_CANCEL, 0};
    if (g_gtk_test.enabled) g_timeout_add(160, on_gtk_simple_dlg_cap, &cap_ctx);
    while (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        std::string id = gtk_entry_get_text(GTK_ENTRY(e_id));
        if (id.empty()) {
            gtk_label_set_markup(GTK_LABEL(lbl_err), "<span foreground=\"#cc0000\"><b>Interface ID cannot be empty.</b></span>");
            gtk_widget_show(lbl_err);
            continue;
        }
        bool dup = false;
        for (std::size_t i = 0; i < ctx->ifaces->size(); ++i) {
            if (i == static_cast<std::size_t>(edit_idx)) continue;
            if (gtk_iface_ci_eq((*ctx->ifaces)[i].id, id)) { dup = true; break; }
        }
        if (dup) {
            std::string mk = "<span foreground=\"#cc0000\"><b>An interface with ID '" + escape_markup(id) + "' already exists.</b></span>";
            gtk_label_set_markup(GTK_LABEL(lbl_err), mk.c_str());
            gtk_widget_show(lbl_err);
            continue;
        }
        std::string h_s = gtk_entry_get_text(GTK_ENTRY(ew.e_host));
        if (h_s.empty()) {
            gtk_label_set_markup(GTK_LABEL(lbl_err), "<span foreground=\"#cc0000\"><b>Host / Serial Device cannot be empty.</b></span>");
            gtk_widget_show(lbl_err);
            continue;
        }
        const std::string cur_t = GTK_IFACE_DRIVERS[ew.drv_idx].type;
        if ((cur_t == "axudp" || cur_t == "axip") && radio::is_all_ascii_digits(h_s)) {
            uint32_t did = 0;
            if (!radio::parse_dmr_id(h_s, did)) {
                gtk_label_set_markup(GTK_LABEL(lbl_err), "<span foreground=\"#cc0000\"><b>Invalid DMR ID (allowed 1..16777214).</b></span>");
                gtk_widget_show(lbl_err);
                continue;
            }
        }
        std::string nm_s = gtk_entry_get_text(GTK_ENTRY(ew.e_netmask));
        if ((cur_t == "axudp" || cur_t == "axip") && !nm_s.empty() && !gtk_is_valid_ipv4(nm_s)) {
            gtk_label_set_markup(GTK_LABEL(lbl_err), "<span foreground=\"#cc0000\"><b>Invalid IPv4 netmask (e.g. 255.0.0.0).</b></span>");
            gtk_widget_show(lbl_err);
            continue;
        }
        gchar* m_raw = gtk_combo_box_get_active_text(GTK_COMBO_BOX(ew.cb_mode));
        std::string mode = m_raw ? m_raw : "client";
        if (m_raw) g_free(m_raw);
        gchar* lp_raw = gtk_combo_box_get_active_text(GTK_COMBO_BOX(ew.cb_lpol));
        std::string lpol = lp_raw ? lp_raw : "inherit";
        if (lp_raw) g_free(lp_raw);
        radio::RadioInterfaceConfig ic = gtk_build_iface_config(
            ew.drv_idx, id,
            gtk_entry_get_text(GTK_ENTRY(e_desc)),
            h_s,
            gtk_entry_get_text(GTK_ENTRY(ew.e_port)),
            mode,
            gtk_entry_get_text(GTK_ENTRY(ew.e_kport)),
            nm_s,
            gtk_entry_get_text(GTK_ENTRY(ew.e_peer)),
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ew.chk_in)) != FALSE,
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ew.chk_out)) != FALSE,
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ew.chk_same)) != FALSE,
            lpol,
            &src);
        (*ctx->ifaces)[static_cast<std::size_t>(edit_idx)] = std::move(ic);
        repopulate_gtk_ifaces(ctx);
        break;
    }
    gtk_widget_destroy(dlg);
}

void on_settings_add_iface_clicked(GtkWidget*, gpointer user_data) {
    run_gtk_add_iface_wizard(static_cast<GtkSettingsCtx*>(user_data));
}

void on_settings_edit_iface_clicked(GtkWidget*, gpointer user_data) {
    auto* ctx = static_cast<GtkSettingsCtx*>(user_data);
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(ctx->if_tree));
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;
    gint idx = -1;
    gtk_tree_model_get(model, &iter, 4, &idx, -1);
    if (idx >= 0 && static_cast<std::size_t>(idx) < ctx->ifaces->size()) {
        run_gtk_edit_iface_dialog(ctx, idx);
    }
}

void on_settings_if_row_activated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer user_data) {
    on_settings_edit_iface_clicked(nullptr, user_data);
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

void gtk_apply_inline_hint_state(GtkWidget* entry, bool show_hint) {
    GdkColor dim_col{};
    gdk_color_parse("#808080", &dim_col);
    if (show_hint) {
        g_object_set_data(G_OBJECT(entry), "oyk_hint", GINT_TO_POINTER(1));
        gtk_entry_set_text(GTK_ENTRY(entry), "<inline>");
        gtk_widget_modify_text(entry, GTK_STATE_NORMAL, &dim_col);
    } else {
        g_object_set_data(G_OBJECT(entry), "oyk_hint", GINT_TO_POINTER(0));
        gtk_widget_modify_text(entry, GTK_STATE_NORMAL, nullptr);
    }
}

void gtk_setup_inline_hint_entry(GtkWidget* entry, const std::string& initial) {
    if (initial.empty()) {
        gtk_apply_inline_hint_state(entry, true);
    } else {
        gtk_entry_set_text(GTK_ENTRY(entry), initial.c_str());
        gtk_apply_inline_hint_state(entry, false);
    }
    g_signal_connect(entry, "focus-in-event",
                     G_CALLBACK(+[](GtkWidget* w, GdkEventFocus*, gpointer) -> gboolean {
                         if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "oyk_hint")) == 1) {
                             gtk_entry_set_text(GTK_ENTRY(w), "");
                             gtk_apply_inline_hint_state(w, false);
                         }
                         return FALSE;
                     }),
                     nullptr);
    g_signal_connect(entry, "focus-out-event",
                     G_CALLBACK(+[](GtkWidget* w, GdkEventFocus*, gpointer) -> gboolean {
                         const char* txt = gtk_entry_get_text(GTK_ENTRY(w));
                         if (!txt || txt[0] == '\0') {
                             gtk_apply_inline_hint_state(w, true);
                         }
                         return FALSE;
                     }),
                     nullptr);
}

std::string gtk_inline_hint_entry_value(GtkWidget* entry) {
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "oyk_hint")) == 1) {
        return {};
    }
    return gtk_entry_get_text(GTK_ENTRY(entry));
}

void apply_gtk_theme_for(GtkAppState* st,
                         const std::string& theme_name,
                         const std::vector<ThemeConfig>& custom_themes,
                         GtkWidget* extra_win);
void apply_gtk_theme(GtkAppState* st);
bool gtk_os_prefers_dark();
ThemeConfig resolve_gtk_palette(GtkAppState* st,
                                const std::string& name,
                                const std::vector<ThemeConfig>& custom_themes,
                                bool os_dark);
GdkColor gdk_from_rgb(uint32_t rgb);

void gtk_attach_esc_close(GtkWidget* dlg, gint response_id) {
    auto esc_cb = +[](GtkWidget* d, GdkEventKey* kev, gpointer rp) -> gboolean {
        if (kev && !kev->is_modifier && kev->keyval == GDK_Escape) {
            gtk_dialog_response(GTK_DIALOG(d), static_cast<gint>(GPOINTER_TO_INT(rp)));
            return TRUE;
        }
        return FALSE;
    };
    g_signal_connect(dlg, "key-press-event", G_CALLBACK(esc_cb), GINT_TO_POINTER(response_id));
}

void open_settings_dialog(GtkAppState* st, int initial_page) {
    if (st && st->modal_open > 0) return;
    struct ModalGuard {
        GtkAppState* app;
        explicit ModalGuard(GtkAppState* a) : app(a) { if (app) ++app->modal_open; }
        ~ModalGuard() { if (app && app->modal_open > 0) --app->modal_open; }
    } modal_guard(st);
    node::NodeConfig draft = st->engine->config();
    std::string active_theme = normalized_port_theme_name(draft.theme_name, true);
    const bool os_dark = gtk_os_prefers_dark();

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        oyk_gui_screen_title(OYK_SCR_SETTINGS),
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        "Save & Apply", GTK_RESPONSE_ACCEPT,
        static_cast<const char*>(nullptr));
    gtk_window_set_default_size(GTK_WINDOW(dlg), 620, 460);
    gtk_attach_esc_close(dlg, GTK_RESPONSE_CANCEL);

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
    GtkWidget* t_node = make_table(10);
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

    GtkWidget* cb_learn_pol = gtk_combo_box_new_text();
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_learn_pol), "none");
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_learn_pol), "inbound");
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_learn_pol), "outbound");
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_learn_pol), "both");
    int lp_idx = 1;
    if (draft.learned_peer_policy == radio::LearnedPeerPolicy::None) lp_idx = 0;
    else if (draft.learned_peer_policy == radio::LearnedPeerPolicy::Outbound) lp_idx = 2;
    else if (draft.learned_peer_policy == radio::LearnedPeerPolicy::Both) lp_idx = 3;
    gtk_combo_box_set_active(GTK_COMBO_BOX(cb_learn_pol), lp_idx);
    attach_row(t_node, 4, "Learned Peer Policy:", cb_learn_pol);

    GtkWidget* e_new_welc = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_new_welc), draft.new_user_welcome.c_str());
    attach_row(t_node, 5, "New User Welcome:", e_new_welc);

    GtkWidget* e_if_file = gtk_entry_new();
    gtk_setup_inline_hint_entry(e_if_file, draft.interfaces_file);
    attach_row(t_node, 6, "Interfaces File:", e_if_file);

    GtkWidget* e_pr_file = gtk_entry_new();
    gtk_setup_inline_hint_entry(e_pr_file, draft.peers_file);
    attach_row(t_node, 7, "Peers File:", e_pr_file);

    GtkWidget* e_th_file = gtk_entry_new();
    gtk_setup_inline_hint_entry(e_th_file, draft.themes_file);
    attach_row(t_node, 8, "Themes File:", e_th_file);

    GtkWidget* chk_accept = gtk_check_button_new_with_label("Allow incoming connections by default");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_accept), draft.accept_incoming_default ? TRUE : FALSE);
    gtk_table_attach(GTK_TABLE(t_node), chk_accept, 0, 2, 9, 10, GTK_FILL, GTK_FILL, 0, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), t_node, gtk_label_new(oyk_gui_settings_tab_title(0)));

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
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), t_bbs, gtk_label_new(oyk_gui_settings_tab_title(1)));

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
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), t_chat, gtk_label_new(oyk_gui_settings_tab_title(2)));

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
    GtkWidget* btn_edit_if = gtk_button_new_with_label("Edit Selected...");
    GtkWidget* btn_rem_if = gtk_button_new_with_label("Remove Selected");
    gtk_box_pack_start(GTK_BOX(if_btns), btn_add_if, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(if_btns), btn_edit_if, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(if_btns), btn_rem_if, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(if_vbox), if_btns, FALSE, FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), if_vbox, gtk_label_new(oyk_gui_settings_tab_title(3)));

    // Tab 5: Peers
    GtkWidget* peer_vbox = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(peer_vbox), 8);
    GtkListStore* peer_store = gtk_list_store_new(7, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                                  G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    GtkWidget* peer_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(peer_store));
    add_text_column(GTK_TREE_VIEW(peer_tree), "ID", 0, 80);
    add_text_column(GTK_TREE_VIEW(peer_tree), "Callsign", 1, 80);
    add_text_column(GTK_TREE_VIEW(peer_tree), "Iface", 2, 70);
    add_text_column(GTK_TREE_VIEW(peer_tree), "Host / Port", 3, 140);
    add_text_column(GTK_TREE_VIEW(peer_tree), "ACL", 4, 65);
    add_text_column(GTK_TREE_VIEW(peer_tree), "Tag", 5, 55);
    GtkWidget* peer_sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(peer_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(peer_sc), peer_tree);
    gtk_box_pack_start(GTK_BOX(peer_vbox), peer_sc, TRUE, TRUE, 0);

    GtkWidget* peer_btns = gtk_hbox_new(FALSE, 6);
    GtkWidget* btn_add_peer = gtk_button_new_with_label("Add Peer...");
    GtkWidget* btn_edit_peer = gtk_button_new_with_label("Edit Selected...");
    GtkWidget* btn_rem_peer = gtk_button_new_with_label("Remove Selected");
    GtkWidget* btn_keep_peer = gtk_button_new_with_label("Keep Auto");
    gtk_box_pack_start(GTK_BOX(peer_btns), btn_add_peer, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(peer_btns), btn_edit_peer, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(peer_btns), btn_rem_peer, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(peer_btns), btn_keep_peer, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(peer_vbox), peer_btns, FALSE, FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), peer_vbox, gtk_label_new(oyk_gui_settings_tab_title(4)));

    // Tab 6: Rig Control
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
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), t_rig, gtk_label_new(oyk_gui_settings_tab_title(5)));

    // Tab 6: Line Printer
    GtkWidget* t_prn = make_table(6);
    GtkWidget* chk_prn = gtk_check_button_new_with_label("Enable Line-by-Line Printer Logging");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_prn), draft.logging.printer.enabled ? TRUE : FALSE);
    gtk_table_attach(GTK_TABLE(t_prn), chk_prn, 0, 2, 0, 1, GTK_FILL, GTK_FILL, 0, 0);

    GtkWidget* e_prn_dev = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_prn_dev), draft.logging.printer.device.c_str());
    attach_row(t_prn, 1, "Device (/dev/parport0, /dev/ttyS0):", e_prn_dev);

    GtkWidget* cb_prn_proto = gtk_combo_box_new_text();
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_prn_proto), "generic");
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_prn_proto), "mx80");
    gtk_combo_box_append_text(GTK_COMBO_BOX(cb_prn_proto), "escp2");
    int prn_pidx = 0;
    if (draft.logging.printer.protocol == "mx80") prn_pidx = 1;
    else if (draft.logging.printer.protocol == "escp2") prn_pidx = 2;
    gtk_combo_box_set_active(GTK_COMBO_BOX(cb_prn_proto), prn_pidx);
    attach_row(t_prn, 2, "Protocol:", cb_prn_proto);

    GtkWidget* e_prn_baud = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_prn_baud), std::to_string(draft.logging.printer.baud).c_str());
    attach_row(t_prn, 3, "Serial Baud Rate:", e_prn_baud);

    GtkWidget* e_prn_lvl = gtk_entry_new();
    {
        std::ostringstream loss;
        loss << "0x" << std::hex << draft.logging.printer.level;
        gtk_entry_set_text(GTK_ENTRY(e_prn_lvl), loss.str().c_str());
    }
    attach_row(t_prn, 4, "Log Mask (hex / symbols):", e_prn_lvl);

    GtkWidget* chk_prn_cond = gtk_check_button_new_with_label("Use 132-column condensed mode (MX-80 / ESC/P2)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_prn_cond), draft.logging.printer.condensed ? TRUE : FALSE);
    gtk_table_attach(GTK_TABLE(t_prn), chk_prn_cond, 0, 2, 5, 6, GTK_FILL, GTK_FILL, 0, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), t_prn, gtk_label_new(oyk_gui_settings_tab_title(6)));

    // Tab 8: Theme & Color Palette (live preview across app & modal)
    GtkWidget* th_vbox = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(th_vbox), 8);
    GtkWidget* th_top = gtk_hbox_new(FALSE, 8);
    GtkWidget* cb_theme = gtk_combo_box_new_text();
    GtkWidget* btn_th_del = gtk_button_new_with_label("Delete Custom");
    gtk_box_pack_start(GTK_BOX(th_top), gtk_label_new("Theme:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(th_top), cb_theme, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(th_top), btn_th_del, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(th_vbox), th_top, FALSE, FALSE, 0);

    GtkWidget* th_hint = gtk_label_new("Built-in dark/light/native are never saved; editing clones custom-dark or custom-light.");
    gtk_misc_set_alignment(GTK_MISC(th_hint), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(th_vbox), th_hint, FALSE, FALSE, 0);

    GtkListStore* th_store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* th_tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(th_store));
    add_text_column(GTK_TREE_VIEW(th_tv), oyk_gui_table_column(OYK_TBL_THEME, 0), 0, 145);
    add_text_column(GTK_TREE_VIEW(th_tv), oyk_gui_table_column(OYK_TBL_THEME, 1), 1, 115);
    add_text_column(GTK_TREE_VIEW(th_tv), "#RRGGBB", 2, 95);
    {
        GtkCellRenderer* rend = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(
            oyk_gui_table_column(OYK_TBL_THEME, 3), rend, "cell-background", 3, nullptr);
        gtk_tree_view_column_set_min_width(col, 80);
        gtk_tree_view_append_column(GTK_TREE_VIEW(th_tv), col);
    }
    GtkWidget* th_sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(th_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(th_sc), th_tv);
    gtk_box_pack_start(GTK_BOX(th_vbox), th_sc, TRUE, TRUE, 2);

    GtkWidget* th_ed = gtk_hbox_new(FALSE, 6);
    GtkWidget* e_th_hex = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(e_th_hex), 10);
    GtkWidget* btn_th_pick = gtk_button_new_with_label("Pick Color...");
    GtkWidget* btn_th_apply = gtk_button_new_with_label("Apply Color");
    gtk_box_pack_start(GTK_BOX(th_ed), gtk_label_new("Color:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(th_ed), e_th_hex, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(th_ed), btn_th_pick, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(th_ed), btn_th_apply, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(th_vbox), th_ed, FALSE, FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), th_vbox, gtk_label_new(oyk_gui_settings_tab_title(7)));

    struct GtkThemeTabCtx {
        GtkAppState* st;
        node::NodeConfig* draft;
        std::string* active;
        bool os_dark;
        GtkWidget* cb;
        GtkWidget* btn_del;
        GtkListStore* store;
        GtkWidget* tv;
        GtkWidget* e_hex;
        GtkWidget* btn_apply;
        GtkWidget* dlg;
        std::vector<std::string> names;
        bool populating;
    } th_ctx{st, &draft, &active_theme, os_dark, cb_theme, btn_th_del, th_store, th_tv, e_th_hex, btn_th_apply, dlg, {}, false};

    auto refresh_th_rows = [&](GtkThemeTabCtx* c) {
        const ThemeConfig pal = resolve_gtk_palette(c->st, *c->active, c->draft->themes, c->os_dark);
        gtk_list_store_clear(c->store);
        for (std::size_t i = 0; i < kThemeRoleCount; ++i) {
            const std::string hx = format_hex_rgb(pal.colors[i]);
            GtkTreeIter it;
            gtk_list_store_append(c->store, &it);
            gtk_list_store_set(c->store, &it,
                               0, kThemeRoleLabels[i],
                               1, kThemeRoleKeys[i],
                               2, hx.c_str(),
                               3, hx.c_str(),
                               -1);
        }
        gtk_widget_set_sensitive(c->btn_del, !is_builtin_theme_name(*c->active) ? TRUE : FALSE);
    };

    {
        th_ctx.populating = true;
        th_ctx.names = {"native", "dark", "light"};
        for (const auto& ct : draft.themes) {
            if (!is_builtin_theme_name(ct.name)) th_ctx.names.push_back(ct.name);
        }
        int sel = 0;
        for (int i = 0; i < static_cast<int>(th_ctx.names.size()); ++i) {
            gtk_combo_box_append_text(GTK_COMBO_BOX(cb_theme), th_ctx.names[static_cast<std::size_t>(i)].c_str());
            if (theme_to_lower(th_ctx.names[static_cast<std::size_t>(i)]) == theme_to_lower(active_theme)) sel = i;
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(cb_theme), sel);
        th_ctx.populating = false;
        refresh_th_rows(&th_ctx);
        GtkTreePath* p0 = gtk_tree_path_new_from_indices(0, -1);
        gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(th_tv)), p0);
        gtk_tree_path_free(p0);
        const ThemeConfig pal0 = resolve_gtk_palette(st, active_theme, draft.themes, os_dark);
        gtk_entry_set_text(GTK_ENTRY(e_th_hex), format_hex_rgb(pal0.colors[0]).c_str());
    }

    auto th_combo_cb = +[](GtkComboBox* cb, gpointer p) {
        auto* c = static_cast<GtkThemeTabCtx*>(p);
        if (c->populating) return;
        int idx = gtk_combo_box_get_active(cb);
        if (idx >= 0 && idx < static_cast<int>(c->names.size())) {
            *c->active = c->names[static_cast<std::size_t>(idx)];
            const ThemeConfig pal = resolve_gtk_palette(c->st, *c->active, c->draft->themes, c->os_dark);
            gtk_list_store_clear(c->store);
            for (std::size_t i = 0; i < kThemeRoleCount; ++i) {
                const std::string hx = format_hex_rgb(pal.colors[i]);
                GtkTreeIter it;
                gtk_list_store_append(c->store, &it);
                gtk_list_store_set(c->store, &it, 0, kThemeRoleLabels[i],
                                   1, kThemeRoleKeys[i], 2, hx.c_str(), 3, hx.c_str(), -1);
            }
            gtk_widget_set_sensitive(c->btn_del, !is_builtin_theme_name(*c->active) ? TRUE : FALSE);
            apply_gtk_theme_for(c->st, *c->active, c->draft->themes, c->dlg);
        }
    };
    g_signal_connect(cb_theme, "changed", G_CALLBACK(th_combo_cb), &th_ctx);

    auto th_sel_cb = +[](GtkTreeSelection* sel, gpointer p) {
        auto* c = static_cast<GtkThemeTabCtx*>(p);
        GtkTreeModel* mod = nullptr;
        GtkTreeIter iter;
        if (gtk_tree_selection_get_selected(sel, &mod, &iter)) {
            gchar* hx = nullptr;
            gtk_tree_model_get(mod, &iter, 2, &hx, -1);
            if (hx) { gtk_entry_set_text(GTK_ENTRY(c->e_hex), hx); g_free(hx); }
        }
    };
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(th_tv)), "changed",
                     G_CALLBACK(th_sel_cb), &th_ctx);

    auto th_apply_cb = +[](GtkWidget*, gpointer p) {
        auto* c = static_cast<GtkThemeTabCtx*>(p);
        std::string ht = gtk_entry_get_text(GTK_ENTRY(c->e_hex));
        uint32_t dummy = 0;
        if (!parse_hex_rgb(ht, dummy)) return;
        GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(c->tv));
        GtkTreeModel* mod = nullptr;
        GtkTreeIter iter;
        int role_idx = 0;
        if (gtk_tree_selection_get_selected(sel, &mod, &iter)) {
            GtkTreePath* path = gtk_tree_model_get_path(mod, &iter);
            if (path) { role_idx = gtk_tree_path_get_indices(path)[0]; gtk_tree_path_free(path); }
        }
        if (is_builtin_theme_name(*c->active)) {
            ThemeConfig cloned = resolve_gtk_palette(c->st, *c->active, c->draft->themes, c->os_dark);
            const std::string la = theme_to_lower(*c->active);
            cloned.is_dark_base = (la == "light") ? false : ((la == "native") ? c->os_dark : true);
            cloned.name = make_custom_clone_name(c->draft->themes, cloned.is_dark_base);
            c->draft->themes.push_back(cloned);
            *c->active = cloned.name;
            c->populating = true;
            gtk_combo_box_append_text(GTK_COMBO_BOX(c->cb), cloned.name.c_str());
            c->names.push_back(cloned.name);
            gtk_combo_box_set_active(GTK_COMBO_BOX(c->cb), static_cast<int>(c->names.size()) - 1);
            c->populating = false;
        }
        for (auto& ct : c->draft->themes) {
            if (theme_to_lower(ct.name) == theme_to_lower(*c->active)) {
                ct.colors[static_cast<std::size_t>(role_idx)] = dummy;
                break;
            }
        }
        const ThemeConfig pal = resolve_gtk_palette(c->st, *c->active, c->draft->themes, c->os_dark);
        gtk_list_store_clear(c->store);
        for (std::size_t i = 0; i < kThemeRoleCount; ++i) {
            const std::string nhx = format_hex_rgb(pal.colors[i]);
            GtkTreeIter it;
            gtk_list_store_append(c->store, &it);
            gtk_list_store_set(c->store, &it, 0, kThemeRoleLabels[i],
                               1, kThemeRoleKeys[i], 2, nhx.c_str(), 3, nhx.c_str(), -1);
        }
        gtk_widget_set_sensitive(c->btn_del, !is_builtin_theme_name(*c->active) ? TRUE : FALSE);
        apply_gtk_theme_for(c->st, *c->active, c->draft->themes, c->dlg);
    };
    g_signal_connect(btn_th_apply, "clicked", G_CALLBACK(th_apply_cb), &th_ctx);

    auto th_pick_cb = +[](GtkWidget*, gpointer p) {
        auto* c = static_cast<GtkThemeTabCtx*>(p);
        GtkWidget* cdlg = gtk_color_selection_dialog_new("Pick Theme Color");
        gtk_window_set_transient_for(GTK_WINDOW(cdlg), GTK_WINDOW(c->dlg));
        GtkColorSelection* cs = GTK_COLOR_SELECTION(GTK_COLOR_SELECTION_DIALOG(cdlg)->colorsel);
        uint32_t cur = 0;
        if (parse_hex_rgb(gtk_entry_get_text(GTK_ENTRY(c->e_hex)), cur)) {
            GdkColor gc = gdk_from_rgb(cur);
            gtk_color_selection_set_current_color(cs, &gc);
        }
        if (gtk_dialog_run(GTK_DIALOG(cdlg)) == GTK_RESPONSE_OK) {
            GdkColor gc{};
            gtk_color_selection_get_current_color(cs, &gc);
            uint32_t rgb = ((static_cast<uint32_t>(gc.red >> 8) & 0xFFu) << 16) |
                           ((static_cast<uint32_t>(gc.green >> 8) & 0xFFu) << 8) |
                           (static_cast<uint32_t>(gc.blue >> 8) & 0xFFu);
            gtk_entry_set_text(GTK_ENTRY(c->e_hex), format_hex_rgb(rgb).c_str());
            gtk_button_clicked(GTK_BUTTON(c->btn_apply));
        }
        gtk_widget_destroy(cdlg);
    };
    g_signal_connect(btn_th_pick, "clicked", G_CALLBACK(th_pick_cb), &th_ctx);

    auto th_del_cb = +[](GtkWidget*, gpointer p) {
        auto* c = static_cast<GtkThemeTabCtx*>(p);
        if (is_builtin_theme_name(*c->active)) return;
        const std::string ln = theme_to_lower(*c->active);
        c->draft->themes.erase(
            std::remove_if(c->draft->themes.begin(), c->draft->themes.end(),
                           [&](const ThemeConfig& ct) { return theme_to_lower(ct.name) == ln; }),
            c->draft->themes.end());
        *c->active = "native";
        c->populating = true;
        GtkTreeModel* m = gtk_combo_box_get_model(GTK_COMBO_BOX(c->cb));
        if (m) gtk_list_store_clear(GTK_LIST_STORE(m));
        c->names = {"native", "dark", "light"};
        for (const auto& ct : c->draft->themes) {
            if (!is_builtin_theme_name(ct.name)) c->names.push_back(ct.name);
        }
        for (const auto& nm : c->names) gtk_combo_box_append_text(GTK_COMBO_BOX(c->cb), nm.c_str());
        gtk_combo_box_set_active(GTK_COMBO_BOX(c->cb), 0);
        c->populating = false;
        const ThemeConfig pal = resolve_gtk_palette(c->st, *c->active, c->draft->themes, c->os_dark);
        gtk_list_store_clear(c->store);
        for (std::size_t i = 0; i < kThemeRoleCount; ++i) {
            const std::string nhx = format_hex_rgb(pal.colors[i]);
            GtkTreeIter it;
            gtk_list_store_append(c->store, &it);
            gtk_list_store_set(c->store, &it, 0, kThemeRoleLabels[i],
                               1, kThemeRoleKeys[i], 2, nhx.c_str(), 3, nhx.c_str(), -1);
        }
        gtk_widget_set_sensitive(c->btn_del, FALSE);
        apply_gtk_theme_for(c->st, *c->active, c->draft->themes, c->dlg);
    };
    g_signal_connect(btn_th_del, "clicked", G_CALLBACK(th_del_cb), &th_ctx);

    GtkSettingsCtx sctx;
    sctx.dlg = dlg;
    sctx.if_store = if_store;
    sctx.if_tree = if_tree;
    sctx.ifaces = &draft.interfaces;
    sctx.peer_store = peer_store;
    sctx.peer_tree = peer_tree;
    sctx.peers = &draft.peers;
    repopulate_gtk_ifaces(&sctx);
    repopulate_gtk_peers(&sctx);
    g_signal_connect(btn_add_if, "clicked", G_CALLBACK(on_settings_add_iface_clicked), &sctx);
    g_signal_connect(btn_edit_if, "clicked", G_CALLBACK(on_settings_edit_iface_clicked), &sctx);
    g_signal_connect(btn_rem_if, "clicked", G_CALLBACK(on_settings_rem_iface_clicked), &sctx);
    g_signal_connect(if_tree, "row-activated", G_CALLBACK(on_settings_if_row_activated), &sctx);
    g_signal_connect(btn_add_peer, "clicked", G_CALLBACK(on_settings_add_peer_clicked), &sctx);
    g_signal_connect(btn_edit_peer, "clicked", G_CALLBACK(on_settings_edit_peer_clicked), &sctx);
    g_signal_connect(btn_rem_peer, "clicked", G_CALLBACK(on_settings_rem_peer_clicked), &sctx);
    g_signal_connect(btn_keep_peer, "clicked", G_CALLBACK(on_settings_keep_auto_peers_clicked), &sctx);
    g_signal_connect(peer_tree, "row-activated", G_CALLBACK(on_settings_peer_row_activated), &sctx);

    gtk_widget_show_all(dlg);
    if (initial_page >= 0 && initial_page < oyk_gui_settings_tab_count()) {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), initial_page);
    }

    struct GtkSettingsTestCtx {
        GtkWidget* dlg;
        GtkWidget* nb;
        GtkWidget* btn_add_if;
        GtkWidget* btn_edit_if;
        GtkWidget* if_tree;
        GtkWidget* btn_edit_peer;
        GtkWidget* peer_tree;
        GtkWidget* cb_theme;
        GtkAppState* st;
        int phase;
    } st_test{dlg, nb, btn_add_if, btn_edit_if, if_tree, btn_edit_peer, peer_tree, cb_theme, st, 0};
    if (g_gtk_test.enabled) {
        g_timeout_add(160, +[](gpointer p) -> gboolean {
            auto* c = static_cast<GtkSettingsTestCtx*>(p);
            auto select_first = [](GtkWidget* tv) {
                GtkTreePath* path = gtk_tree_path_new_first();
                gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(tv)), path);
                gtk_tree_path_free(path);
            };
            switch (c->phase++) {
                case 0:
                    gtk_capture_window_png(c->dlg, "settings");
                    gtk_notebook_set_current_page(GTK_NOTEBOOK(c->nb), 1);
                    return TRUE;
                case 1:
                    gtk_capture_window_png(c->dlg, "settings_bbs");
                    gtk_notebook_set_current_page(GTK_NOTEBOOK(c->nb), 2);
                    return TRUE;
                case 2:
                    gtk_capture_window_png(c->dlg, "settings_chat");
                    gtk_notebook_set_current_page(GTK_NOTEBOOK(c->nb), 3);
                    return TRUE;
                case 3:
                    gtk_capture_window_png(c->dlg, "settings_ifaces");
                    gtk_button_clicked(GTK_BUTTON(c->btn_add_if));
                    return TRUE;
                case 4:
                    select_first(c->if_tree);
                    gtk_button_clicked(GTK_BUTTON(c->btn_edit_if));
                    return TRUE;
                case 5:
                    gtk_notebook_set_current_page(GTK_NOTEBOOK(c->nb), 4);
                    select_first(c->peer_tree);
                    return TRUE;
                case 6:
                    gtk_capture_window_png(c->dlg, "peers");
                    select_first(c->peer_tree);
                    gtk_button_clicked(GTK_BUTTON(c->btn_edit_peer));
                    return TRUE;
                case 7:
                    gtk_notebook_set_current_page(GTK_NOTEBOOK(c->nb), 5);
                    return TRUE;
                case 8:
                    gtk_capture_window_png(c->dlg, "settings_rig");
                    gtk_notebook_set_current_page(GTK_NOTEBOOK(c->nb), 6);
                    return TRUE;
                case 9:
                    gtk_capture_window_png(c->dlg, "settings_printer");
                    gtk_notebook_set_current_page(GTK_NOTEBOOK(c->nb), 7);
                    return TRUE;
                case 10:
                    gtk_capture_window_png(c->dlg, "settings_theme");
                    // Preview a different theme and verify Esc reverts cleanly and modal blocks Alt-N
                    if (c->st && c->st->modal_open > 0) {
                        gtk_combo_box_set_active(GTK_COMBO_BOX(c->cb_theme), 1);
                    }
                    {
                        GdkEventKey kev{};
                        kev.type = GDK_KEY_PRESS;
                        kev.window = c->dlg->window;
                        kev.send_event = TRUE;
                        kev.keyval = GDK_Escape;
                        gboolean handled = FALSE;
                        g_signal_emit_by_name(c->dlg, "key-press-event", &kev, &handled);
                    }
                    return FALSE;
                default:
                    gtk_dialog_response(GTK_DIALOG(c->dlg), GTK_RESPONSE_CANCEL);
                    return FALSE;
            }
        }, &st_test);
    }

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
        {
            int lpi = gtk_combo_box_get_active(GTK_COMBO_BOX(cb_learn_pol));
            next.learned_peer_policy =
                (lpi == 0) ? radio::LearnedPeerPolicy::None :
                (lpi == 2) ? radio::LearnedPeerPolicy::Outbound :
                (lpi == 3) ? radio::LearnedPeerPolicy::Both :
                             radio::LearnedPeerPolicy::Inbound;
        }
        next.new_user_welcome = gtk_entry_get_text(GTK_ENTRY(e_new_welc));
        next.interfaces_file = gtk_inline_hint_entry_value(e_if_file);
        next.peers_file = gtk_inline_hint_entry_value(e_pr_file);
        next.themes_file = gtk_inline_hint_entry_value(e_th_file);
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

        next.logging.printer.enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_prn)) != FALSE;
        next.logging.printer.device = gtk_entry_get_text(GTK_ENTRY(e_prn_dev));
        {
            int ppi = gtk_combo_box_get_active(GTK_COMBO_BOX(cb_prn_proto));
            next.logging.printer.protocol = (ppi == 1) ? "mx80" : ((ppi == 2) ? "escp2" : "generic");
        }
        try {
            int pb = std::stoi(gtk_entry_get_text(GTK_ENTRY(e_prn_baud)));
            if (pb < 300) {
                show_err("Printer baud rate must be >= 300.");
                continue;
            }
            next.logging.printer.baud = static_cast<uint32_t>(pb);
        } catch (...) {
            show_err("Invalid printer baud rate.");
            continue;
        }
        {
            uint32_t pmask = 0;
            if (!node::parse_log_mask(gtk_entry_get_text(GTK_ENTRY(e_prn_lvl)), pmask)) {
                show_err("Invalid printer log mask (use e.g. 0x77 or ALL).");
                continue;
            }
            next.logging.printer.level = pmask;
        }
        next.logging.printer.condensed = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_prn_cond)) != FALSE;
        next.theme_name = active_theme;
        next.themes = draft.themes;

        auto res = st->engine->apply_config(next, true);
        if (!res.applied) {
            show_err(res.error);
            continue;
        }
        apply_gtk_theme(st);
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

    apply_gtk_theme(st);
    g_object_unref(if_store);
    g_object_unref(peer_store);
    g_object_unref(th_store);
    gtk_widget_destroy(dlg);
}

void on_settings_clicked(GtkWidget*, gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);
    if (st && st->modal_open > 0) return;
    open_settings_dialog(st, 0);
}

bool gtk_os_prefers_dark() {
    if (GtkSettings* gs = gtk_settings_get_default()) {
        gboolean pref_dark = FALSE;
        gchar* tname = nullptr;
        GObjectClass* klass = G_OBJECT_GET_CLASS(gs);
        if (klass && g_object_class_find_property(klass, "gtk-application-prefer-dark-theme")) {
            g_object_get(gs, "gtk-application-prefer-dark-theme", &pref_dark, nullptr);
        }
        if (klass && g_object_class_find_property(klass, "gtk-theme-name")) {
            g_object_get(gs, "gtk-theme-name", &tname, nullptr);
        }
        bool dark = (pref_dark != FALSE);
        if (!dark && tname) {
            if (theme_to_lower(tname).find("dark") != std::string::npos) dark = true;
        }
        g_free(tname);
        if (dark) return true;
    }
    if (const char* env_t = std::getenv("GTK_THEME")) {
        if (theme_to_lower(env_t).find("dark") != std::string::npos) return true;
    }
    return false;
}

GdkColor gdk_from_rgb(uint32_t rgb) {
    GdkColor c{};
    c.red   = static_cast<guint16>(((rgb >> 16) & 0xFFu) * 257u);
    c.green = static_cast<guint16>(((rgb >> 8) & 0xFFu) * 257u);
    c.blue  = static_cast<guint16>((rgb & 0xFFu) * 257u);
    return c;
}

void apply_gtk_theme_recursive(GtkWidget* w, const ThemeConfig* t) {
    if (!w) return;
    if (!t) {
        gtk_widget_modify_bg(w, GTK_STATE_NORMAL, nullptr);
        gtk_widget_modify_bg(w, GTK_STATE_PRELIGHT, nullptr);
        gtk_widget_modify_bg(w, GTK_STATE_SELECTED, nullptr);
        gtk_widget_modify_fg(w, GTK_STATE_NORMAL, nullptr);
        gtk_widget_modify_fg(w, GTK_STATE_PRELIGHT, nullptr);
        gtk_widget_modify_fg(w, GTK_STATE_SELECTED, nullptr);
        gtk_widget_modify_base(w, GTK_STATE_NORMAL, nullptr);
        gtk_widget_modify_base(w, GTK_STATE_SELECTED, nullptr);
        gtk_widget_modify_text(w, GTK_STATE_NORMAL, nullptr);
        gtk_widget_modify_text(w, GTK_STATE_SELECTED, nullptr);
    } else {
        GdkColor bg = gdk_from_rgb(t->colors[static_cast<std::size_t>(ThemeRole::Bg)]);
        GdkColor fg = gdk_from_rgb(t->colors[static_cast<std::size_t>(ThemeRole::Fg)]);
        GdkColor pbg = gdk_from_rgb(t->colors[static_cast<std::size_t>(ThemeRole::PanelBg)]);
        GdkColor pfg = gdk_from_rgb(t->colors[static_cast<std::size_t>(ThemeRole::PanelFg)]);
        GdkColor bbg = gdk_from_rgb(t->colors[static_cast<std::size_t>(ThemeRole::ButtonBg)]);
        GdkColor bfg = gdk_from_rgb(t->colors[static_cast<std::size_t>(ThemeRole::ButtonFg)]);
        GdkColor ibg = gdk_from_rgb(t->colors[static_cast<std::size_t>(ThemeRole::InputBg)]);
        GdkColor ifg = gdk_from_rgb(t->colors[static_cast<std::size_t>(ThemeRole::InputFg)]);
        GdkColor sbg = gdk_from_rgb(t->colors[static_cast<std::size_t>(ThemeRole::SelectionBg)]);
        GdkColor sfg = gdk_from_rgb(t->colors[static_cast<std::size_t>(ThemeRole::SelectionFg)]);

        if (GTK_IS_ENTRY(w) || GTK_IS_TEXT_VIEW(w) || GTK_IS_TREE_VIEW(w)) {
            gtk_widget_modify_base(w, GTK_STATE_NORMAL, &ibg);
            gtk_widget_modify_text(w, GTK_STATE_NORMAL, &ifg);
            gtk_widget_modify_base(w, GTK_STATE_SELECTED, &sbg);
            gtk_widget_modify_text(w, GTK_STATE_SELECTED, &sfg);
        } else if (GTK_IS_BUTTON(w)) {
            gtk_widget_modify_bg(w, GTK_STATE_NORMAL, &bbg);
            gtk_widget_modify_fg(w, GTK_STATE_NORMAL, &bfg);
        } else if (GTK_IS_MENU_BAR(w) || GTK_IS_MENU(w) || GTK_IS_MENU_ITEM(w)) {
            gtk_widget_modify_bg(w, GTK_STATE_NORMAL, &pbg);
            gtk_widget_modify_fg(w, GTK_STATE_NORMAL, &pfg);
            gtk_widget_modify_bg(w, GTK_STATE_PRELIGHT, &sbg);
            gtk_widget_modify_fg(w, GTK_STATE_PRELIGHT, &sfg);
        } else {
            gtk_widget_modify_bg(w, GTK_STATE_NORMAL, &bg);
            gtk_widget_modify_fg(w, GTK_STATE_NORMAL, &fg);
        }
    }
    if (GTK_IS_CONTAINER(w)) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(w));
        for (GList* it = children; it != nullptr; it = it->next) {
            apply_gtk_theme_recursive(GTK_WIDGET(it->data), t);
        }
        g_list_free(children);
    }
}

ThemeConfig query_gtk_os_native_theme(GtkWidget* ref_widget, bool os_dark) {
    ThemeConfig out = resolve_effective_theme(os_dark ? "dark" : "light", {}, true, os_dark);
    out.name = "native";
    out.is_dark_base = os_dark;
    GtkStyle* st = ref_widget ? gtk_widget_get_style(ref_widget) : nullptr;
    if (!st) st = gtk_widget_get_default_style();
    if (!st) return out;
    auto from_gdk = [](const GdkColor& c) -> uint32_t {
        uint32_t r = static_cast<uint32_t>(c.red >> 8) & 0xFFu;
        uint32_t g = static_cast<uint32_t>(c.green >> 8) & 0xFFu;
        uint32_t b = static_cast<uint32_t>(c.blue >> 8) & 0xFFu;
        return (r << 16) | (g << 8) | b;
    };
    out.colors[static_cast<std::size_t>(ThemeRole::Bg)]          = from_gdk(st->bg[GTK_STATE_NORMAL]);
    out.colors[static_cast<std::size_t>(ThemeRole::Fg)]          = from_gdk(st->fg[GTK_STATE_NORMAL]);
    out.colors[static_cast<std::size_t>(ThemeRole::PanelBg)]     = from_gdk(st->bg[GTK_STATE_NORMAL]);
    out.colors[static_cast<std::size_t>(ThemeRole::PanelFg)]     = from_gdk(st->fg[GTK_STATE_NORMAL]);
    out.colors[static_cast<std::size_t>(ThemeRole::HeaderBg)]    = from_gdk(st->bg[GTK_STATE_PRELIGHT]);
    out.colors[static_cast<std::size_t>(ThemeRole::HeaderFg)]    = from_gdk(st->fg[GTK_STATE_PRELIGHT]);
    out.colors[static_cast<std::size_t>(ThemeRole::StatusBg)]    = from_gdk(st->bg[GTK_STATE_NORMAL]);
    out.colors[static_cast<std::size_t>(ThemeRole::StatusFg)]    = from_gdk(st->fg[GTK_STATE_INSENSITIVE]);
    out.colors[static_cast<std::size_t>(ThemeRole::Border)]      = from_gdk(st->mid[GTK_STATE_NORMAL]);
    out.colors[static_cast<std::size_t>(ThemeRole::Accent)]      = from_gdk(st->bg[GTK_STATE_SELECTED]);
    out.colors[static_cast<std::size_t>(ThemeRole::SelectionBg)] = from_gdk(st->bg[GTK_STATE_SELECTED]);
    out.colors[static_cast<std::size_t>(ThemeRole::SelectionFg)] = from_gdk(st->fg[GTK_STATE_SELECTED]);
    out.colors[static_cast<std::size_t>(ThemeRole::ButtonBg)]    = from_gdk(st->bg[GTK_STATE_NORMAL]);
    out.colors[static_cast<std::size_t>(ThemeRole::ButtonFg)]    = from_gdk(st->fg[GTK_STATE_NORMAL]);
    out.colors[static_cast<std::size_t>(ThemeRole::InputBg)]     = from_gdk(st->base[GTK_STATE_NORMAL]);
    out.colors[static_cast<std::size_t>(ThemeRole::InputFg)]     = from_gdk(st->text[GTK_STATE_NORMAL]);
    return out;
}

ThemeConfig resolve_gtk_palette(GtkAppState* st,
                                const std::string& theme_name,
                                const std::vector<ThemeConfig>& custom_themes,
                                bool os_dark) {
    const std::string eff = normalized_port_theme_name(theme_name, true);
    if (theme_to_lower(eff) == "native") {
        return query_gtk_os_native_theme(st ? st->window : nullptr, os_dark);
    }
    return resolve_effective_theme(theme_name, custom_themes, true, os_dark);
}

void apply_gtk_theme_for(GtkAppState* st,
                         const std::string& theme_name,
                         const std::vector<ThemeConfig>& custom_themes,
                         GtkWidget* extra_win = nullptr) {
    if (!st) return;
    const std::string eff = normalized_port_theme_name(theme_name, true);
    const ThemeConfig* ptr = nullptr;
    ThemeConfig resolved;
    if (theme_to_lower(eff) != "native") {
        resolved = resolve_effective_theme(theme_name, custom_themes, true, gtk_os_prefers_dark());
        ptr = &resolved;
    }
    if (st->window) {
        apply_gtk_theme_recursive(st->window, ptr);
        gtk_widget_queue_draw(st->window);
    }
    if (st->term_window) {
        apply_gtk_theme_recursive(st->term_window, ptr);
        gtk_widget_queue_draw(st->term_window);
    }
    if (extra_win) {
        apply_gtk_theme_recursive(extra_win, ptr);
        gtk_widget_queue_draw(extra_win);
    }
}

void apply_gtk_theme(GtkAppState* st) {
    if (!st || !st->engine) return;
    const node::NodeConfig cfg = st->engine->config();
    apply_gtk_theme_for(st, cfg.theme_name, cfg.themes, nullptr);
}

bool confirm_and_exit(GtkAppState* st) {
    const int auto_cnt = static_cast<int>(st->engine->auto_learned_peer_count());
    auto snap = st->engine->snapshot();
    OykGuiExitPrompt ep;
    oyk_gui_build_exit_prompt(auto_cnt, static_cast<int>(snap.total_active_connections), &ep);
    if (ep.has_auto_learned) {
        enum { RESP_SAVE = 10, RESP_DISCARD = 11, RESP_REVIEW = 12 };
        GtkWidget* dlg = gtk_dialog_new_with_buttons(
            ep.title,
            GTK_WINDOW(st->window),
            static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
            "Save & Exit", RESP_SAVE,
            "Discard & Exit", RESP_DISCARD,
            "Review Peers", RESP_REVIEW,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
            static_cast<const char*>(nullptr));
        GtkWidget* lbl = gtk_label_new(ep.body);
        gtk_misc_set_padding(GTK_MISC(lbl), 14, 12);
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), lbl, TRUE, TRUE, 4);
        gtk_widget_show_all(dlg);
        GtkSimpleCapCtx cap_ctx{dlg, "exit", RESP_DISCARD, 0};
        if (g_gtk_test.enabled) g_timeout_add(160, on_gtk_simple_dlg_cap, &cap_ctx);
        gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        if (resp == RESP_REVIEW) {
            open_settings_dialog(st, 4);
            return false;
        }
        if (resp == RESP_SAVE) {
            std::string err;
            (void)st->engine->persist_auto_learned_peers(&err);
            gtk_main_quit();
            return true;
        }
        if (resp == RESP_DISCARD) {
            st->engine->discard_auto_learned_peers();
            gtk_main_quit();
            return true;
        }
        return false;
    }

    GtkWidget* dlg = gtk_message_dialog_new(
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "%s", ep.body);
    gtk_window_set_title(GTK_WINDOW(dlg), ep.title);
    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    if (resp == GTK_RESPONSE_YES) {
        gtk_main_quit();
        return true;
    }
    return false;
}

GdkPixbuf* create_oyk_pixbuf(const unsigned char* rgba, int w, int h) {
    GdkPixbuf* src = gdk_pixbuf_new_from_data(
        rgba, GDK_COLORSPACE_RGB, TRUE, 8, w, h, w * 4, nullptr, nullptr);
    if (!src) return nullptr;
    GdkPixbuf* copy = gdk_pixbuf_copy(src);
    g_object_unref(src);
    return copy;
}

void on_about_clicked(GtkWidget*, gpointer user_data) {
    auto* st = static_cast<GtkAppState*>(user_data);
    if (st && st->modal_open > 0) return;
    struct ModalGuard { GtkAppState* a; ModalGuard(GtkAppState* x) : a(x) { if (a) ++a->modal_open; } ~ModalGuard() { if (a && a->modal_open > 0) --a->modal_open; } } mg(st);
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        oyk_gui_screen_title(OYK_SCR_ABOUT),
        GTK_WINDOW(st->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_STOCK_OK, GTK_RESPONSE_OK,
        static_cast<const char*>(nullptr));
    gtk_attach_esc_close(dlg, GTK_RESPONSE_OK);
    GtkWidget* hbox = gtk_hbox_new(FALSE, 14);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 16);
    GdkPixbuf* pb64 = create_oyk_pixbuf(kOykLogoSolid64Rgba, 64, 64);
    if (pb64) {
        GtkWidget* align = gtk_alignment_new(0.0f, 0.0f, 0.0f, 0.0f);
        GtkWidget* img = gtk_image_new_from_pixbuf(pb64);
        g_object_unref(pb64);
        gtk_container_add(GTK_CONTAINER(align), img);
        gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 0);
    }
    GtkWidget* lbl = gtk_label_new(oyk_gui_about_summary());
    gtk_misc_set_alignment(GTK_MISC(lbl), 0.0f, 0.0f);
    gtk_label_set_justify(GTK_LABEL(lbl), GTK_JUSTIFY_LEFT);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), hbox, TRUE, TRUE, 4);
    gtk_widget_show_all(dlg);
    GtkSimpleCapCtx cap_ctx{dlg, "about", GTK_RESPONSE_OK, 0};
    if (g_gtk_test.enabled) g_timeout_add(160, on_gtk_simple_dlg_cap, &cap_ctx);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
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

    GdkPixbuf* pb16 = create_oyk_pixbuf(kOykLogoSolid16Rgba, 16, 16);
    GdkPixbuf* pb32 = create_oyk_pixbuf(kOykLogoSolid32Rgba, 32, 32);
    GList* icon_list = nullptr;
    if (pb16) icon_list = g_list_append(icon_list, pb16);
    if (pb32) icon_list = g_list_append(icon_list, pb32);
    if (icon_list) {
        gtk_window_set_default_icon_list(icon_list);
        g_list_free(icon_list);
    }

    st.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(st.window), oyk_gui_screen_title(OYK_SCR_MAIN));
    if (pb32) gtk_window_set_icon(GTK_WINDOW(st.window), pb32);
    if (pb16) g_object_unref(pb16);
    if (pb32) g_object_unref(pb32);

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
    g_signal_connect(st.window, "key-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventKey*, gpointer p) -> gboolean {
                         auto* app = static_cast<GtkAppState*>(p);
                         return (app && app->modal_open > 0) ? TRUE : FALSE;
                     }),
                     &st);

    GtkWidget* root_vbox = gtk_vbox_new(FALSE, 4);
    gtk_container_add(GTK_CONTAINER(st.window), root_vbox);

    // Menu bar built from shared oyk_gui Node menu schema
    GtkWidget* menubar = gtk_menu_bar_new();
    GtkWidget* node_menu = gtk_menu_new();
    GtkWidget* node_root = gtk_menu_item_new_with_mnemonic("_Node");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(node_root), node_menu);

    const int n_menu = oyk_gui_node_menu_count();
    for (int mi = 0; mi < n_menu; ++mi) {
        const OykGuiMenuItem* item = oyk_gui_node_menu_item(mi);
        if (!item) continue;
        if (item->is_separator) {
            gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), gtk_separator_menu_item_new());
            continue;
        }
        GtkWidget* w = gtk_menu_item_new_with_mnemonic(item->gtk_label ? item->gtk_label : item->label);
        switch (item->action) {
            case OYK_ACT_ALLOW_INCOMING: g_signal_connect(w, "activate", G_CALLBACK(on_allow_incoming_clicked), &st); break;
            case OYK_ACT_DENY_INCOMING:  g_signal_connect(w, "activate", G_CALLBACK(on_deny_incoming_clicked), &st); break;
            case OYK_ACT_LOCAL_TERM:     g_signal_connect(w, "activate", G_CALLBACK(on_local_terminal_clicked), &st); break;
            case OYK_ACT_FILES:          g_signal_connect(w, "activate", G_CALLBACK(on_approve_files_clicked), &st); break;
            case OYK_ACT_RIG:            g_signal_connect(w, "activate", G_CALLBACK(on_rig_control_clicked), &st); break;
            case OYK_ACT_SETTINGS:       g_signal_connect(w, "activate", G_CALLBACK(on_settings_clicked), &st); break;
            case OYK_ACT_TERMINATE:      g_signal_connect(w, "activate", G_CALLBACK(on_terminate_clicked), &st); break;
            case OYK_ACT_BAN:            g_signal_connect(w, "activate", G_CALLBACK(on_ban_clicked), &st); break;
            case OYK_ACT_EXIT:           g_signal_connect(w, "activate", G_CALLBACK(on_exit_clicked), &st); break;
            default: break;
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(node_menu), w);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), node_root);
    GtkWidget* help_menu = gtk_menu_new();
    GtkWidget* help_root = gtk_menu_item_new_with_mnemonic("_Help");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_root), help_menu);
    GtkWidget* about_item = gtk_menu_item_new_with_mnemonic("_About OYK-Node...");
    g_signal_connect(about_item, "activate", G_CALLBACK(on_about_clicked), &st);
    gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), about_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), help_root);
    st.menubar = menubar;
    st.node_root = node_root;
    st.node_menu = node_menu;
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
    const int if_widths[5] = { 80, 75, 55, 170, 80 };
    for (int ci = 0; ci < oyk_gui_table_column_count(OYK_TBL_IFACES) && ci < 5; ++ci) {
        add_text_column(GTK_TREE_VIEW(iface_tv), oyk_gui_table_column(OYK_TBL_IFACES, ci), ci, if_widths[ci]);
    }
    GtkWidget* iface_sc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(iface_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(iface_sc), iface_tv);
    gtk_container_add(GTK_CONTAINER(iface_frame), iface_sc);
    gtk_paned_pack1(GTK_PANED(top_hpaned), iface_frame, TRUE, FALSE);

    // Right: Last 5 New Callsigns Heard
    GtkWidget* heard_frame = gtk_frame_new("Last 5 New Callsigns Heard");
    st.heard_store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* heard_tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(st.heard_store));
    const int hd_widths[4] = { 32, 95, 85, 80 };
    for (int ci = 0; ci < oyk_gui_table_column_count(OYK_TBL_HEARD) && ci < 4; ++ci) {
        add_text_column(GTK_TREE_VIEW(heard_tv), oyk_gui_table_column(OYK_TBL_HEARD, ci), ci, hd_widths[ci]);
    }
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
    const int sess_widths[7] = { 55, 100, 90, 85, 105, 85, 130 };
    for (int ci = 0; ci < oyk_gui_table_column_count(OYK_TBL_CONNECTIONS) && ci < 7; ++ci) {
        add_text_column(GTK_TREE_VIEW(st.session_tree), oyk_gui_table_column(OYK_TBL_CONNECTIONS, ci), ci, sess_widths[ci]);
    }
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

    const char* tmode = std::getenv("OYK_UI_TEST_MODE");
    const char* cdir = std::getenv("OYK_UI_CAPTURE_DIR");
    g_gtk_test.enabled = (tmode && std::string(tmode) == "1");
    g_gtk_test.cap_dir = cdir ? cdir : "";
    g_gtk_test.main_step = 0;
    g_gtk_test.main_win = st.window;
    if (g_gtk_test.enabled) {
        gtk_window_move(GTK_WINDOW(st.window), 40, 40);
        g_timeout_add(250, +[](gpointer p) -> gboolean {
            auto* app = static_cast<GtkAppState*>(p);
            switch (g_gtk_test.main_step++) {
                case 0:
                    gtk_inject_demo_sabm(app->engine);
                    return TRUE;
                case 1:
                    refresh_ui(app);
                    gtk_capture_window_png(app->window, "main");
                    gtk_menu_shell_select_item(GTK_MENU_SHELL(app->menubar), app->node_root);
                    return TRUE;
                case 2:
                    gtk_capture_window_png(app->window, "menu");
                    gtk_menu_shell_deactivate(GTK_MENU_SHELL(app->menubar));
                    gtk_menu_popdown(GTK_MENU(app->node_menu));
                    return TRUE;
                case 3:
                    on_settings_clicked(nullptr, app);
                    return TRUE;
                case 4:
                    on_terminate_clicked(nullptr, app);
                    return TRUE;
                case 5:
                    on_ban_clicked(nullptr, app);
                    return TRUE;
                case 6:
                    on_local_terminal_clicked(nullptr, app);
                    return TRUE;
                case 7:
                    if (app->term_window) {
                        while (gtk_events_pending()) gtk_main_iteration();
                        gdk_flush();
                        gtk_capture_window_png(app->term_window, "terminal");
                        if (auto* ts = static_cast<GtkTermWindowState*>(
                                g_object_get_data(G_OBJECT(app->term_window), "oyk_ts"))) {
                            show_gtk_term_help_dialog(ts);
                        }
                        gtk_widget_destroy(app->term_window);
                        app->term_window = nullptr;
                    }
                    return TRUE;
                case 8:
                    on_approve_files_clicked(nullptr, app);
                    return TRUE;
                case 9:
                    on_about_clicked(nullptr, app);
                    return TRUE;
                case 10:
                    on_exit_clicked(nullptr, app);
                    return FALSE;
                default:
                    return FALSE;
            }
        }, &st);
    }

    gtk_widget_show_all(st.window);
    apply_gtk_theme(&st);
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
