#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
"""Scan tracked files or full git history for secrets, credentials, keys, and local machine paths."""

import argparse
import os
import re
import subprocess
import sys

SUSPICIOUS_PATH_RE = re.compile(
    r"(^|/)(\.env($|\.)|\.netrc$|\.npmrc$|\.pypirc$|id_rsa|id_ed25519|id_dsa|"
    r"local\.properties$|google-services\.json$|.*service[_-]?account.*\.json$|"
    r".*\.(pem|key|p12|pfx|jks|keystore|secret)$|secrets\.[^/]+$|.*credentials[^/]*$)",
    re.I,
)

SKIP_EXACT = {"LICENSE"}
SKIP_PREFIXES = ("docs/specs/rfc",)

PATTERNS = [
    ("Private Key Header", re.compile(r"-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----")),
    (
        "GitHub Token",
        re.compile(
            r"\b(ghp_[A-Za-z0-9]{20,}|gho_[A-Za-z0-9]{20,}|ghu_[A-Za-z0-9]{20,}|"
            r"ghs_[A-Za-z0-9]{20,}|ghr_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})\b"
        ),
    ),
    ("AWS Access Key", re.compile(r"\b(AKIA|ASIA|AIDA|AROA)[0-9A-Z]{16}\b")),
    ("Google/Firebase API Key", re.compile(r"\bAIza[0-9A-Za-z\-_]{35}\b")),
    ("Slack Token", re.compile(r"\bxox[baprs]-[0-9A-Za-z\-]{10,}\b")),
    (
        "OpenAI/Anthropic/Stripe Key",
        re.compile(r"\b(sk-[A-Za-z0-9\-_]{20,}|pk_live_[A-Za-z0-9]{20,}|rk_live_[A-Za-z0-9]{20,})\b"),
    ),
    (
        "JWT Token",
        re.compile(r"\beyJ[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,}\b"),
    ),
    ("Credentialed URL", re.compile(r"[a-zA-Z]+://[^/\s:@]+:[^/\s@]+@[^\s]+")),
    (
        "Authorization Header",
        re.compile(r"\bAuthorization\s*:\s*(Bearer|Basic)\s+[A-Za-z0-9+/=._\-]{8,}", re.I),
    ),
    (
        "Hardcoded Secret Assignment",
        re.compile(
            r"(?i)(api[_-]?key|api[_-]?secret|access[_-]?token|auth[_-]?token|client[_-]?secret|"
            r"private[_-]?key|keystore[_-]?pass|storePassword|keyPassword|wifi[_-]?(pass|psk)|"
            r"db[_-]?pass|secret[_-]?key|bearer[_-]?token)\s*[:=]\s*[\"']([^\s\"']{4,})[\"']"
        ),
    ),
    (
        "Hardcoded Local Home Path",
        re.compile(r"(/home/[a-zA-Z0-9._-]+/|/Users/[a-zA-Z0-9._-]+/|C:\\Users\\[a-zA-Z0-9._-]+\\)"),
    ),
]


def should_skip_path(path: str) -> bool:
    if path in SUSPICIOUS_EXEMPT:
        return True
    if path in SKIP_EXACT:
        return True
    return any(path.startswith(pref) for pref in SKIP_PREFIXES)


SUSPICIOUS_EXEMPT = {"scripts/check_secrets.py"}


def scan_text(path: str, data: bytes, hits: list) -> None:
    if should_skip_path(path):
        return
    if b"\x00" in data[:4096]:
        return
    text = data.decode("utf-8", errors="replace")
    lines = text.splitlines()
    for name, rx in PATTERNS:
        for m in rx.finditer(text):
            line_no = text.count("\n", 0, m.start()) + 1
            line_txt = lines[line_no - 1].strip() if 0 < line_no <= len(lines) else ""
            if "System.getenv" in line_txt or "getenv(" in line_txt:
                continue
            hits.append(f"{path}:{line_no} [{name}] {line_txt[:120]}")


def scan_worktree() -> list:
    hits = []
    tracked = subprocess.check_output(["git", "ls-files", "-z"]).split(b"\x00")
    staged = subprocess.check_output(["git", "diff", "--cached", "--name-only", "-z"]).split(b"\x00")
    paths = sorted(set(p.decode("utf-8", "replace") for p in tracked + staged if p))
    for path in paths:
        if should_skip_path(path):
            continue
        if SUSPICIOUS_PATH_RE.search(path):
            hits.append(f"{path} [Suspicious Filename]")
        if os.path.isfile(path):
            with open(path, "rb") as f:
                scan_text(path, f.read(), hits)
    return hits


def scan_history() -> list:
    hits = []
    objs = (
        subprocess.check_output(["git", "rev-list", "--all", "--objects"])
        .decode("utf-8", errors="replace")
        .splitlines()
    )
    blobs = {}
    for line in objs:
        parts = line.split(" ", 1)
        if len(parts) == 2:
            sha, path = parts
            blobs[sha] = path
            if not should_skip_path(path) and SUSPICIOUS_PATH_RE.search(path):
                hits.append(f"{path} ({sha[:10]}) [Suspicious Filename in History]")

    p1 = subprocess.Popen(["git", "cat-file", "--batch-check"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    out, _ = p1.communicate(("\n".join(blobs.keys()) + "\n").encode())
    blob_shas = [
        l.split()[0]
        for l in out.decode("utf-8", errors="replace").splitlines()
        if len(l.split()) >= 3 and l.split()[1] == "blob" and int(l.split()[2]) < 2_000_000
    ]

    p2 = subprocess.Popen(["git", "cat-file", "--batch"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    p2.stdin.write(("\n".join(blob_shas) + "\n").encode())
    p2.stdin.close()
    for _ in range(len(blob_shas)):
        hdr = p2.stdout.readline().decode("utf-8", errors="replace").strip()
        if not hdr:
            break
        sha, _, sz = hdr.split()
        data = p2.stdout.read(int(sz))
        p2.stdout.read(1)
        scan_text(blobs.get(sha, "?"), data, hits)
    p2.wait()

    for label, cmd in [
        ("commit-msg", ["git", "log", "--all", "--format=%H %s%n%b"]),
        ("tag-msg", ["git", "tag", "-n99"]),
    ]:
        txt = subprocess.check_output(cmd)
        scan_text(label, txt, hits)

    return hits


def main() -> int:
    ap = argparse.ArgumentParser(description="Scan repo/history for credentials and secrets.")
    ap.add_argument("--history", action="store_true", help="Scan entire git commit & tag history.")
    args = ap.parse_args()

    hits = scan_history() if args.history else scan_worktree()
    hits = sorted(set(hits))
    if hits:
        print("SECRET/SENSITIVE DATA CHECK FAILED:", file=sys.stderr)
        for h in hits:
            print("  -", h, file=sys.stderr)
        return 1
    scope = "full git history" if args.history else "working tree & index"
    print(f"[PASS] No credentials, API keys, or local home paths found in {scope}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
