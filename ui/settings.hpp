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

struct Settings {
    WindowModeSetting window_mode = WindowModeSetting::Windowed;
    int ui_scale = 0;      // 0 automatic, 1-4 the toolbar and page scale
    int frame_cap = 0;     // frames a second: 0 the display's refresh (vsync), else 30, 60, 120 or 144
    int music_volume = 100;    // 0-100, in tens
    int effects_volume = 100;  // 0-100, in tens
    std::string language = "en";
    std::string game_dir;  // empty: look in the usual places
    bool gamepad_cursor = true;  // a pointer moved by the left stick on screens without a map
    // Controls: a command's name (platform::command_name) to a key name
    // (SDL_GetKeyName) or a gamepad button name (SDL's), for the commands the
    // player has changed. Commands not listed keep their default.
    std::map<std::string, std::string> keys;
    std::map<std::string, std::string> buttons;
};

inline constexpr int kFrameCaps[] = {0, 30, 60, 120, 144};

bool load_settings(const std::string& path, Settings& out);
bool save_settings(const std::string& path, const Settings& settings);

// The text form, for the file and the tests.
std::string settings_text(const Settings& settings);
Settings parse_settings(const std::string& text);

}  // namespace gaius::ui
