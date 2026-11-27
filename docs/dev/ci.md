<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# Continuous Integration & Verification Checks

**oyk-node** uses a single local CI script ([scripts/ci.sh](../../scripts/ci.sh)) so contributors run the exact same verification pipeline locally that runs on GitHub Actions ([.github/workflows/ci.yml](../../.github/workflows/ci.yml)).

---

## Quick Gate (`scripts/ci.sh --quick`)

```bash
./scripts/ci.sh --quick
```

Runs all seven mandatory host verification stages cleanly in isolated temporary build directories:

| Step | Command / Mechanism | What It Verifies |
| --- | --- | --- |
| `spdx` | `python3 scripts/check_spdx.py` | Every tracked text file carries `SPDX-License-Identifier` and `Copyright (C)` (binary/third-party assets covered by [REUSE.toml](../../REUSE.toml)) |
| `docs` | `python3 scripts/check_docs.py` | All relative Markdown links resolve cleanly and every port in `ports/` is documented in `README.md` and `feature-matrix.md` |
| `secrets` | `python3 scripts/check_secrets.py` | Zero API keys, tokens, private keys, or personal home paths (`--all` when `CI=true`, pending changes locally, `--staged` pre-commit, `--unpushed` pre-push) |
| `stack` | `python3 scripts/check_stack.py --limit 1024` | Compiles `ports/compat/*.cpp` with `-std=c++98 -Os -fstack-usage` and fails if any function exceeds **1,024 bytes** of stack |
| `parity` | `python3 scripts/check_port_parity.py` | Verifies UI gallery PNG + thumbnail completeness across all 15 capture sets × 22 screens, graphical controls, and shared cross-port unit-test parity |
| `host` | `cmake ... -DOYK_WERROR=ON && ctest` | Enforces zero warnings (`-Wall -Wextra -Wpedantic -Werror`) across all host binaries & passes all **12 CTest suites** |
| `sanitizers` | `cmake ... -DOYK_SANITIZE=ON -DOYK_WERROR=ON` | Runs all **12 CTest suites** under **ASan + UBSan + LSan** (using [tests/lsan_suppressions.txt](../../tests/lsan_suppressions.txt) only for system `libncurses`) |

---

## Targeted Steps & Optional Emulators

Run specific steps or adjust parallelism:

```bash
# Run only static checks (takes ~2 seconds)
./scripts/ci.sh --steps spdx,docs,secrets,stack

# Run all 6 quick steps + available retro emulators (GSplus, FS-UAE, Hatari, Mini vMac)
./scripts/ci.sh --steps spdx,docs,secrets,stack,host,sanitizers,emulators --jobs 8
```

---

## Seeded Mutation Fuzzing (`oyk_fuzz_tests`)

[tests/test_fuzz.cpp](../../tests/test_fuzz.cpp) exercises 7 untrusted wire and config parsers (both C++20 and strict C++98 portable code paths):

1. AX.25 frame decode & CRC-16 FCS
2. KISS framing decoder
3. Config parser (`node.cfg` / `peers.cfg` with mixed `CRLF`/`CR`/`LF`) + callsign/peer parsers
4. Banlist loader, flags/lift parsers, & appeal sanitizer
5. YAPP / YAPP-C state machine (`oyk_yapp_feed_bytes`)
6. Local terminal EOL / word-wrap / `Ctrl+A` escape command parser
7. PPP / Async HDLC / LCP / IPCP frame decoder (`oyk_ppp`)

Customize iteration count and PRNG seed for deeper stress runs:

```bash
OYK_FUZZ_ITERS=100000 OYK_FUZZ_SEED=0xDEADBEEF ./build/oyk_fuzz_tests
```

---

## Pre-Commit & Pre-Push Workflow

```bash
# Before git commit:
python3 scripts/check_secrets.py --staged
./scripts/ci.sh --quick

# Before git push:
python3 scripts/check_secrets.py --unpushed
```

See [UI Gallery](../../scripts/generate_ui_gallery.sh) and [scripts/check_tui_parity.py](../../scripts/check_tui_parity.py) for UI screenshot regeneration and TUI structural parity validation.
