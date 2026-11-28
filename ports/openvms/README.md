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
MMS /MACRO=("ARCH=ALPHA")
```

Linker option files are provided for all four architectures (`VMS_VAX.OPT`, `VMS_ALPHA.OPT`, `VMS_IA64.OPT`, `VMS_X86_64.OPT`).
