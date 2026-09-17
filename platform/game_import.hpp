// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — platform/game_import.hpp
//
// Bringing the player's Caesar files to a device that can't simply point at a
// folder. On Android the app can't read another app's or the user's storage
// without a broad permission, so Gaius asks the system's folder picker
// (ACTION_OPEN_DOCUMENT_TREE) and copies the chosen folder's files into its
// own game folder (platform::paths().game), where the rest of Gaius reads them
// like any other folder. The copy runs on a background thread in the activity
// (android/app/src/main/java/org/gaius/game/GaiusActivity.java).
//
// Elsewhere these do nothing: desktop players name the folder on the command
// line, in Settings, or drop it on the window.

#pragma once

#include <string>

namespace gaius::platform {

bool can_import_game_folder();
// Opens the picker; false if it can't be opened.
bool import_game_folder();
// While the copy runs; with the files copied so far.
bool import_in_progress(int* files_copied = nullptr);

// Whether a folder holds the game's files: its scenario files (EMPIRE2.001)
// and the city sprites (HOUSES.PL8), in any letter case.
bool looks_like_game_folder(const std::string& dir);

}  // namespace gaius::platform
