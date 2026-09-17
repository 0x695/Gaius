// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — apps/viewer/settings_page.hpp
//
// The Settings screen (Phase 9). It takes the place of the original's Options
// screen (findings section 42) and holds everything the player can set:
//   Game    the original's game and scroll speeds and messages option, and its
//           menu: resume, pause, load, save, restart, exit
//   Sound   the original's tunes, effects and city sounds switches, and
//           Gaius's music and effects volumes
//   Video   the window, the UI scale, the frame rate, and the original's
//           position indicator and icon names
//   Keys    each command's key and gamepad button, and the gamepad pointer
//   Lang    Gaius's language (ui/strings.hpp)
//   Files   where the game's files are
// The original's settings are CAESAR.INF's words (ui::GameOptions), Gaius's
// are gaius.cfg (ui::Settings); the screen changes both and the viewer writes
// both. Its menu keeps the original's order and meanings (0x0ECDF):
// Pause stops the clock until a click or T (0x0F01E), Restart asks first
// (0x0F43F), Exit asks first (0x0ED2C).
//
// Like apps/viewer/screens.hpp, the page is a ui::Page built from the
// settings, and its actions are applied back by apply_settings_action.

#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "platform/input.hpp"
#include "ui/options_screen.hpp"
#include "ui/panel.hpp"
#include "ui/settings.hpp"
#include "ui/strings.hpp"

namespace gaius::viewer {

enum SettingsTab { kSettingsGame, kSettingsSound, kSettingsVideo, kSettingsKeys, kSettingsLanguage, kSettingsFiles,
                   kSettingsTabCount };

// The rows whose arrows change a value.
enum class SettingRow {
    GameSpeed, ScrollSpeed, Messages,
    Tunes, Effects, CitySounds, MusicVolume, EffectsVolume,
    WindowMode, UiScale, FrameRate, PositionIndicator, IconNames,
    GamepadPointer, Language,
    Binding,  // + the index into platform::kBindable
};

inline constexpr int kActionSettingsTab = 1200;  // + SettingsTab
inline constexpr int kActionSettingDown = 1300;  // + 2 x row id; +1 for up (see row_id)
inline constexpr int kActionResume = 1400, kActionPause = 1401, kActionSettingsLoad = 1402,
                     kActionSettingsSave = 1403, kActionRestart = 1404, kActionExit = 1405, kActionYes = 1406,
                     kActionNo = 1407, kActionResetControls = 1408, kActionImportGame = 1409,
                     kActionRescanGame = 1410;

inline int row_id(SettingRow row, int binding = 0) { return static_cast<int>(row) + binding; }

// What the page shows besides the two settings files.
struct SettingsView {
    SettingsTab tab = kSettingsGame;
    bool messages_on = true;          // the save's DS:0x6C78
    bool in_game = true;              // a career is under way (Save, Pause, Restart apply)
    bool windowed_platform = true;    // the window mode can change (not on Android)
    bool can_import = false;          // Android: a folder can be imported
    bool game_found = true;
    std::string game_dir;
    std::vector<ui::Catalog> languages;  // English first
    int capturing = -1;               // the binding being captured: index * 2 + (gamepad ? 1 : 0)
    enum class Confirm { None, Restart, Exit } confirm = Confirm::None;
};

namespace settings_detail {

inline std::string on_off(bool on) { return ui::tr(on ? "On" : "Off"); }

inline ui::PanelRow row(const std::string& label, const std::string& value, int id) {
    return {label, value, kActionSettingDown + 2 * id, kActionSettingDown + 2 * id + 1};
}

inline const char* window_mode_name(ui::WindowModeSetting m) {
    switch (m) {
        case ui::WindowModeSetting::Windowed: return "Windowed";
        case ui::WindowModeSetting::Borderless: return "Borderless";
        case ui::WindowModeSetting::Fullscreen: return "Fullscreen";
    }
    return "Windowed";
}

inline const char* binding_label(platform::CommandType t) {
    using C = platform::CommandType;
    switch (t) {
        case C::Menu: return "Menu";
        case C::CycleTool: return "Next tool";
        case C::PreviousTool: return "Previous tool";
        case C::CycleVariant: return "Building variant";
        case C::ToggleTime: return "Pause time";
        case C::CycleScreen: return "Next screen";
        case C::ToggleWindowMode: return "Window mode";
        case C::Select: return "Select";
        case C::Secondary: return "Back";
        default: return "?";
    }
}

}  // namespace settings_detail

inline ui::Page settings_page(const ui::Settings& s, const ui::GameOptions& o, const SettingsView& v) {
    using settings_detail::on_off;
    using settings_detail::row;
    ui::Page page;
    if (v.confirm != SettingsView::Confirm::None) {
        page.title = ui::tr(v.confirm == SettingsView::Confirm::Restart ? "Restart the game?" : "Leave the game?");
        page.rows.push_back({ui::tr(v.confirm == SettingsView::Confirm::Restart ? "The career in progress is lost"
                                                                                 : "Unsaved progress is lost"),
                             ""});
        page.buttons.push_back({ui::tr("Yes"), kActionYes});
        page.buttons.push_back({ui::tr("No"), kActionNo});
        return page;
    }
    static constexpr const char* kTabs[] = {"Game", "Sound", "Video", "Keys", "Lang", "Files"};
    for (int i = 0; i < kSettingsTabCount; ++i) page.tabs.push_back({ui::tr(kTabs[i]), kActionSettingsTab + i});
    page.selected_tab = v.tab;
    switch (v.tab) {
        case kSettingsGame:
            page.title = ui::tr("Game");
            page.rows.push_back(row(ui::tr("Game speed"), std::to_string(o.speed()), row_id(SettingRow::GameSpeed)));
            page.rows.push_back(
                row(ui::tr("Scroll speed"), std::to_string(o.scroll_speed()), row_id(SettingRow::ScrollSpeed)));
            page.rows.push_back(row(ui::tr("Messages"), on_off(v.messages_on), row_id(SettingRow::Messages)));
            page.buttons.push_back({ui::tr("Resume"), kActionResume, v.in_game});
            page.buttons.push_back({ui::tr("Pause"), kActionPause, v.in_game});
            page.buttons.push_back({ui::tr("Load"), kActionSettingsLoad, v.game_found});
            page.buttons.push_back({ui::tr("Save"), kActionSettingsSave, v.in_game});
            page.buttons.push_back({ui::tr("Restart"), kActionRestart, v.in_game});
            page.buttons.push_back({ui::tr("Exit"), kActionExit});
            break;
        case kSettingsSound:
            page.title = ui::tr("Sound");
            page.rows.push_back(row(ui::tr("Tunes"), on_off(o.tunes()), row_id(SettingRow::Tunes)));
            page.rows.push_back(row(ui::tr("Effects"), on_off(o.effects()), row_id(SettingRow::Effects)));
            page.rows.push_back(row(ui::tr("City sounds"), on_off(!o.city_sounds_off()), row_id(SettingRow::CitySounds)));
            page.rows.push_back(
                row(ui::tr("Music volume"), std::to_string(s.music_volume) + " %", row_id(SettingRow::MusicVolume)));
            page.rows.push_back(row(ui::tr("Effects volume"), std::to_string(s.effects_volume) + " %",
                                    row_id(SettingRow::EffectsVolume)));
            break;
        case kSettingsVideo:
            page.title = ui::tr("Video");
            if (v.windowed_platform)
                page.rows.push_back(row(ui::tr("Window"), ui::tr(settings_detail::window_mode_name(s.window_mode)),
                                        row_id(SettingRow::WindowMode)));
            page.rows.push_back(row(ui::tr("UI scale"),
                                    s.ui_scale == 0 ? ui::tr("Automatic") : std::to_string(s.ui_scale) + "x",
                                    row_id(SettingRow::UiScale)));
            page.rows.push_back(row(ui::tr("Frame rate"),
                                    s.frame_cap == 0 ? ui::tr("Display") : std::to_string(s.frame_cap),
                                    row_id(SettingRow::FrameRate)));
            page.rows.push_back(row(ui::tr("Position indicator"), on_off(!o.position_indicator_off()),
                                    row_id(SettingRow::PositionIndicator)));
            page.rows.push_back(row(ui::tr("Icon names"), on_off(!o.icon_name_off()), row_id(SettingRow::IconNames)));
            break;
        case kSettingsKeys: {
            page.title = ui::tr("Arrows - left key, right button");
            const int count = static_cast<int>(std::size(platform::kBindable));
            for (int i = 0; i < count; ++i) {
                const platform::CommandType t = platform::kBindable[i];
                std::string key = platform::key_name(platform::key_for(t));
                std::string button = platform::button_label(platform::button_for(t));
                if (t == platform::CommandType::PreviousTool && key.empty())
                    key = std::string(ui::tr("Shift")) + "+" +
                          platform::key_name(platform::key_for(platform::CommandType::CycleTool));
                if (v.capturing == 2 * i) key = ui::tr("press a key");
                if (v.capturing == 2 * i + 1) button = ui::tr("press a button");
                if (key.empty()) key = "-";
                if (button.empty()) button = "-";
                page.rows.push_back(row(ui::tr(settings_detail::binding_label(t)), key + " / " + button,
                                        row_id(SettingRow::Binding, i)));
            }
            page.rows.push_back(row(ui::tr("Gamepad pointer"), on_off(s.gamepad_cursor), row_id(SettingRow::GamepadPointer)));
            page.buttons.push_back({ui::tr("Reset"), kActionResetControls});
            break;
        }
        case kSettingsLanguage: {
            page.title = ui::tr("Language");
            std::string name = s.language;
            for (const ui::Catalog& c : v.languages)
                if (c.code == s.language) name = c.name;
            page.rows.push_back(row(ui::tr("Language"), name, row_id(SettingRow::Language)));
            page.rows.push_back({ui::tr("The game's own texts stay in English"), ""});
            break;
        }
        case kSettingsFiles:
            page.title = ui::tr("Game files");
            page.rows.push_back({ui::tr("Folder"), v.game_dir.empty() ? "-" : v.game_dir});
            page.rows.push_back({ui::tr("Caesar's files"), ui::tr(v.game_found ? "found" : "not found")});
            if (v.can_import) {
                page.rows.push_back({ui::tr("Import copies a folder you choose"), ""});
                page.buttons.push_back({ui::tr("Import"), kActionImportGame});
            } else {
                page.rows.push_back({ui::tr("Drop the Caesar folder on this window"), ""});
            }
            page.buttons.push_back({ui::tr("Look again"), kActionRescanGame});
            break;
        case kSettingsTabCount: break;
    }
    return page;
}

// Folder paths are long: the page shows the last `chars` characters.
inline std::string tail(const std::string& text, size_t chars) {
    return text.size() <= chars ? text : "..." + text.substr(text.size() - chars + 3);
}

// An arrow on a settings row. Changes `s`, `o` and `messages_on`; binding rows
// start a capture instead (returned in `capture`: index * 2 + gamepad).
// Returns true when something changed.
inline bool adjust_setting(int action, ui::Settings& s, ui::GameOptions& o, bool& messages_on,
                           const std::vector<ui::Catalog>& languages, int* capture = nullptr) {
    if (action < kActionSettingDown || action >= kActionSettingDown + 200) return false;
    const int id = (action - kActionSettingDown) / 2;
    const int delta = (action - kActionSettingDown) % 2 ? 1 : -1;
    const auto step = [](int value, int d, int lo, int hi, int by) { return std::clamp(value + d * by, lo, hi); };
    const auto flip = [&o](int index) { o.set(index, o.words[static_cast<size_t>(index)] ? 0 : 1); };
    if (id >= static_cast<int>(SettingRow::Binding)) {
        const int index = id - static_cast<int>(SettingRow::Binding);
        if (index >= static_cast<int>(std::size(platform::kBindable))) return false;
        if (capture) *capture = index * 2 + (delta > 0 ? 1 : 0);
        return false;
    }
    switch (static_cast<SettingRow>(id)) {
        case SettingRow::GameSpeed: o.set(ui::kOptSpeed, step(o.speed() / 10 * 10, delta, 0, 100, 10)); break;
        case SettingRow::ScrollSpeed: o.set(ui::kOptScroll, step(o.scroll_speed() / 10 * 10, delta, 0, 100, 10)); break;
        case SettingRow::Messages: messages_on = !messages_on; break;
        case SettingRow::Tunes: flip(ui::kOptTunes); break;
        case SettingRow::Effects: flip(ui::kOptEffects); break;
        case SettingRow::CitySounds: flip(ui::kOptCitySoundsOff); break;
        case SettingRow::MusicVolume: s.music_volume = step(s.music_volume, delta, 0, 100, 10); break;
        case SettingRow::EffectsVolume: s.effects_volume = step(s.effects_volume, delta, 0, 100, 10); break;
        case SettingRow::WindowMode:
            s.window_mode = static_cast<ui::WindowModeSetting>((static_cast<int>(s.window_mode) + delta + 3) % 3);
            break;
        case SettingRow::UiScale: s.ui_scale = (s.ui_scale + delta + 5) % 5; break;
        case SettingRow::FrameRate: {
            const int n = static_cast<int>(std::size(ui::kFrameCaps));
            int i = static_cast<int>(std::find(std::begin(ui::kFrameCaps), std::end(ui::kFrameCaps), s.frame_cap) -
                                     std::begin(ui::kFrameCaps));
            if (i >= n) i = 0;
            s.frame_cap = ui::kFrameCaps[(i + delta + n) % n];
            break;
        }
        case SettingRow::PositionIndicator: flip(ui::kOptPositionOff); break;
        case SettingRow::IconNames: flip(ui::kOptIconNameOff); break;
        case SettingRow::GamepadPointer: s.gamepad_cursor = !s.gamepad_cursor; break;
        case SettingRow::Language: {
            if (languages.empty()) return false;
            const int n = static_cast<int>(languages.size());
            int i = 0;
            while (i < n && languages[static_cast<size_t>(i)].code != s.language) ++i;
            if (i >= n) i = 0;
            s.language = languages[static_cast<size_t>((i + delta + n) % n)].code;
            break;
        }
        case SettingRow::Binding: break;
    }
    return true;
}

// The settings file's control bindings, from the platform's current ones and
// back: only the commands that differ from the defaults are written.
inline void store_bindings(ui::Settings& s) {
    s.keys.clear();
    s.buttons.clear();
    std::vector<std::pair<int, int>> current;
    for (platform::CommandType t : platform::kBindable)
        current.push_back({platform::key_for(t), platform::button_for(t)});
    platform::reset_bindings();
    size_t i = 0;
    for (platform::CommandType t : platform::kBindable) {
        const auto [key, button] = current[i++];
        if (key != platform::key_for(t)) s.keys[platform::command_name(t)] = key < 0 ? "none" : platform::key_name(key);
        if (button != platform::button_for(t))
            s.buttons[platform::command_name(t)] = button < 0 ? "none" : platform::button_name(button);
    }
    i = 0;
    for (platform::CommandType t : platform::kBindable) {
        platform::bind_key(t, current[i].first);
        platform::bind_button(t, current[i].second);
        ++i;
    }
}

inline void apply_bindings(const ui::Settings& s) {
    platform::reset_bindings();
    for (const auto& [name, key] : s.keys)
        if (const auto t = platform::command_from_name(name))
            platform::bind_key(*t, key == "none" ? -1 : platform::key_from_name(key));
    for (const auto& [name, button] : s.buttons)
        if (const auto t = platform::command_from_name(name))
            platform::bind_button(*t, button == "none" ? -1 : platform::button_from_name(button));
}

}  // namespace gaius::viewer
