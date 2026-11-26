<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Agent & Contributor Guidelines

This file defines mandatory rules for AI coding agents and contributors working in this repository.

## 1. SPDX & Copyright Headers

Every source, build, script, configuration, and documentation file created or modified in this repository MUST include an SPDX license identifier and copyright header matching the comment style of its neighboring files:

- **C++20 headers & sources** (`include/`, `src/`, `tests/test_node.cpp`):
  ```cpp
  // SPDX-License-Identifier: GPL-3.0-or-later
  // Copyright (C) 2026 KR4OYK - David Kuder
  ```
- **Portable C++98 / Windows RC files** (`ports/`, `tests/test_portable.cpp`):
  ```c
  /* SPDX-License-Identifier: GPL-3.0-or-later */
  /* Copyright (C) 2026 KR4OYK - David Kuder */
  ```
- **Scripts, CMake files, Makefiles, and `.cfg` files** (`CMakeLists.txt`, `makefile*`, `*.sh`, `configs/*.cfg`):
  ```sh
  # SPDX-License-Identifier: GPL-3.0-or-later
  # Copyright (C) 2026 KR4OYK - David Kuder
  ```
  *(For shell scripts with a shebang such as `#!/usr/bin/env bash`, keep the shebang on line 1 and place the SPDX and copyright lines immediately below it.)*
- **Markdown files** (`*.md`):
  ```markdown
  <!-- SPDX-License-Identifier: GPL-3.0-or-later -->
  <!-- Copyright (C) 2026 KR4OYK - David Kuder -->
  ```
- **Verbatim third-party documents** (`LICENSE`, `docs/specs/rfc*.txt`): Do NOT modify or prepend headers to verbatim external specification or license texts.

## 2. Language Convention

- Use **US English** spelling and phrasing consistently in all code identifiers, comments, documentation, and Git commit messages (for example: `color`, `behavior`, `initialize`, `license`, `center`).

## 3. Forking & Attribution Rules

When working in a **fork** of this repository:

1. **Preserve all attribution and licensing files**: Keep `LICENSE`, `AUTHORS`, and every existing `SPDX-License-Identifier` and `Copyright` line intact across all files.
2. **Append, never replace**: Add the fork owner's / user's copyright line **below** the existing copyright line(s) from this repository — **never** in place of them.
3. **Notify the user explicitly**: Tell the user when their copyright line has been added below the upstream copyright line(s) and that upstream attribution (`LICENSE`, `AUTHORS`, SPDX, and original copyright notices) has been preserved.

Example in a forked C++ file:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KR4OYK - David Kuder
// Copyright (C) 2026 <Fork Author Name>
```

## 4. Technical Invariants

- **Build & verify**: Run `cmake --build build -j && ctest --test-dir build --output-on-failure` after changes.
- **Portable core (`ports/compat/`)**: Must remain strict C++98 with zero STL, zero exceptions, zero RTTI, and 16-bit-safe integer widths (`sizeof(int) == 2`).
- **Config line endings**: Config I/O must accept `CRLF`, `CR`, and `LF`, write `CRLF` by default on MS-DOS/Windows and `LF` on POSIX, preserve original line endings on write-back, and let `node.cfg`'s first line ending override mixed line endings without changing AX.25/KISS/Telnet wire protocols.
- **Opt-in ports**: Keep `BUILD_MSDOS_PORT`, `BUILD_WIN16_PORT`, `BUILD_WINDOWS_PORT`, `BUILD_TINYTMUX_PORT`, and `BUILD_PICO_PORT` `OFF` by default; `oyk-node-gtk` builds automatically when `libgtk2.0-dev` is installed.

## 5. Cross-Port Feature Parity & UI Consistency

- **Feature parity**: Whenever possible, maintain feature parity across all ports (POSIX ncurses, X11 GTK+ 2.0, Windows 16/32/64-bit, MS-DOS, Teensy 4.1 / TinyTMUX, and future ports). New functionality, configuration options, and operator workflows added to one port should be implemented across the other ports in the same change unless prevented by target hardware or OS constraints.
- **Consistent UI & dialog layouts**: Maintain a consistent structure, navigation flow, logical grouping (such as tabs and menu organization), and control layout across all ports for every screen and dialog created.
- **Permitted platform exceptions**:
  - **File dialogs**: Windows and GTK builds may use their respective standard/common OS file selection dialogs, while text-mode builds (ncurses, MS-DOS, TinyTMUX VT100) use the built-in single-pane Norton/Midnight Commander-style file selector.
  - **Mobile platforms**: Future Android or iOS ports may adapt layouts and navigation patterns where required to comply with Android (Material) or iOS (Human Interface) design guidelines.

## 6. UI Gallery Maintenance

- **Keep screenshots synchronized with UI tests**: Whenever UI flow unit tests, dialogs, wizards, or operator screens are added or modified across any port (`tests/test_ncurses_ui.py`, `tests/test_portable.cpp`, `tests/test_tinytmux.cpp`, `src/ui/gtk2_ui.cpp`, or `ports/windows/win_main.cpp`), update the corresponding UI capture hooks and regenerate `docs/user/ui-gallery.md` and `docs/user/images/ui-gallery/*.png` before committing.
- **Regeneration command**: Run `./scripts/generate_ui_gallery.sh` (using `Xvfb` for X11 GTK+ 2.0 and Wine for Windows captures) and visually inspect representative PNG outputs across all five ports.
