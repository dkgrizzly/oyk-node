<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Shared System V / Strict-C89 Middleware (`ports/sysv/`)

Shared strict ANSI C89 (`-std=c89 -pedantic`) engine, canonical 23-screen TUI cell renderer (`sysv_tui.*`), BSD/WIN/UCX socket & serial KISS abstraction (`sysv_net.*`), and shared OSF/Motif 1.2 X11 UI (`sysv_motif_ui.*`) used by retro-Unix and OpenVMS targets without full ISO C++98/C++20 toolchains:

- **Motorola 88K SVR4** (`ports/m88k/`)
- **AT&T 3B2 SVR3.2** (`ports/att3b2/`)
- **OpenVMS** (`ports/openvms/`)

Verified on Linux hosts via strict C89 CMake target `oyk_sysv_tests`.
