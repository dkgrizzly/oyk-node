<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Developer Documentation

Guides and references for contributors working on **oyk-node**:

- [Architecture Overview](architecture.md) — Layered design, threading and lock discipline, `NodeSnapshot` UI boundary, and external BBS/Chat hooks.
- [Implementing Radio Interfaces](radio-interfaces.md) — Subclassing `RadioInterface`, registering drivers with `InterfaceRegistry`, and wire framing summaries.
- [Porting & Testing Guide](porting-and-testing.md) — Portable C++98 core (`ports/compat/`), CMake options, native CTest, MinGW-w64 Windows cross-builds, and headless QEMU MS-DOS testing.
- [AI Use Disclosure](ai-disclosure.md) — Transparency statement and principles for AI-assisted contributions.
- [Agent & Contributor Guidelines](../../AGENTS.md) — Mandatory SPDX/copyright headers, US English rules, and forking attribution requirements.
- [Project Specification & Standards](../specs/README.md) — Consolidated specification and bundled IETF RFCs.
