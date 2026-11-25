/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_TINYTMUX_CONFIG_H
#define OYK_TINYTMUX_CONFIG_H

/*
 * Feature and target configuration macros for the Teensy 4.1 / TinyTMUX port.
 * Works identically under Arduino 1.8.19 + Teensyduino 1.62 and host CMake tests.
 */

#ifndef OYK_TINYTMUX
#define OYK_TINYTMUX 1
#endif

#ifndef OYK_TARGET_IMXRT1060
#if defined(TEENSYDUINO)
#define OYK_TARGET_IMXRT1060 1
#endif
#endif

#ifndef OYK_HAS_FATFS
#if defined(TEENSYDUINO)
#define OYK_HAS_FATFS 1
#endif
#endif

#ifndef OYK_HAS_LWIP
#define OYK_HAS_LWIP 0
#endif

#ifndef OYK_HAS_TINYUSB
#define OYK_HAS_TINYUSB 0
#endif

#endif /* OYK_TINYTMUX_CONFIG_H */
