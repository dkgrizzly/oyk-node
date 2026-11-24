<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Project Specifications & Referenced Standards

## Project Specifications

- [oyk-node-spec.md](oyk-node-spec.md) — Consolidated specification for `oyk-node` (interfaces, config & line-ending rules, severity logging, daily rotation, banlist/appeals, UI, and portability targets).

## Bundled Freely Redistributable Standards

Original plain-text IETF RFC documents (retained in their original `.txt` format):

- [rfc1226.txt](rfc1226.txt) — **RFC 1226**: *Internet Protocol Encapsulation of AX.25 Frames* (B. Kantor, May 1991) — IP protocol `93` (AXIP).
- [rfc854.txt](rfc854.txt) — **RFC 854**: *Telnet Protocol Specification* (J. Postel & J. Reynolds, May 1983) — Telnet NVT and IAC command framing.
- [rfc858.txt](rfc858.txt) — **RFC 858**: *Telnet Suppress Go Ahead Option* (J. Postel & J. Reynolds, May 1983) — Telnet SGA option (`0x03`).

## External Standards Referenced (Not Bundled Due to Copyright)

The following standards are referenced by the implementation but are not redistributed in this repository:

- **AX.25 Link Access Protocol for Amateur Packet Radio (v2.0 / v2.2)** — Tucson Amateur Packet Radio (TAPR): <https://www.tapr.org/pdf/AX25.2.2.pdf>
- **The KISS TNC: A Simple Host-to-TNC Communications Protocol** — M. Chepponis (K3MC) & P. Karn (KA9Q), ARRL 6th Computer Networking Conference (1987): <http://www.ax25.net/kiss.aspx>
- **LIM EMS 4.0** — Lotus / Intel / Microsoft Expanded Memory Specification Version 4.0 (1987), `INT 67h` functions `40h`–`46h`.
