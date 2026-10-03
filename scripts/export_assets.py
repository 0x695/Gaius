#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Export the pictures, sprites, animations, sounds, music and scenario maps from
a Caesar folder to PNG, WAV and MIDI, using the tools Gaius builds.

    export_assets.py OUT [--game FOLDER] [--only KINDS] [--wav-music]

KINDS is a comma-separated list of: pictures sheets animations sounds music maps.
Everything is read from your own copy of the game and written under OUT; nothing
is changed in the game folder. The files are the game's property, so OUT must not
be inside the Gaius repository (export/ is ignored by git, and is allowed)."""

import argparse
import sys
import tempfile
from pathlib import Path

from _gaius import (ScriptError, files_with_extension, find_file, game_folder, main_wrapper, output_dir, run,
                    tool)

KINDS = ("pictures", "sheets", "animations", "sounds", "music", "maps")

# Which palette each .VPX picture is shown in, with how sure that is. The files
# carry no palette of their own, and the game loads one before it shows each
# picture. DEFINITIVE: the engine's own load, or a DOSBox capture of the screen.
# INFERENCE: the best match by eye; the picture decodes either way.
PICTURES = {
    "EMAP2.VPX": ("EMAP2.P32", "DEFINITIVE: matches the map capture"),
    "NEWFORUM.VPX": ("NEWFORUM.256", "DEFINITIVE: the forum capture's palette"),
    "TEMPLE.VPX": ("TEMPLE.256", "DEFINITIVE: loaded with it by the ratings screen"),
    "WAR2.VPX": ("WAR2.256", "DEFINITIVE: the battle screen"),
    "WARMESS.VPX": ("WAR2.256", "DEFINITIVE: the battle offer, 0x09611"),
    "C_VITAE.VPX": ("SHADE.256", "DEFINITIVE: the governor's page, in the interface palette"),
    "PANEL1.VPX": ("PANEL1.256", "DEFINITIVE: agrees with PANEL1.P32"),
    "TITLE3.VPX": ("TITLE3.256", "DEFINITIVE: renders the title screen"),
    "LOSEPIC.VPX": ("LOSEPIC.P32", "same name"),
    "WINPIC.VPX": ("WINPIC.P32", "same name"),
    "IMPRLOGO.VPX": ("IMPRLOGO.256", "same name"),
    "IMPRSEN.VPX": ("IMPRLOGO.256", "INFERENCE: the same intro sequence as IMPRLOGO"),
    "LOGO.VPX": ("IMPRLOGO.256", "INFERENCE: it uses only the 16 standard colours and a grey, which IMPRLOGO.256 holds"),
    "ROME1.VPX": ("ROME1.256", "same name; the natural-looking match"),
    "ROME2.VPX": ("ROME1.256", "INFERENCE: the same scene as ROME1"),
    "ROME.VPX": ("SHADE.256", "INFERENCE: only colours 0-15, which SHADE, PANEL1 and TEMPLE share"),
    "PANEL1A.VPX": ("SHADE.256", "the city bar's panel (the main bar and the province view); the colours match the DOSBox captures"),
    "PANEL1B.VPX": ("SHADE.256", "UNRESOLVED: the executable never names it"),
    "PANEL1C.VPX": ("SHADE.256", "UNRESOLVED: the executable never names it"),
    "PANEL1D.VPX": ("SHADE.256", "the city bar's panel for the infrastructure and construction pages (0x0FDED)"),
}

# Sprite sheets use the city palette. The battle banners belong to the battle
# screen's. Fonts are drawn by the game in whatever colour it asks for, so they
# are exported as white ink.
SHEET_PALETTE = "SHADE.256"
SHEET_PALETTES = {"SPRITE2X.PL8": "WAR2.256"}
FONTS = {"FONT1.PL8", "FONT2.PL8", "ROMFONT.PL8", "MINIFONT.PL1"}

# (animation, the picture it plays over, that picture's palette): the battle screen's.
ANIMATIONS = (("LOSE0001.VAS", "WAR2.VPX", "WAR2.256"), ("WINS0001.VAS", "WAR2.VPX", "WAR2.256"))


class Report:
    def __init__(self):
        self.manifest = []
        self.failures = []
        self.counts = {}

    def wrote(self, kind, relative, note=""):
        self.counts[kind] = self.counts.get(kind, 0) + 1
        self.manifest.append("%-44s %s" % (relative, note))

    def failed(self, what, why):
        self.failures.append("%s: %s" % (what, why))


def attempt(report, what, args):
    result = run(args)
    if result.returncode != 0:
        lines = (result.stderr or result.stdout).strip().splitlines()
        report.failed(what, lines[-1] if lines else "exit status %d" % result.returncode)
        return False
    return True


def export_pictures(game, out, report):
    folder = output_dir(out / "pictures")
    dump = tool("dump_vpx")
    for picture in files_with_extension(game, "VPX"):
        palette_name, confidence = PICTURES.get(picture.name.upper(), (None, "no palette known: greyscale indices"))
        palette = find_file(game, palette_name) if palette_name else None
        target = folder / (picture.stem + ".png")
        args = [dump, picture, target] + ([palette] if palette else [])
        if attempt(report, picture.name, args):
            note = "%s + %s (%s)" % (picture.name, palette.name if palette else "no palette", confidence)
            report.wrote("pictures", "pictures/" + target.name, note)


def export_sheets(game, out, report):
    folder = output_dir(out / "sheets")
    dump = tool("dump_pl8")
    with tempfile.TemporaryDirectory() as scratch:
        white = Path(scratch) / "white.256"
        white.write_bytes(bytes(3) + bytes([63]) * 765)  # colour 0 black, everything else white (6-bit values)
        for sheet in files_with_extension(game, "PL8", "PL1"):
            name = sheet.name.upper()
            if name in FONTS:
                palette, note = white, "white ink on black; transparent in the frames"
            else:
                palette = find_file(game, SHEET_PALETTES.get(name, SHEET_PALETTE))
                note = "in %s; colour 0 is transparent in the frames" % (palette.name if palette else "greyscale")
            frames = output_dir(folder / sheet.stem)
            args = [dump, sheet, folder / (sheet.stem + ".png")] + ([palette] if palette else [])
            args += ["--cols", "16", "--frames", frames]
            if attempt(report, sheet.name, args):
                report.wrote("sheets", "sheets/%s.png" % sheet.stem, "%s: contact sheet and sheets/%s/frame_NNN.png, %s"
                             % (sheet.name, sheet.stem, note))


def export_animations(game, out, report):
    folder = output_dir(out / "animations")
    dump = tool("dump_vas")
    for name, picture_name, palette_name in ANIMATIONS:
        animation = find_file(game, name)
        picture, palette = find_file(game, picture_name), find_file(game, palette_name)
        if not (animation and picture and palette):
            continue  # a game folder without them (a demo) simply has none
        frames = output_dir(folder / animation.stem)
        if attempt(report, name, [dump, animation, picture, palette, frames / "frame_"]):
            report.wrote("animations", "animations/%s/" % animation.stem,
                         "%s over %s in %s, one PNG a frame" % (name, picture_name, palette_name))


def export_sounds(game, out, report):
    folder = output_dir(out / "sounds")
    dump = tool("dump_voc")
    for voc in files_with_extension(game, "VOC"):
        target = folder / (voc.stem + ".wav")
        if attempt(report, voc.name, [dump, voc, target]):
            report.wrote("sounds", "sounds/" + target.name, "%s as stored: 8-bit mono at its own rate" % voc.name)


def export_music(game, out, report, wav):
    convert = tool("xmi2mid")
    for music in files_with_extension(game, "XMI", "XM2"):
        folder = output_dir(out / "music" / music.suffix.lstrip(".").lower())
        target = folder / (music.stem + ".mid")
        if attempt(report, music.name, [convert, music, target]):
            report.wrote("music", "music/%s/%s" % (music.suffix.lstrip(".").lower(), target.name),
                         "%s as Standard MIDI" % music.name)
    if wav:
        render = tool("render_audio")
        folder = output_dir(out / "music" / "wav")
        for music in files_with_extension(game, "XMI"):
            target = folder / (music.stem + ".wav")
            if attempt(report, music.name + " (wav)", [render, game, music.name, target]):
                report.wrote("music", "music/wav/" + target.name,
                             "%s as the game plays it: the AdLib's YM3812, mono 16-bit 44.1 kHz" % music.name)


def export_maps(game, out, report):
    folder = output_dir(out / "maps")
    view = tool("empire_view")
    scenarios = files_with_extension(game, *("%03d" % n for n in range(50)))
    for scenario in (s for s in scenarios if s.name.upper().startswith("EMPIRE2.")):
        target = folder / (scenario.name.upper() + ".png")
        if attempt(report, scenario.name, [view, scenario, "--png", target]):
            report.wrote("maps", "maps/" + target.name, "%s: the province's map" % scenario.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out", help="folder to write to (created; not inside the repository)")
    parser.add_argument("--game", help="the Caesar folder (default: $GAIUS_GAME, $GAIUS_TEST_ASSETS, Gaius's own)")
    parser.add_argument("--only", help="comma-separated kinds to export: " + " ".join(KINDS))
    parser.add_argument("--wav-music", action="store_true",
                        help="also render each .XMI as the game plays it, to WAV (slow, large)")
    options = parser.parse_args()

    kinds = KINDS
    if options.only:
        kinds = tuple(k.strip() for k in options.only.split(",") if k.strip())
        unknown = [k for k in kinds if k not in KINDS]
        if unknown:
            raise ScriptError("unknown kind %s; choose from %s" % (", ".join(unknown), ", ".join(KINDS)))

    game = game_folder(options.game)
    out = output_dir(options.out)
    report = Report()
    steps = {
        "pictures": lambda: export_pictures(game, out, report),
        "sheets": lambda: export_sheets(game, out, report),
        "animations": lambda: export_animations(game, out, report),
        "sounds": lambda: export_sounds(game, out, report),
        "music": lambda: export_music(game, out, report, options.wav_music),
        "maps": lambda: export_maps(game, out, report),
    }
    for kind in kinds:
        steps[kind]()
        print("%-11s %d written" % (kind, report.counts.get(kind, 0)))

    (out / "MANIFEST.txt").write_text(
        "Exported by scripts/export_assets.py from %s\n"
        "These files are the original game's: for your own use, never to commit or share.\n\n%s\n"
        % (game, "\n".join(report.manifest)), encoding="utf-8")
    print("manifest: %s" % (out / "MANIFEST.txt"))
    for failure in report.failures:
        print("failed  %s" % failure, file=sys.stderr)
    return 1 if report.failures else 0


if __name__ == "__main__":
    main_wrapper(main)
