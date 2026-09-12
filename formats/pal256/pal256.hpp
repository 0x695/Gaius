// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — formats/pal256/pal256.hpp
//
// .256 palette decoder.
//
// Format (DEFINITIVE, per CAESAR_REVERSE_ENGINEERING_COMPLETE.md section 7):
//   - exactly 768 bytes on disk
//   - 256 colors x 3 bytes (R, G, B)
//   - each channel is a 6-bit VGA DAC value (0..63)
//   - expand to 8-bit as (value << 2) | (value >> 4)

#pragma once

#include "formats/common/types.hpp"

namespace gaius::formats::pal256 {

// Throws FormatError if the file isn't exactly 768 bytes.
Palette load(const std::string& path);

}  // namespace gaius::formats::pal256
