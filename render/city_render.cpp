// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/city_render.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

#include "formats/pal256/pal256.hpp"
#include "formats/pl8/pl8.hpp"

namespace gaius::render {

namespace {

std::string find_asset(const std::string& dir, const std::string& upper) {
    namespace fs = std::filesystem;
    std::string lower = upper;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const std::string& name : {upper, lower}) {
        fs::path p = fs::path(dir) / name;
        if (fs::exists(p)) return p.string();
    }
    throw formats::FormatError("render: " + upper + " not found in " + dir);
}

const formats::PL8Frame* frame_at(const formats::PL8Sheet& sheet, int index) {
    if (index < 0 || index >= static_cast<int>(sheet.frames.size())) return nullptr;
    const formats::PL8Frame& f = sheet.frames[static_cast<size_t>(index)];
    return f.pixels.empty() ? nullptr : &f;
}

// Copies the w x h window of `frame` at (sx, sy) to (dx, dy) in `out`,
// clipped to both; with `transparent`, index 0 is skipped.
void blit(formats::IndexedImage& out, const formats::PL8Frame& frame, int sx, int sy, int w, int h, int dx, int dy,
          bool transparent) {
    for (int r = 0; r < h; ++r) {
        const int fy = sy + r, oy = dy + r;
        if (fy < 0 || fy >= frame.height || oy < 0 || oy >= out.height) continue;
        for (int c = 0; c < w; ++c) {
            const int fx = sx + c, ox = dx + c;
            if (fx < 0 || fx >= frame.width || ox < 0 || ox >= out.width) continue;
            const uint8_t p = frame.pixels[static_cast<size_t>(fy) * frame.width + fx];
            if (transparent && p == 0) continue;
            out.pixels[static_cast<size_t>(oy) * out.width + ox] = p;
        }
    }
}

bool animates_with_water(int t) {
    return (t >= 0x4A && t <= 0x5D) || (t >= 0x62 && t <= 0x75) || (t >= 0x8A && t <= 0x91);
}

// Routine 0x20204 for one cell of a building tile.
void draw_building_cell(formats::IndexedImage& out, const CitySprites& s, int tile, uint8_t flags, int px, int py) {
    const int index = tile - 0xC8;
    if (index < 0 || index >= static_cast<int>(kBuildingMetrics.size())) return;
    int extra = kBuildingMetrics[static_cast<size_t>(index)].extra;
    int part = flags & 0x0F;
    const formats::PL8Sheet* sheet = &s.buildings;
    int frame = index;

    switch (tile) {
        case 0xE8:
            sheet = &s.variants;
            frame = (flags & 0x10) ? 0 : 1;
            break;
        case 0xEA:
            sheet = &s.variants;
            frame = (flags & 0x10) ? 2 : 3;
            break;
        case 0xF3:
            sheet = &s.variants;
            frame = 4;
            break;
        case 0xF4:
            sheet = &s.variants;
            frame = 5;
            break;
        case 0xF5:
        case 0xF6:
            sheet = &s.variants;
            if (flags & 0x08) {
                // Bottom row: actor-driven frames; with no actor the lookup
                // reads 0 (see the header).
                extra = 0;
                if (flags & 0x02) {
                    part = 0;
                    frame = 16;
                } else {
                    part &= 1;
                    frame = 8;
                }
            } else {
                frame = tile == 0xF5 ? 6 : 7;
            }
            break;
        default:
            break;
    }

    const formats::PL8Frame* f = frame_at(*sheet, frame);
    if (!f) return;
    const int sx = (part & 3) * kCellPx;
    const int sy = ((part & 0x0C) >> 2) * kCellPx + extra;
    blit(out, *f, sx, sy, kCellPx, kCellPx, px, py, false);
    if (extra > 0 && (part & 0x0C) == 0) blit(out, *f, sx, 0, kCellPx, extra, px, py - extra, true);
}

}  // namespace

CitySprites load_city_sprites(const std::string& asset_dir) {
    CitySprites s;
    s.terrain = formats::pl8::load(find_asset(asset_dir, "FIXTS.PL8"));
    s.buildings = formats::pl8::load(find_asset(asset_dir, "HOUSES.PL8"));
    s.variants = formats::pl8::load(find_asset(asset_dir, "HOUSES2.PL8"));
    s.people = formats::pl8::load(find_asset(asset_dir, "MOREMEN.PL8"));
    s.palette = formats::pal256::load(find_asset(asset_dir, "SHADE.256"));
    return s;
}

void render_city(const model::CityMap& city, const CitySprites& sprites, int col0, int row0, int cols, int rows,
                 formats::IndexedImage& out, const RenderPhase& phase,
                 const std::array<model::Actor, model::kActorCount>* actors) {
    out.width = cols * kCellPx;
    out.height = rows * kCellPx;
    out.pixels.assign(static_cast<size_t>(out.width) * out.height, 0);

    // 0x6733 after each tile row: actors whose feet (y + 8) are in `row`,
    // inside the x window, drawn in order of y.
    std::vector<const model::Actor*> in_row;
    auto draw_actors_in_row = [&](int row) {
        if (!actors) return;
        in_row.clear();
        for (const model::Actor& a : *actors) {
            if (a.active() == 0 || a.type() >= 11) continue;
            const int x = a.screen_x(), y = a.screen_y();
            if (x < (col0 - 1) * kCellPx || x >= (col0 + cols) * kCellPx) continue;
            if ((y + 8) / kCellPx != row) continue;
            in_row.push_back(&a);
        }
        std::stable_sort(in_row.begin(), in_row.end(),
                         [](const model::Actor* p, const model::Actor* q) { return p->screen_y() < q->screen_y(); });
        for (const model::Actor* a : in_row) {
            const formats::PL8Frame* f = frame_at(sprites.people, a->frame());
            if (!f) continue;
            blit(out, *f, 0, 0, f->width, f->height, a->screen_x() - col0 * kCellPx,
                 a->screen_y() + 8 - f->height - row0 * kCellPx, true);
        }
    };

    for (int r = 0; r < rows; ++r) {
        const int row = row0 + r;
        if (row < 0 || row >= model::kCityH) continue;
        for (int c = 0; c < cols; ++c) {
            const int col = col0 + c;
            if (col < 0 || col >= model::kCityW) continue;
            const int px = c * kCellPx, py = r * kCellPx;
            const uint8_t flags = city.operational_state[row][col];
            int t = city.tile[row][col];
            if (t >= 0xC8) {
                draw_building_cell(out, sprites, t, flags, px, py);
                continue;
            }
            if (t >= 0x36 && t <= 0x43 && (flags & 0x10)) t = 0x41;
            if (animates_with_water(t)) t += phase.water;
            if (t == 0xA8 || t == 0xAB || t == 0xAE || t == 0xB1) t += phase.blink;
            if (t >= 0xC8) {
                draw_building_cell(out, sprites, t, flags, px, py);
            } else if (const formats::PL8Frame* f = frame_at(sprites.terrain, t)) {
                blit(out, *f, 0, 0, kCellPx, kCellPx, px, py, false);
            }
        }
        draw_actors_in_row(row);
    }
    draw_actors_in_row(row0 + rows);
}

}  // namespace gaius::render
