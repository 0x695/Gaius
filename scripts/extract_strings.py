#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Lists every text Gaius translates (each ui::tr("...") and tr("...") in the
sources) into lang/template.txt, the starting point for a language file.

    python scripts/extract_strings.py            # writes lang/template.txt
    python scripts/extract_strings.py --check lang/de.txt
                                                 # lists what de.txt lacks

A language file is "English = translation" lines (ui/strings.hpp). Texts
named through a table (tr(kTabs[i]) and the like) are found by their table's
literals where the table sits in the same file, listed in TABLES below.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = ["apps", "ui", "platform"]
# Tables whose entries go through tr(): file -> the table's name.
TABLES = {
    "apps/viewer/screens.hpp": ["kTabs", "kAsk"],
    "apps/viewer/settings_page.hpp": ["kTabs"],
    "apps/viewer/main.cpp": ["kProvinceCommands"],
    "apps/viewer/save_view.hpp": ["kOverlayNames"],
}
# Functions whose returned literals go through tr() by their callers.
FUNCTIONS = {
    "apps/viewer/settings_page.hpp": ["window_mode_name", "binding_label"],
}

CALL = re.compile(r'\btr\(\s*(?:[^;"()]*\?\s*)?"((?:[^"\\]|\\.)*)"(?:\s*:\s*"((?:[^"\\]|\\.)*)")?')


def strings_in(path, text):
    found = []
    for m in CALL.finditer(text):
        found += [g for g in m.groups() if g]
    rel = path.relative_to(ROOT).as_posix()
    for table in TABLES.get(rel, []):
        m = re.search(table + r"\[\]\s*=\s*\{(.*?)\};", text, re.S)
        if m:
            found += re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    for function in FUNCTIONS.get(rel, []):
        m = re.search(function + r"\(.*?\{(.*?)\n\}", text, re.S)
        if m:
            found += [s for s in re.findall(r'return "((?:[^"\\]|\\.)*)"', m.group(1))]
    return [s for s in found if s.strip() and s.strip() != "?"]


def all_strings():
    seen = []
    for folder in SOURCES:
        for path in sorted((ROOT / folder).rglob("*")):
            if path.suffix not in (".cpp", ".hpp"):
                continue
            for s in strings_in(path, path.read_text(encoding="utf-8")):
                if s not in seen:
                    seen.append(s)
    return seen


def main():
    strings = all_strings()
    if len(sys.argv) == 3 and sys.argv[1] == "--check":
        have = set()
        for line in pathlib.Path(sys.argv[2]).read_text(encoding="utf-8-sig").splitlines():
            if " = " in line and not line.lstrip().startswith("#"):
                have.add(line.split(" = ", 1)[0].strip())
        missing = [s for s in strings if s not in have]
        for s in missing:
            print(s)
        print(f"{len(missing)} of {len(strings)} not translated", file=sys.stderr)
        return 0
    out = ROOT / "lang" / "template.txt"
    lines = [
        "# language = (the language's name, in itself)",
        "# Gaius's texts, one \"English = translation\" a line. Copy this file to",
        "# lang/<code>.txt, translate what follows each \" = \", and add the code to",
        "# lang/languages.txt. Lines left untranslated show in English.",
        "# Written by scripts/extract_strings.py; don't edit it by hand.",
        "",
    ]
    lines += [f"{s} = " for s in strings]
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{out.relative_to(ROOT)}: {len(strings)} texts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
