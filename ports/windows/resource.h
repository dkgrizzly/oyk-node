/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_WIN_RESOURCE_H
#define OYK_WIN_RESOURCE_H

/* Menu & Command IDs */
#define IDR_MAINMENU        101
#define IDM_ALLOW_INCOMING  201
#define IDM_DENY_INCOMING   202
#define IDM_TERMINAL        203
#define IDM_APPROVE_FILES   207
#define IDM_SETTINGS        208
#define IDM_TERMINATE       204
#define IDM_BAN             205
#define IDM_EXIT            206

/* Main Window Control IDs */
#define IDC_HEADER_TEXT     301
#define IDC_INCOMING_BADGE  302
#define IDC_IFACE_LIST      303
#define IDC_HEARD_LIST      304
#define IDC_SESSION_LIST    305
#define IDC_LOG_LIST        306
#define IDC_STATUS_TEXT     307

/* Modal Dialog IDs */
#define IDD_TERMINATE       401
#define IDC_TERM_LIST       402

#define IDD_BAN             410
#define IDC_BAN_CALL        411
#define IDC_BAN_REASON      412
#define IDC_BAN_HARD        413
#define IDC_BAN_APPEAL      414
#define IDC_BAN_LIST        415
#define IDC_BAN_ADD         416
#define IDC_BAN_REMOVE      417
#define IDC_BAN_LIFT        418

#define IDD_TERMINAL        420
#define IDC_LTERM_PORT      421
#define IDC_LTERM_TARGET    422
#define IDC_LTERM_CONNECT   423
#define IDC_LTERM_DISC      424
#define IDC_LTERM_UPLOAD    425
#define IDC_LTERM_ABORT     426
#define IDC_LTERM_OUTPUT    427
#define IDC_LTERM_STATUS    428
#define IDC_LTERM_INPUT     429
#define IDC_LTERM_SEND      430
#define IDC_LTERM_LBL_PORT  431
#define IDC_LTERM_LBL_TGT   432

#define IDD_APPROVE         440
#define IDC_APP_LIST        441
#define IDC_APP_SUBDIR      442
#define IDC_APP_APPROVE     443
#define IDC_APP_DELETE      444
#define IDC_APP_REFRESH     445
#define IDC_APP_STATUS      446

#define IDD_SETTINGS        450
#define IDC_SET_CALL        451
#define IDC_SET_ALIAS       452
#define IDC_SET_ACCEPT      453
#define IDC_SET_WELCOME     454
#define IDC_SET_INFO        455
#define IDC_SET_BBS_EN      456
#define IDC_SET_BBS_SSID    457
#define IDC_SET_BBS_CALL    458
#define IDC_SET_BBS_ALIAS   459
#define IDC_SET_FILES_EN    460
#define IDC_SET_FILES_PATH  461
#define IDC_SET_BBS_WELC    462
#define IDC_SET_CHAT_EN     463
#define IDC_SET_CHAT_SSID   464
#define IDC_SET_CHAT_CALL   465
#define IDC_SET_CHAT_ALIAS  466
#define IDC_SET_CHAT_TOPIC  467
#define IDC_SET_CHAT_WELC   468
#define IDC_SET_IF_LIST     469
#define IDC_SET_IF_ADD      470
#define IDC_SET_IF_REM      471

#define IDD_ADD_IFACE       480
#define IDC_AIF_ID          481
#define IDC_AIF_TYPE        482
#define IDC_AIF_DESC        483
#define IDC_AIF_HOST        484
#define IDC_AIF_PORT        485
#define IDC_AIF_PEER        486

#endif /* OYK_WIN_RESOURCE_H */
