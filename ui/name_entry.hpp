// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — ui/name_entry.hpp
//
// The governor's name and the dialog that edits it (the start screen's "Choose
// name", 0x27F84 -> 0x0C41D). Transcribed from the US-build CSR.EXE
// (2026-09-16); findings section 40.
//
// The name is 12 characters, kept at DS:0x0DD0 ("  Octavian  " to begin
// with) and saved as the 12 bytes at DS:0x6B9E (final_state bytes 12-23).
// The dialog edits it in place two ways: an up arrow over and a down arrow
// under each letter, which step through '@', 'A'-'Z' and 'a'-'z' -- so it
// needs no keyboard -- and the keyboard through the engine's text editor
// 100F:0F79 (0x11069).

#pragma once

#include <array>
#include <string>
#include <vector>

#include "formats/common/types.hpp"
#include "model/city_state.hpp"
#include "ui/game_font.hpp"

namespace gaius::ui {

inline constexpr int kNameLength = 12;
inline constexpr const char* kDefaultGovernorName = "  Octavian  ";  // DS:0x0DD0

// The name saved in `state` (final_state bytes 12-23), or the default when the
// state has none.
std::string governor_name(const model::CityState& state);
void set_governor_name(model::CityState& state, const std::string& name);

struct NameEntry {
    std::array<char, kNameLength> name{};
    int cursor = 0;  // DS:0x6D32
};

NameEntry begin_name_entry(const std::string& name);
std::string name_text(const NameEntry& e);

// 0x0CB68, a letter's up arrow: 'Z' becomes 'a'; anything below '@' becomes
// '@'; otherwise the next character, at most 'z'.
void name_letter_up(NameEntry& e, int i);
// 0x0CBD3, the down arrow: the previous character, at least '@'. It means to
// turn 'a' into 'Z' but tests the name's first character plus i against 'a'
// instead of the letter itself -- transcribed as it is.
void name_letter_down(NameEntry& e, int i);

// 0x11069, one key: returns 1 for Escape, 2 for Enter, 0 otherwise.
// Backspace clears the character before the cursor and moves onto it; Left
// and Right move the cursor; Delete clears the character under it (on the last
// one, then steps back); '_', digits and letters overwrite the character under
// the cursor and advance, stopping on the last. Anything else does nothing.
enum class NameKey { Character, Escape, Enter, Backspace, Left, Right, Delete };
int name_key(NameEntry& e, NameKey key, char ch = 0);

// A click: an up arrow (P_BLOCKS cells (4-15, 5)) or down arrow ((4-15, 7))
// changes its letter, and a click on the name (x 0x40-0x100, y 0x60-0x70,
// 0x0C859) puts the cursor there. Returns whether it hit anything.
bool name_click(NameEntry& e, int x, int y);

struct NameArt {
    formats::PL8Sheet blocks;  // P_BLOCKS.PL8
    formats::Palette palette;  // SHADE.256
    GameFont font;             // FONT1.PL8
};
NameArt load_name_art(const std::string& asset_dir);

// Draws the dialog over `rgb` (320 x 200): a 14 x 5 panel at (0x30, 0x40), the
// arrows (frames 18 and 19), the name in FONT1 at (0x44 + 16i, 0x64) and the
// cursor, a '-' at (0x44 + 16 x cursor, 0x69).
void compose_name_entry(const NameEntry& e, const NameArt& art, std::vector<uint8_t>& rgb);

}  // namespace gaius::ui
