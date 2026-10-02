// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius CLI tool: dump_pl8
//
// Decode a .PL8 sprite sheet (or a 1-bit .PL1 font) and write every frame out
// as a PNG contact sheet plus a per-frame summary, optionally colored via a
// palette. By default the frames sit left to right in one row, each as wide
// as it is; --cols N lays them out in a grid of N columns instead, every cell
// as large as the largest frame, which keeps a sheet of many frames viewable.
// --frames DIR also writes each frame as its own RGBA PNG (frame_000.png ...),
// with index 0 transparent, as the engine draws sprites.
//
// Usage:
//   dump_pl8 <input.pl8|input.pl1> <output.png> [palette.p32|palette.256] [--cols N] [--frames DIR]

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
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
    std::vector<std::string> positional;
    int cols = 0;
    std::string frames_dir;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--cols" && i + 1 < argc) {
            cols = std::atoi(argv[++i]);
        } else if (a == "--frames" && i + 1 < argc) {
            frames_dir = argv[++i];
        } else {
            positional.push_back(a);
        }
    }
    if (positional.size() < 2) {
        std::fprintf(stderr,
                     "usage: %s <input.pl8|input.pl1> <output.png> [palette.p32|palette.256] [--cols N] [--frames DIR]\n",
                     argv[0]);
        return 1;
    }

    const std::string in_path = positional[0];
    const std::string out_path = positional[1];

    try {
        PL8Sheet sheet = ends_with(lower(in_path), ".pl1") ? pl8::load_pl1(in_path) : pl8::load(in_path);

        Palette pal;
        if (positional.size() >= 3) {
            const std::string& p = positional[2];
            pal = ends_with(lower(p), ".256") ? pal256::load(p) : p32::load(p);
        } else {
            std::fprintf(stderr, "note: no palette given, writing grayscale index visualization\n");
            pal = grayscale_fallback_palette();
        }

        std::printf("%s: %zu frames\n", in_path.c_str(), sheet.frames.size());

        if (sheet.frames.empty()) {
            std::fprintf(stderr, "error: no frames decoded\n");
            return 1;
        }

        int row_w = 0, max_w = 0, max_h = 0;
        for (size_t i = 0; i < sheet.frames.size(); ++i) {
            const auto& fr = sheet.frames[i];
            std::printf("  frame %3zu: %3dx%-3d  origin (%d,%d)\n", i, fr.width, fr.height, fr.x, fr.y);
            row_w += fr.width;
            max_w = std::max(max_w, static_cast<int>(fr.width));
            max_h = std::max(max_h, static_cast<int>(fr.height));
        }
        if (row_w == 0 || max_h == 0) {
            std::fprintf(stderr, "error: all frames are zero-sized, nothing to render\n");
            return 1;
        }

        // The sheet's layout: one row of packed frames, or a grid of equal cells.
        const int frame_count = static_cast<int>(sheet.frames.size());
        const int grid_cols = cols > 0 ? std::min(cols, frame_count) : frame_count;
        const int grid_rows = (frame_count + grid_cols - 1) / grid_cols;
        const int total_w = cols > 0 ? grid_cols * max_w : row_w;
        const int total_h = cols > 0 ? grid_rows * max_h : max_h;

        std::vector<uint8_t> rgb(static_cast<size_t>(total_w) * total_h * 3, 0);
        int x_cursor = 0;
        int empty_frames = 0;
        for (int n = 0; n < frame_count; ++n) {
            const auto& fr = sheet.frames[static_cast<size_t>(n)];
            const int cell_x = cols > 0 ? (n % grid_cols) * max_w : x_cursor;
            const int cell_y = cols > 0 ? (n / grid_cols) * max_h : 0;
            x_cursor += fr.width;
            size_t expected = static_cast<size_t>(fr.width) * fr.height;
            // Placeholder frames (see formats/pl8/pl8.hpp) have no real
            // pixel data even though width/height are nonzero -- skip
            // drawing them rather than reading out of bounds.
            if (fr.pixels.size() != expected) {
                ++empty_frames;
                continue;
            }
            for (int y = 0; y < fr.height; ++y) {
                for (int x = 0; x < fr.width; ++x) {
                    uint8_t idx = fr.pixels[static_cast<size_t>(y) * fr.width + x];
                    RGB c = (idx < pal.colors.size()) ? pal.colors[idx] : RGB{255, 0, 255};
                    size_t dst = (static_cast<size_t>(cell_y + y) * total_w + (cell_x + x)) * 3;
                    rgb[dst + 0] = c.r;
                    rgb[dst + 1] = c.g;
                    rgb[dst + 2] = c.b;
                }
            }
        }
        if (empty_frames > 0) std::printf("note: %d placeholder frame(s) with no pixel data skipped\n", empty_frames);

        if (!stbi_write_png(out_path.c_str(), total_w, total_h, 3, rgb.data(), total_w * 3)) {
            std::fprintf(stderr, "error: failed to write PNG to %s\n", out_path.c_str());
            return 1;
        }
        std::printf("wrote contact sheet %dx%d -> %s\n", total_w, total_h, out_path.c_str());

        if (!frames_dir.empty()) {
            int written = 0;
            for (int n = 0; n < frame_count; ++n) {
                const auto& fr = sheet.frames[static_cast<size_t>(n)];
                if (fr.width == 0 || fr.height == 0 ||
                    fr.pixels.size() != static_cast<size_t>(fr.width) * fr.height)
                    continue;
                std::vector<uint8_t> rgba(fr.pixels.size() * 4);
                for (size_t i = 0; i < fr.pixels.size(); ++i) {
                    const uint8_t idx = fr.pixels[i];
                    const RGB c = idx < pal.colors.size() ? pal.colors[idx] : RGB{255, 0, 255};
                    rgba[i * 4 + 0] = c.r;
                    rgba[i * 4 + 1] = c.g;
                    rgba[i * 4 + 2] = c.b;
                    rgba[i * 4 + 3] = idx == 0 ? 0 : 255;
                }
                char name[32];
                std::snprintf(name, sizeof name, "frame_%03d.png", n);
                const std::string path = frames_dir + "/" + name;
                if (!stbi_write_png(path.c_str(), fr.width, fr.height, 4, rgba.data(), fr.width * 4)) {
                    std::fprintf(stderr, "error: failed to write %s (does the folder exist?)\n", path.c_str());
                    return 1;
                }
                ++written;
            }
            std::printf("wrote %d frame PNGs -> %s\n", written, frames_dir.c_str());
        }
        return 0;
    } catch (const FormatError& e) {
        std::fprintf(stderr, "format error: %s\n", e.what());
        return 2;
    }
}
