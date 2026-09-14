// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/campaign.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

#include "systems/actors.hpp"
#include "systems/battle.hpp"
#include "systems/military.hpp"
#include "systems/plebs.hpp"

namespace gaius::systems::campaign {

namespace {

// The engine addresses the tile layer by flat index and doesn't bound it: a
// write past the last row or column spills into the next row or out of the
// layer. Gaius follows the flat index inside the map and ignores the rest;
// out of the map a read is 0 (INFERENCE: what lies beyond the layer isn't
// modeled).
struct Tiles {
    model::CityMap& city;
    uint8_t get(int i) const {
        if (i < 0 || i >= 10000) return 0;
        return city.tile[static_cast<size_t>(i / 100)][static_cast<size_t>(i % 100)];
    }
    void put(int i, uint8_t v) {
        if (i < 0 || i >= 10000) return;
        city.tile[static_cast<size_t>(i / 100)][static_cast<size_t>(i % 100)] = v;
    }
};

bool grass(uint8_t t) { return t >= 0x1E && t <= 0x35; }

// 0x074A1: the eight neighbours of a cell, clockwise from north, as land (or
// the map's edge) or water.
struct Around {
    std::array<uint8_t, 8> flag{};
    int land = 0;        // DS:0x788
    int vertical = 0;    // DS:0x78C, land (or edge) above or below
    int horizontal = 0;  // DS:0x78E, left or right
    int se_nw = 0;       // DS:0x790
    int ne_sw = 0;       // DS:0x792
    int longest = 0;     // DS:0x796, the longest run of land going round
};

Around look_around(const Tiles& t, int x, int y) {
    Around a;
    int run = 0;
    const int c = y * 100 + x;
    auto edge_vertical = [&](size_t i) {
        a.flag[i] = 1;
        ++a.land;
        ++a.vertical;
        ++run;
    };
    auto edge_horizontal = [&](size_t i) {
        a.flag[i] = 1;
        ++a.land;
        ++a.horizontal;
        ++run;
    };
    auto land = [&](size_t i, int* extra) {
        a.flag[i] = 1;
        ++a.land;
        ++run;
        if (extra) ++*extra;
    };
    for (size_t i = 0; i < 9; ++i) {
        switch (i) {
            case 0:
                if (y == 0) edge_vertical(0);
                else if (t.get(c - 100) != 0) land(0, &a.vertical);
                else run = 0;
                break;
            case 1:
                if (y == 0) edge_vertical(1);
                else if (x == 99) edge_horizontal(1);
                else if (t.get(c - 99) != 0) land(1, &a.ne_sw);
                else run = 0;
                break;
            case 2:
                if (x == 99) edge_horizontal(2);
                else if (t.get(c + 1) != 0) land(2, &a.horizontal);
                else run = 0;
                break;
            case 3:
                if (x == 99) edge_horizontal(3);
                else if (y == 99) edge_vertical(3);
                else if (t.get(c + 101) != 0) land(3, &a.se_nw);
                else run = 0;
                break;
            case 4:
                if (y == 99) edge_vertical(4);
                else if (t.get(c + 100) != 0) land(4, &a.vertical);
                else run = 0;
                break;
            case 5:
                if (x == 0) edge_horizontal(5);
                else if (y == 99) edge_vertical(5);
                else if (t.get(c + 99) != 0) land(5, &a.ne_sw);
                else run = 0;
                break;
            case 6:
                if (x == 0) edge_horizontal(6);
                else if (t.get(c - 1) != 0) land(6, &a.horizontal);
                else run = 0;
                break;
            case 7:
                if (x == 0) edge_horizontal(7);
                else if (y == 0) edge_vertical(7);
                else if (t.get(c - 101) != 0) land(7, &a.se_nw);
                else run = 0;
                break;
            default:  // slot 8: north again, so a run can wrap round
                if (y == 0 || t.get(c - 100) != 0) ++run;
                else run = 0;
                break;
        }
        // The orthogonal land in slots 0/2/4/6 and every edge also counts
        // DS:0x78A, which nothing here reads.
        if (run > a.longest) a.longest = run;
    }
    return a;
}

// 3496:15D8: neighbour pattern (0 water, 1 land, 2 either), shore tile, and
// whether it alternates with the next tile (0) or not (1).
struct Shore {
    std::array<uint8_t, 8> flag;
    uint8_t tile;
    uint8_t fixed;
};
constexpr std::array<Shore, 12> kShores = {{
    {{0, 2, 1, 2, 1, 2, 1, 2}, 7, 0},  {{1, 2, 1, 2, 0, 2, 1, 2}, 1, 0},  {{1, 2, 0, 2, 1, 2, 1, 2}, 10, 0},
    {{1, 2, 1, 2, 1, 2, 0, 2}, 4, 0},  {{0, 2, 1, 2, 1, 2, 0, 2}, 27, 1}, {{1, 2, 1, 2, 0, 2, 0, 2}, 25, 1},
    {{0, 2, 0, 2, 1, 2, 1, 2}, 28, 1}, {{1, 2, 0, 2, 0, 2, 1, 2}, 26, 1}, {{1, 2, 2, 2, 2, 2, 1, 0}, 22, 0},
    {{1, 0, 1, 2, 2, 2, 2, 2}, 16, 0}, {{2, 2, 1, 0, 1, 2, 2, 2}, 19, 0}, {{2, 2, 2, 2, 1, 0, 1, 2}, 13, 0},
}};

// 0x06F42
void lay_lakes(Tiles& t, month::Random& r) {
    for (int i = 0; i < 10000; ++i) t.put(i, 0x1D);
    for (int y = 1; y < 100; ++y) {
        for (int x = 1; x < 99; ++x) {
            r.advance();
            if (2 > (r.draw & 0x7FF)) {
                const int c = y * 100 + x;
                for (int d : {0, 1, 100, 101}) t.put(c + d, 0);
            }
        }
    }
    for (int y = 0; y < 100; ++y) {
        for (int x = 1; x < 100; ++x) {
            r.advance();
            const int c = y * 100 + x;
            if (r.low7 >= 0x50 || t.get(c) != 0) continue;
            r.advance();
            if (r.walk >= 0x32) {
                for (int d : {0, -1, 100, 99}) t.put(c + d, 0);
            }
            if (r.low7 < 0x32) {
                for (int d : {0, 1, 100, 101}) t.put(c + d, 0);
            }
        }
    }
}

// 0x072DD
void erode(Tiles& t) {
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            const int c = y * 100 + x;
            if (t.get(c) == 0) continue;
            const Around a = look_around(t, x, y);
            if (a.vertical == 0 || a.horizontal == 0 || a.land <= 2 || a.longest <= 2 ||
                (a.land >= 5 && a.se_nw == 2 && a.longest == 3) || (a.land >= 5 && a.ne_sw == 2 && a.longest == 3)) {
                t.put(c, 0);
            }
        }
    }
}

// 0x07228 with 0x07410
void lay_shores(Tiles& t, int& variant) {
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            const int c = y * 100 + x;
            if (t.get(c) != 0x1D) continue;
            const Around a = look_around(t, x, y);
            if (a.land >= 8) continue;
            for (const Shore& s : kShores) {
                bool fits = true;
                for (size_t k = 0; k < 8 && fits; ++k) fits = s.flag[k] == 2 || s.flag[k] == a.flag[k];
                if (!fits) continue;
                t.put(c, s.tile);
                if (s.fixed != 1) {
                    if (++variant > 1) variant = 0;
                    t.put(c, static_cast<uint8_t>(s.tile + variant));
                }
                break;
            }
        }
    }
}

// 0x07180
void vary_grass(Tiles& t, month::Random& r) {
    for (int i = 0; i < 10000; ++i) {
        if (t.get(i) != 0x1D) continue;
        r.advance();
        r.advance();
        t.put(i, static_cast<uint8_t>((r.low7 & 1) ? 0x2E + (r.low7 & 7) : 0x1E + (r.low7 & 15)));
    }
}

// 0x07C12 with its steps 0x07CD9, 0x07FC1, 0x08161.
bool lay_river(Tiles& t, month::Random& r) {
    const auto backup = t.city.tile;  // 0x07BBC (the engine keeps it in the C9D4 layer)
    struct River {
        int dir = 4, row = 1, col = 0, state = 0;  // DS:0x4F52, 4F4E, 4F50, 4F4C
    } v;
    auto straight = [&] {
        const int here = v.row * 100 + v.col;
        switch (v.dir) {
            case 4:
                if (grass(t.get(here + 100))) {
                    t.put(here, 0x4A);
                    if (++v.row > 100) v.state = 2;
                } else if (grass(t.get(here - 1))) {
                    t.put(here, 0x6A);
                    v.dir = 6;
                    if (--v.col < 0) v.state = 2;
                } else if (grass(t.get(here + 1))) {
                    t.put(here, 0x6E);
                    v.dir = 2;
                    if (++v.col >= 100) v.state = 2;
                } else {
                    v.state = 1;
                }
                break;
            case 6:
                if (grass(t.get(here - 1))) {
                    t.put(here, 0x56);
                    if (--v.col < 0) v.state = 2;
                } else if (grass(t.get(here + 100))) {
                    t.put(here, 0x62);
                    v.dir = 4;
                    if (++v.row >= 100) v.state = 2;
                } else {
                    v.state = 1;
                }
                break;
            case 2:
                if (grass(t.get(here + 1))) {
                    t.put(here, 0x56);
                    if (++v.col >= 100) v.state = 2;
                } else if (grass(t.get(here + 100))) {
                    t.put(here, 0x66);
                    v.dir = 4;
                    if (++v.row >= 100) v.state = 2;
                } else {
                    v.state = 1;
                }
                break;
            default: break;
        }
    };
    // Heading west or east, bend south (or finish at the bottom row).
    auto bend_south = [&](uint8_t piece) {
        const int here = v.row * 100 + v.col;
        if (v.row >= 99) {
            t.put(here, piece);
            v.state = 2;
        } else if (grass(t.get(here + 100))) {
            t.put(here, piece);
            v.dir = 4;
            if (++v.row >= 100) v.state = 2;
        } else {
            straight();
        }
    };
    auto turn = [&](bool west) {
        const int here = v.row * 100 + v.col;
        switch (v.dir) {
            case 4:
                if (west && grass(t.get(here - 1))) {
                    t.put(here, 0x6A);
                    v.dir = 6;
                    if (--v.col < 0) v.state = 2;
                } else if (!west && grass(t.get(here + 1))) {
                    t.put(here, 0x6E);
                    v.dir = 2;
                    if (++v.col >= 100) v.state = 2;
                } else {
                    straight();
                }
                break;
            case 6: bend_south(0x62); break;
            case 2: bend_south(0x66); break;
            default: break;
        }
    };

    for (int attempt = 0; attempt < 3; ++attempt) {
        v = River{};
        v.col = r.low7 / 2 + 24;
        while (!grass(t.get(v.col))) {
            r.advance();
            v.col = r.low7 / 2 + 24;
        }
        t.put(v.col, 0x4A);
        while (v.state == 0) {
            r.advance();
            if (r.low7 <= 60) straight();
            else if (r.low7 <= 90) turn(true);
            else turn(false);
        }
        if (v.state == 2) return true;
        t.city.tile = backup;  // 0x07BE7
    }
    return false;
}

void set(model::CityState& s, int ds, int v) { model::set_global_word(s, static_cast<uint16_t>(ds), v); }
int g(const model::CityState& s, uint16_t ds) { return model::global_word(s, ds); }

}  // namespace

void generate_city(model::CityMap& city, month::Random& random, int& shore_variant) {
    Tiles t{city};
    for (int tries = 0; tries < 1000; ++tries) {  // the engine has no limit
        random.advance();
        lay_lakes(t, random);
        erode(t);
        erode(t);
        erode(t);
        lay_shores(t, shore_variant);
        vary_grass(t, random);
        if (lay_river(t, random)) return;
    }
}

void new_game(model::CityState& state, month::Random& random) {
    set(state, 0x6C32, -13);
    set(state, 0x6C26, 0);
    set(state, 0x6C24, 0);
    set(state, 0x6C30, 1);
    set(state, 0x6C2E, 100);
    set(state, 0x6C2C, 10);
    set(state, 0x6C2A, 1);
    set(state, 0x6C28, 20);
    state.table_50.assign(50, 0);  // 0x05B6B
    set(state, 0x6C98, -12);
    set(state, 0x6C96, random.low7 & 15);
    random.advance();
    set(state, 0x6C94, random.low7 & 3);
    set(state, 0x6C00, 5);
    set(state, 0x6BE6, 0);
}

void start_province(model::CityState& state, const formats::empire2::EmpireMap& province_map,
                    month::Random& random, int difficulty, int& shore_variant) {
    state.empire = province_map;
    generate_city(state.city, random, shore_variant);

    // 0x05730
    random.advance();
    for (int ds : {0x6C26, 0x6C24, 0x6C22, 0x6C20, 0x6C1E, 0x6C6A, 0x6C68, 0x6C66, 0x6C9A, 0x6C7A, 0x6C72, 0x6C70,
                        0x6CA0, 0x6C9C, 0x6C9E, 0x6C10})
        set(state, ds, 0);
    set(state, 0x6C1C, 1);

    const int rank = g(state, 0x6C30);
    int funds = g(state, 0x6C0C);
    for (int above = 1; above <= 3; ++above) {
        if (rank > above && funds >= 5000) funds -= 1000;
    }
    if (rank > 4 && funds >= 4000) {
        funds -= (rank - 4) * 200;
        if (funds < 4000) funds = 4000;
    }
    set(state, 0x6CA2, funds);
    set(state, 0x6C98, g(state, 0x6C98) + 1);

    set(state, plebs::kPlebs, 120);
    set(state, 0x6C64, 50);
    for (int ds : {plebs::kFirePrevention, plebs::kBuildingMaintenance, plebs::kRoadMaintenance,
                        plebs::kConstruction, plebs::kArmyDuty})
        set(state, ds, 10);
    set(state, plebs::kUnassigned, 20);
    set(state, 0x6C0A, 0);
    set(state, plebs::kWelfare, 88);
    set(state, military::kArmyWages, 20);
    set(state, military::kConscription, 10);
    set(state, 0x6C54, 0);
    set(state, 0x6C04, 5);
    set(state, 0x6C02, 5);
    for (int ds : {0x6C3C, 0x6C3A, 0x6C38, 0x6C36, 0x6C34, 0x6C82, 0x6C80, 0x6C0E, 0x6C52, 0x6C50, 0x6C4C, 0x6C4A,
                        0x6C48, 0x6C44, 0x6C42, 0x6C40, 0x6C3E})
        set(state, ds, 0);
    set(state, military::kRegulars, 2);
    set(state, 0x6C00, 5);  // and DS:0x6BFE = 5, the simulation's industrial rate sum
    for (int ds : {0x6BFA, 0x6BF8, 0x6BF6, 0x6BF4, 0x6BF2, 0x6BF0, 0x6C8A, 0x6BCC, 0x6BCA, 0x6BC8, 0x6BC6, 0x6BC4,
                        0x6BC2, 0x6BC0, 0x6BBE, 0x6BBC, 0x6BB8, 0x6BB6, 0x6BB4, 0x6BB2, 0x6BB0, 0x6BFC, 0x6BAE, 0x6BAC,
                        0x6BAA})
        set(state, ds, 0);
    set(state, 0x6BBA, 50);

    // 0x05A9A: the four other city layers.
    for (auto& row : state.city.service_flags) row.fill(0);
    for (auto& row : state.city.coverage) row.fill(0);
    for (auto& row : state.city.operational_state) row.fill(0);
    for (auto& row : state.city.land_value) row.fill(0);
    // 0x05ADD: every walker, and their counters.
    for (int slot = 0; slot < static_cast<int>(model::kActorCount); ++slot) actors::release(state, slot);
    for (int ds : {0x6C1A, 0x6C18, 0x6C14, 0x6C16, 0x6C12}) set(state, ds, 0);
    // 0x05B17, 0x05B33, 0x05B4F, 0x05B80, 0x05B95, 0x05BAA: records, the
    // workshops per goods, population milestones, and the histories.
    for (std::vector<uint8_t>* table : {&state.table_480, &state.table_720, &state.table_120, &state.table_8,
                                        &state.table_10, &state.table_60_a, &state.table_60_b, &state.table_60_c,
                                        &state.table_60_d, &state.table_72})
        std::fill(table->begin(), table->end(), uint8_t{0});

    // 0x0621F: the Prima Cohors on the first province cell holding the city.
    for (int y = 0; y < 40; ++y) {
        bool placed = false;
        for (int x = 0; x < 40 && !placed; ++x) {
            if (state.empire.cells[static_cast<size_t>(y * 40 + x)] != 0x4A) continue;
            const int slot = actors::spawn(state, military::kCohortType, x, y);
            if (slot >= 0) {
                auto& r = state.objects[static_cast<size_t>(slot)].raw;
                r[0x2E] = static_cast<uint8_t>(x);
                r[0x2F] = static_cast<uint8_t>(y);
                r[0x2A] = static_cast<uint8_t>(g(state, 0x6C12) - 1);
                r[0x31] = military::kCohortMobilized;
                r[military::kCohortMorale] = 5;
                r[military::kCohortRegulars] = 2;
            }
            placed = true;
        }
        if (placed) break;
    }

    battle::load_race(state);
    const int construction_need = plebs::set_needs(state, difficulty);  // 0x2DC72
    plebs::assign(state);                                                // 0x2DD21
    plebs::set_thresholds(state, construction_need);                     // 0x2DE0F

    // 0x06307: the highway's entry.
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            uint8_t& cell = state.empire.cells[static_cast<size_t>(y * 40 + x)];
            if (cell != 0x41) continue;
            set(state, 0x6C90, x);
            set(state, 0x6C8E, y);
            cell = 0x78;
            return;
        }
    }
}

}  // namespace gaius::systems::campaign
