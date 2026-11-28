<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Motorola 88K System V/88 Release 4 Port (`ports/m88k/`)

Native **mc88100** Motorola System V Release 4.0 port built with Motorola CCCS 1.0 (`/usr/ccs/bin/cc`) on top of the shared strict-C89 core in [ports/sysv/](../sysv/README.md).

## Targets

- **`oyk-node-m88k`**: SVR4 color `curses` TUI (`start_color()`, `SIGWINCH`, `/dev/ttya*` serial KISS, BSD `-lsocket -lnsl`).
- **`oyk-node-m88k-motif`**: Native X11R5 + OSF/Motif 1.2 GUI (`-lXm -lXt -lX11 -lgen -lsocket -lnsl`) with `mwm` window framing, titlebar icon (`XmNiconPixmap`), and live 19-role theme palette.
- **`oyk-m88k-test`**: Native mc88100 standalone unit-test runner.

## Building on Motorola 88K SVR4

```sh
(cd ports/m88k && make)
(cd ports/m88k && make test)
```

## Remote Verification & UI Gallery Capture

Set connection credentials exclusively via environment variables (`OYK_M88K_HOST`, `OYK_M88K_USER`, `OYK_M88K_PASS`, optional `OYK_M88K_REMOTE_DIR`):

```sh
OYK_M88K_HOST=... OYK_M88K_USER=... OYK_M88K_PASS=... \
    ./ports/m88k/run_m88k_remote.sh --build --unit-tests --capture /tmp/m88k_caps
```
