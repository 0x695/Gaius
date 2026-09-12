// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — platform/paths.hpp
//
// Per-OS config/save-data directory resolution. See GAIUS_MASTERPLAN.md
// section 5a point 6: "Save/config paths... per-platform conventions from
// day one... so save files and settings don't need a migration story
// later."
//
// Deliberately thin: SDL2's SDL_GetPrefPath already implements exactly
// the per-OS convention we want (%APPDATA% / ~/.local/share /
// ~/Library/Application Support / Android app-private storage / iOS app
// sandbox) — no reason to reinvent it. This module just wraps it with a
// fixed org/app identity so callers never have to think about the
// underlying OS convention.

#pragma once

#include <string>

namespace gaius::platform {

// Returns a writable, per-user, per-OS-appropriate directory for Gaius
// save files and settings, guaranteed to exist (created if necessary) and
// to end with a path separator. Throws std::runtime_error if the
// platform refuses to provide one (extremely rare — e.g. a locked-down
// sandbox with no writable storage at all).
std::string pref_path();

// Same idea, but for a subdirectory under pref_path() — e.g.
// data_path("saves") or data_path("screenshots"). Created if missing.
std::string data_path(const std::string& subdir);

}  // namespace gaius::platform
