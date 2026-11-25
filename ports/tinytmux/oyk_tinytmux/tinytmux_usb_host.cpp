/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "tinytmux_usb_host.hpp"
#include "board_tinytmux.hpp"

#include <stdio.h>
#include <cstring>

#if defined(TEENSYDUINO)
#include <Arduino.h>
#include <USBHost_t36.h>

class OykUsbCdcNetDriver : public USBDriver {
public:
    OykUsbCdcNetDriver(USBHost& /*host*/)
        : slot_(-1),
          rxpipe_(NULL),
          txpipe_(NULL),
          is_ncm_(false),
          data_iface_(0),
          data_alt_(0),
          ctrl_sent_(false),
          alt_ready_(false),
          rx_len_ready_(0),
          rx_queued_(false),
          tx_busy_(false),
          ncm_seq_(1) {
        init();
    }

    int bound_slot() const { return slot_; }
    void set_bound_slot(int s) { slot_ = s; }
    bool is_ncm() const { return is_ncm_; }

    void poll_driver(TinyTmuxUsbHost* host) {
        Device_t* dev = *(Device_t* volatile*)&device;
        if (!dev) return;

        if (!ctrl_sent_ && !alt_ready_) {
            setup_t setup;
            mk_setup(setup, 0x01, 0x0B, data_alt_, data_iface_, 0);
            if (queue_Control_Transfer(dev, &setup, NULL, this)) {
                ctrl_sent_ = true;
            }
        }
        if (alt_ready_ && rx_len_ready_ > 0 && host && slot_ >= 0) {
            uint16_t n = rx_len_ready_;
            if (is_ncm_) {
                host->on_ncm_rx(slot_, rx_buf_, static_cast<size_t>(n));
            } else {
                host->on_eth_rx(slot_, rx_buf_, static_cast<size_t>(n));
            }
            rx_len_ready_ = 0;
        }
        if (alt_ready_ && !rx_queued_ && rx_len_ready_ == 0 && rxpipe_) {
            rx_queued_ = queue_Data_Transfer(rxpipe_, rx_buf_, sizeof(rx_buf_), this);
        }
    }

    bool send_frame(const oyk_u8* eth_frame, size_t eth_len) {
        if (!*this || !alt_ready_ || !txpipe_ || tx_busy_ || !eth_frame || eth_len < 14) {
            return false;
        }
        size_t send_len = 0;
        if (is_ncm_) {
            send_len = tinytmux_usb_ncm16_wrap(eth_frame, eth_len, ncm_seq_++,
                                               tx_buf_, sizeof(tx_buf_));
            if (send_len == 0) return false;
        } else {
            if (eth_len > sizeof(tx_buf_)) return false;
            memcpy(tx_buf_, eth_frame, eth_len);
            send_len = eth_len;
        }
        tx_busy_ = queue_Data_Transfer(txpipe_, tx_buf_, static_cast<uint32_t>(send_len), this);
        return tx_busy_;
    }

protected:
    virtual bool claim(Device_t* dev, int type, const uint8_t* descriptors, uint32_t len) {
        if (type != 0 && type != 2) return false;
        if (type == 0 && dev->bDeviceClass == 0xEF) return false;

        const uint8_t* p = descriptors;
        const uint8_t* end = descriptors + len;
        bool found_comm = false;
        bool found_ncm = false;
        bool in_data_iface = false;
        uint8_t cur_iface = 0;
        uint8_t cur_alt = 0;
        uint8_t chosen_iface = 0;
        uint8_t chosen_alt = 0;
        uint8_t rx_ep = 0;
        uint8_t tx_ep = 0;
        uint16_t rx_mps = 0;
        uint16_t tx_mps = 0;

        while (p < end) {
            uint8_t dlen = p[0];
            if (dlen < 2 || p + dlen > end) break;
            uint8_t dtype = p[1];
            if (dtype == 4 && dlen >= 9) {
                cur_iface = p[2];
                cur_alt = p[3];
                uint8_t cls = p[5];
                uint8_t sub = p[6];
                if (cls == 0x02 && (sub == 0x06 || sub == 0x0D)) {
                    found_comm = true;
                    found_ncm = (sub == 0x0D);
                    in_data_iface = false;
                } else if (found_comm && cls == 0x0A) {
                    in_data_iface = true;
                    if (rx_ep == 0 || tx_ep == 0) {
                        chosen_iface = cur_iface;
                        chosen_alt = cur_alt;
                        rx_ep = 0;
                        tx_ep = 0;
                    }
                } else {
                    in_data_iface = false;
                }
            } else if (dtype == 5 && dlen >= 7 && in_data_iface && (p[3] & 0x03U) == 2U) {
                uint8_t ep = p[2];
                uint16_t mps = static_cast<uint16_t>(p[4] | (p[5] << 8));
                if ((ep & 0x80U) != 0U && rx_ep == 0) {
                    rx_ep = static_cast<uint8_t>(ep & 0x0FU);
                    rx_mps = mps;
                } else if ((ep & 0x80U) == 0U && tx_ep == 0) {
                    tx_ep = static_cast<uint8_t>(ep & 0x0FU);
                    tx_mps = mps;
                }
            }
            p += dlen;
        }

        if (!found_comm || rx_ep == 0 || tx_ep == 0) return false;
        rxpipe_ = new_Pipe(dev, 2, rx_ep, 1, rx_mps ? rx_mps : 512U);
        txpipe_ = new_Pipe(dev, 2, tx_ep, 0, tx_mps ? tx_mps : 512U);
        if (!rxpipe_ || !txpipe_) return false;

        is_ncm_ = found_ncm;
        data_iface_ = chosen_iface;
        data_alt_ = chosen_alt;
        alt_ready_ = (data_alt_ == 0);
        ctrl_sent_ = alt_ready_;
        rx_len_ready_ = 0;
        rx_queued_ = false;
        tx_busy_ = false;
        rxpipe_->callback_function = rx_cb;
        txpipe_->callback_function = tx_cb;
        if (alt_ready_) {
            rx_queued_ = queue_Data_Transfer(rxpipe_, rx_buf_, sizeof(rx_buf_), this);
        }
        return true;
    }

    virtual void control(const Transfer_t* /*transfer*/) {
        alt_ready_ = true;
    }

    virtual void disconnect() {
        rxpipe_ = NULL;
        txpipe_ = NULL;
        ctrl_sent_ = false;
        alt_ready_ = false;
        rx_len_ready_ = 0;
        rx_queued_ = false;
        tx_busy_ = false;
    }

private:
    void init() {
        contribute_Pipes(mypipes_, 2);
        contribute_Transfers(mytransfers_, 4);
        driver_ready_for_device(this);
    }

    static void rx_cb(const Transfer_t* transfer) {
        if (!transfer || !transfer->driver) return;
        OykUsbCdcNetDriver* self = static_cast<OykUsbCdcNetDriver*>(transfer->driver);
        if (transfer->length > 0 && self->rx_len_ready_ == 0) {
            uint16_t n = static_cast<uint16_t>(transfer->length);
            if (n <= sizeof(self->rx_buf_)) {
                self->rx_len_ready_ = n;
            }
        }
        self->rx_queued_ = false;
    }

    static void tx_cb(const Transfer_t* transfer) {
        if (!transfer || !transfer->driver) return;
        OykUsbCdcNetDriver* self = static_cast<OykUsbCdcNetDriver*>(transfer->driver);
        self->tx_busy_ = false;
    }

    Pipe_t mypipes_[2] __attribute__((aligned(32)));
    Transfer_t mytransfers_[4] __attribute__((aligned(32)));
    oyk_u8 rx_buf_[2048] __attribute__((aligned(32)));
    oyk_u8 tx_buf_[2048] __attribute__((aligned(32)));
    int slot_;
    Pipe_t* rxpipe_;
    Pipe_t* txpipe_;
    bool is_ncm_;
    uint8_t data_iface_;
    uint8_t data_alt_;
    bool ctrl_sent_;
    volatile bool alt_ready_;
    volatile uint16_t rx_len_ready_;
    volatile bool rx_queued_;
    volatile bool tx_busy_;
    oyk_u16 ncm_seq_;
};

static USBHost s_usb_host;
static USBHub s_usb_hub1(s_usb_host);
static USBHub s_usb_hub2(s_usb_host);
static USBSerial_BigBuffer s_usb_ser1(s_usb_host, 1);
static USBSerial_BigBuffer s_usb_ser2(s_usb_host, 1);
static OykUsbCdcNetDriver s_usb_net1(s_usb_host);

class OykUsbPrinterDriver : public USBDriver {
public:
    OykUsbPrinterDriver(USBHost& /*host*/)
        : txpipe_(NULL),
          tx_busy_(false) {
        init();
    }

    size_t write_bytes(const oyk_u8* data, size_t len) {
        if (!*this || !txpipe_ || !data || len == 0) return 0;
        size_t sent = 0;
        while (sent < len) {
            unsigned long wait_start = millis();
            while (tx_busy_ && (millis() - wait_start) < 250UL) {
                yield();
            }
            if (tx_busy_) break;
            size_t chunk = len - sent;
            if (chunk > sizeof(tx_buf_)) chunk = sizeof(tx_buf_);
            memcpy(tx_buf_, data + sent, chunk);
            tx_busy_ = queue_Data_Transfer(txpipe_, tx_buf_, static_cast<uint32_t>(chunk), this);
            if (!tx_busy_) break;
            wait_start = millis();
            while (tx_busy_ && (millis() - wait_start) < 250UL) {
                yield();
            }
            if (tx_busy_) break;
            sent += chunk;
        }
        return sent;
    }

protected:
    virtual bool claim(Device_t* dev, int type, const uint8_t* descriptors, uint32_t len) {
        if (type != 0 && type != 2) return false;
        const uint8_t* p = descriptors;
        const uint8_t* end = descriptors + len;
        bool in_prn_iface = false;
        uint8_t tx_ep = 0;
        uint16_t tx_mps = 0;

        while (p < end) {
            uint8_t dlen = p[0];
            if (dlen < 2 || p + dlen > end) break;
            uint8_t dtype = p[1];
            if (dtype == 4 && dlen >= 9) {
                if (tx_ep != 0) break;
                /* USB Printer Class (0x07), SubClass Printers (0x01) */
                in_prn_iface = (p[5] == 0x07 && p[6] == 0x01);
            } else if (dtype == 5 && dlen >= 7 && in_prn_iface && (p[3] & 0x03U) == 2U) {
                uint8_t ep = p[2];
                if ((ep & 0x80U) == 0U && tx_ep == 0) {
                    tx_ep = static_cast<uint8_t>(ep & 0x0FU);
                    tx_mps = static_cast<uint16_t>(p[4] | (p[5] << 8));
                }
            }
            p += dlen;
        }

        if (tx_ep == 0) return false;
        txpipe_ = new_Pipe(dev, 2, tx_ep, 0, tx_mps ? tx_mps : 64U);
        if (!txpipe_) return false;
        tx_busy_ = false;
        txpipe_->callback_function = tx_cb;
        return true;
    }

    virtual void disconnect() {
        txpipe_ = NULL;
        tx_busy_ = false;
    }

private:
    void init() {
        contribute_Pipes(mypipes_, 1);
        contribute_Transfers(mytransfers_, 2);
        driver_ready_for_device(this);
    }

    static void tx_cb(const Transfer_t* transfer) {
        if (!transfer || !transfer->driver) return;
        OykUsbPrinterDriver* self = static_cast<OykUsbPrinterDriver*>(transfer->driver);
        self->tx_busy_ = false;
    }

    Pipe_t mypipes_[1] __attribute__((aligned(32)));
    Transfer_t mytransfers_[2] __attribute__((aligned(32)));
    oyk_u8 tx_buf_[512] __attribute__((aligned(32)));
    Pipe_t* txpipe_;
    volatile bool tx_busy_;
};

static OykUsbPrinterDriver s_usb_printer1(s_usb_host);

static USBSerial_BigBuffer* s_ser_devs[2] = { &s_usb_ser1, &s_usb_ser2 };
static int s_ser_slots[2] = { -1, -1 };
static int s_net_slot = -1;
static int s_printer_slot = -1;
static bool s_usb_host_begun = false;
#endif

static void uh_str_copy(char* dst, size_t dst_sz, const char* src) {
    if (!dst || dst_sz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static oyk_u32 uh_parse_ip(const char* text) {
    oyk_u32 ip = 0;
    if (text && text[0]) oyk_parse_ipv4(text, &ip);
    return ip;
}

static TinyTmuxUsbHost* g_active_usb_host = 0;

void tinytmux_usb_host_set_active(TinyTmuxUsbHost* host) {
    g_active_usb_host = host;
}

TinyTmuxUsbHost* tinytmux_usb_host_active() {
    return g_active_usb_host;
}

static oyk_u16 read_le16(const oyk_u8* p) {
    return (oyk_u16)(((oyk_u16)p[1] << 8) | (oyk_u16)p[0]);
}

static void write_le16(oyk_u8* p, oyk_u16 v) {
    p[0] = (oyk_u8)(v & 0xFFU);
    p[1] = (oyk_u8)((v >> 8) & 0xFFU);
}

static void write_le32(oyk_u8* p, oyk_u32 v) {
    p[0] = (oyk_u8)(v & 0xFFU);
    p[1] = (oyk_u8)((v >> 8) & 0xFFU);
    p[2] = (oyk_u8)((v >> 16) & 0xFFU);
    p[3] = (oyk_u8)((v >> 24) & 0xFFU);
}

static oyk_u16 read_be16(const oyk_u8* p) {
    return (oyk_u16)(((oyk_u16)p[0] << 8) | (oyk_u16)p[1]);
}

static void write_be16(oyk_u8* p, oyk_u16 v) {
    p[0] = (oyk_u8)((v >> 8) & 0xFFU);
    p[1] = (oyk_u8)(v & 0xFFU);
}

static oyk_u32 read_u32_raw(const oyk_u8* p) {
    oyk_u32 v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

static void write_u32_raw(oyk_u8* p, oyk_u32 v) {
    std::memcpy(p, &v, 4);
}

static oyk_u16 inet_checksum(const oyk_u8* data, size_t len) {
    oyk_u32 sum = 0;
    size_t i = 0;
    while (i + 1 < len) {
        sum += (oyk_u32)(((oyk_u16)data[i] << 8) | (oyk_u16)data[i + 1]);
        i += 2;
    }
    if (i < len) {
        sum += (oyk_u32)((oyk_u16)data[i] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFUL) + (sum >> 16);
    }
    return (oyk_u16)(~sum & 0xFFFFUL);
}

static oyk_u16 udp_checksum(oyk_u32 src_ip_net, oyk_u32 dst_ip_net,
                            const oyk_u8* udp_hdr_and_pay, size_t udp_len) {
    oyk_u8 pseudo[12];
    oyk_u32 sum = 0;
    size_t i;
    write_u32_raw(pseudo, src_ip_net);
    write_u32_raw(pseudo + 4, dst_ip_net);
    pseudo[8] = 0;
    pseudo[9] = 17; /* IPPROTO_UDP */
    write_be16(pseudo + 10, (oyk_u16)udp_len);

    for (i = 0; i < 12; i += 2) {
        sum += (oyk_u32)(((oyk_u16)pseudo[i] << 8) | (oyk_u16)pseudo[i + 1]);
    }
    i = 0;
    while (i + 1 < udp_len) {
        if (i != 6) { /* Skip checksum field */
            sum += (oyk_u32)(((oyk_u16)udp_hdr_and_pay[i] << 8) | (oyk_u16)udp_hdr_and_pay[i + 1]);
        }
        i += 2;
    }
    if (i < udp_len) {
        sum += (oyk_u32)((oyk_u16)udp_hdr_and_pay[i] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFUL) + (sum >> 16);
    }
    oyk_u16 cs = (oyk_u16)(~sum & 0xFFFFUL);
    return (cs == 0) ? 0xFFFFU : cs;
}

extern "C" size_t tinytmux_usb_ncm16_wrap(const oyk_u8* eth_frame, size_t eth_len,
                                          oyk_u16 seq, oyk_u8* out, size_t out_cap) {
    /* NTH16 (12 bytes) + NDP16 with 2 entries (16 bytes) = 28 byte header */
    const size_t hdr_len = 28;
    if (!eth_frame || !out || eth_len < 14 || hdr_len + eth_len > out_cap) return 0;

    std::memset(out, 0, hdr_len);
    /* NTH16 signature "NCMH" (0x484D434E little-endian) */
    write_le32(out + 0, 0x484D434EUL);
    write_le16(out + 4, 12U);
    write_le16(out + 6, seq);
    write_le16(out + 8, (oyk_u16)(hdr_len + eth_len));
    write_le16(out + 10, 12U); /* wNdpIndex */

    /* NDP16 signature "NCM0" (0x304D434E little-endian) */
    write_le32(out + 12, 0x304D434EUL);
    write_le16(out + 16, 16U); /* wLength */
    write_le16(out + 18, 0U);  /* wNextNdpIndex */
    write_le16(out + 20, (oyk_u16)hdr_len);
    write_le16(out + 22, (oyk_u16)eth_len);
    write_le16(out + 24, 0U);
    write_le16(out + 26, 0U);

    std::memcpy(out + hdr_len, eth_frame, eth_len);
    return hdr_len + eth_len;
}

extern "C" size_t tinytmux_usb_ncm16_unwrap(const oyk_u8* ntb, size_t ntb_len,
                                            oyk_u8* eth_out, size_t eth_cap) {
    if (!ntb || !eth_out || ntb_len < 28) return 0;
    /* Check NTH16 signature "NCMH" */
    if (ntb[0] != 'N' || ntb[1] != 'C' || ntb[2] != 'M' || ntb[3] != 'H') return 0;
    oyk_u16 nth_len = read_le16(ntb + 4);
    oyk_u16 blk_len = read_le16(ntb + 8);
    oyk_u16 ndp_idx = read_le16(ntb + 10);
    if (nth_len < 12 || blk_len > ntb_len || (size_t)ndp_idx + 16U > ntb_len) return 0;
    /* Check NDP16 signature "NCM0" */
    const oyk_u8* ndp = ntb + ndp_idx;
    if (ndp[0] != 'N' || ndp[1] != 'C' || ndp[2] != 'M' || ndp[3] != '0') return 0;
    oyk_u16 ndp_len = read_le16(ndp + 4);
    if (ndp_len < 16 || (size_t)ndp_idx + ndp_len > ntb_len) return 0;
    oyk_u16 dg_idx = read_le16(ndp + 8);
    oyk_u16 dg_len = read_le16(ndp + 10);
    if (dg_idx == 0 || dg_len < 14) return 0;
    if ((size_t)dg_idx + dg_len > ntb_len || dg_len > eth_cap) return 0;
    std::memcpy(eth_out, ntb + dg_idx, dg_len);
    return (size_t)dg_len;
}

TinyTmuxUsbHost::TinyTmuxUsbHost()
    : eth_tx_hook_(0), eth_tx_ctx_(0),
      acm_tx_hook_(0), acm_tx_ctx_(0),
      printer_tx_hook_(0), printer_tx_ctx_(0),
      acm_rx_cb_(0), acm_rx_ctx_(0),
      ax25_rx_cb_(0), ax25_rx_ctx_(0),
      udp_rx_cb_(0), udp_rx_ctx_(0) {
    init();
}

void TinyTmuxUsbHost::init() {
    std::memset(slots_, 0, sizeof(slots_));
    std::memset(scratch_tx_, 0, sizeof(scratch_tx_));
    std::memset(scratch_rx_, 0, sizeof(scratch_rx_));
    tinytmux_usb_host_set_active(this);
#if defined(TEENSYDUINO)
    s_ser_slots[0] = -1;
    s_ser_slots[1] = -1;
    s_net_slot = -1;
    s_printer_slot = -1;
    s_usb_net1.set_bound_slot(-1);
    if (!s_usb_host_begun) {
        s_usb_host.begin();
        s_usb_host_begun = true;
    }
#endif
}

void TinyTmuxUsbHost::poll() {
#if defined(TEENSYDUINO)
    s_usb_host.Task();

    for (int k = 0; k < 2; ++k) {
        USBSerial_BigBuffer* ser = s_ser_devs[k];
        bool connected = static_cast<bool>(*ser);
        if (connected && s_ser_slots[k] < 0) {
            ser->begin(115200);
            s_ser_slots[k] = attach_slot(-1, TMUX_USBH_CDC_ACM,
                                         static_cast<oyk_u8>(k + 1),
                                         static_cast<oyk_u8>(k),
                                         ser->idVendor(), ser->idProduct(), 0, 0);
        } else if (!connected && s_ser_slots[k] >= 0) {
            detach_slot(s_ser_slots[k]);
            s_ser_slots[k] = -1;
        }
        if (connected && s_ser_slots[k] >= 0) {
            oyk_u8 buf[128];
            while (ser->available() > 0) {
                int n = ser->readBytes(reinterpret_cast<char*>(buf), sizeof(buf));
                if (n <= 0) break;
                on_acm_rx(s_ser_slots[k], buf, static_cast<size_t>(n));
            }
        }
    }

    bool net_connected = static_cast<bool>(s_usb_net1);
    if (net_connected && s_net_slot < 0) {
        int kind = s_usb_net1.is_ncm() ? TMUX_USBH_CDC_NCM : TMUX_USBH_CDC_ECM;
        s_net_slot = attach_slot(-1, kind, 0x10U, 0,
                                 s_usb_net1.idVendor(), s_usb_net1.idProduct(), 0, 0);
        s_usb_net1.set_bound_slot(s_net_slot);
    } else if (!net_connected && s_net_slot >= 0) {
        detach_slot(s_net_slot);
        s_net_slot = -1;
        s_usb_net1.set_bound_slot(-1);
    }
    s_usb_net1.poll_driver(this);

    bool prn_connected = static_cast<bool>(s_usb_printer1);
    if (prn_connected && s_printer_slot < 0) {
        s_printer_slot = attach_slot(-1, TMUX_USBH_PRINTER, 0x20U, 0,
                                     s_usb_printer1.idVendor(), s_usb_printer1.idProduct(),
                                     0, "usbprn0");
    } else if (!prn_connected && s_printer_slot >= 0) {
        detach_slot(s_printer_slot);
        s_printer_slot = -1;
    }
#endif
}

int TinyTmuxUsbHost::parse_device_slot(const char* dev_name) {
    if (!dev_name || !dev_name[0]) return -1;
    if (std::strncmp(dev_name, "usbh", 4) == 0 &&
        dev_name[4] >= '0' && dev_name[4] < ('0' + TINYTMUX_MAX_USB_RADIOS) &&
        dev_name[5] == '\0') {
        return (int)(dev_name[4] - '0');
    }
    return -1;
}

int TinyTmuxUsbHost::attach_slot(int slot, int kind, oyk_u8 daddr, oyk_u8 idx,
                                 oyk_u16 vid, oyk_u16 pid, const oyk_u8 mac[6],
                                 const char* label) {
    if (slot < 0) {
        for (int i = 0; i < TINYTMUX_MAX_USB_RADIOS; ++i) {
            if (!slots_[i].in_use) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return -1;

    TinyTmuxUsbRadioSlot& s = slots_[slot];
    std::memset(&s, 0, sizeof(s));
    s.in_use = 1;
    s.kind = kind;
    s.daddr = daddr;
    s.idx = idx;
    s.vid = vid;
    s.pid = pid;
    s.ip_id = 1;
    s.ncm_seq = 1;
    if (mac) {
        std::memcpy(s.mac, mac, 6);
    } else {
        tinytmux_board_read_mac(s.mac);
        s.mac[5] = (oyk_u8)(s.mac[5] ^ (oyk_u8)(slot + 0x20U));
    }
    /* Sensible default USB CDC-ECM/NCM link-local/private IPv4 (192.168.7.2 <-> 192.168.7.1) */
    s.local_ip = uh_parse_ip("192.168.7.2");
    s.peer_ip = uh_parse_ip("192.168.7.1");
    if (label && label[0]) {
        uh_str_copy(s.label, sizeof(s.label), label);
    } else {
        sprintf(s.label, "usbh%d", slot);
    }
    return slot;
}

void TinyTmuxUsbHost::detach_slot(int slot) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return;
    std::memset(&slots_[slot], 0, sizeof(slots_[slot]));
}

void TinyTmuxUsbHost::detach_by_dev(oyk_u8 daddr, int kind, oyk_u8 idx) {
    for (int i = 0; i < TINYTMUX_MAX_USB_RADIOS; ++i) {
        if (!slots_[i].in_use) continue;
        if (slots_[i].daddr == daddr &&
            (kind < 0 || slots_[i].kind == kind) &&
            (idx == 0xFFU || slots_[i].idx == idx)) {
            detach_slot(i);
        }
    }
}

void TinyTmuxUsbHost::configure_slot_ips(int slot, oyk_u32 local_ip_net, oyk_u32 peer_ip_net) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return;
    if (local_ip_net != 0) slots_[slot].local_ip = local_ip_net;
    if (peer_ip_net != 0) slots_[slot].peer_ip = peer_ip_net;
}

const TinyTmuxUsbRadioSlot* TinyTmuxUsbHost::slot_info(int slot) const {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return 0;
    return &slots_[slot];
}

int TinyTmuxUsbHost::find_slot_by_kind(int kind, int start_slot) const {
    if (start_slot < 0) start_slot = 0;
    for (int i = start_slot; i < TINYTMUX_MAX_USB_RADIOS; ++i) {
        if (slots_[i].in_use && slots_[i].kind == kind) return i;
    }
    return -1;
}

int TinyTmuxUsbHost::find_first_net_slot() const {
    for (int i = 0; i < TINYTMUX_MAX_USB_RADIOS; ++i) {
        if (slots_[i].in_use &&
            (slots_[i].kind == TMUX_USBH_CDC_ECM || slots_[i].kind == TMUX_USBH_CDC_NCM)) {
            return i;
        }
    }
    return -1;
}

int TinyTmuxUsbHost::find_first_acm_slot() const {
    for (int i = 0; i < TINYTMUX_MAX_USB_RADIOS; ++i) {
        if (slots_[i].in_use && slots_[i].kind == TMUX_USBH_CDC_ACM) return i;
    }
    return -1;
}

int TinyTmuxUsbHost::find_first_printer_slot() const {
    for (int i = 0; i < TINYTMUX_MAX_USB_RADIOS; ++i) {
        if (slots_[i].in_use && slots_[i].kind == TMUX_USBH_PRINTER) return i;
    }
    return -1;
}

int TinyTmuxUsbHost::count_active() const {
    int count = 0;
    for (int i = 0; i < TINYTMUX_MAX_USB_RADIOS; ++i) {
        if (slots_[i].in_use) ++count;
    }
    return count;
}

int TinyTmuxUsbHost::has_net_device() const {
    return (find_first_net_slot() >= 0) ? 1 : 0;
}

int TinyTmuxUsbHost::has_acm_device() const {
    return (find_first_acm_slot() >= 0) ? 1 : 0;
}

int TinyTmuxUsbHost::has_printer_device() const {
    return (find_first_printer_slot() >= 0) ? 1 : 0;
}

void TinyTmuxUsbHost::format_short_status(char* out, size_t out_sz) const {
    if (!out || out_sz == 0) return;
    int has_net = has_net_device();
    int has_acm = has_acm_device();
    int has_prn = has_printer_device();
    if (has_net && has_acm && has_prn) {
        uh_str_copy(out, out_sz, "NSP");
    } else if (has_net && has_acm) {
        uh_str_copy(out, out_sz, "N+S");
    } else if (has_net && has_prn) {
        uh_str_copy(out, out_sz, "N+P");
    } else if (has_acm && has_prn) {
        uh_str_copy(out, out_sz, "S+P");
    } else if (has_net) {
        uh_str_copy(out, out_sz, "NET");
    } else if (has_acm) {
        uh_str_copy(out, out_sz, "SER");
    } else if (has_prn) {
        uh_str_copy(out, out_sz, "PRN");
    } else {
        uh_str_copy(out, out_sz, "-");
    }
}

void TinyTmuxUsbHost::set_eth_tx_hook(TinyTmuxUsbEthTxFn cb, void* ctx) {
    eth_tx_hook_ = cb;
    eth_tx_ctx_ = ctx;
}

void TinyTmuxUsbHost::set_acm_tx_hook(TinyTmuxUsbAcmTxFn cb, void* ctx) {
    acm_tx_hook_ = cb;
    acm_tx_ctx_ = ctx;
}

void TinyTmuxUsbHost::set_printer_tx_hook(TinyTmuxUsbPrinterTxFn cb, void* ctx) {
    printer_tx_hook_ = cb;
    printer_tx_ctx_ = ctx;
}

void TinyTmuxUsbHost::set_acm_rx_callback(TinyTmuxUsbAcmRxFn cb, void* ctx) {
    acm_rx_cb_ = cb;
    acm_rx_ctx_ = ctx;
}

void TinyTmuxUsbHost::set_ax25_rx_callback(TinyTmuxUsbAx25RxFn cb, void* ctx) {
    ax25_rx_cb_ = cb;
    ax25_rx_ctx_ = ctx;
}

void TinyTmuxUsbHost::set_udp_rx_callback(TinyTmuxUsbUdpRxFn cb, void* ctx) {
    udp_rx_cb_ = cb;
    udp_rx_ctx_ = ctx;
}

int TinyTmuxUsbHost::send_eth_frame(int slot, const oyk_u8* eth_frame, size_t eth_len) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return 0;
    TinyTmuxUsbRadioSlot& s = slots_[slot];
    if (!s.in_use || !eth_frame || eth_len < 14 || eth_len > 1520) return 0;
    if (s.kind != TMUX_USBH_CDC_ECM && s.kind != TMUX_USBH_CDC_NCM) return 0;

    s.tx_packets++;
    if (eth_tx_hook_) {
        eth_tx_hook_(slot, eth_frame, eth_len, eth_tx_ctx_);
        return 1;
    }

#if defined(TEENSYDUINO)
    if (slot == s_net_slot && static_cast<bool>(s_usb_net1)) {
        return s_usb_net1.send_frame(eth_frame, eth_len) ? 1 : 0;
    }
    return 0;
#else
    return 0;
#endif
}

int TinyTmuxUsbHost::send_arp_request(int slot, oyk_u32 target_ip_net) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return 0;
    const TinyTmuxUsbRadioSlot& s = slots_[slot];
    if (!s.in_use || (s.kind != TMUX_USBH_CDC_ECM && s.kind != TMUX_USBH_CDC_NCM)) return 0;

    oyk_u8 arp[42];
    std::memset(arp, 0, sizeof(arp));
    std::memset(arp, 0xFF, 6);               /* Broadcast dst MAC */
    std::memcpy(arp + 6, s.mac, 6);          /* Src MAC */
    arp[12] = 0x08; arp[13] = 0x06;          /* EtherType ARP */
    arp[14] = 0x00; arp[15] = 0x01;          /* HTYPE Ethernet */
    arp[16] = 0x08; arp[17] = 0x00;          /* PTYPE IPv4 */
    arp[18] = 6; arp[19] = 4;
    arp[20] = 0x00; arp[21] = 0x01;          /* Opcode 1 = Request */
    std::memcpy(arp + 22, s.mac, 6);
    write_u32_raw(arp + 28, s.local_ip);
    std::memset(arp + 32, 0, 6);
    write_u32_raw(arp + 38, target_ip_net);
    return send_eth_frame(slot, arp, sizeof(arp));
}

int TinyTmuxUsbHost::send_ipv4_packet(int slot, oyk_u32 dst_ip_net, oyk_u8 proto,
                                      const oyk_u8* payload, size_t pay_len) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return 0;
    TinyTmuxUsbRadioSlot& s = slots_[slot];
    if (!s.in_use || (s.kind != TMUX_USBH_CDC_ECM && s.kind != TMUX_USBH_CDC_NCM)) return 0;
    if (14U + 20U + pay_len > sizeof(scratch_tx_)) return 0;

    oyk_u8* eth = scratch_tx_;
    if (s.has_peer_mac && dst_ip_net == s.peer_ip) {
        std::memcpy(eth, s.peer_mac, 6);
    } else if (s.has_peer_mac) {
        std::memcpy(eth, s.peer_mac, 6);
    } else {
        std::memset(eth, 0xFF, 6);
    }
    std::memcpy(eth + 6, s.mac, 6);
    eth[12] = 0x08; eth[13] = 0x00; /* IPv4 */

    oyk_u8* ip = eth + 14;
    std::memset(ip, 0, 20);
    ip[0] = 0x45;
    ip[1] = 0x00;
    oyk_u16 total_len = (oyk_u16)(20U + pay_len);
    write_be16(ip + 2, total_len);
    write_be16(ip + 4, s.ip_id++);
    ip[6] = 0x40; ip[7] = 0x00; /* Don't fragment */
    ip[8] = 64;   /* TTL */
    ip[9] = proto;
    write_u32_raw(ip + 12, s.local_ip);
    write_u32_raw(ip + 16, dst_ip_net);
    oyk_u16 ipcs = inet_checksum(ip, 20);
    write_be16(ip + 10, ipcs);

    if (pay_len > 0 && payload) {
        std::memcpy(ip + 20, payload, pay_len);
    }
    return send_eth_frame(slot, eth, 14U + total_len);
}

int TinyTmuxUsbHost::send_udp(int slot, oyk_u32 dst_ip_net, oyk_u16 src_port_host,
                              oyk_u16 dst_port_host, const oyk_u8* payload, size_t pay_len) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return 0;
    if (8U + pay_len > 1480U) return 0;
    oyk_u8 udp_buf[1488];
    write_be16(udp_buf + 0, src_port_host);
    write_be16(udp_buf + 2, dst_port_host);
    oyk_u16 ulen = (oyk_u16)(8U + pay_len);
    write_be16(udp_buf + 4, ulen);
    write_be16(udp_buf + 6, 0U);
    if (pay_len > 0 && payload) {
        std::memcpy(udp_buf + 8, payload, pay_len);
    }
    oyk_u16 cs = udp_checksum(slots_[slot].local_ip, dst_ip_net, udp_buf, ulen);
    write_be16(udp_buf + 6, cs);
    return send_ipv4_packet(slot, dst_ip_net, 17U /* UDP */, udp_buf, ulen);
}

int TinyTmuxUsbHost::send_axudp(int slot, const OykFrame* frame, oyk_u32 dst_ip_net,
                                oyk_u16 src_port_host, oyk_u16 dst_port_host) {
    if (!frame) return 0;
    oyk_u8 raw[320];
    size_t raw_len = oyk_ax25_encode(frame, raw, sizeof(raw) - 2);
    if (raw_len == 0) return 0;
    oyk_u16 fcs = oyk_crc16_fcs(raw, raw_len);
    raw[raw_len++] = (oyk_u8)(fcs & 0xFFU);
    raw[raw_len++] = (oyk_u8)((fcs >> 8) & 0xFFU);
    return send_udp(slot, dst_ip_net, src_port_host, dst_port_host, raw, raw_len);
}

int TinyTmuxUsbHost::send_axip(int slot, const OykFrame* frame, oyk_u32 dst_ip_net) {
    if (!frame) return 0;
    oyk_u8 raw[320];
    size_t raw_len = oyk_ax25_encode(frame, raw, sizeof(raw) - 2);
    if (raw_len == 0) return 0;
    oyk_u16 fcs = oyk_crc16_fcs(raw, raw_len);
    raw[raw_len++] = (oyk_u8)(fcs & 0xFFU);
    raw[raw_len++] = (oyk_u8)((fcs >> 8) & 0xFFU);
    return send_ipv4_packet(slot, dst_ip_net, 93U /* IPPROTO_AXIP */, raw, raw_len);
}

size_t TinyTmuxUsbHost::send_acm_bytes(int slot, const oyk_u8* data, size_t len) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return 0;
    TinyTmuxUsbRadioSlot& s = slots_[slot];
    if (!s.in_use || s.kind != TMUX_USBH_CDC_ACM || !data || len == 0) return 0;

    s.tx_packets++;
    if (acm_tx_hook_) {
        return acm_tx_hook_(slot, data, len, acm_tx_ctx_);
    }

#if defined(TEENSYDUINO)
    for (int k = 0; k < 2; ++k) {
        if (s_ser_slots[k] == slot && static_cast<bool>(*s_ser_devs[k])) {
            return s_ser_devs[k]->write(data, len);
        }
    }
    return 0;
#else
    return 0;
#endif
}

size_t TinyTmuxUsbHost::send_printer_bytes(int slot, const oyk_u8* data, size_t len) {
    if (slot < 0) slot = find_first_printer_slot();
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return 0;
    TinyTmuxUsbRadioSlot& s = slots_[slot];
    if (!s.in_use || s.kind != TMUX_USBH_PRINTER || !data || len == 0) return 0;

    s.tx_packets++;
    if (printer_tx_hook_) {
        return printer_tx_hook_(slot, data, len, printer_tx_ctx_);
    }

#if defined(TEENSYDUINO)
    if (slot == s_printer_slot && static_cast<bool>(s_usb_printer1)) {
        return s_usb_printer1.write_bytes(data, len);
    }
    return 0;
#else
    return 0;
#endif
}

void TinyTmuxUsbHost::handle_arp(int slot, const oyk_u8* frame, size_t len) {
    if (len < 42) return;
    TinyTmuxUsbRadioSlot& s = slots_[slot];
    oyk_u16 htype = read_be16(frame + 14);
    oyk_u16 ptype = read_be16(frame + 16);
    oyk_u16 op    = read_be16(frame + 20);
    if (htype != 1U || ptype != 0x0800U) return;

    oyk_u32 sender_ip = read_u32_raw(frame + 28);
    oyk_u32 target_ip = read_u32_raw(frame + 38);

    if (sender_ip == s.peer_ip || s.peer_ip == 0) {
        std::memcpy(s.peer_mac, frame + 22, 6);
        s.has_peer_mac = 1;
        if (s.peer_ip == 0) s.peer_ip = sender_ip;
    }

    if (op == 1U /* ARP Request */ && target_ip == s.local_ip) {
        oyk_u8 reply[42];
        std::memcpy(reply, frame + 22, 6);     /* Dst MAC */
        std::memcpy(reply + 6, s.mac, 6);      /* Src MAC */
        reply[12] = 0x08; reply[13] = 0x06;
        reply[14] = 0x00; reply[15] = 0x01;
        reply[16] = 0x08; reply[17] = 0x00;
        reply[18] = 6; reply[19] = 4;
        reply[20] = 0x00; reply[21] = 0x02;    /* ARP Reply */
        std::memcpy(reply + 22, s.mac, 6);
        write_u32_raw(reply + 28, s.local_ip);
        std::memcpy(reply + 32, frame + 22, 6);
        write_u32_raw(reply + 38, sender_ip);
        send_eth_frame(slot, reply, sizeof(reply));
    }
}

void TinyTmuxUsbHost::handle_ipv4(int slot, const oyk_u8* frame, size_t len) {
    if (len < 34) return;
    TinyTmuxUsbRadioSlot& s = slots_[slot];
    const oyk_u8* ip = frame + 14;
    size_t ip_avail = len - 14;
    if ((ip[0] >> 4) != 4) return;
    size_t ihl = (size_t)(ip[0] & 0x0F) * 4U;
    if (ihl < 20 || ihl > ip_avail) return;
    size_t total_len = read_be16(ip + 2);
    if (total_len < ihl || total_len > ip_avail) return;

    oyk_u8 proto = ip[9];
    oyk_u32 src_ip = read_u32_raw(ip + 12);
    oyk_u32 dst_ip = read_u32_raw(ip + 16);

    if (src_ip != 0 && (src_ip == s.peer_ip || s.peer_ip == 0)) {
        std::memcpy(s.peer_mac, frame + 6, 6);
        s.has_peer_mac = 1;
        if (s.peer_ip == 0) s.peer_ip = src_ip;
    }

    if (s.local_ip != 0 && dst_ip != s.local_ip && dst_ip != 0xFFFFFFFFUL) {
        return;
    }

    if (proto == 1U /* ICMP */ && total_len >= ihl + 8) {
        const oyk_u8* icmp = ip + ihl;
        size_t icmp_len = total_len - ihl;
        if (icmp[0] == 8U && icmp[1] == 0U && icmp_len <= 1480U) {
            oyk_u8 reply[1480];
            std::memcpy(reply, icmp, icmp_len);
            reply[0] = 0U; /* Echo Reply */
            reply[2] = 0U;
            reply[3] = 0U;
            oyk_u16 cs = inet_checksum(reply, icmp_len);
            write_be16(reply + 2, cs);
            send_ipv4_packet(slot, src_ip, 1U, reply, icmp_len);
        }
        return;
    }

    if (proto == 93U /* AXIP */ && ax25_rx_cb_) {
        const oyk_u8* pay = ip + ihl;
        size_t pay_len = total_len - ihl;
        if (pay_len >= 17 && oyk_verify_fcs(pay, pay_len)) {
            OykFrame f;
            if (oyk_ax25_decode(pay, pay_len - 2, &f)) {
                ax25_rx_cb_(slot, 1, &f, src_ip, 0, ax25_rx_ctx_);
            }
        }
        return;
    }

    if (proto == 17U /* UDP */ && total_len >= ihl + 8) {
        const oyk_u8* udp = ip + ihl;
        oyk_u16 src_port = read_be16(udp + 0);
        oyk_u16 dst_port = read_be16(udp + 2);
        oyk_u16 ulen     = read_be16(udp + 4);
        if (ulen < 8 || ihl + ulen > total_len) return;
        const oyk_u8* pay = udp + 8;
        size_t pay_len = (size_t)(ulen - 8);

        if (udp_rx_cb_) {
            udp_rx_cb_(slot, src_ip, src_port, dst_port, pay, pay_len, udp_rx_ctx_);
        }
        if (ax25_rx_cb_ && pay_len >= 17 && oyk_verify_fcs(pay, pay_len)) {
            OykFrame f;
            if (oyk_ax25_decode(pay, pay_len - 2, &f)) {
                ax25_rx_cb_(slot, 0, &f, src_ip, src_port, ax25_rx_ctx_);
            }
        }
    }
}

void TinyTmuxUsbHost::on_eth_rx(int slot, const oyk_u8* frame, size_t len) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return;
    if (!slots_[slot].in_use || !frame || len < 14) return;
    slots_[slot].rx_packets++;

    oyk_u16 ethertype = read_be16(frame + 12);
    if (ethertype == 0x0806U) {
        handle_arp(slot, frame, len);
    } else if (ethertype == 0x0800U) {
        handle_ipv4(slot, frame, len);
    }
}

void TinyTmuxUsbHost::on_ncm_rx(int slot, const oyk_u8* ntb, size_t ntb_len) {
    size_t eth_len = tinytmux_usb_ncm16_unwrap(ntb, ntb_len, scratch_rx_, sizeof(scratch_rx_));
    if (eth_len > 0) {
        on_eth_rx(slot, scratch_rx_, eth_len);
    }
}

void TinyTmuxUsbHost::on_acm_rx(int slot, const oyk_u8* data, size_t len) {
    if (slot < 0 || slot >= TINYTMUX_MAX_USB_RADIOS) return;
    if (!slots_[slot].in_use || !data || len == 0) return;
    slots_[slot].rx_packets++;
    if (acm_rx_cb_) {
        acm_rx_cb_(slot, data, len, acm_rx_ctx_);
    }
}
