// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius CLI tool: render_city
//
// Draws a save's city with the original game's sprites and palette, the way
// its city view does (see render/city_render.hpp), and writes it as a PNG.
// The sprites come from your own copy of the game.
//
// Usage:
//   render_city <CAESARxx.SAV> <asset_dir> <out.png> [col row cols rows] [--steps N]
// Without a rectangle it draws the whole 100x100 city (1600x1600 pixels).
// --steps N first runs N simulation steps (systems::month::run_step, walkers
// included) and lists the city walkers it leaves.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <vector>

#include "formats/save/save.hpp"
#include "model/city_state.hpp"
#include "render/city_render.hpp"
#include "stb_image_write.h"
#include "systems/month.hpp"

using namespace gaius;

int main(int argc, char** argv) {
    int steps = 0;
    std::vector<char*> args;
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
            steps = std::atoi(argv[++i]);
        } else {
            args.push_back(argv[i]);
        }
    }
    if (args.size() != 4 && args.size() != 8) {
        std::fprintf(stderr, "usage: %s <CAESARxx.SAV> <asset_dir> <out.png> [col row cols rows] [--steps N]\n", argv[0]);
        return 1;
    }
    int col = 0, row = 0, cols = model::kCityW, rows = model::kCityH;
    if (args.size() == 8) {
        col = std::atoi(args[4]);
        row = std::atoi(args[5]);
        cols = std::atoi(args[6]);
        rows = std::atoi(args[7]);
    }
    if (cols <= 0 || rows <= 0) {
        std::fprintf(stderr, "cols and rows must be positive\n");
        return 1;
    }

    try {
        auto state = std::make_unique<model::CityState>(model::load(formats::save::load(args[1])));
        if (steps > 0) {
            systems::month::SimState sim = systems::month::sim_state_from_save(*state);
            for (int i = 0; i < steps; ++i) systems::month::run_step(*state, sim);
            std::printf("after %d steps (month %d, year %d, step %d):\n", steps, sim.month + 1, sim.year, sim.step);
            for (size_t i = 0; i < state->objects.size(); ++i) {
                const model::Actor& a = state->objects[i];
                if (!a.active() || a.type() >= 11) continue;
                std::printf("  slot %2zu: type %2d state %2d at cell (%d,%d), heading for (%d,%d), frame %d\n", i, a.type(),
                            a.state(), a.screen_x() / 16, a.screen_y() / 16, a.raw_x(), a.raw_y(), a.frame());
            }
        }
        const render::CitySprites sprites = render::load_city_sprites(args[2]);
        formats::IndexedImage img;
        render::render_city(state->city, sprites, col, row, cols, rows, img, {}, &state->objects);

        std::vector<uint8_t> rgb(static_cast<size_t>(img.width) * img.height * 3);
        for (size_t i = 0; i < img.pixels.size(); ++i) {
            const formats::RGB& c = sprites.palette.colors[img.pixels[i]];
            rgb[i * 3] = c.r;
            rgb[i * 3 + 1] = c.g;
            rgb[i * 3 + 2] = c.b;
        }
        if (!stbi_write_png(args[3], img.width, img.height, 3, rgb.data(), img.width * 3)) {
            std::fprintf(stderr, "failed to write %s\n", args[3]);
            return 3;
        }
        std::printf("wrote %s (%dx%d, cells %d..%d x %d..%d)\n", args[3], img.width, img.height, col, col + cols - 1,
                    row, row + rows - 1);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
    return 0;
}
