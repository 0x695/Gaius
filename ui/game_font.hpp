// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — ui/game_font.hpp
//
// The original game's text font: FONT1.PL8 (100 frames, 8x8), drawn the way
// the engine's text routine 100F:1583 draws it. That routine looks each
// character up in the 96-byte table at DS:0F64 (index = char - 0x20; value =
// frame + 1, 0 = no glyph), draws the frame with index 0 transparent, and
// advances 8 pixels per character whether or not it drew one. The engine
// loads the sheet at 54E0:C648 (call at flat 0xFC1A).
//
// DEFINITIVE: text read out of real DOSBox captures through this table
// spells the game's own messages ("The province's funds are now at or below
// 1,000 Denarii"), and test_ui_game_font_matches_screenshot draws a word
// pixel for pixel. The glyphs come from the user's own copy of the game, so
// callers load the sheet; ui/font.hpp's placeholder remains the fallback when
// it isn't available.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "formats/common/types.hpp"

namespace gaius::ui {

inline constexpr int kGameGlyphPx = 8;

// DS:0F64, characters 0x20-0x7F.
inline constexpr std::array<uint8_t, 96> kGameFontFrameTable = {
    0,  65, 72, 0,  0,  71, 0,  98, 74, 73, 0,  63, 70, 64, 69, 0,   // ' '-'/'
    62, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 62, 0,  67, 0,  66,  // '0'-'?'
    0,  27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,  // '@'-'O'
    42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 0,  99, 0,  0,  0,   // 'P'-'_'
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,  // '`'-'o'
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 0,  0,  0,  0,  0,   // 'p'-DEL
};

// The FONT1.PL8 frame for `c`, or -1 when the font has none.
inline int game_glyph_frame(char c) {
    const int i = static_cast<unsigned char>(c) - 0x20;
    if (i < 0 || i >= static_cast<int>(kGameFontFrameTable.size())) return -1;
    return static_cast<int>(kGameFontFrameTable[static_cast<size_t>(i)]) - 1;
}

struct GameFont {
    formats::PL8Sheet sheet;   // FONT1.PL8, or MINIFONT.PL1
    formats::Palette palette;  // the palette the glyph indices are drawn with
    int advance = kGameGlyphPx;  // pixels per character: 8 for FONT1, 6 for MINIFONT
    // MINIFONT.PL1 is 1 bit per pixel, so its glyphs carry no colour: when set,
    // every non-zero pixel is drawn in `ink` instead of through the palette.
    bool use_ink = false;
    formats::RGB ink{};
};

// MINIFONT.PL1 (formats::pl8::load_pl1): 72 frames of 8x6, a 5x5 font of
// capitals, digits and a little punctuation, laid out so the same DS:0F64
// table maps characters to it (lower case folds onto the capitals). The engine
// loads it at 54E0:C254 and advances 6 px per character. Drawn in `ink`.
GameFont load_mini_font(const std::string& asset_dir, formats::RGB ink);

// As game_text_width, at the font's own advance.
int game_text_width(const char* text, int scale, const GameFont& font);

// Loads FONT1.PL8 from `asset_dir` (upper- or lower-case name) and pairs it
// with `palette`. Throws formats::FormatError if it's missing or malformed.
GameFont load_game_font(const std::string& asset_dir, const formats::Palette& palette);

// Width in logical pixels of `text` at `scale`: 8 per character, as the
// engine advances for FONT1.
int game_text_width(const char* text, int scale);

// Draws `text` into an RGB24 buffer at logical (x, y), each glyph pixel
// scaled to scale x scale, index 0 transparent. Clips to the buffer.
void draw_game_text(std::vector<uint8_t>& rgb, int w, int h, int x, int y, const char* text, int scale,
                    const GameFont& font);

}  // namespace gaius::ui
