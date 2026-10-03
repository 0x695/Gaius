// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — apps/viewer/game_folder_note.hpp
//
// What to tell the player when the folder they chose (or the one Gaius found) cannot be played from: the likely
// mistake is GOG's top folder, which holds the international release of Caesar beside a US folder (platform/
// game_import.hpp, GameFolderStatus). The text is for the setup screen and the Settings screen, whose rows are
// about 38 characters wide, so wrap_note breaks it into lines.
//
// No SDL, so the wording is tested (test_game_folder_note).

#pragma once

#include <string>
#include <vector>

#include "platform/game_import.hpp"
#include "ui/strings.hpp"

namespace gaius::viewer {

// One short paragraph for a folder that is not Usable; empty for one that is.
inline std::string game_folder_note(const platform::GameFolderReport& report) {
    using platform::GameFolderStatus;
    switch (report.status) {
        case GameFolderStatus::Usable: return std::string();
        case GameFolderStatus::NotCaesar: return ui::tr("That folder doesn't hold Caesar's files");
        case GameFolderStatus::International:
            return ui::tr("That is the international release of Caesar. Gaius plays the US release: GOG keeps it in a folder called US.");
        case GameFolderStatus::Incomplete: {
            std::string names;
            for (size_t i = 0; i < report.missing.size() && i < 3; ++i) names += (i ? ", " : "") + report.missing[i];
            if (report.missing.size() > 3)
                names += " " + std::string(ui::tr("and")) + " " + std::to_string(report.missing.size() - 3) + " " +
                         ui::tr("more");
            return std::string(ui::tr("Some of Caesar's files are missing:")) + " " + names;
        }
    }
    return std::string();
}

// `text` broken at spaces into lines of at most `width` characters (a longer word gets a line to itself).
inline std::vector<std::string> wrap_note(const std::string& text, size_t width = 36) {
    std::vector<std::string> lines;
    std::string line, word;
    const auto flush_word = [&]() {
        if (word.empty()) return;
        if (!line.empty() && line.size() + 1 + word.size() > width) {
            lines.push_back(line);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
        word.clear();
    };
    for (const char c : text) {
        if (c == ' ') flush_word();
        else word += c;
    }
    flush_word();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

}  // namespace gaius::viewer
