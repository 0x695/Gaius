// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — ui/pad_glyphs.hpp
//
// The buttons of a gamepad drawn as little pictures, for the screens that name a binding (Settings > Keys, the controller
// help): a Steam Deck, Xbox-style or generic pad shows A B X Y as lettered discs (green, red, blue, yellow), the bumpers
// and triggers as L1 R1 L2 R2, the stick clicks, the D-pad with its arm, View and Menu, Steam and the four back grips.
// Drawn in code from a few shapes, 9 pixels high at 1x, so there is no art to ship; a PlayStation or Nintendo pad
// keeps its own names in text (platform::button_glyph says 0 for those).
//
// A glyph sits inside a string as kPadMark followed by its one-letter code, so a row's value can be "Q / <glyph>"; the
// panel (ui/panel.hpp) draws the text between glyphs with its font and the glyphs with this.
//   a b x y   the face buttons          l r   the bumpers (L1, R1 on the Deck)   L R   the triggers (L2, R2)
//   s t       the stick clicks (L3, R3) u d < >  the D-pad's four arms
//   v m g     View, Menu, Steam         e f h i   the back grips L4, L5, R4, R5

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gaius::ui {

inline constexpr char kPadMark = '\x01';

inline std::string pad_markup(char code) { return std::string(1, kPadMark) + code; }

// Whether `code` names a glyph.
bool is_pad_code(char code);
// The size at `scale`, 0 for an unknown code.
int pad_glyph_w(char code, int scale);
int pad_glyph_h(char code, int scale);
// Draws it with its top left at (x, y); clipped to the buffer.
void draw_pad_glyph(std::vector<uint8_t>& rgb, int w, int h, int x, int y, char code, int scale);

}  // namespace gaius::ui
