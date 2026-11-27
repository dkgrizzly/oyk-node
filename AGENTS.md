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

- **Build & verify**: Run `cmake --build build -j && ctest --test-dir build --output-on-failure` (or `./scripts/ci.sh --quick` for SPDX, docs, secrets, `1,024 B` portable stack check, `-DOYK_WERROR=ON`, and `-DOYK_SANITIZE=ON` ASan/UBSan checks) after changes.
- **Portable core (`ports/compat/`)**: Must remain strict C++98 with zero STL, zero exceptions, zero RTTI, 16-bit-safe integer widths (`sizeof(int) == 2`), and every function's static stack frame within `<= 1,024` bytes (`python3 scripts/check_stack.py --limit 1024`).
- **Governance checks**: Keep `python3 scripts/check_spdx.py` and `python3 scripts/check_docs.py` passing cleanly whenever adding files or documentation.
- **Config line endings**: Config I/O must accept `CRLF`, `CR`, and `LF`, write `CRLF` by default on MS-DOS/Windows and `LF` on POSIX, preserve original line endings on write-back, and let `node.cfg`'s first line ending override mixed line endings without changing AX.25/KISS/Telnet wire protocols.
- **Opt-in ports**: Keep `BUILD_MSDOS_PORT`, `BUILD_MSDOS_GFX_PORT`, `BUILD_WIN16_PORT`, `BUILD_WINDOWS_PORT`, `BUILD_TINYTMUX_PORT`, `BUILD_PICO_PORT`, `BUILD_MACCLASSIC_PORT`, `BUILD_GSOS_PORT`, `BUILD_AMIGA_PORT`, and `BUILD_ATARIST_PORT` `OFF` by default (`BUILD_MACOS_PORT` defaults `OFF` outside Apple hosts and `ON` on `APPLE`); `oyk-node-gtk` builds automatically when `libgtk2.0-dev` is installed.
- **Shared GUI & TUI middleware**: Keep cross-port menu schemas, screen titles, table columns, wizard validation rules, exit prompts, and theme controllers in `ports/compat/oyk_gui.*` (`oyk_gui_theme.cpp` for portable builds) alongside `ports/compat/oyk_tui.*` and `ports/compat/oyk_term.*` so GTK+ 2.0, Windows, Classic Macintosh, macOS, Amiga Workbench, and Atari ST stay synchronized.

## 5. Cross-Port Feature Parity & UI Consistency

- **Feature parity**: Whenever possible, maintain feature parity across all ports (POSIX ncurses, X11 GTK+ 2.0, Windows 16/32/64-bit, MS-DOS, Teensy 4.1 / TinyTMUX, Raspberry Pi Pico, Classic Macintosh, Apple IIGS GS/OS, Amiga Workbench, Atari ST GEM, macOS, Android, and future ports). New functionality, configuration options, and operator workflows added to one port should be implemented across the other ports in the same change unless prevented by target hardware or OS constraints.
- **Family Lineage Rule**:
  - **TUI family** (`ncurses`, `dos`, `dos_gfx`, `tinytmux`, `pico`): Every TUI build MUST maintain visual, keyboard/mouse navigation, and unit-test parity with the TUI family via `ports/compat/oyk_tui.*`.
  - **GUI family** (`gtk`, `win`, `win16`, `macclassic`, `macos`, `gsos`, `amiga`, `atarist`): Every desktop and retro GUI build MUST maintain visual, interactive, and unit-test parity with the GUI family and the parent build from which it was derived via `ports/compat/oyk_gui.*` (or `gsos_core.*` for strict C89 65C816).
  - **Mobile platforms** (`android`, future `ios`): Mobile ports follow Android Material 3 or OS Human Interface Guidelines and are excluded from strict desktop visual layout parity, while maintaining functional and unit-test parity including the **Last 5 New Callsigns Heard (`MHEARD`)** console card and 7 Settings sections.
- **Consistent UI & dialog layouts**: Maintain a consistent structure, navigation flow, logical grouping (such as tabs and menu organization), and control layout across all ports for every screen and dialog created.
- **Canonical TUI Layout Contract**: Every text-mode or framebuffer-TUI port (`ncurses`, `dos`, `dos_gfx`, `tinytmux`, `pico`) MUST derive screen and popup geometry through the shared C++98 TUI middle layer (`ports/compat/oyk_tui.*`) and preserve these structural invariants:
  - **Dynamic Full-Display Fill**: Both Main Console and Local Terminal MUST detect and fill the entire available display grid (`getmaxyx`/`KEY_RESIZE` on `ncurses`, BIOS/BDA columns & rows on DOS text, mode dimensions `width/8 x height/char_h` on DOS-GFX, and ANSI CPR `\033[s\033[999;999H\033[6n\033[u` probe clamped to `[24..50] x [80..132]` with `80x25` fallback on TinyTMUX and Pico).
  - **Main Console**: Row 1 menu bar (` Node ` highlighted + `Tab/F9:Term` + `F10/M`), Row 2 status banner with `[INCOMING: ALLOW]`/`[INCOMING: DENY]` (or `[IN:ALLOW]`/`[IN:DENY]`), side-by-side `Interfaces` and `Last 5 New Callsigns Heard`, `Active Connections`, `Node Activity Log`, and bottom status bar.
  - **Drop-Down `Node Menu`**: Floating box anchored below Row 1 with horizontal dividers, `[Key]` hotkeys, arrow/hotkey/mouse navigation, L-shaped drop shadow, and NO `[X]` close button.
  - **Floating Modal Rule**: `Node Settings`, `Theme Settings`, `Add Interface`, `Edit Interface`, `Edit Peer`, `Terminate Active Connection`, `Ban / Unban Callsign`, `Auto-Learned Peers` exit/bootloader prompt, and built-in `File Selector` MUST render as centered floating framed dialogs over the live main console with L-shaped drop shadows (never full-screen clears) AND a clickable `[X]` close button in the top-right title border (`rect.left + rect.w - 5`).
  - **Settings Modal**: Box titled `Node Settings` containing all 7 canonical tabs (`1:Node`, `2:BBS`, `3:CHAT`, `4:Ifaces`, `5:Peers`, `6:Rig`, `7:Prn`) inside the top row of the modal, a horizontal divider below the tabs, body rows, feedback line, horizontal divider, and a single bottom keybar inside the modal.
  - **Terminate Connection Modal**: Box titled `Terminate Active Connection` with styled table header `ID    Callsign    Port       State`, columnar session rows, and a horizontal divider above the bottom keybar.
  - **Theme Settings Modal**: Box titled `Theme Settings` with `< theme >` selector, `Role / Key / Color / Preview` table with distinct per-row preview swatches, single live-preview feedback line, horizontal divider, and single bottom keybar (no duplicated hint lines).
- **Canonical GUI Layout Contract**: Every desktop and retro GUI port MUST implement all 12 canonical screens (`main`, `settings`, `add_iface`, `edit_iface`, `peers`, `edit_peer`, `terminate`, `ban`, `exit`, `theme`, `terminal`, `files`) with interactive mouse hit-testing and keyboard navigation:
  - **Native Host Window Frames & Controls**: GUI ports MUST render main and modal windows with authentic OS/toolkit non-client window frames (titlebars, borders, close buttons) wherever supported by the host: X11 window manager (`xfwm4`) for GTK+ 2.0, Win32 virtual desktop frames for Windows/Wine, QuickDraw `NewWindow`/`NewCWindow` (`noGrowDocProc` positioned at `top >= 42` below the menu bar) on Classic Macintosh, Intuition `WFLG_DRAGBAR | WFLG_CLOSEGADGET` windows (never `WFLG_BORDERLESS | WFLG_BACKDROP`) on Amiga Workbench, GEM AES `wind_create`/`wind_open` (`NAME | CLOSER | MOVER`) windows on Atari ST/TT, and a framed GS/OS Platinum desktop window on Apple IIGS.
  - **Desktop-Framed Modal Captures**: GTK+ 2.0 (`gdk_window_get_frame_extents` + `gdk_get_default_root_window`) and Windows/Wine (`UnionRect` + `GetDC(NULL)`) gallery captures MUST capture the root/desktop union of the framed main console window and centered framed modal dialog so the parent window stays visible behind dialogs.
  - **Native or Rich Graphical Controls**: GUI ports MUST use native host radio and checkbox widgets where provided (`GtkCheckButton`/`GtkRadioButton`, Win32 `BS_AUTOCHECKBOX`/`BS_AUTORADIOBUTTON`, Material `Checkbox`/`RadioButton`). Where a retro toolkit lacks built-in vector controls (`macclassic`, `gsos`, `amiga`, `atarist`), ports MUST draw bespoke graphical checkboxes (framed box + vector checkmark) and circular/octagonal radio indicators (ring + filled center dot). Text mimics (`[x]`, `[*]`, `[ ]`, `(x)`, `(*)`, `( )`, or `[X]` outside title-bar close buttons) are FORBIDDEN in GUI ports.
  - **Main Console & Floating Modals**: Top OS menu bar, `OYK PACKET NODE: <call> (<alias>) | Uptime: ... | Active Connections: <n>` header with distinct `[INCOMING: ALLOW]`/`[INCOMING: DENY]` badge, high-contrast column headers in `Interfaces`, `Last 5 New Callsigns Heard`, and `Active Connections`, `Node Activity Log`, quick-action footer, and centered floating framed dialogs with top-right `[X]` close box and right-aligned action buttons.
  - **Canonical Dialog Bodies**: `Node Settings` renders all 7 canonical tab bodies (`1:Node`, `2:BBS`, `3:CHAT`, `4:Ifaces`, `5:Peers`, `6:Rig`, `7:Prn`); `Add Interface` shows 5 graphical radio drivers, driver descriptions, and Serial (`Device`/`Baud`) vs Network (`Bind Host`/`Port`) fields; `Edit Interface`, `Edit Peer`, and `Ban` use framed input fields + graphical checkboxes; `Auto-Learned Peers` exit prompt provides all 4 buttons (`Save & Exit`, `Discard`, `Review Peers`, `Cancel`); `Theme Settings` provides `<`/`>` cycle buttons, accurate per-role swatches, and whole-screen live preview; `Terminal` provides a `Ctrl+A` hotkey bar + viewport + `Input:` field + `[ Send ]`; `Files` provides dual panes with column headers.
- **Canonical Unit-Test Parity Contract**: Every C++ port test suite MUST invoke `oyk_run_shared_port_parity_tests` (`ports/compat/oyk_port_parity_tests.hpp`), and strict C89 ports (`gsos`) MUST execute equivalent C89 parity checks (`ports/gsos/test_gsos.c`, `tests/test_gsos.cpp`). Compliance is enforced by `python3 scripts/check_port_parity.py` during `./scripts/ci.sh --quick`.
- **Permitted platform exceptions**:
  - **File dialogs**: Windows and GTK builds may use their respective standard/common OS file selection dialogs, while text-mode builds (ncurses, MS-DOS, MS-DOS Graphical, TinyTMUX VT100, Pico) use the built-in single-pane Norton/Midnight Commander-style file selector.
  - **Mobile platforms**: Android and future iOS ports may adapt layouts and navigation patterns where required to comply with Android Material 3 or Apple Human Interface Guidelines, while preserving MHEARD display and functional parity.

## 6. UI Gallery Maintenance

- **Keep screenshots synchronized with UI tests**: Whenever UI flow unit tests, dialogs, wizards, or operator screens are added or modified across any port (`tests/test_ncurses_ui.py`, `tests/test_portable.cpp`, `tests/test_tinytmux.cpp`, `tests/test_pico.cpp`, `src/ui/gtk2_ui.cpp`, `ports/windows/win_main.cpp`, `ports/macclassic/mac_ui.cpp`, `ports/macos/macos_main.mm`, `ports/gsos/gsos_ui.c`, `ports/amiga/amiga_ui.cpp`, `ports/atarist/atari_ui.cpp`, or `ports/android/app/src/test/java/com/kr4oyk/oyknode/OykUiGalleryTest.kt`), update the corresponding UI capture hooks and regenerate `docs/user/ui-gallery.md` and `docs/user/images/ui-gallery/*.png` before committing.
- **Regeneration & parity check command**: Run `./scripts/generate_ui_gallery.sh` (which runs `scripts/check_tui_parity.py` across all TUI captures, `Xvfb` for GTK+ 2.0, Wine for Windows, Mini vMac for Classic Mac, GSplus for GS/OS, FS-UAE for Amiga, Hatari for Atari ST, and Robolectric for Android) and visually inspect representative PNG outputs and per-port contact sheets side-by-side before committing.

## 7. Secrets, Credentials & Privileged Information

- **Never commit secrets or credentials**: Do NOT commit API keys, personal access tokens, OAuth/JWT secrets, SSH/PGP/TLS private keys, Android `.jks`/`.keystore` signing files, Wi-Fi passwords/PSKs, `.env` files, `local.properties`, `google-services.json`, or live operator configs (`node.cfg`, `peers.cfg`, `banlist.cfg`) containing real passwords or private infrastructure endpoints.
- **No hardcoded local paths or identities**: Never hardcode personal machine paths (`/home/<user>/`, `/Users/<user>/`, `C:\Users\<user>\`) in source code, unit tests, configs, or scripts; use relative paths, environment variables, or CMake variables instead.
- **Runtime secret injection**: Load signing keys, passwords, and API credentials exclusively from environment variables (e.g., `OYK_ANDROID_KEYSTORE_PASS`, `OYK_ANDROID_KEY_PASS`) or gitignored local files.
- **Mandatory pre-commit & pre-push scan**: Run `python3 scripts/check_secrets.py` (pending changes) or `python3 scripts/check_secrets.py --staged` (staged changes only) before every commit, and `python3 scripts/check_secrets.py --unpushed` before pushing or tagging a release. Optional full-repo audits remain available via `--all` (all tracked files) and `--history` (entire git history). If any secret or privileged credential is ever found in the working tree or commits, stop immediately and alert the user so the credential can be rotated and scrubbed.
