/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "esp32c6_console.hpp"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum C6lKeyCode {
    C6L_ESC = 0x1B,
    C6L_TAB = 0x09,
    C6L_ENTER = 0x0D,
    C6L_BS = 0x08,
    C6L_UP = 0x100,
    C6L_DOWN,
    C6L_LEFT,
    C6L_RIGHT,
    C6L_HOME,
    C6L_END,
    C6L_F1,
    C6L_F2,
    C6L_F3,
    C6L_F4,
    C6L_F9,
    C6L_F10
};

static char s_c6l_sgr[8][40];

static const char* c6l_format_sgr(int slot, oyk_u32 fg, oyk_u32 bg, int set_bg, int bold, Esp32C6TermType tt) {
    char* dst = s_c6l_sgr[slot & 7];
    if (tt >= C6L_TERM_VT340) {
        int fc = oyk_rgb_to_xterm256(fg);
        int bc = oyk_rgb_to_xterm256(bg);
        if (set_bg) sprintf(dst, "\033[%s38;5;%d;48;5;%dm", bold ? "1;" : "", fc, bc);
        else sprintf(dst, "\033[%s38;5;%dm", bold ? "1;" : "", fc);
    } else {
        strcpy(dst, bold ? "\033[1m" : "\033[0m");
    }
    return dst;
}

Esp32C6Console::Esp32C6Console()
    : node_(NULL), bridge_(NULL), tx_fn_(NULL), tx_ctx_(NULL),
      term_type_(C6L_TERM_VT100), view_(C6L_VIEW_CONSOLE), prompt_kind_(C6L_PROMPT_NONE),
      term_rows_(25), term_cols_(80), mouse_enabled_(0), now_ms_(0), last_esc_ms_(0),
      esc_state_(0), esc_len_(0), x10_len_(0), menu_sel_(0), settings_tab_(0), settings_sel_(0),
      wiz_same_iface_(0), wiz_lpol_(0), wiz_field_(0), wiz_btn_(1),
      edit_iface_idx_(0), edit_iface_field_(0), edit_peer_idx_(0), edit_peer_field_(0),
      edit_btn_(0), term_sel_(0), ban_sel_(0), ban_hard_(1), theme_sel_(0), theme_role_sel_(0),
      theme_preview_dirty_(0),
      prompt_cursor_(0), files_focus_btn_(0), fm_preview_active_(0),
      bootloader_pending_(0), bootloader_start_ms_(0), lterm_sid_(0), lterm_ctrl_a_(0),
      lterm_help_open_(0), lterm_line_count_(0), lterm_input_len_(0) {
    memset(&draft_, 0, sizeof(draft_));
    oyk_config_init_default(&draft_);
    status_fb_[0] = '\0';
    dialog_fb_[0] = '\0';
    esc_buf_[0] = '\0';
    memset(x10_buf_, 0, sizeof(x10_buf_));
    memset(&wiz_, 0, sizeof(wiz_));
    wiz_id_[0] = '\0'; wiz_desc_[0] = '\0'; wiz_host_[0] = '\0';
    wiz_port_[0] = '\0'; wiz_mask_[0] = '\0'; wiz_peer_[0] = '\0';
    memset(&edit_iface_buf_, 0, sizeof(edit_iface_buf_));
    memset(&edit_peer_buf_, 0, sizeof(edit_peer_buf_));
    ban_call_[0] = '\0'; ban_reason_[0] = '\0';
    prompt_label_[0] = '\0';
    prompt_buf_[0] = '\0';
    memset(&file_mgr_, 0, sizeof(file_mgr_));
    fm_preview_buf_[0] = '\0';
    memset(lterm_lines_, 0, sizeof(lterm_lines_));
    lterm_input_[0] = '\0';
    oyk_term_eol_init(&lterm_eol_, OYK_TERM_EOL_CR, 1);
}

void Esp32C6Console::init(OykPortableNode* node, Esp32C6NetBridge* bridge,
                          Esp32C6ConsoleTxFn tx_fn, void* tx_ctx) {
    node_ = node;
    bridge_ = bridge;
    tx_fn_ = tx_fn;
    tx_ctx_ = tx_ctx;
    term_type_ = C6L_TERM_VT100;
    view_ = C6L_VIEW_CONSOLE;
    prompt_kind_ = C6L_PROMPT_NONE;
    term_rows_ = 25;
    term_cols_ = 80;
    mouse_enabled_ = 0;
    if (node_) draft_ = node_->config();
    lterm_line_count_ = 0;
    lterm_input_len_ = 0;
    lterm_input_[0] = '\0';
    oyk_term_eol_init(&lterm_eol_, OYK_TERM_EOL_CR, 1);
    append_lterm_line("*** OYK ESP32-C6 Local Terminal (Ctrl+A Z:Help  Ctrl+A B:BBS  Ctrl+A T:Chat)");
    strcpy(status_fb_, "Ready. Ctrl-N:Menu | Tab/F9:Term | O:OLED | ?:About");
}

void Esp32C6Console::write_str(const char* s) {
    if (tx_fn_ && s && s[0]) tx_fn_(s, strlen(s), tx_ctx_);
}

void Esp32C6Console::send_probes() {
    write_str("\033[c\033[s\033[999;999H\033[6n\033[u");
}

void Esp32C6Console::on_usb_connect() {
    view_ = C6L_VIEW_CONSOLE;
    prompt_kind_ = C6L_PROMPT_NONE;
    send_probes();
    render();
}

const OykConfig* Esp32C6Console::active_theme_cfg() const {
    if (view_ == C6L_VIEW_SETTINGS && settings_tab_ == 7 && theme_preview_dirty_) return &draft_;
    return node_ ? &node_->config() : NULL;
}

const char* Esp32C6Console::term_type_label() const {
    switch (term_type_) {
        case C6L_TERM_VT220:    return "VT220";
        case C6L_TERM_VT340:    return "VT340";
        case C6L_TERM_XTERM256: return "xterm-256";
        default:                return "VT100";
    }
}

const char* Esp32C6Console::sgr_border() const {
    if (term_type_ < C6L_TERM_VT220) return "";
    OykThemeConfig tc; oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return c6l_format_sgr(0, tc.colors[OYK_TR_BORDER], tc.colors[OYK_TR_BG], 0, 0, term_type_);
}
const char* Esp32C6Console::sgr_title() const {
    if (term_type_ < C6L_TERM_VT220) return "\033[1m";
    OykThemeConfig tc; oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return c6l_format_sgr(1, tc.colors[OYK_TR_ACCENT], tc.colors[OYK_TR_BG], 0, 1, term_type_);
}
const char* Esp32C6Console::sgr_header() const {
    if (term_type_ < C6L_TERM_VT220) return "\033[1m";
    OykThemeConfig tc; oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return c6l_format_sgr(2, tc.colors[OYK_TR_HEADER_FG], tc.colors[OYK_TR_HEADER_BG], 1, 1, term_type_);
}
const char* Esp32C6Console::sgr_menubar() const {
    if (term_type_ < C6L_TERM_VT220) return "\033[7m";
    OykThemeConfig tc; oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return c6l_format_sgr(3, tc.colors[OYK_TR_BUTTON_FG], tc.colors[OYK_TR_BUTTON_BG], 1, 0, term_type_);
}
const char* Esp32C6Console::sgr_menusel() const {
    if (term_type_ < C6L_TERM_VT220) return "\033[7m";
    OykThemeConfig tc; oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return c6l_format_sgr(4, tc.colors[OYK_TR_SELECTION_FG], tc.colors[OYK_TR_SELECTION_BG], 1, 1, term_type_);
}
const char* Esp32C6Console::sgr_list_hdr() const {
    if (term_type_ < C6L_TERM_VT220) return "\033[1;7m";
    OykThemeConfig tc; oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return c6l_format_sgr(5, tc.colors[OYK_TR_PANEL_FG], tc.colors[OYK_TR_PANEL_BG], 1, 1, term_type_);
}
const char* Esp32C6Console::sgr_sel() const {
    if (term_type_ < C6L_TERM_VT220) return "\033[7m";
    OykThemeConfig tc; oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return c6l_format_sgr(6, tc.colors[OYK_TR_SELECTION_FG], tc.colors[OYK_TR_SELECTION_BG], 1, 1, term_type_);
}
const char* Esp32C6Console::sgr_warn() const {
    if (term_type_ < C6L_TERM_VT220) return "\033[1m";
    OykThemeConfig tc; oyk_theme_resolve_effective(active_theme_cfg(), 0, 1, &tc);
    return c6l_format_sgr(7, tc.colors[OYK_TR_WARN], tc.colors[OYK_TR_BG], 0, 1, term_type_);
}
const char* Esp32C6Console::sgr_ok() const {
    return (term_type_ >= C6L_TERM_VT340) ? "\033[1;32m" : "\033[1m";
}

void Esp32C6Console::draw_box(int row, int col, int h, int w, const char* title, int clear_interior, int with_close) {
    if (h < 2 || w < 4) return;
    char line[256];
    int inner = w - 2;
    if (inner > 140) inner = 140;
    const char* bdr = sgr_border();

    sprintf(line, "\033[%d;%dH%s\033(0l", row, col, bdr);
    write_str(line);
    for (int i = 0; i < inner; ++i) write_str("q");
    write_str("k\033(B\033[0m");

    if (with_close && w >= 8) {
        OykTuiRect rr; rr.top = row - 1; rr.left = col - 1; rr.h = h; rr.w = w;
        int cc = oyk_tui_close_btn_col(&rr) + 1;
        if (cc > col + 1 && cc + 2 < col + w) {
            sprintf(line, "\033[%d;%dH\033[1;37;41m[X]\033[0m", row, cc);
            write_str(line);
        }
    }
    if (title && title[0]) {
        int max_t = with_close ? (inner - 6) : (inner - 2);
        if (max_t > 0) {
            sprintf(line, "\033[%d;%dH%s %.*s \033[0m", row, col + 2, sgr_title(), max_t, title);
            write_str(line);
        }
    }
    for (int r = 1; r < h - 1; ++r) {
        if (clear_interior) {
            sprintf(line, "\033[%d;%dH%s\033(0x\033(B%*s%s\033(0x\033(B\033[0m",
                    row + r, col, bdr, inner, "", bdr);
        } else {
            sprintf(line, "\033[%d;%dH%s\033(0x\033(B\033[%d;%dH%s\033(0x\033(B\033[0m",
                    row + r, col, bdr, row + r, col + w - 1, bdr);
        }
        write_str(line);
    }
    sprintf(line, "\033[%d;%dH%s\033(0m", row + h - 1, col, bdr);
    write_str(line);
    for (int i = 0; i < inner; ++i) write_str("q");
    write_str("j\033(B\033[0m");
}

void Esp32C6Console::draw_hdiv(int row, int col, int w) {
    if (w < 4) return;
    char line[64];
    int inner = w - 2;
    sprintf(line, "\033[%d;%dH%s\033(0t", row, col, sgr_border());
    write_str(line);
    for (int i = 0; i < inner; ++i) write_str("q");
    write_str("u\033(B\033[0m");
}

void Esp32C6Console::draw_shadow(int row, int col, int h, int w) {
    char buf[160];
    int max_c = term_cols_;
    for (int r = 1; r <= h; ++r) {
        int sr = row + r;
        if (sr > term_rows_) break;
        if (col + w <= max_c) {
            int sw = (col + w + 1 <= max_c) ? 2 : 1;
            sprintf(buf, "\033[%d;%dH\033[0;90;40m%*s\033[0m", sr, col + w, sw, "");
            write_str(buf);
        }
    }
    int br = row + h;
    if (br <= term_rows_ && col + 2 <= max_c) {
        int bw = w;
        if (col + 2 + bw - 1 > max_c) bw = max_c - (col + 2) + 1;
        if (bw > 0) {
            sprintf(buf, "\033[%d;%dH\033[0;90;40m%*s\033[0m", br, col + 2, bw, "");
            write_str(buf);
        }
    }
}

void Esp32C6Console::tui_get_size_cb(void* ctx, int* out_rows, int* out_cols) {
    const Esp32C6Console* c = static_cast<const Esp32C6Console*>(ctx);
    if (out_rows) *out_rows = c ? c->term_rows_ : 25;
    if (out_cols) *out_cols = c ? c->term_cols_ : 80;
}

void Esp32C6Console::tui_put_text_cb(void* ctx, int row, int col, int max_w, int style, const char* text) {
    Esp32C6Console* c = static_cast<Esp32C6Console*>(ctx);
    if (!c || max_w <= 0) return;
    if (max_w > 132) max_w = 132;
    const char* sgr = "\033[0m";
    switch (style) {
        case OYK_TUI_STYLE_BORDER:
        case OYK_TUI_STYLE_DLG_BORDER: sgr = c->sgr_border(); break;
        case OYK_TUI_STYLE_TITLE:      sgr = c->sgr_title(); break;
        case OYK_TUI_STYLE_HEADER:     sgr = c->sgr_header(); break;
        case OYK_TUI_STYLE_MENU_BAR:
        case OYK_TUI_STYLE_DLG_BAR:    sgr = c->sgr_menubar(); break;
        case OYK_TUI_STYLE_MENU_SEL:
        case OYK_TUI_STYLE_DLG_SEL:    sgr = c->sgr_sel(); break;
        case OYK_TUI_STYLE_LIST_HDR:   sgr = c->sgr_list_hdr(); break;
        case OYK_TUI_STYLE_WARN:
        case OYK_TUI_STYLE_ERROR:      sgr = c->sgr_warn(); break;
        case OYK_TUI_STYLE_CLOSE_BTN:
            sgr = (c->term_type_ >= C6L_TERM_VT220) ? "\033[1;37;41m" : "\033[7m";
            break;
        default: sgr = "\033[0m"; break;
    }
    char out[256];
    const char* txt = text ? text : "";
    sprintf(out, "\033[%d;%dH%s%-*.*s\033[0m", row + 1, col + 1, sgr, max_w, max_w, txt);
    c->write_str(out);
}

void Esp32C6Console::tui_put_box_cb(void* ctx, int top, int left, int h, int w,
                                     int /*bdr*/, int /*fill*/, int fill_interior) {
    Esp32C6Console* c = static_cast<Esp32C6Console*>(ctx);
    if (c) c->draw_box(top + 1, left + 1, h, w, NULL, fill_interior, 0);
}

void Esp32C6Console::tui_put_hdiv_cb(void* ctx, int row, int left, int w, int /*style*/) {
    Esp32C6Console* c = static_cast<Esp32C6Console*>(ctx);
    if (c) c->draw_hdiv(row + 1, left + 1, w);
}

void Esp32C6Console::tui_put_shadow_cb(void* ctx, int top, int left, int h, int w) {
    Esp32C6Console* c = static_cast<Esp32C6Console*>(ctx);
    if (c) c->draw_shadow(top + 1, left + 1, h, w);
}

OykTuiBackend Esp32C6Console::make_tui_backend() {
    OykTuiBackend b;
    memset(&b, 0, sizeof(b));
    b.ctx = this;
    b.get_size = Esp32C6Console::tui_get_size_cb;
    b.put_text = Esp32C6Console::tui_put_text_cb;
    b.put_box = Esp32C6Console::tui_put_box_cb;
    b.put_hdiv = Esp32C6Console::tui_put_hdiv_cb;
    b.put_shadow = Esp32C6Console::tui_put_shadow_cb;
    return b;
}

void Esp32C6Console::render() {
    if (!node_ || !tx_fn_) return;
    if (term_type_ >= C6L_TERM_VT220) write_str("\033[?25l");

    switch (view_) {
        case C6L_VIEW_LOCAL_TERM: render_local_term(); break;
        case C6L_VIEW_FILES:      render_console(); render_files(); break;
        case C6L_VIEW_RIG:        render_console(); render_rig(); break;
        case C6L_VIEW_SETTINGS:   render_console(); render_settings(); break;
        case C6L_VIEW_LORA:       render_console(); render_lora(); break;
        case C6L_VIEW_STORAGE:    render_console(); render_storage(); break;
        case C6L_VIEW_HW:         render_console(); render_hw(); break;
        case C6L_VIEW_BOOTLOADER: render_console(); render_bootloader(); break;
        case C6L_VIEW_ABOUT:      render_console(); render_about(); break;
        case C6L_VIEW_MENU:       render_console(); render_menu(); break;
        default:                  render_console(); break;
    }

    if (prompt_kind_ == C6L_PROMPT_ADD_WIZARD) render_wizard_modal();
    else if (prompt_kind_ == C6L_PROMPT_EDIT_IFACE) render_edit_iface_modal();
    else if (prompt_kind_ == C6L_PROMPT_EDIT_PEER) render_edit_peer_modal();
    else if (prompt_kind_ == C6L_PROMPT_TERM_SID) render_terminate_modal();
    else if (prompt_kind_ == C6L_PROMPT_BAN_CALL) render_ban_modal();
    else if (prompt_kind_ == C6L_PROMPT_CONFIRM_FORMAT) render_confirm_format_modal();
    else if (prompt_kind_ == C6L_PROMPT_SET_TEXT ||
             prompt_kind_ == C6L_PROMPT_SET_BOOL ||
             prompt_kind_ == C6L_PROMPT_LTERM_CONNECT ||
             prompt_kind_ == C6L_PROMPT_FM_UPLOAD ||
             prompt_kind_ == C6L_PROMPT_FM_MKDIR ||
             prompt_kind_ == C6L_PROMPT_FM_RENAME) render_text_prompt();
    else if (view_ == C6L_VIEW_LOCAL_TERM && lterm_help_open_) render_local_term_help();
}

void Esp32C6Console::render_oled(Esp32C6OledSnapshot* out) {
    if (!node_ || !out) return;
    memset(out, 0, sizeof(*out));
    oyk_format_callsign(&node_->config().callsign, out->callsign, sizeof(out->callsign));
    oyk_format_callsign(&node_->config().alias, out->alias, sizeof(out->alias));
    out->accept_incoming = node_->accept_incoming();
    out->active_conns = node_->total_active_connections();
    out->uptime_sec = node_->uptime_seconds();
    const Esp32C6LoraConfig* lc = esp32c6_lora_config();
    const Esp32C6LoraStats* ls = esp32c6_lora_stats();
    out->lora_freq_hz = lc->freq_hz;
    out->lora_rx_pkts = ls->rx_packets;
    out->lora_tx_pkts = ls->tx_packets;
    out->last_rssi_dbm = ls->last_rssi_dbm;
    const Esp32C6RadioState* rs = esp32c6_radio_state();
    out->wifi_connected = rs->wifi_connected;
    out->bt_enabled = rs->ble_enabled;
    out->wifi_ip_net = rs->ip_addr_net;
    out->grove_baud = 115200UL;
    if (node_->new_heard_count() > 0) {
        const OykNewHeard& rh = node_->new_heard(0);
        strncpy(out->last_heard, rh.callsign, sizeof(out->last_heard) - 1);
        out->last_heard[sizeof(out->last_heard) - 1] = '\0';
    }
    esp32c6_oled_render(out);
}

void Esp32C6Console::render_console() {
    char buf[320], call_s[16], rad_s[12];
    oyk_format_callsign(&node_->config().callsign, call_s, sizeof(call_s));
    if (bridge_) bridge_->format_short_radio_status(rad_s, sizeof(rad_s));
    else strcpy(rad_s, "NONE");

    OykTuiConsoleLayout lay;
    oyk_tui_compute_console_layout(term_rows_, term_cols_, &lay);
    const int ncols = lay.cols;

    write_str("\033[H\033[2J");
    int hint_col = ncols - 32;
    if (hint_col < 9) hint_col = 9;
    sprintf(buf, "%s%-*s\033[0m\033[1;2H%s Node \033[0m\033[1;%dH%s %-21s  [%9s]\033[0m",
            sgr_menubar(), ncols, "", sgr_menusel(), hint_col, sgr_menubar(),
            lterm_sid_ ? "Tab/F9:Term(ACTIVE)" : "Tab/F9:Term | Ctrl-N",
            term_type_label());
    write_str(buf);

    char hdr_txt[160];
    sprintf(hdr_txt, " OYK C6L(UnitC6L): %-9s Up:%-4lus C:%-2d [IN:%-5s] OLED:P%d RF:%s",
            call_s, static_cast<unsigned long>(node_->uptime_seconds()),
            node_->total_active_connections(),
            node_->accept_incoming() ? "ALLOW" : "DENY",
            esp32c6_oled_page() + 1, rad_s);
    sprintf(buf, "\033[2;1H%s%-*.*s\033[0m", sgr_header(), ncols, ncols, hdr_txt);
    write_str(buf);

    if (lay.too_small) {
        sprintf(buf, "\033[4;2HTerminal too small (%dx%d). Min 60x16.", ncols, lay.rows);
        write_str(buf);
        return;
    }

    draw_box(lay.iface_box.top + 1, lay.iface_box.left + 1, lay.iface_box.h, lay.iface_box.w, "Interfaces", 0, 0);
    draw_box(lay.heard_box.top + 1, lay.heard_box.left + 1, lay.heard_box.h, lay.heard_box.w, "Last 5 New Callsigns Heard", 0, 0);

    const int iw = lay.iface_box.w - 2, hw = lay.heard_box.w - 2;
    const int il = lay.iface_box.left + 2, hl = lay.heard_box.left + 2;
    const int top_hdr = lay.iface_box.top + 2;
    char lhdr[96], rhdr[96];
    sprintf(lhdr, " %-8s %-11s %-2s %-15s", "Port", "Type", "C", "Status");
    sprintf(rhdr, " %-2s %-9s %-7s %-8s", "#", "Callsign", "Iface", "Time");
    sprintf(buf, "\033[%d;%dH%s%-*.*s\033[0m\033[%d;%dH%s%-*.*s\033[0m",
            top_hdr, il, sgr_list_hdr(), iw, iw, lhdr,
            top_hdr, hl, sgr_list_hdr(), hw, hw, rhdr);
    write_str(buf);

    const int top_vis = (lay.iface_vis_rows > lay.heard_vis_rows) ? lay.iface_vis_rows : lay.heard_vis_rows;
    for (int i = 0; i < top_vis; ++i) {
        char lft[96], rgt[96];
        if (i < lay.iface_vis_rows && i < node_->iface_count()) {
            const OykIfaceRuntime& r = node_->iface(i);
            sprintf(lft, " %-8.8s %-11.11s %-2d %-15.15s",
                    r.cfg.id, r.cfg.type, node_->active_connections_on(i), r.status);
        } else {
            lft[0] = '\0';
        }
        if (i < lay.heard_vis_rows && i < node_->new_heard_count()) {
            const OykNewHeard& h = node_->new_heard(i);
            sprintf(rgt, " %-2d %-9.9s %-7.7s %-8.8s", i + 1, h.callsign, h.iface_id, h.time_hms);
        } else {
            rgt[0] = '\0';
        }
        sprintf(buf, "\033[%d;%dH%-*.*s\033[%d;%dH%-*.*s",
                top_hdr + 1 + i, il, iw, iw, lft,
                top_hdr + 1 + i, hl, hw, hw, rgt);
        write_str(buf);
    }

    draw_box(lay.conn_box.top + 1, lay.conn_box.left + 1, lay.conn_box.h, lay.conn_box.w,
             "Active Connections", 0, 0);
    const int cw = lay.conn_box.w - 2;
    const int crow0 = lay.conn_box.top + 2;
    sprintf(buf, "\033[%d;%dH%s %-4s %-10s %-8s %-9s %-10s %-18s\033[0m",
            crow0, 2, sgr_list_hdr(), "ID", "Callsign", "Iface", "Since", "State", "Target");
    write_str(buf);
    int printed = 0;
    for (int si = 0; si < node_->max_sessions() && printed < lay.conn_vis_rows; ++si) {
        const OykSession& s = node_->session_slot(si);
        if (s.state == OYK_SESS_FREE) continue;
        char cs[16], line[128];
        oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
        const char* ifid = (s.iface_idx >= 0) ? node_->iface(s.iface_idx).cfg.id : "local";
        sprintf(line, " %-4lu %-10.10s %-8.8s %-9.9s %-10.10s %-18.18s",
                static_cast<unsigned long>(s.id), cs, ifid, s.since_hms, "CONNECTED",
                s.in_bbs ? "BBS" : (s.in_chat ? "CHAT" : "NODE"));
        sprintf(buf, "\033[%d;2H%-*.*s", crow0 + 1 + printed, cw, cw, line);
        write_str(buf);
        ++printed;
    }

    draw_box(lay.log_box.top + 1, lay.log_box.left + 1, lay.log_box.h, lay.log_box.w,
             "Node Activity Log", 0, 0);
    const int lrows = lay.log_vis_rows;
    const int lcnt = node_->console_log_count();
    const int start_log = (lcnt > lrows) ? (lcnt - lrows) : 0;
    for (int li = 0; li < lrows && (start_log + li) < lcnt; ++li) {
        sprintf(buf, "\033[%d;2H%-*.*s", lay.log_box.top + 2 + li, ncols - 2, ncols - 2,
                node_->console_log_line(start_log + li));
        write_str(buf);
    }

    sprintf(buf, "\033[%d;1H%s %-*.*s\033[0m",
            lay.status_row + 1, sgr_menubar(), ncols - 1, ncols - 1, status_fb_);
    write_str(buf);
}

void Esp32C6Console::render_menu() {
    static const char* kItems[13] = {
        " [A] Allow Incoming Connections ",
        " [D] Deny Incoming Connections  ",
        " [L] Local Terminal...          ",
        " [F] Manage Files Area...       ",
        " [R] Radio Control...           ",
        " [S] Settings...                ",
        " [T] Terminate Connection...    ",
        " [B] Ban / Unban Callsign...    ",
        " [K] LoRa SX1262 Radio...       ",
        " [G] Storage (16MB Flash)...    ",
        " [P] Unit C6L Hardware & Pins   ",
        " [?] About OYK-Node...          ",
        " [U] Exit / Bootloader          "
    };
    const int mh = 17, mw = 35;
    draw_box(2, 1, mh, mw, "", 1, 0);
    draw_shadow(2, 1, mh, mw);
    int r = 3;
    for (int i = 0; i < 13; ++i) {
        if (i == 2 || i == 8) draw_hdiv(r++, 1, mw);
        char line[128];
        snprintf(line, sizeof(line), "\033[%d;2H%s%-*.*s\033[0m", r++,
                 (i == menu_sel_) ? sgr_menusel() : "",
                 mw - 2, mw - 2, kItems[i]);
        write_str(line);
    }
}

void Esp32C6Console::render_settings() {
    static const char* kTabs[8] = {
        "1:Node", "2:BBS", "3:CHAT", "4:Ifaces", "5:Peers", "6:Rig", "7:Prn", "8:Thm"
    };
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 21, 74, &mr);
    const int top = mr.top + 1, left = mr.left + 1;
    draw_box(top, left, mr.h, mr.w, "Node Settings", 1, 1);
    draw_shadow(top, left, mr.h, mr.w);

    char buf[192];
    int col = left + 2;
    sprintf(buf, "\033[%d;%dH%s%-*s\033[0m", top + 1, left + 1, sgr_menubar(), mr.w - 2, "");
    write_str(buf);
    for (int t = 0; t < 8; ++t) {
        sprintf(buf, "\033[%d;%dH%s%s%s%s\033[0m", top + 1, col,
                (t == settings_tab_) ? sgr_sel() : sgr_menubar(),
                (t == settings_tab_) ? "[" : " ",
                kTabs[t],
                (t == settings_tab_) ? "]" : " ");
        write_str(buf);
        col += static_cast<int>(strlen(kTabs[t])) + 2;
    }
    draw_hdiv(top + 2, left, mr.w);

    char cs[16], al[16];
    oyk_format_callsign(&draft_.callsign, cs, sizeof(cs));
    oyk_format_callsign(&draft_.alias, al, sizeof(al));
    if (settings_tab_ == 0) {
        const char* labels[7] = { "Node Callsign", "Node Alias", "Incoming Policy",
                                  "Interfaces File", "Peers File", "Themes File", "Learned Policy" };
        char vals[7][32];
        strcpy(vals[0], cs); strcpy(vals[1], al);
        strcpy(vals[2], draft_.accept_incoming ? "[ON]" : "[OFF]");
        strcpy(vals[3], draft_.interfaces_file);
        strcpy(vals[4], draft_.peers_file);
        strcpy(vals[5], draft_.themes_file);
        strcpy(vals[6], oyk_format_learned_policy(draft_.learned_peer_policy));
        for (int i = 0; i < 7; ++i) {
            sprintf(buf, "\033[%d;%dH%-17s: %s%-26.26s\033[0m", top + 3 + i, left + 3,
                    labels[i], (i == settings_sel_) ? sgr_sel() : "", vals[i]);
            write_str(buf);
        }
    } else if (settings_tab_ == 1) {
        sprintf(buf, "\033[%d;%dHEnable Packet BBS : %s", top + 3, left + 3, draft_.bbs_enabled ? "[ON]" : "[OFF]");
        write_str(buf);
        sprintf(buf, "\033[%d;%dHBBS Call/Alias    : %s-%u (%s)", top + 4, left + 3,
                cs, static_cast<unsigned int>(draft_.bbs_ssid), "OYKBBS");
        write_str(buf);
        sprintf(buf, "\033[%d;%dHFiles Area Path   : %s", top + 5, left + 3, draft_.bbs_files_path);
        write_str(buf);
    } else if (settings_tab_ == 2) {
        sprintf(buf, "\033[%d;%dHEnable Multi-User CHAT : %s", top + 3, left + 3, draft_.chat_enabled ? "[ON]" : "[OFF]");
        write_str(buf);
        sprintf(buf, "\033[%d;%dHCHAT SSID / Topic      : -%u / %s", top + 4, left + 3,
                static_cast<unsigned int>(draft_.chat_ssid), draft_.chat_topic);
        write_str(buf);
    } else if (settings_tab_ == 3) {
        sprintf(buf, "\033[%d;%dH%s %-8s %-11s %-18s %-8s\033[0m", top + 3, left + 1, sgr_list_hdr(),
                "ID", "Type", "Host/Device", "Policy");
        write_str(buf);
        for (int i = 0; i < draft_.iface_count && i < 10; ++i) {
            const OykIfaceConfig& ic = draft_.ifaces[i];
            sprintf(buf, "\033[%d;%dH%s %-8.8s %-11.11s %-18.18s %s/%s\033[0m",
                    top + 4 + i, left + 1, (i == settings_sel_) ? sgr_sel() : "",
                    ic.id, ic.type, ic.device[0] ? ic.device : ic.host,
                    ic.block_inbound ? "-" : "IN", ic.block_outbound ? "-" : "OUT");
            write_str(buf);
        }
    } else if (settings_tab_ == 4) {
        sprintf(buf, "\033[%d;%dH%s %-9s %-10s %-8s %-18s %-6s\033[0m", top + 3, left + 1, sgr_list_hdr(),
                "ID", "Callsign", "Iface", "Endpoint", "Mode");
        write_str(buf);
        for (int i = 0; i < draft_.peer_count && i < 10; ++i) {
            const OykPeerConfig& pr = draft_.peers[i];
            char ep[48];
            sprintf(ep, "%s:%u", pr.host, static_cast<unsigned int>(pr.port));
            sprintf(buf, "\033[%d;%dH%s %-9.9s %-10.10s %-8.8s %-18.18s %-6s\033[0m",
                    top + 4 + i, left + 1, (i == settings_sel_) ? sgr_sel() : "",
                    pr.id, pr.callsign, pr.interface_id, ep, pr.auto_learned ? "AUTO" : "STATIC");
            write_str(buf);
        }
    } else if (settings_tab_ == 5) {
        sprintf(buf, "\033[%d;%dHEnable Rig Control : %s", top + 3, left + 3, draft_.rig_enabled ? "[ON]" : "[OFF]");
        write_str(buf);
        sprintf(buf, "\033[%d;%dHRig Protocol/Host  : %s (%s:%u)", top + 4, left + 3,
                draft_.rig_protocol, draft_.rig_host, static_cast<unsigned int>(draft_.rig_port));
        write_str(buf);
    } else if (settings_tab_ == 6) {
        sprintf(buf, "\033[%d;%dHEnable Line Printer : %s", top + 3, left + 3, draft_.printer_enabled ? "[ON]" : "[OFF]");
        write_str(buf);
        sprintf(buf, "\033[%d;%dHPrinter Protocol    : %s (%lu baud)", top + 4, left + 3,
                draft_.printer_protocol, static_cast<unsigned long>(draft_.printer_baud));
        write_str(buf);
    } else {
        OykThemeConfig pal;
        oyk_theme_resolve_effective(&draft_, 0, 1, &pal);
        sprintf(buf, "\033[%d;%dHActive Theme: < %s >", top + 3, left + 3,
                draft_.theme_name[0] ? draft_.theme_name : "dark");
        write_str(buf);
        sprintf(buf, "\033[%d;%dH%s %-18s %-14s %-10s %-14s\033[0m", top + 4, left + 1, sgr_list_hdr(),
                "Role", "Key", "Color", "Preview");
        write_str(buf);
        for (int i = 0; i < 10 && i < OYK_THEME_COLOR_COUNT; ++i) {
            char hx[12];
            oyk_theme_format_hex(pal.colors[i], hx, sizeof(hx));
            sprintf(buf, "\033[%d;%dH%s %-18s %-14s %-10s \033[48;5;%dm      \033[0m",
                    top + 5 + i, left + 1, (i == theme_role_sel_) ? sgr_sel() : "",
                    oyk_theme_role_label(i), oyk_theme_role_key(i), hx,
                    oyk_rgb_to_xterm256(pal.colors[i]));
            write_str(buf);
        }
    }

    if (dialog_fb_[0]) {
        sprintf(buf, "\033[%d;%dH%s%-.68s\033[0m", top + mr.h - 4, left + 2, sgr_warn(), dialog_fb_);
        write_str(buf);
    }
    draw_hdiv(top + mr.h - 3, left, mr.w);
    const char* keybar = (settings_tab_ == 7)
        ? "1-8/Tab:Tabs  Lt/Rt:Theme  Up/Dn:Role  S:Save & Apply  Esc:Close"
        : ((settings_tab_ == 3 || settings_tab_ == 4)
            ? "1-8:Tabs  N/A:Add  E/Enter:Edit  S:Save  Esc:Close"
            : "1-8/Tab/Lt/Rt:Tabs  Enter/Space:Edit  S:Save & Apply  Esc:Close");
    sprintf(buf, "\033[%d;%dH%s %-69.69s \033[0m", top + mr.h - 2, left + 1, sgr_menubar(), keybar);
    write_str(buf);
}

void Esp32C6Console::render_wizard_modal() {
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 19, 68, &mr);
    const int top = mr.top + 1, left = mr.left + 1;
    draw_box(top, left, mr.h, mr.w, "Add Interface", 1, 1);
    draw_shadow(top, left, mr.h, mr.w);

    char pbuf[160];
    static const char* kBars[4] = {
        "[##......] 1/4", "[####....] 2/4", "[######..] 3/4", "[########] 4/4"
    };
    sprintf(pbuf, "\033[%d;%dH%s%s\033[0m", top, left + mr.w - 19, sgr_title(),
            kBars[wiz_.step & 3]);
    write_str(pbuf);
    sprintf(pbuf, "\033[%d;%dH%s", top + 1, left + 2, oyk_gui_wiz_step_prompt(&wiz_));
    write_str(pbuf);

    if (wiz_.step == 0) {
        int n = oyk_gui_wiz_driver_count(wiz_.driver_mask);
        for (int i = 0; i < n; ++i) {
            sprintf(pbuf, "\033[%d;%dH%s%d. (%c) %-12s %s\033[0m",
                    top + 3 + i, left + 3,
                    (i == wiz_.selected_driver_idx) ? sgr_sel() : "",
                    i + 1, (i == wiz_.selected_driver_idx) ? '*' : ' ',
                    oyk_gui_wiz_driver_type(wiz_.driver_mask, i),
                    oyk_gui_wiz_driver_desc(wiz_.driver_mask, i));
            write_str(pbuf);
        }
    } else if (wiz_.step == 1) {
        sprintf(pbuf, "\033[%d;%dHInterface ID   : %s%-28.28s\033[0m", top + 4, left + 3,
                (wiz_field_ == 0) ? sgr_sel() : "", wiz_id_);
        write_str(pbuf);
        sprintf(pbuf, "\033[%d;%dHDescription    : %s%-36.36s\033[0m", top + 5, left + 3,
                (wiz_field_ == 1) ? sgr_sel() : "", wiz_desc_);
        write_str(pbuf);
    } else if (wiz_.step == 2) {
        sprintf(pbuf, "\033[%d;%dHInterface    : %s (%s)", top + 2, left + 3,
                wiz_id_, oyk_gui_wiz_driver_type(wiz_.driver_mask, wiz_.selected_driver_idx));
        write_str(pbuf);
        sprintf(pbuf, "\033[%d;%dHHost / Device  : %s%-28.28s\033[0m", top + 4, left + 3,
                (wiz_field_ == 0) ? sgr_sel() : "", wiz_host_);
        write_str(pbuf);
        sprintf(pbuf, "\033[%d;%dHPort / Baud    : %s%-16.16s\033[0m", top + 5, left + 3,
                (wiz_field_ == 1) ? sgr_sel() : "", wiz_port_);
        write_str(pbuf);
        sprintf(pbuf, "\033[%d;%dHAllow Inbound  : %s", top + 7, left + 3, wiz_.draft.block_inbound ? "[OFF]" : "[ON]");
        write_str(pbuf);
        sprintf(pbuf, "\033[%d;%dHAllow Outbound : %s", top + 8, left + 3, wiz_.draft.block_outbound ? "[OFF]" : "[ON]");
        write_str(pbuf);
        sprintf(pbuf, "\033[%d;%dHSame-Iface Fwd : %s", top + 9, left + 3, wiz_same_iface_ ? "[ON]" : "[OFF]");
        write_str(pbuf);
        sprintf(pbuf, "\033[%d;%dHLearned Policy : < %s >", top + 10, left + 3,
                oyk_gui_format_learned_policy(wiz_lpol_));
        write_str(pbuf);
    } else {
        sprintf(pbuf, "\033[%d;%dHDriver         : %s", top + 3, left + 3,
                oyk_gui_wiz_driver_type(wiz_.driver_mask, wiz_.selected_driver_idx));
        write_str(pbuf);
        sprintf(pbuf, "\033[%d;%dHInterface ID   : %s", top + 4, left + 3, wiz_id_);
        write_str(pbuf);
        sprintf(pbuf, "\033[%d;%dHEndpoint       : %s:%s", top + 5, left + 3, wiz_host_, wiz_port_);
        write_str(pbuf);
        sprintf(pbuf, "\033[%d;%dHAllow In/Out   : %s / %s (Same:%s)", top + 6, left + 3,
                wiz_.draft.block_inbound ? "false" : "true",
                wiz_.draft.block_outbound ? "false" : "true",
                wiz_same_iface_ ? "true" : "false");
        write_str(pbuf);
    }
    draw_hdiv(top + mr.h - 3, left, mr.w);
    OykTuiBackend b = make_tui_backend();
    OykTuiButton btns[3];
    btns[0].label = "< Back";
    btns[0].enabled = (wiz_.step > 0) ? 1 : 0;
    btns[1].label = (wiz_.step == 3) ? "Finish" : "Next >";
    btns[1].enabled = 1;
    btns[2].label = "Cancel";
    btns[2].enabled = 1;
    oyk_tui_draw_button_row(&b, mr.top + mr.h - 2, mr.left + 2, mr.w - 4,
                            btns, 3, wiz_btn_,
                            (wiz_.step > 0) ? "B:Back  Esc:Cancel" : "1-5:Pick  Esc:Cancel");
}

void Esp32C6Console::render_edit_iface_modal() {
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 18, 66, &mr);
    const int top = mr.top + 1, left = mr.left + 1;
    draw_box(top, left, mr.h, mr.w, "Edit Interface", 1, 1);
    draw_shadow(top, left, mr.h, mr.w);

    char buf[160];
    sprintf(buf, "\033[%d;%dHDriver         : %s", top + 2, left + 3, edit_iface_buf_.type);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHInterface ID   : %s%-24.24s\033[0m", top + 3, left + 3,
            (edit_iface_field_ == 0) ? sgr_sel() : "", edit_iface_buf_.id);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHHost / Device  : %s", top + 4, left + 3,
            edit_iface_buf_.device[0] ? edit_iface_buf_.device : edit_iface_buf_.host);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHAllow Inbound  : %s", top + 6, left + 3,
            edit_iface_buf_.block_inbound ? "[OFF]" : "[ON]");
    write_str(buf);
    sprintf(buf, "\033[%d;%dHAllow Outbound : %s", top + 7, left + 3,
            edit_iface_buf_.block_outbound ? "[OFF]" : "[ON]");
    write_str(buf);
    sprintf(buf, "\033[%d;%dHSame-Iface Fwd : %s", top + 8, left + 3,
            edit_iface_buf_.allow_same_interface ? "[ON]" : "[OFF]");
    write_str(buf);
    sprintf(buf, "\033[%d;%dHLearned Policy : < %s >", top + 9, left + 3,
            oyk_gui_format_learned_policy(edit_iface_buf_.learned_peer_policy));
    write_str(buf);
    draw_hdiv(top + mr.h - 3, left, mr.w);
    OykTuiBackend b = make_tui_backend();
    OykTuiButton btns[2];
    btns[0].label = " Save ";
    btns[0].enabled = 1;
    btns[1].label = "Cancel";
    btns[1].enabled = 1;
    oyk_tui_draw_button_row(&b, mr.top + mr.h - 2, mr.left + 2, mr.w - 4,
                            btns, 2, edit_btn_, "Space:Toggle  Enter:Save");
}

void Esp32C6Console::render_edit_peer_modal() {
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 16, 60, &mr);
    const int top = mr.top + 1, left = mr.left + 1;
    draw_box(top, left, mr.h, mr.w, "Edit Peer", 1, 1);
    draw_shadow(top, left, mr.h, mr.w);

    char buf[160];
    sprintf(buf, "\033[%d;%dHPeer ID        : %s%-20.20s\033[0m", top + 2, left + 3,
            (edit_peer_field_ == 0) ? sgr_sel() : "", edit_peer_buf_.id);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHCallsign       : %s", top + 3, left + 3, edit_peer_buf_.callsign);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHInterface      : < %s >", top + 4, left + 3, edit_peer_buf_.interface_id);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHEndpoint       : %s:%u", top + 5, left + 3,
            edit_peer_buf_.host, static_cast<unsigned int>(edit_peer_buf_.port));
    write_str(buf);
    sprintf(buf, "\033[%d;%dHAllow Inbound  : %s", top + 7, left + 3, edit_peer_buf_.allow_inbound ? "[ON]" : "[OFF]");
    write_str(buf);
    sprintf(buf, "\033[%d;%dHAllow Outbound : %s", top + 8, left + 3, edit_peer_buf_.allow_outbound ? "[ON]" : "[OFF]");
    write_str(buf);
    draw_hdiv(top + mr.h - 3, left, mr.w);
    OykTuiBackend b = make_tui_backend();
    OykTuiButton btns[2];
    btns[0].label = " Save ";
    btns[0].enabled = 1;
    btns[1].label = "Cancel";
    btns[1].enabled = 1;
    oyk_tui_draw_button_row(&b, mr.top + mr.h - 2, mr.left + 2, mr.w - 4,
                            btns, 2, edit_btn_, "Enter/S:Save  Esc:Cancel");
}

void Esp32C6Console::render_terminate_modal() {
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 15, 64, &mr);
    const int top = mr.top + 1, left = mr.left + 1;
    draw_box(top, left, mr.h, mr.w, "Terminate Active Connection", 1, 1);
    draw_shadow(top, left, mr.h, mr.w);

    char buf[160];
    sprintf(buf, "\033[%d;%dH%s %-5s %-12s %-10s %-14s\033[0m", top + 2, left + 1, sgr_list_hdr(),
            "ID", "Callsign", "Port", "State");
    write_str(buf);
    int r = 0;
    for (int i = 0; i < node_->max_sessions() && r < 7; ++i) {
        const OykSession& s = node_->session_slot(i);
        if (s.state != OYK_SESS_CONNECTED) continue;
        char cs[16]; oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
        const char* ifid = (s.iface_idx >= 0) ? node_->iface(s.iface_idx).cfg.id : "local";
        sprintf(buf, "\033[%d;%dH%s %-5lu %-12.12s %-10.10s %-14s\033[0m", top + 3 + r, left + 1,
                (r == term_sel_) ? sgr_sel() : "",
                static_cast<unsigned long>(s.id), cs, ifid, "CONNECTED");
        write_str(buf);
        ++r;
    }
    draw_hdiv(top + mr.h - 3, left, mr.w);
    sprintf(buf, "\033[%d;%dH%s %-60.60s \033[0m", top + mr.h - 2, left + 1, sgr_menubar(),
            "Up/Dn:Select  Enter:Terminate  Esc:Cancel");
    write_str(buf);
}

void Esp32C6Console::render_ban_modal() {
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 16, 66, &mr);
    const int top = mr.top + 1, left = mr.left + 1;
    draw_box(top, left, mr.h, mr.w, "Ban / Unban Callsign (banlist.cfg)", 1, 1);
    draw_shadow(top, left, mr.h, mr.w);

    char buf[160];
    sprintf(buf, "\033[%d;%dHCallsign to Ban: %s%-16.16s\033[0m", top + 2, left + 3, sgr_sel(), ban_call_);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHActive Bans (%d):", top + 4, left + 3, node_->ban_count());
    write_str(buf);
    for (int i = 0; i < node_->ban_count() && i < 6; ++i) {
        const OykBanEntry& b = node_->ban_entry(i);
        sprintf(buf, "\033[%d;%dH%s %-10.10s %-8s %-26.26s\033[0m", top + 5 + i, left + 2,
                (i == ban_sel_) ? sgr_sel() : "",
                b.key, (b.flags & 1U) ? "HARD" : "SOFT", b.reason);
        write_str(buf);
    }
    draw_hdiv(top + mr.h - 3, left, mr.w);
    sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m", top + mr.h - 2, left + 1, sgr_menubar(),
            "Type+Enter:Ban  Del:Unban  Esc:Close");
    write_str(buf);
}

void Esp32C6Console::render_files() {
    OykTuiBackend b = make_tui_backend();
    oyk_tui_draw_dual_pane_modal(&b, term_rows_, term_cols_, &file_mgr_,
                                 files_focus_btn_, "Manage BBS Files Area",
                                 fm_preview_buf_, fm_preview_active_);
}

void Esp32C6Console::render_rig() {
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 13, 64, &mr);
    const int top = mr.top + 1, left = mr.left + 1;
    draw_box(top, left, mr.h, mr.w, "Radio / Rig Control", 1, 1);
    draw_shadow(top, left, mr.h, mr.w);
    char buf[160];
    sprintf(buf, "\033[%d;%dHProtocol : %s (%s:%u)", top + 3, left + 3,
            node_->config().rig_protocol, node_->config().rig_host,
            static_cast<unsigned int>(node_->config().rig_port));
    write_str(buf);
    sprintf(buf, "\033[%d;%dHState    : %s", top + 4, left + 3,
            node_->config().rig_enabled ? "ENABLED" : "DISABLED");
    write_str(buf);
    draw_hdiv(top + mr.h - 3, left, mr.w);
    sprintf(buf, "\033[%d;%dH%s %-60.60s \033[0m", top + mr.h - 2, left + 1, sgr_menubar(),
            "Esc:Close");
    write_str(buf);
}

void Esp32C6Console::render_lora() {
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 14, 66, &mr);
    const int top = mr.top + 1, left = mr.left + 1;
    draw_box(top, left, mr.h, mr.w, "SX1262 LoRa Transceiver (Unit C6L)", 1, 1);
    draw_shadow(top, left, mr.h, mr.w);
    const Esp32C6LoraConfig* lc = esp32c6_lora_config();
    const Esp32C6LoraStats* ls = esp32c6_lora_stats();
    char buf[160];
    sprintf(buf, "\033[%d;%dHFrequency  : %.3f MHz  (SF%u / CR 4/%u / %d dBm)",
            top + 2, left + 3, static_cast<double>(lc->freq_hz) / 1000000.0,
            static_cast<unsigned int>(lc->sf), static_cast<unsigned int>(lc->cr),
            static_cast<int>(lc->tx_power_dbm));
    write_str(buf);
    sprintf(buf, "\033[%d;%dHSPI2 Pins  : SCK=%u MOSI=%u MISO=%u CS=%u DIO1=%u BUSY=%u",
            top + 4, left + 3, OYK_C6L_SPI_SCK_PIN, OYK_C6L_SPI_MOSI_PIN,
            OYK_C6L_SPI_MISO_PIN, OYK_C6L_LORA_CS_PIN, OYK_C6L_LORA_DIO1_PIN, OYK_C6L_LORA_BUSY_PIN);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHPI4IO RF   : 0x%02X (LNA/ANT/NRST)  RX:%lu TX:%lu RSSI:%d",
            top + 6, left + 3, static_cast<unsigned int>(esp32c6_board_pi4io_out_reg()),
            static_cast<unsigned long>(ls->rx_packets),
            static_cast<unsigned long>(ls->tx_packets), ls->last_rssi_dbm);
    write_str(buf);
    draw_hdiv(top + mr.h - 3, left, mr.w);
    sprintf(buf, "\033[%d;%dH%s %-62.62s \033[0m", top + mr.h - 2, left + 1, sgr_menubar(),
            "Esc:Close");
    write_str(buf);
}

void Esp32C6Console::render_storage() {
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 15, 68, &mr);
    const int top = mr.top + 1, left = mr.left + 1;
    draw_box(top, left, mr.h, mr.w, "16 MB Flash LittleFS Storage Manager", 1, 1);
    draw_shadow(top, left, mr.h, mr.w);
    const Esp32C6FsStatus* fs = esp32c6_fs_status();
    char buf[160];
    sprintf(buf, "\033[%d;%dHMedium     : %s", top + 2, left + 3, fs->medium_label);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHCapacity   : %lu KB used / %lu KB total", top + 3, left + 3,
            static_cast<unsigned long>(fs->used_kb), static_cast<unsigned long>(fs->total_kb));
    write_str(buf);
    sprintf(buf, "\033[%d;%dHPartition  : offset=0x%lX size=0x%lX", top + 4, left + 3,
            static_cast<unsigned long>(fs->flash_partition_offset),
            static_cast<unsigned long>(fs->flash_partition_size));
    write_str(buf);
    sprintf(buf, "\033[%d;%dHFlash Logs : %s (wear guard)", top + 6, left + 3,
            node_->config().allow_flash_logging ? "ALLOWED" : "DISABLED");
    write_str(buf);
    if (dialog_fb_[0]) {
        sprintf(buf, "\033[%d;%dH%s%s\033[0m", top + mr.h - 4, left + 3, sgr_warn(), dialog_fb_);
        write_str(buf);
    }
    draw_hdiv(top + mr.h - 3, left, mr.w);
    sprintf(buf, "\033[%d;%dH%s %-64.64s \033[0m", top + mr.h - 2, left + 1, sgr_menubar(),
            "S:Save node.cfg  L:Toggle Flash Logs  F:Format (type FORMAT)  Esc:Close");
    write_str(buf);
}

void Esp32C6Console::render_hw() {
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 16, 70, &mr);
    const int top = mr.top + 1, left = mr.left + 1;
    draw_box(top, left, mr.h, mr.w, "M5Stack Unit C6L Hardware Profile", 1, 1);
    draw_shadow(top, left, mr.h, mr.w);
    const Esp32C6BoardProfile* p = esp32c6_current_board_profile();
    char buf[160];
    sprintf(buf, "\033[%d;%dHBoard      : %s", top + 2, left + 3, p->board_label);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHSPI2/LoRa  : SCK=%u MOSI=%u MISO=%u CS=%u DIO1=%u BUSY=%u",
            top + 4, left + 3, p->spi_sck, p->spi_mosi, p->spi_miso,
            p->lora_cs, p->lora_dio1, p->lora_busy);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHSSD1306    : 64x48 SPI CS=%u DC=%u RST=%u  Page=%d/4",
            top + 5, left + 3, p->oled_cs, p->oled_dc, p->oled_rst, esp32c6_oled_page() + 1);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHPI4IO 0x43 : SDA=%u SCL=%u INT=%u  RGB=GPIO%u Buzzer=GPIO%u",
            top + 6, left + 3, p->i2c_sda, p->i2c_scl, p->pi4io_int, p->rgb_led_pin, p->buzzer_pin);
    write_str(buf);
    sprintf(buf, "\033[%d;%dHGrove ser0 : TX=GPIO%u RX=GPIO%u", top + 7, left + 3,
            p->grove_tx_pin, p->grove_rx_pin);
    write_str(buf);
    draw_hdiv(top + mr.h - 3, left, mr.w);
    sprintf(buf, "\033[%d;%dH%s %-66.66s \033[0m", top + mr.h - 2, left + 1, sgr_menubar(),
            "O:Cycle OLED Page  Esc:Close");
    write_str(buf);
}

void Esp32C6Console::render_bootloader() {
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 13, 72, &mr);
    const int top = mr.top + 1, left = mr.left + 1;
    const int auto_cnt = node_->auto_learned_peer_count();
    draw_box(top, left, mr.h, mr.w,
             (auto_cnt > 0 && !bootloader_pending_) ? "Auto-Learned Peers - ROM Boot" : "Confirm Reset / Bootloader",
             1, 1);
    draw_shadow(top, left, mr.h, mr.w);
    char buf[160];
    OykTuiBackend b = make_tui_backend();
    if (!bootloader_pending_ && auto_cnt > 0) {
        sprintf(buf, "\033[%d;%dH%d auto-learned peer(s) were added this session.", top + 2, left + 2, auto_cnt);
        write_str(buf);
        sprintf(buf, "\033[%d;%dHSave them to config, discard them, or review in Peers?", top + 4, left + 2);
        write_str(buf);
        sprintf(buf, "\033[%d;%dHS:Save & Reboot  D:Discard  R:Review Peers  N/Esc:Cancel", top + 6, left + 2);
        write_str(buf);
        draw_hdiv(top + mr.h - 3, left, mr.w);
        OykTuiButton btns[4];
        btns[0].label = "Save & Exit";  btns[0].enabled = 1;
        btns[1].label = "Discard";      btns[1].enabled = 1;
        btns[2].label = "Review Peers"; btns[2].enabled = 1;
        btns[3].label = "Cancel";       btns[3].enabled = 1;
        oyk_tui_draw_button_row(&b, mr.top + mr.h - 2, mr.left + 2, mr.w - 4,
                                btns, 4, 0, NULL);
    } else {
        sprintf(buf, "\033[%d;%dHPress Y to restart ESP32-C6 Unit C6L, or Esc to cancel.", top + 3, left + 2);
        write_str(buf);
        draw_hdiv(top + mr.h - 3, left, mr.w);
        OykTuiButton btns[2];
        btns[0].label = "Reboot"; btns[0].enabled = 1;
        btns[1].label = "Cancel"; btns[1].enabled = 1;
        oyk_tui_draw_button_row(&b, mr.top + mr.h - 2, mr.left + 2, mr.w - 4,
                                btns, 2, 0, "Y:Reboot  N/Esc:Cancel");
    }
}

void Esp32C6Console::render_about() {
    OykTuiBackend b = make_tui_backend();
    oyk_tui_draw_about_modal(&b, term_rows_, term_cols_, "Platform: ESP32-C6 (M5Stack Unit C6L)");
}

void Esp32C6Console::render_local_term() {
    OykTuiTermLayout tlay;
    oyk_tui_compute_term_layout(term_rows_, term_cols_, &tlay);
    const int body_w = (tlay.cols > 2) ? (tlay.cols - 2) : 1;
    const int body_h = (tlay.body_rows > 0) ? tlay.body_rows : 1;
    write_str("\033[H\033[2J");
    char hdr[160], buf[256];
    sprintf(hdr, " LOCAL TERMINAL | %s [EOL:%s] | Tab/F9:Console",
            lterm_sid_ ? "CONNECTED (LOCAL)" : "DISCONNECTED",
            oyk_term_eol_label(&lterm_eol_));
    sprintf(buf, "\033[1;1H%s%-*.*s\033[0m", sgr_header(), tlay.cols, tlay.cols, hdr);
    write_str(buf);
    int start = (lterm_line_count_ > body_h) ? (lterm_line_count_ - body_h) : 0;
    for (int i = 0; i < body_h && (start + i) < lterm_line_count_; ++i) {
        sprintf(buf, "\033[%d;2H%-*.*s", tlay.body_top + 1 + i, body_w, body_w,
                lterm_lines_[start + i]);
        write_str(buf);
    }
    const char* hk_bar = oyk_term_hotkey_bar_text(1, lterm_ctrl_a_);
    sprintf(buf, "\033[%d;1H%s %-*.*s \033[0m",
            tlay.hotkey_row + 1, sgr_menubar(), tlay.cols - 2, tlay.cols - 2, hk_bar);
    write_str(buf);
    sprintf(buf, "\033[%d;1H> %s", tlay.input_row + 1, lterm_input_);
    write_str(buf);
}

void Esp32C6Console::render_local_term_help() {
    OykTuiBackend b = make_tui_backend();
    oyk_tui_draw_term_help_modal(&b, term_rows_, term_cols_);
}

void Esp32C6Console::render_confirm_format_modal() {
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 9, 54, &mr);
    const int top = mr.top + 1, left = mr.left + 1;
    draw_box(top, left, mr.h, mr.w, "Confirm Format 16MB LittleFS", 1, 1);
    draw_shadow(top, left, mr.h, mr.w);
    char buf[128];
    sprintf(buf, "\033[%d;%dHType FORMAT to erase storage:", top + 2, left + 3);
    write_str(buf);
    sprintf(buf, "\033[%d;%dH> %s%s\033[0m", top + 4, left + 3, sgr_sel(), prompt_buf_);
    write_str(buf);
}

void Esp32C6Console::render_text_prompt() {
    OykTuiRect mr;
    oyk_tui_center_modal(term_rows_, term_cols_, 8, 56, &mr);
    const int top = mr.top + 1, left = mr.left + 1;
    draw_box(top, left, mr.h, mr.w, prompt_label_[0] ? prompt_label_ : "Edit Setting", 1, 1);
    draw_shadow(top, left, mr.h, mr.w);
    char buf[128];
    sprintf(buf, "\033[%d;%dH> %s%s\033[0m", top + 3, left + 3, sgr_sel(), prompt_buf_);
    write_str(buf);
}

void Esp32C6Console::tick(oyk_u32 now_ms) {
    now_ms_ = now_ms;
    if (esc_state_ == 1 && (now_ms - last_esc_ms_) >= 150UL) {
        esc_state_ = 0;
        handle_key_code(C6L_ESC);
    }
    if (bootloader_pending_ && node_ && (node_->disconnecting_count() == 0 || (now_ms - bootloader_start_ms_) >= 3000UL)) {
        bootloader_pending_ = 0;
        esp32c6_board_enter_bootloader();
    }
}

void Esp32C6Console::feed_input(const oyk_u8* data, size_t len) {
    if (!data) return;
    for (size_t i = 0; i < len; ++i) handle_char(data[i]);
}

void Esp32C6Console::handle_char(oyk_u8 ch) {
    if (esc_state_ == 3) {
        x10_buf_[x10_len_++] = ch;
        if (x10_len_ == 3) {
            int btn = static_cast<int>(x10_buf_[0]) - 32;
            int col1 = static_cast<int>(x10_buf_[1]) - 32;
            int row1 = static_cast<int>(x10_buf_[2]) - 32;
            esc_state_ = 0; x10_len_ = 0;
            handle_mouse_event(row1, col1, ((btn & 3) == 2) ? 2 : 0);
        }
        return;
    }
    if (esc_state_ == 1) {
        if (ch == '[') { esc_state_ = 2; esc_len_ = 0; esc_buf_[0] = '\0'; return; }
        esc_state_ = 0;
        handle_key_code(C6L_ESC);
        handle_char(ch);
        return;
    }
    if (esc_state_ == 2) {
        if (esc_len_ == 0 && ch == 'M') { esc_state_ = 3; x10_len_ = 0; return; }
        if (esc_len_ + 1 < sizeof(esc_buf_)) { esc_buf_[esc_len_++] = static_cast<char>(ch); esc_buf_[esc_len_] = '\0'; }
        if ((ch >= 'A' && ch <= 'Z') || ch == '~' || ch == 'c' || ch == 'm' || ch == 'R') {
            esc_state_ = 0;
            if (ch == 'c' && esc_buf_[0] == '?') handle_da_csi();
            else if (ch == 'R') handle_cpr_csi();
            else if (ch == 'A') handle_key_code(C6L_UP);
            else if (ch == 'B') handle_key_code(C6L_DOWN);
            else if (ch == 'C') handle_key_code(C6L_RIGHT);
            else if (ch == 'D') handle_key_code(C6L_LEFT);
            else if (ch == 'H') handle_key_code(C6L_HOME);
            else if (ch == 'F') handle_key_code(C6L_END);
            else if ((ch == 'M' || ch == 'm') && esc_buf_[0] == '<') {
                int b = 0, c1 = 1, r1 = 1;
                if (sscanf(esc_buf_ + 1, "%d;%d;%d", &b, &c1, &r1) == 3 && ch == 'M') {
                    int kind = (b == 64 || b == 65) ? ((b == 64) ? 3 : 4) : (((b & 3) == 2) ? 2 : 0);
                    handle_mouse_event(r1, c1, kind);
                }
            } else if (strcmp(esc_buf_, "21~") == 0) handle_key_code(C6L_F10);
            else if (strcmp(esc_buf_, "20~") == 0) handle_key_code(C6L_F9);
        }
        return;
    }
    if (ch == 0x1B) { esc_state_ = 1; last_esc_ms_ = now_ms_; return; }
    if (ch == '\r' || ch == '\n') handle_key_code(C6L_ENTER);
    else if (ch == '\t') handle_key_code(C6L_TAB);
    else if (ch == 0x7F || ch == 0x08) handle_key_code(C6L_BS);
    else handle_key_code(static_cast<int>(ch));
}

void Esp32C6Console::handle_da_csi() {
    if (term_type_ < C6L_TERM_XTERM256) term_type_ = C6L_TERM_XTERM256;
    if (!mouse_enabled_) { mouse_enabled_ = 1; write_str("\033[?1000h\033[?1006h"); }
    render();
}

void Esp32C6Console::handle_cpr_csi() {
    int r = 0, c = 0;
    if (sscanf(esc_buf_, "%d;%dR", &r, &c) == 2) {
        if (r < 24) r = 24;
        if (r > 50) r = 50;
        if (c < 80) c = 80;
        if (c > 132) c = 132;
        if (r != term_rows_ || c != term_cols_) { term_rows_ = r; term_cols_ = c; render(); }
    }
}

void Esp32C6Console::handle_mouse_event(int row1, int col1, int kind) {
    if (view_ == C6L_VIEW_CONSOLE && kind == 0 && row1 == 1 && col1 <= 8) {
        view_ = C6L_VIEW_MENU; menu_sel_ = 0; render(); return;
    }
    if (view_ == C6L_VIEW_MENU) {
        if (kind == 2) { view_ = C6L_VIEW_CONSOLE; render(); return; }
        if (kind == 4) { menu_sel_ = (menu_sel_ + 1) % 13; render(); return; }
        if (kind == 3) { menu_sel_ = (menu_sel_ + 12) % 13; render(); return; }
    }
    if (kind == 0 && prompt_kind_ == C6L_PROMPT_NONE && view_ == C6L_VIEW_SETTINGS) {
        OykTuiRect mr; oyk_tui_center_modal(term_rows_, term_cols_, 21, 74, &mr);
        if (oyk_tui_hit_close_btn(&mr, row1 - 1, col1 - 1)) { view_ = C6L_VIEW_CONSOLE; render(); }
    }
}

void Esp32C6Console::handle_key_code(int key) {
    if (prompt_kind_ != C6L_PROMPT_NONE) { handle_prompt_key(key); return; }
    switch (view_) {
        case C6L_VIEW_CONSOLE:    handle_console_key(key); break;
        case C6L_VIEW_MENU:       handle_menu_key(key); break;
        case C6L_VIEW_SETTINGS:   handle_settings_key(key); break;
        case C6L_VIEW_FILES:      handle_files_key(key); break;
        case C6L_VIEW_RIG:        handle_rig_key(key); break;
        case C6L_VIEW_LORA:       handle_lora_key(key); break;
        case C6L_VIEW_STORAGE:    handle_storage_key(key); break;
        case C6L_VIEW_BOOTLOADER: handle_bootloader_key(key); break;
        case C6L_VIEW_LOCAL_TERM: handle_local_term_key(key); break;
        case C6L_VIEW_HW:
        case C6L_VIEW_ABOUT:
            if (key == C6L_ESC || key == C6L_ENTER || key == 'q' || key == 'Q') {
                view_ = C6L_VIEW_CONSOLE; render();
            } else if (view_ == C6L_VIEW_HW && (key == 'o' || key == 'O')) {
                esp32c6_oled_next_page(); render();
            }
            break;
    }
}

void Esp32C6Console::exec_menu_hotkey(int key) {
    if (key == 'a' || key == 'A') { if (node_) node_->set_accept_incoming(1); strcpy(status_fb_, "Incoming ALLOWED."); render(); }
    else if (key == 'd' || key == 'D') { if (node_) node_->set_accept_incoming(0); strcpy(status_fb_, "Incoming DENIED."); render(); }
    else if (key == 'l' || key == 'L') { view_ = C6L_VIEW_LOCAL_TERM; render(); }
    else if (key == 'f' || key == 'F') { fm_preview_active_ = 0; files_focus_btn_ = 0; refresh_files_lists(); view_ = C6L_VIEW_FILES; render(); }
    else if (key == 'r' || key == 'R') { view_ = C6L_VIEW_RIG; render(); }
    else if (key == 's' || key == 'S') open_settings(0);
    else if (key == 't' || key == 'T') open_terminate_dialog();
    else if (key == 'b' || key == 'B') open_ban_dialog();
    else if (key == 'k' || key == 'K') { view_ = C6L_VIEW_LORA; render(); }
    else if (key == 'g' || key == 'G') { dialog_fb_[0] = '\0'; view_ = C6L_VIEW_STORAGE; render(); }
    else if (key == 'p' || key == 'P') { view_ = C6L_VIEW_HW; render(); }
    else if (key == '?') { view_ = C6L_VIEW_ABOUT; render(); }
    else if (key == 'u' || key == 'U') { bootloader_pending_ = 0; view_ = C6L_VIEW_BOOTLOADER; render(); }
}

void Esp32C6Console::handle_console_key(int key) {
    if (key == 0x0E || key == C6L_F10) { view_ = C6L_VIEW_MENU; menu_sel_ = 0; render(); }
    else if (key == C6L_TAB || key == C6L_F9) { view_ = C6L_VIEW_LOCAL_TERM; render(); }
    else if (key == 0x0C) { send_probes(); render(); }
}

void Esp32C6Console::handle_menu_key(int key) {
    static const int kAct[13] = { 'a','d','l','f','r','s','t','b','k','g','p','?','u' };
    if (key == C6L_ESC || key == C6L_F10 || key == 0x0E || key == 'x' || key == 'X') {
        view_ = C6L_VIEW_CONSOLE; render(); return;
    }
    if (key == C6L_UP) { menu_sel_ = (menu_sel_ + 12) % 13; render(); return; }
    if (key == C6L_DOWN) { menu_sel_ = (menu_sel_ + 1) % 13; render(); return; }
    int chosen = 0;
    if (key == C6L_ENTER) chosen = kAct[menu_sel_];
    else {
        for (int i = 0; i < 13; ++i) {
            if (key == kAct[i] || (kAct[i] >= 'a' && kAct[i] <= 'z' && key == (kAct[i] - 32))) {
                chosen = kAct[i];
                break;
            }
        }
    }
    if (!chosen) return;
    view_ = C6L_VIEW_CONSOLE;
    exec_menu_hotkey(chosen);
}

void Esp32C6Console::open_settings(int tab) {
    if (node_) draft_ = node_->config();
    settings_tab_ = (tab >= 0 && tab < 8) ? tab : 0;
    settings_sel_ = 0;
    theme_role_sel_ = 0;
    theme_preview_dirty_ = 0;
    dialog_fb_[0] = '\0';
    view_ = C6L_VIEW_SETTINGS; render();
}

void Esp32C6Console::open_wizard() {
    oyk_gui_wiz_init(&wiz_, &draft_, OYK_DRV_ALL);
    strcpy(wiz_id_, "axudp1");
    strcpy(wiz_desc_, "Secondary AXUDP Link");
    strcpy(wiz_host_, "0.0.0.0");
    strcpy(wiz_port_, "10094");
    strcpy(wiz_mask_, "255.255.255.0");
    wiz_peer_[0] = '\0';
    wiz_same_iface_ = 0; wiz_lpol_ = 0; wiz_field_ = 0; wiz_btn_ = 1;
    prompt_kind_ = C6L_PROMPT_ADD_WIZARD; render();
}

void Esp32C6Console::open_edit_iface(int idx) {
    if (idx < 0 || idx >= draft_.iface_count) idx = 0;
    edit_iface_idx_ = idx;
    edit_iface_buf_ = draft_.ifaces[idx];
    edit_iface_field_ = 0;
    edit_btn_ = 0;
    prompt_kind_ = C6L_PROMPT_EDIT_IFACE; render();
}

void Esp32C6Console::open_edit_peer(int idx) {
    if (idx < 0 || idx >= draft_.peer_count) idx = 0;
    edit_peer_idx_ = idx;
    edit_peer_buf_ = draft_.peers[idx];
    edit_peer_field_ = 0;
    edit_btn_ = 0;
    prompt_kind_ = C6L_PROMPT_EDIT_PEER; render();
}

void Esp32C6Console::open_terminate_dialog() {
    term_sel_ = 0; prompt_kind_ = C6L_PROMPT_TERM_SID; render();
}

void Esp32C6Console::open_ban_dialog() {
    ban_sel_ = 0; ban_call_[0] = '\0'; strcpy(ban_reason_, "Operator ban");
    prompt_kind_ = C6L_PROMPT_BAN_CALL; render();
}

void Esp32C6Console::handle_settings_key(int key) {
    if (key == C6L_ESC) { theme_preview_dirty_ = 0; view_ = C6L_VIEW_CONSOLE; render(); return; }
    if (key >= '1' && key <= '8') { settings_tab_ = key - '1'; settings_sel_ = 0; render(); return; }
    if (key == C6L_TAB) { settings_tab_ = (settings_tab_ + 1) % 8; settings_sel_ = 0; render(); return; }
    if (key == C6L_LEFT || key == C6L_RIGHT) {
        if (settings_tab_ == 7) {
            int dark_cur = (strcmp(draft_.theme_name, "light") == 0) ? 0 : 1;
            strcpy(draft_.theme_name, dark_cur ? "light" : "dark");
            theme_preview_dirty_ = 1;
            sprintf(dialog_fb_, "Previewing '%s'", draft_.theme_name);
        } else {
            settings_tab_ = (key == C6L_RIGHT) ? ((settings_tab_ + 1) % 8) : ((settings_tab_ + 7) % 8);
            settings_sel_ = 0;
        }
        render();
        return;
    }
    if (settings_tab_ == 7) {
        if (key == C6L_UP && theme_role_sel_ > 0) { --theme_role_sel_; render(); return; }
        if (key == C6L_DOWN && theme_role_sel_ + 1 < OYK_THEME_COLOR_COUNT) { ++theme_role_sel_; render(); return; }
    } else {
        if (key == C6L_UP && settings_sel_ > 0) { --settings_sel_; render(); return; }
        if (key == C6L_DOWN) { ++settings_sel_; render(); return; }
    }
    if (settings_tab_ == 3 && (key == 'n' || key == 'N' || key == 'a' || key == 'A')) { open_wizard(); return; }
    if (settings_tab_ == 3 && (key == 'e' || key == 'E' || key == C6L_ENTER)) { open_edit_iface(settings_sel_); return; }
    if (settings_tab_ == 4 && (key == 'e' || key == 'E' || key == C6L_ENTER)) { open_edit_peer(settings_sel_); return; }
    if (key == 's' || key == 'S') {
        OykConfigApplyResult ar;
        if (node_ && node_->apply_config(&draft_, 1, &ar)) {
            if (bridge_) bridge_->reconfig();
            strcpy(status_fb_, "Settings saved and applied.");
        }
        theme_preview_dirty_ = 0;
        view_ = C6L_VIEW_CONSOLE; render();
    }
}

void Esp32C6Console::handle_files_key(int key) {
    OykGuiFilePane* p = (file_mgr_.active_pane == 0) ? &file_mgr_.inc : &file_mgr_.files;
    if (fm_preview_active_) {
        if (key == C6L_ESC || key == C6L_ENTER || key == 'q' || key == 'Q' || key == 'v' || key == 'V') {
            fm_preview_active_ = 0;
            render();
        }
        return;
    }
    if (key == C6L_ESC) { view_ = C6L_VIEW_CONSOLE; render(); return; }
    if (key == C6L_TAB || key == C6L_LEFT || key == C6L_RIGHT) {
        file_mgr_.active_pane = 1 - file_mgr_.active_pane; render(); return;
    }
    if (key == '1') { file_mgr_.active_pane = 0; render(); return; }
    if (key == '2') { file_mgr_.active_pane = 1; render(); return; }
    if (key == C6L_UP && p->count > 0 && p->sel > 0) { --p->sel; render(); return; }
    if (key == C6L_DOWN && p->count > 0 && p->sel + 1 < p->count) { ++p->sel; render(); return; }
    if (key == C6L_BS || key == '<' || key == '-') {
        oyk_gui_filemgr_up(&file_mgr_, file_mgr_.active_pane);
        render(); return;
    }
    if (key == C6L_ENTER) {
        int act = oyk_gui_filemgr_activate(&file_mgr_, file_mgr_.active_pane);
        if (act == 0 && p->count > 0 && p->sel >= 0 && p->sel < p->count && !p->entries[p->sel].is_dir) {
            int trunc = 0;
            if (oyk_gui_filemgr_view(&file_mgr_, fm_preview_buf_, sizeof(fm_preview_buf_), &trunc)) {
                fm_preview_active_ = 1;
            }
        }
        render(); return;
    }
    if (key == 'a' || key == 'A') { int ex = 0; oyk_gui_filemgr_approve(&file_mgr_, 1, &ex); render(); return; }
    if (key == 'u' || key == 'U') {
        strncpy(prompt_label_, "Upload local file path", sizeof(prompt_label_) - 1);
        prompt_buf_[0] = '\0'; prompt_cursor_ = 0; prompt_kind_ = C6L_PROMPT_FM_UPLOAD; render(); return;
    }
    if (key == 'n' || key == 'N') {
        strncpy(prompt_label_, "New subdirectory name", sizeof(prompt_label_) - 1);
        prompt_buf_[0] = '\0'; prompt_cursor_ = 0; prompt_kind_ = C6L_PROMPT_FM_MKDIR; render(); return;
    }
    if (key == 'm' || key == 'M') {
        const char* cur = (p->count > 0 && p->sel >= 0 && p->sel < p->count && !p->entries[p->sel].is_parent)
                              ? p->entries[p->sel].name : "";
        if (cur[0]) {
            strncpy(prompt_label_, "Rename entry to", sizeof(prompt_label_) - 1);
            strncpy(prompt_buf_, cur, sizeof(prompt_buf_) - 1);
            prompt_buf_[sizeof(prompt_buf_) - 1] = '\0';
            prompt_cursor_ = static_cast<int>(strlen(prompt_buf_));
            prompt_kind_ = C6L_PROMPT_FM_RENAME; render();
        }
        return;
    }
    if (key == 'd' || key == 'D') { oyk_gui_filemgr_delete(&file_mgr_); render(); return; }
    if (key == 'v' || key == 'V') {
        int trunc = 0;
        if (oyk_gui_filemgr_view(&file_mgr_, fm_preview_buf_, sizeof(fm_preview_buf_), &trunc)) fm_preview_active_ = 1;
        render(); return;
    }
    if (key == 'r' || key == 'R') {
        oyk_gui_filemgr_refresh(&file_mgr_);
        strcpy(file_mgr_.feedback, "Refreshed files lists.");
        render();
    }
}
void Esp32C6Console::handle_rig_key(int key) { if (key == C6L_ESC) { view_ = C6L_VIEW_CONSOLE; render(); } }
void Esp32C6Console::handle_lora_key(int key) { if (key == C6L_ESC) { view_ = C6L_VIEW_CONSOLE; render(); } }

void Esp32C6Console::handle_storage_key(int key) {
    if (key == C6L_ESC) { view_ = C6L_VIEW_CONSOLE; render(); return; }
    if (key == 'l' || key == 'L') {
        OykConfig cfg = node_->config();
        cfg.allow_flash_logging = cfg.allow_flash_logging ? 0 : 1;
        OykConfigApplyResult ar; node_->apply_config(&cfg, 0, &ar);
        strcpy(dialog_fb_, cfg.allow_flash_logging ? "Flash logging ALLOWED." : "Flash logging DISABLED.");
        render();
    } else if (key == 's' || key == 'S') {
        if (oyk_config_save(&node_->config(), "node.cfg")) strcpy(dialog_fb_, "Saved node.cfg to Flash.");
        else strcpy(dialog_fb_, "Failed to save node.cfg.");
        render();
    } else if (key == 'f' || key == 'F') {
        prompt_buf_[0] = '\0'; prompt_cursor_ = 0;
        prompt_kind_ = C6L_PROMPT_CONFIRM_FORMAT; render();
    }
}

void Esp32C6Console::handle_bootloader_key(int key) {
    if (key == C6L_ESC || key == 'n' || key == 'N') { view_ = C6L_VIEW_CONSOLE; render(); return; }
    if (key == 's' || key == 'S') {
        char err[64]; node_->persist_auto_learned_peers(err, sizeof(err));
        bootloader_pending_ = 1; bootloader_start_ms_ = now_ms_; render();
    } else if (key == 'd' || key == 'D') {
        node_->discard_auto_learned_peers();
        bootloader_pending_ = 1; bootloader_start_ms_ = now_ms_; render();
    } else if (key == 'r' || key == 'R') {
        open_settings(4);
    } else if (key == 'y' || key == 'Y') {
        bootloader_pending_ = 1; bootloader_start_ms_ = now_ms_; render();
    }
}

void Esp32C6Console::handle_local_term_key(int key) {
    if (lterm_help_open_) {
        if (key == C6L_ESC || key == C6L_ENTER || key == ' ') {
            lterm_help_open_ = 0;
            render();
        }
        return;
    }
    if (key == C6L_TAB || key == C6L_F9) { view_ = C6L_VIEW_CONSOLE; render(); return; }
    if (lterm_ctrl_a_) {
        lterm_ctrl_a_ = 0;
        int cmd = oyk_term_decode_ctrl_a_cmd(key);
        if (cmd == OYK_TCMD_HELP) { lterm_help_open_ = 1; render(); return; }
        if (cmd == OYK_TCMD_CONSOLE) { view_ = C6L_VIEW_CONSOLE; render(); return; }
        if (cmd == OYK_TCMD_CYCLE_EOL) { oyk_term_eol_cycle_mode(&lterm_eol_); render(); return; }
        if (cmd == OYK_TCMD_BBS && node_) {
            char err[64]; int loc = 0;
            if (node_->open_local_terminal(-1, "BBS", &lterm_sid_, NULL, &loc, err, sizeof(err))) {
                append_lterm_line("*** Connected locally to BBS");
            }
            render(); return;
        }
        if (cmd == OYK_TCMD_CHAT && node_) {
            char err[64]; int loc = 0;
            if (node_->open_local_terminal(-1, "CHAT", &lterm_sid_, NULL, &loc, err, sizeof(err))) {
                append_lterm_line("*** Connected locally to CHAT");
            }
            render(); return;
        }
        render(); return;
    }
    if (key == 0x01) { lterm_ctrl_a_ = 1; return; }
    if (key == C6L_ESC) { view_ = C6L_VIEW_CONSOLE; render(); return; }
    if (key == C6L_ENTER) {
        if (lterm_sid_ && node_) {
            lterm_input_[lterm_input_len_] = '\0';
            char tx[112];
            size_t eol_len = 0;
            const char* eol_s = oyk_term_eol_tx_bytes(&lterm_eol_, &eol_len);
            strcpy(tx, lterm_input_);
            if (eol_s && eol_len > 0) memcpy(tx + lterm_input_len_, eol_s, eol_len);
            node_->send_local_bytes(lterm_sid_, reinterpret_cast<const oyk_u8*>(tx),
                                    static_cast<size_t>(lterm_input_len_) + eol_len);
        }
        lterm_input_len_ = 0; lterm_input_[0] = '\0'; render();
    } else if (key == C6L_BS && lterm_input_len_ > 0) {
        lterm_input_[--lterm_input_len_] = '\0'; render();
    } else if (key >= 0x20 && key < 0x7F && lterm_input_len_ < 90) {
        lterm_input_[lterm_input_len_++] = static_cast<char>(key);
        lterm_input_[lterm_input_len_] = '\0'; render();
    }
}

void Esp32C6Console::handle_prompt_key(int key) {
    if (key == C6L_ESC) { prompt_kind_ = C6L_PROMPT_NONE; render(); return; }
    if (prompt_kind_ == C6L_PROMPT_ADD_WIZARD) {
        if (key == 'b' || key == 'B') { if (wiz_.step > 0) --wiz_.step; wiz_btn_ = 1; render(); return; }
        if (wiz_.step == 0 && key >= '1' && key <= '5') { wiz_.selected_driver_idx = key - '1'; render(); return; }
        if (wiz_.step == 2 && key == ' ') { wiz_same_iface_ = 1 - wiz_same_iface_; render(); return; }
        if (key == C6L_LEFT) {
            if (wiz_btn_ > (wiz_.step == 0 ? 1 : 0)) --wiz_btn_;
            render(); return;
        }
        if (key == C6L_RIGHT) {
            if (wiz_btn_ < 2) ++wiz_btn_;
            render(); return;
        }
        if (key == C6L_ENTER) {
            if (wiz_btn_ == 0 && wiz_.step > 0) { --wiz_.step; render(); return; }
            if (wiz_btn_ == 2) { prompt_kind_ = C6L_PROMPT_NONE; render(); return; }
            if (wiz_.step < 3) { ++wiz_.step; wiz_btn_ = 1; render(); return; }
            prompt_kind_ = C6L_PROMPT_NONE; render();
        }
    } else if (prompt_kind_ == C6L_PROMPT_EDIT_IFACE) {
        if (key == ' ') { edit_iface_buf_.allow_same_interface = 1 - edit_iface_buf_.allow_same_interface; render(); return; }
        if (key == C6L_LEFT || key == C6L_RIGHT) { edit_btn_ = 1 - edit_btn_; render(); return; }
        if (key == 's' || key == 'S' || (key == C6L_ENTER && edit_btn_ == 0)) {
            if (edit_iface_idx_ >= 0 && edit_iface_idx_ < draft_.iface_count) draft_.ifaces[edit_iface_idx_] = edit_iface_buf_;
            prompt_kind_ = C6L_PROMPT_NONE; render();
        } else if (key == C6L_ENTER && edit_btn_ == 1) {
            prompt_kind_ = C6L_PROMPT_NONE; render();
        }
    } else if (prompt_kind_ == C6L_PROMPT_EDIT_PEER) {
        if (key == C6L_LEFT || key == C6L_RIGHT) { edit_btn_ = 1 - edit_btn_; render(); return; }
        if (key == 's' || key == 'S' || (key == C6L_ENTER && edit_btn_ == 0)) {
            if (edit_peer_idx_ >= 0 && edit_peer_idx_ < draft_.peer_count) draft_.peers[edit_peer_idx_] = edit_peer_buf_;
            prompt_kind_ = C6L_PROMPT_NONE; render();
        } else if (key == C6L_ENTER && edit_btn_ == 1) {
            prompt_kind_ = C6L_PROMPT_NONE; render();
        }
    } else if (prompt_kind_ == C6L_PROMPT_FM_UPLOAD ||
               prompt_kind_ == C6L_PROMPT_FM_MKDIR ||
               prompt_kind_ == C6L_PROMPT_FM_RENAME) {
        if (key == C6L_ENTER) {
            Esp32C6PromptKind pk = prompt_kind_;
            prompt_kind_ = C6L_PROMPT_NONE;
            int ex = 0;
            if (pk == C6L_PROMPT_FM_UPLOAD && prompt_buf_[0]) oyk_gui_filemgr_upload(&file_mgr_, prompt_buf_, 1, &ex);
            else if (pk == C6L_PROMPT_FM_MKDIR && prompt_buf_[0]) oyk_gui_filemgr_mkdir(&file_mgr_, prompt_buf_);
            else if (pk == C6L_PROMPT_FM_RENAME && prompt_buf_[0]) oyk_gui_filemgr_rename(&file_mgr_, prompt_buf_);
            prompt_buf_[0] = '\0'; prompt_cursor_ = 0; render();
        } else if (key == C6L_BS && prompt_cursor_ > 0) {
            prompt_buf_[--prompt_cursor_] = '\0'; render();
        } else if (key >= 0x20 && key < 0x7F && prompt_cursor_ < 90) {
            prompt_buf_[prompt_cursor_++] = static_cast<char>(key);
            prompt_buf_[prompt_cursor_] = '\0'; render();
        }
    } else if (prompt_kind_ == C6L_PROMPT_TERM_SID) {
        if (key == C6L_ENTER) {
            for (int i = 0, r = 0; i < node_->max_sessions(); ++i) {
                const OykSession& s = node_->session_slot(i);
                if (s.state == OYK_SESS_CONNECTED && r++ == term_sel_) {
                    char fb[64]; node_->terminate_session(s.id, fb, sizeof(fb));
                    break;
                }
            }
            prompt_kind_ = C6L_PROMPT_NONE; render();
        }
    } else if (prompt_kind_ == C6L_PROMPT_BAN_CALL) {
        if (key == C6L_ENTER && ban_call_[0]) {
            char fb[64];
            node_->ban_callsign(ban_call_, 1U, "Operator ban", "", fb, sizeof(fb));
            ban_call_[0] = '\0'; render();
        } else if (key == C6L_BS && ban_call_[0]) {
            ban_call_[strlen(ban_call_) - 1] = '\0'; render();
        } else if (key >= 0x20 && key < 0x7F && strlen(ban_call_) < 10) {
            size_t l = strlen(ban_call_);
            ban_call_[l] = static_cast<char>(toupper(key));
            ban_call_[l + 1] = '\0'; render();
        }
    } else if (prompt_kind_ == C6L_PROMPT_CONFIRM_FORMAT) {
        if (key == C6L_ENTER) {
            if (esp32c6_fs_format_storage(prompt_buf_)) strcpy(dialog_fb_, "16MB Flash LittleFS formatted.");
            else strcpy(dialog_fb_, "Format aborted (type exact FORMAT).");
            prompt_kind_ = C6L_PROMPT_NONE; render();
        } else if (key == C6L_BS && prompt_cursor_ > 0) {
            prompt_buf_[--prompt_cursor_] = '\0'; render();
        } else if (key >= 0x20 && key < 0x7F && prompt_cursor_ < 30) {
            prompt_buf_[prompt_cursor_++] = static_cast<char>(key);
            prompt_buf_[prompt_cursor_] = '\0'; render();
        }
    }
}

void Esp32C6Console::refresh_files_lists() {
    const char* froot = (node_ && node_->config().bbs_files_path[0])
                            ? node_->config().bbs_files_path : "files";
    if (strcmp(file_mgr_.files.root, froot) != 0 || !file_mgr_.inc.root[0]) {
        oyk_gui_filemgr_init(&file_mgr_, "incoming", froot);
    } else {
        oyk_gui_filemgr_refresh(&file_mgr_);
    }
}

void Esp32C6Console::append_lterm_line(const char* line) {
    if (!line) return;
    if (lterm_line_count_ >= C6L_LTERM_HIST_ROWS) {
        for (int i = 0; i + 1 < C6L_LTERM_HIST_ROWS; ++i) {
            strcpy(lterm_lines_[i], lterm_lines_[i + 1]);
        }
        lterm_line_count_ = C6L_LTERM_HIST_ROWS - 1;
    }
    strncpy(lterm_lines_[lterm_line_count_], line, 127);
    lterm_lines_[lterm_line_count_][127] = '\0';
    ++lterm_line_count_;
}

void Esp32C6Console::on_local_term_event(oyk_u32 /*sid*/, int /*event_type*/,
                                         const oyk_u8* data, size_t len, const char* /*reason*/) {
    if (data && len > 0) {
        char tmp[128];
        size_t cp = (len < sizeof(tmp) - 1) ? len : sizeof(tmp) - 1;
        memcpy(tmp, data, cp);
        tmp[cp] = '\0';
        append_lterm_line(tmp);
    }
    if (view_ == C6L_VIEW_LOCAL_TERM) render();
}
