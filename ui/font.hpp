// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — ui/font.hpp
//
// A small built-in 5x7 bitmap font for UI text.
//
// THIS IS NOT THE ORIGINAL GAME'S FONT, and is not presented as one.
// Caesar ships four font resources -- FONT1.PL8 and FONT2.PL8 (100
// frames of 8x8 each), ROMFONT.PL8 (27 frames of 16x17) and
// MINIFONT.PL1 -- and none of them is currently usable:
// formats::pl8 parses their frame *descriptors* correctly (frame count,
// per-frame width/height/origin all read as sensible values) but the
// decoded *pixels* are noise. Verified specifically, so a future session
// doesn't repeat it: dumping FONT1.PL8 with no palette shows only 10
// distinct index values, all in 0..11, with index 0 dominant and index
// 11 the clear ink colour -- exactly the shape a 2-to-10 colour glyph
// sheet should have, so the indices are plausible and the problem is not
// the palette. But rendering those indices at high contrast produces no
// glyph shapes at any of the layouts tried. Conclusion: PL8's pixel path
// handles HOUSES.PL8 (the documented worked example it was validated
// against) but does not handle the font sheets' encoding variant. That
// is a real, specific, newly-narrowed open item -- see docs/FORMATS.md.
//
// So: this font exists so the toolbar can have working, scalable text
// today, and is a straight swap-out once the PL8 font variant is
// decoded. `draw_text` takes a scale factor from ui::Metrics, so text
// scales with icons and hit targets exactly as masterplan 5a point 3
// requires, whichever font is behind it.
//
// Uppercase only (A-Z, 0-9, space and a little punctuation); anything
// unmapped renders as blank rather than throwing, since UI text must
// never be able to crash a frame.

#pragma once

#include <cstdint>
#include <vector>

#include "formats/common/types.hpp"

namespace gaius::ui {

constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;
constexpr int kGlyphAdvance = 6;  // 5px glyph + 1px letterspacing

// Width in logical pixels that `text` will occupy at `scale`.
int text_width(const char* text, int scale);

// Blits `text` into an RGB24 buffer at logical (x, y), top-left origin.
// Clips against the buffer bounds. Lowercase input is upcased.
void draw_text(std::vector<uint8_t>& rgb, int w, int h, int x, int y, const char* text, int scale,
               formats::RGB color);

}  // namespace gaius::ui
