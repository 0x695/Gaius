# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared helpers for the Gaius scripts: finding the built tools, the player's
game folder and saves, and keeping exported game art out of the repository.

Python 3.8+, standard library only."""

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
EXE = ".exe" if os.name == "nt" else ""

# Build folders to look in, after $GAIUS_TOOLS: a multi-config generator (Visual
# Studio) puts the tools in build/<Config>, a single-config one in build/.
BUILD_DIRS = ("build/RelWithDebInfo", "build/Release", "build/MinSizeRel", "build/Debug", "build")


class ScriptError(Exception):
    """Something the user can fix: a missing tool, a wrong folder, a bad save."""


def tool(name):
    """The path of a built command-line tool (tools/<name>.cpp)."""
    folders = []
    if os.environ.get("GAIUS_TOOLS"):
        folders.append(Path(os.environ["GAIUS_TOOLS"]))
    folders += [REPO / d for d in BUILD_DIRS]
    for folder in folders:
        candidate = folder / (name + EXE)
        if candidate.is_file():
            return candidate
    found = shutil.which(name)
    if found:
        return Path(found)
    raise ScriptError(
        "can't find the tool '%s'. Build the project first (see README.md: cmake --build build), "
        "or set GAIUS_TOOLS to the folder that holds the tools." % name
    )


def run(args, check=False):
    """Run a command, capturing its output as text. Returns the CompletedProcess."""
    result = subprocess.run([str(a) for a in args], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            universal_newlines=True)
    if check and result.returncode != 0:
        message = (result.stderr or result.stdout).strip().splitlines()
        raise ScriptError("%s failed: %s" % (Path(str(args[0])).name, message[-1] if message else result.returncode))
    return result


# --- Case-insensitive files ---------------------------------------------------
# The game's files are upper case, but copies made on Linux or by archive tools
# often aren't. Names are matched without regard to case.

def listing(folder):
    try:
        return {entry.name.upper(): Path(entry.path) for entry in os.scandir(str(folder)) if entry.is_file()}
    except OSError:
        return {}


def find_file(folder, name):
    """folder/name, ignoring case, or None."""
    return listing(folder).get(name.upper())


def files_with_extension(folder, *extensions):
    """Every file in folder with one of the extensions (no dot, any case), sorted by name."""
    wanted = {"." + e.upper().lstrip(".") for e in extensions}
    return sorted((p for n, p in listing(folder).items() if Path(n).suffix in wanted), key=lambda p: p.name.upper())


# --- The game folder ------------------------------------------------------------

def default_game_folder():
    """Where Gaius itself keeps the game's files (platform/paths.cpp)."""
    if os.name == "nt":
        base = os.environ.get("APPDATA")
        return Path(base) / "Gaius" / "game" if base else None
    base = os.environ.get("XDG_DATA_HOME") or os.path.join(os.path.expanduser("~"), ".local", "share")
    return Path(base) / "gaius" / "game"


def is_game_folder(folder):
    return folder is not None and find_file(folder, "CSR.EXE") is not None


def game_folder(argument=None):
    """The player's Caesar folder: the argument, else $GAIUS_GAME, $GAIUS_TEST_ASSETS, Gaius's own folder.
    An argument that isn't a game folder is an error rather than a reason to look elsewhere."""
    if argument:
        folder = Path(argument)
        if not is_game_folder(folder):
            raise ScriptError("%s doesn't look like a Caesar folder (no CSR.EXE in it)" % folder)
        return folder
    for candidate in (os.environ.get("GAIUS_GAME"), os.environ.get("GAIUS_TEST_ASSETS"), default_game_folder()):
        if candidate and is_game_folder(Path(candidate)):
            return Path(candidate)
    raise ScriptError(
        "no Caesar folder found. Pass --game <folder>, or set GAIUS_GAME to the folder that holds CSR.EXE "
        "(your own copy: no game files are part of this project)."
    )


# --- Output folders ---------------------------------------------------------------

def output_dir(path):
    """Create and return an output folder for game art or sound.

    CLAUDE.md's first rule: no original game assets in the repository. Pictures
    and sounds exported from a player's copy are exactly that, so a folder inside
    the repository is refused unless git ignores it (export/ is ignored)."""
    out = Path(path).resolve()
    try:
        out.relative_to(REPO)
    except ValueError:
        inside = False
    else:
        inside = True
    if inside:
        try:
            ignored = subprocess.run(["git", "-C", str(REPO), "check-ignore", "-q", str(out / "x")],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        except OSError:
            ignored = False
        if not ignored:
            raise ScriptError(
                "%s is inside the repository and git doesn't ignore it. Exported game files must never be "
                "committed: choose a folder outside the repository, or one under export/." % out
            )
    out.mkdir(parents=True, exist_ok=True)
    return out


# --- Saves ---------------------------------------------------------------------------

def find_saves(paths):
    """The .SAV files named by a list of files and folders, in the order given (a folder's by name)."""
    saves = []
    for entry in paths:
        entry = Path(entry)
        if entry.is_dir():
            saves += files_with_extension(entry, "SAV")
        elif entry.is_file():
            saves.append(entry)
        else:
            raise ScriptError("%s: no such file or folder" % entry)
    if not saves:
        raise ScriptError("no .SAV files found")
    return saves


def parse_summary(text):
    """save_inspect --summary prints one 'key: value' line per fact."""
    facts = {}
    for line in text.splitlines():
        key, sep, value = line.partition(": ")
        if sep:
            facts[key.strip()] = value.strip()
    return facts


def save_summary(path):
    """The facts about one save (save_inspect --summary), numbers as ints."""
    result = run([tool("save_inspect"), "--summary", path])
    if result.returncode != 0:
        raise ScriptError("%s: %s" % (path, (result.stderr or "not a Caesar save").strip()))
    facts = parse_summary(result.stdout)
    for key, value in list(facts.items()):
        if re.fullmatch(r"-?\d+", value):
            facts[key] = int(value)
    return facts


def month_number(facts):
    """The save's date as one number of months, so dates can be compared and subtracted.
    The year is a signed counter (the first session's saves run -11 to -1) and the month 0-11."""
    return facts["year"] * 12 + facts["month"]


def describe_date(facts):
    return "year %d, month %d" % (facts["year"], facts["month"] + 1)


def print_table(rows, header):
    """Print rows (lists of strings) under a header, columns padded to fit."""
    widths = [max(len(str(r[i])) for r in [header] + rows) for i in range(len(header))]
    for row in [header] + rows:
        print("  ".join(str(cell).ljust(widths[i]) for i, cell in enumerate(row)).rstrip())


def main_wrapper(main):
    """Run a script's main(), turning ScriptError into a message and exit status 2."""
    try:
        sys.exit(main())
    except ScriptError as error:
        print("error: %s" % error, file=sys.stderr)
        sys.exit(2)
    except KeyboardInterrupt:
        sys.exit(130)
