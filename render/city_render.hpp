// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — render/city_render.hpp
//
// Draws a city the way the original's city view does: one 16x16 cell per
// tile, from the game's own sprite sheets and palette. Transcribed from the
// US-build CSR.EXE (2026-09-13) -- the main city draw loop at flat 0x1FFD7,
// its per-cell routine at 0x1FFEF, the building path at 0x20204, and the
// blitters 303E:0BF0 (whole 16x16 tile), 303E:11B4 (16x16 window of a larger
// sprite) and 303E:105C (the rows above a building's footprint).
//
// What the engine loads for the city view:
//   FIXTS.PL8   at 68F6:0000 -- terrain and everything below tile 0xC8
//   HOUSES.PL8  at 54E0:0000 -- buildings, frame = tile - 0xC8
//   HOUSES2.PL8 at 494C:0000 -- building states and animation frames
//   SHADE.256                -- the city view's palette (a DOSBox capture's
//                               DAC equals it in all 256 entries)
//
// How a cell is drawn:
//   - Tile < 0xC8: FIXTS frame = tile id, copied whole. Tiles 0x36-0x43 with
//     7BB4 bit 0x10 draw frame 0x41 instead (routine 0x20156; the engine also
//     clears 7BB4 bit 0x80 there, which a renderer here doesn't). Water-type
//     tiles 0x4A-0x5D, 0x62-0x75 and 0x8A-0x91 add a 0-2 animation phase, and
//     0xA8/AB/AE/B1 a 0-1 blink.
//   - Tile >= 0xC8: every cell of a building draws its own 16x16 slice of the
//     building's sprite, chosen by the cell's part index (7BB4 low nibble,
//     4*dy + dx): the window at (dx*16, dy*16 + extra). Then each cell in the
//     building's top row draws the sprite's `extra` rows above itself, with
//     index 0 transparent -- that's how houses rise above their footprint.
//     The frame is HOUSES tile - 0xC8, except: bath houses 0xE8/0xEA use
//     HOUSES2 0/1 and 2/3 (watered / dry, from 7BB4 bit 0x10), 0xF3 HOUSES2 4,
//     0xF4 HOUSES2 5, and 0xF5/0xF6 HOUSES2 6/7 for their top two rows. Their
//     bottom row (7BB4 bit 0x08) draws frames picked from a runtime actor
//     table the save doesn't hold; with no actor the engine's lookup yields 0,
//     so this draws HOUSES2 8 (left two cells) and 16 (right cell), which is
//     what a real capture shows.
//
// Not modeled: animation beyond the phases in RenderPhase, the animated
// variants of 0xEC/0xEE/0xF1/0xF4 (all gated on frame counters), and the
// overlay map modes (routine 0x1FB94, drawn from SHADE.PL8). Walkers are
// drawn from a save's object table (below); moving them is simulation work
// that isn't transcribed yet.

#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "formats/common/types.hpp"
#include "model/city_state.hpp"
#include "systems/construction.hpp"

namespace gaius::render {

inline constexpr int kCellPx = 16;

struct CitySprites {
    formats::PL8Sheet terrain;    // FIXTS.PL8
    formats::PL8Sheet buildings;  // HOUSES.PL8
    formats::PL8Sheet variants;   // HOUSES2.PL8
    formats::PL8Sheet people;     // MOREMEN.PL8, at 630D:0000 -- walkers and other city actors
    formats::Palette palette;     // SHADE.256
};

// Loads the four files from `asset_dir` (upper- or lower-case names).
// Throws formats::FormatError if one is missing or malformed.
CitySprites load_city_sprites(const std::string& asset_dir);

// Per-building-tile sprite metrics (the table at 3496:14B2) live with the
// other engine rules, in systems/construction.hpp.
using systems::construction::BuildingMetrics;
using systems::construction::kBuildingMetrics;

// The engine's frame counters, for the few tiles that animate. Zero is a
// valid still frame.
struct RenderPhase {
    int water = 0;  // 0..2 (DS:0x57EA)
    int blink = 0;  // 0..1 ((DS:0x6D3A >> 2) & 1)
};

// Renders cells [col0, col0 + cols) x [row0, row0 + rows) into `out`
// (cols*16 x rows*16, palette indices into sprites.palette). Cells are drawn
// in the engine's order -- rows top to bottom, each left to right -- so a
// building's upper rows overlap the cells above it the same way.
//
// With `actors` (a save's object table) it also draws the city's walkers the
// way the engine does: after each tile row, the list builder at flat 0x6733
// collects the active actors of types 0-10 whose feet (y + 8) fall in that
// row, sorts them by y (0x6DA6, ties in table order) and draws each one's
// MOREMEN.PL8 frame (record word +0) with its bottom edge at y + 8 and index
// 0 transparent (0x6946 -> 303E:15B7). The next row's buildings are drawn
// over them afterwards, which is how people disappear behind a house. One
// more row's worth is drawn after the last row, as the engine does.
void render_city(const model::CityMap& city, const CitySprites& sprites, int col0, int row0, int cols, int rows,
                 formats::IndexedImage& out, const RenderPhase& phase = {},
                 const std::array<model::Actor, model::kActorCount>* actors = nullptr);

}  // namespace gaius::render
