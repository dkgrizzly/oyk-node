/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * TinyUSB Dual-CDC USB Descriptors for OYK Packet Node (Teensy 4.1 / TinyTMUX).
 */

#if defined(OYK_HAS_TINYUSB)

#include "tusb.h"
#include <string.h>

#define OYK_USB_VID     0x16C0 /* Van Ooijen Technische Informatica / Teensy shared VID */
#define OYK_USB_PID     0x048B /* Dual USB-CDC ACM */
#define OYK_USB_BCD     0x0200

static tusb_desc_device_t const s_desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = OYK_USB_BCD,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = OYK_USB_VID,
    .idProduct          = OYK_USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const* tud_descriptor_device_cb(void) {
    return (uint8_t const*)&s_desc_device;
}

enum {
    ITF_NUM_CDC_0 = 0,
    ITF_NUM_CDC_0_DATA,
    ITF_NUM_CDC_1,
    ITF_NUM_CDC_1_DATA,
    ITF_NUM_TOTAL
};

#define EPNUM_CDC_0_NOTIF   0x81
#define EPNUM_CDC_0_OUT     0x02
#define EPNUM_CDC_0_IN      0x82

#define EPNUM_CDC_1_NOTIF   0x83
#define EPNUM_CDC_1_OUT     0x04
#define EPNUM_CDC_1_IN      0x84

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + 2 * TUD_CDC_DESC_LEN)

static uint8_t const s_desc_fs_config[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 250),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_0, 4, EPNUM_CDC_0_NOTIF, 8, EPNUM_CDC_0_OUT, EPNUM_CDC_0_IN, 64),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_1, 5, EPNUM_CDC_1_NOTIF, 8, EPNUM_CDC_1_OUT, EPNUM_CDC_1_IN, 64),
};

static uint8_t const s_desc_hs_config[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 250),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_0, 4, EPNUM_CDC_0_NOTIF, 8, EPNUM_CDC_0_OUT, EPNUM_CDC_0_IN, 512),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_1, 5, EPNUM_CDC_1_NOTIF, 8, EPNUM_CDC_1_OUT, EPNUM_CDC_1_IN, 512),
};

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return (tud_speed_get() == TUSB_SPEED_HIGH) ? s_desc_hs_config : s_desc_fs_config;
}

static char const* s_string_desc[] = {
    (const char[]){ 0x09, 0x04 },            /* 0: English (US) */
    "KR4OYK",                                /* 1: Manufacturer */
    "OYK Packet Node (TinyTMUX Teensy 4.1)", /* 2: Product */
    "OYK-IMXRT1062",                         /* 3: Serial */
    "OYK Operator Console (usb0)",           /* 4: CDC0 */
    "OYK Auxiliary / KISS (usb1)"            /* 5: CDC1 */
};

static uint16_t s_desc_str[48];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count = 0;

    if (index == 0) {
        memcpy(&s_desc_str[1], s_string_desc[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(s_string_desc) / sizeof(s_string_desc[0])) {
            return NULL;
        }
        const char* str = s_string_desc[index];
        chr_count = strlen(str);
        if (chr_count > 47) chr_count = 47;
        for (size_t i = 0; i < chr_count; ++i) {
            s_desc_str[1 + i] = (uint16_t)str[i];
        }
    }

    s_desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return s_desc_str;
}

#endif /* OYK_HAS_TINYUSB */
