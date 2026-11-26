/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * Amiga Workbench Native Entry Point (OS 3.1 / 2.04 / 1.3 + AROS m68k)
 */

#include "amiga_policy.hpp"
#include "amiga_net.hpp"
#include "amiga_ui.hpp"
#include "amiga_capture.hpp"

#include <string.h>

#ifdef __AMIGA__
#include <workbench/icon.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/icon.h>
#endif

static int am_check_workbench_tooltype(const char* prog_name) {
#ifdef __AMIGA__
    if (!prog_name || !prog_name[0]) return -1;
    struct Library* icon_lib = OpenLibrary(reinterpret_cast<CONST_STRPTR>("icon.library"), 33);
    if (!icon_lib) return -1;
    IconBase = icon_lib;
    int res = -1;
    struct DiskObject* dobj = GetDiskObject(reinterpret_cast<CONST_STRPTR>(prog_name));
    if (dobj) {
        if (dobj->do_ToolTypes) {
            UBYTE* val = FindToolType(dobj->do_ToolTypes,
                                      reinterpret_cast<CONST_STRPTR>("PRIVATESCREEN"));
            if (val) {
                res = oyk_amiga_parse_tooltype_privatescreen(reinterpret_cast<const char*>(val));
            }
        }
        FreeDiskObject(dobj);
    }
    CloseLibrary(icon_lib);
    return res;
#else
    (void)prog_name;
    return -1;
#endif
}

int main(int argc, char** argv) {
    int cli_priv = -1;
    int ui_test = 0;
    const char* cfg_path = "NODE.CFG";
    oyk_amiga_parse_cli_flags(argc, argv, &cli_priv, &ui_test, &cfg_path);
    if (!ui_test && oyk_amiga_is_ui_test_mode()) {
        ui_test = 1;
    }

    OykPortableNode* node = new OykPortableNode();
    OykAmigaNetState* net = new OykAmigaNetState();
    OykConfig* init_cfg = new OykConfig();
    if (!node || !net || !init_cfg) {
        if (node) delete node;
        if (net) delete net;
        if (init_cfg) delete init_cfg;
        oyk_amiga_ui_show_fatal("Out of Chip/Fast RAM starting OYK Node.");
        return 1;
    }

    char err_buf[128];
    err_buf[0] = '\0';
    oyk_config_init_default(init_cfg);
    (void)oyk_config_load(cfg_path, init_cfg, err_buf, sizeof(err_buf));

    int tt_priv = (argc > 0 && argv) ? am_check_workbench_tooltype(argv[0]) : -1;
    int use_priv = oyk_amiga_should_use_private_screen(init_cfg, cli_priv, tt_priv);

    oyk_amiga_net_init(net, node);
    node->init(init_cfg, oyk_amiga_tx_callback, net, err_buf, sizeof(err_buf));
    delete init_cfg;

    int rc = oyk_amiga_ui_run(node, net, use_priv, ui_test);

    oyk_amiga_net_shutdown(net);
    node->shutdown();
    delete net;
    delete node;
    return rc;
}
