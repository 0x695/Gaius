// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — platform/paths.hpp
//
// Where Gaius keeps its own files, per GAIUS_MASTERPLAN.md section 5a point
// 6: "Save/config paths... per-platform conventions from day one".
//
// Three folders, never the game's own:
//   settings  gaius.cfg (Gaius's settings) and caesar.inf (Gaius's copy of the
//             original's options; the game's CAESAR.INF is only read)
//   saves     the save slots CAESAR01.SAV-CAESAR08.SAV
//   game      where the player's Caesar files can be put (or imported, on
//             Android) when no folder is given
//
// Per OS:
//   Windows       %APPDATA%\Gaius\settings, \saves, \game
//   Linux, Deck   $XDG_CONFIG_HOME/gaius (default ~/.config/gaius) for
//                 settings; $XDG_DATA_HOME/gaius (default ~/.local/share/
//                 gaius) for saves and game
//   Android       the app's internal storage for settings and saves; its
//                 external files folder (Android/data/<package>/files/game,
//                 reachable over USB and by file managers, no permission
//                 needed) for game
//   anything else SDL_GetPrefPath("Gaius", "Gaius") for all three
//
// The original writes its saves (CAESARXX.SAV ...) and CAESAR.INF into the
// game's folder; Gaius never writes there, so the two can't collide. (The
// one deliberate exception is the Cohort 2 hand-over, which must leave
// csr0.dat and cohort.csr where cohort.exe looks: findings section 44.)

#pragma once

#include <string>

namespace gaius::platform {

enum class Os { Windows, Linux, Android, Other };

// What resolve_paths needs to know about the machine. current_environment()
// fills it from the running system; tests fill it by hand.
struct PathEnvironment {
    Os os = Os::Other;
    std::string appdata;           // %APPDATA%
    std::string home;              // $HOME
    std::string xdg_config_home;   // $XDG_CONFIG_HOME (may be empty)
    std::string xdg_data_home;     // $XDG_DATA_HOME (may be empty)
    std::string android_internal;  // SDL_AndroidGetInternalStoragePath()
    std::string android_external;  // SDL_AndroidGetExternalStoragePath()
    std::string sdl_pref;          // SDL_GetPrefPath("Gaius", "Gaius")
};

struct GaiusPaths {
    std::string settings, saves, game;  // no trailing separator
};

// Pure: the folders for an environment. An empty folder means the
// environment can't provide one.
GaiusPaths resolve_paths(const PathEnvironment& env);

PathEnvironment current_environment();

// The running system's folders, created if missing. Throws
// std::runtime_error if one can't be resolved or created. The first call
// also moves the settings and saves of the layout before 2026-09-17
// (SDL_GetPrefPath("Gaius", "Gaius")/settings and /saves) into place.
const GaiusPaths& paths();

// Kept for callers of the old interface: `subdir` under the old pref folder.
std::string pref_path();
std::string data_path(const std::string& subdir);

}  // namespace gaius::platform
