// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — ui/maps_screen.hpp
//
// The original maps screen (0x0B717): a 100 x 100 map of the city, one pixel a
// cell, beside six buttons -- urbanization (the city on or off), water
// distribution, administration, road layout, land value and trouble areas --
// and a legend. Transcribed from the US-build CSR.EXE (2026-09-16); findings
// section 39. Like the rest of ui/, no SDL.

#pragma once

#include <vector>

#include "formats/common/types.hpp"
#include "model/city_state.hpp"
#include "ui/game_font.hpp"

namespace gaius::ui {

// DS:0x6D26.
enum class MapMode { None = 0, Water = 1, LandValue = 2, Administration = 3, Trouble = 4, Roads = 5 };

struct MapsArt {
    formats::PL8Sheet blocks;  // P_BLOCKS.PL8: the panels and buttons
    formats::Palette palette;  // SHADE.256, the interface palette (INFERENCE, section 38.2)
    GameFont font;             // FONT1.PL8: the buttons' labels
    GameFont mini;             // MINIFONT.PL1: the legend
};

// Throws formats::FormatError when a file is missing or malformed.
MapsArt load_maps_art(const std::string& asset_dir);

// Where the map sits: cell (x, y) is pixel (kMapX + x, kMapY + y).
inline constexpr int kMapX = 0x16, kMapY = 0x26;

// 1F6F:0009's colour for one cell (a SHADE.256 index). `show_city` is
// DS:0x6D28, the urbanization button.
uint8_t map_colour(const model::CityMap& city, MapMode mode, bool show_city, int x, int y);

// The buttons (DS:0x0704, cells (18, 2)-(18, 7)): what a click at (x, y)
// does. Returns false when it isn't on one; otherwise sets `toggle_city` or
// `mode`.
bool maps_button_at(int x, int y, bool& toggle_city, MapMode& mode);

// A click on the map (0x0DCB4, x 0x16-0x79, y 0x26-0x89): the cell under it.
bool maps_cell_at(int x, int y, int& cell_x, int& cell_y);

// The whole screen, 320 x 200.
void compose_maps_screen(const model::CityMap& city, MapMode mode, bool show_city, const MapsArt& art,
                         std::vector<uint8_t>& rgb);

}  // namespace gaius::ui
