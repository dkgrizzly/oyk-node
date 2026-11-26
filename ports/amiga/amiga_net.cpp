/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "amiga_net.hpp"
#include "amiga_policy.hpp"

#include <stdio.h>
#include <string.h>

#ifdef __AMIGA__
#include <exec/execbase.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <devices/serial.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
extern struct ExecBase* SysBase;
#endif

void oyk_amiga_detect_caps(OykAmigaNetCaps* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->has_serial_device = 1; /* Amiga built-in Paula/CIA serial.device */

#ifdef __AMIGA__
    out->exec_version = SysBase ? static_cast<int>(SysBase->LibNode.lib_Version) : 34;

    /* Probe AmiTCP / Roadshow / UAE bsdsocket.library cleanly without hard dependency */
    {
        struct Library* sock_lib = OpenLibrary(reinterpret_cast<CONST_STRPTR>("bsdsocket.library"), 3);
        if (sock_lib) {
            out->has_bsdsocket = 1;
            CloseLibrary(sock_lib);
        }
    }

    /* Probe serial.device presence in Exec device list or via FindName */
    if (SysBase) {
        Forbid();
        if (FindName(&SysBase->DeviceList, reinterpret_cast<CONST_STRPTR>("serial.device")) != NULL) {
            out->has_serial_device = 1;
        }
        Permit();
    }

    /* Inspect Workbench default screen dimensions & color depth */
    {
        struct Library* ilib = OpenLibrary(reinterpret_cast<CONST_STRPTR>("intuition.library"), 33);
        struct Library* glib = OpenLibrary(reinterpret_cast<CONST_STRPTR>("graphics.library"), 33);
        if (ilib && glib) {
            IntuitionBase = reinterpret_cast<struct IntuitionBase*>(ilib);
            GfxBase = reinterpret_cast<struct GfxBase*>(glib);
            if (out->exec_version >= 36) {
                struct Screen* wb = LockPubScreen(reinterpret_cast<CONST_STRPTR>("Workbench"));
                if (wb) {
                    out->wb_width = static_cast<int>(wb->Width);
                    out->wb_height = static_cast<int>(wb->Height);
                    if (out->exec_version >= 39) {
                        out->wb_depth = static_cast<int>(GetBitMapAttr(wb->RastPort.BitMap, BMA_DEPTH));
                    } else {
                        out->wb_depth = static_cast<int>(wb->RastPort.BitMap->Depth);
                    }
                    UnlockPubScreen(NULL, wb);
                }
            } else {
                struct Screen wb_copy;
                memset(&wb_copy, 0, sizeof(wb_copy));
                if (GetScreenData(&wb_copy, sizeof(wb_copy), WBENCHSCREEN, NULL)) {
                    out->wb_width = static_cast<int>(wb_copy.Width);
                    out->wb_height = static_cast<int>(wb_copy.Height);
                    out->wb_depth = static_cast<int>(wb_copy.BitMap.Depth);
                }
            }
        }
        if (glib) CloseLibrary(glib);
        if (ilib) CloseLibrary(ilib);
    }
#else
    out->exec_version = 40;
    out->wb_width = 640;
    out->wb_height = 256;
    out->wb_depth = 2;
#endif

    if (out->wb_width <= 0) out->wb_width = 640;
    if (out->wb_height <= 0) out->wb_height = 256;
    if (out->wb_depth <= 0) out->wb_depth = 2;
    out->wb_colors = oyk_amiga_colors_from_depth(out->wb_depth);

    sprintf(out->backend_summary, "serial.device; %s (%d clr)",
            out->has_bsdsocket ? "bsdsocket.library" : "no bsdsocket",
            out->wb_colors);
}

int oyk_amiga_net_init(OykAmigaNetState* net, OykPortableNode* node) {
    if (!net) return 0;
    memset(net, 0, sizeof(*net));
    oyk_amiga_detect_caps(&net->caps);
    net->node = node;
    net->initialized = 1;
    return 1;
}

void oyk_amiga_net_poll(OykAmigaNetState* net) {
    (void)net;
}

void oyk_amiga_net_shutdown(OykAmigaNetState* net) {
    if (net) net->initialized = 0;
}

int oyk_amiga_tx_callback(int iface_idx,
                          const OykFrame* frame,
                          oyk_u32 peer_ip,
                          oyk_u16 peer_port,
                          oyk_u16 peer_handle,
                          void* ctx) {
    (void)iface_idx;
    (void)peer_ip;
    (void)peer_port;
    (void)peer_handle;
    OykAmigaNetState* net = static_cast<OykAmigaNetState*>(ctx);
    if (net) ++net->tx_packets;
    return (frame != NULL) ? 1 : 0;
}
