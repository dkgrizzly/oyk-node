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
#define IDM_RIG_CONTROL     209
#define IDM_SETTINGS        208
#define IDM_PEERS           210
#define IDM_THEME           211
#define IDM_TERMINATE       204
#define IDM_BAN             205
#define IDM_EXIT            206
#define IDM_ABOUT           212

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
#define IDC_LTERM_HELP      433

#define IDD_APPROVE           440
#define IDC_APP_TAB_INC       441
#define IDC_APP_TAB_APPROVED  442
#define IDC_APP_TAB_BOX       443
#define IDC_APP_L_INC         444
#define IDC_APP_LIST          445
#define IDC_APP_L_SUB         446
#define IDC_APP_SUBDIR        447
#define IDC_APP_APPROVE       448
#define IDC_APP_DELETE        449
#define IDC_APP_L_APP         510
#define IDC_APP_APPROVED_LIST 511
#define IDC_APP_REMOVE        512
#define IDC_APP_REFRESH       513
#define IDC_APP_STATUS        514

#define IDD_SETTINGS        450
#define IDC_SET_TAB_NODE    451
#define IDC_SET_TAB_BBS     452
#define IDC_SET_TAB_CHAT    453
#define IDC_SET_TAB_IF      454
#define IDC_SET_TAB_PEERS   497
#define IDC_SET_TAB_RIG     494
#define IDC_SET_TAB_PRN     495
#define IDC_SET_TAB_BOX     455

#define IDC_SET_L_CALL      456
#define IDC_SET_CALL        457
#define IDC_SET_L_ALIAS     458
#define IDC_SET_ALIAS       459
#define IDC_SET_L_WELC      460
#define IDC_SET_WELCOME     461
#define IDC_SET_L_INFO      462
#define IDC_SET_INFO        463
#define IDC_SET_L_LPOL      498
#define IDC_SET_LEARN_POL   499
#define IDC_SET_L_NWELC     507
#define IDC_SET_NEW_WELC    508
#define IDC_SET_L_IFFILE    509
#define IDC_SET_IF_FILE     510
#define IDC_SET_L_PRFILE    511
#define IDC_SET_PR_FILE     512
#define IDC_SET_L_THFILE    515
#define IDC_SET_TH_FILE     516
#define IDC_SET_ACCEPT      464

#define IDC_SET_BBS_EN      465
#define IDC_SET_L_BSSID     466
#define IDC_SET_BBS_SSID    467
#define IDC_SET_L_BCALL     468
#define IDC_SET_BBS_CALL    469
#define IDC_SET_L_BALIAS    470
#define IDC_SET_BBS_ALIAS   471
#define IDC_SET_FILES_EN    472
#define IDC_SET_L_FPATH     473
#define IDC_SET_FILES_PATH  474
#define IDC_SET_L_MPATH     475
#define IDC_SET_MAIL_PATH   476
#define IDC_SET_L_BWELC     477
#define IDC_SET_BBS_WELC    478

#define IDC_SET_CHAT_EN     479
#define IDC_SET_L_CSSID     480
#define IDC_SET_CHAT_SSID   481
#define IDC_SET_L_CCALL     482
#define IDC_SET_CHAT_CALL   483
#define IDC_SET_L_CALIAS    484
#define IDC_SET_CHAT_ALIAS  485
#define IDC_SET_L_CTOP      486
#define IDC_SET_CHAT_TOPIC  487
#define IDC_SET_L_CWELC     488
#define IDC_SET_CHAT_WELC   489

#define IDC_SET_L_IF        490
#define IDC_SET_IF_LIST     491
#define IDC_SET_IF_ADD      492
#define IDC_SET_IF_REM      493
#define IDC_SET_IF_EDIT     496

#define IDD_ADD_IFACE       500
#define IDC_AIF_ID          501
#define IDC_AIF_TYPE        502
#define IDC_AIF_DESC        503
#define IDC_AIF_HOST        504
#define IDC_AIF_PORT        505
#define IDC_AIF_PEER        506

/* Settings Tab 5: Rig Control */
#define IDC_SET_RIG_EN      520
#define IDC_SET_L_RPROTO    521
#define IDC_SET_RIG_PROTO   522
#define IDC_SET_L_RHOST     523
#define IDC_SET_RIG_HOST    524
#define IDC_SET_L_RPORT     525
#define IDC_SET_RIG_PORT    526
#define IDC_SET_L_RPOLL     527
#define IDC_SET_RIG_POLL    528
#define IDC_SET_RIG_NOTIFY  529

/* Radio / Rig Control Dialog */
#define IDD_RIG_CONTROL     540
#define IDC_RIG_HEADER      541
#define IDC_RIG_STATE       542
#define IDC_RIG_L_A         543
#define IDC_RIG_EDIT_A      544
#define IDC_RIG_L_B         545
#define IDC_RIG_EDIT_B      546
#define IDC_RIG_L_C         547
#define IDC_RIG_EDIT_C      548
#define IDC_RIG_BTN_1       549
#define IDC_RIG_BTN_2       550
#define IDC_RIG_BTN_3       551
#define IDC_RIG_BTN_4       552
#define IDC_RIG_BTN_5       553
#define IDC_RIG_BTN_REF     554
#define IDC_RIG_STATUS      555

/* Settings Tab 6: Line Printer */
#define IDC_SET_PRN_EN      560
#define IDC_SET_L_PDEV      561
#define IDC_SET_PRN_DEV     562
#define IDC_SET_L_PPROTO    563
#define IDC_SET_PRN_PROTO   564
#define IDC_SET_L_PBAUD     565
#define IDC_SET_PRN_BAUD    566
#define IDC_SET_L_PLVL      567
#define IDC_SET_PRN_LVL     568
#define IDC_SET_PRN_COND    569

/* Add Interface Wizard Controls */
#define IDC_AIF_PROMPT      570
#define IDC_AIF_PROG        571
#define IDC_AIF_DRV_LIST    572
#define IDC_AIF_L_ID        573
#define IDC_AIF_L_DESC      574
#define IDC_AIF_L_HOST      575
#define IDC_AIF_L_PORT      576
#define IDC_AIF_L_PEER      577
#define IDC_AIF_SUMMARY     578
#define IDC_AIF_ERR         579
#define IDC_AIF_BACK        580
#define IDC_AIF_NEXT        581
#define IDC_AIF_L_MASK      582
#define IDC_AIF_MASK        583
#define IDC_AIF_IN          584
#define IDC_AIF_OUT         585
#define IDC_AIF_L_LPOL      586
#define IDC_AIF_LPOL        587
#define IDC_AIF_SAME        588

/* Single-View Edit Interface Dialog */
#define IDD_EDIT_IFACE      590
#define IDC_EIF_DRV         591
#define IDC_EIF_ID          592
#define IDC_EIF_DESC        593
#define IDC_EIF_L_HOST      594
#define IDC_EIF_HOST        595
#define IDC_EIF_L_PORT      596
#define IDC_EIF_PORT        597
#define IDC_EIF_L_PEER      598
#define IDC_EIF_PEER        599
#define IDC_EIF_ERR         600
#define IDC_EIF_L_MASK      601
#define IDC_EIF_MASK        602
#define IDC_EIF_IN          603
#define IDC_EIF_OUT         604
#define IDC_EIF_L_LPOL      605
#define IDC_EIF_LPOL        606
#define IDC_EIF_SAME        607

/* Peers Tab & Edit Peer Dialog */
#define IDC_SET_L_PEER      610
#define IDC_SET_PEER_LIST   611
#define IDC_SET_PEER_ADD    612
#define IDC_SET_PEER_EDIT   613
#define IDC_SET_PEER_REM    614
#define IDC_SET_PEER_KEEP   615

#define IDD_EDIT_PEER       620
#define IDC_EP_ID           621
#define IDC_EP_CALL         622
#define IDC_EP_DESC         623
#define IDC_EP_IFACE        624
#define IDC_EP_HOST         625
#define IDC_EP_PORT         626
#define IDC_EP_IN           627
#define IDC_EP_OUT          628
#define IDC_EP_ERR          629

/* Auto-Learned Peers Exit Review Dialog */
#define IDD_EXIT_PEERS      640
#define IDC_XPR_MSG         641
#define IDC_XPR_DISCARD     642
#define IDC_XPR_REVIEW      643

/* Theme Settings Dialog */
#define IDD_THEME           650
#define IDC_TH_COMBO        651
#define IDC_TH_DEL          652
#define IDC_TH_HINT         653
#define IDC_TH_LIST         654
#define IDC_TH_HEX          655
#define IDC_TH_PICK         656
#define IDC_TH_APPLY_COLOR  657
#define IDC_TH_PREVIEW      658

/* About Dialog */
#define IDD_ABOUT           660
#define IDC_ABOUT_TEXT      661

/* Terminal Hotkey Help Dialog */
#define IDD_TERM_HELP       670
#define IDC_THELP_TEXT      671

/* Dual-Pane Jailed File Manager Controls */
#define IDC_APP_LBL_INC     672
#define IDC_APP_LBL_FILES   673
#define IDC_APP_UPLOAD      674
#define IDC_APP_MKDIR       675
#define IDC_APP_RENAME      676
#define IDC_APP_VIEW        677

/* Generic Input Prompt Dialog */
#define IDD_FM_PROMPT       680
#define IDC_FMP_LABEL       681
#define IDC_FMP_EDIT        682

#endif /* OYK_WIN_RESOURCE_H */
