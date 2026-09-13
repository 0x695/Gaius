// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — formats/pal256/pal256.hpp
//
// .256 palette decoder.
//
// Format (DEFINITIVE -- CAESAR_REVERSE_ENGINEERING_COMPLETE.md section 7, and
// confirmed in the US-build CSR.EXE, 2026-09-13):
//   - exactly 768 bytes on disk, no header
//   - 256 colors x 3 bytes (R, G, B)
//   - each channel is a 6-bit VGA DAC value (0..63)
//
// How the engine uses one: the generic file loader (flat 0x10B0E) reads the
// whole file into a buffer, 100F:0B50 stores that buffer's address, and
// 2EF9:0CF4 hands it untouched to BIOS INT 10h AX=1012h (set DAC block,
// BX=0, CX=256) -- which takes exactly these 6-bit R,G,B triples. In a
// non-VGA video mode it programs only the first 16 entries instead.
//
// 8-bit expansion, (value << 2) | (value >> 4), is not in the executable: the
// DAC is 6-bit, and this is the conventional mapping of 0..63 onto 0..255.
//
// Validated against all eight real .256 files (every byte is 0..63),
// PANEL1.256/SHADE.256 against the matching .P32 files, and a pixel-by-pixel
// render of PANEL1.VPX through both -- see test_pal256_corpus.

#pragma once

#include "formats/common/types.hpp"

namespace gaius::formats::pal256 {

// Throws FormatError if the file isn't exactly 768 bytes, or if any channel
// byte is above 63 (the engine would pass it to the 6-bit DAC unchecked).
Palette load(const std::string& path);

}  // namespace gaius::formats::pal256
