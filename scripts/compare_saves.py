#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare two Caesar saves.

    compare_saves.py A.SAV B.SAV [--full] [--cells N]

By default a short table of what differs between the two cities: the date, the
funds, the people, the ratings, the Legion. --full runs save_diff for the whole
account instead: bytes per saved block, every global word that changed, how many
cells of each of the five city layers changed, and which walkers did. --cells N
adds the first N changed cells of each layer to that.

Exit status: 0 if the saves are identical, 1 if they differ, 2 on an error."""

import argparse
import sys

from _gaius import ScriptError, find_saves, main_wrapper, print_table, run, save_summary, tool


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("a", help="the first save")
    parser.add_argument("b", help="the second save")
    parser.add_argument("--full", action="store_true", help="the whole account (save_diff)")
    parser.add_argument("--cells", type=int, default=0, metavar="N", help="with --full: list N changed cells a layer")
    options = parser.parse_args()
    a, b = find_saves([options.a])[0], find_saves([options.b])[0]

    if options.full:
        args = [tool("save_diff"), a, b]
        if options.cells:
            args += ["--cells", str(options.cells)]
        result = run(args)
        sys.stdout.write(result.stdout)
        if result.returncode == 2:
            raise ScriptError((result.stderr or "can't compare these files").strip())
        return result.returncode

    fa, fb = save_summary(a), save_summary(b)
    rows = []
    for key in fa:
        if key == "file":
            continue
        va, vb = fa[key], fb.get(key, "")
        if va == vb:
            continue
        delta = "%+d" % (vb - va) if isinstance(va, int) and isinstance(vb, int) else ""
        rows.append([key, va, vb, delta])
    # The exit status is the whole file's: the summary is only a handful of facts.
    status = run([tool("save_diff"), a, b]).returncode
    if status == 2:
        raise ScriptError("can't compare these files")
    if rows:
        print_table(rows, ["", a.name, b.name, "change"])
    elif status == 0:
        print("The saves are identical.")
    else:
        print("The saves agree on everything in the summary, but differ elsewhere (--full shows where).")
    return status


if __name__ == "__main__":
    main_wrapper(main)
