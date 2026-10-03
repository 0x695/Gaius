#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prints the release notes of one version: its section of CHANGELOG.md, without the heading.

    python scripts/release_notes.py 0.9.0
    python scripts/release_notes.py 0.9.0 --changelog path/to/CHANGELOG.md

The section starts at the line "## [0.9.0]" (a date or "unreleased" may follow) and ends at the next "## [".
Exits 1, saying why, when the version has no section or the section is empty. The release workflow uses this for
the body of the GitHub release and fails before building anything if it would be empty.

Python 3.8+, standard library only."""

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def section(text, version):
    """The notes of `version` in a changelog's text, stripped, or None when it has no section."""
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if re.match(r"^##\s+\[" + re.escape(version) + r"\](\s|$)", line):
            start = i + 1
            break
    if start is None:
        return None
    end = len(lines)
    for j in range(start, len(lines)):
        if lines[j].startswith("## ["):
            end = j
            break
    return "\n".join(lines[start:end]).strip()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("version", help="the version, as in the VERSION.txt file (0.9.0)")
    parser.add_argument("--changelog", default=str(REPO / "CHANGELOG.md"))
    args = parser.parse_args(argv)
    try:
        text = Path(args.changelog).read_text(encoding="utf-8")
    except OSError as e:
        print("release_notes: can't read %s: %s" % (args.changelog, e), file=sys.stderr)
        return 1
    notes = section(text, args.version)
    if not notes:
        what = "has no section" if notes is None else "has an empty section"
        print("release_notes: %s %s for %s" % (args.changelog, what, args.version), file=sys.stderr)
        return 1
    print(notes)
    return 0


if __name__ == "__main__":
    sys.exit(main())
