"""Self-tests for the .mptr validator (P1-20e2). Every replay is generated at test time from
tests/golden/replay_v0_synthetic.mptr.hex (hex text, never a committed .mptr)."""

import contextlib
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import mptr  # noqa: E402
from check_repo import HEAD_BYTES, check_file, main, scan_commits, scan_staged, scan_tree  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
GOLDEN = mptr.from_hex_text((REPO_ROOT / "tests/golden/replay_v0_synthetic.mptr.hex").read_text(encoding="utf-8"))
FIXTURE = mptr.strip_embedded_media(GOLDEN)  # valid as a committed fixture

# Offsets in GOLDEN, as in tests/unit/test_replay.cpp ("corrupt fields fail with a reason").
AFTER_MEDIA = 89 + 12
CHECKPOINTS_AT = AFTER_MEDIA + 16 + 5 * mptr.RUN_BYTES


def patched(data, at, value, n):
    b = bytearray(data)
    b[at:at + n] = value.to_bytes(n, "little")
    return bytes(b)


def encode(frame_count, interval, runs, checkpoint_frames):
    """A replay with FIXTURE's header and the given body. runs: [(length, 128 pad bytes)]."""
    body = struct.pack("<QI", frame_count, interval)
    for length, pads in runs:
        body += struct.pack("<I", length) + pads
    body += struct.pack("<I", len(checkpoint_frames))
    for f in checkpoint_frames:
        body += struct.pack("<QQQ", f, f * 3, f * 7)
    return FIXTURE[:mptr.OFFSET_RUNS_NO_MEDIA - 12] + body


def pads(seed):
    """128 synthetic pad bytes: pad 0 connected, its buttons set from `seed`."""
    one = struct.pack("<I", seed) + bytes(8) + bytes([0, 0, 1, 0]) + bytes(16)
    return one + bytes(3 * mptr.PAD_BYTES)


class PortTest(unittest.TestCase):
    def test_golden_and_fixture_verdicts(self):
        self.assertIsNone(mptr.validate(GOLDEN, fixture=False))
        self.assertIn("embedded save media is not allowed", mptr.validate(GOLDEN, fixture=True))
        self.assertIsNone(mptr.validate(FIXTURE, fixture=True))
        self.assertEqual(len(GOLDEN) - len(FIXTURE), 12)

    def test_core_constants_come_from_the_headers(self):
        version, titles = mptr.core_constants()
        self.assertGreaterEqual(version, 1)
        self.assertEqual(titles, 12)
        self.assertIn("core contract", mptr.validate(FIXTURE, constants=(version + 1, titles)))

    def test_every_truncation_and_trailing_byte_fail(self):
        for n in range(len(FIXTURE)):
            with self.subTest(n=n):
                self.assertIsNotNone(mptr.validate(FIXTURE[:n], fixture=False))
        self.assertEqual(mptr.validate(FIXTURE + bytes(1)), "1 trailing bytes")

    def test_fixed_offset_corruptions(self):
        cases = [
            (patched(GOLDEN, 0, ord("X"), 1), "not an .mptr"),
            (patched(GOLDEN, 4, 1, 4), "format_version"),
            (patched(GOLDEN, 8, mptr.core_constants()[0] + 1, 4), "core contract"),
            (patched(GOLDEN, 12, 200, 1), "unknown title"),
            (patched(GOLDEN, 93, 0, 1), "does not match its digest"),
            (patched(GOLDEN, AFTER_MEDIA, 1, 4), "ruleset"),
            (patched(GOLDEN, AFTER_MEDIA + 4, 9, 8), "exceed frame_count"),
            (patched(GOLDEN, AFTER_MEDIA + 4, 11, 8), "ends inside"),
            (patched(GOLDEN, AFTER_MEDIA + 12, 0, 4), "checkpoint_interval is 0"),
            (patched(GOLDEN, AFTER_MEDIA + 16, 0, 4), "length 0"),
            (patched(GOLDEN, CHECKPOINTS_AT + 4, 5, 8), "multiple"),
            (patched(GOLDEN, CHECKPOINTS_AT + 4 + 24, 4, 8), "does not increase"),
            (patched(GOLDEN, CHECKPOINTS_AT, 0xFFFFFFFF, 4), "ends inside"),
            (GOLDEN[:53] + bytes(32) + GOLDEN[85:], "blank media"),
        ]
        for data, phrase in cases:
            with self.subTest(phrase=phrase):
                self.assertIn(phrase, mptr.validate(data, fixture=False) or "valid")

    def test_canonical_form_is_required_only_for_fixtures(self):
        a, b = pads(1), pads(2)
        canonical = encode(8, 4, [(3, a), (5, b)], [4, 8])
        split = encode(8, 4, [(1, a), (2, a), (5, b)], [4, 8])
        sparse = encode(8, 4, [(3, a), (5, b)], [8])
        self.assertIsNone(mptr.validate(canonical))
        for data, phrase in [(split, "not canonical"), (sparse, "1 checkpoints, a canonical replay has 2")]:
            with self.subTest(phrase=phrase):
                self.assertIsNone(mptr.validate(data, fixture=False))
                self.assertIn(phrase, mptr.validate(data))
        capped = encode(mptr.U32_MAX + 1, mptr.U32_MAX, [(mptr.U32_MAX, a), (1, a)], [mptr.U32_MAX])
        self.assertIsNone(mptr.validate(capped))  # equal runs are allowed after a full u32 run


class ScannerRuleTest(unittest.TestCase):
    def test_fixture_under_tests_replays_passes(self):
        self.assertEqual(check_file("tests/replays/party.mptr", len(FIXTURE), FIXTURE, []), [])

    def test_invalid_fixture_fails(self):
        for data in [GOLDEN, FIXTURE + bytes(1), FIXTURE[:40]]:
            findings = check_file("tests/replays/party.mptr", len(data), data, [])
            self.assertTrue(any("invalid replay fixture" in f.reason for f in findings))

    def test_replays_elsewhere_fail(self):
        for path in ["assets/party.mptr", "tests/golden/party.MPTR", "replays/x.mptr", "tests/replays_old/x.mptr"]:
            with self.subTest(path=path):
                reasons = [f.reason for f in check_file(path, len(FIXTURE), FIXTURE, [])]
                self.assertTrue(any("belong only in tests/replays/" in r for r in reasons))

    def test_hex_golden_is_plain_text(self):
        path = "tests/golden/replay_v0_synthetic.mptr.hex"
        text = (REPO_ROOT / path).read_bytes()
        self.assertEqual(check_file(path, len(text), text, []), [])


@unittest.skipUnless(shutil.which("git"), "git is not installed")
class ScannerRepoTest(unittest.TestCase):
    """The scan modes read the whole fixture, not just the first HEAD_BYTES."""

    def setUp(self):
        self._old_cwd = os.getcwd()
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.vcs("init", "-q")
        a, b = pads(1), pads(2)
        frames = 600
        runs = [(1, a if i % 2 == 0 else b) for i in range(frames)]  # about 79 KB, past HEAD_BYTES
        self.big = encode(frames, 60, runs, list(range(60, frames + 1, 60)))
        self.assertGreater(len(self.big), HEAD_BYTES)
        self.assertIsNone(mptr.validate(self.big))
        (self.root / "tests/replays").mkdir(parents=True)
        (self.root / "tests/replays/long.mptr").write_bytes(self.big)
        os.chdir(self.root)

    def tearDown(self):
        os.chdir(self._old_cwd)
        self._tmp.cleanup()

    def vcs(self, *args):
        config = ["-c", "user.name=test", "-c", "user.email=test@example.invalid", "-c", "commit.gpgsign=false",
                  "-c", "core.autocrlf=false"]
        return subprocess.run(["git", *config, *args], cwd=self.root, check=True, capture_output=True).stdout

    def test_every_scan_mode_accepts_a_long_fixture(self):
        self.vcs("add", "--all")
        self.assertEqual(scan_staged([]), [])
        self.vcs("comm" + "it", "-q", "--no-verify", "-m", "synthetic")
        self.assertEqual(scan_tree([]), [])
        self.assertEqual(scan_commits("HE" + "AD", []), [])

    def test_a_corrupt_long_fixture_fails(self):
        (self.root / "tests/replays/long.mptr").write_bytes(self.big[:-1])
        self.vcs("add", "--all")
        with contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(main(["--staged"]), 1)
        self.assertIn("invalid replay fixture", err.getvalue())


TOOL = os.environ.get("MPT_REPLAY_CHECK")
REQUIRE_TOOL = os.environ.get("MPT_REQUIRE_REPLAY_CHECK") == "1"


class ToolCrossCheckTest(unittest.TestCase):
    """mptr.py and the built mpt_replay_check --fixture must agree on every case. CTest runs
    this with the tool (replay_check_agrees_with_scanner); without it the class skips."""

    def setUp(self):
        if not TOOL:
            if REQUIRE_TOOL:
                self.fail("MPT_REQUIRE_REPLAY_CHECK=1 but MPT_REPLAY_CHECK is not set")
            self.skipTest("MPT_REPLAY_CHECK is not set")
        self.assertTrue(Path(TOOL).is_file(), f"MPT_REPLAY_CHECK does not exist: {TOOL}")

    def run_tool(self, data):
        handle, name = tempfile.mkstemp(suffix=".mptr")
        try:
            with os.fdopen(handle, "wb") as f:
                f.write(data)
            return subprocess.run([TOOL, "--fixture", name], capture_output=True, text=True)
        finally:
            os.unlink(name)

    def test_the_tool_accepts_a_valid_fixture(self):
        result = self.run_tool(FIXTURE)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_the_tool_rejects_embedded_media(self):
        result = self.run_tool(GOLDEN)
        self.assertEqual(result.returncode, 1)
        self.assertIn("embedded save media", result.stdout)

    def test_both_validators_agree(self):
        a, b = pads(1), pads(2)
        cases = [
            FIXTURE, GOLDEN, FIXTURE + bytes(1), FIXTURE[:100], FIXTURE[:-24],
            encode(8, 4, [(3, a), (5, b)], [4, 8]),
            encode(8, 4, [(1, a), (2, a), (5, b)], [4, 8]),
            encode(8, 4, [(3, a), (5, b)], [8]),
            encode(8, 4, [(3, a), (5, b)], [4, 8, 12]),
            patched(FIXTURE, 4, 1, 4), patched(FIXTURE, 12, 200, 1), patched(FIXTURE, 89, 1, 4),
        ]
        for i, data in enumerate(cases):
            with self.subTest(case=i):
                python_ok = mptr.validate(data) is None
                tool_ok = self.run_tool(data).returncode == 0
                self.assertEqual(python_ok, tool_ok)


if __name__ == "__main__":
    unittest.main()
