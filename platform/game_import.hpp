// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — platform/game_import.hpp
//
// Bringing the player's Caesar files to a device that can't simply point at a
// folder. On Android the app can't read another app's or the user's storage
// without a broad permission, so Gaius asks the system's folder picker
// (ACTION_OPEN_DOCUMENT_TREE) and copies the chosen folder's files into its
// own game folder (platform::paths().game), where the rest of Gaius reads them
// like any other folder. The activity looks in the chosen folder for the one that
// holds Caesar's US files (GOG's top folder holds it in a folder called US) and
// copies only that, so the player may choose either. The copy runs on a background
// thread in the activity (android/app/src/main/java/org/gaius/game/GaiusActivity.java).
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
// How the last import ended (None: none yet, or the picker was dismissed).
enum class ImportResult {
    None,
    Done,      // the game's files were copied
    NotFound,  // the chosen folder holds no complete Caesar US release
    Failed,    // reading or writing failed part-way
};
ImportResult import_result();

// What a folder is, as a place for Gaius to play from. Gaius works from the US release of Caesar (the one every finding
// was made against). GOG's download keeps it in a folder called US, beside the files of an international release
// (several languages, music as .MDI files, a different executable) that Gaius cannot play from: it lacks files the US
// release has, among them SHADE.256, the city's palette. Pointing Gaius at GOG's top folder is the likely mistake,
// so the report says which of these it is.
enum class GameFolderStatus {
    NotCaesar,      // none of Caesar's files
    International,  // Caesar's files, but the international release
    Incomplete,     // Caesar's files, some Gaius needs missing (the US release with files lost, or another version)
    Usable,         // every file Gaius cannot play without is there
};

struct GameFolderReport {
    GameFolderStatus status = GameFolderStatus::NotCaesar;
    std::vector<std::string> missing;  // the essential files not found, in upper case (empty when Usable)
};

// The files Gaius cannot play without: the first province's map, the city and walker sprites, the city palette and the
// game's font. All are looked for in any letter case.
extern const char* const kEssentialGameFiles[];
extern const int kEssentialGameFileCount;

GameFolderReport inspect_game_folder(const std::string& dir);

// Whether a folder is one Gaius can play from (inspect_game_folder says Usable).
bool looks_like_game_folder(const std::string& dir);

// The usable game folders in and below `root`, down to `depth` levels (platform/game_detect.cpp): breadth first, the US
// build (a folder named US) ahead of any other. GOG's top folder is not one of them -- its US folder is.
std::vector<std::string> game_folders_under(const std::string& root, int depth = 3);

// The first of `dirs` that is a usable game folder, else the first usable folder below one of them (depth levels down; the
// US build ahead of the others): where Gaius looks in the settings' folder and in its own game folder, so a GOG top folder
// copied whole, or imported from a phone, plays from the US folder inside it. Empty if there is none.
std::string first_game_folder(const std::vector<std::string>& dirs, int depth = 3);

// Looks for the game where it is usually installed -- GAIUS_GAME_DIR and GAIUS_TEST_ASSETS, GOG's registry entries and
// default folders, Steam's libraries, the usual game folders on every fixed drive (on Linux: ~/GOG Games, Steam's
// folders and the like) -- and returns every game folder it finds, the likeliest first. Empty if there is none.
std::vector<std::string> detect_game_folders();

// A native "choose a folder" dialog, where the system has one (Windows): the folder picked, or an empty string if
// the player cancelled. can_pick_folder says whether pick_folder can do anything here.
bool can_pick_folder();
std::string pick_folder(const std::string& title);

}  // namespace gaius::platform
