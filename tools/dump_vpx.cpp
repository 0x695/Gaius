// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius CLI tool: dump_vpx
//
// Decode a .VPX file and write it out as a PNG, optionally paired with a
// palette (.P32 or .256). This is the Phase 0/1 "prove the pipeline"
// tool: EMAP2.VPX + EMAP2.P32 should reproduce the game's map screen (and
// EMAP2_decoded.png at nibble level -- that reference widens .P32 as n * 17).
//
// Usage:
//   dump_vpx <input.vpx> <output.png> [palette.p32|palette.256]
//   dump_vpx <input.vpx> <output.png>          (grayscale index fallback)

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "formats/p32/p32.hpp"
#include "formats/pal256/pal256.hpp"
#include "formats/vpx/vpx.hpp"
#include "stb_image_write.h"

using namespace gaius::formats;

namespace {

Palette grayscale_fallback_palette() {
    Palette pal;
    for (int i = 0; i < 256; ++i) pal.colors[i] = RGB{static_cast<uint8_t>(i), static_cast<uint8_t>(i), static_cast<uint8_t>(i)};
    pal.filled_count = 256;
    return pal;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <input.vpx> <output.png> [palette.p32|palette.256]\n", argv[0]);
        return 1;
    }

    std::string in_path = argv[1];
    std::string out_path = argv[2];

    try {
        vpx::DecodeResult result = vpx::decode(in_path);

        Palette pal;
        if (argc >= 4) {
            std::string pal_path = argv[3];
            std::string lp = lower(pal_path);
            if (ends_with(lp, ".p32")) {
                pal = p32::load(pal_path);
            } else if (ends_with(lp, ".256")) {
                pal = pal256::load(pal_path);
            } else {
                std::fprintf(stderr, "warning: unrecognized palette extension, treating as .p32\n");
                pal = p32::load(pal_path);
            }
        } else {
            std::fprintf(stderr, "note: no palette given, writing grayscale index visualization\n");
            pal = grayscale_fallback_palette();
        }

        std::vector<uint8_t> rgb(static_cast<size_t>(result.image.width) * result.image.height * 3);
        for (size_t i = 0; i < result.image.pixels.size(); ++i) {
            uint8_t idx = result.image.pixels[i];
            RGB c = (idx < pal.colors.size()) ? pal.colors[idx] : RGB{255, 0, 255};  // magenta = "out of palette"
            rgb[i * 3 + 0] = c.r;
            rgb[i * 3 + 1] = c.g;
            rgb[i * 3 + 2] = c.b;
        }

        if (!stbi_write_png(out_path.c_str(), result.image.width, result.image.height, 3, rgb.data(),
                             result.image.width * 3)) {
            std::fprintf(stderr, "error: failed to write PNG to %s\n", out_path.c_str());
            return 1;
        }

        std::printf("decoded %s (%dx%d) -> %s\n", in_path.c_str(), result.image.width, result.image.height,
                    out_path.c_str());
        for (int p = 0; p < 4; ++p) {
            const auto& h = result.headers[p];
            std::printf("  block %d: packed=0x%04X decoded=0x%04X fill_pair=0x%04X field3=0x%04X\n", p,
                        h.packed_size, h.decoded_size, h.fill_pair, h.field3);
        }
        return 0;
    } catch (const FormatError& e) {
        std::fprintf(stderr, "format error: %s\n", e.what());
        return 2;
    }
}
