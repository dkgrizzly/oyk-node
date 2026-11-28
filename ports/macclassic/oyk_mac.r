/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "Multiverse.r"
#include "oyk_mac_icons.r"

resource 'MBAR' (128) {
    { 128, 129, 130, 131, 132, 133 };
};

resource 'MENU' (128) {
    128,
    textMenuProc,
    0x7FFFFFFD,
    enabled,
    apple,
    {
        "About OYK Packet Node...", noIcon, noKey, noMark, plain;
        "-",                        noIcon, noKey, noMark, plain;
    };
};

resource 'MENU' (129) {
    129,
    textMenuProc,
    0x7FFFFFF7,
    enabled,
    "File",
    {
        "Open Config...",        noIcon, "O",   noMark, plain;
        "Save Config",           noIcon, "S",   noMark, plain;
        "Manage Files Area...",  noIcon, noKey, noMark, plain;
        "-",                     noIcon, noKey, noMark, plain;
        "Quit",                  noIcon, "Q",   noMark, plain;
    };
};

resource 'MENU' (130) {
    130,
    textMenuProc,
    0x7FFFFFFD,
    enabled,
    "Edit",
    {
        "Undo",  noIcon, "Z", noMark, plain;
        "-",     noIcon, noKey, noMark, plain;
        "Cut",   noIcon, "X", noMark, plain;
        "Copy",  noIcon, "C", noMark, plain;
        "Paste", noIcon, "V", noMark, plain;
        "Clear", noIcon, noKey, noMark, plain;
    };
};

resource 'MENU' (131) {
    131,
    textMenuProc,
    0x7FFFFDFB,
    enabled,
    "Node",
    {
        "Allow Incoming Connections", noIcon, noKey, noMark, plain;
        "Deny Incoming Connections",  noIcon, noKey, noMark, plain;
        "-",                          noIcon, noKey, noMark, plain;
        "Local Terminal...",          noIcon, "T",   noMark, plain;
        "Manage Files Area...",       noIcon, noKey, noMark, plain;
        "Radio Control...",           noIcon, noKey, noMark, plain;
        "Settings...",                noIcon, ",",   noMark, plain;
        "Terminate Connection...",    noIcon, noKey, noMark, plain;
        "Ban / Unban Callsign...",    noIcon, noKey, noMark, plain;
        "-",                          noIcon, noKey, noMark, plain;
        "Quit",                       noIcon, "Q",   noMark, plain;
    };
};

resource 'MENU' (132) {
    132,
    textMenuProc,
    allEnabled,
    enabled,
    "Window",
    {
        "Operator Console",   noIcon, "1", noMark, plain;
        "Local Terminal",     noIcon, "2", noMark, plain;
        "Manage Files Area",  noIcon, "3", noMark, plain;
        "Radio Control",      noIcon, "4", noMark, plain;
    };
};

resource 'MENU' (133) {
    133,
    textMenuProc,
    allEnabled,
    enabled,
    "Help",
    {
        "Terminal Hotkeys (^A ?)", noIcon, "?", noMark, plain;
        "About OYK Packet Node...", noIcon, noKey, noMark, plain;
    };
};

resource 'ALRT' (128) {
    { 60, 80, 180, 420 },
    128,
    {
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
    },
    centerMainScreen
};

resource 'DITL' (128) {
    {
        { 88, 250, 108, 326 },
        Button { enabled, "OK" };
        { 12, 16, 78, 326 },
        StaticText { disabled, "^0" };
    };
};

resource 'vers' (1) {
    0x01,
    0x00,
    release,
    0x00,
    verUS,
    "1.0",
    "OYK Amateur Radio Packet Node 1.0"
};

resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    2048 * 1024,
    1024 * 1024
};

#ifdef OYK_FAT_BINARY
resource 'cfrg' (0) {
    {
        kPowerPCCFragArch, kIsCompleteCFrag, kNoVersionNum, kNoVersionNum,
        kDefaultStackSize, kNoAppSubFolder,
        kApplicationCFrag, kDataForkCFragLocator, kZeroOffset, kCFragGoesToEOF,
        "OYK Node"
    };
};
#endif
