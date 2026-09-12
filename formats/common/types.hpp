// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — formats/common/types.hpp
//
// Shared, format-agnostic value types used across every Layer 1 decoder
// (VPX, P32, .256, PL8, EMPIRE2, SAV). Deliberately dumb structs: no game
// logic lives here, only the shapes that let decoders hand data to each
// other and to tools/ without every decoder reinventing "a palette" or
// "an indexed image."
//
// See GAIUS_MASTERPLAN.md section 5 (Layer 1 — Exact import).

#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace gaius::formats {

// A parse/format error. All decoders throw this (never silently return
// garbage) so CLI tools and future engine code can fail loudly — see
// masterplan's confidence-labeling discipline: we don't guess.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

struct RGB {
    uint8_t r = 0, g = 0, b = 0;
};

// A 256-entry VGA-style palette. P32 decoders only ever fill the first 32
// entries (see CAESAR_REVERSE_ENGINEERING_COMPLETE.md section 6); callers
// must not assume entries 32..255 are meaningful unless the palette came
// from a .256 loader.
struct Palette {
    std::array<RGB, 256> colors{};
    int filled_count = 0;  // how many leading entries are actually known
};

// An 8-bit indexed image: raw palette-index bytes, no color information.
// This is exactly what VPX decodes to (masterplan: VPX carries no palette
// of its own — it must be paired with a separate .P32/.256 file to render).
struct IndexedImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // width * height, row-major, index into a Palette

    uint8_t at(int x, int y) const { return pixels[static_cast<size_t>(y) * width + x]; }
};

// One sprite frame decoded from a PL8 sheet.
struct PL8Frame {
    int width = 0;
    int height = 0;
    int x = 0;  // origin offset, as stored in the frame descriptor
    int y = 0;
    std::vector<uint8_t> pixels;  // width * height indexed bytes
};

struct PL8Sheet {
    std::vector<PL8Frame> frames;
};

}  // namespace gaius::formats
