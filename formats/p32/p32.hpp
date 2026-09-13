// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — formats/p32/p32.hpp
//
// .P32 palette decoder.
//
// Format (DEFINITIVE, per CAESAR_REVERSE_ENGINEERING_COMPLETE.md section 6):
//   - exactly 64 bytes on disk
//   - 32 packed 16-bit colors, little-endian
//   - each 16-bit value: bits 0..3 = R, bits 4..7 = unused/reserved,
//                         bits 8..11 = B, bits 12..15 = G
//   - each 4-bit component is a VGA DAC value of component * 4 (0..60),
//     widened to 8-bit like .256: (dac << 2) | (dac >> 4), so 15 -> 243
//
// The x4 is DEFINITIVE, from the running game (2026-09-13): DOSBox screenshots
// of the map and forum screens hold EMAP2.P32 and FORUM32.P32 in their DAC as
// exactly nibble * 4 (32/32 entries each), and PANEL1.256 stores PANEL1.P32's
// colours the same way. This decoder previously used component * 17 (15 ->
// 255), up to 12/255 brighter than the game; so was EMAP2_decoded.png, which
// test_vpx_golden_image now compares at nibble level for that reason.
//
// A .P32 file only ever describes the first 32 palette entries. Pair it
// with the corresponding VPX's actual usage — most Caesar screens only
// use a handful of the 32 colors — but do not assume entries 32..255 of
// a Palette filled by this loader are anything but black/zero.

#pragma once

#include "formats/common/types.hpp"

namespace gaius::formats::p32 {

// Throws FormatError if the file isn't exactly 64 bytes.
Palette load(const std::string& path);

}  // namespace gaius::formats::p32
