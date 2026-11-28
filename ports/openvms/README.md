<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# OYK Packet Node — OpenVMS Port (`ports/openvms/`)

Strict ANSI C89 port targeting **OpenVMS** across **VAX**, **Alpha**, **Itanium (`IA64`)**, and **x86-64**, sharing `ports/sysv/` for both:

- **SMG$ / VT100 Console** (`OYK_CONSOLE.EXE`): SMG$ screen management, VT100 escape output, `$QIO` terminal I/O, and UCX/TCP/IP Services sockets.
- **DECwindows Motif 1.2** (`OYK_MOTIF.EXE`): 23-screen X11/Motif desktop GUI via `ports/sysv/sysv_motif_ui.c`.

## Building on OpenVMS

```text
@BUILD_VMS.COM
```

Or with MMS / MMK:

```text
MMS /MACRO=(ARCH=VAX) ALL,OYK_MOTIF.EXE
```

Linker option files are provided for all four architectures (`VMS_VAX.OPT`, `VMS_ALPHA.OPT`, `VMS_IA64.OPT`, `VMS_X86_64.OPT`) plus DECwindows Motif 1.2 (`VMS_MOTIF.OPT`).

## Capturing DECwindows Motif UI Gallery Screenshots

Run `ports/openvms/run_vms_motif_gallery.sh` from a Linux host with `Xvfb`, `xdpyinfo`, `xwininfo`, `xprop`, ImageMagick `import`, and Python 3 installed. It launches `DECW$MWM.EXE` and `OYK_MOTIF.EXE` (`--ui-test` / `OYK_UI_TEST_MODE=1`) over remote TCP/IP X11 to capture all 23 canonical DECwindows Motif screens:

```bash
OYK_VMS_HOST=<vms-host-or-ip> \
OYK_VMS_USER=<vms-username> \
OYK_VMS_PASS=<vms-password> \
OYK_VMS_DISPLAY_HOST=<linux-x11-host-ip> \
  ./ports/openvms/run_vms_motif_gallery.sh --build docs/user/images/ui-gallery
```

Optional environment variables:

- `OYK_VMS_WORK_DIR`: DCL working directory containing the OpenVMS build tree (default: `[.OYK.PORTS.OPENVMS]`).
- `OYK_VMS_DISPLAY_NUM`: Xvfb display number to bind with `-ac -listen tcp` (auto-selected from `99..125` if omitted).
