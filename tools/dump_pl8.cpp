// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius CLI tool: dump_pl8
//
// Decode a .PL8 sprite sheet and write every frame out as a PNG contact
// sheet (frames laid left-to-right in a single row) plus a per-frame
// summary, optionally colored via a palette.
//
// Usage:
//   dump_pl8 <input.pl8> <output.png> [palette.p32|palette.256]

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "formats/p32/p32.hpp"
#include "formats/pal256/pal256.hpp"
#include "formats/pl8/pl8.hpp"
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
        std::fprintf(stderr, "usage: %s <input.pl8> <output.png> [palette.p32|palette.256]\n", argv[0]);
        return 1;
    }

    std::string in_path = argv[1];
    std::string out_path = argv[2];

    try {
        PL8Sheet sheet = pl8::load(in_path);

        Palette pal;
        if (argc >= 4) {
            std::string p = argv[3];
            std::string lp = lower(p);
            pal = ends_with(lp, ".256") ? pal256::load(p) : p32::load(p);
        } else {
            std::fprintf(stderr, "note: no palette given, writing grayscale index visualization\n");
            pal = grayscale_fallback_palette();
        }

        std::printf("%s: %zu frames\n", in_path.c_str(), sheet.frames.size());

        if (sheet.frames.empty()) {
            std::fprintf(stderr, "error: no frames decoded\n");
            return 1;
        }

        int total_w = 0, max_h = 0;
        for (size_t i = 0; i < sheet.frames.size(); ++i) {
            const auto& fr = sheet.frames[i];
            std::printf("  frame %3zu: %3dx%-3d  origin (%d,%d)\n", i, fr.width, fr.height, fr.x, fr.y);
            total_w += fr.width;
            max_h = std::max(max_h, static_cast<int>(fr.height));
        }
        if (total_w == 0 || max_h == 0) {
            std::fprintf(stderr, "error: all frames are zero-sized, nothing to render\n");
            return 1;
        }

        std::vector<uint8_t> rgb(static_cast<size_t>(total_w) * max_h * 3, 0);
        int x_cursor = 0;
        int empty_frames = 0;
        for (const auto& fr : sheet.frames) {
            size_t expected = static_cast<size_t>(fr.width) * fr.height;
            // Placeholder frames (see formats/pl8/pl8.hpp) have no real
            // pixel data even though width/height are nonzero -- skip
            // drawing them rather than reading out of bounds.
            if (fr.pixels.size() != expected) {
                ++empty_frames;
                x_cursor += fr.width;
                continue;
            }
            for (int y = 0; y < fr.height; ++y) {
                for (int x = 0; x < fr.width; ++x) {
                    uint8_t idx = fr.pixels[static_cast<size_t>(y) * fr.width + x];
                    RGB c = (idx < pal.colors.size()) ? pal.colors[idx] : RGB{255, 0, 255};
                    size_t dst = (static_cast<size_t>(y) * total_w + (x_cursor + x)) * 3;
                    rgb[dst + 0] = c.r;
                    rgb[dst + 1] = c.g;
                    rgb[dst + 2] = c.b;
                }
            }
            x_cursor += fr.width;
        }
        if (empty_frames > 0) std::printf("note: %d placeholder frame(s) with no pixel data skipped\n", empty_frames);

        if (!stbi_write_png(out_path.c_str(), total_w, max_h, 3, rgb.data(), total_w * 3)) {
            std::fprintf(stderr, "error: failed to write PNG to %s\n", out_path.c_str());
            return 1;
        }
        std::printf("wrote contact sheet %dx%d -> %s\n", total_w, max_h, out_path.c_str());
        return 0;
    } catch (const FormatError& e) {
        std::fprintf(stderr, "format error: %s\n", e.what());
        return 2;
    }
}
