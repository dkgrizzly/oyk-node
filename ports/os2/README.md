<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# OYK Packet Node — OS/2 2.0 / Warp 3.0 / Warp 4.0 Port (`ports/os2/`)

Strict C++98 32-bit OS/2 port built against the shared `ports/compat/` core (`oyk_portable.*`, `oyk_gui.*`, `oyk_tui.*`, `oyk_term.*`):

- **`oykos2.exe` (VIO / KBD / MOU Console)**: Dynamic full-display VIO text-mode UI implementing all 23 canonical TUI screens via `ports/compat/oyk_tui.*`.
- **`oykos2pm.exe` (Presentation Manager GUI)**: Native 32-bit PM window (`WinCreateStdWindow`, GPI graphics, native RC checkboxes/radios/comboboxes, `IDI_OYK_NODE` icon, left-aligned About logo, whole-screen live theme preview).
- **Dynamic TCP/IP + COM1-COM4 KISS**: Loads `SO32DLL` dynamically via `DosLoadModule`/`DosQueryProcAddr` (graceful fallback when IBM TCP/IP or MPTS is absent) plus `DosDevIOCtl` `COM1`–`COM4` serial KISS.

## Building with OpenWatcom

```sh
wmake -f makefile.os2
```
