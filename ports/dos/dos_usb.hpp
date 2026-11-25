/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_DOS_USB_HPP
#define OYK_DOS_USB_HPP

/*
 * MS-DOS RadioFoundry Transport & Bret Johnson DOS USB Stack (USBDOS / USBUHCI)
 *
 * Design principles:
 *   - ZERO compile-time dependency on Bret Johnson's USB SDK or Michael Brutman's mTCP.
 *   - ZERO runtime hard requirement: if USBUHCI/USBUHCIL, Crynwr Packet Driver, or
 *     mTCP are not loaded, probes fail gracefully and oyknode.exe continues running
 *     with whichever transports are available (COM/INT14 KISS, local console, etc.).
 *   - Supports talking to RadioFoundry radios over:
 *       1. USB CDC Network interface (CDC-ECM / CDC-NCM) via Bret Johnson's USBDOS
 *          stack or a Crynwr Packet Driver (INT 60h..7Fh).
 *       2. USB CDC Serial interface (CDC-ACM) via Bret Johnson's USBDOS / USBCOM
 *          (INT 14h / COM1..COM4) carrying either KISS serial or RFC 1661/1662 PPP
 *          (IPv4/UDP for AXUDP + RadioFoundry UDP 4191 zone/channel control).
 *       3. Standard PC COM1..COM4 8250/16550 UART or BIOS INT 14h KISS serial.
 */

#include "../compat/oyk_portable.hpp"

/* Standard USB CDC Class / Subclass / Request constants */
#define OYK_USB_CLASS_CDC            0x02U
#define OYK_USB_SUBCLASS_ACM         0x02U
#define OYK_USB_SUBCLASS_ECM         0x06U
#define OYK_USB_SUBCLASS_NCM         0x0DU
#define OYK_USB_CLASS_CDC_DATA       0x0AU

#define OYK_USB_CDC_SET_LINE_CODING        0x20U
#define OYK_USB_CDC_GET_LINE_CODING        0x21U
#define OYK_USB_CDC_SET_CONTROL_LINE_STATE 0x22U

/* Transport mode for a RadioFoundry / DOS link */
#define OYK_DOS_LINK_NONE            0
#define OYK_DOS_LINK_USBDOS_CDC_NET  1
#define OYK_DOS_LINK_USBDOS_CDC_SER  2
#define OYK_DOS_LINK_PKTDRV_NET      3
#define OYK_DOS_LINK_SERIAL_COM      4

struct OykDosUsbStackInfo {
    int installed;
    oyk_u8 amis_mux_id;      /* AMIS INT 2Dh multiplex ID if detected */
    oyk_u16 version;         /* BCD version from AMIS CX */
    oyk_u16 entry_seg;       /* Private API far pointer segment */
    oyk_u16 entry_off;       /* Private API far pointer offset */
    int host_count;          /* Detected UHCI/OHCI/EHCI host controllers */
    char vendor[12];
    char product[12];
};

struct OykDosCdcDeviceInfo {
    int found;
    int is_network;          /* 1 = CDC-ECM/NCM, 0 = CDC-ACM serial */
    int is_ncm;              /* 1 = CDC-NCM (NTH16/NDP16 framing), 0 = CDC-ECM */
    oyk_u8 host_index;
    oyk_u8 dev_addr;
    oyk_u8 ctrl_iface;
    oyk_u8 data_iface;
    oyk_u8 ep_in;
    oyk_u8 ep_out;
    oyk_u16 max_pkt;
    oyk_u8 mac[6];
};

/* USB CDC helper packet builders (also portable & unit-testable on host) */
size_t oyk_usb_cdc_build_set_line_coding(oyk_u32 baud, oyk_u8 stop_bits,
                                         oyk_u8 parity, oyk_u8 data_bits,
                                         oyk_u8 setup_out[8], oyk_u8 data_out[7]);
size_t oyk_usb_cdc_build_set_ctrl_line_state(int dtr, int rts,
                                             oyk_u8 iface_num, oyk_u8 setup_out[8]);

/* USB CDC-NCM 16-bit (NTH16 + NDP16) Ethernet frame wrap & unwrap helpers */
size_t oyk_usb_ncm16_wrap(const oyk_u8* eth_frame, size_t eth_len,
                          oyk_u16 seq, oyk_u8* out, size_t out_cap);
size_t oyk_usb_ncm16_unwrap(const oyk_u8* ntb, size_t ntb_len,
                            oyk_u8* eth_out, size_t eth_cap);

/* Standalone lightweight RFC 1661/1662 PPP + IPv4/UDP framing for RadioFoundry */
typedef void (*OykDosUdpRxCallback)(oyk_u32 src_ip, oyk_u16 src_port,
                                    oyk_u16 dst_port,
                                    const oyk_u8* payload, size_t len,
                                    void* ctx);

typedef int (*OykDosRawTxByteFn)(oyk_u8 byte, void* ctx);
typedef int (*OykDosRawTxEthFn)(const oyk_u8* frame, size_t len, void* ctx);

struct OykDosMiniNet {
    oyk_u32 local_ip;        /* Network byte order (e.g. 192.168.7.2) */
    oyk_u32 peer_ip;         /* Network byte order (e.g. 192.168.7.1) */
    oyk_u8 local_mac[6];
    oyk_u8 peer_mac[6];
    int has_peer_mac;
    oyk_u16 ip_id;

    /* PPP HDLC RX decoder state */
    int ppp_in_frame;
    int ppp_escaped;
    int ppp_up;
    oyk_u16 ppp_len;
    oyk_u8 ppp_rx[600];

    OykDosRawTxByteFn tx_byte;
    void* tx_byte_ctx;
    OykDosRawTxEthFn tx_eth;
    void* tx_eth_ctx;
    OykDosUdpRxCallback udp_cb;
    void* udp_ctx;
};

void oyk_dos_mininet_init(OykDosMiniNet* net, oyk_u32 local_ip, oyk_u32 peer_ip);
void oyk_dos_mininet_feed_eth(OykDosMiniNet* net, const oyk_u8* frame, size_t len);
void oyk_dos_mininet_feed_ppp_byte(OykDosMiniNet* net, oyk_u8 b);
int oyk_dos_mininet_send_udp(OykDosMiniNet* net, oyk_u32 dst_ip,
                             oyk_u16 src_port, oyk_u16 dst_port,
                             const oyk_u8* payload, size_t len);
void oyk_dos_mininet_ppp_open(OykDosMiniNet* net);

/* MS-DOS runtime hardware & TSR probes (no-ops returning 0 on non-DOS test builds) */
int oyk_dos_usb_detect(OykDosUsbStackInfo* out);
int oyk_dos_usb_find_cdc(const OykDosUsbStackInfo* stack, int want_network,
                         OykDosCdcDeviceInfo* out);

/* Crynwr Packet Driver (INT 60h..7Fh) lightweight standalone interface */
struct OykDosPktDrv {
    int active;
    oyk_u8 int_no;
    oyk_u16 handle;
    oyk_u8 mac[6];
};

int oyk_dos_pktdrv_init(OykDosPktDrv* drv);
void oyk_dos_pktdrv_shutdown(OykDosPktDrv* drv);
int oyk_dos_pktdrv_send(OykDosPktDrv* drv, const oyk_u8* eth, size_t len);
int oyk_dos_pktdrv_poll_rx(OykDosPktDrv* drv, oyk_u8* out, size_t cap);

/* Direct PC COM1..COM4 (8250/16550 UART) or BIOS/USBCOM INT 14h serial driver */
struct OykDosSerial {
    int active;
    int use_int14;
    oyk_u16 io_base;         /* 0x3F8, 0x2F8, 0x3E8, 0x2E8 */
    oyk_u8 com_index;        /* 0..3 */
    oyk_u32 baud;
};

int oyk_dos_serial_open(OykDosSerial* ser, const char* device, oyk_u32 baud);
void oyk_dos_serial_close(OykDosSerial* ser);
int oyk_dos_serial_write_byte(OykDosSerial* ser, oyk_u8 b);
int oyk_dos_serial_read_byte(OykDosSerial* ser, oyk_u8* out_b);

#endif /* OYK_DOS_USB_HPP */
