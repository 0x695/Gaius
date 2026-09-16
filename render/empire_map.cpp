// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/empire_map.hpp"

namespace gaius::render {

void render_empire_map(const formats::IndexedImage& map, const formats::PL8Sheet& pointers,
                       const std::array<formats::screen_data::Marker, 50>& markers,
                       const std::vector<uint8_t>& given, int current, formats::IndexedImage& out) {
    out = map;
    for (uint8_t province : kMarkerOrder) {
        if (province >= given.size() || given[province] == 0) continue;
        const int index = province == current ? kCurrentMarkerFrame : kMarkerFrame;
        if (index >= static_cast<int>(pointers.frames.size())) continue;
        const formats::PL8Frame& f = pointers.frames[static_cast<size_t>(index)];
        const formats::screen_data::Marker m = markers[province];
        for (int r = 0; r < f.height; ++r) {
            const int y = m.y + r;
            if (y < 0 || y >= out.height) continue;
            for (int c = 0; c < f.width; ++c) {
                const int x = m.x + c;
                if (x < 0 || x >= out.width) continue;
                const uint8_t p = f.pixels[static_cast<size_t>(r) * f.width + c];
                if (p != 0) out.pixels[static_cast<size_t>(y) * out.width + x] = p;
            }
        }
    }
}

}  // namespace gaius::render
