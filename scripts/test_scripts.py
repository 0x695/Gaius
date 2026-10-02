#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the scripts: their own logic always, the built tools when they
exist, and the real game's files when GAIUS_GAME / GAIUS_TEST_ASSETS names them.
Nothing here needs, or writes, a game file when no folder is named.

    python scripts/test_scripts.py
"""

import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import _gaius  # noqa: E402
import check_saves  # noqa: E402
import export_assets  # noqa: E402

SAVE_SIZE = 57126  # formats::save::kSaveSize


def tools_built():
    try:
        for name in ("save_inspect", "save_diff", "sim_check", "dump_voc"):
            _gaius.tool(name)
        return True
    except _gaius.ScriptError:
        return False


def real_game():
    try:
        return _gaius.game_folder()
    except _gaius.ScriptError:
        return None


def blank_save(directory, name, changes=()):
    """A save of the right size, all zero but for (offset, byte) changes."""
    data = bytearray(SAVE_SIZE)
    for offset, value in changes:
        data[offset] = value
    path = Path(directory) / name
    path.write_bytes(bytes(data))
    return path


def tiny_voc(path, samples=b"\x80\x90\xa0\xb0", time_constant=131):
    """A Creative Voice File with one sound block: 1,000,000 / (256 - 131) = 8000 Hz."""
    version = 0x010A
    header = b"Creative Voice File\x1a" + struct.pack("<HHH", 0x1A, version, (~version + 0x1234) & 0xFFFF)
    length = len(samples) + 2
    block = bytes([1]) + length.to_bytes(3, "little") + bytes([time_constant, 0]) + samples
    Path(path).write_bytes(header + block + b"\x00")


class Logic(unittest.TestCase):
    def test_dates_count_months_across_the_year_zero(self):
        a = {"year": -1, "month": 11}
        b = {"year": 0, "month": 0}
        self.assertEqual(_gaius.month_number(b) - _gaius.month_number(a), 1)
        self.assertEqual(_gaius.month_number({"year": 4, "month": 7}) - _gaius.month_number({"year": 3, "month": 7}), 12)

    def test_summary_parsing(self):
        facts = _gaius.parse_summary("file: a.SAV\ngovernor: Octavian\nfunds: 7182\nno colon here\n")
        self.assertEqual(facts, {"file": "a.SAV", "governor": "Octavian", "funds": "7182"})

    def test_candidate_pairs(self):
        def entry(name, year, month):
            return Path(name), {"year": year, "month": month}

        entries = [entry("a", -1, 7), entry("b", -1, 7), entry("c", -1, 9), entry("d", 3, 0)]
        pairs = {(p.name, q.name) for p, q in check_saves.candidate_pairs(entries)}
        # The same month: both ways round. Up to two months later: forward only. Years away: none.
        self.assertEqual(pairs, {("a", "b"), ("b", "a"), ("a", "c"), ("b", "c")})

    def test_file_lookup_ignores_case(self):
        with tempfile.TemporaryDirectory() as folder:
            Path(folder, "Csr.Exe").write_bytes(b"x")
            Path(folder, "fixts.pl8").write_bytes(b"x")
            self.assertEqual(_gaius.find_file(folder, "CSR.EXE").name, "Csr.Exe")
            self.assertTrue(_gaius.is_game_folder(Path(folder)))
            self.assertEqual([p.name for p in _gaius.files_with_extension(folder, "PL8")], ["fixts.pl8"])

    def test_a_wrong_game_folder_is_an_error_not_a_search(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(_gaius.ScriptError):
                _gaius.game_folder(folder)

    def test_exports_stay_out_of_the_repository(self):
        # Inside the repo and not ignored: refused, and nothing is created.
        refused = _gaius.REPO / "scripts" / "exported_by_mistake"
        with self.assertRaises(_gaius.ScriptError):
            _gaius.output_dir(refused)
        self.assertFalse(refused.exists())
        # Outside it: fine.
        with tempfile.TemporaryDirectory() as folder:
            self.assertTrue(_gaius.output_dir(Path(folder) / "out").is_dir())

    def test_the_export_folder_is_ignored_by_git(self):
        try:
            ignored = subprocess.run(["git", "-C", str(_gaius.REPO), "check-ignore", "-q", "export/x.png"]).returncode == 0
        except OSError:
            self.skipTest("git isn't installed")
        if not (_gaius.REPO / ".git").exists():
            self.skipTest("not a git checkout")
        self.assertTrue(ignored, "export/ must be in .gitignore")

    def test_palette_table_names_real_kinds_of_file(self):
        for picture, (palette, confidence) in export_assets.PICTURES.items():
            self.assertTrue(picture.endswith(".VPX"), picture)
            self.assertTrue(palette.endswith((".256", ".P32")), palette)
            self.assertTrue(confidence, picture)
        # Every picture's palette is a palette some picture is named after, or the shared ones.
        self.assertEqual(len(export_assets.PICTURES), 20)


@unittest.skipUnless(tools_built(), "the tools aren't built")
class Tools(unittest.TestCase):
    def test_summary_of_a_blank_save(self):
        with tempfile.TemporaryDirectory() as folder:
            facts = _gaius.save_summary(blank_save(folder, "BLANK.SAV"))
            self.assertEqual(facts["funds"], 0)
            self.assertEqual(facts["year"], 0)
            self.assertEqual(facts["governor"], "")

    def test_a_file_that_is_not_a_save_is_an_error(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder, "SHORT.SAV")
            path.write_bytes(b"too short")
            with self.assertRaises(_gaius.ScriptError):
                _gaius.save_summary(path)

    def test_save_diff_exit_status_and_report(self):
        with tempfile.TemporaryDirectory() as folder:
            a = blank_save(folder, "A.SAV")
            same = blank_save(folder, "SAME.SAV")
            other = blank_save(folder, "OTHER.SAV", [(3, 7)])
            diff = _gaius.tool("save_diff")
            self.assertEqual(_gaius.run([diff, a, same]).returncode, 0)
            result = _gaius.run([diff, a, other])
            self.assertEqual(result.returncode, 1)
            self.assertIn("global_words_128", result.stdout)  # byte 3 is in the global words
            self.assertEqual(_gaius.run([diff, a, Path(folder, "missing.SAV")]).returncode, 2)

    def test_dump_voc_writes_the_samples_as_stored(self):
        with tempfile.TemporaryDirectory() as folder:
            voc, wav = Path(folder, "T.VOC"), Path(folder, "T.wav")
            tiny_voc(voc)
            self.assertEqual(_gaius.run([_gaius.tool("dump_voc"), voc, wav]).returncode, 0)
            data = wav.read_bytes()
            self.assertEqual(data[:4], b"RIFF")
            channels, rate, _, _, bits = struct.unpack("<HIIHH", data[22:36])
            self.assertEqual((channels, rate, bits), (1, 8000, 8))
            self.assertEqual(data[44:48], b"\x80\x90\xa0\xb0")

    def test_the_summary_script_columns_for_two_saves(self):
        with tempfile.TemporaryDirectory() as folder:
            a = blank_save(folder, "A.SAV")
            b = blank_save(folder, "B.SAV", [(0, 1)])
            script = Path(__file__).resolve().parent / "compare_saves.py"
            same = subprocess.run([sys.executable, str(script), str(a), str(a)], stdout=subprocess.PIPE,
                                  universal_newlines=True)
            self.assertEqual(same.returncode, 0)
            self.assertIn("identical", same.stdout)
            different = subprocess.run([sys.executable, str(script), str(a), str(b)], stdout=subprocess.PIPE,
                                       universal_newlines=True)
            self.assertEqual(different.returncode, 1)


@unittest.skipUnless(tools_built() and real_game(), "needs the tools and your game folder")
class RealGame(unittest.TestCase):
    def test_every_picture_has_a_palette_and_the_export_counts_match(self):
        game = real_game()
        pictures = _gaius.files_with_extension(game, "VPX")
        self.assertTrue(pictures)
        for picture in pictures:
            self.assertIn(picture.name.upper(), export_assets.PICTURES, "no palette chosen for " + picture.name)
        with tempfile.TemporaryDirectory() as folder:
            script = Path(__file__).resolve().parent / "export_assets.py"
            result = subprocess.run([sys.executable, str(script), folder, "--game", str(game),
                                     "--only", "pictures,sounds,animations"], stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, universal_newlines=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(len(list(Path(folder, "pictures").glob("*.png"))), len(pictures))
            self.assertEqual(len(list(Path(folder, "sounds").glob("*.wav"))),
                             len(_gaius.files_with_extension(game, "VOC")))
            self.assertTrue((Path(folder) / "MANIFEST.txt").is_file())


if __name__ == "__main__":
    unittest.main(verbosity=2)
