// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/settings.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

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

}  // namespace

std::string settings_text(const Settings& s) {
    std::ostringstream o;
    o << "# Gaius settings. Unknown keys are ignored; delete the file for the defaults.\n";
    o << "window_mode = " << static_cast<int>(s.window_mode) << "\n";
    o << "ui_scale = " << s.ui_scale << "\n";
    o << "frame_cap = " << s.frame_cap << "\n";
    o << "music_volume = " << s.music_volume << "\n";
    o << "effects_volume = " << s.effects_volume << "\n";
    o << "language = " << s.language << "\n";
    o << "game_dir = " << s.game_dir << "\n";
    o << "gamepad_cursor = " << (s.gamepad_cursor ? 1 : 0) << "\n";
    o << "touch_hints_seen = " << (s.touch_hints_seen ? 1 : 0) << "\n";
    for (const auto& [command, key] : s.keys) o << "key." << command << " = " << key << "\n";
    for (const auto& [command, button] : s.buttons) o << "button." << command << " = " << button << "\n";
    return o.str();
}

Settings parse_settings(const std::string& text) {
    Settings s;
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
        if (key == "window_mode" && to_int(value, n) && n >= 0 && n <= 2) {
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
        } else if (key == "gamepad_cursor" && to_int(value, n)) {
            s.gamepad_cursor = n != 0;
        } else if (key == "touch_hints_seen" && to_int(value, n)) {
            s.touch_hints_seen = n != 0;
        } else if (key.rfind("key.", 0) == 0 && key.size() > 4 && !value.empty()) {
            s.keys[key.substr(4)] = value;
        } else if (key.rfind("button.", 0) == 0 && key.size() > 7 && !value.empty()) {
            s.buttons[key.substr(7)] = value;
        }
    }
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
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << settings_text(settings);
    return static_cast<bool>(f);
}

}  // namespace gaius::ui
