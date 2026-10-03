// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — formats/common/game_files.hpp
//
// Finding one of the game's files in the player's folder whatever letter case it has on disk. The original is a DOS
// game: its files are HOUSES.PL8 and EMPIRE2.001, but a copy that has passed through an installer, an unzip or a
// download is often houses.pl8, Houses.Pl8 or Empire2.001, and on Linux (a case-sensitive file system) the name has
// to match exactly. Every loader goes through game_file_path, so a folder in any case works on every system.

#pragma once

#include <string>

namespace gaius::formats {

// The path of the file `name` (written as the original does, in upper case) in `dir`, with the letter case it really
// has. The exact name is tried first (which settles it on a case-insensitive file system), then the folder is
// searched for a name that differs only in ASCII letter case. A file that isn't there gives dir/name, so the error
// that follows names the file the game wanted.
std::string game_file_path(const std::string& dir, const std::string& name);

bool game_file_exists(const std::string& dir, const std::string& name);

// The search alone: the name of the entry of `dir` that equals `name` ignoring ASCII letter case, or empty if there is
// none. Public so a test can check it on a file system that would have found the file anyway.
std::string find_name_ignoring_case(const std::string& dir, const std::string& name);

}  // namespace gaius::formats
