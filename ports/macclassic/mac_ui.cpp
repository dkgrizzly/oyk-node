/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "mac_ui.hpp"
#include "mac_capture.hpp"
#include "oyk_rig.hpp"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <MacTypes.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <Dialogs.h>
#include <Events.h>
#include <ToolUtils.h>
#include <Devices.h>
#include <Files.h>
#include <StandardFile.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Gestalt.h>
#include <LowMem.h>
#if defined(__has_include)
#  if __has_include(<Appearance.h>)
#    include <Appearance.h>
#    define OYK_HAVE_APPEARANCE_H 1
#  endif
#endif

#ifndef gdDevType
#  define gdDevType 0
#endif

static void mu_copy(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1U;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void mu_c2p(const char* src, Str255 dst) {
    if (!src) { dst[0] = 0; return; }
    size_t n = strlen(src);
    if (n > 255U) n = 255U;
    dst[0] = static_cast<unsigned char>(n);
    if (n > 0U) memcpy(dst + 1, src, n);
}

static void mu_p2c(ConstStr255Param src, char* dst, size_t cap) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = static_cast<size_t>(src[0]);
    if (n >= cap) n = cap - 1U;
    if (n > 0U) memcpy(dst, src + 1, n);
    dst[n] = '\0';
}

enum McHitAction {
    MC_HIT_NONE = 0,
    MC_HIT_ACTION,
    MC_HIT_SET_SCREEN,
    MC_HIT_CLOSE_MODAL,
    MC_HIT_TAB,
    MC_HIT_WIZ_DRIVER,
    MC_HIT_WIZ_NEXT,
    MC_HIT_WIZ_BACK,
    MC_HIT_TOGGLE_INCOMING,
    MC_HIT_TERM_SEND,
    MC_HIT_TERM_HELP,
    MC_HIT_FILE_UPLOAD,
    MC_HIT_THEME_NEXT,
    MC_HIT_THEME_PREV,
    MC_HIT_THEME_APPLY,
    MC_HIT_THEME_CANCEL,
    MC_HIT_QUIT_NOW
};

struct McHitZone {
    Rect rect;
    int kind;
    int arg;
};

struct OykMacUiState {
    OykPortableNode* node;
    OykMacNetState* net;
    WindowPtr main_win;
    int scr_w;
    int scr_h;
    int has_color;
    int running;
    int active_screen; /* OykGuiScreenId */
    int settings_tab;  /* 0..6 */
    int sel_iface;
    int sel_peer;
    int sel_conn;
    int sel_ban;
    int sel_theme_role;
    int show_term_help;
    int ctrl_a_pending;

    OykConfig draft_cfg;
    OykGuiAddIfaceWizard wiz;
    OykGuiThemeCtrl theme_ctrl;
    OykThemeConfig active_theme;
    OykTermEolParser term_eol;
    char term_lines[28][128];
    int term_line_count;
    char term_input[96];
    char status_msg[96];

    McHitZone hits[32];
    int hit_count;
    char fmt_buf[256];
    int console_draw_count;
    int modal_draw_count;
    oyk_u32 last_console_bg;
    oyk_u32 last_modal_bg;
};

static oyk_u32 mc_u32_from_rgb(const RGBColor& c) {
    oyk_u32 r = static_cast<oyk_u32>((c.red >> 8) & 0xFFU);
    oyk_u32 g = static_cast<oyk_u32>((c.green >> 8) & 0xFFU);
    oyk_u32 b = static_cast<oyk_u32>((c.blue >> 8) & 0xFFU);
    return (r << 16) | (g << 8) | b;
}

static oyk_u32 mc_luma(oyk_u32 rgb) {
    oyk_u32 r = (rgb >> 16) & 0xFFUL;
    oyk_u32 g = (rgb >> 8) & 0xFFUL;
    oyk_u32 b = rgb & 0xFFUL;
    return (r * 299UL + g * 587UL + b * 114UL) / 1000UL;
}

void oyk_mac_query_os_native_theme(void* win_ptr, int has_color_qd, OykThemeConfig* out_pal) {
    if (!out_pal) return;
    memset(out_pal, 0, sizeof(*out_pal));
    mu_copy(out_pal->name, sizeof(out_pal->name), "native");
    out_pal->is_dark_base = 0;

    if (!has_color_qd) {
        out_pal->colors[OYK_TR_BG]           = 0xFFFFFFUL;
        out_pal->colors[OYK_TR_FG]           = 0x000000UL;
        out_pal->colors[OYK_TR_PANEL_BG]     = 0xFFFFFFUL;
        out_pal->colors[OYK_TR_PANEL_FG]     = 0x000000UL;
        out_pal->colors[OYK_TR_HEADER_BG]    = 0x000000UL;
        out_pal->colors[OYK_TR_HEADER_FG]    = 0xFFFFFFUL;
        out_pal->colors[OYK_TR_STATUS_BG]    = 0xFFFFFFUL;
        out_pal->colors[OYK_TR_STATUS_FG]    = 0x000000UL;
        out_pal->colors[OYK_TR_BORDER]       = 0x000000UL;
        out_pal->colors[OYK_TR_ACCENT]       = 0x000000UL;
        out_pal->colors[OYK_TR_SELECTION_BG] = 0x000000UL;
        out_pal->colors[OYK_TR_SELECTION_FG] = 0xFFFFFFUL;
        out_pal->colors[OYK_TR_BUTTON_BG]    = 0xFFFFFFUL;
        out_pal->colors[OYK_TR_BUTTON_FG]    = 0x000000UL;
        out_pal->colors[OYK_TR_INPUT_BG]     = 0xFFFFFFUL;
        out_pal->colors[OYK_TR_INPUT_FG]     = 0x000000UL;
        out_pal->colors[OYK_TR_OK]           = 0x000000UL;
        out_pal->colors[OYK_TR_WARN]         = 0x000000UL;
        out_pal->colors[OYK_TR_ERROR]        = 0x000000UL;
        return;
    }

    /* Classic Mac OS System 7 / Platinum Color QuickDraw baseline */
    out_pal->colors[OYK_TR_BG]           = 0xFFFFFFUL;
    out_pal->colors[OYK_TR_FG]           = 0x000000UL;
    out_pal->colors[OYK_TR_PANEL_BG]     = 0xDDDDDDUL;
    out_pal->colors[OYK_TR_PANEL_FG]     = 0x222222UL;
    out_pal->colors[OYK_TR_HEADER_BG]    = 0xCCCCCCUL;
    out_pal->colors[OYK_TR_HEADER_FG]    = 0x000000UL;
    out_pal->colors[OYK_TR_STATUS_BG]    = 0xDDDDDDUL;
    out_pal->colors[OYK_TR_STATUS_FG]    = 0x000000UL;
    out_pal->colors[OYK_TR_BORDER]       = 0x555555UL;
    out_pal->colors[OYK_TR_ACCENT]       = 0x3366CCUL;
    out_pal->colors[OYK_TR_SELECTION_BG] = 0xCCCCFFUL;
    out_pal->colors[OYK_TR_SELECTION_FG] = 0x000000UL;
    out_pal->colors[OYK_TR_BUTTON_BG]    = 0xEEEEEEUL;
    out_pal->colors[OYK_TR_BUTTON_FG]    = 0x000000UL;
    out_pal->colors[OYK_TR_INPUT_BG]     = 0xFFFFFFUL;
    out_pal->colors[OYK_TR_INPUT_FG]     = 0x000000UL;
    out_pal->colors[OYK_TR_OK]           = 0x1F8F4EUL;
    out_pal->colors[OYK_TR_WARN]         = 0xAA6600UL;
    out_pal->colors[OYK_TR_ERROR]        = 0xCC2222UL;

    /* Query window/system default WCTabHandle (System 6.0.8 - Mac OS 9.2.2) */
    AuxWinHandle aux = NULL;
    WindowPtr wp = static_cast<WindowPtr>(win_ptr);
    if (wp) GetAuxWin(wp, &aux);
    if (!aux || !(**aux).awCTable) {
        aux = NULL;
        GetAuxWin(NULL, &aux);
    }
    if (aux && (**aux).awCTable) {
        CTabHandle ct = (**aux).awCTable;
        short max_idx = (**ct).ctSize;
        for (short i = 0; i <= max_idx && i < 16; ++i) {
            short entry = (**ct).ctTable[i].value;
            oyk_u32 col = mc_u32_from_rgb((**ct).ctTable[i].rgb);
            if (entry == wContentColor) {
                out_pal->colors[OYK_TR_BG] = col;
                out_pal->colors[OYK_TR_INPUT_BG] = col;
            } else if (entry == wFrameColor) {
                out_pal->colors[OYK_TR_BORDER] = col;
            } else if (entry == wTextColor) {
                out_pal->colors[OYK_TR_FG] = col;
                out_pal->colors[OYK_TR_INPUT_FG] = col;
                out_pal->colors[OYK_TR_BUTTON_FG] = col;
                out_pal->colors[OYK_TR_STATUS_FG] = col;
            } else if (entry == wTitleBarColor) {
                out_pal->colors[OYK_TR_HEADER_BG] = col;
                out_pal->colors[OYK_TR_HEADER_FG] = (mc_luma(col) < 120UL) ? 0xFFFFFFUL : 0x000000UL;
            } else if (entry == wHiliteColor) {
                out_pal->colors[OYK_TR_ACCENT] = col;
            }
        }
    }

    /* Query user Highlight Color from Color Control Panel (LowMem 0x0DA0) */
    volatile unsigned long hilite_addr = 0x0DA0UL;
    const volatile RGBColor* hrgb_ptr = reinterpret_cast<const volatile RGBColor*>(hilite_addr);
    RGBColor hrgb;
    hrgb.red = hrgb_ptr->red;
    hrgb.green = hrgb_ptr->green;
    hrgb.blue = hrgb_ptr->blue;
    oyk_u32 hcol = mc_u32_from_rgb(hrgb);
    out_pal->colors[OYK_TR_SELECTION_BG] = hcol;
    out_pal->colors[OYK_TR_SELECTION_FG] = (mc_luma(hcol) < 140UL) ? 0xFFFFFFUL : 0x000000UL;

#ifdef OYK_HAVE_APPEARANCE_H
    long app_attr = 0;
    if (Gestalt(gestaltAppearanceAttr, &app_attr) == noErr &&
        (app_attr & (1L << gestaltAppearanceExists)) != 0L) {
        RGBColor ac;
        if (GetThemeBrushAsColor(kThemeBrushDocumentWindowBackground, 8, true, &ac) == noErr) {
            out_pal->colors[OYK_TR_BG] = mc_u32_from_rgb(ac);
        }
        if (GetThemeBrushAsColor(kThemeBrushDialogBackgroundActive, 8, true, &ac) == noErr) {
            out_pal->colors[OYK_TR_PANEL_BG] = mc_u32_from_rgb(ac);
            out_pal->colors[OYK_TR_STATUS_BG] = out_pal->colors[OYK_TR_PANEL_BG];
        }
        if (GetThemeTextColor(kThemeTextColorDialogActive, 8, true, &ac) == noErr) {
            out_pal->colors[OYK_TR_FG] = mc_u32_from_rgb(ac);
            out_pal->colors[OYK_TR_PANEL_FG] = out_pal->colors[OYK_TR_FG];
        }
    }
#endif

    out_pal->is_dark_base = (mc_luma(out_pal->colors[OYK_TR_BG]) < 100UL) ? 1 : 0;
}

static RGBColor mc_rgb(oyk_u32 c) {
    RGBColor out;
    out.red   = static_cast<unsigned short>(((c >> 16) & 0xFFU) * 257U);
    out.green = static_cast<unsigned short>(((c >> 8)  & 0xFFU) * 257U);
    out.blue  = static_cast<unsigned short>((c & 0xFFU) * 257U);
    return out;
}

static void mc_set_fg(const OykMacUiState* ui, oyk_u32 rgb) {
    if (ui && ui->has_color) {
        RGBColor c = mc_rgb(rgb);
        RGBForeColor(&c);
    } else {
        ForeColor(blackColor);
    }
}

static void mc_set_bg(const OykMacUiState* ui, oyk_u32 rgb) {
    if (ui && ui->has_color) {
        RGBColor c = mc_rgb(rgb);
        RGBBackColor(&c);
    } else {
        BackColor(whiteColor);
    }
}

static void mc_fill_color(const OykMacUiState* ui, const Rect* r, oyk_u32 rgb, const Pattern* bw_pat) {
    if (ui && ui->has_color) {
        RGBColor prev;
        GetForeColor(&prev);
        RGBColor c = mc_rgb(rgb);
        RGBForeColor(&c);
        PaintRect(r);
        RGBForeColor(&prev);
    } else {
        FillRect(r, bw_pat ? bw_pat : &qd.white);
    }
}

static void mc_draw_text(int x, int y, const char* s) {
    if (!s || !s[0]) return;
    Str255 ps;
    mu_c2p(s, ps);
    MoveTo(static_cast<short>(x), static_cast<short>(y));
    DrawString(ps);
}

static void mc_draw_text_clip(int x, int y, const char* s, int max_chars) {
    if (!s || !s[0] || max_chars <= 0) return;
    char tmp[160];
    size_t lim = static_cast<size_t>(max_chars);
    if (lim >= sizeof(tmp)) lim = sizeof(tmp) - 1U;
    size_t n = strlen(s);
    if (n > lim) {
        if (lim > 3U) {
            memcpy(tmp, s, lim - 3U);
            memcpy(tmp + lim - 3U, "...", 3);
            tmp[lim] = '\0';
        } else {
            memcpy(tmp, s, lim);
            tmp[lim] = '\0';
        }
    } else {
        memcpy(tmp, s, n + 1U);
    }
    mc_draw_text(x, y, tmp);
}

static void mc_add_hit(OykMacUiState* ui, const Rect& r, int kind, int arg) {
    if (!ui || ui->hit_count >= 32) return;
    ui->hits[ui->hit_count].rect = r;
    ui->hits[ui->hit_count].kind = kind;
    ui->hits[ui->hit_count].arg = arg;
    ++ui->hit_count;
}

static void mc_draw_button(OykMacUiState* ui, int x, int y, int w, int h,
                           const char* label, int is_default, int hit_kind, int hit_arg) {
    Rect r;
    SetRect(&r, static_cast<short>(x), static_cast<short>(y),
            static_cast<short>(x + w), static_cast<short>(y + h));
    if (ui->has_color) {
        oyk_u32 fill_col = is_default ? ui->active_theme.colors[OYK_TR_ACCENT]
                                      : ui->active_theme.colors[OYK_TR_BUTTON_BG];
        mc_fill_color(ui, &r, fill_col, &qd.white);
        mc_set_fg(ui, ui->active_theme.colors[OYK_TR_BORDER]);
        FrameRoundRect(&r, 8, 8);
    } else {
        FillRoundRect(&r, 8, 8, &qd.white);
        PenNormal();
        FrameRoundRect(&r, 8, 8);
        if (is_default) {
            Rect outer = r;
            InsetRect(&outer, -3, -3);
            PenSize(2, 2);
            FrameRoundRect(&outer, 12, 12);
            PenNormal();
        }
    }
    TextFont(0);
    TextSize(10);
    Str255 ps;
    mu_c2p(label, ps);
    short tw = StringWidth(ps);
    short tx = static_cast<short>(x + (w - tw) / 2);
    if (tx < x + 4) tx = static_cast<short>(x + 4);
    short ty = static_cast<short>(y + h - 5);
    oyk_u32 txt_col = ui->active_theme.colors[OYK_TR_BUTTON_FG];
    if (ui->has_color && is_default) {
        txt_col = (mc_luma(ui->active_theme.colors[OYK_TR_ACCENT]) < 140UL) ? 0xFFFFFFUL : 0x000000UL;
    }
    mc_set_fg(ui, txt_col);
    MoveTo(tx, ty);
    DrawString(ps);
    mc_add_hit(ui, r, hit_kind, hit_arg);
}

static void mc_draw_checkbox(OykMacUiState* ui, int x, int y, int checked,
                             const char* label, int hit_kind, int hit_arg) {
    int sz = oyk_gui_control_box_size(10);
    int top = y - sz + 1;
    Rect box;
    SetRect(&box, static_cast<short>(x), static_cast<short>(top),
            static_cast<short>(x + sz), static_cast<short>(top + sz));
    mc_fill_color(ui, &box, ui->active_theme.colors[OYK_TR_INPUT_BG], &qd.white);
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_BORDER]);
    FrameRect(&box);
    if (checked) {
        mc_set_fg(ui, ui->has_color ? ui->active_theme.colors[OYK_TR_ACCENT] : ui->active_theme.colors[OYK_TR_FG]);
        PenSize(1, 1);
        MoveTo(static_cast<short>(x + 2), static_cast<short>(top + 2));
        LineTo(static_cast<short>(x + sz - 3), static_cast<short>(top + sz - 3));
        MoveTo(static_cast<short>(x + sz - 3), static_cast<short>(top + 2));
        LineTo(static_cast<short>(x + 2), static_cast<short>(top + sz - 3));
    }
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);
    if (label && label[0]) {
        mc_draw_text(x + oyk_gui_control_label_offset(10), y, label);
    }
    Rect hit;
    SetRect(&hit, static_cast<short>(x), static_cast<short>(top - 1),
            static_cast<short>(x + sz + 110), static_cast<short>(top + sz + 2));
    mc_add_hit(ui, hit, hit_kind, hit_arg);
}

static void mc_draw_radio(OykMacUiState* ui, int x, int y, int selected,
                          const char* label, int hit_kind, int hit_arg) {
    int sz = oyk_gui_control_box_size(10);
    int top = y - sz + 1;
    Rect oval;
    SetRect(&oval, static_cast<short>(x), static_cast<short>(top),
            static_cast<short>(x + sz), static_cast<short>(top + sz));
    mc_fill_color(ui, &oval, ui->active_theme.colors[OYK_TR_INPUT_BG], &qd.white);
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_BORDER]);
    FrameOval(&oval);
    if (selected) {
        Rect dot = oval;
        InsetRect(&dot, 3, 3);
        mc_set_fg(ui, ui->has_color ? ui->active_theme.colors[OYK_TR_ACCENT] : ui->active_theme.colors[OYK_TR_FG]);
        PaintOval(&dot);
    }
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);
    if (label && label[0]) {
        mc_draw_text_clip(x + oyk_gui_control_label_offset(10), y, label, 64);
    }
    Rect hit;
    SetRect(&hit, static_cast<short>(x), static_cast<short>(top - 1),
            static_cast<short>(x + sz + 160), static_cast<short>(top + sz + 2));
    mc_add_hit(ui, hit, hit_kind, hit_arg);
}

static void mc_draw_field_row(OykMacUiState* ui, int x, int y,
                              const char* label, const char* value, int box_w) {
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);
    mc_draw_text(x, y, label);
    Rect fr;
    SetRect(&fr, static_cast<short>(x + 118), static_cast<short>(y - 10),
            static_cast<short>(x + 118 + box_w), static_cast<short>(y + 3));
    mc_fill_color(ui, &fr, ui->active_theme.colors[OYK_TR_INPUT_BG], &qd.white);
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_BORDER]);
    FrameRect(&fr);
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_INPUT_FG]);
    mc_draw_text_clip(x + 122, y, value ? value : "", (box_w - 8) / 6);
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);
}

static void mc_draw_box(OykMacUiState* ui, int x, int y, int w, int h, const char* title) {
    Rect r;
    SetRect(&r, static_cast<short>(x), static_cast<short>(y),
            static_cast<short>(x + w), static_cast<short>(y + h));
    mc_fill_color(ui, &r, ui->active_theme.colors[OYK_TR_BG], &qd.white);
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_BORDER]);
    FrameRect(&r);

    Rect bar;
    SetRect(&bar, static_cast<short>(x), static_cast<short>(y),
            static_cast<short>(x + w), static_cast<short>(y + 16));
    if (ui->has_color) {
        mc_fill_color(ui, &bar, ui->active_theme.colors[OYK_TR_HEADER_BG], &qd.black);
        mc_set_fg(ui, ui->active_theme.colors[OYK_TR_HEADER_FG]);
    } else {
        FillRect(&bar, &qd.black);
        TextMode(srcBic);
        ForeColor(blackColor);
    }
    TextFont(0);
    TextSize(9);
    mc_draw_text_clip(x + 6, y + 12, title, (w - 12) / 6);
    TextMode(srcOr);
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);
}

static void mc_draw_modal_frame(OykMacUiState* ui, int mw, int mh,
                                const char* title, Rect* out_rect) {
    int left = (ui->scr_w - mw) / 2;
    int top = (ui->scr_h - 24 - mh) / 2;
    if (left < 8) left = 8;
    if (top < 6) top = 6;

    /* Classic dim backdrop over live console on 1-bit B&W; keep Color QD crisp */
    if (!ui->has_color) {
        Rect win_r = ui->main_win->portRect;
        PenPat(&qd.gray);
        PenMode(notPatBic);
        PaintRect(&win_r);
        PenNormal();
    }

    Rect shadow;
    SetRect(&shadow, static_cast<short>(left + 3), static_cast<short>(top + 3),
            static_cast<short>(left + mw + 3), static_cast<short>(top + mh + 3));
    mc_fill_color(ui, &shadow, 0x111111UL, &qd.black);

    Rect box;
    SetRect(&box, static_cast<short>(left), static_cast<short>(top),
            static_cast<short>(left + mw), static_cast<short>(top + mh));
    mc_fill_color(ui, &box, ui->active_theme.colors[OYK_TR_BG], &qd.white);
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_BORDER]);
    PenSize(2, 2);
    FrameRect(&box);
    PenNormal();
    Rect inner = box;
    InsetRect(&inner, 3, 3);
    FrameRect(&inner);

    /* Title bar + classic close square on left & [X] on right */
    Rect tbar;
    SetRect(&tbar, static_cast<short>(left + 3), static_cast<short>(top + 3),
            static_cast<short>(left + mw - 3), static_cast<short>(top + 21));
    if (ui->has_color) {
        mc_fill_color(ui, &tbar, ui->active_theme.colors[OYK_TR_HEADER_BG], &qd.black);
        mc_set_fg(ui, ui->active_theme.colors[OYK_TR_HEADER_FG]);
    } else {
        FillRect(&tbar, &qd.black);
        TextMode(srcBic);
    }
    TextFont(0);
    TextSize(10);
    mc_draw_text_clip(left + 12, top + 16, title, (mw - 42) / 6);
    TextMode(srcOr);

    /* Close button */
    Rect close_r;
    SetRect(&close_r, static_cast<short>(left + mw - 20), static_cast<short>(top + 6),
            static_cast<short>(left + mw - 7), static_cast<short>(top + 18));
    mc_fill_color(ui, &close_r, 0xFFFFFFUL, &qd.white);
    ForeColor(blackColor);
    FrameRect(&close_r);
    MoveTo(close_r.left + 2, close_r.top + 2);
    LineTo(close_r.right - 3, close_r.bottom - 3);
    MoveTo(close_r.right - 3, close_r.top + 2);
    LineTo(close_r.left + 2, close_r.bottom - 3);
    mc_add_hit(ui, close_r, MC_HIT_CLOSE_MODAL, 0);

    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);
    if (out_rect) *out_rect = box;
}

static void mc_append_term_line(OykMacUiState* ui, const char* line) {
    if (!ui || !line) return;
    if (ui->term_line_count >= 28) {
        for (int i = 0; i + 1 < 28; ++i) {
            mu_copy(ui->term_lines[i], 128, ui->term_lines[i + 1]);
        }
        ui->term_line_count = 27;
    }
    mu_copy(ui->term_lines[ui->term_line_count++], 128, line);
}

static void mc_draw_main_console(OykMacUiState* ui) {
    const OykConfig& cfg = ui->node->config();
    int w = ui->main_win->portRect.right - ui->main_win->portRect.left;
    int h = ui->main_win->portRect.bottom - ui->main_win->portRect.top;
    int compact = (w < 540) ? 1 : 0;

    ++ui->console_draw_count;
    ui->last_console_bg = ui->active_theme.colors[OYK_TR_BG];

    Rect full = ui->main_win->portRect;
    mc_fill_color(ui, &full, ui->active_theme.colors[OYK_TR_BG], &qd.white);

    /* Row 1: Node Banner + [INCOMING] pill */
    Rect bnr;
    SetRect(&bnr, 4, 4, static_cast<short>(w - 4), 24);
    if (ui->has_color) {
        mc_fill_color(ui, &bnr, ui->active_theme.colors[OYK_TR_HEADER_BG], &qd.black);
        mc_set_fg(ui, ui->active_theme.colors[OYK_TR_HEADER_FG]);
    } else {
        FillRect(&bnr, &qd.black);
        TextMode(srcBic);
    }
    char call_s[16];
    oyk_format_callsign(&cfg.callsign, call_s, sizeof(call_s));
    char alias_s[16];
    oyk_format_callsign(&cfg.alias, alias_s, sizeof(alias_s));
    char header[160];
    oyk_gui_format_console_header(call_s, alias_s, ui->node->uptime_seconds(),
                                  ui->node->total_active_connections(),
                                  ui->node->accept_incoming(), compact,
                                  header, sizeof(header));
    TextFont(4);
    TextSize(9);
    mc_draw_text_clip(10, 17, header, (w - 110) / 6);
    TextMode(srcOr);

    const char* pill = oyk_gui_format_incoming_pill(ui->node->accept_incoming(), compact);
    Rect pill_r;
    SetRect(&pill_r, static_cast<short>(w - (compact ? 82 : 122)), 6,
            static_cast<short>(w - 8), 22);
    oyk_u32 pill_bg = ui->node->accept_incoming() ? ui->active_theme.colors[OYK_TR_OK]
                                                  : ui->active_theme.colors[OYK_TR_ERROR];
    mc_fill_color(ui, &pill_r, pill_bg, &qd.white);
    if (ui->has_color) {
        mc_set_fg(ui, (mc_luma(pill_bg) < 140UL) ? 0xFFFFFFUL : 0x000000UL);
    } else {
        ForeColor(blackColor);
    }
    mc_draw_text(pill_r.left + 4, 17, pill);
    ForeColor(blackColor);
    FrameRect(&pill_r);
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);
    mc_add_hit(ui, pill_r, MC_HIT_TOGGLE_INCOMING, 0);

    /* Side-by-side: Interfaces (left) & Last 5 New Callsigns Heard (right) */
    int mid_x = (w * 54) / 100;
    int top_h = (h * 34) / 100;
    if (top_h < 84) top_h = 84;
    mc_draw_box(ui, 4, 28, mid_x - 6, top_h, "Interfaces");
    mc_draw_box(ui, mid_x + 2, 28, w - mid_x - 6, top_h, "Last 5 New Callsigns Heard");

    TextFont(4);
    TextSize(9);
    char hdr_if[64];
    sprintf(hdr_if, "%-8s %-8s %-6s %-6s",
            oyk_gui_table_column(OYK_TBL_IFACES, 0),
            oyk_gui_table_column(OYK_TBL_IFACES, 1),
            oyk_gui_table_column(OYK_TBL_IFACES, 3),
            oyk_gui_table_column(OYK_TBL_IFACES, 4));
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_PANEL_FG]);
    mc_draw_text_clip(10, 56, hdr_if, (mid_x - 18) / 6);
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);

    for (int i = 0; i < cfg.iface_count && i < 4 && i < ui->node->iface_count(); ++i) {
        const OykIfaceRuntime& rt = ui->node->iface(i);
        char row[80];
        sprintf(row, "%-8s %-8s %-6s %lu/%lu",
                cfg.ifaces[i].id,
                cfg.ifaces[i].type,
                rt.running ? "UP" : "DOWN",
                static_cast<unsigned long>(rt.rx_frames),
                static_cast<unsigned long>(rt.tx_frames));
        mc_draw_text_clip(10, 70 + i * 13, row, (mid_x - 18) / 6);
    }

    char hdr_h[64];
    sprintf(hdr_h, "%-2s %-9s %-7s %-6s",
            oyk_gui_table_column(OYK_TBL_HEARD, 0),
            oyk_gui_table_column(OYK_TBL_HEARD, 1),
            oyk_gui_table_column(OYK_TBL_HEARD, 2),
            oyk_gui_table_column(OYK_TBL_HEARD, 3));
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_PANEL_FG]);
    mc_draw_text_clip(mid_x + 8, 56, hdr_h, (w - mid_x - 18) / 6);
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);

    for (int i = 0; i < ui->node->new_heard_count() && i < 5; ++i) {
        const OykNewHeard& he = ui->node->new_heard(i);
        char row[64];
        sprintf(row, "%-2d %-9s %-7s %s", i + 1, he.callsign, he.iface_id, he.time_hms);
        mc_draw_text_clip(mid_x + 8, 70 + i * 13, row, (w - mid_x - 18) / 6);
    }

    /* Middle: Active Connections */
    int conn_y = 28 + top_h + 4;
    int conn_h = (h * 26) / 100;
    if (conn_h < 68) conn_h = 68;
    mc_draw_box(ui, 4, conn_y, w - 8, conn_h, "Active Connections");
    char hdr_c[96];
    sprintf(hdr_c, "%-4s %-10s %-8s %-7s %-10s %-8s",
            oyk_gui_table_column(OYK_TBL_CONNECTIONS, 0),
            oyk_gui_table_column(OYK_TBL_CONNECTIONS, 1),
            oyk_gui_table_column(OYK_TBL_CONNECTIONS, 2),
            oyk_gui_table_column(OYK_TBL_CONNECTIONS, 3),
            oyk_gui_table_column(OYK_TBL_CONNECTIONS, 4),
            oyk_gui_table_column(OYK_TBL_CONNECTIONS, 5));
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_PANEL_FG]);
    mc_draw_text_clip(10, conn_y + 28, hdr_c, (w - 20) / 6);
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);

    int shown_c = 0;
    for (int i = 0; i < ui->node->max_sessions() && shown_c < 3; ++i) {
        const OykSession& s = ui->node->session_slot(i);
        if (s.state == OYK_SESS_FREE) continue;
        char cs[16];
        oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
        const char* pname = (s.iface_idx >= 0 && s.iface_idx < cfg.iface_count)
                                ? cfg.ifaces[s.iface_idx].id : "local";
        char row[96];
        sprintf(row, "%-4lu %-10s %-8s %-7s %-10s %-8s",
                static_cast<unsigned long>(s.id), cs, pname,
                s.is_local_term ? "LOCAL" : "IN",
                (s.state == OYK_SESS_CONNECTED) ? "CONNECTED" : "OPENING",
                s.since_hms);
        mc_draw_text_clip(10, conn_y + 42 + shown_c * 13, row, (w - 20) / 6);
        ++shown_c;
    }
    if (shown_c == 0) {
        mc_draw_text(10, conn_y + 44, "(no active connections)");
    }

    /* Bottom: Node Activity Log + Action Bar */
    int log_y = conn_y + conn_h + 4;
    int bar_y = h - 26;
    int log_h = bar_y - log_y - 4;
    if (log_h > 36) {
        mc_draw_box(ui, 4, log_y, w - 8, log_h, "Node Activity Log");
        int max_rows = (log_h - 22) / 12;
        int total_l = ui->node->console_log_count();
        int start = (total_l > max_rows) ? (total_l - max_rows) : 0;
        for (int i = start; i < total_l; ++i) {
            const char* ln = ui->node->console_log_line(i);
            if (!ln) continue;
            mc_draw_text_clip(10, log_y + 28 + (i - start) * 12, ln, (w - 20) / 6);
        }
    }

    /* Footer quick buttons (matching desktop ports) */
    int bx = 6;
    int bw = compact ? 74 : 92;
    mc_draw_button(ui, bx, bar_y, bw, 20, "Terminal", 1, MC_HIT_SET_SCREEN, OYK_SCR_TERMINAL);
    bx += bw + 6;
    mc_draw_button(ui, bx, bar_y, bw, 20, "Files", 0, MC_HIT_SET_SCREEN, OYK_SCR_FILES);
    bx += bw + 6;
    mc_draw_button(ui, bx, bar_y, bw, 20, "Settings", 0, MC_HIT_SET_SCREEN, OYK_SCR_SETTINGS);
    bx += bw + 6;
    mc_draw_button(ui, bx, bar_y, bw, 20, "Theme", 0, MC_HIT_SET_SCREEN, OYK_SCR_THEME);
    bx += bw + 6;
    mc_draw_button(ui, bx, bar_y, bw, 20, "Peers", 0, MC_HIT_SET_SCREEN, OYK_SCR_PEERS);
}

static void mc_draw_modal_content(OykMacUiState* ui) {
    int mw = (ui->scr_w >= 620) ? 540 : 464;
    int mh = (ui->scr_h >= 460) ? 350 : 264;
    Rect box;
    const OykConfig& cfg = ui->node->config();
    int scr = ui->active_screen;
    const char* title = oyk_gui_screen_title(scr);
    if (scr == OYK_SCR_PEERS) title = oyk_gui_screen_title(OYK_SCR_SETTINGS);

    ++ui->modal_draw_count;
    ui->last_modal_bg = ui->active_theme.colors[OYK_TR_BG];

    mc_draw_modal_frame(ui, mw, mh, title, &box);
    int lx = box.left + 14;
    int ty = box.top + 38;
    int bw = box.right - box.left - 28;
    int by = box.bottom - 30;
    TextFont(4);
    TextSize(9);

    if (scr == OYK_SCR_SETTINGS || scr == OYK_SCR_PEERS) {
        int active_tab = (scr == OYK_SCR_PEERS) ? 4 : ui->settings_tab;
        int tx = lx;
        int tw = bw / 7;
        for (int t = 0; t < oyk_gui_settings_tab_count(); ++t) {
            Rect tr;
            SetRect(&tr, static_cast<short>(tx), static_cast<short>(ty - 12),
                    static_cast<short>(tx + tw - 2), static_cast<short>(ty + 4));
            int sel = (t == active_tab);
            mc_fill_color(ui, &tr, sel ? ui->active_theme.colors[OYK_TR_SELECTION_BG]
                                       : ui->active_theme.colors[OYK_TR_BG],
                          sel ? &qd.black : &qd.white);
            FrameRect(&tr);
            if (!ui->has_color && sel) TextMode(srcBic);
            else if (ui->has_color) mc_set_fg(ui, sel ? ui->active_theme.colors[OYK_TR_SELECTION_FG] : ui->active_theme.colors[OYK_TR_FG]);
            const char* ttitle = oyk_gui_settings_tab_title(t);
            if (strcmp(ttitle, "Interfaces") == 0) ttitle = "Ifaces";
            else if (strcmp(ttitle, "Printer") == 0) ttitle = "Prn";
            char tl[20];
            sprintf(tl, "%d:%s", t + 1, ttitle);
            mc_draw_text_clip(tx + 4, ty, tl, (tw - 6) / 6);
            TextMode(srcOr);
            mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);
            mc_add_hit(ui, tr, MC_HIT_TAB, t);
            tx += tw;
        }
        MoveTo(lx, ty + 8);
        LineTo(lx + bw, ty + 8);

        if (active_tab == 4) {
            char phdr[80];
            sprintf(phdr, "%-8s %-9s %-8s %-18s %-7s",
                    oyk_gui_table_column(OYK_TBL_PEERS, 0),
                    oyk_gui_table_column(OYK_TBL_PEERS, 1),
                    oyk_gui_table_column(OYK_TBL_PEERS, 2),
                    oyk_gui_table_column(OYK_TBL_PEERS, 3),
                    oyk_gui_table_column(OYK_TBL_PEERS, 4));
            mc_set_fg(ui, ui->active_theme.colors[OYK_TR_PANEL_FG]);
            mc_draw_text_clip(lx, ty + 24, phdr, bw / 6);
            mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);
            for (int p = 0; p < cfg.peer_count && p < 8; ++p) {
                char row[96];
                char ep[32];
                sprintf(ep, "%s:%u", cfg.peers[p].host,
                        static_cast<unsigned int>(cfg.peers[p].port));
                sprintf(row, "%s %-8s %-9s %-8s %-18s %s",
                        (p == ui->sel_peer) ? ">" : " ",
                        cfg.peers[p].id, cfg.peers[p].callsign,
                        cfg.peers[p].interface_id, ep,
                        cfg.peers[p].auto_learned ? "AUTO" : "STATIC");
                mc_draw_text_clip(lx, ty + 40 + p * 14, row, bw / 6);
            }
            mc_draw_button(ui, lx, by, 72, 20, "Edit Peer", 0, MC_HIT_SET_SCREEN, OYK_SCR_EDIT_PEER);
            mc_draw_button(ui, box.right - 92, by, 78, 20, "Close", 1, MC_HIT_CLOSE_MODAL, 0);
        } else if (active_tab == 1) {
            char bbs_cs[16], ssid_s[16], max_s[24];
            OykCallsign bcall;
            oyk_config_resolved_bbs_callsign(&ui->draft_cfg, &bcall);
            oyk_format_callsign(&bcall, bbs_cs, sizeof(bbs_cs));
            sprintf(ssid_s, "%u", static_cast<unsigned int>(ui->draft_cfg.bbs_ssid));
            sprintf(max_s, "%lu", static_cast<unsigned long>(ui->draft_cfg.bbs_max_msg_bytes));
            mc_draw_checkbox(ui, lx, ty + 24, ui->draft_cfg.bbs_enabled,
                             "Enable Packet BBS", MC_HIT_NONE, 0);
            mc_draw_field_row(ui, lx, ty + 42, "BBS Callsign:", bbs_cs, 180);
            mc_draw_field_row(ui, lx, ty + 60, "BBS SSID:", ssid_s, 80);
            mc_draw_field_row(ui, lx, ty + 78, "Mail Path:", ui->draft_cfg.bbs_mail_path[0] ? ui->draft_cfg.bbs_mail_path : "mail/", 180);
            mc_draw_field_row(ui, lx, ty + 96, "Files Path:", ui->draft_cfg.bbs_files_path[0] ? ui->draft_cfg.bbs_files_path : "files/", 180);
            mc_draw_checkbox(ui, lx, ty + 114, ui->draft_cfg.bbs_files_enabled,
                             "Enable BBS Files Area", MC_HIT_NONE, 0);
            mc_draw_button(ui, box.right - 176, by, 78, 20, "Cancel", 0, MC_HIT_CLOSE_MODAL, 0);
            mc_draw_button(ui, box.right - 92, by, 78, 20, "Save", 1, MC_HIT_CLOSE_MODAL, 0);
        } else if (active_tab == 2) {
            char chat_cs[16], cssid[16];
            OykCallsign ccall;
            oyk_config_resolved_chat_callsign(&ui->draft_cfg, &ccall);
            oyk_format_callsign(&ccall, chat_cs, sizeof(chat_cs));
            sprintf(cssid, "%u", static_cast<unsigned int>(ui->draft_cfg.chat_ssid));
            mc_draw_checkbox(ui, lx, ty + 24, ui->draft_cfg.chat_enabled,
                             "Enable Multi-User CHAT", MC_HIT_NONE, 0);
            mc_draw_field_row(ui, lx, ty + 44, "CHAT Callsign:", chat_cs, 180);
            mc_draw_field_row(ui, lx, ty + 62, "CHAT SSID:", cssid, 80);
            mc_draw_field_row(ui, lx, ty + 80, "Default Topic:", ui->draft_cfg.chat_topic[0] ? ui->draft_cfg.chat_topic : "Lobby", 180);
            mc_draw_button(ui, box.right - 176, by, 78, 20, "Cancel", 0, MC_HIT_CLOSE_MODAL, 0);
            mc_draw_button(ui, box.right - 92, by, 78, 20, "Save", 1, MC_HIT_CLOSE_MODAL, 0);
        } else if (active_tab == 3) {
            mc_set_fg(ui, ui->active_theme.colors[OYK_TR_PANEL_FG]);
            mc_draw_text(lx, ty + 24, "ID       Driver       Endpoint              State");
            mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);
            for (int i = 0; i < ui->draft_cfg.iface_count && i < 6; ++i) {
                const OykIfaceConfig& ic = ui->draft_cfg.ifaces[i];
                char ep[40], row[96];
                if (ic.device[0]) sprintf(ep, "%s @ %lu", ic.device, static_cast<unsigned long>(ic.baud));
                else sprintf(ep, "%s:%u", ic.host, static_cast<unsigned int>(ic.port));
                sprintf(row, "%-8s %-12s %-21s %s", ic.id, ic.type, ep, "UP");
                mc_draw_text_clip(lx, ty + 40 + i * 14, row, bw / 6);
            }
            mc_draw_button(ui, lx, by, 84, 20, "Add Iface...", 0, MC_HIT_SET_SCREEN, OYK_SCR_ADD_IFACE);
            mc_draw_button(ui, lx + 92, by, 84, 20, "Edit...", 0, MC_HIT_SET_SCREEN, OYK_SCR_EDIT_IFACE);
            mc_draw_button(ui, box.right - 92, by, 78, 20, "Close", 1, MC_HIT_CLOSE_MODAL, 0);
        } else if (active_tab == 5) {
            char rp[16], rint[16];
            sprintf(rp, "%u", static_cast<unsigned int>(ui->draft_cfg.rig_port));
            sprintf(rint, "%u s", static_cast<unsigned int>(ui->draft_cfg.rig_poll_sec));
            mc_draw_checkbox(ui, lx, ty + 24, ui->draft_cfg.rig_enabled,
                             "Enable Radio / Rig Control", MC_HIT_NONE, 0);
            mc_draw_field_row(ui, lx, ty + 44, "Protocol:", ui->draft_cfg.rig_protocol[0] ? ui->draft_cfg.rig_protocol : "rigctld", 180);
            mc_draw_field_row(ui, lx, ty + 62, "Host / Addr:", ui->draft_cfg.rig_host[0] ? ui->draft_cfg.rig_host : "127.0.0.1", 180);
            mc_draw_field_row(ui, lx, ty + 80, "Port:", rp, 80);
            mc_draw_field_row(ui, lx, ty + 98, "Poll Interval:", rint, 80);
            mc_draw_button(ui, box.right - 176, by, 78, 20, "Cancel", 0, MC_HIT_CLOSE_MODAL, 0);
            mc_draw_button(ui, box.right - 92, by, 78, 20, "Save", 1, MC_HIT_CLOSE_MODAL, 0);
        } else if (active_tab == 6) {
            char pbaud[16];
            sprintf(pbaud, "%lu", static_cast<unsigned long>(ui->draft_cfg.printer_baud));
            mc_draw_checkbox(ui, lx, ty + 24, ui->draft_cfg.printer_enabled,
                             "Enable Direct Line-by-Line Printer Logging", MC_HIT_NONE, 0);
            mc_draw_field_row(ui, lx, ty + 44, "Device / Port:", ui->draft_cfg.printer_device[0] ? ui->draft_cfg.printer_device : ".BOut", 180);
            mc_draw_field_row(ui, lx, ty + 62, "Protocol:", ui->draft_cfg.printer_protocol[0] ? ui->draft_cfg.printer_protocol : "generic", 140);
            mc_draw_field_row(ui, lx, ty + 80, "Baud Rate:", pbaud, 80);
            mc_draw_checkbox(ui, lx, ty + 100, ui->draft_cfg.printer_condensed,
                             "132-Column Condensed Mode", MC_HIT_NONE, 0);
            mc_draw_button(ui, box.right - 176, by, 78, 20, "Cancel", 0, MC_HIT_CLOSE_MODAL, 0);
            mc_draw_button(ui, box.right - 92, by, 78, 20, "Save", 1, MC_HIT_CLOSE_MODAL, 0);
        } else {
            char cs[16];
            char al[16];
            oyk_format_callsign(&ui->draft_cfg.callsign, cs, sizeof(cs));
            oyk_format_callsign(&ui->draft_cfg.alias, al, sizeof(al));
            mc_draw_field_row(ui, lx, ty + 28, "Node Callsign:", cs, 140);
            mc_draw_field_row(ui, lx, ty + 46, "Node Alias:", al, 140);
            char ln[128];
            sprintf(ln, "Net Backend:    %s", ui->net->caps.backend_summary);
            mc_draw_text_clip(lx, ty + 64, ln, bw / 6);
            mc_draw_checkbox(ui, lx, ty + 84, ui->draft_cfg.accept_incoming,
                             "Accept Incoming Connections", MC_HIT_TOGGLE_INCOMING, 0);
            mc_draw_checkbox(ui, lx, ty + 102, ui->draft_cfg.bbs_enabled,
                             "Enable Packet BBS & CHAT", MC_HIT_NONE, 0);
            mc_draw_text(lx, ty + 122, ui->status_msg);
            mc_draw_button(ui, box.right - 176, by, 78, 20, "Cancel", 0, MC_HIT_CLOSE_MODAL, 0);
            mc_draw_button(ui, box.right - 92, by, 78, 20, "Save", 1, MC_HIT_CLOSE_MODAL, 0);
        }
    } else if (scr == OYK_SCR_ADD_IFACE) {
        mc_draw_text_clip(lx, ty, oyk_gui_wiz_step_prompt(&ui->wiz), bw / 6);
        char step_s[48];
        sprintf(step_s, "Step %d of 4", ui->wiz.step + 1);
        mc_draw_text(lx, ty + 16, step_s);
        if (ui->wiz.step == 0) {
            int cnt = oyk_gui_wiz_driver_count(ui->wiz.driver_mask);
            for (int i = 0; i < cnt; ++i) {
                char row[96];
                sprintf(row, "%-12s %s",
                        oyk_gui_wiz_driver_type(ui->wiz.driver_mask, i),
                        oyk_gui_wiz_driver_desc(ui->wiz.driver_mask, i));
                mc_draw_radio(ui, lx + 4, ty + 35 + i * 16,
                              i == ui->wiz.selected_driver_idx, row,
                              MC_HIT_WIZ_DRIVER, i);
            }
        } else if (ui->wiz.step == 1) {
            char ln[96];
            sprintf(ln, "Interface ID:   [%s]", ui->wiz.draft.id);
            mc_draw_text(lx, ty + 42, ln);
            sprintf(ln, "Description:    [%s]",
                    ui->wiz.draft.description[0] ? ui->wiz.draft.description : "Classic Mac Interface");
            mc_draw_text_clip(lx, ty + 62, ln, bw / 6);
        } else if (ui->wiz.step == 2) {
            char ln[96];
            sprintf(ln, "Host / Device:  [%s]",
                    ui->wiz.draft.host[0] ? ui->wiz.draft.host : ui->wiz.draft.device);
            mc_draw_text(lx, ty + 42, ln);
            sprintf(ln, "Port / Baud:    [%lu]",
                    static_cast<unsigned long>(ui->wiz.draft.port ? ui->wiz.draft.port : ui->wiz.draft.baud));
            mc_draw_text(lx, ty + 62, ln);
        } else {
            char sum[320];
            oyk_gui_wiz_format_summary(&ui->wiz, sum, sizeof(sum), 0);
            int y = ty + 34;
            const char* p = sum;
            while (*p && y < by - 14) {
                const char* nl = strchr(p, '\n');
                char line[88];
                size_t len = nl ? static_cast<size_t>(nl - p) : strlen(p);
                if (len >= sizeof(line)) len = sizeof(line) - 1U;
                memcpy(line, p, len);
                line[len] = '\0';
                mc_draw_text_clip(lx, y, line, bw / 6);
                y += 13;
                p = nl ? (nl + 1) : "";
            }
        }
        mc_draw_button(ui, lx, by, 72, 20, "Back", 0, MC_HIT_WIZ_BACK, 0);
        mc_draw_button(ui, box.right - 176, by, 78, 20, "Cancel", 0, MC_HIT_CLOSE_MODAL, 0);
        mc_draw_button(ui, box.right - 92, by, 78, 20,
                       (ui->wiz.step >= 3) ? "Finish" : "Next >", 1, MC_HIT_WIZ_NEXT, 0);
    } else if (scr == OYK_SCR_EDIT_IFACE) {
        const OykIfaceConfig* ic = (ui->sel_iface < cfg.iface_count) ? &cfg.ifaces[ui->sel_iface] : NULL;
        char ln[96];
        sprintf(ln, "Interface ID:   [%s]", ic ? ic->id : "axudp0");
        mc_draw_text(lx, ty + 8, ln);
        sprintf(ln, "Driver Type:    [%s]", ic ? ic->type : "axudp");
        mc_draw_text(lx, ty + 28, ln);
        sprintf(ln, "Bind / Host:    [%s]", ic ? ic->host : "127.0.0.1");
        mc_draw_text(lx, ty + 48, ln);
        sprintf(ln, "Port / Baud:    [%u]", ic ? static_cast<unsigned int>(ic->port) : 10093U);
        mc_draw_text(lx, ty + 68, ln);
        sprintf(ln, "Netmask:        [%s]", (ic && ic->netmask[0]) ? ic->netmask : "255.0.0.0");
        mc_draw_text(lx, ty + 88, ln);
        mc_draw_checkbox(ui, lx, ty + 108, ic ? !ic->block_inbound : 1,
                         "Allow Inbound", MC_HIT_NONE, 0);
        mc_draw_checkbox(ui, lx + 140, ty + 108, ic ? !ic->block_outbound : 1,
                         "Allow Outbound", MC_HIT_NONE, 0);
        mc_draw_button(ui, box.right - 176, by, 78, 20, "Cancel", 0, MC_HIT_CLOSE_MODAL, 0);
        mc_draw_button(ui, box.right - 92, by, 78, 20, "Save", 1, MC_HIT_CLOSE_MODAL, 0);
    } else if (scr == OYK_SCR_EDIT_PEER) {
        const OykPeerConfig* pc = (ui->sel_peer < cfg.peer_count) ? &cfg.peers[ui->sel_peer] : NULL;
        char ln[96];
        sprintf(ln, "Peer ID:        [%s]", pc ? pc->id : "remote0");
        mc_draw_text(lx, ty + 8, ln);
        sprintf(ln, "Callsign:       [%s]", pc ? pc->callsign : "R3MOT-7");
        mc_draw_text(lx, ty + 28, ln);
        sprintf(ln, "Interface:      [%s]", pc ? pc->interface_id : "axudp0");
        mc_draw_text(lx, ty + 48, ln);
        sprintf(ln, "Host / DMR ID:  [%s]", pc ? pc->host : "12.34.56.78");
        mc_draw_text(lx, ty + 68, ln);
        sprintf(ln, "UDP Port:       [%u]", pc ? static_cast<unsigned int>(pc->port) : 10093U);
        mc_draw_text(lx, ty + 88, ln);
        mc_draw_checkbox(ui, lx, ty + 108, pc ? pc->allow_inbound : 1,
                         "Allow Inbound", MC_HIT_NONE, 0);
        mc_draw_checkbox(ui, lx + 140, ty + 108, pc ? pc->allow_outbound : 1,
                         "Allow Outbound", MC_HIT_NONE, 0);
        mc_draw_button(ui, box.right - 176, by, 78, 20, "Cancel", 0, MC_HIT_CLOSE_MODAL, 0);
        mc_draw_button(ui, box.right - 92, by, 78, 20, "Save", 1, MC_HIT_CLOSE_MODAL, 0);
    } else if (scr == OYK_SCR_TERMINATE) {
        const char* cols[4];
        oyk_gui_terminate_columns(cols, 4);
        char thdr[80];
        sprintf(thdr, "%-5s %-11s %-10s %-10s", cols[0], cols[1], cols[2], cols[3]);
        mc_set_fg(ui, ui->active_theme.colors[OYK_TR_PANEL_FG]);
        mc_draw_text(lx, ty, thdr);
        mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);
        MoveTo(lx, ty + 6);
        LineTo(lx + bw, ty + 6);
        int y = ty + 22;
        for (int i = 0; i < ui->node->max_sessions(); ++i) {
            const OykSession& s = ui->node->session_slot(i);
            if (s.state == OYK_SESS_FREE) continue;
            char cs[16];
            oyk_format_callsign(&s.remote_call, cs, sizeof(cs));
            const char* pname = (s.iface_idx >= 0 && s.iface_idx < cfg.iface_count)
                                    ? cfg.ifaces[s.iface_idx].id : "local";
            char row[80];
            sprintf(row, "%-5lu %-11s %-10s %-10s",
                    static_cast<unsigned long>(s.id), cs, pname, "CONNECTED");
            mc_draw_text_clip(lx, y, row, bw / 6);
            y += 14;
        }
        mc_draw_button(ui, lx, by, 108, 20, "Kick Selected", 0, MC_HIT_CLOSE_MODAL, 0);
        mc_draw_button(ui, box.right - 92, by, 78, 20, "Cancel", 1, MC_HIT_CLOSE_MODAL, 0);
    } else if (scr == OYK_SCR_BAN) {
        mc_draw_text(lx, ty, "Callsign: [W9SPAM-9]   Lift: [7d]   Reason: [Flooding]");
        mc_draw_checkbox(ui, lx, ty + 16, 1, "Hard Ban", MC_HIT_NONE, 0);
        mc_draw_checkbox(ui, lx + 130, ty + 16, 1, "Appeal Allowed", MC_HIT_NONE, 0);
        char bhdr[80];
        sprintf(bhdr, "%-10s %-11s %-11s %-14s",
                oyk_gui_table_column(OYK_TBL_BANS, 0),
                oyk_gui_table_column(OYK_TBL_BANS, 1),
                oyk_gui_table_column(OYK_TBL_BANS, 2),
                oyk_gui_table_column(OYK_TBL_BANS, 4));
        mc_set_fg(ui, ui->active_theme.colors[OYK_TR_PANEL_FG]);
        mc_draw_text(lx, ty + 32, bhdr);
        mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);
        for (int b = 0; b < ui->node->ban_count() && b < 4; ++b) {
            const OykBanEntry& be = ui->node->ban_entry(b);
            char row[80];
            sprintf(row, "%-10s %-11s %-11s %-14s", be.key, be.banned_at, be.lift_at, be.reason);
            mc_draw_text_clip(lx, ty + 48 + b * 14, row, bw / 6);
        }
        mc_draw_button(ui, lx, by, 72, 20, "Ban Call", 0, MC_HIT_CLOSE_MODAL, 0);
        mc_draw_button(ui, box.right - 92, by, 78, 20, "Close", 1, MC_HIT_CLOSE_MODAL, 0);
    } else if (scr == OYK_SCR_EXIT_PEERS) {
        OykGuiExitPrompt ep;
        oyk_gui_build_exit_prompt(1, ui->node->total_active_connections(), &ep);
        const char* p = ep.body;
        int y = ty + 16;
        while (*p) {
            const char* nl = strchr(p, '\n');
            char line[96];
            size_t len = nl ? static_cast<size_t>(nl - p) : strlen(p);
            if (len >= sizeof(line)) len = sizeof(line) - 1U;
            memcpy(line, p, len);
            line[len] = '\0';
            mc_draw_text_clip(lx, y, line, bw / 6);
            y += 16;
            p = nl ? (nl + 1) : "";
        }
        mc_draw_button(ui, lx, by, 92, 20, "Save & Quit", 1, MC_HIT_QUIT_NOW, 0);
        mc_draw_button(ui, lx + 98, by, 68, 20, "Discard", 0, MC_HIT_QUIT_NOW, 0);
        mc_draw_button(ui, lx + 172, by, 92, 20, "Review Peers", 0, MC_HIT_SET_SCREEN, OYK_SCR_PEERS);
        mc_draw_button(ui, box.right - 84, by, 70, 20, "Cancel", 0, MC_HIT_CLOSE_MODAL, 0);
    } else if (scr == OYK_SCR_THEME) {
        char tsel[96];
        sprintf(tsel, "Theme: < %s >  (System %s)", ui->theme_ctrl.active_name,
                ui->has_color ? "Color QuickDraw" : "1-bit B&W");
        mc_draw_text(lx, ty, tsel);
        mc_draw_button(ui, box.right - 140, ty - 12, 24, 16, "<", 0, MC_HIT_THEME_PREV, 0);
        mc_draw_button(ui, box.right - 110, ty - 12, 24, 16, ">", 0, MC_HIT_THEME_NEXT, 0);

        char hdr[64];
        sprintf(hdr, "%-16s %-8s %-9s %s",
                oyk_gui_table_column(OYK_TBL_THEME, 0),
                oyk_gui_table_column(OYK_TBL_THEME, 1),
                oyk_gui_table_column(OYK_TBL_THEME, 2),
                oyk_gui_table_column(OYK_TBL_THEME, 3));
        mc_set_fg(ui, ui->active_theme.colors[OYK_TR_PANEL_FG]);
        mc_draw_text(lx, ty + 20, hdr);
        mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);

        int max_r = (mh > 300) ? OYK_THEME_COLOR_COUNT : 8;
        int step_y = (mh > 300) ? 12 : 14;
        for (int r = 0; r < max_r && r < OYK_THEME_COLOR_COUNT; ++r) {
            oyk_u32 col = ui->active_theme.colors[r];
            char hex_s[12];
            oyk_theme_format_hex(col, hex_s, sizeof(hex_s));
            char row[64];
            sprintf(row, "%-16s %-8s %-9s",
                    oyk_theme_role_label(r),
                    oyk_theme_role_key(r),
                    hex_s);
            int ry = ty + 32 + r * step_y;
            mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);
            mc_draw_text_clip(lx, ry, row, (bw - 70) / 6);
            Rect sw;
            SetRect(&sw, static_cast<short>(lx + bw - 58), static_cast<short>(ry - 9),
                    static_cast<short>(lx + bw - 8), static_cast<short>(ry + 1));
            const Pattern* pats[4] = { &qd.black, &qd.dkGray, &qd.gray, &qd.ltGray };
            mc_fill_color(ui, &sw, col, pats[r & 3]);
            mc_set_fg(ui, ui->active_theme.colors[OYK_TR_BORDER]);
            FrameRect(&sw);
            mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);
        }
            mc_draw_button(ui, box.right - 176, by, 78, 20, "Cancel", 0, MC_HIT_THEME_CANCEL, 0);
            mc_draw_button(ui, box.right - 92, by, 78, 20, "Apply", 1, MC_HIT_THEME_APPLY, 0);
    } else if (scr == OYK_SCR_TERMINAL) {
        mc_draw_text_clip(lx, ty, "Ctrl+A ? Help | Ctrl+A C Conn | Ctrl+A D Disc | Ctrl+A X Close", bw / 6);
        Rect term_r;
        SetRect(&term_r, static_cast<short>(lx), static_cast<short>(ty + 6),
                static_cast<short>(lx + bw), static_cast<short>(by - 28));
        mc_fill_color(ui, &term_r, ui->active_theme.colors[OYK_TR_BG], &qd.white);
        FrameRect(&term_r);
        int cols = (bw - 12) / 6;
        int y = term_r.top + 13;
        for (int i = 0; i < ui->term_line_count && y < term_r.bottom - 4; ++i) {
            OykTermWrapSpan spans[4];
            int scnt = oyk_term_wrap_line(ui->term_lines[i], strlen(ui->term_lines[i]),
                                          cols, spans, 4);
            for (int k = 0; k < scnt && y < term_r.bottom - 4; ++k) {
                char seg[128];
                size_t slen = spans[k].len;
                if (slen >= sizeof(seg)) slen = sizeof(seg) - 1U;
                memcpy(seg, ui->term_lines[i] + spans[k].start, slen);
                seg[slen] = '\0';
                mc_draw_text(lx + 6, y, seg);
                y += 12;
            }
        }
        char in_ln[110];
        sprintf(in_ln, "Prompt [%s]> %s_", oyk_term_eol_label(&ui->term_eol), ui->term_input);
        mc_draw_text_clip(lx, by - 10, in_ln, bw / 6);
        mc_draw_button(ui, box.right - 176, by, 78, 20, "Send", 1, MC_HIT_TERM_SEND, 0);
        mc_draw_button(ui, box.right - 92, by, 78, 20, "Close", 0, MC_HIT_CLOSE_MODAL, 0);
    } else if (scr == OYK_SCR_FILES) {
        int half = bw / 2 - 4;
        mc_draw_box(ui, lx, ty - 6, half, by - ty - 6, "Pending Incoming");
        mc_draw_box(ui, lx + half + 8, ty - 6, half, by - ty - 6, "Approved BBS Files");
        char ihdr[48];
        sprintf(ihdr, "%-9s %-10s %-6s",
                oyk_gui_table_column(OYK_TBL_FILES_INCOMING, 0),
                oyk_gui_table_column(OYK_TBL_FILES_INCOMING, 1),
                oyk_gui_table_column(OYK_TBL_FILES_INCOMING, 2));
        mc_draw_text_clip(lx + 6, ty + 22, ihdr, (half - 10) / 6);
        char ahdr[48];
        sprintf(ahdr, "%-16s %-7s",
                oyk_gui_table_column(OYK_TBL_FILES_APPROVED, 0),
                oyk_gui_table_column(OYK_TBL_FILES_APPROVED, 1));
        mc_draw_text_clip(lx + half + 14, ty + 22, ahdr, (half - 10) / 6);

        OykPendingIncoming pend[4];
        OykApprovedFile appr[4];
        int pcnt = oyk_bbs_list_incoming("INCOMING", pend, 4);
        int acnt = oyk_bbs_list_approved("FILES", appr, 4);
        if (pcnt == 0) {
            mc_draw_text_clip(lx + 6, ty + 36, "W4ABC-7  PACKETS.TXT 2048", (half - 10) / 6);
        } else {
            for (int i = 0; i < pcnt; ++i) {
                char row[160];
                sprintf(row, "%-9.9s %-10.20s %lu", pend[i].uploader, pend[i].filename,
                        static_cast<unsigned long>(pend[i].size));
                mc_draw_text_clip(lx + 6, ty + 36 + i * 13, row, (half - 10) / 6);
            }
        }
        if (acnt == 0) {
            mc_draw_text_clip(lx + half + 14, ty + 36, "README.TXT       1420", (half - 10) / 6);
            mc_draw_text_clip(lx + half + 14, ty + 49, "NETLIST.CFG       618", (half - 10) / 6);
        } else {
            for (int j = 0; j < acnt; ++j) {
                char row[160];
                sprintf(row, "%-16.32s %lu", appr[j].rel_path, static_cast<unsigned long>(appr[j].size));
                mc_draw_text_clip(lx + half + 14, ty + 36 + j * 13, row, (half - 10) / 6);
            }
        }
        mc_draw_button(ui, lx, by, 86, 20, "Upload...", 0, MC_HIT_FILE_UPLOAD, 0);
        mc_draw_button(ui, box.right - 92, by, 78, 20, "Close", 1, MC_HIT_CLOSE_MODAL, 0);
    } else {
        mc_draw_text(lx, ty + 14, "Radio CAT / Hamlib Rig Control");
        mc_draw_text(lx, ty + 34, "Protocol:   Hamlib rigctld (port 4532)");
        mc_draw_text(lx, ty + 52, "Serial/CAT: Modem (.AOut) @ 9600 bps");
        mc_draw_text(lx, ty + 70, "Frequency:  145.050 MHz (FM)");
        mc_draw_button(ui, box.right - 92, by, 78, 20, "Close", 1, MC_HIT_CLOSE_MODAL, 0);
    }
}

static void mc_redraw(OykMacUiState* ui) {
    if (!ui || !ui->main_win) return;
    SetPort(ui->main_win);
    ui->hit_count = 0;
    oyk_gui_theme_resolve(&ui->theme_ctrl, &ui->active_theme);
    mc_set_bg(ui, ui->active_theme.colors[OYK_TR_BG]);
    mc_set_fg(ui, ui->active_theme.colors[OYK_TR_FG]);

    mc_draw_main_console(ui);
    if (ui->active_screen != OYK_SCR_MAIN) {
        mc_draw_modal_content(ui);
    }
}

static void mc_on_theme_preview(const OykThemeConfig* pal, const char* active_name, void* ctx) {
    (void)active_name;
    OykMacUiState* ui = static_cast<OykMacUiState*>(ctx);
    if (!ui) return;
    if (pal) ui->active_theme = *pal;
    mc_redraw(ui);
}

static void mc_pick_file(OykMacUiState* ui) {
    if (ui->net->caps.has_std_file_7) {
        StandardFileReply reply;
        SFTypeList tl;
        tl[0] = 'TEXT';
        StandardGetFile(NULL, -1, tl, &reply);
        if (reply.sfGood) {
            mu_p2c(reply.sfFile.name, ui->status_msg, sizeof(ui->status_msg));
        }
    } else {
        SFReply reply;
        Point pt;
        pt.h = 80;
        pt.v = 80;
        SFTypeList tl;
        tl[0] = 'TEXT';
        SFGetFile(pt, "\pSelect File", NULL, -1, tl, NULL, &reply);
        if (reply.good) {
            mu_p2c(reply.fName, ui->status_msg, sizeof(ui->status_msg));
        }
    }
}

static void mc_sync_menus(const OykMacUiState* ui) {
    MenuHandle nm = GetMenuHandle(131);
    if (!nm) return;
    if (ui->node->accept_incoming()) {
        DisableItem(nm, 1);
        EnableItem(nm, 2);
        CheckItem(nm, 1, true);
        CheckItem(nm, 2, false);
    } else {
        EnableItem(nm, 1);
        DisableItem(nm, 2);
        CheckItem(nm, 1, false);
        CheckItem(nm, 2, true);
    }
}

static void mc_dispatch_action(OykMacUiState* ui, int action) {
    switch (action) {
        case OYK_ACT_ALLOW_INCOMING:
            ui->node->set_accept_incoming(1);
            break;
        case OYK_ACT_DENY_INCOMING:
            ui->node->set_accept_incoming(0);
            break;
        case OYK_ACT_LOCAL_TERM:
            ui->active_screen = OYK_SCR_TERMINAL;
            break;
        case OYK_ACT_FILES:
            ui->active_screen = OYK_SCR_FILES;
            break;
        case OYK_ACT_RIG:
            ui->active_screen = OYK_SCR_RIG;
            break;
        case OYK_ACT_SETTINGS:
            ui->draft_cfg = ui->node->config();
            ui->active_screen = OYK_SCR_SETTINGS;
            break;
        case OYK_ACT_PEERS:
            ui->active_screen = OYK_SCR_PEERS;
            break;
        case OYK_ACT_THEME:
            ui->active_screen = OYK_SCR_THEME;
            break;
        case OYK_ACT_TERMINATE:
            ui->active_screen = OYK_SCR_TERMINATE;
            break;
        case OYK_ACT_BAN:
            ui->active_screen = OYK_SCR_BAN;
            break;
        case OYK_ACT_EXIT:
            if (ui->node->auto_learned_peer_count() > 0) {
                ui->active_screen = OYK_SCR_EXIT_PEERS;
            } else {
                ui->running = 0;
            }
            break;
        default:
            break;
    }
    mc_sync_menus(ui);
}

static void mc_handle_menu(OykMacUiState* ui, long choice) {
    if (choice == 0L) return;
    short mid = static_cast<short>((choice >> 16) & 0xFFFFL);
    short item = static_cast<short>(choice & 0xFFFFL);
    if (mid == 128) {
        if (item == 1) {
            Str255 msg;
            mu_c2p("OYK-Node 1.0 (Classic Macintosh 68k+PPC)", msg);
            ParamText(msg, "\p", "\p", "\p");
            Alert(128, NULL);
        } else {
            Str255 da_name;
            GetMenuItemText(GetMenuHandle(128), item, da_name);
            OpenDeskAcc(da_name);
        }
    } else if (mid == 129) {
        if (item == 1) mc_pick_file(ui);
        else if (item == 2) oyk_config_save(&ui->node->config(), "NODE.CFG");
        else if (item == 3) mc_dispatch_action(ui, OYK_ACT_FILES);
        else if (item == 5) mc_dispatch_action(ui, OYK_ACT_EXIT);
    } else if (mid == 131) {
        const OykGuiMenuItem* mi = oyk_gui_node_menu_item(item - 1);
        if (mi) mc_dispatch_action(ui, mi->action);
    } else if (mid == 132) {
        if (item == 1) ui->active_screen = OYK_SCR_MAIN;
        else if (item == 2) ui->active_screen = OYK_SCR_TERMINAL;
        else if (item == 3) ui->active_screen = OYK_SCR_FILES;
        else if (item == 4) ui->active_screen = OYK_SCR_RIG;
    } else if (mid == 133) {
        if (item == 1) ui->active_screen = OYK_SCR_TERMINAL;
        else mc_handle_menu(ui, (128L << 16) | 1L);
    }
    HiliteMenu(0);
    mc_redraw(ui);
}

static void mc_seed_gallery_fixture(OykMacUiState* ui) {
    OykPortableNode* n = ui->node;
    char lbuf[112];
    n->log_message(OYK_LOG_INFO, "OYK-Node started on Classic Macintosh");
    sprintf(lbuf, "Driver: %s", ui->net->caps.backend_summary);
    n->log_message(OYK_LOG_INFO, lbuf);

    /* Simulate inbound SABM from W4ABC-7 (matching 127.0.0.x netmask) for Heard & Connections */
    OykFrame rx_f;
    memset(&rx_f, 0, sizeof(rx_f));
    oyk_parse_callsign("KR4OYK-7", &rx_f.dst);
    oyk_parse_callsign("W4ABC-7", &rx_f.src);
    rx_f.control = 0x3FU; /* SABM+P */
    rx_f.has_pid = 0;
    n->on_rx_frame(0, &rx_f, 0x7F000002UL, 10093U, 0U);
    n->set_iface_status(0, 1, "UP");

    oyk_u32 sid = 0;
    OykCallsign rcall;
    int isloc = 0;
    char err_b[96];
    err_b[0] = '\0';
    n->open_local_terminal(0, "BBS", &sid, &rcall, &isloc, err_b, sizeof(err_b));
    n->ban_callsign("W9SPAM-9", 1UL, "Flooding", "7d", err_b, sizeof(err_b));

    /* Seed sample terminal session lines */
    mc_append_term_line(ui, "*** Connected to N0BBS-7 via axudp0");
    mc_append_term_line(ui, "Welcome to N0BBS-7 Classic Packet BBS (YAPP-C enabled).");
    mc_append_term_line(ui, "Msg#  Date    From    Subject");
    mc_append_term_line(ui, "  104 Nov 26  W4ABC-7 AXUDP link tests on System 6.0.8");
    mc_append_term_line(ui, "  105 Nov 26  KR4OYK  Cross-port oyk_gui parity confirmed");
    mc_append_term_line(ui, "BBS> L");
    mu_copy(ui->term_input, sizeof(ui->term_input), "B");

    /* Seed sample files + wizard state for clear screenshots */
    oyk_gui_wiz_init(&ui->wiz, &n->config(), OYK_DRV_ALL);
    oyk_gui_wiz_select_driver(&ui->wiz, &n->config(), 2); /* kiss-serial */
    mu_copy(ui->wiz.draft.id, sizeof(ui->wiz.draft.id), "kiss0");
    mu_copy(ui->wiz.draft.device, sizeof(ui->wiz.draft.device), "Modem");
    ui->wiz.draft.baud = 9600UL;
}

static void mc_run_capture_tour(OykMacUiState* ui) {
    struct FlowCap {
        int scr;
        const char* suffix;
    };
    static const FlowCap kFlows[12] = {
        { OYK_SCR_MAIN,       "main"       },
        { OYK_SCR_SETTINGS,   "settings"   },
        { OYK_SCR_ADD_IFACE,  "add_iface"  },
        { OYK_SCR_EDIT_IFACE, "edit_iface" },
        { OYK_SCR_PEERS,      "peers"      },
        { OYK_SCR_EDIT_PEER,  "edit_peer"  },
        { OYK_SCR_TERMINATE,  "terminate"  },
        { OYK_SCR_BAN,        "ban"        },
        { OYK_SCR_EXIT_PEERS, "exit"       },
        { OYK_SCR_THEME,      "theme"      },
        { OYK_SCR_TERMINAL,   "terminal"   },
        { OYK_SCR_FILES,      "files"      }
    };

    mc_seed_gallery_fixture(ui);
    for (int i = 0; i < 12; ++i) {
        ui->active_screen = kFlows[i].scr;
        ui->settings_tab = (kFlows[i].scr == OYK_SCR_PEERS) ? 4 : 0;
        mc_redraw(ui);

        char fname[40];
        sprintf(fname, "%s_%s.bmp",
                ui->has_color ? "mccl" : "mcbw",
                kFlows[i].suffix);
        oyk_mac_capture_screen_bmp(fname, ui->has_color);
    }
    oyk_mac_finish_test_and_shutdown(0);
}

int oyk_mac_ui_run(OykPortableNode* node, OykMacNetState* net, int ui_test_mode) {
    OykMacUiState* ui = new OykMacUiState();
    if (!ui) return 1;
    memset(ui, 0, sizeof(*ui));
    ui->node = node;
    ui->net = net;
    ui->running = 1;
    ui->active_screen = OYK_SCR_MAIN;
    ui->has_color = net->caps.has_color_qd;

    /* Switch Mac II to 8-bit 256-color Color QuickDraw when supported. */
    if (ui->has_color) {
        GDHandle gd = GetMainDevice();
        if (gd && (**gd).gdPMap && (**(**gd).gdPMap).pixelSize < 8) {
            if (HasDepth(gd, 8, 1 << gdDevType, 1)) {
                SetDepth(gd, 8, 1 << gdDevType, 1);
            }
        }
    }

    ui->scr_w = static_cast<int>(qd.screenBits.bounds.right - qd.screenBits.bounds.left);
    ui->scr_h = static_cast<int>(qd.screenBits.bounds.bottom - qd.screenBits.bounds.top);
    if (ui->has_color) {
        GDHandle gd = GetMainDevice();
        if (gd) {
            ui->scr_w = static_cast<int>((**gd).gdRect.right - (**gd).gdRect.left);
            ui->scr_h = static_cast<int>((**gd).gdRect.bottom - (**gd).gdRect.top);
        }
    }

    Handle mbar = GetNewMBar(128);
    if (mbar) {
        SetMenuBar(mbar);
        AppendResMenu(GetMenuHandle(128), 'DRVR');
        DrawMenuBar();
    }

    Rect wrect;
    SetRect(&wrect, 4, 22, static_cast<short>(ui->scr_w - 4),
            static_cast<short>(ui->scr_h - 4));
    Str255 wtitle;
    mu_c2p(oyk_gui_screen_title(OYK_SCR_MAIN), wtitle);
    if (ui->has_color) {
        ui->main_win = NewCWindow(NULL, &wrect, wtitle, true, noGrowDocProc,
                                  reinterpret_cast<WindowPtr>(-1L), false, 0L);
    } else {
        ui->main_win = NewWindow(NULL, &wrect, wtitle, true, noGrowDocProc,
                                 reinterpret_cast<WindowPtr>(-1L), false, 0L);
    }
    if (!ui->main_win) {
        delete ui;
        return 1;
    }

    ui->draft_cfg = node->config();
    oyk_gui_wiz_init(&ui->wiz, &node->config(), OYK_DRV_ALL);
    OykThemeConfig os_pal;
    oyk_mac_query_os_native_theme(ui->main_win, ui->has_color, &os_pal);
    oyk_gui_theme_init(&ui->theme_ctrl, &ui->draft_cfg, 1, os_pal.is_dark_base);
    oyk_gui_theme_set_os_native(&ui->theme_ctrl, &os_pal);
    oyk_gui_theme_set_preview_cb(&ui->theme_ctrl, mc_on_theme_preview, ui);
    oyk_term_eol_init(&ui->term_eol, OYK_TERM_EOL_CR, 1);
    mc_sync_menus(ui);
    mc_redraw(ui);

    if (ui_test_mode) {
        mc_run_capture_tour(ui);
        DisposeWindow(ui->main_win);
        delete ui;
        return 0;
    }

    unsigned long next_tick = TickCount();
    while (ui->running) {
        EventRecord ev;
        Boolean got = false;
        if (net->caps.has_wait_next_event) {
            got = WaitNextEvent(everyEvent, &ev, 6L, NULL);
        } else {
            SystemTask();
            got = GetNextEvent(everyEvent, &ev);
        }

        if (got) {
            if (ev.what == mouseDown) {
                WindowPtr wp = NULL;
                short part = FindWindow(ev.where, &wp);
                if (part == inMenuBar) {
                    mc_handle_menu(ui, MenuSelect(ev.where));
                } else if (part == inSysWindow) {
                    SystemClick(&ev, wp);
                } else if (part == inDrag) {
                    Rect drag_r = qd.screenBits.bounds;
                    InsetRect(&drag_r, 4, 24);
                    DragWindow(wp, ev.where, &drag_r);
                } else if (part == inContent && wp == ui->main_win) {
                    SetPort(ui->main_win);
                    Point pt = ev.where;
                    GlobalToLocal(&pt);
                    for (int i = ui->hit_count - 1; i >= 0; --i) {
                        if (PtInRect(pt, &ui->hits[i].rect)) {
                            const McHitZone& hz = ui->hits[i];
                            if (hz.kind == MC_HIT_SET_SCREEN) {
                                ui->active_screen = hz.arg;
                            } else if (hz.kind == MC_HIT_CLOSE_MODAL) {
                                if (ui->active_screen == OYK_SCR_THEME) {
                                    oyk_gui_theme_revert(&ui->theme_ctrl);
                                }
                                ui->active_screen = OYK_SCR_MAIN;
                            } else if (hz.kind == MC_HIT_TAB) {
                                ui->settings_tab = hz.arg;
                                ui->active_screen = (hz.arg == 4) ? OYK_SCR_PEERS : OYK_SCR_SETTINGS;
                            } else if (hz.kind == MC_HIT_WIZ_DRIVER) {
                                oyk_gui_wiz_select_driver(&ui->wiz, &ui->node->config(), hz.arg);
                            } else if (hz.kind == MC_HIT_WIZ_NEXT) {
                                if (ui->wiz.step < 3) oyk_gui_wiz_advance(&ui->wiz, &ui->node->config());
                                else ui->active_screen = OYK_SCR_MAIN;
                            } else if (hz.kind == MC_HIT_WIZ_BACK) {
                                oyk_gui_wiz_back(&ui->wiz);
                            } else if (hz.kind == MC_HIT_TOGGLE_INCOMING) {
                                ui->node->set_accept_incoming(!ui->node->accept_incoming());
                                mc_sync_menus(ui);
                            } else if (hz.kind == MC_HIT_FILE_UPLOAD) {
                                mc_pick_file(ui);
                            } else if (hz.kind == MC_HIT_THEME_NEXT) {
                                oyk_gui_theme_cycle(&ui->theme_ctrl, 1);
                            } else if (hz.kind == MC_HIT_THEME_PREV) {
                                oyk_gui_theme_cycle(&ui->theme_ctrl, -1);
                            } else if (hz.kind == MC_HIT_THEME_APPLY) {
                                mu_copy(ui->draft_cfg.theme_name, sizeof(ui->draft_cfg.theme_name),
                                        ui->theme_ctrl.active_name);
                                OykConfigApplyResult tres;
                                ui->node->apply_config(&ui->draft_cfg, 1, &tres);
                                mu_copy(ui->theme_ctrl.saved_name, sizeof(ui->theme_ctrl.saved_name),
                                        ui->theme_ctrl.active_name);
                                ui->theme_ctrl.dirty = 0;
                                ui->active_screen = OYK_SCR_MAIN;
                            } else if (hz.kind == MC_HIT_THEME_CANCEL) {
                                oyk_gui_theme_revert(&ui->theme_ctrl);
                                ui->active_screen = OYK_SCR_MAIN;
                            } else if (hz.kind == MC_HIT_QUIT_NOW) {
                                ui->running = 0;
                            }
                            mc_redraw(ui);
                            break;
                        }
                    }
                }
            } else if (ev.what == keyDown || ev.what == autoKey) {
                char ch = static_cast<char>(ev.message & charCodeMask);
                if ((ev.modifiers & cmdKey) != 0) {
                    mc_handle_menu(ui, MenuKey(ch));
                } else if (ch == 0x1B) {
                    if (ui->active_screen == OYK_SCR_THEME) {
                        oyk_gui_theme_revert(&ui->theme_ctrl);
                    }
                    ui->active_screen = OYK_SCR_MAIN;
                    mc_redraw(ui);
                } else if (ui->active_screen == OYK_SCR_TERMINAL) {
                    if (ui->ctrl_a_pending) {
                        ui->ctrl_a_pending = 0;
                        int tcmd = oyk_term_decode_ctrl_a_cmd(static_cast<unsigned char>(ch));
                        if (tcmd == OYK_TCMD_CONSOLE) ui->active_screen = OYK_SCR_MAIN;
                        else if (tcmd == OYK_TCMD_CYCLE_EOL) oyk_term_eol_cycle_mode(&ui->term_eol);
                        else if (tcmd == OYK_TCMD_CLEAR) ui->term_line_count = 0;
                    } else if (ch == 0x01) {
                        ui->ctrl_a_pending = 1;
                    } else if (ch == '\r' || ch == '\n') {
                        mc_append_term_line(ui, ui->term_input);
                        ui->term_input[0] = '\0';
                    } else if (ch == '\b' || ch == 0x7F) {
                        size_t tl = strlen(ui->term_input);
                        if (tl > 0) ui->term_input[tl - 1U] = '\0';
                    } else if (static_cast<unsigned char>(ch) >= 0x20U) {
                        size_t tl = strlen(ui->term_input);
                        if (tl + 1U < sizeof(ui->term_input)) {
                            ui->term_input[tl] = ch;
                            ui->term_input[tl + 1U] = '\0';
                        }
                    }
                    mc_redraw(ui);
                }
            } else if (ev.what == updateEvt) {
                BeginUpdate(reinterpret_cast<WindowPtr>(ev.message));
                mc_redraw(ui);
                EndUpdate(reinterpret_cast<WindowPtr>(ev.message));
            }
        }

        oyk_mac_net_poll(net);
        unsigned long now = TickCount();
        if (now - next_tick >= 60UL) {
            next_tick = now;
            node->tick();
        }
    }

    DisposeWindow(ui->main_win);
    delete ui;
    return 0;
}

void oyk_mac_ui_show_fatal(const char* msg) {
    Str255 ps;
    mu_c2p(msg ? msg : "Fatal error", ps);
    ParamText(ps, "\p", "\p", "\p");
    Alert(128, NULL);
}

int oyk_mac_ui_run_theme_preview_self_test(OykPortableNode* node,
                                           OykMacNetState* net,
                                           int* out_bg_changes,
                                           int* out_fg_changes) {
    if (!node || !net) return 0;
    OykMacUiState* ui = new OykMacUiState();
    if (!ui) return 0;
    memset(ui, 0, sizeof(*ui));
    ui->node = node;
    ui->net = net;
    ui->running = 1;
    ui->active_screen = OYK_SCR_THEME;
    ui->has_color = net->caps.has_color_qd;
    ui->scr_w = static_cast<int>(qd.screenBits.bounds.right - qd.screenBits.bounds.left);
    ui->scr_h = static_cast<int>(qd.screenBits.bounds.bottom - qd.screenBits.bounds.top);
    if (ui->has_color) {
        GDHandle gd = GetMainDevice();
        if (gd) {
            ui->scr_w = static_cast<int>((**gd).gdRect.right - (**gd).gdRect.left);
            ui->scr_h = static_cast<int>((**gd).gdRect.bottom - (**gd).gdRect.top);
        }
    }

    Rect wrect;
    SetRect(&wrect, 4, 22, static_cast<short>(ui->scr_w - 4),
            static_cast<short>(ui->scr_h - 4));
    if (ui->has_color) {
        ui->main_win = NewCWindow(NULL, &wrect, "\pOYK Theme Test", true, noGrowDocProc,
                                  reinterpret_cast<WindowPtr>(-1L), false, 0L);
    } else {
        ui->main_win = NewWindow(NULL, &wrect, "\pOYK Theme Test", true, noGrowDocProc,
                                 reinterpret_cast<WindowPtr>(-1L), false, 0L);
    }
    if (!ui->main_win) {
        delete ui;
        return 0;
    }

    ui->draft_cfg = node->config();
    OykThemeConfig os_pal;
    oyk_mac_query_os_native_theme(ui->main_win, ui->has_color, &os_pal);
    oyk_gui_theme_init(&ui->theme_ctrl, &ui->draft_cfg, 1, os_pal.is_dark_base);
    oyk_gui_theme_set_os_native(&ui->theme_ctrl, &os_pal);
    oyk_gui_theme_set_preview_cb(&ui->theme_ctrl, mc_on_theme_preview, ui);
    mc_redraw(ui);

    int ok = 1;
    if (strcmp(ui->theme_ctrl.active_name, "native") != 0) ok = 0;

    int bg_changes = 0;
    int fg_changes = 0;
    oyk_u32 prev_cbg = ui->last_console_bg;
    oyk_u32 prev_mbg = ui->last_modal_bg;
    int prev_cdraw = ui->console_draw_count;
    int prev_mdraw = ui->modal_draw_count;
    RGBColor prev_cpx = {0, 0, 0};
    RGBColor prev_mpx = {0, 0, 0};
    if (ui->has_color) {
        SetPort(ui->main_win);
        GetCPixel(12, 32, &prev_cpx);
        GetCPixel(static_cast<short>(ui->scr_w / 2), static_cast<short>(ui->scr_h / 2), &prev_mpx);
    }

    const char* seq[4] = { "dark", "light", NULL, "native" };
    char terr[64];
    for (int i = 0; i < 4; ++i) {
        if (seq[i]) {
            oyk_gui_theme_select(&ui->theme_ctrl, seq[i]);
        } else {
            oyk_gui_theme_set_role_rgb(&ui->theme_ctrl, OYK_TR_BG, 0x334455UL,
                                       terr, sizeof(terr));
        }
        if (ui->console_draw_count > prev_cdraw && ui->last_console_bg != prev_cbg) {
            if (ui->has_color) {
                RGBColor cur_cpx;
                SetPort(ui->main_win);
                GetCPixel(12, 32, &cur_cpx);
                if (memcmp(&cur_cpx, &prev_cpx, sizeof(RGBColor)) != 0) ++bg_changes;
                prev_cpx = cur_cpx;
            } else {
                ++bg_changes;
            }
        }
        if (ui->modal_draw_count > prev_mdraw && ui->last_modal_bg != prev_mbg) {
            if (ui->has_color) {
                RGBColor cur_mpx;
                SetPort(ui->main_win);
                GetCPixel(static_cast<short>(ui->scr_w / 2), static_cast<short>(ui->scr_h / 2), &cur_mpx);
                if (memcmp(&cur_mpx, &prev_mpx, sizeof(RGBColor)) != 0) ++fg_changes;
                prev_mpx = cur_mpx;
            } else {
                ++fg_changes;
            }
        }
        prev_cbg = ui->last_console_bg;
        prev_mbg = ui->last_modal_bg;
        prev_cdraw = ui->console_draw_count;
        prev_mdraw = ui->modal_draw_count;
    }

    /* Switch to light then revert via Cancel path back to native */
    oyk_gui_theme_select(&ui->theme_ctrl, "light");
    oyk_gui_theme_revert(&ui->theme_ctrl);
    if (strcmp(ui->theme_ctrl.active_name, "native") != 0 ||
        ui->last_console_bg != os_pal.colors[OYK_TR_BG] ||
        ui->last_modal_bg != os_pal.colors[OYK_TR_BG]) {
        ok = 0;
    }

    if (out_bg_changes) *out_bg_changes = bg_changes;
    if (out_fg_changes) *out_fg_changes = fg_changes;
    if (bg_changes < 4 || fg_changes < 4) ok = 0;

    DisposeWindow(ui->main_win);
    delete ui;
    return ok;
}
