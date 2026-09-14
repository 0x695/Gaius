// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — formats/screen_data/screen_data.hpp
//
// Two small data files the screens read, from the US-build CSR.EXE
// (2026-09-15); docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 34.
//
// CONTFRM.GD8 (1000 bytes, loaded whole into 3496:0330 by 0x0FD77): the Forum
// screen's click map. 40 x 25 cells of 8 x 8 pixels over the 320 x 200
// NEWFORUM.VPX; 0x0DF57 takes the cell under the mouse, (x / 8) + (y / 8) x 40,
// and a nonzero value v calls the far pointer at DS:0x0764 + 4v -- the eight
// figures of the picture, each its own advisor. The value's shapes match the
// figures exactly. The international build's file differs because its picture
// does. DEFINITIVE.
//
// EDATA.CSR (320 bytes; 0x0FD5B loads the first 200 into 3496:1650): the
// empire map's province markers. 0x0D21E draws a marker for each province
// given, at the pair of words 3496:1650 + 4 x province, byte-swapped (so
// big-endian in the file), less 8 in x and 32 in y (and 10 more in x for
// province 39). The last 120 bytes aren't loaded by this build. DEFINITIVE for
// the 200 read.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "formats/common/types.hpp"

namespace gaius::formats::screen_data {

inline constexpr int kGridW = 40, kGridH = 25, kGridCell = 8;

struct ClickMap {
    std::array<uint8_t, kGridW * kGridH> cells{};

    // The region under screen pixel (x, y) of the 320 x 200 screen, 0 for none.
    int region_at(int x, int y) const {
        if (x < 0 || y < 0 || x >= kGridW * kGridCell || y >= kGridH * kGridCell) return 0;
        return cells[static_cast<size_t>(y / kGridCell) * kGridW + static_cast<size_t>(x / kGridCell)];
    }
};

ClickMap load_click_map(const std::string& path);

struct Marker {
    int x, y;  // the marker's drawing position, as 0x0D21E computes it
};

// The 50 province markers.
std::array<Marker, 50> load_province_markers(const std::string& path);

}  // namespace gaius::formats::screen_data
