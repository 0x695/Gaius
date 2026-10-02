#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check Gaius's simulation against real saves: for anyone contributing saves.

    check_saves.py FILE_OR_FOLDER ... [--no-months] [--strict]

Two checks, both comparing against what the original engine saved:

  rebuild  sim_check: rebuild the service layers from the save's own city and
           compare them cell for cell with the layers the engine saved.
  month    month_check: for saves of one city a month or two apart, step the
           earlier forward and see whether it reproduces the later (tiles,
           records, population). Saves carry no step counter, so every pair
           within two months of each other is tried, both ways round when
           they're in the same month.

A save written between a month's steps legitimately differs on "rebuild": that
isn't a bug in Gaius or in the save. Report a difference only after checking
the save wasn't taken mid-month (a change in the housing pass, or after step 101).
A save with no neighbour within two months isn't month-checked at all.

Exit status: 0 normally; 1 with --strict if any check differs; 2 on an error."""

import argparse
import re
import sys

from _gaius import ScriptError, find_saves, main_wrapper, month_number, run, save_summary, tool

MAX_GAP = 2  # months: month_check's default is two months of steps


def rebuild_check(path):
    """('match'|'differs', detail) from sim_check."""
    result = run([tool("sim_check"), path])
    if result.returncode == 2:
        raise ScriptError("%s: %s" % (path, (result.stderr or "can't load").strip()))
    detail = re.search(r"land value A2C4: (\d+)/10000", result.stdout)
    return ("match" if result.returncode == 0 else "differs",
            "land value %s/10000 cells" % (detail.group(1) if detail else "?"))


def month_check(earlier, later):
    """(windows, first, last) from month_check: how many start steps reproduce `later`, and the range of
    steps between the saves they imply; None if none does."""
    result = run([tool("month_check"), earlier, later])
    if result.returncode == 2:
        raise ScriptError((result.stderr or "can't load").strip())
    if result.returncode != 0:
        return None
    spans = [(int(m.group(1)), int(m.group(2))) for m in
             re.finditer(r"matches after (\d+)-(\d+) steps", result.stdout)]
    return len(spans), min(a for a, _ in spans), max(b for _, b in spans)


def candidate_pairs(entries):
    """Ordered pairs (earlier, later) of saves up to MAX_GAP months apart. Saves in the same month are tried
    both ways round, since nothing in the file says which was first."""
    pairs = []
    for i, (pa, fa) in enumerate(entries):
        for j, (pb, fb) in enumerate(entries):
            if i == j:
                continue
            gap = month_number(fb) - month_number(fa)
            if 0 <= gap <= MAX_GAP:
                pairs.append((pa, pb))
    return pairs


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("saves", nargs="+", help=".SAV files and/or folders of them")
    parser.add_argument("--no-months", action="store_true", help="only the rebuild check (fast)")
    parser.add_argument("--strict", action="store_true", help="exit 1 if any check differs")
    options = parser.parse_args()

    saves = find_saves(options.saves)
    differing = 0

    print("rebuild: service layers rebuilt from each save's city, against the engine's")
    for path in saves:
        verdict, detail = rebuild_check(path)
        differing += verdict != "match"
        print("  %-8s %-14s %s" % (verdict, path.name, detail))

    if not options.no_months:
        entries = [(p, save_summary(p)) for p in saves]
        pairs = candidate_pairs(entries)
        print("\nmonth: a save stepped forward to the next, for saves up to %d months apart" % MAX_GAP)
        if not pairs:
            print("  no two saves are within %d months of each other, so there is nothing to check" % MAX_GAP)
        matched, partnered = set(), set()
        for earlier, later in pairs:
            partnered.update((earlier, later))
            result = month_check(earlier, later)
            if result is None:
                continue  # an unrelated pair, or the wrong way round: not an error
            matched.update((earlier, later))
            windows, first, last = result
            print("  match    %-14s -> %-14s %d start steps; %d-%d steps apart" %
                  (earlier.name, later.name, windows, first, last))
        alone = sorted(p.name for p in partnered - matched)
        if alone:
            print("  no match for %s (within %d months of a save from another city or session?)" %
                  (", ".join(alone), MAX_GAP))
        if pairs and not matched:
            differing += 1
            print("  none of the %d candidate pairs reproduces" % len(pairs))

    if differing:
        print("\n%d check(s) differ. A save written mid-month differs on 'rebuild' legitimately (see --help)." % differing)
    return 1 if (options.strict and differing) else 0


if __name__ == "__main__":
    main_wrapper(main)
