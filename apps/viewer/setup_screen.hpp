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

#include "platform/game_import.hpp"
#include "platform/input.hpp"
#include "platform/paths.hpp"
#include "platform/window.hpp"
#include "ui/panel.hpp"
#include "ui/settings.hpp"
#include "ui/strings.hpp"

namespace gaius::viewer {

// The places a game folder can be, in order: the settings' folder, Gaius's
// own game folder, the folder Gaius runs from, the working folder.
inline std::string find_game_folder(const ui::Settings& settings) {
    std::vector<std::string> candidates;
    if (!settings.game_dir.empty()) candidates.push_back(settings.game_dir);
    try {
        candidates.push_back(platform::paths().game);
    } catch (const std::exception&) {
    }
    if (char* base = SDL_GetBasePath()) {
        candidates.push_back(base);
        SDL_free(base);
    }
    std::error_code ec;
    candidates.push_back(std::filesystem::current_path(ec).string());
    for (const std::string& dir : candidates)
        if (platform::looks_like_game_folder(dir)) return dir;
    return std::string();
}

// The setup screen. Returns the game folder once there is one, or an empty
// string if the player quits.
inline std::string run_setup_screen(ui::Settings& settings, const std::string& settings_path, int logical_w,
                                    int logical_h) {
    constexpr int kImport = 1, kLookAgain = 2, kQuit = 3;
    platform::Window window("Gaius", logical_w, logical_h, 960, 600);
    platform::open_gamepads();
    const ui::Metrics m = ui::metrics_for(ui::Breakpoint::Desktop);
    std::string game_dir = find_game_folder(settings);
    std::string gaius_game;
    try {
        gaius_game = platform::paths().game;
    } catch (const std::exception&) {
    }
    std::vector<uint8_t> frame;
    std::string note;
    int hovered = -1;
    Uint32 last_look = SDL_GetTicks();
    while (game_dir.empty()) {
        const bool importing = platform::import_in_progress();
        ui::Page page;
        page.title = ui::tr("Gaius needs Caesar's files");
        page.rows.push_back({ui::tr("Gaius plays your own copy of Caesar"), ""});
        page.rows.push_back({ui::tr("(1992, DOS - the GOG version works)"), ""});
        if (platform::can_import_game_folder()) {
            page.rows.push_back({ui::tr("Import copies its folder into Gaius"), ""});
            page.buttons.push_back({ui::tr(importing ? "Importing..." : "Import"), kImport, !importing});
        } else {
            page.rows.push_back({ui::tr("Drop the Caesar folder on this window,"), ""});
            page.rows.push_back({ui::tr("start Gaius with the folder's path,"), ""});
            page.rows.push_back({ui::tr("or copy the files into"), ""});
            page.rows.push_back({gaius_game.size() > 38 ? "..." + gaius_game.substr(gaius_game.size() - 35) : gaius_game,
                                 ""});
        }
        if (!note.empty()) page.rows.push_back({note, ""});
        page.buttons.push_back({ui::tr("Look again"), kLookAgain});
        page.buttons.push_back({ui::tr("Quit"), kQuit});
        const ui::PanelLayout lay = ui::layout(page, m, logical_w, logical_h);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_DROPFILE) {
                const std::string dropped = event.drop.file;
                SDL_free(event.drop.file);
                if (platform::looks_like_game_folder(dropped)) {
                    game_dir = dropped;
                } else {
                    note = ui::tr("That folder doesn't hold Caesar's files");
                }
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
                case kLookAgain:
                    game_dir = find_game_folder(settings);
                    if (game_dir.empty()) note = ui::tr("Still no Caesar files found");
                    break;
                case kQuit: return std::string();
                default: break;
            }
        }
        // An import finishing, or files copied in by hand: look now and then.
        if (game_dir.empty() && SDL_GetTicks() - last_look > 1000) {
            last_look = SDL_GetTicks();
            if (!platform::import_in_progress()) game_dir = find_game_folder(settings);
        }
        frame.assign(static_cast<size_t>(logical_w) * logical_h * 3, 0);
        ui::render(page, lay, frame, logical_w, logical_h, m, nullptr, hovered);
        window.present_rgb24(frame);
        SDL_Delay(16);
    }
    // Remember a folder found outside Gaius's own places.
    try {
        if (game_dir != platform::paths().game) {
            settings.game_dir = game_dir;
            ui::save_settings(settings_path, settings);
        }
    } catch (const std::exception&) {
    }
    return game_dir;
}

}  // namespace gaius::viewer
