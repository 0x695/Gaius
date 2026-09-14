// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — formats/vas/vas.hpp
//
// .VAS animations (LOSE0001.VAS, WINS0001.VAS): frames of XOR deltas over the
// four interleaved 16000-byte planes a .VPX image decodes to. Read from the
// US-build CSR.EXE (2026-09-15), docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md
// section 34.
//
// The player (0x23A06, 0x23ACA, 0x23B8A) loads the file into 494C:0000 and,
// for its counter n from 2 up to the word at +2, takes the 32-bit offset at
// +0x10 + 4 x (n - 1) -- so the table starts at +0x14 and holds one frame fewer
// than that word (LOSE0001.VAS 21 frames, WINS0001.VAS 20; one of the three
// players stops LOSE0001 after 13). 100F:0851 then
// selects each VGA plane in turn and has 2EF9:111F XOR that plane's block onto
// the screen, stepping to the next block by its length:
//
//   +0  u16  block length (from the block's start)
//   +2  u16  plane size, 16000
//   +4  4 bytes the decoder skips
//   +8  runs until their counts reach the plane size, each a u16 w:
//       w & 0x8000   XOR the next (w & 0x7FFF) + 1 bytes onto the plane
//       otherwise    skip (w & 0x7FFF) + 1 plane bytes
//
// Each frame is drawn onto both display pages, so the animation accumulates
// over whatever picture the screen shows. DEFINITIVE for the format (the
// decoder is transcribed and every block of both files ends exactly where the
// next begins); which screen and picture they play over is section 34.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "formats/common/types.hpp"

namespace gaius::formats::vas {

inline constexpr size_t kPlaneSize = 16000;

using Planes = std::array<std::vector<uint8_t>, 4>;

struct Animation {
    std::vector<uint8_t> data;             // the whole file
    std::vector<uint32_t> frame_offsets;   // one per frame, into data
};

// Parses and checks the frame table and every plane block. Throws FormatError
// on any inconsistency.
Animation load(const std::string& path);
Animation parse(std::vector<uint8_t> data);

// XORs frame `frame` onto `planes` (each kPlaneSize bytes).
void apply_frame(const Animation& animation, size_t frame, Planes& planes);

// A 320x200 image's pixels as the four planes (pixel 4i + p is plane p's byte
// i, as formats::vpx interleaves them), and back.
Planes to_planes(const IndexedImage& image);
IndexedImage to_image(const Planes& planes);

}  // namespace gaius::formats::vas
