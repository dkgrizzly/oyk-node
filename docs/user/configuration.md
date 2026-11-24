<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Configuration Reference

## Configuration File Discovery

When `-c` / `--config` is not passed, `oyk-node` checks for `node.cfg` in strict priority order:

1. `~/.oyk/node.cfg`
2. `/etc/oyk/node.cfg`
3. `./node.cfg`

If none exist, built-in defaults are used (`N0CALL-7`, alias `OYKNOD`, AXUDP on `0.0.0.0:10093`).

Starter templates are in `configs/node.cfg` and `configs/banlist.cfg`.

## Line-Ending Rules

- `CRLF`, `CR`, and `LF` line endings are accepted on all platforms when reading `node.cfg` and `banlist.cfg`.
- New config files default to `CRLF` on MS-DOS/Windows and `LF` on POSIX.
- Config files are written back to disk preserving the line-ending style they had when read.
- The **first line ending encountered in `node.cfg`** takes precedence and overrides any mixed line endings found in `node.cfg` or `banlist.cfg`.
- Config file line endings never alter AX.25, KISS, AXUDP, AXIP, or Telnet wire protocols.

---

## `node.cfg` Sections

### `[node]`

| Key | Default | Description |
| --- | --- | --- |
| `callsign` | `N0CALL-7` | Node AX.25 callsign with optional SSID (`0`–`15`). |
| `alias` | `OYKNOD` | Secondary node callsign/alias accepted on incoming connections. |
| `info` | `OYK C++ Modular Amateur Radio Packet Node` | Description returned by remote `INFO` command and beacons. |
| `accept_incoming` | `true` | Initial policy for accepting new incoming connections. |
| `paclen` | `236` | Max AX.25 `I`-frame payload size (`32`–`256` bytes). |
| `maxframe` | `4` | AX.25 modulo-8 transmit window size (`1`–`7`). |
| `t1_ms` | `3000` | AX.25 `T1` retransmission timer in milliseconds. |
| `n2_retries` | `5` | AX.25 `N2` retry limit before closing a link. |
| `idle_timeout_sec` | `600` | Idle disconnect timeout in seconds (`0` disables). |
| `beacon_interval_sec` | `0` | Periodic AX.25 `UI` ID beacon interval in seconds (`0` disables). |
| `logpath` | `logs/` | Directory where all `[logfiles]` are written (created if missing). |
| `consolelevel` | `0x77` | Severity bitmask shown in local console (all except `DEBUG` by default). |
| `log_rotate_daily` | `true` | Automatically rotate log files daily (`<file>.YYYYMMDD`). |
| `banlist` | `<cfg_dir>/banlist.cfg` | Optional path override for `banlist.cfg`. |

### Severity Bitmask Values

Used by `consolelevel` and `[logfiles]`. Accepts hex (`0x..`), binary (`0b..`), decimal, or pipe/comma-separated names (`ERROR|WARNING|INFO|KICK|BAN`):

| Hex | Decimal | Name | Description |
| --- | --- | --- | --- |
| `0x00` | `0` | `SILENT` | No messages |
| `0x01` | `1` | `ERROR` | Runtime or I/O errors |
| `0x02` | `2` | `WARNING` | Warnings (e.g. incoming denied) |
| `0x04` | `4` | `INFO` | Normal lifecycle events, connects, new callsigns |
| `0x10` | `16` | `KICK` | Operator-terminated connections |
| `0x20` | `32` | `BAN` | Ban/unban actions and restricted ban sessions |
| `0x40` | `64` | `APPEAL` | Submitted ban appeals |
| `0x80` | `128` | `DEBUG` | Verbose frame-level diagnostics |

### `[logfiles]`

Maps filenames stored under `logpath` to severity bitmasks:

```ini
[logfiles]
node.log = 0x37      # Default: ERROR | WARNING | INFO | KICK | BAN
appeals.log = 0x40   # Default: APPEAL (callsign, timestamp, <=140-char ASCII appeal)
# debug.log = 0x80   # Optional verbose debug log
```

Set a file to `0` (`SILENT`) to disable writing to it.

### `[interface <id>]`

Define one or more radio/transport ports, each with a unique `<id>`:

#### AXUDP (`type = axudp`)
```ini
[interface axudp0]
type = axudp
description = Primary AXUDP Link
bind_host = 0.0.0.0
bind_port = 10093
auto_learn = true
peer = W1AW-7@192.168.1.50:10093
peer = 127.0.0.1:10094
```

#### AXIP (`type = axip`, IP protocol 93 / RFC 1226)
```ini
[interface axip0]
type = axip
description = AXIP Backbone
bind_host = 0.0.0.0
auto_learn = true
peer = W1AW-7@198.51.100.10
```

#### KISS Serial (`type = kiss-serial`)
```ini
[interface kiss0]
type = kiss-serial
description = VHF 1200bps TNC
device = /dev/ttyUSB0
baud = 9600
kiss_port = 0
txdelay = 30
persistence = 63
slottime = 10
```

#### KISS TCP/IP (`type = kiss-tcp`)
```ini
[interface direwolf0]
type = kiss-tcp
description = Direwolf KISS TCP
mode = client          # "client" (default) or "server"
host = 127.0.0.1
port = 8001
kiss_port = 0
reconnect_ms = 3000
```

#### Incoming Telnet (`type = telnet`)
```ini
[interface telnet0]
type = telnet
description = Incoming Telnet Access
bind_host = 0.0.0.0
bind_port = 8010
max_clients = 32
```

---

## `banlist.cfg` Format

Stored in the same directory as `node.cfg` unless overridden by `banlist =` in `[node]`.

Each non-comment line has the form:

```ini
CALLSIGN[-SSID]=YYYYMMDD-HHMMSS,YYYYMMDD-HHMMSS|permanent,flags,reason
```

Example:

```ini
B4DCAL=20261102-130105,20271102-130105,0b0000,Banned for abusive language.
```

- **Key**: Base callsign (`B4DCAL`) matches all SSIDs (`0`–`15`); explicit SSID (`B4DCAL-7`) matches only that SSID.
- **Field 1**: Ban creation timestamp (`YYYYMMDD-HHMMSS`, local time).
- **Field 2**: Ban lift timestamp (`YYYYMMDD-HHMMSS`) or the keyword `permanent`. Expired entries are automatically treated as unbanned.
- **Field 3**: Bitfield accepting binary (`0b0000`), hex (`0x00`), or decimal without prefix:
  - Bit 0 (`0b0001` / `0x01`): Appeal allowed
  - Bit 1 (`0b0010` / `0x02`): User has been notified of the ban
  - Bit 2 (`0b0100` / `0x04`): User has already used their appeal
  - Bit 3 (`0b1000` / `0x08`): Hard ban (no further connections after initial notification)
- **Field 4**: Ban reason displayed to the user (commas inside the reason are preserved).
