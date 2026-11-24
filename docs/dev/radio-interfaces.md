<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Implementing Radio Interfaces

All link-layer and transport drivers implement `oyk::radio::RadioInterface` ([radio_interface.hpp](../../include/oyk/radio/radio_interface.hpp)) and register a factory with `oyk::radio::InterfaceRegistry` ([interface_registry.hpp](../../include/oyk/radio/interface_registry.hpp)).

## Interface Contract

```cpp
class RadioInterface {
public:
    using FrameCallback = std::function<void(const ax25::Frame&, const RadioFrameMeta&)>;

    virtual std::string id() const = 0;
    virtual std::string type() const = 0;
    virtual std::string description() const = 0;
    virtual std::string status_summary() const = 0;

    virtual bool start(FrameCallback on_frame, std::string& error_out) = 0;
    virtual void stop() = 0;
    virtual bool is_running() const = 0;

    virtual bool send_frame(const ax25::Frame& frame,
                            const std::string& target_endpoint = "") = 0;
    virtual RadioInterfaceStats stats() const = 0;
};
```

## Adding a New Driver (e.g. AGWPE or Linux `AF_AX25`)

1. Create `include/oyk/radio/<driver>_interface.hpp` and `src/radio/<driver>_interface.cpp` subclassing `RadioInterface`.
2. Read driver-specific parameters from `RadioInterfaceConfig::params` and `RadioInterfaceConfig::peers`.
3. Use a wake pipe alongside `poll()` so `stop()` joins cleanly and immediately.
4. Register your factory in `src/radio/interface_registry.cpp`:
   ```cpp
   register_type("agwpe", [](const RadioInterfaceConfig& cfg) {
       return std::make_unique<AgwpeInterface>(cfg);
   });
   ```
5. Add the new `.cpp` file to `oyknode_core` in `CMakeLists.txt`.
6. Configure the new driver in `node.cfg` using an `[interface <id>]` section with `type = <your-type>`.

No changes are required in `NodeEngine`, `NcursesUI`, or `Gtk2UI` — active connection counts, MHEARD, last 5 new callsigns heard, ban enforcement, and cross-port `CONNECT` work automatically.

## Built-In Wire Framing Summary

| Type | Wire Format | FCS | Notes |
| --- | --- | --- | --- |
| `axudp` | Raw AX.25 frame + 2-byte LE CRC-16-CCITT inside UDP datagram | Yes | Static peers, broadcast peers, dynamic auto-learn |
| `axip` | Raw AX.25 frame + 2-byte LE CRC-16-CCITT inside IPv4 proto `93` (RFC 1226) | Yes | Strips IPv4 header + options on RX; requires `CAP_NET_RAW` |
| `kiss-serial` | `FEND (0xC0)` + `(port<<4)\|cmd` + escaped AX.25 + `FEND` over TTY/PTY | No (TNC) | Raw 8N1 `termios`, optional `txdelay`/`persistence`/`slottime`/`txtail`/`fullduplex` |
| `kiss-tcp` | Same KISS framing over TCP stream | No (TNC) | Supports `mode=client` (auto-reconnect) and `mode=server` |
| `telnet` | Incoming TCP with RFC 854/858 IAC handling & callsign prompt | N/A | Bridges into `NodeEngine` as synthetic AX.25 L2 frames; rejects outbound `SABM` with `DM` |
