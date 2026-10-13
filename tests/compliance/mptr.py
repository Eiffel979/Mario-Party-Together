"""Python port of the .mptr replay rules (ADR 0006 and its P1-05 amendment, points 1-3 and 6).

The compliance scanner uses it to check committed replay fixtures under tests/replays/
(P1-20e2) without needing a build. core/src/replay.cpp is the reference implementation:
when the built mpt_replay_check is available, check_repo.py runs it as well and requires
both to agree. The core contract version and the number of known titles are read from
the core headers, so this port cannot drift from them silently.
"""

from __future__ import annotations

import hashlib
import re
import struct
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

MAGIC = b"MPTR"
FORMAT_VERSION = 0
PAD_BYTES = 32
RUN_BYTES = 4 + 4 * PAD_BYTES
CHECKPOINT_BYTES = 24
U32_MAX = 0xFFFFFFFF
BLANK_DIGEST = bytes(32)

# Byte offsets in a file with no embedded save media (amendment point 1).
OFFSET_EMBEDDED_LENGTH = 85
OFFSET_RUNS_NO_MEDIA = 105


def core_constants(root: Path = REPO_ROOT) -> tuple[int, int]:
    """(kCoreContractVersion, number of TitleId values) from the core headers."""
    runtime_h = (root / "core/include/mpt/core/title_runtime.h").read_text(encoding="utf-8")
    title_h = (root / "core/include/mpt/core/title_id.h").read_text(encoding="utf-8")
    version = re.search(r"kCoreContractVersion\s*=\s*(\d+)", runtime_h)
    titles = re.search(r"std::array<TitleId,\s*(\d+)>\s*kAllTitles", title_h)
    if not version or not titles:
        raise RuntimeError("cannot read kCoreContractVersion or kAllTitles from the core headers")
    return int(version.group(1)), int(titles.group(1))


class _Truncated(Exception):
    pass


class _Reader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.at = 0

    def remaining(self) -> int:
        return len(self.data) - self.at

    def take(self, n: int, field: str) -> bytes:
        if self.remaining() < n:
            raise _Truncated(field)
        out = self.data[self.at:self.at + n]
        self.at += n
        return out

    def u8(self, field: str) -> int:
        return self.take(1, field)[0]

    def u32(self, field: str) -> int:
        return struct.unpack("<I", self.take(4, field))[0]

    def u64(self, field: str) -> int:
        return struct.unpack("<Q", self.take(8, field))[0]


def validate(data: bytes, fixture: bool = True, constants: tuple[int, int] | None = None) -> str | None:
    """Returns None if `data` is a valid replay, else the first problem.

    fixture=False is the general strict parse (decode_replay). fixture=True adds the
    committed-fixture rule (validate_replay_fixture): no embedded save media, maximal runs,
    and a checkpoint at every multiple of the interval and nowhere else.
    """
    contract_version, title_count = constants or core_constants()
    r = _Reader(data)
    try:
        if r.take(4, "the magic") != MAGIC:
            return "not an .mptr file"
        format_version = r.u32("format_version")
        if format_version != FORMAT_VERSION:
            return f"unsupported format_version {format_version}"
        recorded = r.u32("core_contract_version")
        if recorded != contract_version:
            return f"recorded with core contract {recorded}, this build is {contract_version}"
        title = r.u8("title")
        if title >= title_count:
            return f"unknown title {title}"
        r.take(32, "dump_sha256")
        r.u64("seed")
        media_digest = r.take(32, "save_media_sha256")

        embedded_length = r.u32("save_media_embedded length")
        embedded = r.take(embedded_length, "save_media_embedded")
        if embedded_length:
            if media_digest == BLANK_DIGEST:
                return "embedded save media in a replay of blank media"
            if hashlib.sha256(embedded).digest() != media_digest:
                return "embedded save media does not match its digest"
            if embedded_length < 4:
                return "embedded save media is shorter than its 4-byte version"

        if r.u32("ruleset length") != 0:
            return "a ruleset is not supported in format v0"
        frame_count = r.u64("frame_count")
        interval = r.u32("checkpoint_interval")
        if interval == 0:
            return "checkpoint_interval is 0"

        runs: list[tuple[int, bytes]] = []
        frames = 0
        while frames < frame_count:
            if r.remaining() < RUN_BYTES:
                raise _Truncated("the input runs")
            length = r.u32("an input run")
            if length == 0:
                return "an input run of length 0"
            if length > frame_count - frames:
                return "the input runs exceed frame_count"
            runs.append((length, r.take(RUN_BYTES - 4, "an input run")))
            frames += length

        count = r.u32("the checkpoint count")
        if r.remaining() // CHECKPOINT_BYTES < count:
            raise _Truncated("the checkpoints")
        previous = None
        checkpoint_frames = []
        for _ in range(count):
            frame = r.u64("a checkpoint")
            r.u64("a checkpoint")
            r.u64("a checkpoint")
            if frame == 0 or frame % interval != 0:
                return f"checkpoint frame {frame} is not a positive multiple of {interval}"
            if previous is not None and frame <= previous:
                return f"checkpoint frame {frame} does not increase"
            if frame > frame_count:
                return f"checkpoint frame {frame} is past the last frame {frame_count}"
            previous = frame
            checkpoint_frames.append(frame)
        if r.remaining():
            return f"{r.remaining()} trailing bytes"
    except _Truncated as e:
        return f"file ends inside {e.args[0]}"

    if not fixture:
        return None
    if embedded_length:
        return "embedded save media is not allowed in a committed fixture (it is user data)"
    for i in range(1, len(runs)):
        if runs[i][1] == runs[i - 1][1] and runs[i - 1][0] != U32_MAX:
            return f"input runs {i - 1} and {i} carry the same pads (not canonical)"
    expected = frame_count // interval
    if len(checkpoint_frames) != expected:
        return (f"{len(checkpoint_frames)} checkpoints, a canonical replay has {expected} "
                f"(one at every multiple of {interval})")
    return None


def from_hex_text(text: str) -> bytes:
    """Bytes from a .mptr.hex golden: hex pairs, with '#' comment lines ignored."""
    return bytes.fromhex(" ".join(line for line in text.splitlines() if line.strip() and not line.startswith("#")))


def strip_embedded_media(data: bytes) -> bytes:
    """The same replay with its embedded save image removed (the digest stays)."""
    length = struct.unpack_from("<I", data, OFFSET_EMBEDDED_LENGTH)[0]
    start = OFFSET_EMBEDDED_LENGTH + 4
    return data[:OFFSET_EMBEDDED_LENGTH] + struct.pack("<I", 0) + data[start + length:]
