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
#include <vector>

namespace gaius::platform {

bool can_import_game_folder();
// Opens the picker; false if it can't be opened.
bool import_game_folder();
// While the copy runs; with the files copied so far.
bool import_in_progress(int* files_copied = nullptr);

// Whether a folder holds the game's files: its scenario files (EMPIRE2.001)
// and the city sprites (HOUSES.PL8), in any letter case.
bool looks_like_game_folder(const std::string& dir);

// The game's folders in and below `root`, down to `depth` levels (platform/game_detect.cpp): breadth first, the US
// build (a folder named US) ahead of any other.
std::vector<std::string> game_folders_under(const std::string& root, int depth = 3);

// Looks for the game where it is usually installed -- GAIUS_GAME_DIR and GAIUS_TEST_ASSETS, GOG's registry entries and
// default folders, Steam's libraries, the usual game folders on every fixed drive (on Linux: ~/GOG Games, Steam's
// folders and the like) -- and returns every game folder it finds, the likeliest first. Empty if there is none.
std::vector<std::string> detect_game_folders();

// A native "choose a folder" dialog, where the system has one (Windows): the folder picked, or an empty string if
// the player cancelled. can_pick_folder says whether pick_folder can do anything here.
bool can_pick_folder();
std::string pick_folder(const std::string& title);

}  // namespace gaius::platform
