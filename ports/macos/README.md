<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# OYK-Node for macOS (Mac OS X 10.4 PowerPC – macOS 15+ Apple Silicon)

Unified AppKit frontend driven by shared `oyk_gui` & `oyk_term` middleware,
supporting three CPU architectures via `macos_engine_bridge`:

| Architecture | OS Versions | Core Engine | Build Mode |
| --- | --- | --- | --- |
| Apple Silicon (`arm64`) | macOS 11.0+ | C++20 `oyk::node::NodeEngine` | Default Universal |
| Intel (`x86_64`) | macOS 10.15+ / 11+ | C++20 `oyk::node::NodeEngine` | Default Universal |
| PowerPC (`ppc`) | Mac OS X 10.4–10.6 | Strict C++98 `OykPortableNode` | `-DOYK_MACOS_PORTABLE_CORE=ON` |

## Building on macOS (Universal Intel + Apple Silicon `.app` + `.dmg`)

```sh
./ports/macos/build_macos.sh
```

## Building on Mac OS X 10.4 / 10.5 PowerPC

```sh
OYK_MACOS_ARCHS=ppc OYK_MACOS_MIN_VERSION=10.4 OYK_MACOS_PORTABLE_CORE=ON ./ports/macos/build_macos.sh
```
