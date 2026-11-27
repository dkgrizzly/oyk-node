#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KR4OYK - David Kuder
"""Scan pending, staged, unpushed, tracked, or historical files for secrets and local paths."""

import argparse
import os
import re
import subprocess
import sys
import threading

SUSPICIOUS_PATH_RE = re.compile(
    r"(^|/)(\.env($|\.)|\.netrc$|\.npmrc$|\.pypirc$|id_rsa|id_ed25519|id_dsa|"
    r"local\.properties$|google-services\.json$|.*service[_-]?account.*\.json$|"
    r".*\.(pem|key|p12|pfx|jks|keystore|secret)$|secrets\.[^/]+$|.*credentials[^/]*$)",
    re.I,
)

SKIP_EXACT = {"LICENSE"}
SKIP_PREFIXES = ("docs/specs/rfc",)
SUSPICIOUS_EXEMPT = {"scripts/check_secrets.py"}
MAX_BLOB_BYTES = 2_000_000

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
    if path in SUSPICIOUS_EXEMPT or path in SKIP_EXACT:
        return True
    return any(path.startswith(pref) for pref in SKIP_PREFIXES)


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


def _git_z_paths(args: list) -> list:
    out = subprocess.check_output(["git"] + args)
    return [p.decode("utf-8", "replace") for p in out.split(b"\x00") if p]


def _filter_small_blobs(shas: list) -> list:
    if not shas:
        return []
    unique_shas = list(dict.fromkeys(shas))
    p = subprocess.Popen(
        ["git", "cat-file", "--batch-check"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
    )
    out, _ = p.communicate(("\n".join(unique_shas) + "\n").encode("utf-8"))
    if p.returncode != 0:
        raise subprocess.CalledProcessError(p.returncode, ["git", "cat-file", "--batch-check"])
    small = []
    for line in out.decode("utf-8", errors="replace").splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[1] == "blob" and int(parts[2]) < MAX_BLOB_BYTES:
            small.append(parts[0])
    return small


def cat_file_batch_scan(shas: list, label_map: dict, hits: list) -> None:
    """Stream blob SHAs to `git cat-file --batch` without stdin/stdout pipe deadlocks."""
    blob_shas = _filter_small_blobs(shas)
    if not blob_shas:
        return
    p = subprocess.Popen(
        ["git", "cat-file", "--batch"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
    )

    def _writer() -> None:
        try:
            assert p.stdin is not None
            for sha in blob_shas:
                p.stdin.write((sha + "\n").encode("utf-8"))
            p.stdin.close()
        except BrokenPipeError:
            pass

    t = threading.Thread(target=_writer, daemon=True)
    t.start()
    assert p.stdout is not None
    for _ in range(len(blob_shas)):
        hdr_line = p.stdout.readline()
        if not hdr_line:
            break
        hdr = hdr_line.decode("utf-8", errors="replace").strip()
        parts = hdr.split()
        if len(parts) < 3 or parts[1] == "missing":
            continue
        sha, obj_type, size = parts[0], parts[1], int(parts[2])
        data = p.stdout.read(size)
        p.stdout.read(1)  # trailing LF
        if obj_type != "blob" or size >= MAX_BLOB_BYTES:
            continue
        scan_text(label_map.get(sha, sha), data, hits)
    t.join()
    p.wait()


def _staged_blob_map(paths: list) -> dict:
    if not paths:
        return {}
    wanted = set(paths)
    out = subprocess.check_output(["git", "ls-files", "--stage", "-z"])
    blobs = {}
    for entry in out.split(b"\x00"):
        if not entry:
            continue
        meta, sep, raw_path = entry.partition(b"\t")
        if not sep:
            continue
        path = raw_path.decode("utf-8", "replace")
        if path not in wanted:
            continue
        parts = meta.decode("utf-8", "replace").split()
        if len(parts) >= 2:
            blobs[parts[1]] = path
    return blobs


def _scan_worktree_paths(paths: list, hits: list) -> None:
    for path in paths:
        if should_skip_path(path):
            continue
        if SUSPICIOUS_PATH_RE.search(path):
            hits.append(f"{path} [Suspicious Filename]")
        if os.path.isfile(path):
            with open(path, "rb") as f:
                scan_text(path, f.read(), hits)


def scan_staged() -> tuple:
    hits = []
    paths = sorted(set(_git_z_paths(["diff", "--cached", "--name-only", "--diff-filter=ACMRTUXB", "-z"])))
    if not paths:
        return hits, "staged changes (none)"
    for path in paths:
        if not should_skip_path(path) and SUSPICIOUS_PATH_RE.search(path):
            hits.append(f"{path} [Suspicious Filename]")
    blobs = _staged_blob_map(paths)
    cat_file_batch_scan(list(blobs.keys()), blobs, hits)
    return hits, "staged changes"


def scan_pending() -> tuple:
    staged_hits, _ = scan_staged()
    hits = list(staged_hits)
    unstaged = sorted(set(_git_z_paths(["diff", "--name-only", "--diff-filter=ACMRTUXB", "-z"])))
    _scan_worktree_paths(unstaged, hits)
    staged_any = _git_z_paths(["diff", "--cached", "--name-only", "--diff-filter=ACMRTUXB", "-z"])
    if not staged_any and not unstaged:
        return hits, "pending changes (none)"
    return hits, "pending changes (staged & modified)"


def resolve_unpushed_range() -> str:
    for ref, rev_range in (
        ("@{u}", "@{u}..HEAD"),
        ("origin/main", "origin/main..HEAD"),
        ("origin/HEAD", "origin/HEAD..HEAD"),
    ):
        if subprocess.call(
            ["git", "rev-parse", "--verify", "--quiet", ref],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ) == 0:
            return rev_range
    return "HEAD"


def scan_unpushed() -> tuple:
    hits = []
    rev_range = resolve_unpushed_range()
    commits = subprocess.check_output(["git", "rev-list", rev_range]).decode("utf-8", "replace").splitlines()
    if not commits:
        return hits, f"unpushed commits ({rev_range}, 0 commits)"

    objs = (
        subprocess.check_output(["git", "rev-list", "--objects", rev_range])
        .decode("utf-8", "replace")
        .splitlines()
    )
    blobs = {}
    for line in objs:
        parts = line.split(" ", 1)
        if len(parts) == 2:
            sha, path = parts
            blobs[sha] = path
            if not should_skip_path(path) and SUSPICIOUS_PATH_RE.search(path):
                hits.append(f"{path} ({sha[:10]}) [Suspicious Filename in Unpushed Commit]")

    cat_file_batch_scan(list(blobs.keys()), blobs, hits)

    log_txt = subprocess.check_output(["git", "log", rev_range, "--format=%H %s%n%b"])
    scan_text("commit-msg", log_txt, hits)

    commit_set = set(commits)
    ref_lines = (
        subprocess.check_output(
            ["git", "for-each-ref", "--format=%(objectname) %(*objectname) %(refname:short)", "refs/tags"]
        )
        .decode("utf-8", "replace")
        .splitlines()
    )
    tags_to_check = set()
    for line in ref_lines:
        parts = line.split(" ", 2)
        if len(parts) != 3:
            continue
        obj_sha, peeled_sha, tag_name = parts
        if obj_sha in commit_set or (peeled_sha and peeled_sha in commit_set):
            tags_to_check.add(tag_name)
    for tag_name in sorted(tags_to_check):
        tag_txt = subprocess.check_output(["git", "tag", "-n99", tag_name])
        scan_text(f"tag-msg:{tag_name}", tag_txt, hits)

    return hits, f"unpushed commits ({rev_range})"


def scan_worktree() -> tuple:
    hits = []
    tracked = _git_z_paths(["ls-files", "-z"])
    staged = _git_z_paths(["diff", "--cached", "--name-only", "-z"])
    paths = sorted(set(tracked + staged))
    _scan_worktree_paths(paths, hits)
    return hits, "all tracked & staged files"


def scan_history() -> tuple:
    hits = []
    objs = (
        subprocess.check_output(["git", "rev-list", "--all", "--objects"])
        .decode("utf-8", "replace")
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

    cat_file_batch_scan(list(blobs.keys()), blobs, hits)

    for label, cmd in [
        ("commit-msg", ["git", "log", "--all", "--format=%H %s%n%b"]),
        ("tag-msg", ["git", "tag", "-n99"]),
    ]:
        txt = subprocess.check_output(cmd)
        scan_text(label, txt, hits)

    return hits, "full git history"


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Scan pending, staged, unpushed, tracked, or historical changes for secrets."
    )
    grp = ap.add_mutually_exclusive_group()
    grp.add_argument("--staged", action="store_true", help="Scan only staged changes in the Git index.")
    grp.add_argument(
        "--unpushed",
        action="store_true",
        help="Scan commits and blobs not yet pushed to upstream (@{u}..HEAD).",
    )
    grp.add_argument("--all", action="store_true", help="Scan all tracked and staged files in working tree.")
    grp.add_argument("--history", action="store_true", help="Scan entire git commit & tag history.")
    args = ap.parse_args()

    if args.staged:
        hits, scope = scan_staged()
    elif args.unpushed:
        hits, scope = scan_unpushed()
    elif args.all:
        hits, scope = scan_worktree()
    elif args.history:
        hits, scope = scan_history()
    else:
        hits, scope = scan_pending()

    hits = sorted(set(hits))
    if hits:
        print("SECRET/SENSITIVE DATA CHECK FAILED:", file=sys.stderr)
        for h in hits:
            print("  -", h, file=sys.stderr)
        return 1
    print(f"[PASS] No credentials, API keys, or local home paths found in {scope}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
