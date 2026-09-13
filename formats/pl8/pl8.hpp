// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — formats/pl8/pl8.hpp
//
// .PL8 sprite sheet decoder.
//
// Per-frame descriptor format (DEFINITIVE, per
// CAESAR_REVERSE_ENGINEERING_COMPLETE.md section 8):
//   uint16 pixel_offset   stored in REVERSED byte order (big-endian: byte0<<8|byte1)
//   uint8  width
//   uint8  height
//   uint16 x              little-endian
//   uint16 y              little-endian
//   -> width*height indexed pixel bytes live at absolute file offset
//      `pixel_offset`, not inline in the descriptor.
//
// Pixel layout (DEFINITIVE, found 2026-09-13 -- corrects this decoder's
// original row-major reading): the width*height bytes are FOUR STREAMS stored
// one after another, interleaved one pixel at a time exactly like .VPX. Row-
// major pixel i is entry i/4 of stream i%4. Proven pixel-exact against real
// DOSBox screenshots of the city view, rendered with SHADE.256: with this
// layout 13 HOUSES.PL8, 13 HOUSES2.PL8 and 72 FIXTS.PL8 frames appear in them
// verbatim on the 16-pixel tile grid; read row-major, or column-major, none do.
// It is also why the font sheets used to decode to noise. Every frame in every
// shipped .PL8 has a pixel count divisible by 4; the decoder rejects one that
// isn't rather than guess how its streams split.
//
// Container header (NEW — resolved here, not fully documented upstream;
// the existing RE corpus flagged "the first/header word preceding the
// descriptor array is still not fully understood" — CAESAR_REVERSE_
// ENGINEERING_COMPLETE.md section 8.1). Verified against two real files:
//   uint16 unknown_a      (42 for HOUSES.PL8, 61 for MINIFONT.PL8 — meaning
//                          still unknown; NOT the frame count)
//   uint16 frame_count    (confirmed: 50 for HOUSES.PL8 matches the
//                          documented worked example exactly; descriptor
//                          table starts immediately after, at byte offset 4)
//
// Confidence: HIGH. Verified byte-for-byte against HOUSES.PL8's documented
// first-frame example (offset=0x0194, w=16, h=16, x=0, y=0) and
// cross-checked against MINIFONT.PL8 (consecutive pixel_offsets step by
// exactly width*height bytes). `unknown_a`'s meaning remains open — flag
// any future finding back into the main RE corpus.
//
// NEW (found while building this decoder): HOUSES.PL8's last 6 of its 50
// descriptors (frames 44-49) all share a pixel_offset sitting EXACTLY at
// end-of-file, with a nonzero declared width/height that would overrun the
// file if dereferenced. This decoder treats "pixel_offset == end of file"
// as a valid empty/placeholder frame (frame.pixels is empty) rather than
// an error — any other case where the declared size overruns the file is
// still treated as a genuine format error. Not yet explained *why* these
// trailing placeholders exist (spare slots for unused housing variants?);
// flag back to the main RE corpus if a consumer of these specific indices
// is ever found in the executable.

#pragma once

#include "formats/common/types.hpp"

namespace gaius::formats::pl8 {

// Throws FormatError on truncation or any frame whose pixel data would
// read past the end of the file.
PL8Sheet load(const std::string& path);

}  // namespace gaius::formats::pl8
