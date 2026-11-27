<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Build Configuration Reference

Authoritative reference for all CMake options, compiler hardening flags, port-specific Makefiles, and external cross-compilation toolchains in **oyk-node**.

---

## Root CMake Options

All opt-in retro, embedded, and Windows cross-targets default to `OFF` so standard Linux/POSIX builds require only a C++20 compiler, CMake, and `libncurses-dev` (`libgtk2.0-dev` optional).

| Option | Default | Scope | Description |
| --- | --- | --- | --- |
| `BUILD_NCURSES_UI` | `ON` | Host Linux / POSIX | Build `oyk-node` with ncurses TUI (`OFF` builds `--headless`-only binary) |
| `BUILD_GTK2_UI` | `ON` | Host Linux / X11 | Auto-build `oyk-node-gtk` when `pkg-config` finds `gtk+-2.0` |
| `BUILD_TESTS` | `ON` | Host | Build all 11 CTest unit, UI, bridge, policy, and fuzz targets |
| `OYK_WERROR` | `OFF` | Native host targets | Add `-Werror` (`-Wall -Wextra -Wpedantic -Werror`) to host binaries & tests |
| `OYK_SANITIZE` | `OFF` | Native host targets | Enable `-fsanitize=address,undefined -fno-omit-frame-pointer` (ASan + UBSan + LSan) |
| `BUILD_MSDOS_PORT` | `OFF` | MS-DOS 16-bit | Enable `oyk-node-dos`, `oyk-dos-tests-build`, and optional QEMU `oyk_dos_qemu_tests` |
| `BUILD_MSDOS_GFX_PORT` | `OFF` | MS-DOS 386+ GFX | Enable `oyk-node-dos-gfx` (`oyknodeg.exe`, XMS/EMS4 + VBE/VGA/EGA) |
| `BUILD_WIN16_PORT` | `OFF` | Windows 3.1 16-bit | Enable `oyk-node-win16` (`oykw16.exe` via OpenWatcom `wmake`) |
| `BUILD_WINDOWS_PORT` | `OFF` | Windows 32/64-bit | Enable `oykw32.exe` / `oykw64.exe` (MinGW-w64 cross-compile or MSVC native) |
| `BUILD_TINYTMUX_PORT` | `OFF` | Teensy 4.1 | Enable IMXRT1060 Teensyduino sketch build via `arduino-builder` |
| `BUILD_PICO_PORT` | `OFF` | Raspberry Pi Pico | Enable `oyk-pico` UF2 builds (`pico_w`, `pico2_w`, `waveshare_core2350b`, `pimoroni_pga2350`) |
| `BUILD_MACCLASSIC_PORT` | `OFF` | Classic Macintosh | Enable 68k + PPC Fat app build via Retro68 (and Mini vMac CTest runner) |
| `BUILD_GSOS_PORT` | `OFF` | Apple IIGS GS/OS | Enable `$B3` OMF v2 S16 build via `vbcc` `vc +iigs` (and GSplus CTest runner) |
| `BUILD_AMIGA_PORT` | `OFF` | Amiga Workbench | Enable OS 3.1/2.04/1.3 `m68k` build via `amiga-gcc` (and FS-UAE CTest runner) |
| `BUILD_ATARIST_PORT` | `OFF` | Atari ST / TT GEM | Enable `OYKNODE.PRG` / `OYKTEST.PRG` via `m68k-atari-mintelf-g++` (and Hatari CTest runner) |
| `BUILD_MACOS_PORT` | `ON` on Apple, `OFF` elsewhere | macOS AppKit | Native `.app` bundle (Universal `arm64`/`x86_64`, or C++98 PowerPC) |

### Additional CMake Variables

| Variable | Target | Purpose |
| --- | --- | --- |
| `MTCP_DIR` | MS-DOS | Optional path to Michael Brutman's mTCP source tree (`~/mTCP`) |
| `FREEDOS_BOOT_IMG` | MS-DOS QEMU | Boot floppy/disk image for `oyk_dos_qemu_tests` (defaults to bundled image if present) |
| `ARDUINO_DIR` | TinyTMUX | Arduino 1.8.19 + Teensyduino 1.62 installation directory |
| `PICO_SDK_PATH` | Pico | Pico SDK checkout directory (auto-discovers `~/.pico-sdk` or `~/pico-sdk`) |
| `OYK_MACOS_PORTABLE_CORE` | macOS (`ppc`) | Use `OykPortableNode` (C++98) instead of C++20 `NodeEngine` on legacy OS X 10.4–10.6 |

---

## Port-Specific Build Commands & Variables

| Port | Native Command | Key Variables / Options |
| --- | --- | --- |
| **MS-DOS Text** | `wmake -C ports/dos -f makefile` | `USE_MTCP=0\|1`, `MTCP_DIR=...`, `USE_DOSUSB=0\|1` ([README](../../ports/dos/README.md)) |
| **MS-DOS Graphical** | `wmake -C ports/dos -f makefile.gfx` | `USE_MTCP=0\|1`, 386+ DOS/4G or real/XMS/EMS4 ([README](../../ports/dos/README.md)) |
| **Windows 16/32/64** | `wmake -C ports/windows -f makefile.win16` / `.win32` | `WATCOM_DIR=...`; or MinGW-w64 via CMake ([README](../../ports/windows/README.md)) |
| **Teensy 4.1 TinyTMUX** | `cmake -S ports/tinytmux -B build-t41 && cmake --build build-t41` | `ARDUINO_DIR=...`, `OYK_TINYTMUX_FETCH_QNETHERNET=ON\|OFF` ([README](../../ports/tinytmux/README.md)) |
| **Raspberry Pi Pico** | `cmake -S ports/pico -B build-pico -DPICO_BOARD=<board>` | `PICO_BOARD` (`pico_w`, `pico2_w`, `waveshare_core2350b`, `pimoroni_pga2350`) ([README](../../ports/pico/README.md)) |
| **Android** | `(cd ports/android && ./gradlew testDebugUnitTest)` | `ANDROID_SDK_ROOT` / `ANDROID_HOME` |
| **Classic Macintosh** | `make -C ports/macclassic` | `RETRO68_PREFIX` (68k + PPC Fat binary + Mini vMac runner) ([README](../../ports/macclassic/README.md)) |
| **Apple IIGS GS/OS** | `make -C ports/gsos` | `VBCC` (`vc +iigs`, `-maxsegsize=16384` OMF v2 S16) ([README](../../ports/gsos/README.md)) |
| **Amiga Workbench** | `make -C ports/amiga` | `AMIGA_GCC` (`m68k-amigaos-g++ -noixemul`) ([README](../../ports/amiga/README.md)) |
| **Atari ST GEM** | `make -C ports/atarist` | `MINTELF` (`m68k-atari-mintelf-g++` + GEMlib) ([README](../../ports/atarist/README.md)) |
| **macOS** | `cmake -B build-mac -DBUILD_MACOS_PORT=ON` | `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` or `OYK_MACOS_PORTABLE_CORE=ON` ([README](../../ports/macos/README.md)) |
