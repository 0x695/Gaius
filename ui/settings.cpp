// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/settings.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace gaius::ui {

namespace {

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

bool to_int(const std::string& s, int& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (*end != '\0') return false;
    out = static_cast<int>(v);
    return true;
}

// The keys this build reads (the key.* and button.* families aside): anything else in a file is kept, not dropped.
bool is_known_key(const std::string& key) {
    static const char* const kKnown[] = {"config_version", "window_mode", "ui_scale", "frame_cap", "music_volume",
                                         "effects_volume", "language", "game_dir", "cursor", "pace", "gamepad_cursor",
                                         "edge_scroll", "touch_hints_seen", "autosave_years", "pause_unfocused"};
    for (const char* k : kKnown)
        if (key == k) return true;
    return key.rfind("key.", 0) == 0 || key.rfind("button.", 0) == 0;
}

// Brings a file of an older layout up to kConfigVersion, in steps on s.file_version (what the file said). Version 0
// (before the key existed) has the same layout as 1, so there is nothing to convert yet; the next change that renames
// a key or changes what one means adds its step here, `if (s.file_version < 2) { ... }`.
void migrate_settings(const Settings& s) {
    (void)s;
}

}  // namespace

std::string settings_text(const Settings& s) {
    std::ostringstream o;
    o << "# Gaius settings. Delete the file for the defaults.\n";
    o << "config_version = " << kConfigVersion << "\n";
    o << "window_mode = " << static_cast<int>(s.window_mode) << "\n";
    o << "ui_scale = " << s.ui_scale << "\n";
    o << "frame_cap = " << s.frame_cap << "\n";
    o << "music_volume = " << s.music_volume << "\n";
    o << "effects_volume = " << s.effects_volume << "\n";
    o << "language = " << s.language << "\n";
    o << "game_dir = " << s.game_dir << "\n";
    o << "cursor = " << static_cast<int>(s.cursor) << "\n";
    o << "pace = " << static_cast<int>(s.pace) << "\n";
    o << "gamepad_cursor = " << (s.gamepad_cursor ? 1 : 0) << "\n";
    o << "edge_scroll = " << (s.edge_scroll ? 1 : 0) << "\n";
    o << "touch_hints_seen = " << (s.touch_hints_seen ? 1 : 0) << "\n";
    o << "autosave_years = " << s.autosave_years << "\n";
    o << "pause_unfocused = " << (s.pause_unfocused ? 1 : 0) << "\n";
    for (const auto& [command, key] : s.keys) o << "key." << command << " = " << key << "\n";
    for (const auto& [command, button] : s.buttons) o << "button." << command << " = " << button << "\n";
    for (const auto& [name, value] : s.unknown) o << name << " = " << value << "\n";
    return o.str();
}

Settings parse_settings(const std::string& text) {
    Settings s;
    s.file_version = 0;  // until the file says
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(t.substr(0, eq));
        const std::string value = trim(t.substr(eq + 1));
        int n = 0;
        if (key == "config_version" && to_int(value, n) && n >= 0) {
            s.file_version = n;
        } else if (key == "window_mode" && to_int(value, n) && n >= 0 && n <= 2) {
            s.window_mode = static_cast<WindowModeSetting>(n);
        } else if (key == "ui_scale" && to_int(value, n) && n >= 0 && n <= 4) {
            s.ui_scale = n;
        } else if (key == "frame_cap" && to_int(value, n) &&
                   std::find(std::begin(kFrameCaps), std::end(kFrameCaps), n) != std::end(kFrameCaps)) {
            s.frame_cap = n;
        } else if (key == "music_volume" && to_int(value, n) && n >= 0 && n <= 100) {
            s.music_volume = n;
        } else if (key == "effects_volume" && to_int(value, n) && n >= 0 && n <= 100) {
            s.effects_volume = n;
        } else if (key == "language" && !value.empty()) {
            s.language = value;
        } else if (key == "game_dir") {
            s.game_dir = value;
        } else if (key == "cursor" && to_int(value, n) && n >= 0 && n <= 1) {
            s.cursor = static_cast<CursorStyle>(n);
        } else if (key == "pace" && to_int(value, n) && n >= 0 && n <= 1) {
            s.pace = static_cast<GamePace>(n);
        } else if (key == "gamepad_cursor" && to_int(value, n)) {
            s.gamepad_cursor = n != 0;
        } else if (key == "edge_scroll" && to_int(value, n)) {
            s.edge_scroll = n != 0;
        } else if (key == "touch_hints_seen" && to_int(value, n)) {
            s.touch_hints_seen = n != 0;
        } else if (key == "autosave_years" && to_int(value, n) &&
                   std::find(std::begin(kAutosaveYears), std::end(kAutosaveYears), n) != std::end(kAutosaveYears)) {
            s.autosave_years = n;
        } else if (key == "pause_unfocused" && to_int(value, n)) {
            s.pause_unfocused = n != 0;
        } else if (key.rfind("key.", 0) == 0 && key.size() > 4 && !value.empty()) {
            s.keys[key.substr(4)] = value;
        } else if (key.rfind("button.", 0) == 0 && key.size() > 7 && !value.empty()) {
            s.buttons[key.substr(7)] = value;
        } else if (!is_known_key(key) && !key.empty()) {
            s.unknown[key] = value;
        }
    }
    migrate_settings(s);
    return s;
}

bool load_settings(const std::string& path, Settings& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream text;
    text << f.rdbuf();
    out = parse_settings(text.str());
    return true;
}

bool save_settings(const std::string& path, const Settings& settings) {
    const std::string temp = path + ".tmp";
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << settings_text(settings);
        f.flush();
        if (!f) {
            f.close();
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            return false;
        }
    }
    std::error_code error;
    std::filesystem::rename(temp, path, error);  // replaces the old file in one step
    if (error) {
        std::filesystem::remove(temp, error);
        return false;
    }
    return true;
}

}  // namespace gaius::ui
