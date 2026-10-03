// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — apps/viewer/save_slots.hpp
//
// The files in the saves folder (platform::paths().saves), all the original's .SAV format (formats::save::write):
//   CAESAR01.SAV-CAESAR08.SAV   the eight slots the player writes from the Forum's save page
//   QUICKSAV.SAV                the quicksave: one key writes it, one reads it back
//   AUTOSAV1.SAV-AUTOSAV3.SAV   the game's own saves, each year or every few (Settings::autosave_years), in turn,
//                               so the last three are always there to go back to
//   AWAY.SAV                    the city as it was when the player left it: the window lost the focus (alt-tab,
//                               another browser tab, the phone's home button) or the game closed, and it had changed
//                               since it was last saved or loaded. Time only runs between those moments, so a year's
//                               autosave alone would lose everything built while the clock was stopped.
// The quick and automatic ones are apart from the eight on purpose: a key press or a year turning never overwrites
// something the player chose to keep. The Load page reaches them through its "Autosaves" button.
//
// No SDL here, so the rules are tested (test_save_slots).

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace gaius::viewer {

inline constexpr int kAutosaveSlots = 3;

inline std::string slot_file_name(int slot) {  // slot 0-7 -> CAESAR01.SAV
    char name[16];
    std::snprintf(name, sizeof name, "CAESAR%02d.SAV", slot + 1);
    return name;
}
inline std::string quicksave_file_name() { return "QUICKSAV.SAV"; }
inline std::string away_save_file_name() { return "AWAY.SAV"; }
inline std::string autosave_file_name(int index) {  // index 0-2 -> AUTOSAV1.SAV
    char name[16];
    std::snprintf(name, sizeof name, "AUTOSAV%d.SAV", index + 1);
    return name;
}

enum class SaveKind { Quick, Auto, Away };

// One of the quick or automatic saves that exists. `file` is the name in the saves folder; `time` is when it was
// written.
struct RecoverableSave {
    std::string file;
    SaveKind kind = SaveKind::Quick;
    int index = 0;  // which autosave, 0-2
    std::filesystem::file_time_type time;
};

// The quicksave, the autosaves and the away save that exist in `dir`, newest first.
inline std::vector<RecoverableSave> recoverable_saves(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::vector<RecoverableSave> out;
    std::error_code error;
    auto add = [&](const std::string& name, SaveKind kind, int index) {
        const fs::path p = dir / name;
        if (!fs::is_regular_file(p, error)) return;
        const fs::file_time_type t = fs::last_write_time(p, error);
        if (error) return;
        out.push_back({name, kind, index, t});
    };
    add(quicksave_file_name(), SaveKind::Quick, 0);
    for (int i = 0; i < kAutosaveSlots; ++i) add(autosave_file_name(i), SaveKind::Auto, i);
    add(away_save_file_name(), SaveKind::Away, 0);
    std::stable_sort(out.begin(), out.end(),
                     [](const RecoverableSave& a, const RecoverableSave& b) { return a.time > b.time; });
    return out;
}

// The autosave file to write next: one that is missing, else the one written longest ago, so the three hold the
// latest three autosaves whatever the interval or the game year (which goes back when a save is loaded).
inline int next_autosave(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    int best = 0;
    fs::file_time_type best_time{};
    bool have_best = false;
    for (int i = 0; i < kAutosaveSlots; ++i) {
        std::error_code error;
        const fs::path p = dir / autosave_file_name(i);
        if (!fs::is_regular_file(p, error)) return i;
        const fs::file_time_type t = fs::last_write_time(p, error);
        if (error) return i;
        if (!have_best || t < best_time) {
            best = i;
            best_time = t;
            have_best = true;
        }
    }
    return best;
}

// A fingerprint of a save's bytes (64-bit FNV-1a, never 0), to tell whether the city has changed since it was last
// written to or read from a save file: the viewer keeps the fingerprint of that moment and compares at the moments
// it would write an away save.
inline uint64_t save_fingerprint(const std::vector<uint8_t>& bytes) {
    uint64_t h = 1469598103934665603ull;
    for (const uint8_t b : bytes) h = (h ^ b) * 1099511628211ull;
    return h ? h : 1;
}

// A game year has turned: is an autosave due? `countdown` is the years left until the next one; the caller sets it to
// the interval when a career starts or a save loads. An interval of 0 (autosave off) is never due.
inline bool autosave_due(int& countdown, int interval_years) {
    if (interval_years <= 0) return false;
    countdown = std::min(countdown, interval_years);  // the setting may have been shortened meanwhile
    if (--countdown > 0) return false;
    countdown = interval_years;
    return true;
}

}  // namespace gaius::viewer
