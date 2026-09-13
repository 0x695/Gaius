// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius CLI tool: render_city
//
// Draws a save's city with the original game's sprites and palette, the way
// its city view does (see render/city_render.hpp), and writes it as a PNG.
// The sprites come from your own copy of the game.
//
// Usage:
//   render_city <CAESARxx.SAV> <asset_dir> <out.png> [col row cols rows]
// Without a rectangle it draws the whole 100x100 city (1600x1600 pixels).

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <vector>

#include "formats/save/save.hpp"
#include "model/city_state.hpp"
#include "render/city_render.hpp"
#include "stb_image_write.h"

using namespace gaius;

int main(int argc, char** argv) {
    if (argc != 4 && argc != 8) {
        std::fprintf(stderr, "usage: %s <CAESARxx.SAV> <asset_dir> <out.png> [col row cols rows]\n", argv[0]);
        return 1;
    }
    int col = 0, row = 0, cols = model::kCityW, rows = model::kCityH;
    if (argc == 8) {
        col = std::atoi(argv[4]);
        row = std::atoi(argv[5]);
        cols = std::atoi(argv[6]);
        rows = std::atoi(argv[7]);
    }
    if (cols <= 0 || rows <= 0) {
        std::fprintf(stderr, "cols and rows must be positive\n");
        return 1;
    }

    try {
        const model::CityState state = model::load(formats::save::load(argv[1]));
        const render::CitySprites sprites = render::load_city_sprites(argv[2]);
        formats::IndexedImage img;
        render::render_city(state.city, sprites, col, row, cols, rows, img, {}, &state.objects);

        std::vector<uint8_t> rgb(static_cast<size_t>(img.width) * img.height * 3);
        for (size_t i = 0; i < img.pixels.size(); ++i) {
            const formats::RGB& c = sprites.palette.colors[img.pixels[i]];
            rgb[i * 3] = c.r;
            rgb[i * 3 + 1] = c.g;
            rgb[i * 3 + 2] = c.b;
        }
        if (!stbi_write_png(argv[3], img.width, img.height, 3, rgb.data(), img.width * 3)) {
            std::fprintf(stderr, "failed to write %s\n", argv[3]);
            return 3;
        }
        std::printf("wrote %s (%dx%d, cells %d..%d x %d..%d)\n", argv[3], img.width, img.height, col, col + cols - 1,
                    row, row + rows - 1);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
    return 0;
}
