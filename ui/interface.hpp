// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — ui/interface.hpp
//
// The drawing primitives the original's interface screens share, on an
// indexed 320 x 200 canvas in the interface palette (SHADE.256): the stone
// panels, insets and buttons of P_BLOCKS.PL8, the sprites of POINTERS.PL8,
// filled rectangles, and text in FONT1 and MINIFONT. Transcribed from the
// US-build CSR.EXE; findings sections 38.3, 38.4, 39 and 41.

#pragma once

#include <string>
#include <vector>

#include "formats/common/types.hpp"

namespace gaius::ui {

struct InterfaceArt {
    formats::PL8Sheet blocks;    // P_BLOCKS.PL8 (2EF9:0290's sheet)
    formats::PL8Sheet pointers;  // POINTERS.PL8 (2EF9:0298's)
    formats::PL8Sheet font;      // FONT1.PL8 at 54E0:C648
    formats::PL8Sheet mini;      // MINIFONT.PL1 at 54E0:C254
    formats::Palette palette;    // SHADE.256, the palette saved at 68F6:B6A8
};

// Throws formats::FormatError when a file is missing or malformed.
InterfaceArt load_interface_art(const std::string& asset_dir);

formats::IndexedImage blank_canvas();

// 303E:0BF0: a block drawn whole, colour 0 included.
void draw_block(formats::IndexedImage& img, const formats::PL8Sheet& sheet, int frame, int x, int y);
// 303E:13D6: a sprite, colour 0 transparent.
void draw_sprite(formats::IndexedImage& img, const formats::PL8Sheet& sheet, int frame, int x, int y);
// 303E:15B7 as the history bars use it: the frame's top `rows` rows, drawn
// with their last row on `bottom` (colour 0 transparent).
void draw_sprite_rows(formats::IndexedImage& img, const formats::PL8Sheet& sheet, int frame, int x, int bottom,
                      int rows);
// 100F:0EF9.
void fill_rect(formats::IndexedImage& img, int x, int y, int w, int h, uint8_t colour);

// 1F6F:1EC1: a framed stone panel of cols x rows 16-pixel cells.
void draw_panel(formats::IndexedImage& img, const InterfaceArt& art, int x, int y, int cols, int rows);
// 1F6F:2008: a sunken inset.
void draw_inset(formats::IndexedImage& img, const InterfaceArt& art, int x, int y, int cols, int rows);
// 1F6F:2115: plain stone.
void draw_stone(formats::IndexedImage& img, const InterfaceArt& art, int x, int y, int cols, int rows);

// 100F:1583: text in FONT1 (8 px a character) or MINIFONT (6 px, every set
// bit in colour 0 -- 1F6F:292B).
enum class Font { Font1, Mini };
void draw_text(formats::IndexedImage& img, const InterfaceArt& art, Font font, int x, int y, const std::string& text);

// 100F:17E2's digits: `digits` characters, the value's last digits. Mode 0
// pads with zeros; modes 1 and 2 with spaces (a zero value shows one '0').
std::string number_text(long value, int digits, int mode);
void draw_number(formats::IndexedImage& img, const InterfaceArt& art, Font font, int x, int y, long value,
                 int digits, int mode = 1);

void canvas_to_rgb(const formats::IndexedImage& img, const formats::Palette& palette, std::vector<uint8_t>& rgb);

}  // namespace gaius::ui
