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

#endif /* OYK_WIN_RESOURCE_H */
