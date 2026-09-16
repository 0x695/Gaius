// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — formats/empire2/empire2.hpp
//
// EMPIRE2.0xx strategic map decoder. Format is DEFINITIVE, per
// CAESAR_EMPIRE2_RE.md / CAESAR_REVERSE_ENGINEERING_COMPLETE.md section 14:
//
//   struct Empire2File {
//       uint8_t prefix[2];   // 14 14 in all 50 files and all 17 saves.
//                            // The engine never reads it as a field: it
//                            // is loaded and saved with the map at
//                            // 3496:2752 and only reached by grid reads
//                            // that run off the top-left corner
//                            // (dispatch findings section 43). Not a
//                            // width or height -- the engine's 40s are
//                            // constants.
//       uint8_t cell[40][40];
//   };  // exactly 1602 bytes
//
// This is Layer 1 (exact import): it does not interpret cell values
// (terrain/route/object semantics — see CAESAR_EMPIRE2_RE_v2.md and the
// path-property table) beyond exposing them as raw bytes for Layer 2 to
// build on.

#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "formats/common/types.hpp"

namespace gaius::formats::empire2 {

constexpr int kMapW = 40;
constexpr int kMapH = 40;
constexpr size_t kFileSize = 2 + kMapW * kMapH;  // 1602

struct EmpireMap {
    std::array<uint8_t, 2> prefix{};              // unused by the engine (findings section 43) — preserved verbatim
    std::array<uint8_t, kMapW * kMapH> cells{};    // row * 40 + column

    uint8_t at(int row, int col) const { return cells[static_cast<size_t>(row) * kMapW + col]; }
};

// Throws FormatError if the file isn't exactly 1602 bytes.
EmpireMap load(const std::string& path);

// Writes exactly 1602 bytes: prefix followed by the 1600-byte cell array.
// Round-tripping load() -> save() on an untouched file must be byte-identical;
// this is the Phase 0 corpus test (all 50 supplied EMPIRE2.0xx scenarios).
void save(const EmpireMap& map, const std::string& path);

}  // namespace gaius::formats::empire2
