// Gaius — formats/vpx/vpx.hpp
//
// .VPX graphics decoder — port of the validated `caesar_vpx.py` prototype.
// Format is DEFINITIVE per CAESAR_VPX_FORMAT.md / COMPLETE.md section 5:
//
//   - 4 consecutive blocks, each with an 8-byte header:
//       uint16 packed_size    (includes this 8-byte header)
//       uint16 decoded_size   (always 0x3E80 = 16000 for Caesar VPX)
//       uint16 fill_pair      (low byte = fill A, high byte = fill B)
//       uint16 field3         (consumed by the engine, not needed to decode)
//     followed by (packed_size - 8) bytes of RLE-compressed payload.
//   - each compressed command byte: bits 7..6 = type, bits 5..0 = count-1
//       00xxxxxx: explicit-byte RLE (read 1 more byte, repeat count times)
//       01xxxxxx: fill-A RLE (repeat fill_pair low byte count times)
//       10xxxxxx: fill-B RLE (repeat fill_pair high byte count times)
//       11xxxxxx: literal run (copy count bytes verbatim)
//   - the four decoded 16000-byte planes are NOT bit planes; they are
//     interleaved 1 byte at a time into a 64000-byte (320x200) framebuffer:
//       image[i*4 + p] = plane[p][i]  for i in 0..16000, p in 0..4
//
// VPX carries no palette of its own — pair the IndexedImage this produces
// with a Palette from formats::p32 or formats::pal256 to render it.

#pragma once

#include "formats/common/types.hpp"

namespace gaius::formats::vpx {

constexpr int kWidth = 320;
constexpr int kHeight = 200;
constexpr int kPlaneSize = kWidth * kHeight / 4;  // 16000

struct BlockHeader {
    uint16_t packed_size = 0;
    uint16_t decoded_size = 0;
    uint16_t fill_pair = 0;
    uint16_t field3 = 0;
};

struct DecodeResult {
    IndexedImage image;             // 320x200, ready to pair with a Palette
    std::array<BlockHeader, 4> headers{};
};

// Throws FormatError on any structural inconsistency (truncated header,
// wrong decoded size, trailing/short compressed data, trailing file bytes)
// rather than guessing — matches the Python prototype's strict checks.
DecodeResult decode(const std::string& path);

}  // namespace gaius::formats::vpx
