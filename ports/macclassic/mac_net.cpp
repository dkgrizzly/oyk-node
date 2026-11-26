/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "mac_net.hpp"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifdef TARGET_API_MAC_OS8
#  undef TARGET_API_MAC_OS8
#endif

#include <MacTypes.h>
#include <OSUtils.h>
#include <Gestalt.h>
#include <Traps.h>
#include <Devices.h>
#include <Serial.h>
#include <Multiverse.h>
#include <Memory.h>

/* MacTCP UDP parameter block definitions (68k Macintosh byte layout) */
#pragma pack(push, 2)
struct OykMacUDPCreatePB {
    Ptr           rcvBuff;
    unsigned long rcvBuffLen;
    ProcPtr       notifyProc;
    unsigned short localPort;
    Ptr           userDataPtr;
    unsigned short endingPort;
};

struct OykMacUDPSendPB {
    unsigned short reserved;
    unsigned long  remoteHost;
    unsigned short remotePort;
    Ptr            wdsPtr;
    Boolean        checkSum;
    unsigned char  pad;
    unsigned short sendLength;
    Ptr            userDataPtr;
    unsigned short localPort;
};

struct OykMacUDPReceivePB {
    unsigned short timeOut;
    unsigned long  remoteHost;
    unsigned short remotePort;
    Ptr            rcvBuff;
    unsigned short rcvBuffLen;
    unsigned short secondTimeStamp;
    Ptr            userDataPtr;
    unsigned long  destHost;
    unsigned short destPort;
};

union OykMacUDPcsParam {
    OykMacUDPCreatePB  create;
    OykMacUDPSendPB    send;
    OykMacUDPReceivePB receive;
    unsigned char      raw[48];
};

struct OykMacUDPiopb {
    QElemPtr          qLink;
    short             qType;
    short             ioTrap;
    Ptr               ioCmdAddr;
    ProcPtr           ioCompletion;
    OSErr             ioResult;
    StringPtr         ioNamePtr;
    short             ioVRefNum;
    short             ioCRefNum;
    short             csCode;
    StreamPtr         udpStream;
    OykMacUDPcsParam  csParam;
};
#pragma pack(pop)

enum {
    OYK_MACTCP_UDP_CREATE  = 20,
    OYK_MACTCP_UDP_READ    = 21,
    OYK_MACTCP_UDP_BFRRET  = 22,
    OYK_MACTCP_UDP_WRITE   = 23,
    OYK_MACTCP_UDP_RELEASE = 24,
    OYK_MACTCP_IPCTL_ADDR  = 15
};

static void mn_copy(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1U;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int mn_strieq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower(static_cast<unsigned char>(*a)) !=
            tolower(static_cast<unsigned char>(*b))) {
            return 0;
        }
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

static int mn_trap_available(unsigned short trap) {
    TrapType t = ((trap & 0x0800U) != 0U) ? kToolboxTrapType : kOSTrapType;
    return (NGetTrapAddress(trap, t) !=
            NGetTrapAddress(_Unimplemented, kToolboxTrapType)) ? 1 : 0;
}

static int mn_port_index(const char* dev_name) {
    if (!dev_name || !dev_name[0]) return 0;
    if (mn_strieq(dev_name, "Printer") || mn_strieq(dev_name, "COM2") ||
        mn_strieq(dev_name, ".BIn") || mn_strieq(dev_name, ".BOut") ||
        mn_strieq(dev_name, "portb")) {
        return 1;
    }
    return 0;
}

static unsigned short mn_baud_to_ser_code(oyk_u32 baud) {
    if (baud >= 57600UL) return baud57600;
    if (baud >= 38400UL) return baud38400;
    if (baud >= 19200UL) return baud19200;
    if (baud >= 9600UL)  return baud9600;
    if (baud >= 4800UL)  return baud4800;
    if (baud >= 2400UL)  return baud2400;
    if (baud >= 1200UL)  return baud1200;
    return baud300;
}

static int mn_open_ipp(short* out_refnum, oyk_u32* out_ip) {
    unsigned char ipp_name[5];
    ipp_name[0] = 4;
    ipp_name[1] = '.';
    ipp_name[2] = 'I';
    ipp_name[3] = 'P';
    ipp_name[4] = 'P';
    short ref = 0;
    OSErr err = OpenDriver(ipp_name, &ref);
    if (err != noErr) {
        return 0;
    }
    if (out_refnum) *out_refnum = ref;
    if (out_ip) {
        GetAddrParamBlock pb;
        memset(&pb, 0, sizeof(pb));
        pb.ioCRefNum = ref;
        pb.csCode = OYK_MACTCP_IPCTL_ADDR;
        if (PBControlSync(reinterpret_cast<ParmBlkPtr>(&pb)) == noErr) {
            *out_ip = static_cast<oyk_u32>(pb.ourAddress);
        }
    }
    return 1;
}

void oyk_mac_detect_caps(OykMacNetCaps* caps) {
    if (!caps) return;
    memset(caps, 0, sizeof(*caps));
    caps->sys_version_bcd = 0x0608;
    caps->has_serial = 1;

    if (mn_trap_available(_Gestalt)) {
        caps->has_gestalt = 1;
        long v = 0;
        if (Gestalt(gestaltSystemVersion, &v) == noErr) {
            caps->sys_version_bcd = static_cast<int>(v & 0xFFFFL);
        }
        long qd = 0;
        if (Gestalt(gestaltQuickdrawVersion, &qd) == noErr) {
            if (qd >= gestalt8BitQD) caps->has_color_qd = 1;
        }
        long sf = 0;
        if (Gestalt(gestaltStandardFileAttr, &sf) == noErr && (sf & 1L) != 0L) {
            caps->has_std_file_7 = 1;
        }
        long ot = 0;
        if (Gestalt('otvr', &ot) == noErr && ot != 0L) {
            /* Open Transport present (System 7.5+ / Mac OS 8/9 / PPC); also provides .IPP compat */
            caps->has_open_transport = 1;
        }
    } else {
        SysEnvRec env;
        memset(&env, 0, sizeof(env));
        if (SysEnvirons(1, &env) == noErr) {
            caps->sys_version_bcd = static_cast<int>(env.systemVersion);
            caps->has_color_qd = env.hasColorQD ? 1 : 0;
        }
    }

    caps->has_wait_next_event = mn_trap_available(_WaitNextEvent);

    short dummy_ref = 0;
    oyk_u32 ip = 0;
    if (mn_open_ipp(&dummy_ref, &ip)) {
        caps->has_mactcp = 1;
        caps->local_ip_net = ip;
    }

    if (caps->has_open_transport && caps->has_mactcp) {
        caps->active_backend = OYK_MAC_NET_OPENTPT;
        mn_copy(caps->backend_summary, sizeof(caps->backend_summary),
                "Open Transport + MacTCP (.IPP) + Serial KISS");
    } else if (caps->has_mactcp) {
        caps->active_backend = OYK_MAC_NET_MACTCP;
        mn_copy(caps->backend_summary, sizeof(caps->backend_summary),
                "MacTCP (.IPP) + Serial KISS");
    } else {
        caps->active_backend = OYK_MAC_NET_SERIAL_ONLY;
        mn_copy(caps->backend_summary, sizeof(caps->backend_summary),
                "Serial KISS (.AIn/.BIn) [No TCP/IP stack installed]");
    }
}

int oyk_mac_serial_open(OykMacNetState* st, const char* dev_name, oyk_u32 baud) {
    if (!st) return 0;
    int idx = mn_port_index(dev_name);
    OykMacSerialPort* sp = &st->serial_ports[idx];
    if (sp->open) return 1;

    unsigned char in_p[8];
    unsigned char out_p[8];
    if (idx == 0) {
        in_p[0] = 4;  memcpy(in_p + 1, ".AIn", 4);
        out_p[0] = 5; memcpy(out_p + 1, ".AOut", 5);
    } else {
        in_p[0] = 4;  memcpy(in_p + 1, ".BIn", 4);
        out_p[0] = 5; memcpy(out_p + 1, ".BOut", 5);
    }

    short in_ref = 0;
    short out_ref = 0;
    if (OpenDriver(out_p, &out_ref) != noErr) return 0;
    if (OpenDriver(in_p, &in_ref) != noErr) {
        CloseDriver(out_ref);
        return 0;
    }

    unsigned short ser_cfg = static_cast<unsigned short>(
        mn_baud_to_ser_code(baud) | data8 | stop10 | noParity);
    SerReset(out_ref, ser_cfg);
    SerReset(in_ref, ser_cfg);

    SerShk shk;
    memset(&shk, 0, sizeof(shk));
    SerHShake(out_ref, &shk);
    SerHShake(in_ref, &shk);

    sp->open = 1;
    sp->in_ref = in_ref;
    sp->out_ref = out_ref;
    sp->baud = baud;
    oyk_kiss_decoder_init(&sp->kiss_dec);
    mn_copy(sp->dev_name, sizeof(sp->dev_name), dev_name ? dev_name : (idx == 0 ? "Modem" : "Printer"));
    return 1;
}

void oyk_mac_serial_close_all(OykMacNetState* st) {
    if (!st) return;
    for (int i = 0; i < 2; ++i) {
        if (st->serial_ports[i].open) {
            ParamBlockRec pb;
            memset(&pb, 0, sizeof(pb));
            pb.ioParam.ioRefNum = st->serial_ports[i].in_ref;
            PBKillIOSync(&pb);
            pb.ioParam.ioRefNum = st->serial_ports[i].out_ref;
            PBKillIOSync(&pb);
            CloseDriver(st->serial_ports[i].in_ref);
            CloseDriver(st->serial_ports[i].out_ref);
            memset(&st->serial_ports[i], 0, sizeof(st->serial_ports[i]));
        }
    }
}

int oyk_mac_serial_write(OykMacNetState* st, const char* dev_name,
                         const oyk_u8* data, size_t len) {
    if (!st || !data || len == 0) return 0;
    int idx = mn_port_index(dev_name);
    OykMacSerialPort* sp = &st->serial_ports[idx];
    if (!sp->open) return 0;
    ParamBlockRec pb;
    memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioRefNum = sp->out_ref;
    pb.ioParam.ioBuffer = reinterpret_cast<Ptr>(const_cast<oyk_u8*>(data));
    pb.ioParam.ioReqCount = static_cast<long>(len);
    OSErr err = PBWriteSync(&pb);
    return (err == noErr) ? 1 : 0;
}

size_t oyk_mac_serial_read(OykMacNetState* st, const char* dev_name,
                           oyk_u8* out, size_t cap) {
    if (!st || !out || cap == 0) return 0;
    int idx = mn_port_index(dev_name);
    OykMacSerialPort* sp = &st->serial_ports[idx];
    if (!sp->open) return 0;
    long avail = 0;
    if (SerGetBuf(sp->in_ref, &avail) != noErr || avail <= 0) return 0;
    if (static_cast<size_t>(avail) > cap) avail = static_cast<long>(cap);
    ParamBlockRec pb;
    memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioRefNum = sp->in_ref;
    pb.ioParam.ioBuffer = reinterpret_cast<Ptr>(out);
    pb.ioParam.ioReqCount = avail;
    if (PBReadSync(&pb) != noErr) return 0;
    return static_cast<size_t>(pb.ioParam.ioActCount);
}

int oyk_mac_net_init(OykMacNetState* st, OykPortableNode* node) {
    if (!st) return 0;
    memset(st, 0, sizeof(*st));
    st->node = node;
    oyk_mac_detect_caps(&st->caps);
    if (st->caps.has_mactcp) {
        if (mn_open_ipp(&st->ipp_refnum, &st->caps.local_ip_net)) {
            st->ipp_open = 1;
        }
    }
    return 1;
}

static void mn_release_udp(OykMacNetState* st, int slot_idx) {
    if (!st || !st->ipp_open) return;
    OykMacUdpSlot* u = &st->udp_slots[slot_idx];
    if (!u->active || !u->stream_ptr) {
        memset(u, 0, sizeof(*u));
        return;
    }
    OykMacUDPiopb pb;
    memset(&pb, 0, sizeof(pb));
    pb.ioCRefNum = st->ipp_refnum;
    pb.csCode = OYK_MACTCP_UDP_RELEASE;
    pb.udpStream = static_cast<StreamPtr>(u->stream_ptr);
    PBControlSync(reinterpret_cast<ParmBlkPtr>(&pb));
    if (pb.csParam.create.rcvBuff) {
        DisposePtr(pb.csParam.create.rcvBuff);
    }
    memset(u, 0, sizeof(*u));
}

static void mn_release_tcp(OykMacNetState* st, int slot_idx) {
    if (!st || !st->ipp_open) return;
    OykMacTcpSlot* t = &st->tcp_slots[slot_idx];
    if (!t->active || !t->stream_ptr) {
        memset(t, 0, sizeof(*t));
        return;
    }
    TCPiopb pb;
    memset(&pb, 0, sizeof(pb));
    pb.ioCRefNum = st->ipp_refnum;
    pb.csCode = TCPAbort;
    pb.tcpStream = static_cast<StreamPtr>(t->stream_ptr);
    PBControlSync(reinterpret_cast<ParmBlkPtr>(&pb));
    memset(&pb, 0, sizeof(pb));
    pb.ioCRefNum = st->ipp_refnum;
    pb.csCode = TCPRelease;
    pb.tcpStream = static_cast<StreamPtr>(t->stream_ptr);
    PBControlSync(reinterpret_cast<ParmBlkPtr>(&pb));
    if (pb.csParam.create.rcvBuff) {
        DisposePtr(pb.csParam.create.rcvBuff);
    }
    memset(t, 0, sizeof(*t));
}

void oyk_mac_net_shutdown(OykMacNetState* st) {
    if (!st) return;
    for (int i = 0; i < OYK_MAX_INTERFACES; ++i) {
        mn_release_udp(st, i);
    }
    for (int j = 0; j < OYK_MAX_SESSIONS; ++j) {
        mn_release_tcp(st, j);
    }
    oyk_mac_serial_close_all(st);
    st->ipp_open = 0;
}

int oyk_mac_net_sync_ifaces(OykMacNetState* st, const OykConfig* cfg) {
    if (!st || !cfg) return 0;
    for (int i = 0; i < OYK_MAX_INTERFACES; ++i) {
        mn_release_udp(st, i);
    }
    for (int j = 0; j < OYK_MAX_SESSIONS; ++j) {
        mn_release_tcp(st, j);
    }
    oyk_mac_serial_close_all(st);

    for (int idx = 0; idx < cfg->iface_count && idx < OYK_MAX_INTERFACES; ++idx) {
        const OykIfaceConfig* ic = &cfg->ifaces[idx];
        if (mn_strieq(ic->type, "kiss-serial")) {
            int ok = oyk_mac_serial_open(st, ic->device[0] ? ic->device : "Modem",
                                         ic->baud ? ic->baud : 9600UL);
            if (st->node) {
                st->node->set_iface_status(idx, ok, ok ? "KISS Serial Up" : "Serial Port Busy");
            }
        } else if (mn_strieq(ic->type, "axudp")) {
            if (!st->ipp_open) {
                if (st->node) st->node->set_iface_status(idx, 0, "MacTCP/OT Missing");
                continue;
            }
            Ptr rbuf = NewPtrClear(4096L);
            if (!rbuf) {
                if (st->node) st->node->set_iface_status(idx, 0, "Out of Memory");
                continue;
            }
            OykMacUDPiopb pb;
            memset(&pb, 0, sizeof(pb));
            pb.ioCRefNum = st->ipp_refnum;
            pb.csCode = OYK_MACTCP_UDP_CREATE;
            pb.csParam.create.rcvBuff = rbuf;
            pb.csParam.create.rcvBuffLen = 4096UL;
            pb.csParam.create.localPort = ic->port ? ic->port : 10093U;
            if (PBControlSync(reinterpret_cast<ParmBlkPtr>(&pb)) == noErr &&
                pb.udpStream != NULL) {
                st->udp_slots[idx].active = 1;
                st->udp_slots[idx].iface_idx = idx;
                st->udp_slots[idx].bind_port = pb.csParam.create.localPort;
                st->udp_slots[idx].stream_ptr = pb.udpStream;
                if (st->node) st->node->set_iface_status(idx, 1, "UDP Listening");
            } else {
                DisposePtr(rbuf);
                if (st->node) st->node->set_iface_status(idx, 0, "UDP Open Failed");
            }
        } else if (mn_strieq(ic->type, "telnet") || mn_strieq(ic->type, "kiss-tcp")) {
            if (!st->ipp_open) {
                if (st->node) st->node->set_iface_status(idx, 0, "MacTCP/OT Missing");
                continue;
            }
            if (st->node) {
                st->node->set_iface_status(idx, 1,
                                           mn_strieq(ic->type, "telnet")
                                               ? "Telnet Ready"
                                               : "KISS-TCP Ready");
            }
        }
    }
    return 1;
}

void oyk_mac_net_poll(OykMacNetState* st) {
    if (!st || !st->node) return;
    const OykConfig* cfg = &st->node->config();

    /* 1. Poll KISS serial interfaces */
    for (int idx = 0; idx < cfg->iface_count && idx < OYK_MAX_INTERFACES; ++idx) {
        const OykIfaceConfig* ic = &cfg->ifaces[idx];
        if (!mn_strieq(ic->type, "kiss-serial")) continue;
        const char* dev = ic->device[0] ? ic->device : "Modem";
        size_t n = oyk_mac_serial_read(st, dev, st->io_buf, sizeof(st->io_buf));
        if (n > 0) {
            OykMacSerialPort* sp = &st->serial_ports[mn_port_index(dev)];
            for (size_t k = 0; k < n; ++k) {
                oyk_u8 kport = 0;
                oyk_u8 kcmd = 0;
                if (oyk_kiss_decoder_feed_byte(&sp->kiss_dec, st->io_buf[k], &kport, &kcmd) == 1) {
                    if (kcmd == 0U && sp->kiss_dec.len >= 15U) {
                        OykFrame f;
                        if (oyk_ax25_decode(sp->kiss_dec.buf, sp->kiss_dec.len, &f)) {
                            st->node->on_rx_frame(idx, &f, 0UL, 0U, 0U);
                        }
                    }
                }
            }
        }
    }

    /* 2. Poll MacTCP / OpenTransport compat UDP streams */
    if (st->ipp_open) {
        for (int i = 0; i < OYK_MAX_INTERFACES; ++i) {
            if (!st->udp_slots[i].active) continue;
            OykMacUDPiopb pb;
            memset(&pb, 0, sizeof(pb));
            pb.ioCRefNum = st->ipp_refnum;
            pb.csCode = OYK_MACTCP_UDP_READ;
            pb.udpStream = static_cast<StreamPtr>(st->udp_slots[i].stream_ptr);
            pb.csParam.receive.timeOut = 0;
            if (PBControlSync(reinterpret_cast<ParmBlkPtr>(&pb)) == noErr &&
                pb.csParam.receive.rcvBuff && pb.csParam.receive.rcvBuffLen > 0) {
                oyk_u16 blen = pb.csParam.receive.rcvBuffLen;
                if (blen > sizeof(st->io_buf)) blen = sizeof(st->io_buf);
                memcpy(st->io_buf, pb.csParam.receive.rcvBuff, blen);
                oyk_u32 rip = static_cast<oyk_u32>(pb.csParam.receive.remoteHost);
                oyk_u16 rport = static_cast<oyk_u16>(pb.csParam.receive.remotePort);
                pb.csCode = OYK_MACTCP_UDP_BFRRET;
                PBControlSync(reinterpret_cast<ParmBlkPtr>(&pb));
                OykFrame f;
                if (oyk_ax25_decode(st->io_buf, blen, &f)) {
                    st->node->on_rx_frame(st->udp_slots[i].iface_idx, &f, rip, rport, 0U);
                }
            }
        }
    }
}

int oyk_mac_tx_callback(int iface_idx,
                        const OykFrame* frame,
                        oyk_u32 peer_ip,
                        oyk_u16 peer_port,
                        oyk_u16 peer_handle,
                        void* user_ctx) {
    (void)peer_handle;
    OykMacNetState* st = static_cast<OykMacNetState*>(user_ctx);
    if (!st || !st->node || !frame) return 0;
    const OykConfig* cfg = &st->node->config();
    if (iface_idx < 0 || iface_idx >= cfg->iface_count) return 0;
    const OykIfaceConfig* ic = &cfg->ifaces[iface_idx];

    if (mn_strieq(ic->type, "kiss-serial")) {
        oyk_u8 raw[512];
        oyk_u8 kiss[768];
        size_t flen = oyk_ax25_encode(frame, raw, sizeof(raw));
        if (flen == 0) return 0;
        size_t klen = oyk_kiss_encode(0, raw, flen, kiss, sizeof(kiss));
        if (klen == 0) return 0;
        return oyk_mac_serial_write(st, ic->device[0] ? ic->device : "Modem", kiss, klen);
    }

    if (mn_strieq(ic->type, "axudp") && st->ipp_open && st->udp_slots[iface_idx].active) {
        oyk_u8 pkt[512];
        size_t plen = oyk_ax25_encode(frame, pkt, sizeof(pkt));
        if (plen == 0) return 0;
        wdsEntry wds[2];
        wds[0].length = static_cast<unsigned short>(plen);
        wds[0].ptr = reinterpret_cast<Ptr>(pkt);
        wds[1].length = 0;
        wds[1].ptr = NULL;

        OykMacUDPiopb pb;
        memset(&pb, 0, sizeof(pb));
        pb.ioCRefNum = st->ipp_refnum;
        pb.csCode = OYK_MACTCP_UDP_WRITE;
        pb.udpStream = static_cast<StreamPtr>(st->udp_slots[iface_idx].stream_ptr);
        pb.csParam.send.remoteHost = static_cast<unsigned long>(peer_ip);
        pb.csParam.send.remotePort = peer_port ? peer_port : 10093U;
        pb.csParam.send.wdsPtr = reinterpret_cast<Ptr>(wds);
        pb.csParam.send.checkSum = true;
        pb.csParam.send.localPort = st->udp_slots[iface_idx].bind_port;
        return (PBControlSync(reinterpret_cast<ParmBlkPtr>(&pb)) == noErr) ? 1 : 0;
    }

    return 0;
}
