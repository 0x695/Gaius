// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — ui/battle_screen.hpp
//
// The original battle screen (0x22116): WAR2.VPX, the two sides' strength
// bars and banners, the Cohort's figures, the round's two-line message and the
// .VAS animations, with the tactic buttons read by the click's x. Transcribed
// from the US-build CSR.EXE (2026-09-16); findings section 38. The rounds
// themselves are systems::battle.
//
// Like the rest of ui/, no SDL: the screen is a state machine stepped once a
// frame and composed into an RGB buffer.
//
// Where Gaius differs: the original draws the names, figures and banners
// straight into video memory every frame, under the next animation frame's
// XOR; Gaius keeps the picture and the animation in `screen` and draws the
// text and banners on top when composing. The sounds aren't played.

#pragma once

#include <array>
#include <string>
#include <vector>

#include "formats/common/types.hpp"
#include "formats/vas/vas.hpp"
#include "model/city_state.hpp"
#include "systems/battle.hpp"
#include "ui/game_font.hpp"

namespace gaius::ui {

struct BattleArt {
    formats::IndexedImage background;  // WAR2.VPX (0x0934B)
    formats::Palette palette;          // WAR2.256
    formats::PL8Sheet banners;         // SPRITE2X.PL8, loaded at 630D:0000 (0x222B6)
    formats::PL8Sheet blocks;          // P_BLOCKS.PL8: the retreat dialog's panel and buttons
    formats::vas::Animation lose;      // LOSE0001.VAS
    formats::vas::Animation wins;      // WINS0001.VAS
    GameFont font;                     // FONT2.PL8, loaded over FONT1 for the battle (0x22297)
    GameFont mini;                     // MINIFONT.PL1
    GameFont dialog_font;              // FONT1.PL8, which the retreat dialog loads back (0x2371D)
    formats::Palette interface_palette;  // SHADE.256: the palette the retreat dialog switches to (0x23738)
};

// Throws formats::FormatError when a file is missing or malformed.
BattleArt load_battle_art(const std::string& asset_dir);

// The screen's words.
struct BattleScreen {
    int cohort = -1, army = -1;    // DS:0x6DC1, DS:0x6DBF
    formats::IndexedImage screen;  // what the display pages hold
    int message_timer = 0;         // DS:0x57FA: frames the message shows; 0 = the buttons
    int closing = 0;               // DS:0x5800: frames until the screen closes (it closes at 1)
    std::array<std::string, 2> message;
    int lose_frame = 0;            // DS:0x5802: LOSE0001.VAS, counter 2-22 (the defeat)
    int wins_frame = 0;            // DS:0x5804: WINS0001.VAS, 2-21 (the victory)
    int hit_frame = 0;             // DS:0x5806: LOSE0001.VAS, 2-14 (a lost round, a retreat)
    int start_men = 0;             // DS:0x57FC: Centuries x 2 at the start
    int start_army = 0;            // DS:0x57FE
    int start_regulars = 0, start_irregulars = 0, start_auxiliaries = 0;  // DS:0x57F8 / F6 / F4
    int roman_banner = 0;          // DS:0x57F0: the Cohort's number x 4
    int barbarian_banner = 0;      // DS:0x57F2 = DS:0x6BD8, 40-43
    bool confirming_retreat = false;  // 0x23708's dialog
    bool retreated = false;
    systems::battle::Round last;
    bool has_round = false;

    bool finished() const { return closing == 1; }
    bool ended() const { return last.victory || last.defeat || retreated; }
};

// 0x22116: the words at the start (the race is loaded by systems::battle) and
// the background.
BattleScreen begin_battle(const model::CityState& state, const BattleArt& art, int cohort, int army);

// 0x0934B: WAR2.VPX, then the bars -- black under each side's starting height
// + 2, and colour 12 for the Romans' Centuries x 4 at x 0x10F and the army's
// size x 4 at 0x125, 14 wide, up to 64 tall, standing on y 0x62.
void draw_background(BattleScreen& b, const model::CityState& state, const BattleArt& art);

// What a click at (x, y) does while the buttons show (0x2250F): 0-3 a tactic,
// 4 retreat, -1 nothing. Only when neither a message nor the closing count is
// running.
inline constexpr int kRetreatButton = 4;
int button_at(const BattleScreen& b, int x, int y);

// A tactic's round (Tortoise 0x2308E, Assault 0x231E2, Flank 0x23212, Charge
// 0x23242): systems::battle's round, then the message and animation for its
// outcome (0x229A3's tail, 0x22D5D victory, 0x22EDB defeat).
void play_round(BattleScreen& b, model::CityState& state, const BattleArt& art, systems::battle::Tactic tactic,
                const systems::month::Random& random);

// 0x230BE: the dialog 0x23708 asks "Retreat ?"; its Yes button (cell 12, 6)
// retreats, No (cell 12, 7) goes back. Returns whether the click was on one.
bool answer_retreat(BattleScreen& b, model::CityState& state, const BattleArt& art, int x, int y);

// One frame of the loop 0x2244B: the message and animations (0x225EE), then
// the closing count. `clicked` is a click this frame that wasn't a button.
void battle_frame(BattleScreen& b, const model::CityState& state, const BattleArt& art, bool clicked);

// The whole screen as the player sees it.
void compose_battle(const BattleScreen& b, const model::CityState& state, const BattleArt& art,
                    std::vector<uint8_t>& rgb);

}  // namespace gaius::ui
