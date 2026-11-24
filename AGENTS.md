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
- **Opt-in ports**: Keep `BUILD_MSDOS_PORT`, `BUILD_WIN16_PORT`, and `BUILD_WINDOWS_PORT` `OFF` by default; `oyk-node-gtk` builds automatically when `libgtk2.0-dev` is installed.
