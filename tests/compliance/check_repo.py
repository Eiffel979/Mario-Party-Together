#!/usr/bin/env python3
"""Compliance scanner: blocks game dumps, extracted assets and stray binaries.

Enforces CLAUDE.md hard rule 1 and ADR 0001. It runs in CI on every push and
pull request, and locally as a pre-commit hook (.githooks/pre-commit).

Modes:
  check_repo.py                      scan every tracked file in the working tree
  check_repo.py --staged             scan what is staged for the next commit
  check_repo.py --commits A..B       scan files added or changed by each commit in A..B

A file fails if it has a dump/container extension, is a memory-card or save
image (ADR 0004), is a Dawn library or GPU pipeline cache (ADR 0007),
sits in a dump or extracted-asset directory, sits under the
root external/ directory where game-source checkouts live (ADR 0009), starts with
a known console image signature, is binary, or is larger than the size limit.
Legitimate exceptions go in allowlist.txt with a reason.
"""

from __future__ import annotations

import argparse
import fnmatch
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

HEAD_BYTES = 64 * 1024  # how much of each file is inspected for signatures and NUL bytes
MAX_FILE_BYTES = 1024 * 1024
ALLOWLIST = Path(__file__).with_name("allowlist.txt")

# Disc images, cartridge dumps, compressed containers, console executables and key files.
FORBIDDEN_EXTENSIONS = {
    ".z64", ".n64", ".v64", ".u64", ".rom",
    ".iso", ".gcm", ".gcz", ".rvz", ".wia", ".wbfs", ".ciso", ".tgc", ".nkit",
    ".wad", ".dol", ".rel", ".elf", ".rso",
    ".wud", ".wux", ".rpx", ".rpl", ".app", ".h3",
    ".nsp", ".xci", ".nca", ".nso", ".nro", ".npdm",
    ".keys", ".key", ".pem", ".tik", ".cert",
    ".szs", ".arc", ".brres", ".bfres", ".brstm", ".bcstm", ".bfstm", ".thp", ".msbt", ".sarc",
}

# Memory-card and save images. They are user data that embed game-made icons and
# banners, so they are never committed (ADR 0004, P1-20e1).
SAVE_MEDIA_EXTENSIONS = {".gci", ".gcs", ".sav", ".raw"}

# Graphics build output and caches, matched by file name at any depth (ADR 0007 section 6,
# P1-14 gate). Dawn is built locally from pinned source and never committed, and GPU
# pipeline caches hold driver- and shader-derived data from a local run.
FORBIDDEN_FILENAMES = {"webgpu_dawn.dll", "dawn_cache.db", "pipeline_cache.db"}

# Forbidden at any depth.
FORBIDDEN_DIRS = {"dumps", "extracted", "roms", "isos"}

# Forbidden as the first path component only. external/ at the repository root is
# where developer builds fetch pinned game-source checkouts (ADR 0009 §1, P1-20e1).
FORBIDDEN_ROOT_DIRS = {"external"}

# (offset, magic bytes, description)
SIGNATURES = [
    (0x00, bytes.fromhex("80371240"), "N64 ROM (z64)"),
    (0x00, bytes.fromhex("37804012"), "N64 ROM (v64)"),
    (0x00, bytes.fromhex("40123780"), "N64 ROM (n64)"),
    (0x1C, bytes.fromhex("C2339F3D"), "GameCube disc image"),
    (0x18, bytes.fromhex("5D1C9EA3"), "Wii disc image"),
    (0x00, b"WBFS", "WBFS container"),
    (0x00, b"RVZ\x01", "RVZ container"),
    (0x00, b"WIA\x01", "WIA container"),
    (0x00, bytes.fromhex("01C00BB1"), "GCZ container"),
    (0x00, b"CISO", "CISO container"),
    (0x00, b"Yaz0", "Yaz0-compressed game data"),
    (0x00, b"Yay0", "Yay0-compressed game data"),
    (0x00, b"RARC", "RARC archive"),
    (0x00, b"U\xaa8-", "U8 archive"),
    (0x00, b"SARC", "SARC archive"),
    (0x00, b"NSO0", "Switch NSO executable"),
    (0x10, b"NRO0", "Switch NRO executable"),
    (0x00, b"PFS0", "Switch PFS0/NSP package"),
    (0x00, b"\x7fELF", "ELF executable"),
    (0x00, b"MZ", "Windows executable"),
]


class RangeError(Exception):
    """A --commits range that git cannot resolve (for example a base lost to a force-push)."""


@dataclass
class Finding:
    path: str
    reason: str


def load_allowlist(path: Path = ALLOWLIST) -> list[str]:
    if not path.exists():
        return []
    patterns = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            patterns.append(line)
    return patterns


def check_file(path: str, size: int, head: bytes, allowlist: list[str]) -> list[Finding]:
    """Checks one file given its repo-relative POSIX path, total size and first bytes."""
    if any(fnmatch.fnmatchcase(path, pattern) for pattern in allowlist):
        return []

    findings = []
    pure = PurePosixPath(path)
    if pure.suffix.lower() in FORBIDDEN_EXTENSIONS:
        findings.append(Finding(path, f"forbidden file type '{pure.suffix}'"))
    if pure.suffix.lower() in SAVE_MEDIA_EXTENSIONS:
        findings.append(Finding(path, f"memory-card or save image '{pure.suffix}' (save media is user data, ADR 0004)"))
    if pure.name.lower() in FORBIDDEN_FILENAMES:
        findings.append(Finding(path, f"forbidden file '{pure.name}' (Dawn build output or GPU pipeline cache, ADR 0007)"))
    if len(pure.parts) > 1 and pure.parts[0].lower() in FORBIDDEN_ROOT_DIRS:
        findings.append(Finding(path, f"inside the root '{pure.parts[0]}/' directory (game-source checkouts, ADR 0009)"))
    for part in pure.parts[:-1]:
        if part.lower() in FORBIDDEN_DIRS:
            findings.append(Finding(path, f"inside a '{part}/' directory"))
            break
    for offset, magic, description in SIGNATURES:
        if head[offset:offset + len(magic)] == magic:
            findings.append(Finding(path, f"looks like a {description}"))
            break
    if b"\x00" in head[:8192]:
        findings.append(Finding(path, "binary file (add it to allowlist.txt with a reason if it is our own)"))
    if size > MAX_FILE_BYTES:
        findings.append(Finding(path, f"{size} bytes exceeds the {MAX_FILE_BYTES} byte limit"))
    return findings


def git(*args: str) -> str:
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout


def read_blob_head(spec: str) -> tuple[int, bytes]:
    """Returns (size, first HEAD_BYTES bytes) of a git blob without reading all of it."""
    size = int(git("cat-file", "-s", spec).strip())
    with subprocess.Popen(["git", "cat-file", "blob", spec], stdout=subprocess.PIPE) as proc:
        assert proc.stdout is not None
        head = proc.stdout.read(HEAD_BYTES)
        proc.kill()
    return size, head


def scan_tree(allowlist: list[str]) -> list[Finding]:
    findings = []
    for path in git("ls-files", "-z").split("\0"):
        if not path:
            continue
        file = Path(path)
        if not file.is_file():  # deleted in the working tree but still tracked
            continue
        with file.open("rb") as f:
            head = f.read(HEAD_BYTES)
        findings += check_file(path, file.stat().st_size, head, allowlist)
    return findings


def scan_staged(allowlist: list[str]) -> list[Finding]:
    findings = []
    for path in git("diff", "--cached", "--name-only", "-z", "--diff-filter=ACMR").split("\0"):
        if path:
            size, head = read_blob_head(f":{path}")
            findings += check_file(path, size, head, allowlist)
    return findings


def scan_commits(rev_range: str, allowlist: list[str]) -> list[Finding]:
    findings = []
    try:
        commits = git("rev-list", "--reverse", rev_range).split()
    except subprocess.CalledProcessError as e:
        detail = (e.stderr or "").strip().splitlines()
        raise RangeError(
            f"cannot resolve the commit range '{rev_range}'"
            + (f" (git: {detail[0]})" if detail else "")
            + ". Its base may be missing from this checkout, for example after a force-push. "
            "Scan a range that exists, such as origin/main..HEAD."
        ) from e
    for commit in commits:
        changed = git("diff-tree", "--no-commit-id", "--root", "-r", "-z", "--name-only", "--diff-filter=ACMR", commit)
        for path in changed.split("\0"):
            if path:
                size, head = read_blob_head(f"{commit}:{path}")
                findings += [Finding(f"{commit[:10]}:{f.path}", f.reason) for f in check_file(path, size, head, allowlist)]
    return findings


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--staged", action="store_true", help="scan staged changes")
    mode.add_argument("--commits", metavar="RANGE", help="scan each commit in a git revision range")
    args = parser.parse_args(argv)

    allowlist = load_allowlist()
    if args.staged:
        findings = scan_staged(allowlist)
    elif args.commits:
        try:
            findings = scan_commits(args.commits, allowlist)
        except RangeError as e:
            print(f"compliance: {e}", file=sys.stderr)
            return 2
    else:
        findings = scan_tree(allowlist)

    for finding in findings:
        print(f"compliance: {finding.path}: {finding.reason}", file=sys.stderr)
    if findings:
        print(
            f"\ncompliance: {len(findings)} problem(s). Game dumps, extracted assets and binaries must never be "
            "committed (CLAUDE.md rule 1, ADR 0001).",
            file=sys.stderr,
        )
        return 1
    print("compliance: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
