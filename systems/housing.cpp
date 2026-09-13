// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/housing.hpp"

#include "systems/service.hpp"

namespace gaius::systems::housing {

namespace {

using model::CityMap;
using model::kCityH;
using model::kCityW;

constexpr uint8_t kWater = 0x01;
constexpr uint8_t kNetwork = 0x02;
constexpr uint8_t kBathHouse = 0x04;
constexpr uint8_t kMarket = 0x08;
constexpr uint8_t kSchool = 0x40;
constexpr uint8_t kEntertainment = 0x80;
constexpr uint8_t kAllServices = kWater | kNetwork | kBathHouse | kMarket | kSchool | kEntertainment;

// Tile classes, tested with unsigned byte compares exactly as the handlers do.
bool open_ground(uint8_t t) { return t > 0x1C && t < 0x35; }
bool small_house(uint8_t t) { return t >= 0xC8 && t <= 0xCB; }
bool single_house(uint8_t t) { return t >= 0xCF && t <= 0xD0; }
bool house_up_to_d4(uint8_t t) { return t >= 0xC8 && t <= 0xD4; }
bool early_temple(uint8_t t) { return t >= 0xD8 && t <= 0xD9; }
bool middle_temple(uint8_t t) { return t >= 0xDA && t <= 0xDC; }

// Cells a growing building may absorb, grouped by the tests the handlers make.
bool absorbs_small(uint8_t t) { return open_ground(t) || small_house(t) || single_house(t); }
bool absorbs_below(uint8_t t, uint8_t part) { return open_ground(t) || (house_up_to_d4(t) && (part & 0x0F) == 0); }
bool absorbs_corner(uint8_t t, uint8_t part) {
    return open_ground(t) || (house_up_to_d4(t) && part != 0) || small_house(t) || single_house(t);
}
bool temple_absorbs(uint8_t t) { return open_ground(t) || small_house(t) || single_house(t) || early_temple(t); }

// A building's anchor cell, addressed the way the handlers address the grid: as
// a flat index plus offsets (+1 right, +100 down, +101, +2, +200 ...). An offset
// past the end of a row lands on the next row, as in the engine; an offset that
// leaves the grid reads as tile 0 and is never written.
class Site {
public:
    Site(CityMap& city, int x, int y) : city_(city), index_(y * kCityW + x) {}

    int row() const { return index_ / kCityW; }
    int col() const { return index_ % kCityW; }
    int coverage() const { return static_cast<int8_t>(read(city_.coverage, 0)); }
    uint8_t tile(int off) const { return read(city_.tile, off); }
    uint8_t part(int off) const { return read(city_.operational_state, off); }

    void set(int off, uint8_t tile_id, uint8_t part_index) {
        if (!inside(off)) return;
        const int i = index_ + off;
        city_.tile[i / kCityW][i % kCityW] = tile_id;
        city_.operational_state[i / kCityW][i % kCityW] = part_index;
    }
    void update_part(int off, uint8_t or_mask, uint8_t and_mask) {
        if (!inside(off)) return;
        const int i = index_ + off;
        uint8_t& p = city_.operational_state[i / kCityW][i % kCityW];
        p = static_cast<uint8_t>((p | or_mask) & and_mask);
    }

private:
    bool inside(int off) const {
        const int i = index_ + off;
        return i >= 0 && i < kCityW * kCityH;
    }
    uint8_t read(const model::CityGrid<uint8_t>& grid, int off) const {
        if (!inside(off)) return 0;
        const int i = index_ + off;
        return grid[i / kCityW][i % kCityW];
    }

    CityMap& city_;
    int index_;
};

// Fountains, run by the pass for tiles below 0xC8 (not through DS:1212).
void fountain_grow(CityMap& city, int x, int y, const DevelopmentContext& ctx) {  // 0x2BACE, tiles 0xB9/0xBA
    if (static_cast<int8_t>(city.coverage[y][x]) > 10 && ctx.population_units > 50) {
        city.tile[y][x] = 0xBD;
        city.operational_state[y][x] = 0;
        if (city.service_flags[y][x] & kWater) city.tile[y][x] = 0xBB;
    }
}

void fountain_shrink(CityMap& city, int x, int y) {  // 0x2BB48, tiles 0xBB/0xBC/0xBD
    if (static_cast<int8_t>(city.coverage[y][x]) < 10) {
        city.tile[y][x] = 0xBA;
        city.operational_state[y][x] = 0;
        if (city.service_flags[y][x] & kWater) city.tile[y][x] = 0xB9;
    }
}

}  // namespace

bool land_value_allows(model::CityMap& city, int x, int y, int threshold,
                       std::vector<std::pair<int, int>>* collapsed) {
    const int lv = city.land_value[y][x];
    if (lv > threshold) {
        if (collapsed) collapsed->emplace_back(x, y);
        city.tile[y][x] = 0xA7;
        city.coverage[y][x] = 0;
        city.operational_state[y][x] = 0;
        city.land_value[y][x] = 0;
        return true;
    }
    return false;
}

int population_units(const model::CityMap& city) {
    int total = 0;
    for (int y = 0; y < kCityH; ++y) {
        for (int x = 0; x < kCityW; ++x) {
            const uint8_t t = city.tile[y][x];
            if (t >= 0xC8 && t <= 0xD7) total += kPopulationUnitsPerCell[t - 0xC8];
        }
    }
    return total;
}

int develop_building(model::CityMap& city, int x, int y, uint8_t flags, const DevelopmentContext& ctx) {
    Site s(city, x, y);
    const int a = s.coverage();
    const int pop = ctx.population_units;
    auto has = [flags](uint8_t mask) { return (flags & mask) == mask; };
    auto pair = [&s](uint8_t id) {
        s.set(0, id, 0);
        s.set(1, id, 1);
    };
    auto square = [&s](uint8_t id) {
        s.set(0, id, 0);
        s.set(1, id, 1);
        s.set(100, id, 4);
        s.set(101, id, 5);
    };

    switch (s.tile(0)) {
        // ---- 1x1 grades ----
        case 0xC8:  // 0x296DA
            if (land_value_allows(city, x, y, 20, ctx.collapsed)) return 0;
            if (a < 0) s.set(0, 0x1D, 0);
            else if (a > 0) s.set(0, 0xC9, 0);
            return 0;
        case 0xC9:  // 0x2973E
            if (land_value_allows(city, x, y, 30, ctx.collapsed)) return 0;
            if (a < 1) s.set(0, 0xC8, 0);
            else if (a > 1 && has(kWater)) s.set(0, 0xCA, 0);
            return 0;
        case 0xCA:  // 0x297AC
            if (land_value_allows(city, x, y, 40, ctx.collapsed)) return 0;
            if (a < 2 || !has(kWater)) s.set(0, 0xC9, 0);
            else if (a > 2) s.set(0, 0xCB, 0);
            return 0;
        case 0xCB:  // 0x2981A
            if (land_value_allows(city, x, y, 48, ctx.collapsed)) return 0;
            if (a < 3 || !has(kWater)) {
                s.set(0, 0xCA, 0);
                return 0;
            }
            if (a <= 4 || !has(kNetwork) || pop < 25 || s.col() >= 99) return 0;
            if (open_ground(s.tile(1)) || small_house(s.tile(1))) {
                pair(0xCC);
                return 1;
            }
            s.set(0, 0xCF, 0);
            return 0;

        // ---- pairs ----
        case 0xCC:  // 0x2994E
            if (a < 5 || !has(kWater | kNetwork)) {
                s.set(0, 0xCB, 0);
                s.set(1, 0xCB, 0);
                return 1;
            }
            if (a <= 5 || pop < 50) return 0;
            pair(0xCD);
            return 1;
        case 0xCD:  // 0x29A20
            if (a < 6 || !has(kWater | kNetwork)) {
                pair(0xCC);
                return 1;
            }
            if (a <= 6 || !has(kMarket) || pop < 75) return 0;
            pair(0xCE);
            return 1;
        case 0xCE:  // 0x29AE7
            if (a < 7 || !has(kWater | kNetwork | kMarket)) {
                pair(0xCD);
                return 1;
            }
            if (a <= 7 || !has(kBathHouse) || pop < 100) return 0;
            pair(0xD1);
            return 1;

        // ---- one-cell houses, where a pair couldn't form ----
        case 0xCF:  // 0x29BB6
            if (a < 5 || !has(kWater | kNetwork)) {
                s.set(0, 0xCB, 0);
                return 0;
            }
            if (a <= 6 || !has(kMarket) || pop < 75) return 0;
            s.set(0, 0xD0, 0);
            return 0;
        case 0xD0:  // 0x29C2D
            if (a < 7 || !has(kWater | kNetwork | kMarket)) {
                s.set(0, 0xCF, 0);
                return 0;
            }
            if (a <= 7 || !has(kBathHouse) || pop < 100 || s.col() >= 99) return 0;
            if (!absorbs_small(s.tile(1))) return 0;
            pair(0xD1);
            return 1;

        // ---- pairs, continued ----
        case 0xD1:  // 0x29D72
            if (a < 8 || !has(kWater | kNetwork | kMarket | kBathHouse)) {
                pair(0xCE);
                return 1;
            }
            if (a <= 10 || pop < 125) return 0;
            pair(0xD2);
            return 1;
        case 0xD2:  // 0x29E41
            if (a < 11 || !has(kWater | kNetwork | kMarket | kBathHouse)) {
                pair(0xD1);
                return 1;
            }
            if (a <= 13 || !has(kSchool) || pop < 150) return 0;
            pair(0xD3);
            return 1;
        case 0xD3:  // 0x29F19
            if (a < 14 || !has(kWater | kNetwork | kMarket | kBathHouse | kSchool)) {
                pair(0xD2);
                return 1;
            }
            if (a <= 16 || pop < 175) return 0;
            pair(0xD4);
            return 1;
        case 0xD4:  // 0x29FF1 -- can grow into a 2x2
            if (a < 17 || !has(kWater | kNetwork | kMarket | kBathHouse | kSchool)) {
                pair(0xD3);
                return 1;
            }
            if (a <= 18 || !has(kEntertainment) || pop < 200 || s.row() >= 99) return 0;
            if (!absorbs_below(s.tile(100), s.part(100))) return 0;
            if (!absorbs_corner(s.tile(101), s.part(101))) return 0;
            square(0xD5);
            return 1;

        // ---- 2x2 and 3x3 ----
        case 0xD5:  // 0x2A26E
            if (a < 19 || !has(kAllServices)) {
                s.set(0, 0xD4, 0);
                s.set(1, 0xD4, 1);
                s.set(100, 0xD4, 0);
                s.set(101, 0xD4, 1);
                return 1;
            }
            if (a <= 20 || pop < 225) return 0;
            square(0xD6);
            return 1;
        case 0xD6:  // 0x2A403 -- can grow into a 3x3
            if (a < 21 || !has(kAllServices)) {
                square(0xD5);
                return 1;
            }
            if (a <= 22 || pop < 250 || s.row() >= 98 || s.col() >= 98) return 0;
            if (!absorbs_small(s.tile(2)) || !absorbs_small(s.tile(102))) return 0;
            if (!absorbs_below(s.tile(200), s.part(200))) return 0;
            if (!(open_ground(s.tile(201)) || house_up_to_d4(s.tile(201)))) return 0;
            if (!absorbs_corner(s.tile(202), s.part(202))) return 0;
            for (int dy = 0; dy < 3; ++dy)
                for (int dx = 0; dx < 3; ++dx) s.set(100 * dy + dx, 0xD7, static_cast<uint8_t>(4 * dy + dx));
            return 2;
        case 0xD7:  // 0x2A8E3 -- top grade: can only demote
            if (a < 23 || !has(kAllServices)) {
                s.set(0, 0xD6, 0);
                s.set(1, 0xD6, 1);
                s.set(2, 0xD0, 0);
                s.set(100, 0xD6, 4);
                s.set(101, 0xD6, 5);
                s.set(102, 0xD0, 0);
                s.set(200, 0xD4, 0);
                s.set(201, 0xD4, 1);
                s.set(202, 0xD0, 0);
                return 2;
            }
            return 0;

        // ---- 0xD8-0xDF: temple construction stages ----
        case 0xD8:  // 0x2AA96
            if (a > 2 && pop > 5) s.set(0, 0xD9, 0);
            return 0;
        case 0xD9: {  // 0x2AAE8 -- grows downwards
            if (a < 3) {
                s.set(0, 0xD8, 0);
                return 0;
            }
            if (a <= 5 || pop < 15 || s.row() >= 99) return 0;
            const uint8_t below = s.tile(100);
            if (open_ground(below) || small_house(below) || early_temple(below)) {
                s.set(0, 0xDA, 0);
                s.set(100, 0xDA, 4);
            }
            return 0;
        }
        case 0xDA:  // 0x2AC06
            if (a < 6) {
                s.set(0, 0xD9, 0);
                s.set(100, 0x1D, 0);
                return 0;
            }
            if (a > 10 && pop >= 25) {
                s.set(0, 0xDB, 0);
                s.set(100, 0xDB, 4);
            }
            return 0;
        case 0xDB:  // 0x2ACC4
            if (a < 11) {
                s.set(0, 0xDA, 0);
                s.set(100, 0xDA, 4);
                return 0;
            }
            if (a > 14 && pop >= 40) {
                s.set(0, 0xDC, 0);
                s.set(100, 0xDC, 4);
            }
            return 0;
        case 0xDC:  // 0x2AD6F -- widens into a 2x2
            if (a < 15) {
                s.set(0, 0xDB, 0);
                s.set(100, 0xDB, 4);
                return 0;
            }
            if (a <= 17 || pop < 75 || s.col() >= 99) return 0;
            if (!(temple_absorbs(s.tile(1)) || (middle_temple(s.tile(1)) && (s.part(1) & 0x0F) == 0))) return 0;
            if (!(temple_absorbs(s.tile(101)) || (middle_temple(s.tile(101)) && (s.part(101) & 0x0F) != 0))) return 0;
            square(0xDD);
            return 1;
        case 0xDD:  // 0x2B060
            if (a < 18) {
                s.set(0, 0xDC, 0);
                s.set(1, 0x1D, 0);
                s.set(100, 0xDC, 4);
                s.set(101, 0x1D, 0);
                return 1;
            }
            if (a <= 20 || pop < 125) return 0;
            square(0xDE);
            return 1;
        case 0xDE:  // 0x2B1C4 -- widens into a 3x2
            if (a < 21) {
                square(0xDD);
                return 1;
            }
            if (a <= 23 || pop < 200 || s.col() >= 98) return 0;
            if (!(temple_absorbs(s.tile(2)) || (middle_temple(s.tile(2)) && (s.part(2) & 0x0F) == 0))) return 0;
            if (!(temple_absorbs(s.tile(102)) || (middle_temple(s.tile(102)) && (s.part(102) & 0x04) != 0))) return 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 3; ++dx) s.set(100 * dy + dx, 0xDF, static_cast<uint8_t>(4 * dy + dx));
            return 1;
        case 0xDF:  // 0x2B551 -- top stage: can only shrink
            if (a < 24) {
                s.set(0, 0xDE, 0);
                s.set(1, 0xDE, 1);
                s.set(2, 0x1D, 0);
                s.set(100, 0xDE, 4);
                s.set(101, 0xDE, 5);
                s.set(102, 0x1D, 0);
                return 1;
            }
            return 0;

        // ---- bath houses ----
        case 0xE8: {  // 0x2B661
            // The bath house's "connected" bit, which its service handler gates
            // on, is simply whether its own cell has water.
            if (city.service_flags[y][x] & kWater) s.update_part(0, 0x10, 0xFF);
            else s.update_part(0, 0x00, 0xEF);
            if (a <= 12 || pop < 40 || s.col() >= 99 || s.row() >= 99) return 0;
            const uint8_t right = s.tile(1);
            if (!(absorbs_small(right) || right == 0xE8)) return 0;
            const uint8_t below = s.tile(100);
            if (!(absorbs_below(below, s.part(100)) || below == 0xE8)) return 0;
            const uint8_t corner = s.tile(101);
            const bool corner_ok = open_ground(corner) || small_house(corner) || single_house(corner) ||
                                   corner == 0xE8 || (house_up_to_d4(corner) && (s.part(101) & 0x01) != 0);
            if (!corner_ok) return 0;
            square(0xEA);
            return 1;
        }
        case 0xEA: {  // 0x2B95B
            int skip = 0;
            if (a < 12) {
                s.set(0, 0xE8, 0x40);
                s.set(1, 0xE8, 0x40);
                s.set(100, 0x1D, 0);
                s.set(101, 0x1D, 0);
                skip = 1;
            }
            const bool watered = (city.service_flags[y][x] & kWater) != 0;
            for (int off : {0, 1, 100, 101}) s.update_part(off, watered ? 0x10 : 0x00, watered ? 0xFF : 0xEF);
            return skip;
        }

        default:
            return 0;  // DS:1212 points every other id >= 0xC8 at a bare retf
    }
}

// 0x29624: a burning tile. When the generator's previous low 7 bits
// (2EF9:028A) exceed 90 it burns out a stage (0xA8 -> 0xA7, 0xAB -> 0xAA, ...);
// otherwise, away from the map edge, the fire spreads to the neighbour
// 3496:1DE4[random & 7] if that is a building. Both paths first add 32 to
// 028A, mod 128.
static void burning_tile(model::CityMap& city, int col, int row, const DevelopmentContext& ctx) {
    if (!ctx.random) return;
    month::Random& r = *ctx.random;
    if (r.prev_low7 > 90) {
        r.prev_low7 = (r.prev_low7 + 32) & 0x7F;
        --city.tile[row][col];
        return;
    }
    if (row == 0 || row >= 99 || col == 0 || col >= 99) return;
    r.prev_low7 = (r.prev_low7 + 32) & 0x7F;
    static constexpr int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static constexpr int kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    const int d = r.walk & 7;
    if (city.tile[row + kDy[d]][col + kDx[d]] < 0xC8) return;
    if (ctx.spread_fire) ctx.spread_fire(col, row, d);
}

void develop_row(model::CityMap& city, int row, const DevelopmentContext& ctx) {
    for (int col = 0; col < kCityW; ++col) {
        const uint8_t t = city.tile[row][col];
        if (t < 0xC8) {
            city.land_value[row][col] = 0;
            if (t == 0xB9 || t == 0xBA) fountain_grow(city, col, row, ctx);
            else if (t >= 0xBB && t <= 0xBD) fountain_shrink(city, col, row);
            else if (t == 0xA8 || t == 0xAB || t == 0xAE || t == 0xB1) burning_tile(city, col, row, ctx);
            continue;
        }
        if (t < 0xD7) {
            service::evolve_land_value(city, col, row, ctx.land_value_growth);
        } else {
            city.land_value[row][col] = 0;
        }
        if (city.operational_state[row][col] & 0x0F) continue;  // not a building's anchor
        col += develop_building(city, col, row, city.service_flags[row][col], ctx);
    }
}

}  // namespace gaius::systems::housing
