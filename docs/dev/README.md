<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Developer Documentation

Guides and references for contributors working on **oyk-node**:

- [Architecture Overview](architecture.md) — Layered design, threading and lock discipline, `NodeSnapshot` UI boundary, and shared `ports/compat/` GUI/TUI middleware.
- [Build Configuration Reference](build-config.md) — All CMake switches (`OYK_WERROR`, `OYK_SANITIZE`, `BUILD_*_PORT`), Makefiles, and cross-tool toolchains.
- [Implementing Radio Interfaces](radio-interfaces.md) — Subclassing `RadioInterface`, registering drivers with `InterfaceRegistry`, and wire framing summaries.
- [Porting & Testing Guide](porting-and-testing.md) — Portable C++98 core (`ports/compat/`), CMake targets, all 11 host CTest suites, and emulator runners.
- [Continuous Integration & Verification](ci.md) — Unified `./scripts/ci.sh` pipeline (`spdx`, `docs`, `secrets`, `stack`, `host`, `sanitizers`) and mutation fuzzing.
- [AI Use Disclosure](ai-disclosure.md) — Transparency statement and principles for AI-assisted contributions.
- [Agent & Contributor Guidelines](../../AGENTS.md) — Mandatory SPDX/copyright headers, US English rules, and forking attribution requirements.
- [Project Specification & Standards](../specs/README.md) — Consolidated specification and bundled IETF RFCs.
