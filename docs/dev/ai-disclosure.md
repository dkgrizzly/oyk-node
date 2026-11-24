<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# AI Use Disclosure

Portions of **oyk-node** — including source code, unit and integration tests, build scripts, and documentation — were authored or assisted using generative AI coding tools under the direction, architecture guidance, review, and verification of the project maintainer(s) listed in [AUTHORS](../../AUTHORS).

## Principles for AI-Assisted Contributions

- **Human Oversight**: All design decisions, protocol behavior, configuration formats, licensing choices, and final changes are directed and reviewed by a human maintainer.
- **Verification**: AI-assisted changes are expected to compile cleanly with strict warnings enabled (`-Wall -Wextra -Wpedantic`) and pass the automated C++20 and strict C++98 test suites (`ctest`).
- **Attribution & Licensing**: AI tools do not replace human copyright or open-source license obligations. All project files carry `GPL-3.0-or-later` SPDX headers and preserve upstream attribution per [AGENTS.md](../../AGENTS.md).
- **Standards Fidelity**: Referenced external protocol documents (such as IETF RFCs in `docs/specs/`) are verbatim originals from their authoritative publishers, not AI-generated paraphrases.
