// Gaius — formats/p32/p32.hpp
//
// .P32 palette decoder.
//
// Format (DEFINITIVE, per CAESAR_REVERSE_ENGINEERING_COMPLETE.md section 6):
//   - exactly 64 bytes on disk
//   - 32 packed 16-bit colors, little-endian
//   - each 16-bit value: bits 0..3 = R, bits 4..7 = unused/reserved,
//                         bits 8..11 = B, bits 12..15 = G
//   - each 4-bit component expands to 8-bit as component * 17
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
