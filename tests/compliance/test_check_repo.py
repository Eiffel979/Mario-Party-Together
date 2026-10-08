"""Self-test for check_repo.py. Every input here is a few synthetic bytes."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from check_repo import MAX_FILE_BYTES, check_file  # noqa: E402


def reasons(path, head=b"plain text\n", size=None, allowlist=()):
    size = len(head) if size is None else size
    return [f.reason for f in check_file(path, size, head, list(allowlist))]


def at(offset, magic):
    return b"\xff" * offset + magic + b"\xff" * 16


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


if __name__ == "__main__":
    unittest.main()
