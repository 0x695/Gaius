// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — ui/embedded_lang.hpp
//
// The language files (lang/languages.txt and lang/<code>.txt) built into the program, so a player needs nothing
// beside gaius.exe. CMake writes the table (cmake/embed_languages.cmake) from the files in lang/; a lang/ folder
// next to the executable still wins over it, which is how a translator tries a file without rebuilding.

#pragma once

#include <string>

namespace gaius::ui {

struct EmbeddedLanguageFile {
    const char* name;  // "de.txt"; nullptr ends the table
    const unsigned char* data;
    unsigned long size;
};

extern const EmbeddedLanguageFile kEmbeddedLanguageFiles[];
extern const int kEmbeddedLanguageFileCount;

// The text of the built-in file `name` ("languages.txt", "de.txt"), or an empty string.
std::string embedded_language_file(const std::string& name);

}  // namespace gaius::ui
