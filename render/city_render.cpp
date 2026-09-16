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

// Workshop and barracks records as the engine addresses them: DS:0x585C +
// record x 24 + offset, where record 30 runs on into the barracks table at
// DS:0x5B2C. Missing bytes read as 0.
int record_word(const RenderPhase& phase, int record, int offset) {
    auto byte = [&](size_t i) -> int {
        if (i < 720) return (phase.workshop_records && i < phase.workshop_records->size()) ? (*phase.workshop_records)[i] : 0;
        i -= 720;
        return (phase.barracks_records && i < phase.barracks_records->size()) ? (*phase.barracks_records)[i] : 0;
    };
    const size_t o = static_cast<size_t>(record) * 24 + static_cast<size_t>(offset);
    return static_cast<int16_t>(byte(o) | (byte(o + 1) << 8));
}

// The workshop search in 0x20204: the first active record whose row is row_a
// or row_b and whose column is col, col - 1 or col - 2; 30, past the table,
// when there is none.
int find_workshop(const RenderPhase& phase, int col, int row_a, int row_b) {
    for (int i = 0; i < 30; ++i) {
        if (record_word(phase, i, 8) == 0) continue;
        const int r = record_word(phase, i, 2);
        if (r != row_a && r != row_b) continue;
        const int c = record_word(phase, i, 0);
        if (c == col || c == col - 1 || c == col - 2) return i;
    }
    return 30;
}

// Routine 0x20204 for one cell of a building tile.
void draw_building_cell(formats::IndexedImage& out, const CitySprites& s, const model::CityMap& city, int col, int row,
                        int tile, uint8_t flags, int px, int py, const RenderPhase& phase) {
    const int index = tile - 0xC8;
    if (index < 0 || index >= static_cast<int>(kBuildingMetrics.size())) return;
    int extra = kBuildingMetrics[static_cast<size_t>(index)].extra;
    int part = flags & 0x0F;
    const formats::PL8Sheet* sheet = &s.buildings;
    int frame = index;
    const int c32 = phase.ticks % 32, c64 = phase.ticks % 64, c128 = phase.ticks % 128;
    const int stride = (c32 & 6) >> 1;  // 1-3 whenever a DS:0x6D3E animation runs
    auto variant = [&](int f) {
        sheet = &s.variants;
        frame = f;
    };

    switch (tile) {
        case 0xE8:
        case 0xEA: {
            // Bath houses: by water; a watered one shows its HOUSES frame
            // while DS:0x6D3E bit 8 is set.
            const bool watered = (flags & 0x10) != 0;
            if (!(watered && (c32 & 8))) variant((tile == 0xE8 ? 0 : 2) + (watered ? 0 : 1));
            break;
        }
        case 0xEC:  // school
            if (static_cast<int8_t>(city.coverage[static_cast<size_t>(row)][static_cast<size_t>(col)]) > 12 && (c32 & 6) &&
                phase.population_units > 100)
                variant(0x1E + stride);
            break;
        case 0xEE:  // prefecture: busy among houses
            if (c32 & 6) {
                int homes = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int y = row + dy, x = col + dx;
                        if ((dx == 0 && dy == 0) || y < 0 || y >= model::kCityH || x < 0 || x >= model::kCityW) continue;
                        const uint8_t t = city.tile[static_cast<size_t>(y)][static_cast<size_t>(x)];
                        if (t >= 0xC8 && t <= 0xD7) ++homes;
                    }
                }
                if (homes >= 4) variant(0x1B + stride);
            }
            break;
        case 0xF1:  // coliseum
            if (c128 >= 70 && phase.population_units >= 200)
                variant(c128 < 75 ? 0x29 : c128 < 80 ? 0x2A : c128 < 124 ? 0x2B : c128 < 126 ? 0x2A : 0x29);
            break;
        case 0xF3:
            variant(4);
            break;
        case 0xF4:  // market, trading
            variant(5);
            if ((c64 & 0x30) && phase.coverage_base > 0 && phase.population_units >= 30) variant(0x18 + ((c64 & 0x30) >> 4));
            break;
        case 0xF5:
        case 0xF6:
            sheet = &s.variants;
            if (flags & 0x08) {
                // Bottom row: the production level on the left two cells, the
                // goods on the right one.
                const int w = find_workshop(phase, col, row - 2, row - 2);
                extra = 0;
                if (flags & 0x02) {
                    part = 0;
                    frame = 0x10 + record_word(phase, w, 0x04);
                } else {
                    part &= 1;
                    frame = 8 + (record_word(phase, w, 0x10) & 7);
                }
            } else {
                frame = tile == 0xF5 ? 6 : 7;
                if (c32 & 6) {
                    const int w = find_workshop(phase, col, row, row - 1);
                    if (record_word(phase, w, 0x10) > 2) frame = (tile == 0xF5 ? 0x21 : 0x24) + stride;
                }
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
                draw_building_cell(out, sprites, city, col, row, t, flags, px, py, phase);
                continue;
            }
            if (t >= 0x36 && t <= 0x43 && (flags & 0x10)) t = 0x41;
            if (animates_with_water(t)) t += phase.water;
            if (t == 0xA8 || t == 0xAB || t == 0xAE || t == 0xB1) t += phase.blink;
            if (t >= 0xC8) {
                draw_building_cell(out, sprites, city, col, row, t, flags, px, py, phase);
            } else if (const formats::PL8Frame* f = frame_at(sprites.terrain, t)) {
                blit(out, *f, 0, 0, kCellPx, kCellPx, px, py, false);
            }
        }
        draw_actors_in_row(row);
    }
    draw_actors_in_row(row0 + rows);
}

systems::sounds::CitySoundFlags city_sound_flags(const model::CityState& state, int col0, int row0, int cols,
                                                 int rows) {
    systems::sounds::CitySoundFlags f;
    const int x1 = std::min(col0 + cols, model::kCityW), y1 = std::min(row0 + rows, model::kCityH);
    for (int y = std::max(row0, 0); y < y1; ++y) {
        for (int x = std::max(col0, 0); x < x1; ++x) {
            const uint8_t t = state.city.tile[y][x];
            if (t == 0xF0) f.theatre = true;
            if (t == 0xF1) f.coliseum = true;
            if (t == 0xF2) f.hippodrome = true;
            if (t >= 0xE0 && t <= 0xE7) f.forum = true;
            if (t == 0xF5 || t == 0xF6) f.workshop = true;
            if (t == 0xF4) f.market = true;
            if (t == 0xB9 || t == 0xBB) f.fountain = true;
        }
    }
    for (const model::Actor& a : state.objects) {
        const int type = static_cast<int8_t>(a.type());
        if (a.active() == 0 || type < 0 || type > 10) continue;
        const int x = a.screen_x() / 16, y = (a.screen_y() + 8) / 16;
        if (x < col0 || x >= col0 + cols || y < row0 || y >= row0 + rows) continue;
        if (type >= 3 && type <= 4) f.romans = true;
        if (type >= 5 && type <= 7) f.barbarians = true;
    }
    return f;
}

}  // namespace gaius::render
