/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * TinyUSB configuration for OYK Packet Node on IMXRT1060 (Teensy 4.1 / TinyTMUX).
 * TinyUSB is licensed under the MIT license (GPL-3.0-or-later compatible).
 *
 * Exposes 2 USB-CDC ACM ports on USB OTG1 (High-Speed):
 *   - CDC0 (usb0): Local Operator VT100/ANSI Console & Local Packet Terminal
 *   - CDC1 (usb1): Auxiliary KISS TNC / Diagnostic / Secondary Console port
 */

#ifndef OYK_TINYTMUX_TUSB_CONFIG_H
#define OYK_TINYTMUX_TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU                OPT_MCU_MIMXRT10XX
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG              0
#endif

/* Use USB_OTG1 (root hub port 0) in High-Speed Device mode for Dual CDC console */
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED)
#define BOARD_TUD_RHPORT            0
#define BOARD_TUD_MAX_SPEED         OPT_MODE_HIGH_SPEED

/* Use USB_OTG2 (root hub port 1) in High-Speed Host mode for RadioFoundry radios */
#define CFG_TUSB_RHPORT1_MODE       (OPT_MODE_HOST | OPT_MODE_HIGH_SPEED)
#define BOARD_TUH_RHPORT            1
#define BOARD_TUH_MAX_SPEED         OPT_MODE_HIGH_SPEED

/* Place TinyUSB DMA buffers in non-cached OCRAM2 (.dmamem) */
#define CFG_TUSB_MEM_SECTION        __attribute__((section(".dmamem")))
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(64)))

#define CFG_TUD_ENABLED             1
#define CFG_TUD_ENDPOINT0_SIZE      64

/* Device Classes: Dual USB-CDC ACM (matching TinyTMUX usb0 + usb1) */
#define CFG_TUD_CDC                 2
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0

/* CDC Device FIFO buffer sizes */
#define CFG_TUD_CDC_RX_BUFSIZE      1024
#define CFG_TUD_CDC_TX_BUFSIZE      2048
#define CFG_TUD_CDC_EP_BUFSIZE      512

/* Host Stack (Teensy 4.1 USB_OTG2 host port with hub support) */
#define CFG_TUH_ENABLED             1
#define CFG_TUH_ENUMERATION_BUFSIZE 512
#define CFG_TUH_HUB                 1
#define CFG_TUH_DEVICE_MAX          4
#define CFG_TUH_CDC                 4
#define CFG_TUH_CDC_FTDI            0
#define CFG_TUH_CDC_CP210X          0
#define CFG_TUH_CDC_CH34X           0
#define CFG_TUH_NCM                 2
#define CFG_TUH_MSC                 0
#define CFG_TUH_HID                 0
#define CFG_TUH_VENDOR              0

#define CFG_TUH_CDC_RX_BUFSIZE      1024
#define CFG_TUH_CDC_TX_BUFSIZE      1024
#define CFG_TUH_CDC_EP_BUFSIZE      512

#ifdef __cplusplus
}
#endif

#endif /* OYK_TINYTMUX_TUSB_CONFIG_H */
