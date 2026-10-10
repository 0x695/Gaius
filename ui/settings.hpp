// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — ui/settings.hpp
//
// Gaius's own settings, the ones the original doesn't have: the window, the UI
// scale, the frame rate, volumes, the language, the controls and where the
// game's files are. They live in gaius.cfg in the settings folder
// (platform::paths), one "key = value" a line; the original's options stay in
// CAESAR.INF (ui::GameOptions) beside it. Both are shown together on the
// Settings screen (apps/viewer/settings_page.hpp).
//
// The file is forgiving: unknown keys and bad values are ignored (the
// default stays), so an older or newer Gaius can read it.

#pragma once

#include <map>
#include <string>

namespace gaius::ui {

enum class WindowModeSetting { Windowed = 0, Borderless = 1, Fullscreen = 2 };

// The mouse pointer: Gaius's own (a gold arrow the system draws, sharp at any window size) or the original's
// (POINTERS.PL8 frame 0, drawn into the picture; needs the game's files, and Gaius's shows without them).
enum class CursorStyle { Gaius = 0, Original = 1 };

// How fast the simulation's frames run. The original's main loop is bound by its CPU: measured on the GOG release's
// DOSBox (3000 cycles, the setting its window title shows) a game year takes about 85 s at the top game speed, a
// frame every 66 ms (every frame steps at speed 100, and a month is 106 steps). "Original" keeps that pace -- the same barbarians, fires and taxes per minute of play; "Fast" is
// Gaius's earlier pace of a frame every 19 ms, three and a half times quicker.
enum class GamePace { Original = 0, Fast = 1 };
inline constexpr int kFrameMsOriginal = 66, kFrameMsFast = 19;

// The file's layout version, written as config_version. Bump it when a key is renamed or changes meaning and add the
// conversion to migrate_settings (settings.cpp); adding a key needs neither, since a missing key keeps its default.
inline constexpr int kConfigVersion = 1;

// How often the game saves by itself, in game years (0 is never): the "Autosave" row of the Settings screen. The saves
// go to AUTOSAV1-3.SAV in turn (apps/viewer/save_slots.hpp), apart from the eight slots the player writes.
inline constexpr int kAutosaveYears[] = {0, 1, 3, 5};

struct Settings {
    WindowModeSetting window_mode = WindowModeSetting::Windowed;
    int ui_scale = 0;      // 0 automatic, 1-4 the toolbar and page scale
    int frame_cap = 0;     // frames a second: 0 the display's refresh (vsync), else 30, 60, 120 or 144
    int music_volume = 100;    // 0-100, in tens
    int effects_volume = 100;  // 0-100, in tens
    std::string language = "en";
    std::string game_dir;  // empty: look in the usual places
    CursorStyle cursor = CursorStyle::Gaius;
    GamePace pace = GamePace::Original;
    bool gamepad_cursor = true;  // a pointer moved by the left stick on screens without a map
    bool edge_scroll = true;     // the map scrolls when the mouse rests at the window's edge (arrow keys and WASD always do)
    bool touch_hints_seen = false;  // the touch controls page has been shown once
    bool pad_hints_seen = false;    // so has the controller page (a gamepad was connected)
    int autosave_years = 1;         // one of kAutosaveYears
    bool pause_unfocused = true;    // time stops while the window or browser tab is not the one in front
    // The Tribune of the Plebs looks after itself (apps/viewer/tribune_assist.hpp): each month the duties that keep fires,
    // collapses and road wear away are staffed to what the city needs, and welfare keeps the plebs coming. Off is the
    // original's rule, where the player does it by hand on the Tribune's page.
    bool tribune_auto = true;
    // The tutorial (apps/viewer/tutorial.hpp) guides a career's first rank: on until it has been played through or
    // skipped, and the start screen and Settings > Game turn it on again. tutorial_step is the step it has reached,
    // kept here because the save file is the original's and has no room for it.
    bool tutorial = true;
    int tutorial_step = 0;
    // The config_version the file carried when it was read (0: a file from before versions, or none). Settings made
    // by the program always have kConfigVersion.
    int file_version = kConfigVersion;
    // Keys this build doesn't know, from a newer Gaius's file: kept and written back, so going back to an older
    // build and forward again does not lose them.
    std::map<std::string, std::string> unknown;
    // Controls: a command's name (platform::command_name) to a key name
    // (SDL_GetKeyName) or a gamepad button name (SDL's), for the commands the
    // player has changed. Commands not listed keep their default.
    std::map<std::string, std::string> keys;
    std::map<std::string, std::string> buttons;
};

inline constexpr int kFrameCaps[] = {0, 30, 60, 120, 144};

bool load_settings(const std::string& path, Settings& out);
// Written to a temporary file and renamed into place, so a crash part-way leaves the old file.
bool save_settings(const std::string& path, const Settings& settings);

// The text form, for the file and the tests.
std::string settings_text(const Settings& settings);
Settings parse_settings(const std::string& text);

}  // namespace gaius::ui
