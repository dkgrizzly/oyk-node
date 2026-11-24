# Windows Native GUI Ports (16-bit, 32-bit, and 64-bit)

A single native WinAPI codebase (`win_main.cpp` + `oyk_win.rc`) built on `ports/compat/oyk_portable.{hpp,cpp}` and **Winsock 1.1** (`WINSOCK.DLL` / `wsock32.lib`), providing the same operator layout as the Linux ncurses and GTK+ 2.0 interfaces across:

- **16-bit Windows 3.1**: Built with OpenWatcom 1.9 (`makefile.win16` -> `oykw16.exe`).
- **32-bit Windows 95, 98, ME & Windows NT 3.51 – Windows 11**: Built with OpenWatcom 1.9 (`makefile.win32` -> `oykw32.exe`), MinGW-w64 (`i686-w64-mingw32`), or MSVC `Win32`.
- **64-bit Windows XP x64 – Windows 11**: Built with MinGW-w64 (`x86_64-w64-mingw32`) or MSVC `x64`.

## Building

### Windows 3.1 (16-bit, OpenWatcom 1.9)
```bat
cd ports\windows
wmake -f makefile.win16
```

### Windows 95..ME / NT 3.51..11 (32-bit, OpenWatcom 1.9)
```bat
cd ports\windows
wmake -f makefile.win32
```

### Windows 32-bit / 64-bit via CMake (only built when requested)
```bash
cmake -B build-win -DBUILD_WINDOWS_PORT=ON -DBUILD_NCURSES_UI=OFF -DBUILD_GTK2_UI=OFF
cmake --build build-win --target oyk-node-win
```
