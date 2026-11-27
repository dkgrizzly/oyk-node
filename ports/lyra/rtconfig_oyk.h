/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_LYRA_RTCONFIG_OYK_H
#define OYK_LYRA_RTCONFIG_OYK_H

/* Map RT-Thread Kconfig selections to OYK Luckfox Lyra build defines. */
#if defined(PKG_OYK_NODE_LYRA_BOARD_PICOCALC_LYRA_PLUS)
  #define OYK_LYRA_BOARD_ID    "picocalc_lyra_plus"
  #define OYK_LYRA_IS_PICOCALC 1
#elif defined(PKG_OYK_NODE_LYRA_BOARD_PICOCALC_LYRA_B)
  #define OYK_LYRA_BOARD_ID    "picocalc_lyra_b"
  #define OYK_LYRA_IS_PICOCALC 1
#elif defined(PKG_OYK_NODE_LYRA_BOARD_PICOCALC_LYRA)
  #define OYK_LYRA_BOARD_ID    "picocalc_lyra"
  #define OYK_LYRA_IS_PICOCALC 1
#elif defined(PKG_OYK_NODE_LYRA_BOARD_LYRA_ZERO_W)
  #define OYK_LYRA_BOARD_ID    "lyra_zero_w"
  #define OYK_LYRA_IS_PICOCALC 0
#elif defined(PKG_OYK_NODE_LYRA_BOARD_LYRA_PLUS)
  #define OYK_LYRA_BOARD_ID    "lyra_plus"
  #define OYK_LYRA_IS_PICOCALC 0
#elif defined(PKG_OYK_NODE_LYRA_BOARD_LYRA_B)
  #define OYK_LYRA_BOARD_ID    "lyra_b"
  #define OYK_LYRA_IS_PICOCALC 0
#elif defined(PKG_OYK_NODE_LYRA_BOARD_LYRA)
  #define OYK_LYRA_BOARD_ID    "lyra"
  #define OYK_LYRA_IS_PICOCALC 0
#endif

#if defined(PKG_OYK_NODE_PICOCALC_AUDIO)
  #define OYK_LYRA_ENABLE_AUDIO 1
#endif

#endif /* OYK_LYRA_RTCONFIG_OYK_H */
