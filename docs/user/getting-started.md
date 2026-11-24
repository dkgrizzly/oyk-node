# Getting Started with oyk-node

**oyk-node** is a modular Amateur Radio AX.25 Packet Node supporting multiple radio/transport interfaces (`axudp`, `axip`, `kiss-serial`, `kiss-tcp`, and incoming `telnet`), configurable severity logging with daily rotation, persistent ban/appeal handling, and native operator interfaces across Linux, MS-DOS, and Windows.

## Requirements (Linux / POSIX)

- C++20 compiler (`g++` >= 11 or `clang++` >= 14)
- CMake >= 3.16
- `libncurses-dev` (for the terminal UI `oyk-node`)
- `libgtk2.0-dev` *(optional)* — if installed, CMake automatically builds the X11 GUI `oyk-node-gtk`

On Debian / Ubuntu:

```bash
sudo apt install build-essential cmake libncurses-dev
# Optional X11 GTK+ 2.0 GUI:
sudo apt install libgtk2.0-dev pkg-config
```

## Building

```bash
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

This produces:
- `build/oyk-node` — ncurses local text-mode operator console (also supports `--headless`)
- `build/oyk-node-gtk` — X11 GTK+ 2.0 GUI (automatically built when `libgtk2.0-dev` is present)

To build without ncurses:

```bash
cmake -B build -DBUILD_NCURSES_UI=OFF
cmake --build build -j
```

## Initial Configuration

Copy the example configuration files from `configs/` into one of the automatic search locations:

1. `~/.oyk/node.cfg` *(checked first)*
2. `/etc/oyk/node.cfg` *(checked second)*
3. `./node.cfg` *(checked last)*

```bash
mkdir -p ~/.oyk
cp configs/node.cfg ~/.oyk/node.cfg
cp configs/banlist.cfg ~/.oyk/banlist.cfg
```

Edit `~/.oyk/node.cfg` to set your licensed amateur radio `callsign`, `alias`, and radio interfaces.

## Running

```bash
# Auto-discovers ~/.oyk/node.cfg -> /etc/oyk/node.cfg -> ./node.cfg
./build/oyk-node

# Launch X11 GTK+ 2.0 GUI (if built)
./build/oyk-node-gtk

# Override config path or quick parameters from the CLI
./build/oyk-node -c /path/to/node.cfg
./build/oyk-node --callsign K7OYK-7 --alias OYKNOD --port 10093

# Headless / daemon mode (logs to stdout, clean exit on SIGINT/SIGTERM)
./build/oyk-node --headless
```

### Enabling AXIP (IP Protocol 93) Without Running as Root

Linux requires `CAP_NET_RAW` to open raw IP protocol 93 sockets:

```bash
sudo setcap cap_net_raw+ep ./build/oyk-node
```

## Optional Retro & Windows Builds

MS-DOS and Windows targets are disabled by default and only built when requested:
- **MS-DOS (IBM XT 8088 / NEC V20, 640KB + optional EMS 4.0, OpenWatcom + mTCP)**: See [ports/dos/README.md](../../ports/dos/README.md).
- **Windows (16-bit Win 3.1, 32-bit Win 95–ME & NT 3.51–11, 64-bit Win XP x64–11)**: See [ports/windows/README.md](../../ports/windows/README.md).
