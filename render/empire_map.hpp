// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — render/empire_map.hpp
//
// The map of the Empire: EMAP2.VPX with a marker on each province the player
// has been given. Transcribed from the US-build CSR.EXE (2026-09-16); findings
// sections 34.3 and 37.
//
// 0x09276 decodes EMAP2.VPX to the screen, and 0x0D21E draws the markers from
// the interface sheet (POINTERS.PL8, the sheet at A000:8000) through 1F6F:1831:
// walking the provinces in the order 3496:172C lists them, each one given
// (table_50, DS:0x581E) gets frame 0x30 if it is the current province
// (DS:0x6CA6) and 0x31 otherwise, at its EDATA.CSR position. The round dots
// on the map belong to EMAP2.VPX itself. The screen shows it in two places:
// the governor's map button (0x0C060 -> 0x0D174, until a click) and while a
// new province's terrain is generated (0x0D1D3, with "generating scrubland").

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "formats/common/types.hpp"
#include "formats/screen_data/screen_data.hpp"

namespace gaius::render {

// 3496:172C: the order the markers are drawn in, north-west first, so later
// markers overlap earlier ones.
inline constexpr std::array<uint8_t, 50> kMarkerOrder = {
    23, 22, 21, 20, 24, 25, 19, 17, 26, 27, 16, 18, 15, 13, 6,  3,  4,  5,  2,  7,  1,  0,  12, 14, 11,
    10, 8,  9,  28, 49, 29, 30, 31, 33, 34, 32, 36, 37, 35, 38, 39, 40, 41, 42, 43, 45, 44, 46, 47, 48};

inline constexpr int kCurrentMarkerFrame = 0x30, kMarkerFrame = 0x31;

// The status line 0x0D1D3 draws while the terrain is generated, in FONT1 at
// (0x4E, 0xBB).
inline constexpr const char* kGeneratingText = "generating scrubland";
inline constexpr int kGeneratingX = 0x4E, kGeneratingY = 0xBB;

// Copies `map` (EMAP2.VPX, 320 x 200) into `out` and draws the markers. Colour
// index 0 of a marker frame is transparent.
void render_empire_map(const formats::IndexedImage& map, const formats::PL8Sheet& pointers,
                       const std::array<formats::screen_data::Marker, 50>& markers,
                       const std::vector<uint8_t>& given, int current, formats::IndexedImage& out);

}  // namespace gaius::render
