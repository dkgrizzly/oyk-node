<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Windows Native GUI Ports (16-bit, 32-bit, and 64-bit)

A single native WinAPI codebase (`win_main.cpp` + `oyk_win.rc`) built on `ports/compat/oyk_portable.{hpp,cpp}` and **Winsock 1.1** (`WINSOCK.DLL` / `wsock32.lib`), providing the same operator layout as the Linux ncurses and GTK+ 2.0 interfaces across:

- **16-bit Windows 3.1**: Built with OpenWatcom v2 / 1.9 (`makefile.win16` -> `oykw16.exe`).
- **32-bit Windows 95, 98, ME & Windows NT 3.51 – Windows 11**: Built with OpenWatcom v2 / 1.9 (`makefile.win32` -> `oykw32.exe`), MinGW-w64 (`i686-w64-mingw32`), or MSVC `Win32`.
- **64-bit Windows XP x64 – Windows 11**: Built with MinGW-w64 (`x86_64-w64-mingw32`) or MSVC `x64`.

## Building

### Windows 3.1 (16-bit, OpenWatcom v2 / 1.9)

Via CMake on Linux (auto-detects OpenWatcom in `WATCOM`, `/opt/watcom`, or `~/open-watcom-v2/rel`):
```bash
cmake -B build -DBUILD_WIN16_PORT=ON
cmake --build build --target oyk-node-win16   # produces ports/windows/oykw16.exe
```

Or directly with `wmake`:
```bash
export WATCOM=/opt/watcom
export PATH=$WATCOM/binl64:$PATH
cd ports/windows
wmake -f makefile.win16
```

### Windows 95..ME / NT 3.51..11 (32-bit, OpenWatcom v2 / 1.9)
```bash
export WATCOM=/opt/watcom
export PATH=$WATCOM/binl64:$PATH
cd ports/windows
wmake -f makefile.win32
```

### Windows 32-bit / 64-bit via CMake on Linux (MinGW-w64) or Windows (MSVC/MinGW)

On Linux with `mingw-w64` installed (`x86_64-w64-mingw32-g++` and/or `i686-w64-mingw32-g++`), CMake automatically detects both cross-compilers and produces standalone `build/oykw64.exe` and `build/oykw32.exe` executables when `BUILD_WINDOWS_PORT=ON`:

```bash
cmake -B build -DBUILD_WINDOWS_PORT=ON
cmake --build build --target oyk-node-win
```

Or build an individual target:
```bash
cmake --build build --target oyk-node-win64   # produces build/oykw64.exe
cmake --build build --target oyk-node-win32   # produces build/oykw32.exe
```
