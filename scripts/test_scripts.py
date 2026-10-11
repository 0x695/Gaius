#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the scripts: their own logic always, the built tools when they
exist, and the real game's files when GAIUS_GAME / GAIUS_TEST_ASSETS names them.
Nothing here needs, or writes, a game file when no folder is named.

    python scripts/test_scripts.py
"""

import contextlib
import io
import os
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import _gaius  # noqa: E402
import check_saves  # noqa: E402
import dosbox_memory  # noqa: E402
import export_assets  # noqa: E402
import record_session  # noqa: E402
import release_notes  # noqa: E402

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

    def test_a_session_keeps_every_save_written(self):
        with tempfile.TemporaryDirectory() as folder:
            saves, archive = Path(folder, "saves"), Path(folder, "archive")
            saves.mkdir()
            blank_save(saves, "OLD.SAV")
            watcher = record_session.SaveWatcher([saves, Path(folder, "not there yet")], archive)
            self.assertEqual(watcher.poll(), [])  # what was there at the start is not a new save
            # A save caught half written waits; whole, it is taken at the look after the one that found it.
            partial = saves / "CAESARXX.SAV"
            partial.write_bytes(bytes(1000))
            self.assertEqual(watcher.poll(), [])
            self.assertEqual(watcher.poll(), [])
            blank_save(saves, "CAESARXX.SAV")
            self.assertEqual(watcher.poll(), [])
            copied = watcher.poll()
            self.assertEqual([source.name for source, _ in copied], ["CAESARXX.SAV"])
            self.assertTrue(copied[0][1].name.endswith("_CAESARXX.SAV"))
            self.assertEqual(copied[0][1].stat().st_size, SAVE_SIZE)
            self.assertEqual(watcher.poll(), [])
            # The same name saved over: a second copy, the first kept.
            blank_save(saves, "CAESARXX.SAV", [(100, 7)])
            status = partial.stat()
            os.utime(str(partial), ns=(status.st_atime_ns, status.st_mtime_ns + 2_000_000_000))
            watcher.poll()
            again = watcher.poll()
            self.assertEqual(len(again), 1)
            self.assertEqual(len(list(archive.iterdir())), 2)
            self.assertEqual(again[0][1].read_bytes()[100], 7)

    def test_a_memory_snapshot_is_laid_out_as_the_save_routine_writes(self):
        import re
        # The two lists of memory places are Gaius's own, in formats/save and model: one truth, held here.
        header = (_gaius.REPO / "formats" / "save" / "save.hpp").read_text(encoding="utf-8")
        table = header[header.index("kGlobalWordDsAddress[128] = {"):]
        words = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{4}", table[:table.index("};")].split("{", 1)[1])]
        self.assertEqual(dosbox_memory.GLOBAL_WORDS, words)
        model = (_gaius.REPO / "model" / "city_state.cpp").read_text(encoding="utf-8")
        table = model[model.index("kFinalStateWords[] = {"):]
        final = [(int(a, 16), int(b)) for a, b in re.findall(r"\{(0x[0-9A-Fa-f]{4}), *(\d+)\}", table[:table.index("};")])]
        ours = [(w, 2 * i) for i, w in enumerate(dosbox_memory.FINAL_HEAD)]
        ours += [(w, 24 + 2 * i) for i, w in enumerate(dosbox_memory.FINAL_TAIL)]
        self.assertEqual(ours, final)
        # The pieces fill the file, and the blocks start where formats::save's table has them.
        sizes = [size for _, size in dosbox_memory.LAYOUT]
        self.assertEqual(sum(sizes), SAVE_SIZE)
        starts = {offset: sum(sizes[:i]) for i, (offset, _) in enumerate(dosbox_memory.LAYOUT)}
        self.assertEqual(starts[dosbox_memory.DS + 0x5D84], 0x0100)   # the actors
        self.assertEqual(starts[dosbox_memory.TILES], 0x13D4)          # the city's tiles
        self.assertEqual(starts[dosbox_memory.FAR + 0x2752], 0xD724)   # the province
        self.assertEqual(starts[dosbox_memory.NAME_PLACE], dosbox_memory.NAME_IN_SAVE)
        self.assertEqual(dosbox_memory.NAME_IN_SAVE, 0xDEE2 + 12)

    def test_a_memory_snapshot_finds_the_game_and_reads_its_save(self):
        import random
        rng = random.Random(7)
        loaded_at = 0x01A2 * 16  # where DOS put the program; the PC's memory begins 0x20 into DOSBox's block
        pc = bytearray(rng.getrandbits(8) for _ in range(loaded_at + dosbox_memory.LAST + 64))
        game = loaded_at
        pc[game + dosbox_memory.NAMES_AT:game + dosbox_memory.NAMES_AT + len(dosbox_memory.NAMES)] = dosbox_memory.NAMES
        pc[game + dosbox_memory.COSTS_AT:game + dosbox_memory.COSTS_AT + len(dosbox_memory.COSTS)] = dosbox_memory.COSTS
        pc[100:100 + len(dosbox_memory.NAMES)] = dosbox_memory.NAMES  # the same words somewhere else: another candidate
        block = 0x0AF3E020
        self.assertEqual(dosbox_memory.candidates(bytes(pc), block),
                         [block + 100 - dosbox_memory.NAMES_AT, block + game])
        stretch = bytes(pc[game + dosbox_memory.FIRST:game + dosbox_memory.LAST])
        save = dosbox_memory.assemble_from(stretch)
        self.assertEqual(len(save), SAVE_SIZE)
        # The first word is DS:0x6CE2, the tiles are segment 43A5, the last word DS:0x6BE6.
        at = game + dosbox_memory.DS
        self.assertEqual(save[:2], bytes(pc[at + 0x6CE2:at + 0x6CE4]))
        self.assertEqual(save[0x13D4:0x13D4 + 10000], bytes(pc[game + dosbox_memory.TILES:game + dosbox_memory.TILES + 10000]))
        self.assertEqual(save[-2:], bytes(pc[at + 0x6BE6:at + 0x6BE8]))
        self.assertEqual(save[0xC2:0xC4], save[0xDC:0xDE])  # the word the routine writes twice
        # The governor's name is put in from where the routine takes it; the rest is untouched.
        named = dosbox_memory.assemble_from(stretch, b"  Octavian  ")
        self.assertEqual(named[0xDEEE:0xDEFA], b"  Octavian  ")
        self.assertEqual(named[:0xDEEE] + named[0xDEFA:], save[:0xDEEE] + save[0xDEFA:])
        self.assertIsNone(dosbox_memory.assemble_from(stretch[:-1]))  # memory that could not all be read: no save
        # Before a city is begun the tiles are empty.
        self.assertFalse(dosbox_memory.city_present(bytes(SAVE_SIZE)))
        self.assertTrue(dosbox_memory.city_present(bytes([0x1D]) * SAVE_SIZE))

    def test_a_session_keeps_the_originals_state_from_memory(self):
        class Box:
            problem = "DOSBox is not running"

            def __init__(self):
                self.next = (None, False)

            def snapshot(self):
                return self.next

        city, later = bytes([0x1D]) * SAVE_SIZE, bytes([0x1E]) * SAVE_SIZE
        with tempfile.TemporaryDirectory() as folder:
            box = Box()
            memory = record_session.MemorySaves(box, Path(folder, "saves"), 60.0)
            self.assertIsNone(memory.poll(1000.0))  # no game yet: nothing, and another look within seconds
            self.assertEqual(memory.problem, "DOSBox is not running")
            self.assertIsNone(memory.poll(1002.0))
            box.next = (bytes(SAVE_SIZE), True)      # the game's start screen: no city, nothing kept
            self.assertIsNone(memory.poll(1006.0))
            box.next = (city, True)
            self.assertIsNone(memory.poll(1010.0))   # not a minute later yet
            copy, still = memory.poll(1066.0)
            self.assertTrue(still and copy.name.endswith("_MEMORY.SAV"))
            self.assertEqual(copy.read_bytes(), city)
            self.assertIsNone(memory.poll(1126.0))   # the game paused: the same state is not kept twice
            box.next = (later, False)                # caught while the game was changing it: kept, and said
            copy, still = memory.poll(1186.0)
            self.assertFalse(still)
            self.assertEqual(copy.read_bytes(), later)
            self.assertEqual((memory.kept, len(list(Path(folder, "saves").iterdir()))), (2, 2))

    def test_a_session_gathers_the_pictures(self):
        with tempfile.TemporaryDirectory() as folder:
            source, target = Path(folder, "capture"), Path(folder, "screens")
            mover = record_session.PictureMover([Path(folder, "nowhere"), source], target)
            self.assertEqual(mover.poll(), 0)  # no capture folder yet
            source.mkdir()
            old = time.time() - 5
            for name in ("csr_000.png", "csr_001.png", "csr_000.avi"):
                Path(source, name).write_bytes(b"x")
                os.utime(str(Path(source, name)), (old, old))
            Path(source, "csr_002.png").write_bytes(b"x")  # just written: left for the next look
            self.assertEqual(mover.poll(), 2)
            self.assertEqual(sorted(p.name for p in source.iterdir()), ["csr_000.avi", "csr_002.png"])
            names = sorted(p.name for p in target.iterdir())
            self.assertEqual(len(names), 2)  # the same second: the second takes a number
            self.assertTrue(names[0].endswith("_screen.png") and names[1].endswith("_screen_2.png"), names)

    def test_dosbox_is_asked_for_a_picture_only_once_the_game_runs(self):
        # DOSBox 0.74 dies when asked for a screenshot before its first frame; its title says when a program runs.
        program = record_session.DosboxKey.program_in_title
        self.assertEqual(program("DOSBox"), "")  # the splash
        self.assertEqual(program("DOSBox 0.74-2.1, Cpu speed:     3000 cycles, Frameskip  0, Program:   DOSBOX"), "")
        self.assertEqual(program("DOSBox 0.74-2.1, Cpu speed:     3000 cycles, Frameskip  0, Program:      CSR"), "CSR")
        self.assertEqual(program("DOSBox 0.74-2.1, Cpu speed: max 100% cycles, Frameskip  0, Program:      CSR"), "CSR")
        self.assertEqual(program(""), "")

    def test_a_picture_the_game_still_holds_is_not_taken_twice(self):
        with tempfile.TemporaryDirectory() as folder:
            source, target = Path(folder, "capture"), Path(folder, "screens")
            source.mkdir()
            old = time.time() - 5
            Path(source, "csr_000.png").write_bytes(b"")  # DOSBox's failed screenshot: empty, and left open
            os.utime(str(Path(source, "csr_000.png")), (old, old))
            mover = record_session.PictureMover([source], target)
            for _ in range(3):
                self.assertEqual(mover.poll(), 0)
            self.assertFalse(target.exists() and list(target.iterdir()))

    def test_the_video_films_the_games_windows_and_no_microphone(self):
        import json
        profile = record_session.obs_profile(Path("E:/sessions/2026-10-09/video"))
        self.assertIn("FilePath=E:/sessions/2026-10-09/video", profile)  # forward slashes: OBS's own form
        self.assertIn("RecFormat2=mkv", profile)
        self.assertIn('OBSBasic.StopRecording={"bindings":[{"key":"OBS_KEY_F23"}]}', profile)
        scenes = json.loads(record_session.obs_scenes())
        captures = [s for s in scenes["sources"] if s["id"] == "window_capture"]
        self.assertEqual(sorted(s["settings"]["window"].split(":")[2] for s in captures),
                         ["DOSBox.exe", "gaius_viewer.exe"])
        self.assertTrue(all(s["settings"]["priority"] == 2 for s in captures))  # found by program, not title
        # Each game's own sound is the only audio: no source records the desktop's sound (whatever else the PC
        # plays) or the microphone, and none films the screen.
        kinds = {s["id"] for s in scenes["sources"]} | {v["id"] for v in scenes.values() if isinstance(v, dict)}
        self.assertEqual(kinds, {"window_capture", "scene", "wasapi_process_output_capture"})
        self.assertFalse([name for name in scenes if "AudioDevice" in name])
        sounds = [s for s in scenes["sources"] if s["id"] == "wasapi_process_output_capture"]
        self.assertEqual([s["settings"]["window"] for s in sounds], [s["settings"]["window"] for s in captures])
        self.assertTrue(all(s["settings"]["priority"] == 2 and s["mixers"] for s in sounds))
        items = next(s for s in scenes["sources"] if s["id"] == "scene")["settings"]["items"]
        self.assertEqual([i["name"] for i in items], [s["name"] for s in captures + sounds])
        self.assertEqual(len({i["id"] for i in items}), len(items))

    def test_the_originals_saves_are_looked_for_under_the_overlay_first(self):
        folders = record_session.original_save_folders(Path("C:/Caesar"))
        self.assertEqual(folders[0], Path("C:/Caesar/cloud_saves/US"))
        self.assertIn(Path("C:/Caesar/US"), folders)


@unittest.skipUnless(tools_built(), "the tools aren't built")
class Release(unittest.TestCase):
    CHANGELOG = (
        "# Changelog\n\n## [Unreleased]\n\n## [1.2.0] - 2027-01-02\n\n### Added\n- a thing\n- another\n\n"
        "## [1.1.0] - 2026-12-01\n- older\n\n## [1.0.0] - unreleased\n")

    def test_section_is_the_versions_own(self):
        self.assertEqual(release_notes.section(self.CHANGELOG, "1.2.0"), "### Added\n- a thing\n- another")
        self.assertEqual(release_notes.section(self.CHANGELOG, "1.1.0"), "- older")

    def test_missing_and_empty_sections(self):
        self.assertIsNone(release_notes.section(self.CHANGELOG, "9.9.9"))
        self.assertEqual(release_notes.section(self.CHANGELOG, "1.0.0"), "")
        self.assertEqual(release_notes.section(self.CHANGELOG, "1.2"), None)  # a prefix is not the version

    def test_command_exit_codes(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "CHANGELOG.md"
            path.write_text(self.CHANGELOG, encoding="utf-8")
            quiet = io.StringIO()
            with contextlib.redirect_stdout(quiet), contextlib.redirect_stderr(quiet):
                self.assertEqual(release_notes.main(["1.2.0", "--changelog", str(path)]), 0)
                self.assertEqual(release_notes.main(["1.0.0", "--changelog", str(path)]), 1)
                self.assertEqual(release_notes.main(["3.0.0", "--changelog", str(path)]), 1)
                self.assertEqual(release_notes.main(["1.2.0", "--changelog", str(path) + ".nope"]), 1)
            self.assertIn("- a thing", quiet.getvalue())

    def test_version_file_and_changelog_agree(self):
        # The release workflow refuses a tag that is not v<VERSION> or a version with no notes; catch it earlier.
        version = (_gaius.REPO / "VERSION.txt").read_text(encoding="utf-8").strip()
        self.assertRegex(version, r"^\d+\.\d+\.\d+$")
        notes = release_notes.section((_gaius.REPO / "CHANGELOG.md").read_text(encoding="utf-8"), version)
        self.assertTrue(notes, "CHANGELOG.md needs a '## [%s]' section with notes" % version)


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
