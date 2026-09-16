// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — ui/options_screen.hpp
//
// The original's Options screen (the control panel's "Game Options",
// command 40 -> 0x0ECA3, drawn by 0x0B47D) and its dialogs: the game and
// scroll speeds (0x0F0D3), sound (0x0F257), display (0x0EF28), the restart
// question (0x0F0AD through 0x0F43F) and leaving the game (0x0ED2C). The
// settings are the 28 bytes of CAESAR.INF (DS:0x5288), read at start-up
// (0x0F628) and written back when the game ends (0x0F8E2).
// Findings section 42.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "formats/common/types.hpp"
#include "ui/buttons.hpp"
#include "ui/interface.hpp"

namespace gaius::ui {

// CAESAR.INF: 14 words loaded at DS:0x5288. Words 0-4, 12 and 13 are the
// installer's sound set-up (13 is 0x220, a Sound Blaster port) and aren't
// read by anything Gaius models.
struct GameOptions {
    std::array<uint16_t, 14> words{};

    int speed() const { return words[5]; }         // DS:0x5292, 0-100 in tens: the speed gate
    int scroll_speed() const { return words[6]; }  // DS:0x5294, 0-100 in tens: the map scroll
    bool effects() const { return words[7] != 0; }                 // DS:0x5296 "Allow effects"
    bool tunes() const { return words[8] != 0; }                   // DS:0x5298 "Allow tunes"
    bool position_indicator_off() const { return words[9] != 0; }  // DS:0x529A
    bool icon_name_off() const { return words[10] != 0; }          // DS:0x529C
    bool city_sounds_off() const { return words[11] != 0; }        // DS:0x529E
    void set(int index, int value) { words[static_cast<size_t>(index)] = static_cast<uint16_t>(value); }
};
inline constexpr int kOptSpeed = 5, kOptScroll = 6, kOptEffects = 7, kOptTunes = 8, kOptPositionOff = 9,
                     kOptIconNameOff = 10, kOptCitySoundsOff = 11;

// The US release's CAESAR.INF as installed: speed and scroll 100, effects
// and tunes on.
GameOptions default_options();
// Reads a 28-byte CAESAR.INF; false (and `out` untouched) if it can't.
bool load_options(const std::string& path, GameOptions& out);
bool save_options(const std::string& path, const GameOptions& options);

enum class OptionsDialog { None, Speed, Sound, Display, Restart, Exit };

// The menu's nine buttons, (15, 2)-(15, 10), in table order (DS:0x0564).
enum class OptionsItem { Resume, GameSpeed, Sound, Load, Save, Display, Pause, Restart, Exit };

// The buttons of the screen or of the open dialog (DS:0x0564, 0x0634,
// 0x0684, 0x05F4, 0x06E4, 0x06C4), in table order. `messages_on` is the
// save's DS:0x6C78.
std::vector<Button> options_buttons(OptionsDialog dialog, const GameOptions& options, bool messages_on);

// The Options screen and any dialog open on it, its buttons at rest.
formats::IndexedImage compose_options_screen(const InterfaceArt& art, OptionsDialog dialog,
                                             const GameOptions& options, bool messages_on);

// A button of an open dialog was acted on (0x0F204-0x0F250, 0x0F415-0x0F438,
// 0x0F006-0x0F017): changes `options` or `messages_on` and returns whether
// the dialog ends. The display toggles read `buttons`' states, as the
// handlers read the records.
bool options_dialog_button(OptionsDialog dialog, int index, const std::vector<Button>& buttons,
                           GameOptions& options, bool& messages_on);

}  // namespace gaius::ui
