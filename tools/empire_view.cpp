// Gaius CLI tool: empire_view
//
// Render an EMPIRE2.0xx scenario, either as ASCII (matching the
// already-validated `empire2_tools.py ascii_map` logic exactly, for
// direct diffing against that prototype) or as a PNG heatmap colored by
// terrain family per CAESAR_EMPIRE2_RE.md / CAESAR_EMPIRE2_RE_v2.md.
//
// Usage:
//   empire_view <EMPIRE2.0xx> --ascii
//   empire_view <EMPIRE2.0xx> --png <output.png>
//   empire_view <EMPIRE2.0xx> --summary        (cell value histogram)

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "formats/empire2/empire2.hpp"
#include "stb_image_write.h"

using namespace gaius::formats;
using gaius::formats::empire2::EmpireMap;
using gaius::formats::empire2::kMapH;
using gaius::formats::empire2::kMapW;

namespace {

// Mirrors empire2_tools.py's ascii_map() classification exactly.
char classify_ascii(uint8_t v) {
    switch (v) {
        case 0x41: return 'A';
        case 0x4A: return 'C';
        case 0x4B: return 'c';
        case 0x61: return 'R';
        default: break;
    }
    if (v == 0x00) return ' ';
    if (v >= 0x4E && v <= 0x50) return '~';
    if (v >= 0x1D && v <= 0x35) return '.';
    if (v >= 0x51 && v <= 0x60) return '#';
    return '?';
}

// A simple terrain-family color scheme for the PNG heatmap. This is a
// visualization convenience, NOT a claim about the (still-unresolved)
// renderer tile-lookup tables — see GAIUS_ROADMAP.md Phase 8.
RGB classify_color(uint8_t v) {
    if (v == 0x00) return RGB{20, 20, 20};
    if (v == 0x41) return RGB{255, 255, 0};    // special marker A1
    if (v == 0x4A) return RGB{255, 0, 0};      // object type 13 marker
    if (v == 0x4B) return RGB{255, 128, 128};  // companion cell
    if (v == 0x61) return RGB{255, 165, 0};    // route source/seed
    if (v >= 0x4E && v <= 0x50) return RGB{40, 90, 220};   // water family
    if (v >= 0x1D && v <= 0x35) return RGB{90, 160, 60};   // terrain family
    if (v >= 0x51 && v <= 0x60) return RGB{140, 120, 90};  // terrain special
    // generated/route states
    if (v == 0x4C || v == 0x79 || v == 0x7A || v == 0x78) return RGB{200, 100, 200};
    return RGB{100, 100, 100};  // unclassified
}

void print_usage(const char* argv0) {
    std::fprintf(stderr,
                  "usage:\n"
                  "  %s <EMPIRE2.0xx> --ascii\n"
                  "  %s <EMPIRE2.0xx> --png <output.png>\n"
                  "  %s <EMPIRE2.0xx> --summary\n",
                  argv0, argv0, argv0);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    std::string in_path = argv[1];
    std::string mode = argv[2];

    try {
        EmpireMap map = empire2::load(in_path);

        if (mode == "--ascii") {
            std::printf("prefix: %02x %02x\n", map.prefix[0], map.prefix[1]);
            for (int y = 0; y < kMapH; ++y) {
                std::string line(kMapW, ' ');
                for (int x = 0; x < kMapW; ++x) line[x] = classify_ascii(map.at(y, x));
                std::printf("%s\n", line.c_str());
            }
            return 0;
        }

        if (mode == "--summary") {
            std::printf("%s\n", in_path.c_str());
            std::printf("prefix: %02x %02x\n", map.prefix[0], map.prefix[1]);
            std::map<uint8_t, int> counts;
            for (uint8_t v : map.cells) counts[v]++;
            for (auto& [v, c] : counts) std::printf("  %02X %5d\n", v, c);
            for (uint8_t special : {0x41, 0x4A, 0x4B, 0x61}) {
                std::printf("  positions of %02X:", special);
                for (int y = 0; y < kMapH; ++y)
                    for (int x = 0; x < kMapW; ++x)
                        if (map.at(y, x) == special) std::printf(" (%d,%d)", y, x);
                std::printf("\n");
            }
            return 0;
        }

        if (mode == "--png") {
            if (argc < 4) {
                print_usage(argv[0]);
                return 1;
            }
            std::string out_path = argv[3];
            // Render at a fixed per-cell pixel size so the map is actually
            // visible (40x40 raw pixels would be a postage stamp).
            const int cell_px = 8;
            const int w = kMapW * cell_px, h = kMapH * cell_px;
            std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
            for (int y = 0; y < kMapH; ++y) {
                for (int x = 0; x < kMapW; ++x) {
                    RGB c = classify_color(map.at(y, x));
                    for (int py = 0; py < cell_px; ++py) {
                        for (int px = 0; px < cell_px; ++px) {
                            size_t dst = (static_cast<size_t>(y * cell_px + py) * w + (x * cell_px + px)) * 3;
                            rgb[dst + 0] = c.r;
                            rgb[dst + 1] = c.g;
                            rgb[dst + 2] = c.b;
                        }
                    }
                }
            }
            if (!stbi_write_png(out_path.c_str(), w, h, 3, rgb.data(), w * 3)) {
                std::fprintf(stderr, "error: failed to write PNG to %s\n", out_path.c_str());
                return 1;
            }
            std::printf("wrote %dx%d -> %s\n", w, h, out_path.c_str());
            return 0;
        }

        print_usage(argv[0]);
        return 1;
    } catch (const FormatError& e) {
        std::fprintf(stderr, "format error: %s\n", e.what());
        return 2;
    }
}
