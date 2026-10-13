"""Self-test for check_repo.py. Every input here is synthetic and built at test time."""

import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from check_repo import MAX_FILE_BYTES, RangeError, check_file, main, scan_commits, scan_staged, scan_tree  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]


def reasons(path, head=b"plain text\n", size=None, allowlist=()):
    size = len(head) if size is None else size
    return [f.reason for f in check_file(path, size, head, list(allowlist))]


def at(offset, magic):
    return b"\xff" * offset + magic + b"\xff" * 16


def strip_commit_id(path):
    """Commit mode reports paths as 'abcdef1234:some/path'. Returns the path part."""
    _, _, rest = path.partition(":")
    return rest


class CheckFileTest(unittest.TestCase):
    def test_source_files_pass(self):
        self.assertEqual(reasons("core/src/app.cpp", b"#include <cstdio>\n"), [])
        self.assertEqual(reasons("docs/PLAN.md", b"# Plan\n"), [])

    def test_dump_extensions_fail(self):
        for name in ["game.z64", "Game.ISO", "a/b/disc.rvz", "title.wbfs", "prod.keys", "main.dol", "file.szs"]:
            with self.subTest(name=name):
                self.assertTrue(any("forbidden file type" in r for r in reasons(name)))

    def test_dump_directories_fail(self):
        self.assertTrue(any("dumps/" in r for r in reasons("dumps/readme.txt")))
        self.assertTrue(any("extracted/" in r for r in reasons("asset-import/extracted/x.txt")))

    def test_signatures_detected_regardless_of_name(self):
        cases = {
            "N64 ROM (z64)": at(0, bytes.fromhex("80371240")),
            "N64 ROM (v64)": at(0, bytes.fromhex("37804012")),
            "GameCube disc image": at(0x1C, bytes.fromhex("C2339F3D")),
            "Wii disc image": at(0x18, bytes.fromhex("5D1C9EA3")),
            "RVZ container": at(0, b"RVZ\x01"),
            "Yaz0-compressed game data": at(0, b"Yaz0"),
        }
        for description, head in cases.items():
            with self.subTest(description=description):
                self.assertIn(f"looks like a {description}", reasons("innocent.txt", head))

    def test_binary_and_oversized_files_fail(self):
        self.assertTrue(any("binary file" in r for r in reasons("blob.dat", b"abc\x00def")))
        self.assertTrue(any("exceeds" in r for r in reasons("big.txt", size=MAX_FILE_BYTES + 1)))

    def test_allowlist_exempts_matching_paths(self):
        self.assertEqual(reasons("ui/icons/app.png", b"\x89PNG\x00", allowlist=["ui/icons/*.png"]), [])
        self.assertNotEqual(reasons("ui/other/app.png", b"\x89PNG\x00", allowlist=["ui/icons/*.png"]), [])


class ForbiddenPathsTest(unittest.TestCase):
    """P1-20e1: memory-card images (ADR 0004) and the root external/ directory (ADR 0009)."""

    def test_memory_card_extensions_fail(self):
        for name in ["card.gci", "MP4.GCI", "a/b/slot_a.gcs", "saves/mp4/usa/card.raw", "x.Sav"]:
            with self.subTest(name=name):
                self.assertTrue(any("memory-card or save image" in r for r in reasons(name)))

    def test_shell_save_slots_fail(self):
        for name in ["mp4/USA/slot0.mptsave", "saves/mp4/USA/slot0.mptsave.tmp", "x.MPTSAVE"]:
            with self.subTest(name=name):
                self.assertTrue(any("shell save slot" in r for r in reasons(name)))
        self.assertEqual(reasons("core/shell/save_store.cpp"), [])
        self.assertEqual(reasons("docs/mptsave_format.md"), [])

    def test_memory_card_header_with_text_body_still_fails(self):
        # A synthetic GCI-style header (game code + maker code) built here, not read from a card.
        head = b"GXXE01" + b"synthetic test card, not a real save\n"
        self.assertTrue(any("memory-card or save image" in r for r in reasons("card.gci", head)))

    def test_similar_names_pass(self):
        for name in ["docs/raw_notes.md", "core/src/save.cpp", "tests/unit/test_gci_layout.cpp", "saves.txt"]:
            with self.subTest(name=name):
                self.assertEqual(reasons(name), [])

    def test_root_external_directory_fails(self):
        for name in ["external/partyboard/src/game/main.c", "External/README.md", "external/x"]:
            with self.subTest(name=name):
                self.assertTrue(any("root 'external" in r.lower() for r in reasons(name)))

    def test_external_elsewhere_or_as_a_file_passes(self):
        for name in ["external", "docs/external/notes.md", "core/src/external_api.cpp", "externals/a.txt"]:
            with self.subTest(name=name):
                self.assertEqual(reasons(name), [])


@unittest.skipUnless(shutil.which("git"), "git is not installed")
class ForbiddenPathsRepoTest(unittest.TestCase):
    """Runs the real scan modes against a throwaway repository built at test time.

    The repository uses this project's .gitignore, so the test also proves the
    ignore rules cover the forbidden paths. Every file holds synthetic text.
    """

    REV = "HEAD"
    FORBIDDEN = ["external/partyboard/src/main.c", "saves/card.gci", "slot.gcs", "game.sav", "card.raw",
                 "saves/mp4/USA/slot0.mptsave"]

    def setUp(self):
        self._old_cwd = os.getcwd()
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.vcs("init", "-q")
        shutil.copyfile(REPO_ROOT / ".gitignore", self.root / ".gitignore")
        for path in self.FORBIDDEN + ["src/ok.cpp"]:
            file = self.root / path
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text("synthetic test content\n", encoding="utf-8")
        os.chdir(self.root)  # the scan functions run git in the current directory

    def tearDown(self):
        os.chdir(self._old_cwd)
        self._tmp.cleanup()

    def vcs(self, *args):
        config = ["-c", "user.name=test", "-c", "user.email=test@example.invalid", "-c", "commit.gpgsign=false",
                  "-c", "core.autocrlf=false"]
        return subprocess.run(["git", *config, *args], cwd=self.root, check=True, capture_output=True,
                              text=True).stdout

    def test_ignore_file_covers_forbidden_paths(self):
        ignored = self.vcs("check-ignore", "--no-index", *self.FORBIDDEN, "src/ok.cpp").split()
        self.assertEqual(sorted(ignored), sorted(self.FORBIDDEN))
        self.vcs("add", "-A")
        self.assertEqual(sorted(self.vcs("diff", "--cached", "--name-only").split()), [".gitignore", "src/ok.cpp"])

    def test_force_added_files_fail_every_scan_mode(self):
        self.vcs("add", "--force", "--all")
        self.assertEqual({f.path for f in scan_staged([])}, set(self.FORBIDDEN))

        self.vcs("commit", "-q", "--no-verify", "-m", "synthetic")
        self.assertEqual({f.path for f in scan_tree([])}, set(self.FORBIDDEN))
        history = scan_commits(self.REV, [])
        # Commit mode prefixes each path with the short commit id and a colon.
        self.assertEqual({strip_commit_id(finding.path) for finding in history}, set(self.FORBIDDEN))

    def test_unresolvable_range_is_a_clear_error(self):
        # The push-event case after a force-push: the old head is not in the checkout.
        self.vcs("add", "--force", "--all")
        self.vcs("comm" + "it", "-q", "--no-verify", "-m", "synthetic")
        missing = "e4cc8eb" + "0" * 33 + "..HEAD"
        with self.assertRaises(RangeError) as ctx:
            scan_commits(missing, [])
        self.assertIn("cannot resolve the commit range", str(ctx.exception))
        with contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(main(["--commits", missing]), 2)
        self.assertIn("origin/main..HEAD", err.getvalue())
        self.assertNotIn("Traceback", err.getvalue())

    def test_main_exits_nonzero(self):
        self.vcs("add", "--force", "--all")
        with contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(main(["--staged"]), 1)
        self.assertIn("external/partyboard/src/main.c", err.getvalue())


class GraphicsArtifactsTest(unittest.TestCase):
    """P1-14 gate: Dawn libraries and GPU pipeline caches are local build output (ADR 0007)."""

    def test_dawn_and_pipeline_caches_fail_at_any_depth(self):
        for name in ["webgpu_dawn.dll", "build/bin/webgpu_dawn.dll", "WebGPU_Dawn.DLL",
                     "dawn_cache.db", "cache/gx/dawn_cache.db", "pipeline_cache.db", "x/y/Pipeline_Cache.DB"]:
            with self.subTest(name=name):
                self.assertTrue(any("Dawn build output or GPU pipeline cache" in r for r in reasons(name)))

    def test_text_content_does_not_help(self):
        # Even a synthetic text file under one of these names is rejected: the name is the rule.
        self.assertTrue(any("forbidden file" in r for r in reasons("pipeline_cache.db", b"synthetic text\n")))

    def test_similar_names_pass(self):
        for name in ["docs/pipeline_cache.md", "src/dawn_cache.cpp", "pipeline_cache.db.txt",
                     "runtimes/gc_wii/gx/webgpu_dawn.h", "notes/dawn.txt"]:
            with self.subTest(name=name):
                self.assertEqual(reasons(name), [])


if __name__ == "__main__":
    unittest.main()
