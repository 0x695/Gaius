#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw the city in each save to a PNG, with the game's own sprites.

    render_saves.py FILE_OR_FOLDER ... --out OUT [--game FOLDER] [--steps N]
                    [--region COL ROW COLS ROWS]

One PNG per save, named after it, in OUT. --steps runs N simulation steps first
(so you can watch a city grow: Gaius's simulation, not the engine's saved state);
--region draws only a part of the map, in tiles. The pictures come from your
Caesar folder (--game, $GAIUS_GAME, $GAIUS_TEST_ASSETS), so OUT must be outside
the repository, or under export/."""

import argparse
import sys

from _gaius import (ScriptError, find_saves, game_folder, main_wrapper, output_dir, run, tool)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("saves", nargs="+", help=".SAV files and/or folders of them")
    parser.add_argument("--out", required=True, help="folder for the PNGs")
    parser.add_argument("--game", help="the Caesar folder")
    parser.add_argument("--steps", type=int, default=0, help="simulation steps to run before drawing")
    parser.add_argument("--region", nargs=4, type=int, metavar=("COL", "ROW", "COLS", "ROWS"),
                        help="draw only this part of the map")
    options = parser.parse_args()

    game = game_folder(options.game)
    out = output_dir(options.out)
    render = tool("render_city")
    failures = 0
    for save in find_saves(options.saves):
        target = out / (save.stem + ".png")
        args = [render, save, game, target]
        if options.region:
            args += [str(n) for n in options.region]
        if options.steps:
            args += ["--steps", str(options.steps)]
        result = run(args)
        if result.returncode != 0:
            failures += 1
            lines = (result.stderr or result.stdout).strip().splitlines()
            print("failed  %s: %s" % (save.name, lines[-1] if lines else result.returncode), file=sys.stderr)
        else:
            print("%s -> %s" % (save.name, target))
    return 1 if failures else 0


if __name__ == "__main__":
    main_wrapper(main)
