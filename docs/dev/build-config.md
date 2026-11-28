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
| `BUILD_TESTS` | `ON` | Host | Build all 14 CTest unit, UI, bridge, parity, policy, and fuzz targets |
| `OYK_WERROR` | `OFF` | Native host targets | Add `-Werror` (`-Wall -Wextra -Wpedantic -Werror`) to host binaries & tests |
| `OYK_SANITIZE` | `OFF` | Native host targets | Enable `-fsanitize=address,undefined -fno-omit-frame-pointer` (ASan + UBSan + LSan) |
| `BUILD_MSDOS_PORT` | `OFF` | MS-DOS 16-bit | Enable `oyk-node-dos`, `oyk-dos-tests-build`, and optional QEMU `oyk_dos_qemu_tests` |
| `BUILD_MSDOS_GFX_PORT` | `OFF` | MS-DOS 386+ GFX | Enable `oyk-node-dos-gfx` (`oyknodeg.exe`, XMS/EMS4 + VBE/VGA/EGA) |
| `BUILD_WIN16_PORT` | `OFF` | Windows 3.1 16-bit | Enable `oyk-node-win16` (`oykw16.exe` via OpenWatcom `wmake`) |
| `BUILD_WINDOWS_PORT` | `OFF` | Windows 32/64-bit | Enable `oykw32.exe` / `oykw64.exe` (MinGW-w64 cross-compile or MSVC native) |
| `BUILD_TINYTMUX_PORT` | `OFF` | Teensy 4.1 | Enable IMXRT1060 Teensyduino sketch build via `arduino-builder` |
| `BUILD_PICO_PORT` | `OFF` | Raspberry Pi Pico | Enable `oyk-pico` UF2 builds (`pico_w`, `pico2_w`, `waveshare_core2350b`, `pimoroni_pga2350`, `picocalc_*`) |
| `BUILD_LYRA_PORT` | `OFF` | Luckfox Lyra | Enable `oyk_lyra_arm_verify` (`arm-none-eabi` Cortex-A7) and optional `oyk_lyra_rtthread` BSP target |
| `BUILD_ESP32C6_PORT` | `OFF` | ESP32-C6 (Unit C6L) | Enable `oyk-esp32c6` firmware build via ESP-IDF v5.x (`idf.py build`) |
| `BUILD_MACCLASSIC_PORT` | `OFF` | Classic Macintosh | Enable 68k + PPC Fat app build via Retro68 (and Mini vMac CTest runner) |
| `BUILD_GSOS_PORT` | `OFF` | Apple IIGS GS/OS | Enable `$B3` OMF v2 S16 build via `vbcc` `vc +iigs` (and GSplus CTest runner) |
| `BUILD_AMIGA_PORT` | `OFF` | Amiga Workbench | Enable OS 3.1/2.04/1.3 `m68k` build via `amiga-gcc` (and FS-UAE CTest runner) |
| `BUILD_ATARIST_PORT` | `OFF` | Atari ST / TT GEM | Enable `OYKNODE.PRG` / `OYKTEST.PRG` via `m68k-atari-mintelf-g++` (and Hatari CTest runner) |
| `BUILD_M88K_PORT` | `OFF` | Motorola 88K SVR4 | Enable `oyk_m88k_tests` host verification (`curses` + OSF/Motif 1.2 `sysv` core) |
| `BUILD_ATT3B2_PORT` | `OFF` | AT&T 3B2 SVR3.2 | Enable `oyk_att3b2_tests` host verification (`curses`/VT100 + WIN/TCP `sysv` core) |
| `BUILD_TI99_PORT` | `OFF` | TI-99/4A | Enable `oyk_ti99_tests` host verification (SAMS 1 MB, TIPI, F18A/40-col, RS-232 KISS) |
| `BUILD_OPENVMS_PORT` | `OFF` | OpenVMS (`VAX`/`Alpha`/`IA64`/`x86-64`) | Enable `oyk_openvms_console_host`, `oyk_openvms_native_test`, and `oyk_openvms_tests` |
| `BUILD_OS2_PORT` | `OFF` | OS/2 2.0 / Warp 3.0 / 4.0 | Enable `oyk_os2_console_host` (`VIO`) and `oyk_os2_tests` host verification |
| `BUILD_MACOS_PORT` | `ON` on Apple, `OFF` elsewhere | macOS AppKit | Native `.app` bundle (Universal `arm64`/`x86_64`, or C++98 PowerPC) |

### Additional CMake Variables

| Variable | Target | Purpose |
| --- | --- | --- |
| `MTCP_DIR` | MS-DOS | Optional path to Michael Brutman's mTCP source tree (`~/mTCP`) |
| `FREEDOS_BOOT_IMG` | MS-DOS QEMU | Boot floppy/disk image for `oyk_dos_qemu_tests` (defaults to bundled image if present) |
| `ARDUINO_DIR` | TinyTMUX | Arduino 1.8.19 + Teensyduino 1.62 installation directory |
| `PICO_SDK_PATH` | Pico | Pico SDK checkout directory (auto-discovers `~/.pico-sdk` or `~/pico-sdk`) |
| `RTTHREAD_ROOT` / `RK3506_BSP_ROOT` | Luckfox Lyra | Paths to RT-Thread source tree and Rockchip RK3506 BSP (`SConstruct`) |
| `IDF_PATH` | ESP32-C6 | ESP-IDF v5.x checkout directory (auto-discovers `~/opt/esp-idf`) |
| `OYK_MACOS_PORTABLE_CORE` | macOS (`ppc`) | Use `OykPortableNode` (C++98) instead of C++20 `NodeEngine` on legacy OS X 10.4–10.6 |

---

## Port-Specific Build Commands & Variables

| Port | Native Command | Key Variables / Options |
| --- | --- | --- |
| **MS-DOS Text** | `wmake -C ports/dos -f makefile` | `USE_MTCP=0\|1`, `MTCP_DIR=...`, `USE_DOSUSB=0\|1` ([README](../../ports/dos/README.md)) |
| **MS-DOS Graphical** | `wmake -C ports/dos -f makefile.gfx` | `USE_MTCP=0\|1`, 386+ DOS/4G or real/XMS/EMS4 ([README](../../ports/dos/README.md)) |
| **Windows 16/32/64** | `wmake -C ports/windows -f makefile.win16` / `.win32` | `WATCOM_DIR=...`; or MinGW-w64 via CMake ([README](../../ports/windows/README.md)) |
| **Teensy 4.1 TinyTMUX** | `cmake -S ports/tinytmux -B build-t41 && cmake --build build-t41` | `ARDUINO_DIR=...`, `OYK_TINYTMUX_FETCH_QNETHERNET=ON\|OFF` ([README](../../ports/tinytmux/README.md)) |
| **Raspberry Pi Pico** | `cmake -S ports/pico -B build-pico -DPICO_BOARD=<board>` | `PICO_BOARD` (`pico_w`, `pico2_w`, `waveshare_core2350b`, `pimoroni_pga2350`, `picocalc_pico2_w`, `picocalc_pico_w`, `picocalc_pico2`, `picocalc_pico`) ([README](../../ports/pico/README.md)) |
| **Luckfox Lyra** | `cmake -S . -B build-lyra -DBUILD_LYRA_PORT=ON` | `oyk_lyra_arm_verify` (Cortex-A7) & `oyk_lyra_rtthread` (`scons`, `RK3506_BSP_ROOT`, `Kconfig` profiles `lyra*` / `picocalc_lyra*`) ([README](../../ports/lyra/README.md)) |
| **ESP32-C6 Unit C6L** | `(cd ports/esp32c6 && idf.py set-target esp32c6 && idf.py build)` | Requires ESP-IDF v5.x (`IDF_PATH` / `IDF_TOOLS_PATH`) ([README](../../ports/esp32c6/README.md)) |
| **Android** | `(cd ports/android && ./gradlew testDebugUnitTest)` | `ANDROID_SDK_ROOT` / `ANDROID_HOME` |
| **Classic Macintosh** | `make -C ports/macclassic` | `RETRO68_PREFIX` (68k + PPC Fat binary + Mini vMac runner) ([README](../../ports/macclassic/README.md)) |
| **Apple IIGS GS/OS** | `make -C ports/gsos` | `VBCC` (`vc +iigs`, `-maxsegsize=16384` OMF v2 S16) ([README](../../ports/gsos/README.md)) |
| **Amiga Workbench** | `make -C ports/amiga` | `AMIGA_GCC` (`m68k-amigaos-g++ -noixemul`) ([README](../../ports/amiga/README.md)) |
| **Atari ST GEM** | `make -C ports/atarist` | `MINTELF` (`m68k-atari-mintelf-g++` + GEMlib) ([README](../../ports/atarist/README.md)) |
| **Motorola 88K SVR4** | `(cd ports/m88k && make)` | Native `/usr/ccs/bin/cc` C89 + `libcurses` / OSF/Motif 1.2 `libXm` + `-lgen` (`ports/m88k/run_m88k_remote.sh`) ([README](../../ports/m88k/README.md)) |
| **AT&T 3B2 SVR3.2** | `make -C ports/att3b2` | Native `gcc 2.3.3` C89 + `libcurses.a` / `libnet.a` (`ports/att3b2/run_3b2_remote.sh`) ([README](../../ports/att3b2/README.md)) |
| **TI-99/4A** | `make -C ports/ti99` | Host strict-C99 verification + `tms9900-gcc` / `xdt99` FinalGROM99 cart ([README](../../ports/ti99/README.md)) |
| **OpenVMS** | `@BUILD_VMS.COM` / `MMS` | Multi-arch `VAX`/`Alpha`/`IA64`/`x86_64` OPTs (`SMG$` console + DECwindows Motif 1.2) ([README](../../ports/openvms/README.md)) |
| **OS/2 2.0 / Warp 3 / 4** | `wmake -f ports/os2/makefile.os2` | OpenWatcom `wpp386 -bt=os2`, `oykos2.exe` (`os2v2`) + `oykos2pm.exe` (`os2v2_pm` + `wrc`) ([README](../../ports/os2/README.md)) |
| **macOS** | `cmake -B build-mac -DBUILD_MACOS_PORT=ON` | `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` or `OYK_MACOS_PORTABLE_CORE=ON` ([README](../../ports/macos/README.md)) |
