<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# AT&T 3B2 SVR3.2 Port (`ports/att3b2/`)

Native **WE32100** AT&T UNIX System V Release 3.2 port (`oyk-node-3b2` and `oyk-3b2-test`) built with `/usr/local/bin/gcc` (`2.3.3`) on top of the shared strict-C89 core in [ports/sysv/](../sysv/README.md).

## Features

- **SVR3.2 `curses` & VT100 TUI**: Canonical 23-screen operator UI with monochrome/attribute highlighting (`A_REVERSE`, `A_BOLD`, `A_UNDERLINE`) plus optional `--ansi` / `--ansi-color` VT100 rendering.
- **Wollongong WIN/TCP Networking**: Compiles against `<sys/in.h>` and links `-lnet -lnsl_s` (`OYK_SYSV_SVR32`).
- **Serial KISS (`/dev/tty*`)**: Uses SVR3.2 `termio.h` (`TCGETA`/`TCSETA`) non-blocking serial I/O.

## Building on AT&T 3B2

```sh
/usr/local/bin/make -C ports/att3b2
/usr/local/bin/make -C ports/att3b2 test
```

## Live Simulator Runner

Pass host, port, and username strictly via environment variables (`OYK_3B2_HOST`, `OYK_3B2_PORT`, `OYK_3B2_USER`):

```sh
OYK_3B2_HOST=... OYK_3B2_PORT=... OYK_3B2_USER=... \
    ./ports/att3b2/run_3b2_remote.sh --build --unit-tests
```
