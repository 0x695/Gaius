#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""List Caesar saves: who governs, when, how rich, how big, one line each.

    list_saves.py [FILE_OR_FOLDER ...] [--sort date|name]

With no argument it looks in $GAIUS_SAVES, then in Gaius's own saves folder.
Dates are the game's own counters: the year is signed (it starts below zero), the
month runs 1-12."""

import argparse
import os
import sys
from pathlib import Path

from _gaius import ScriptError, find_saves, main_wrapper, month_number, print_table, save_summary


def default_saves_folder():
    if os.name == "nt":
        base = os.environ.get("APPDATA")
        return Path(base) / "Gaius" / "saves" if base else None
    base = os.environ.get("XDG_DATA_HOME") or os.path.join(os.path.expanduser("~"), ".local", "share")
    return Path(base) / "gaius" / "saves"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("saves", nargs="*", help=".SAV files and/or folders of them")
    parser.add_argument("--sort", choices=("date", "name"), default="date", help="order of the list (default: date)")
    options = parser.parse_args()

    places = options.saves
    if not places:
        for candidate in (os.environ.get("GAIUS_SAVES"), default_saves_folder()):
            if candidate and Path(candidate).is_dir():
                places = [candidate]
                break
        if not places:
            raise ScriptError("no saves given; name a .SAV file or a folder of them")

    entries = []
    for path in find_saves(places):
        try:
            entries.append((path, save_summary(path)))
        except ScriptError as error:
            print("skipped: %s" % error, file=sys.stderr)
    if options.sort == "date":
        entries.sort(key=lambda e: (month_number(e[1]), e[0].name.upper()))
    else:
        entries.sort(key=lambda e: e[0].name.upper())

    rows = []
    for path, facts in entries:
        rows.append([path.name, facts["governor"], "%d/%02d" % (facts["year"], facts["month"] + 1),
                     "%d %s" % (facts["rank"], facts["rank_title"]), facts["funds"], facts["population"],
                     facts["forums"], facts["regular centuries"] + facts["irregular centuries"]])
    print_table(rows, ["save", "governor", "year/mo", "rank", "funds", "people", "forums", "centuries"])
    print("\n%d saves" % len(rows))
    return 0


if __name__ == "__main__":
    main_wrapper(main)
