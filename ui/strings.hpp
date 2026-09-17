// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — ui/strings.hpp
//
// Localization of Gaius's own text: the Settings screen, the pages Gaius draws
// (the start screen, the save slots, the promotion and ending pages, the
// Forum pages without the original's art), the toolbar hints and the setup
// screen. Text that is the original game's own -- messages, rank and
// province names, the Forum's words, the battle messages -- stays as the US
// executable has it.
//
// The English text is the key: code writes tr("Save the game"), and a
// language file maps it to a translation. Anything a file doesn't translate
// shows in English, so a partial translation works.
//
// A language file is UTF-8 text in lang/, named by its code (de.txt):
//   # language = Deutsch          (the name the Settings screen shows)
//   Save the game = Spiel speichern
// one "English = translation" a line, split at the first " = ". lang/
// template.txt lists every string (scripts/extract_strings.py writes it).
//
// The game's fonts (FONT1.PL8) and Gaius's placeholder font draw ASCII only,
// so a translation's other characters don't show until a font that has them
// is added.

#pragma once

#include <map>
#include <string>
#include <vector>

namespace gaius::ui {

// The translation of `english` in the current language, or `english`.
const char* tr(const char* english);
std::string tr(const std::string& english);

struct Catalog {
    std::string code;   // "de"
    std::string name;   // "Deutsch", from "# language = "; the code if none
    std::map<std::string, std::string> text;
};

Catalog parse_catalog(const std::string& code, const std::string& file_text);

// Makes `catalog` the current language. An empty catalog (or English) means
// no translation.
void set_catalog(Catalog catalog);
const Catalog& current_catalog();

}  // namespace gaius::ui
