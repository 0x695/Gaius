// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius CLI tool: dump_vas
//
// Plays a .VAS animation over a .VPX picture and writes every frame as a PNG
// (formats/vas/vas.hpp).
//
// Usage:
//   dump_vas <input.vas> <base.vpx> <palette.256|palette.p32> <output_prefix>
//     -> <output_prefix>00.png, 01.png, ...

#include <cstdio>
#include <string>
#include <vector>

#include "formats/p32/p32.hpp"
#include "formats/pal256/pal256.hpp"
#include "formats/vas/vas.hpp"
#include "formats/vpx/vpx.hpp"
#include "stb_image_write.h"

using namespace gaius::formats;

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <input.vas> <base.vpx> <palette.256|palette.p32> <output_prefix>\n", argv[0]);
        return 1;
    }
    try {
        const vas::Animation animation = vas::load(argv[1]);
        vas::Planes planes = vas::to_planes(vpx::decode(argv[2]).image);
        const std::string pal_path = argv[3];
        const bool p32 = pal_path.size() > 4 && (pal_path.substr(pal_path.size() - 4) == ".p32" ||
                                                  pal_path.substr(pal_path.size() - 4) == ".P32");
        const Palette pal = p32 ? p32::load(pal_path) : pal256::load(pal_path);
        for (size_t f = 0; f < animation.frame_offsets.size(); ++f) {
            vas::apply_frame(animation, f, planes);
            const IndexedImage image = vas::to_image(planes);
            std::vector<uint8_t> rgb(image.pixels.size() * 3);
            for (size_t i = 0; i < image.pixels.size(); ++i) {
                const RGB c = pal.colors[image.pixels[i]];
                rgb[i * 3] = c.r;
                rgb[i * 3 + 1] = c.g;
                rgb[i * 3 + 2] = c.b;
            }
            char name[16];
            std::snprintf(name, sizeof name, "%02zu.png", f);
            const std::string out = std::string(argv[4]) + name;
            if (!stbi_write_png(out.c_str(), image.width, image.height, 3, rgb.data(), image.width * 3)) {
                std::fprintf(stderr, "error: failed to write %s\n", out.c_str());
                return 1;
            }
        }
        std::printf("%s: %zu frames\n", argv[1], animation.frame_offsets.size());
        return 0;
    } catch (const FormatError& e) {
        std::fprintf(stderr, "format error: %s\n", e.what());
        return 2;
    }
}
