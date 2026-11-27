/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * Native AppKit Frontend for macOS (Mac OS X 10.4 Tiger PowerPC -> macOS 15+ Apple Silicon)
 *
 * Compiled non-ARC (-fno-objc-arc) so a single Objective-C++ source builds cleanly on:
 *   - Mac OS X 10.4-10.5 PowerPC (ppc) via Tiger/Leopard SDK + OykPortableNode
 *   - macOS Intel (x86_64) & Apple Silicon (arm64) via full C++20 NodeEngine
 * Driven by shared oyk_gui schemas/controllers and oyk_term line/hotkey logic.
 */

#import <Cocoa/Cocoa.h>

#include "macos_engine_bridge.hpp"
#include "oyk_gui.hpp"
#include "oyk_term.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static NSColor* oyk_color_from_u32(oyk_u32 rgb) {
    CGFloat r = static_cast<CGFloat>((rgb >> 16) & 0xFFU) / 255.0;
    CGFloat g = static_cast<CGFloat>((rgb >> 8)  & 0xFFU) / 255.0;
    CGFloat b = static_cast<CGFloat>(rgb & 0xFFU) / 255.0;
    return [NSColor colorWithCalibratedRed:r green:g blue:b alpha:1.0];
}

static oyk_u32 oyk_u32_from_nscolor(NSColor* c, oyk_u32 fallback) {
    if (!c) return fallback;
    NSColor* rgb = [c colorUsingColorSpaceName:NSCalibratedRGBColorSpace];
    if (!rgb) return fallback;
    CGFloat r = 0.0, g = 0.0, b = 0.0, a = 1.0;
    [rgb getRed:&r green:&g blue:&b alpha:&a];
    if (r < 0.0) r = 0.0; else if (r > 1.0) r = 1.0;
    if (g < 0.0) g = 0.0; else if (g > 1.0) g = 1.0;
    if (b < 0.0) b = 0.0; else if (b > 1.0) b = 1.0;
    oyk_u32 ri = static_cast<oyk_u32>(r * 255.0 + 0.5) & 0xFFU;
    oyk_u32 gi = static_cast<oyk_u32>(g * 255.0 + 0.5) & 0xFFU;
    oyk_u32 bi = static_cast<oyk_u32>(b * 255.0 + 0.5) & 0xFFU;
    return (ri << 16) | (gi << 8) | bi;
}

@interface OykColorSwatchView : NSView {
    NSColor* swatchColor_;
}
- (void)setSwatchColor:(NSColor*)c;
@end

@implementation OykColorSwatchView
- (void)dealloc {
    [swatchColor_ release];
    [super dealloc];
}
- (void)setSwatchColor:(NSColor*)c {
    [c retain];
    [swatchColor_ release];
    swatchColor_ = c;
    [self setNeedsDisplay:YES];
}
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    NSColor* fill = swatchColor_ ? swatchColor_ : [NSColor grayColor];
    [fill setFill];
    NSRectFill([self bounds]);
    [[NSColor grayColor] setStroke];
    NSFrameRect([self bounds]);
}
@end

@interface OykAppDelegate : NSObject {
    OykMacOsEngine* engine_;
    NSWindow* mainWindow_;
    NSTextField* bannerLabel_;
    NSScrollView* consoleScrollView_;
    NSTextView* consoleTextView_;
    NSToolbar* toolbar_;
    NSMenuItem* allowItem_;
    NSMenuItem* denyItem_;
    NSTimer* pollTimer_;
    NSWindow* modalWindow_;

    /* Interactive Theme Settings controls */
    NSWindow* themeWindow_;
    NSPopUpButton* themePopup_;
    NSButton* themeDelBtn_;
    NSTextField* themeHexField_;
    OykColorSwatchView* themeActiveSwatch_;
    NSMutableArray* themeRoleLabels_;
    NSMutableArray* themeRoleSwatches_;
    NSMutableArray* themeNamesList_;
    int themeSelectedRole_;
    int themePreviewCount_;

    int uiTestMode_;
    NSString* captureDir_;
    OykGuiAddIfaceWizard wiz_;
    OykGuiThemeCtrl themeCtrl_;
    OykThemeConfig activeTheme_;
    OykConfig draftCfg_;
    OykTermEolParser termEol_;
}
- (void)startWithArgs:(const char*)cfgPath;
- (void)dispatchGuiAction:(int)act;
- (void)applyThemeToAllWindows;
@end

static void macos_theme_preview_trampoline(const OykThemeConfig* pal,
                                           const char* active_name,
                                           void* ctx);

@implementation OykAppDelegate

- (BOOL)detectDarkAppearance {
    if ([NSApp respondsToSelector:@selector(effectiveAppearance)]) {
        id appObj = [NSApp performSelector:@selector(effectiveAppearance)];
        if (appObj) {
            NSString* name = [appObj performSelector:@selector(name)];
            if (name && ([name rangeOfString:@"Dark"].location != NSNotFound)) {
                return YES;
            }
        }
    }
    return NO;
}

- (void)queryOsNativeTheme:(OykThemeConfig*)outPal {
    if (!outPal) return;
    int isDark = [self detectDarkAppearance] ? 1 : 0;
    OykThemeConfig base;
    oyk_macos_build_os_native_theme(isDark, NULL, &base);
    oyk_u32 cols[OYK_THEME_COLOR_COUNT];
    for (int i = 0; i < OYK_THEME_COLOR_COUNT; ++i) cols[i] = base.colors[i];

    cols[OYK_TR_BG]           = oyk_u32_from_nscolor([NSColor windowBackgroundColor], cols[OYK_TR_BG]);
    cols[OYK_TR_FG]           = oyk_u32_from_nscolor([NSColor controlTextColor], cols[OYK_TR_FG]);
    cols[OYK_TR_PANEL_BG]     = oyk_u32_from_nscolor([NSColor controlBackgroundColor], cols[OYK_TR_PANEL_BG]);
    cols[OYK_TR_PANEL_FG]     = oyk_u32_from_nscolor([NSColor controlTextColor], cols[OYK_TR_PANEL_FG]);
    cols[OYK_TR_HEADER_BG]    = oyk_u32_from_nscolor([NSColor controlColor], cols[OYK_TR_HEADER_BG]);
    cols[OYK_TR_HEADER_FG]    = oyk_u32_from_nscolor([NSColor controlTextColor], cols[OYK_TR_HEADER_FG]);
    cols[OYK_TR_STATUS_BG]    = cols[OYK_TR_PANEL_BG];
    if ([NSColor respondsToSelector:@selector(secondaryLabelColor)]) {
        NSColor* sc = [NSColor performSelector:@selector(secondaryLabelColor)];
        cols[OYK_TR_STATUS_FG] = oyk_u32_from_nscolor(sc, cols[OYK_TR_STATUS_FG]);
    } else {
        cols[OYK_TR_STATUS_FG] = oyk_u32_from_nscolor([NSColor disabledControlTextColor], cols[OYK_TR_STATUS_FG]);
    }
    cols[OYK_TR_BORDER]       = oyk_u32_from_nscolor([NSColor gridColor], cols[OYK_TR_BORDER]);
    if ([NSColor respondsToSelector:@selector(controlAccentColor)]) {
        NSColor* ac = [NSColor performSelector:@selector(controlAccentColor)];
        cols[OYK_TR_ACCENT] = oyk_u32_from_nscolor(ac, cols[OYK_TR_ACCENT]);
    } else {
        cols[OYK_TR_ACCENT] = oyk_u32_from_nscolor([NSColor alternateSelectedControlColor], cols[OYK_TR_ACCENT]);
    }
    cols[OYK_TR_SELECTION_BG] = oyk_u32_from_nscolor([NSColor selectedTextBackgroundColor], cols[OYK_TR_SELECTION_BG]);
    cols[OYK_TR_SELECTION_FG] = oyk_u32_from_nscolor([NSColor selectedControlTextColor], cols[OYK_TR_SELECTION_FG]);
    cols[OYK_TR_BUTTON_BG]    = oyk_u32_from_nscolor([NSColor controlColor], cols[OYK_TR_BUTTON_BG]);
    cols[OYK_TR_BUTTON_FG]    = oyk_u32_from_nscolor([NSColor controlTextColor], cols[OYK_TR_BUTTON_FG]);
    cols[OYK_TR_INPUT_BG]     = oyk_u32_from_nscolor([NSColor textBackgroundColor], cols[OYK_TR_INPUT_BG]);
    cols[OYK_TR_INPUT_FG]     = oyk_u32_from_nscolor([NSColor textColor], cols[OYK_TR_INPUT_FG]);
    if ([NSColor respondsToSelector:@selector(systemGreenColor)]) {
        cols[OYK_TR_OK] = oyk_u32_from_nscolor([NSColor performSelector:@selector(systemGreenColor)], cols[OYK_TR_OK]);
    }
    if ([NSColor respondsToSelector:@selector(systemOrangeColor)]) {
        cols[OYK_TR_WARN] = oyk_u32_from_nscolor([NSColor performSelector:@selector(systemOrangeColor)], cols[OYK_TR_WARN]);
    }
    if ([NSColor respondsToSelector:@selector(systemRedColor)]) {
        cols[OYK_TR_ERROR] = oyk_u32_from_nscolor([NSColor performSelector:@selector(systemRedColor)], cols[OYK_TR_ERROR]);
    }

    oyk_macos_build_os_native_theme(isDark, cols, outPal);
}

- (void)applyThemeToViewTree:(NSView*)view {
    if (!view) return;
    NSColor* bg = oyk_color_from_u32(activeTheme_.colors[OYK_TR_BG]);
    NSColor* fg = oyk_color_from_u32(activeTheme_.colors[OYK_TR_FG]);
    NSColor* inpBg = oyk_color_from_u32(activeTheme_.colors[OYK_TR_INPUT_BG]);
    NSColor* inpFg = oyk_color_from_u32(activeTheme_.colors[OYK_TR_INPUT_FG]);

    if (view == bannerLabel_) {
        [bannerLabel_ setBackgroundColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_HEADER_BG])];
        [bannerLabel_ setTextColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_HEADER_FG])];
    } else if ([view isKindOfClass:[NSTextView class]]) {
        NSTextView* tv = (NSTextView*)view;
        [tv setBackgroundColor:bg];
        [tv setTextColor:fg];
        [tv setInsertionPointColor:fg];
    } else if ([view isKindOfClass:[NSScrollView class]]) {
        [(NSScrollView*)view setBackgroundColor:bg];
    } else if ([view isKindOfClass:[NSTextField class]]) {
        NSTextField* tf = (NSTextField*)view;
        if ([tf isEditable]) {
            [tf setBackgroundColor:inpBg];
            [tf setTextColor:inpFg];
        } else if (![tf isKindOfClass:[NSButton class]]) {
            [tf setTextColor:fg];
        }
    }
    [view setNeedsDisplay:YES];
    NSArray* subs = [view subviews];
    for (NSUInteger i = 0; i < [subs count]; ++i) {
        [self applyThemeToViewTree:[subs objectAtIndex:i]];
    }
}

- (void)refreshThemeWindowRows {
    if (!themeWindow_) return;
    if (themeRoleLabels_ && themeRoleSwatches_) {
        for (int r = 0; r < OYK_THEME_COLOR_COUNT; ++r) {
            oyk_u32 col = activeTheme_.colors[r];
            char hx[12];
            oyk_theme_format_hex(col, hx, sizeof(hx));
            NSString* rowStr = [NSString stringWithFormat:@"%-16s  %-14s  %s",
                                oyk_theme_role_label(r), oyk_theme_role_key(r), hx];
            if (r < static_cast<int>([themeRoleLabels_ count])) {
                NSTextField* lbl = [themeRoleLabels_ objectAtIndex:static_cast<NSUInteger>(r)];
                BOOL sel = (r == themeSelectedRole_);
                [lbl setDrawsBackground:sel ? YES : NO];
                if (sel) {
                    [lbl setBackgroundColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_SELECTION_BG])];
                    [lbl setTextColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_SELECTION_FG])];
                } else {
                    [lbl setTextColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_FG])];
                }
                [lbl setStringValue:rowStr];
            }
            if (r < static_cast<int>([themeRoleSwatches_ count])) {
                OykColorSwatchView* sw = [themeRoleSwatches_ objectAtIndex:static_cast<NSUInteger>(r)];
                [sw setSwatchColor:oyk_color_from_u32(col)];
            }
        }
    }
    if (themeSelectedRole_ >= 0 && themeSelectedRole_ < OYK_THEME_COLOR_COUNT) {
        char shx[12];
        oyk_theme_format_hex(activeTheme_.colors[themeSelectedRole_], shx, sizeof(shx));
        if (themeHexField_) {
            [themeHexField_ setStringValue:[NSString stringWithUTF8String:shx]];
        }
        if (themeActiveSwatch_) {
            [themeActiveSwatch_ setSwatchColor:oyk_color_from_u32(activeTheme_.colors[themeSelectedRole_])];
        }
    }
    if (themeDelBtn_) {
        [themeDelBtn_ setEnabled:(!oyk_theme_is_builtin(themeCtrl_.active_name)) ? YES : NO];
    }
}

- (void)applyThemeToAllWindows {
    oyk_gui_theme_resolve(&themeCtrl_, &activeTheme_);
    NSColor* bg = oyk_color_from_u32(activeTheme_.colors[OYK_TR_BG]);

    NSMutableArray* wins = [NSMutableArray arrayWithArray:[NSApp windows]];
    if (mainWindow_ && ![wins containsObject:mainWindow_]) [wins addObject:mainWindow_];
    if (themeWindow_ && ![wins containsObject:themeWindow_]) [wins addObject:themeWindow_];
    if (modalWindow_ && ![wins containsObject:modalWindow_]) [wins addObject:modalWindow_];

    for (NSUInteger i = 0; i < [wins count]; ++i) {
        NSWindow* w = [wins objectAtIndex:i];
        [w setBackgroundColor:bg];
        [self applyThemeToViewTree:[w contentView]];
    }
    [self refreshThemeWindowRows];
    for (NSUInteger i = 0; i < [wins count]; ++i) {
        [[wins objectAtIndex:i] display];
    }
    ++themePreviewCount_;
}

- (void)captureWindow:(NSWindow*)win asFlow:(NSString*)flow {
    if (!uiTestMode_ || !captureDir_ || !win) return;
    [win display];
    NSView* cv = [win contentView];
    NSRect bounds = [cv bounds];
    NSBitmapImageRep* rep = [cv bitmapImageRepForCachingDisplayInRect:bounds];
    if (!rep) return;
    [cv cacheDisplayInRect:bounds toBitmapImageRep:rep];
    NSData* png = [rep representationUsingType:NSPNGFileType properties:[NSDictionary dictionary]];
    if (png) {
        NSString* outPath = [NSString stringWithFormat:@"%@/macos_%@.png", captureDir_, flow];
        [png writeToFile:outPath atomically:YES];
    }
}

- (void)buildMenuBar {
    NSMenu* menubar = [[[NSMenu alloc] initWithTitle:@"MainMenu"] autorelease];

    /* 1. Application Menu ("OYK Node") */
    NSMenuItem* appItem = [[[NSMenuItem alloc] init] autorelease];
    NSMenu* appMenu = [[[NSMenu alloc] initWithTitle:@"OYK Node"] autorelease];
    [appMenu addItemWithTitle:@"About OYK Packet Node..." action:@selector(showAbout:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Settings..." action:@selector(openSettings:) keyEquivalent:@","];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit OYK Node" action:@selector(requestExit:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [menubar addItem:appItem];

    /* 2. File Menu */
    NSMenuItem* fileItem = [[[NSMenuItem alloc] init] autorelease];
    NSMenu* fileMenu = [[[NSMenu alloc] initWithTitle:@"File"] autorelease];
    [fileMenu addItemWithTitle:@"Open Config..." action:@selector(openConfigPanel:) keyEquivalent:@"o"];
    [fileMenu addItemWithTitle:@"Manage Files Area..." action:@selector(openFiles:) keyEquivalent:@""];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    [fileMenu addItemWithTitle:@"Close Window" action:@selector(performClose:) keyEquivalent:@"w"];
    [fileItem setSubmenu:fileMenu];
    [menubar addItem:fileItem];

    /* 3. Edit Menu */
    NSMenuItem* editItem = [[[NSMenuItem alloc] init] autorelease];
    NSMenu* editMenu = [[[NSMenu alloc] initWithTitle:@"Edit"] autorelease];
    [editMenu addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
    [editMenu addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [editMenu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
    [editMenu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
    [editItem setSubmenu:editMenu];
    [menubar addItem:editItem];

    /* 4. Canonical Node Menu driven by oyk_gui_node_menu_item() */
    NSMenuItem* nodeTop = [[[NSMenuItem alloc] init] autorelease];
    NSMenu* nodeMenu = [[[NSMenu alloc] initWithTitle:@"Node"] autorelease];
    int cnt = oyk_gui_node_menu_count();
    for (int i = 0; i < cnt; ++i) {
        const OykGuiMenuItem* gi = oyk_gui_node_menu_item(i);
        if (!gi) continue;
        if (gi->is_separator) {
            [nodeMenu addItem:[NSMenuItem separatorItem]];
            continue;
        }
        NSString* title = [NSString stringWithUTF8String:gi->mac_label];
        NSString* key = @"";
        if (gi->action == OYK_ACT_LOCAL_TERM) key = @"t";
        else if (gi->action == OYK_ACT_SETTINGS) key = @",";
        else if (gi->action == OYK_ACT_EXIT) key = @"q";
        NSMenuItem* mi = [[[NSMenuItem alloc] initWithTitle:title
                                                     action:@selector(handleNodeMenuItem:)
                                              keyEquivalent:key] autorelease];
        [mi setTag:gi->action];
        [mi setTarget:self];
        if (gi->action == OYK_ACT_ALLOW_INCOMING) allowItem_ = mi;
        if (gi->action == OYK_ACT_DENY_INCOMING) denyItem_ = mi;
        [nodeMenu addItem:mi];
    }
    [nodeTop setSubmenu:nodeMenu];
    [menubar addItem:nodeTop];

    /* 5. Window & 6. Help */
    NSMenuItem* winItem = [[[NSMenuItem alloc] init] autorelease];
    NSMenu* winMenu = [[[NSMenu alloc] initWithTitle:@"Window"] autorelease];
    [winMenu addItemWithTitle:@"Operator Console" action:@selector(focusConsole:) keyEquivalent:@"1"];
    [winItem setSubmenu:winMenu];
    [menubar addItem:winItem];
    [NSApp setWindowsMenu:winMenu];

    NSMenuItem* helpItem = [[[NSMenuItem alloc] init] autorelease];
    NSMenu* helpMenu = [[[NSMenu alloc] initWithTitle:@"Help"] autorelease];
    [helpMenu addItemWithTitle:@"Terminal Hotkeys (^A ?)" action:@selector(showHotkeys:) keyEquivalent:@"?"];
    [helpItem setSubmenu:helpMenu];
    [menubar addItem:helpItem];

    [NSApp setMainMenu:menubar];
}

- (void)refreshConsole {
    if (!engine_ || !mainWindow_) return;
    OykMacOsSnapshot* snap = new OykMacOsSnapshot();
    engine_->get_snapshot(snap);

    char banner[160];
    oyk_gui_format_console_header(snap->node_callsign, snap->node_alias,
                                  snap->uptime_seconds, snap->active_connections,
                                  snap->accept_incoming, 0, banner, sizeof(banner));
    char full_banner[200];
    snprintf(full_banner, sizeof(full_banner), "%s   %s",
             banner, oyk_gui_format_incoming_pill(snap->accept_incoming, 0));
    [bannerLabel_ setStringValue:[NSString stringWithUTF8String:full_banner]];

    NSMutableString* body = [NSMutableString string];
    [body appendFormat:@"INTERFACES (%s  |  %s  |  %s  |  %s  |  %s)\n",
                       oyk_gui_table_column(OYK_TBL_IFACES, 0),
                       oyk_gui_table_column(OYK_TBL_IFACES, 1),
                       oyk_gui_table_column(OYK_TBL_IFACES, 2),
                       oyk_gui_table_column(OYK_TBL_IFACES, 3),
                       oyk_gui_table_column(OYK_TBL_IFACES, 4)];
    for (int i = 0; i < snap->iface_count; ++i) {
        [body appendFormat:@"  %-10s %-11s %-4d %-16s %u/%u\n",
                           snap->ifaces[i].id, snap->ifaces[i].type,
                           snap->ifaces[i].active_conns, snap->ifaces[i].status,
                           static_cast<unsigned int>(snap->ifaces[i].rx_frames),
                           static_cast<unsigned int>(snap->ifaces[i].tx_frames)];
    }
    [body appendFormat:@"\nLAST 5 NEW CALLSIGNS HEARD (%s | %s | %s | %s)\n",
                       oyk_gui_table_column(OYK_TBL_HEARD, 0),
                       oyk_gui_table_column(OYK_TBL_HEARD, 1),
                       oyk_gui_table_column(OYK_TBL_HEARD, 2),
                       oyk_gui_table_column(OYK_TBL_HEARD, 3)];
    for (int i = 0; i < snap->heard_count; ++i) {
        [body appendFormat:@"  %-2d %-10s %-10s %s\n",
                           i + 1, snap->heard[i].callsign,
                           snap->heard[i].iface_id, snap->heard[i].when_hms];
    }
    [body appendFormat:@"\nACTIVE CONNECTIONS (%s | %s | %s | %s | %s | %s)\n",
                       oyk_gui_table_column(OYK_TBL_CONNECTIONS, 0),
                       oyk_gui_table_column(OYK_TBL_CONNECTIONS, 1),
                       oyk_gui_table_column(OYK_TBL_CONNECTIONS, 2),
                       oyk_gui_table_column(OYK_TBL_CONNECTIONS, 3),
                       oyk_gui_table_column(OYK_TBL_CONNECTIONS, 4),
                       oyk_gui_table_column(OYK_TBL_CONNECTIONS, 5)];
    if (snap->session_count == 0) {
        [body appendString:@"  (no active connections)\n"];
    } else {
        for (int i = 0; i < snap->session_count; ++i) {
            [body appendFormat:@"  %-4u %-10s %-9s %-6s %-10s %s\n",
                               static_cast<unsigned int>(snap->sessions[i].id),
                               snap->sessions[i].callsign, snap->sessions[i].iface_id,
                               snap->sessions[i].direction, snap->sessions[i].state,
                               snap->sessions[i].since_hms];
        }
    }
    [body appendString:@"\nNODE ACTIVITY LOG\n"];
    for (int i = 0; i < snap->log_count; ++i) {
        [body appendFormat:@"  %s\n", snap->logs[i]];
    }

    [consoleTextView_ setString:body];
    if (allowItem_ && denyItem_) {
        [allowItem_ setState:(snap->accept_incoming ? NSOnState : NSOffState)];
        [denyItem_ setState:(snap->accept_incoming ? NSOffState : NSOnState)];
    }
    delete snap;
}

- (NSWindow*)presentDialogScreen:(int)screenId bodyText:(NSString*)text height:(CGFloat)h {
    NSRect frame = NSMakeRect(160, 180, 660, h);
    NSWindow* w = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSTitledWindowMask | NSClosableWindowMask)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [w setTitle:[NSString stringWithUTF8String:oyk_gui_screen_title(screenId)]];
    [w setBackgroundColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_BG])];

    NSTextView* tv = [[[NSTextView alloc] initWithFrame:NSMakeRect(16, 48, 628, h - 68)] autorelease];
    [tv setEditable:NO];
    [tv setFont:[NSFont fontWithName:@"Monaco" size:11.0]];
    [tv setTextColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_FG])];
    [tv setBackgroundColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_BG])];
    [tv setString:text];
    [[w contentView] addSubview:tv];

    NSButton* closeBtn = [[[NSButton alloc] initWithFrame:NSMakeRect(556, 12, 88, 28)] autorelease];
    [closeBtn setTitle:@"Close"];
    [closeBtn setTarget:w];
    [closeBtn setAction:@selector(performClose:)];
    [[w contentView] addSubview:closeBtn];
    [w makeKeyAndOrderFront:nil];
    return w;
}

- (NSString*)buildScreenText:(int)scr {
    OykMacOsSnapshot* snap = new OykMacOsSnapshot();
    engine_->get_snapshot(snap);
    NSMutableString* s = [NSMutableString string];
    if (scr == OYK_SCR_SETTINGS) {
        [s appendString:@"Tabs: "];
        for (int t = 0; t < oyk_gui_settings_tab_count(); ++t) {
            [s appendFormat:@"[%d:%s] ", t + 1, oyk_gui_settings_tab_title(t)];
        }
        [s appendFormat:@"\n\nCallsign: %s\nAlias:    %s\nIncoming: %s\nEngine:   %s\n",
                        snap->node_callsign, snap->node_alias,
                        snap->accept_incoming ? "ALLOW" : "DENY",
                        engine_->uses_portable_core() ? "Portable C++98 (PowerPC)" : "C++20 NodeEngine"];
    } else if (scr == OYK_SCR_ADD_IFACE) {
        [s appendFormat:@"%s\n\nAvailable Drivers:\n", oyk_gui_wiz_step_prompt(&wiz_)];
        int cnt = oyk_gui_wiz_driver_count(wiz_.driver_mask);
        for (int i = 0; i < cnt; ++i) {
            [s appendFormat:@"  (%c) %-12s %s\n",
                            (i == wiz_.selected_driver_idx) ? '*' : ' ',
                            oyk_gui_wiz_driver_type(wiz_.driver_mask, i),
                            oyk_gui_wiz_driver_desc(wiz_.driver_mask, i)];
        }
    } else if (scr == OYK_SCR_EDIT_IFACE) {
        [s appendFormat:@"Interface ID: axudp0\nType:         axudp\nBind Host:    127.0.0.1\nUDP Port:     10093\nPolicy:       Inbound & Outbound Allowed\n"];
    } else if (scr == OYK_SCR_PEERS) {
        [s appendFormat:@"%s | %s | %s | %s | %s\n\n",
                        oyk_gui_table_column(OYK_TBL_PEERS, 0),
                        oyk_gui_table_column(OYK_TBL_PEERS, 1),
                        oyk_gui_table_column(OYK_TBL_PEERS, 2),
                        oyk_gui_table_column(OYK_TBL_PEERS, 3),
                        oyk_gui_table_column(OYK_TBL_PEERS, 4)];
        const OykConfig& c = engine_->portable_config();
        for (int i = 0; i < c.peer_count; ++i) {
            [s appendFormat:@"  %-8s %-9s %-8s %s:%u  %s\n",
                            c.peers[i].id, c.peers[i].callsign, c.peers[i].interface_id,
                            c.peers[i].host, static_cast<unsigned int>(c.peers[i].port),
                            c.peers[i].auto_learned ? "AUTO" : "STATIC"];
        }
    } else if (scr == OYK_SCR_EDIT_PEER) {
        [s appendString:@"Peer ID:   remote0\nCallsign:  R3MOT-7\nInterface: axudp0\nHost/Port: 12.34.56.78:10093\n"];
    } else if (scr == OYK_SCR_TERMINATE) {
        const char* cols[4];
        oyk_gui_terminate_columns(cols, 4);
        [s appendFormat:@"%-5s %-12s %-10s %-12s\n\n", cols[0], cols[1], cols[2], cols[3]];
        for (int i = 0; i < snap->session_count; ++i) {
            [s appendFormat:@"%-5u %-12s %-10s %-12s\n",
                            static_cast<unsigned int>(snap->sessions[i].id),
                            snap->sessions[i].callsign, snap->sessions[i].iface_id,
                            snap->sessions[i].state];
        }
    } else if (scr == OYK_SCR_BAN) {
        [s appendFormat:@"%s | %s | %s | %s\n\n",
                        oyk_gui_table_column(OYK_TBL_BANS, 0),
                        oyk_gui_table_column(OYK_TBL_BANS, 1),
                        oyk_gui_table_column(OYK_TBL_BANS, 2),
                        oyk_gui_table_column(OYK_TBL_BANS, 4)];
        for (int i = 0; i < snap->ban_count; ++i) {
            [s appendFormat:@"%-10s %-16s %-16s %s\n",
                            snap->bans[i].callsign, snap->bans[i].banned_at,
                            snap->bans[i].lift_at, snap->bans[i].reason];
        }
    } else if (scr == OYK_SCR_EXIT_PEERS) {
        OykGuiExitPrompt ep;
        oyk_gui_build_exit_prompt(snap->auto_learned_count > 0 ? snap->auto_learned_count : 1,
                                  snap->active_connections, &ep);
        [s appendString:[NSString stringWithUTF8String:ep.body]];
    } else if (scr == OYK_SCR_THEME) {
        [s appendFormat:@"Active Theme: %s\n\n%-16s %-8s %-9s\n",
                        themeCtrl_.active_name,
                        oyk_gui_table_column(OYK_TBL_THEME, 0),
                        oyk_gui_table_column(OYK_TBL_THEME, 1),
                        oyk_gui_table_column(OYK_TBL_THEME, 2)];
        for (int r = 0; r < OYK_THEME_COLOR_COUNT; ++r) {
            char hx[12];
            oyk_theme_format_hex(activeTheme_.colors[r], hx, sizeof(hx));
            [s appendFormat:@"%-16s %-8s %-9s\n",
                            oyk_theme_role_label(r), oyk_theme_role_key(r), hx];
        }
    } else if (scr == OYK_SCR_TERMINAL) {
        [s appendString:@"*** Connected to KR4OYK-1 BBS [EOL:CR]\nWelcome to OYK Packet BBS (YAPP-C).\nBBS> L\n1   248   W3TRM-1  ALL   Welcome to OYK Packet Node\n"];
        [s appendString:@"------------------------------------------------------------------------\n"];
        [s appendFormat:@"%s\n", oyk_term_hotkey_bar_text(0, 0)];
        [s appendString:@"> _                                                              [Send]\n"];
    } else if (scr == OYK_SCR_TERM_HELP) {
        [s appendString:@"Command prefix: Ctrl-A (press Ctrl-A, then hotkey)\n\n"];
        for (int hi = 0; hi < oyk_term_help_count(); ++hi) {
            const OykTermHelpEntry* he = oyk_term_help_entry(hi);
            if (he) {
                [s appendFormat:@"%-9s %-36s %s\n", he->key_label, he->action, he->alt_key];
            }
        }
    } else if (scr == OYK_SCR_FILES) {
        [s appendString:@"Incoming: /                         | Files: /\n"];
        [s appendString:@"Name                      Size      | Name                      Size\n"];
        [s appendString:@"------------------------------------+-------------------------------------\n"];
        [s appendString:@"W3TRM-1                   <DIR>     | bulletins                 <DIR>\n"];
        [s appendString:@"                                    | readme.txt                1420 B\n"];
        [s appendString:@"\n[ Upload... ]  [ Approve -> ]  [ Rename ]  [ New Dir ]  [ Delete ]  [ View ]\n"];
    } else if (scr == OYK_SCR_MENU) {
        [s appendString:@"Node Menu Actions:\n\n"];
        for (int i = 0; i < oyk_gui_node_menu_count(); ++i) {
            const OykGuiMenuItem* mi = oyk_gui_node_menu_item(i);
            if (!mi) continue;
            if (mi->is_separator) [s appendString:@"  --------------------------------\n"];
            else [s appendFormat:@"  %s\n", mi->label];
        }
    } else if (scr == OYK_SCR_BBS) {
        [s appendFormat:@"Tabs: [2:BBS] Active\n\nBBS Enabled: true\nBBS Callsign: KR4OYK-1\nBBS Alias:    OYKBBS\nMail Path:    mail\n"];
    } else if (scr == OYK_SCR_CHAT) {
        [s appendFormat:@"Tabs: [3:CHAT] Active\n\nChat Enabled: true\nChat Callsign: KR4OYK-2\nChat Alias:    OYKCHT\nTopic:         Lobby\n"];
    } else if (scr == OYK_SCR_IFACES) {
        [s appendFormat:@"Tabs: [4:Ifaces] Active\n\n  axudp0   axudp  127.0.0.1:10093   IN/OUT  SAME=N\n"];
    } else if (scr == OYK_SCR_RIG) {
        [s appendFormat:@"Tabs: [6:Rig] Active\n\nHamlib Enabled: false\nHost: 127.0.0.1   Port: 4532\n"];
    } else if (scr == OYK_SCR_PRINTER) {
        [s appendFormat:@"Tabs: [7:Prn] Active\n\nProtocol: generic   Baud: 9600   Condensed: false\n"];
    } else if (scr == OYK_SCR_ABOUT) {
        [s appendString:[NSString stringWithUTF8String:oyk_gui_about_summary()]];
    }
    delete snap;
    return s;
}

static void macos_theme_preview_trampoline(const OykThemeConfig* pal,
                                           const char* active_name,
                                           void* ctx) {
    (void)pal;
    (void)active_name;
    OykAppDelegate* del = static_cast<OykAppDelegate*>(ctx);
    if (del) [del applyThemeToAllWindows];
}

- (void)rebuildThemePopup {
    if (!themePopup_) return;
    [themePopup_ removeAllItems];
    if (!themeNamesList_) themeNamesList_ = [[NSMutableArray alloc] init];
    [themeNamesList_ removeAllObjects];

    const char* names[OYK_MAX_THEMES + 4];
    int n = oyk_gui_theme_list_names(&themeCtrl_, names, OYK_MAX_THEMES + 4);
    int sel = 0;
    for (int i = 0; i < n; ++i) {
        NSString* ns = [NSString stringWithUTF8String:names[i]];
        [themePopup_ addItemWithTitle:ns];
        [themeNamesList_ addObject:ns];
        if (strcasecmp(names[i], themeCtrl_.active_name) == 0) sel = i;
    }
    if (n > 0) [themePopup_ selectItemAtIndex:sel];
}

- (void)onThemePopupChanged:(id)sender {
    (void)sender;
    NSInteger idx = [themePopup_ indexOfSelectedItem];
    if (idx >= 0 && idx < static_cast<NSInteger>([themeNamesList_ count])) {
        NSString* nm = [themeNamesList_ objectAtIndex:static_cast<NSUInteger>(idx)];
        oyk_gui_theme_select(&themeCtrl_, [nm UTF8String]);
    }
}

- (void)onThemeRoleClicked:(id)sender {
    themeSelectedRole_ = static_cast<int>([sender tag]);
    if (themeSelectedRole_ < 0 || themeSelectedRole_ >= OYK_THEME_COLOR_COUNT) {
        themeSelectedRole_ = 0;
    }
    [self refreshThemeWindowRows];
}

- (void)onThemeApplyHex:(id)sender {
    (void)sender;
    if (!themeHexField_) return;
    char err[96];
    const char* hx = [[themeHexField_ stringValue] UTF8String];
    int wasBuiltin = oyk_theme_is_builtin(themeCtrl_.active_name);
    if (oyk_gui_theme_set_role_hex(&themeCtrl_, themeSelectedRole_, hx, err, sizeof(err))) {
        if (wasBuiltin) [self rebuildThemePopup];
    } else {
        NSRunAlertPanel(@"Theme Settings", @"%@", @"OK", nil, nil,
                        [NSString stringWithUTF8String:err]);
    }
}

- (void)onThemeColorPanelChanged:(id)sender {
    NSColor* col = [sender color];
    oyk_u32 rgb = oyk_u32_from_nscolor(col, activeTheme_.colors[themeSelectedRole_]);
    char hx[12];
    oyk_theme_format_hex(rgb, hx, sizeof(hx));
    if (themeHexField_) [themeHexField_ setStringValue:[NSString stringWithUTF8String:hx]];
    char err[96];
    int wasBuiltin = oyk_theme_is_builtin(themeCtrl_.active_name);
    if (oyk_gui_theme_set_role_rgb(&themeCtrl_, themeSelectedRole_, rgb, err, sizeof(err))) {
        if (wasBuiltin) [self rebuildThemePopup];
    }
}

- (void)onThemePickColor:(id)sender {
    (void)sender;
    NSColorPanel* cp = [NSColorPanel sharedColorPanel];
    [cp setColor:oyk_color_from_u32(activeTheme_.colors[themeSelectedRole_])];
    [cp setTarget:self];
    [cp setAction:@selector(onThemeColorPanelChanged:)];
    [cp makeKeyAndOrderFront:nil];
}

- (void)onThemeDeleteCustom:(id)sender {
    (void)sender;
    if (oyk_gui_theme_delete_custom(&themeCtrl_)) {
        [self rebuildThemePopup];
    }
}

- (void)onThemeCancel:(id)sender {
    (void)sender;
    oyk_gui_theme_revert(&themeCtrl_);
    if (themeWindow_) {
        [themeWindow_ close];
        [themeWindow_ release];
        themeWindow_ = nil;
    }
}

- (void)onThemeSaveApply:(id)sender {
    (void)sender;
    strncpy(draftCfg_.theme_name, themeCtrl_.active_name, sizeof(draftCfg_.theme_name) - 1);
    draftCfg_.theme_name[sizeof(draftCfg_.theme_name) - 1] = '\0';
    strncpy(themeCtrl_.saved_name, themeCtrl_.active_name, sizeof(themeCtrl_.saved_name) - 1);
    themeCtrl_.saved_name[sizeof(themeCtrl_.saved_name) - 1] = '\0';
    char err[96];
    err[0] = '\0';
    engine_->apply_config(&draftCfg_, 1, err, sizeof(err));
    themeCtrl_.dirty = 0;
    [self applyThemeToAllWindows];
    if (themeWindow_) {
        [themeWindow_ close];
        [themeWindow_ release];
        themeWindow_ = nil;
    }
}

- (NSWindow*)presentThemeSettingsWindow {
    if (themeWindow_) {
        [themeWindow_ makeKeyAndOrderFront:nil];
        return themeWindow_;
    }
    CGFloat w = 640.0;
    CGFloat h = 440.0;
    NSWindow* win = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(160, 140, w, h)
                  styleMask:(NSTitledWindowMask | NSClosableWindowMask)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [win setTitle:[NSString stringWithUTF8String:oyk_gui_screen_title(OYK_SCR_THEME)]];
    themeWindow_ = win;

    NSView* cv = [win contentView];
    NSTextField* lblT = [[[NSTextField alloc] initWithFrame:NSMakeRect(16, h - 38, 60, 20)] autorelease];
    [lblT setEditable:NO]; [lblT setBezeled:NO]; [lblT setDrawsBackground:NO];
    [lblT setStringValue:@"Theme:"];
    [cv addSubview:lblT];

    themePopup_ = [[[NSPopUpButton alloc] initWithFrame:NSMakeRect(76, h - 42, 220, 26) pullsDown:NO] autorelease];
    [themePopup_ setTarget:self];
    [themePopup_ setAction:@selector(onThemePopupChanged:)];
    [cv addSubview:themePopup_];

    themeDelBtn_ = [[[NSButton alloc] initWithFrame:NSMakeRect(306, h - 42, 128, 26)] autorelease];
    [themeDelBtn_ setTitle:@"Delete Custom"];
    [themeDelBtn_ setTarget:self];
    [themeDelBtn_ setAction:@selector(onThemeDeleteCustom:)];
    [cv addSubview:themeDelBtn_];

    NSTextField* hint = [[[NSTextField alloc] initWithFrame:NSMakeRect(16, h - 66, w - 32, 18)] autorelease];
    [hint setEditable:NO]; [hint setBezeled:NO]; [hint setDrawsBackground:NO];
    [hint setFont:[NSFont systemFontOfSize:11.0]];
    [hint setStringValue:@"Live preview repaints all windows. Built-in themes clone to custom on edit."];
    [cv addSubview:hint];

    NSTextField* hdr = [[[NSTextField alloc] initWithFrame:NSMakeRect(16, h - 90, w - 32, 18)] autorelease];
    [hdr setEditable:NO]; [hdr setBezeled:NO]; [hdr setDrawsBackground:NO];
    [hdr setFont:[NSFont fontWithName:@"Monaco" size:11.0]];
    [hdr setStringValue:[NSString stringWithFormat:@"%-16s  %-14s  %-9s  %s",
                         oyk_gui_table_column(OYK_TBL_THEME, 0),
                         oyk_gui_table_column(OYK_TBL_THEME, 1),
                         oyk_gui_table_column(OYK_TBL_THEME, 2),
                         oyk_gui_table_column(OYK_TBL_THEME, 3)]];
    [cv addSubview:hdr];

    NSScrollView* sv = [[[NSScrollView alloc] initWithFrame:NSMakeRect(16, 94, w - 32, h - 190)] autorelease];
    [sv setHasVerticalScroller:YES];
    [sv setBorderType:NSBezelBorder];
    CGFloat listH = OYK_THEME_COLOR_COUNT * 22.0 + 8.0;
    NSView* docView = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, w - 52, listH)] autorelease];

    if (!themeRoleLabels_) themeRoleLabels_ = [[NSMutableArray alloc] init];
    if (!themeRoleSwatches_) themeRoleSwatches_ = [[NSMutableArray alloc] init];
    [themeRoleLabels_ removeAllObjects];
    [themeRoleSwatches_ removeAllObjects];

    for (int r = 0; r < OYK_THEME_COLOR_COUNT; ++r) {
        CGFloat ry = listH - 22.0 * (r + 1) - 4.0;
        NSTextField* rowLbl = [[[NSTextField alloc] initWithFrame:NSMakeRect(6, ry, 420, 18)] autorelease];
        [rowLbl setEditable:NO]; [rowLbl setBezeled:NO];
        [rowLbl setFont:[NSFont fontWithName:@"Monaco" size:11.0]];
        [docView addSubview:rowLbl];
        [themeRoleLabels_ addObject:rowLbl];

        OykColorSwatchView* sw = [[[OykColorSwatchView alloc] initWithFrame:NSMakeRect(436, ry + 1, 60, 16)] autorelease];
        [docView addSubview:sw];
        [themeRoleSwatches_ addObject:sw];

        NSButton* selBtn = [[[NSButton alloc] initWithFrame:NSMakeRect(506, ry - 2, 62, 20)] autorelease];
        [selBtn setTitle:@"Select"];
        [selBtn setTag:r];
        [selBtn setTarget:self];
        [selBtn setAction:@selector(onThemeRoleClicked:)];
        [docView addSubview:selBtn];
    }
    [sv setDocumentView:docView];
    [cv addSubview:sv];

    NSTextField* clbl = [[[NSTextField alloc] initWithFrame:NSMakeRect(16, 56, 48, 20)] autorelease];
    [clbl setEditable:NO]; [clbl setBezeled:NO]; [clbl setDrawsBackground:NO];
    [clbl setStringValue:@"Hex:"];
    [cv addSubview:clbl];

    themeHexField_ = [[[NSTextField alloc] initWithFrame:NSMakeRect(66, 54, 96, 22)] autorelease];
    [themeHexField_ setFont:[NSFont fontWithName:@"Monaco" size:11.0]];
    [cv addSubview:themeHexField_];

    themeActiveSwatch_ = [[[OykColorSwatchView alloc] initWithFrame:NSMakeRect(172, 55, 36, 20)] autorelease];
    [cv addSubview:themeActiveSwatch_];

    NSButton* pickBtn = [[[NSButton alloc] initWithFrame:NSMakeRect(220, 52, 114, 26)] autorelease];
    [pickBtn setTitle:@"Pick Color..."];
    [pickBtn setTarget:self];
    [pickBtn setAction:@selector(onThemePickColor:)];
    [cv addSubview:pickBtn];

    NSButton* applyHexBtn = [[[NSButton alloc] initWithFrame:NSMakeRect(342, 52, 114, 26)] autorelease];
    [applyHexBtn setTitle:@"Apply Color"];
    [applyHexBtn setTarget:self];
    [applyHexBtn setAction:@selector(onThemeApplyHex:)];
    [cv addSubview:applyHexBtn];

    NSButton* cancelBtn = [[[NSButton alloc] initWithFrame:NSMakeRect(w - 220, 14, 92, 28)] autorelease];
    [cancelBtn setTitle:@"Cancel"];
    [cancelBtn setTarget:self];
    [cancelBtn setAction:@selector(onThemeCancel:)];
    [cv addSubview:cancelBtn];

    NSButton* saveBtn = [[[NSButton alloc] initWithFrame:NSMakeRect(w - 120, 14, 104, 28)] autorelease];
    [saveBtn setTitle:@"Save & Apply"];
    [saveBtn setKeyEquivalent:@"\r"];
    [saveBtn setTarget:self];
    [saveBtn setAction:@selector(onThemeSaveApply:)];
    [cv addSubview:saveBtn];

    themeSelectedRole_ = 0;
    [self rebuildThemePopup];
    [self applyThemeToAllWindows];
    [win makeKeyAndOrderFront:nil];
    return win;
}

- (void)runUiCaptureTour {
    struct CapItem { int scr; int wiz_step; const char* flow; };
    static const CapItem kFlows[22] = {
        { OYK_SCR_MAIN,       0, "main"             },
        { OYK_SCR_MENU,       0, "menu"             },
        { OYK_SCR_SETTINGS,   0, "settings"         },
        { OYK_SCR_BBS,        0, "settings_bbs"     },
        { OYK_SCR_CHAT,       0, "settings_chat"    },
        { OYK_SCR_IFACES,     0, "settings_ifaces"  },
        { OYK_SCR_PEERS,      0, "peers"            },
        { OYK_SCR_RIG,        0, "settings_rig"     },
        { OYK_SCR_PRINTER,    0, "settings_printer" },
        { OYK_SCR_ADD_IFACE,  0, "add_iface_1"      },
        { OYK_SCR_ADD_IFACE,  1, "add_iface_2"      },
        { OYK_SCR_ADD_IFACE,  2, "add_iface_3"      },
        { OYK_SCR_ADD_IFACE,  3, "add_iface_4"      },
        { OYK_SCR_EDIT_IFACE, 0, "edit_iface"       },
        { OYK_SCR_EDIT_PEER,  0, "edit_peer"        },
        { OYK_SCR_TERMINATE,  0, "terminate"        },
        { OYK_SCR_BAN,        0, "ban"              },
        { OYK_SCR_EXIT_PEERS, 0, "exit"             },
        { OYK_SCR_THEME,      0, "theme"            },
        { OYK_SCR_TERMINAL,   0, "terminal"         },
        { OYK_SCR_TERM_HELP,  0, "terminal_help"    },
        { OYK_SCR_FILES,      0, "files"            },
        { OYK_SCR_ABOUT,      0, "about"            }
    };
    [self refreshConsole];
    [self captureWindow:mainWindow_ asFlow:@"main"];
    for (int i = 1; i < 23; ++i) {
        if (kFlows[i].scr == OYK_SCR_THEME) {
            NSWindow* tw = [self presentThemeSettingsWindow];
            int startCount = themePreviewCount_;
            oyk_gui_theme_select(&themeCtrl_, "dark");
            NSColor* darkMainBg = [[mainWindow_ backgroundColor] retain];
            NSColor* darkDlgBg = [[tw backgroundColor] retain];
            oyk_gui_theme_select(&themeCtrl_, "light");
            NSColor* lightMainBg = [[mainWindow_ backgroundColor] retain];
            NSColor* lightDlgBg = [[tw backgroundColor] retain];
            char terr[64];
            oyk_gui_theme_set_role_rgb(&themeCtrl_, OYK_TR_BG, 0x223344UL, terr, sizeof(terr));
            oyk_gui_theme_revert(&themeCtrl_);
            if ((themePreviewCount_ - startCount) >= 4 &&
                oyk_u32_from_nscolor(darkMainBg, 0) != oyk_u32_from_nscolor(lightMainBg, 0) &&
                oyk_u32_from_nscolor(darkDlgBg, 0) != oyk_u32_from_nscolor(lightDlgBg, 0)) {
                printf("MACOS_THEME_PREVIEW_TEST_PASS\n");
                fflush(stdout);
            }
            [darkMainBg release]; [darkDlgBg release];
            [lightMainBg release]; [lightDlgBg release];
            [self rebuildThemePopup];
            [self captureWindow:tw asFlow:@"theme"];
            [tw close];
            [tw release];
            themeWindow_ = nil;
        } else {
            wiz_.step = kFlows[i].wiz_step;
            NSWindow* w = [self presentDialogScreen:kFlows[i].scr
                                           bodyText:[self buildScreenText:kFlows[i].scr]
                                             height:380.0];
            [self captureWindow:w asFlow:[NSString stringWithUTF8String:kFlows[i].flow]];
            [w close];
            [w release];
        }
    }
    engine_->stop();
    [NSApp terminate:nil];
}

- (void)dispatchGuiAction:(int)act {
    if (act == OYK_ACT_ALLOW_INCOMING) { engine_->set_accept_incoming(1); [self refreshConsole]; }
    else if (act == OYK_ACT_DENY_INCOMING) { engine_->set_accept_incoming(0); [self refreshConsole]; }
    else if (act == OYK_ACT_EXIT) [self requestExit:nil];
    else if (act == OYK_ACT_THEME) { [self presentThemeSettingsWindow]; }
    else {
        int scr = OYK_SCR_SETTINGS;
        if (act == OYK_ACT_LOCAL_TERM) scr = OYK_SCR_TERMINAL;
        else if (act == OYK_ACT_FILES) scr = OYK_SCR_FILES;
        else if (act == OYK_ACT_PEERS) scr = OYK_SCR_PEERS;
        else if (act == OYK_ACT_TERMINATE) scr = OYK_SCR_TERMINATE;
        else if (act == OYK_ACT_BAN) scr = OYK_SCR_BAN;
        else if (act == OYK_ACT_RIG) scr = OYK_SCR_RIG;
        NSWindow* w = [self presentDialogScreen:scr bodyText:[self buildScreenText:scr] height:380.0];
        (void)w;
    }
}

- (void)handleNodeMenuItem:(id)sender { [self dispatchGuiAction:static_cast<int>([sender tag])]; }
- (void)openSettings:(id)sender { (void)sender; [self dispatchGuiAction:OYK_ACT_SETTINGS]; }
- (void)openFiles:(id)sender { (void)sender; [self dispatchGuiAction:OYK_ACT_FILES]; }
- (void)focusConsole:(id)sender { (void)sender; [mainWindow_ makeKeyAndOrderFront:nil]; }
- (void)showAbout:(id)sender {
    (void)sender;
    NSRunAlertPanel(@"About OYK Packet Node",
                    @"OYK Amateur Radio Packet Node (macOS AppKit)\nShared oyk_gui + oyk_term middleware",
                    @"OK", nil, nil);
}
- (void)showHotkeys:(id)sender {
    (void)sender;
    NSMutableString* s = [NSMutableString string];
    for (int i = 0; i < oyk_term_help_count(); ++i) {
        const OykTermHelpEntry* e = oyk_term_help_entry(i);
        if (e) [s appendFormat:@"%-8s %s\n", e->key_label, e->action];
    }
    NSWindow* w = [self presentDialogScreen:OYK_SCR_TERMINAL bodyText:s height:340.0];
    (void)w;
}
- (void)openConfigPanel:(id)sender {
    (void)sender;
    NSOpenPanel* op = [NSOpenPanel openPanel];
    if ([op runModal] == NSOKButton && [[op filenames] count] > 0) {
        NSString* p = [[op filenames] objectAtIndex:0];
        char err[96];
        engine_->init_and_start([p UTF8String], 0, err, sizeof(err));
        [self refreshConsole];
    }
}
- (void)requestExit:(id)sender {
    (void)sender;
    OykMacOsSnapshot* snap = new OykMacOsSnapshot();
    engine_->get_snapshot(snap);
    if (snap->auto_learned_count > 0) {
        OykGuiExitPrompt ep;
        oyk_gui_build_exit_prompt(snap->auto_learned_count, snap->active_connections, &ep);
        NSInteger r = NSRunAlertPanel([NSString stringWithUTF8String:ep.title],
                                      @"%@", @"Save & Quit", @"Discard", @"Cancel",
                                      [NSString stringWithUTF8String:ep.body]);
        if (r == NSAlertDefaultReturn) {
            char err[96];
            engine_->persist_auto_learned(err, sizeof(err));
        } else if (r == NSAlertOtherReturn) {
            delete snap;
            return;
        }
    }
    delete snap;
    engine_->stop();
    [NSApp terminate:nil];
}

- (void)onPollTimer:(NSTimer*)t {
    (void)t;
    engine_->poll();
    [self refreshConsole];
}

- (void)startWithArgs:(const char*)cfgPath {
    const char* tenv = getenv("OYK_UI_TEST_MODE");
    const char* cenv = getenv("OYK_UI_CAPTURE_DIR");
    uiTestMode_ = (tenv && strcmp(tenv, "1") == 0) ? 1 : 0;
    if (cenv && cenv[0]) captureDir_ = [[NSString stringWithUTF8String:cenv] retain];

    engine_ = new OykMacOsEngine();
    char err[128];
    err[0] = '\0';
    engine_->init_and_start(cfgPath, uiTestMode_, err, sizeof(err));
    draftCfg_ = engine_->portable_config();
    oyk_gui_wiz_init(&wiz_, &draftCfg_, OYK_DRV_ALL);
    OykThemeConfig osPal;
    [self queryOsNativeTheme:&osPal];
    oyk_gui_theme_init(&themeCtrl_, &draftCfg_, 1, osPal.is_dark_base);
    oyk_gui_theme_set_os_native(&themeCtrl_, &osPal);
    oyk_gui_theme_set_preview_cb(&themeCtrl_, macos_theme_preview_trampoline, self);
    oyk_gui_theme_resolve(&themeCtrl_, &activeTheme_);
    oyk_term_eol_init(&termEol_, OYK_TERM_EOL_AUTO, 1);

    [self buildMenuBar];

    NSRect frame = NSMakeRect(120, 120, 760, 520);
    mainWindow_ = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                             NSMiniaturizableWindowMask | NSResizableWindowMask)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [mainWindow_ setTitle:[NSString stringWithUTF8String:oyk_gui_screen_title(OYK_SCR_MAIN)]];
    [mainWindow_ setBackgroundColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_BG])];

    bannerLabel_ = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 484, 736, 24)];
    [bannerLabel_ setEditable:NO];
    [bannerLabel_ setBezeled:NO];
    [bannerLabel_ setDrawsBackground:YES];
    [bannerLabel_ setBackgroundColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_HEADER_BG])];
    [bannerLabel_ setTextColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_HEADER_FG])];
    [bannerLabel_ setFont:[NSFont fontWithName:@"Monaco" size:11.0]];
    [bannerLabel_ setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];
    [[mainWindow_ contentView] addSubview:bannerLabel_];

    consoleScrollView_ = [[NSScrollView alloc] initWithFrame:NSMakeRect(12, 12, 736, 464)];
    [consoleScrollView_ setHasVerticalScroller:YES];
    [consoleScrollView_ setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    consoleTextView_ = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 720, 464)];
    [consoleTextView_ setEditable:NO];
    [consoleTextView_ setFont:[NSFont fontWithName:@"Monaco" size:11.0]];
    [consoleTextView_ setBackgroundColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_BG])];
    [consoleTextView_ setTextColor:oyk_color_from_u32(activeTheme_.colors[OYK_TR_FG])];
    [consoleScrollView_ setDocumentView:consoleTextView_];
    [[mainWindow_ contentView] addSubview:consoleScrollView_];

    [self applyThemeToAllWindows];
    [mainWindow_ makeKeyAndOrderFront:nil];
    [self refreshConsole];

    if (uiTestMode_) {
        [self performSelector:@selector(runUiCaptureTour) withObject:nil afterDelay:0.15];
    } else {
        pollTimer_ = [[NSTimer scheduledTimerWithTimeInterval:0.25
                                                       target:self
                                                     selector:@selector(onPollTimer:)
                                                     userInfo:nil
                                                      repeats:YES] retain];
    }
}

@end

int main(int argc, const char* argv[]) {
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
    SEL actSel = @selector(setActivationPolicy:);
    if ([NSApp respondsToSelector:actSel]) {
        NSMethodSignature* sig = [NSApp methodSignatureForSelector:actSel];
        if (sig) {
            NSInvocation* inv = [NSInvocation invocationWithMethodSignature:sig];
            [inv setTarget:NSApp];
            [inv setSelector:actSel];
            NSInteger policy = 0; /* NSApplicationActivationPolicyRegular */
            [inv setArgument:&policy atIndex:2];
            [inv invoke];
        }
    }
    const char* cfg = NULL;
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], "--config") == 0) cfg = argv[i + 1];
    }
    OykAppDelegate* del = [[OykAppDelegate alloc] init];
    [NSApp setDelegate:del];
    [del startWithArgs:cfg];
    [NSApp run];
    [del release];
    [pool release];
    return 0;
}
