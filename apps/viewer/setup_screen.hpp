// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — apps/viewer/setup_screen.hpp
//
// Where Gaius looks for the player's Caesar files, and the screen it shows
// when it finds none (Phase 9: touch-first onboarding, masterplan 5a point 7).
// Gaius ships no game files, so a first start has to explain where they go:
//   - on a desktop or the Steam Deck: start Gaius with the folder, drop the
//     folder on the window, or put the files in Gaius's game folder;
//   - on Android: Import, which opens the system's folder picker and copies
//     the files in (platform/game_import.hpp).
// The screen is drawn with Gaius's placeholder font, since the game's own
// isn't there yet.

#pragma once

#include <SDL.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "apps/viewer/game_folder_note.hpp"
#include "platform/game_import.hpp"
#include "platform/input.hpp"
#include "platform/paths.hpp"
#include "apps/viewer/window_icon.hpp"
#include "platform/window.hpp"
#include "stb_image_write.h"
#include "ui/panel.hpp"
#include "ui/settings.hpp"
#include "ui/strings.hpp"

namespace gaius::viewer {

// The places a game folder can be, in order: the settings' folder, Gaius's
// own game folder, the folder Gaius runs from, the working folder, and then
// wherever the game is usually installed (platform::detect_game_folders: GOG,
// Steam, the usual games folders, GAIUS_GAME_DIR).
inline std::string find_game_folder(const ui::Settings& settings) {
    std::vector<std::string> candidates;
    if (!settings.game_dir.empty()) candidates.push_back(settings.game_dir);
    try {
        candidates.push_back(platform::paths().game);
    } catch (const std::exception&) {
    }
#if !defined(__ANDROID__)
    // Beside Gaius and the working folder (an Android app has neither).
    if (char* base = SDL_GetBasePath()) {
        candidates.push_back(base);
        SDL_free(base);
    }
    std::error_code ec;
    candidates.push_back(std::filesystem::current_path(ec).string());
#endif
    // The folder itself, or the game in a folder inside one of them (GOG's top folder copied whole, its US folder below).
    const std::string found = platform::first_game_folder(candidates);
    if (!found.empty()) return found;
    const std::vector<std::string> installed = platform::detect_game_folders();
    return installed.empty() ? std::string() : installed.front();
}

// Why a folder that looks like Caesar's cannot be played from, for the places Gaius looks first: the settings' folder
// and Gaius's own. Empty when neither holds a Caesar folder that is unusable (the usual case: nothing is there).
inline std::string unusable_game_folder_note(const ui::Settings& settings) {
    std::vector<std::string> candidates;
    if (!settings.game_dir.empty()) candidates.push_back(settings.game_dir);
    try {
        candidates.push_back(platform::paths().game);
    } catch (const std::exception&) {
    }
    for (const std::string& dir : candidates) {
        const platform::GameFolderReport report = platform::inspect_game_folder(dir);
        if (report.status == platform::GameFolderStatus::International || report.status == platform::GameFolderStatus::Incomplete)
            return game_folder_note(report);
    }
    return std::string();
}

// What a folder the player chose or dropped holds: the usable game folder in or below it (GOG's top folder holds a US
// folder), else a note saying why not. `chosen` is set when there is one.
inline std::string choose_game_folder(const std::string& picked, std::string& chosen) {
    const std::vector<std::string> inside = platform::game_folders_under(picked, 3);
    if (!inside.empty()) {
        chosen = inside.front();
        return std::string();
    }
    return game_folder_note(platform::inspect_game_folder(picked));
}

// Remembers a game folder found outside Gaius's own places, so the next start
// doesn't have to look for it.
inline void remember_game_folder(ui::Settings& settings, const std::string& settings_path, const std::string& dir) {
    if (dir.empty() || settings_path.empty() || settings.game_dir == dir) return;
    try {
        if (dir == platform::paths().game) return;
        settings.game_dir = dir;
        ui::save_settings(settings_path, settings);
    } catch (const std::exception&) {
    }
}

// The setup screen. Returns the game folder once there is one, or an empty
// string if the player quits.
inline std::string run_setup_screen(ui::Settings& settings, const std::string& settings_path, int logical_w,
                                    int logical_h, const std::string& first_note = std::string(),
                                    const std::string& screenshot_path = std::string()) {
    constexpr int kImport = 1, kLookAgain = 2, kQuit = 3, kBrowse = 4;
    platform::Window window("Gaius", logical_w, logical_h, 960, 600);
    window.set_icon(kWindowIcon, kWindowIconSize);
    platform::open_gamepads();
    const ui::Metrics m = ui::metrics_for(ui::Breakpoint::Desktop);
    // A folder the player named that cannot be played from comes with a note (first_note): the screen then waits for
    // them instead of quietly using some other game folder it finds.
    const bool named_by_player = !first_note.empty();
    std::string game_dir = named_by_player ? std::string() : find_game_folder(settings);
    std::string gaius_game;
    try {
        gaius_game = platform::paths().game;
    } catch (const std::exception&) {
    }
    std::vector<uint8_t> frame;
    std::string note = first_note;
    if (note.empty()) note = unusable_game_folder_note(settings);
    int hovered = -1;
    Uint32 last_look = SDL_GetTicks();
    bool was_importing = false;
    while (game_dir.empty()) {
        int copied = 0;
        const bool importing = platform::import_in_progress(&copied);
        if (was_importing && !importing) {
            // The copy has ended: say so if it did not bring a game.
            const platform::ImportResult result = platform::import_result();
            if (result == platform::ImportResult::NotFound)
                note = ui::tr("No complete Caesar US folder in that one. Choose the folder with HOUSES.PL8 in it (GOG: US), or one that holds it.");
            else if (result == platform::ImportResult::Failed)
                note = ui::tr("Copying stopped part-way. Choose the folder again.");
        }
        was_importing = importing;
        ui::Page page;
        page.title = ui::tr("Gaius needs Caesar's files");
        page.rows.push_back({ui::tr("Gaius plays your own copy of Caesar"), ""});
        page.rows.push_back({ui::tr("(1992, DOS - the GOG version works)"), ""});
        // Why the folder the player named (or the one Gaius found) can't be played from, ahead of the instructions.
        for (const std::string& line : wrap_note(note)) page.rows.push_back({line, ""});
        if (platform::can_import_game_folder()) {
            page.rows.push_back({ui::tr("Import copies its folder into Gaius"), ""});
            if (importing) page.rows.push_back({ui::tr("Copying...") + std::string(" ") + std::to_string(copied), ""});
            page.buttons.push_back({ui::tr(importing ? "Importing..." : "Import"), kImport, !importing});
        } else {
            if (note.empty()) page.rows.push_back({ui::tr("Gaius looked in the usual places and found none."), ""});
            page.rows.push_back({platform::can_pick_folder() ? ui::tr("Choose the Caesar folder,") : ui::tr("Drop the Caesar folder on this window,"), ""});
            page.rows.push_back({ui::tr("drop it on this window, or copy the files into"), ""});
            page.rows.push_back({gaius_game.size() > 38 ? "..." + gaius_game.substr(gaius_game.size() - 35) : gaius_game,
                                 ""});
        }
        if (platform::can_pick_folder()) page.buttons.push_back({ui::tr("Choose folder"), kBrowse});
        page.buttons.push_back({ui::tr("Look again"), kLookAgain});
        page.buttons.push_back({ui::tr("Quit"), kQuit});
        const ui::PanelLayout lay = ui::layout(page, m, logical_w, logical_h);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_DROPFILE) {
                const std::string dropped = event.drop.file;
                SDL_free(event.drop.file);
                note = choose_game_folder(dropped, game_dir);
                continue;
            }
            int pw = 0, ph = 0;
            window.physical_size(&pw, &ph);
            const auto cmd = platform::translate_event(event, pw, ph);
            if (!cmd) continue;
            if (cmd->type == platform::CommandType::Quit) return std::string();
            int lx = 0, ly = 0;
            if (cmd->type == platform::CommandType::Hover) {
                hovered = window.window_to_logical(cmd->x, cmd->y, &lx, &ly) ? ui::hit_test(page, lay, lx, ly) : -1;
            }
            if (cmd->type != platform::CommandType::Select || !window.window_to_logical(cmd->x, cmd->y, &lx, &ly))
                continue;
            switch (ui::hit_test(page, lay, lx, ly)) {
                case kImport:
                    if (!platform::import_game_folder()) note = ui::tr("The folder picker didn't open");
                    break;
                case kBrowse: {
                    const std::string picked = platform::pick_folder(ui::tr("Choose the folder with Caesar's files"));
                    if (picked.empty()) break;
                    // The folder itself, or the one inside it that holds the files (GOG's has a US build in a folder).
                    note = choose_game_folder(picked, game_dir);
                    break;
                }
                case kLookAgain:
                    game_dir = find_game_folder(settings);
                    if (game_dir.empty()) {
                        note = unusable_game_folder_note(settings);
                        if (note.empty()) note = ui::tr("Still no Caesar files found");
                    }
                    break;
                case kQuit: return std::string();
                default: break;
            }
        }
        // An import finishing, or files copied in by hand: look now and then.
        if (game_dir.empty() && !named_by_player && SDL_GetTicks() - last_look > 1000) {
            last_look = SDL_GetTicks();
            if (!platform::import_in_progress()) game_dir = find_game_folder(settings);
        }
        frame.assign(static_cast<size_t>(logical_w) * logical_h * 3, 0);
        ui::render(page, lay, frame, logical_w, logical_h, m, nullptr, hovered);
        window.present_rgb24(frame);
        if (!screenshot_path.empty()) {  // --setup-screenshot: one frame of the screen, for checking its wording
            stbi_write_png(screenshot_path.c_str(), logical_w, logical_h, 3, frame.data(), logical_w * 3);
            return std::string();
        }
        SDL_Delay(16);
    }
    remember_game_folder(settings, settings_path, game_dir);
    return game_dir;
}

}  // namespace gaius::viewer
