// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/service.hpp"

#include <algorithm>

namespace gaius::systems::service {

namespace {

// Shared square-radius iteration -- Chebyshev distance <= radius, clipped to
// the grid, matching the bounds arithmetic at the top of 0x2C577/0x2C6AF/
// 0x2C7BB.
template <typename Fn>
void for_each_in_square(int x, int y, int radius, Fn&& fn) {
    int y0 = std::max(0, y - radius);
    int y1 = std::min(model::kCityH - 1, y + radius);
    int x0 = std::max(0, x - radius);
    int x1 = std::min(model::kCityW - 1, x + radius);
    for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
            fn(cy, cx);
        }
    }
}

// 7BB4.10, the gate the road family and bath houses test.
bool connected(const model::CityMap& city, int x, int y) { return (city.operational_state[y][x] & 0x10) != 0; }

// The engine does its layer arithmetic on bytes: adds wrap, and clamps
// compare the byte as signed.
inline int8_t as_signed(uint8_t v) { return static_cast<int8_t>(v); }

inline int8_t add_wrapping(int8_t value, int delta) {
    return static_cast<int8_t>(static_cast<uint8_t>(static_cast<uint8_t>(value) + static_cast<uint8_t>(delta)));
}

}  // namespace

ServiceState::ServiceState() {
    for (auto& row : coverage_ceiling) row.fill(0x3F);
}

void reset_tick(model::CityMap& city, ServiceState& service) {
    for (int y = 0; y < model::kCityH; ++y) {
        for (int x = 0; x < model::kCityW; ++x) {
            city.service_flags[y][x] &= 0x12;
            city.coverage[y][x] = 0;
            service.coverage_ceiling[y][x] = 0x3F;
        }
    }
}

void derive_network_flags(model::CityMap& city) {
    for (int y = 0; y < model::kCityH; ++y) {
        for (int x = 0; x < model::kCityW; ++x) {
            uint8_t& f = city.service_flags[y][x];
            if (f & 0x10) {
                f = static_cast<uint8_t>((f | 0x02) & 0xEF);
            } else {
                f = static_cast<uint8_t>(f & 0xFD);
            }
        }
    }
}

void apply_coverage(model::CityMap& city, ServiceState& service, int x, int y, int delta, int radius, int ceiling) {
    const uint8_t cap_arg = static_cast<uint8_t>(ceiling);
    for_each_in_square(x, y, radius, [&](int cy, int cx) {
        uint8_t& cap = service.coverage_ceiling[cy][cx];
        if (as_signed(cap) > as_signed(cap_arg)) cap = cap_arg;
        uint8_t& cov = city.coverage[cy][cx];
        cov = static_cast<uint8_t>(cov + static_cast<uint8_t>(delta));
        if (as_signed(cov) > as_signed(cap)) cov = cap;
    });
}

void apply_land_value(model::CityMap& city, int x, int y, int delta, int radius, int ceiling) {
    const int8_t cap = as_signed(static_cast<uint8_t>(ceiling));
    for_each_in_square(x, y, radius, [&](int cy, int cx) {
        int8_t& lv = city.land_value[cy][cx];
        lv = add_wrapping(lv, delta);
        if (lv > cap) lv = cap;
    });
}

void evolve_land_value(model::CityMap& city, int x, int y, int growth) {
    int8_t& lv = city.land_value[y][x];
    lv = add_wrapping(lv, (city.service_flags[y][x] & 0x20) ? growth : -2);
    if (lv > 50) lv = 50;
    if (lv < -8) lv = -8;
}

void apply_flags(model::CityMap& city, int x, int y, int radius, uint8_t mask) {
    for_each_in_square(x, y, radius, [&](int cy, int cx) { city.service_flags[cy][cx] |= mask; });
}

void apply_tile_36_3b(model::CityMap& city, ServiceState& service, int x, int y) {
    if (connected(city, x, y)) apply_coverage(city, service, x, y, 1, 1, kCommonCoverageCeiling);
}

void apply_tile_3c_3f(model::CityMap& city, ServiceState& service, int x, int y) {
    apply_coverage(city, service, x, y, connected(city, x, y) ? 2 : 1, 1, kCommonCoverageCeiling);
}

void apply_tile_40(model::CityMap& city, ServiceState& service, int x, int y) {
    apply_coverage(city, service, x, y, connected(city, x, y) ? 3 : 2, 1, kCommonCoverageCeiling);
}

void apply_forum(model::CityMap& city, ServiceState& service, int x, int y, ForumTier tier) {
    const int n = static_cast<int>(tier);                                   // 1..4
    apply_coverage(city, service, x, y, 1, n + 1, kCommonCoverageCeiling);  // radius 2/3/4/5
    apply_flags(city, x, y, 4 + 2 * n, 0x20);                               // radius 6/8/10/12
}

void apply_bath_houses(model::CityMap& city, ServiceState& service, int x, int y) {
    if (!connected(city, x, y)) return;
    apply_coverage(city, service, x, y, 1, 2, kCommonCoverageCeiling);
    apply_flags(city, x, y, 3, 0x04);
}

void apply_oracle(model::CityMap& city, ServiceState& service, int x, int y) {
    apply_coverage(city, service, x, y, 2, 8, kCommonCoverageCeiling);
    apply_land_value(city, x, y, -2, 5, 32);
}

void apply_school_or_hospital(model::CityMap& city, ServiceState& service, int x, int y) {
    apply_coverage(city, service, x, y, 1, 3, kCommonCoverageCeiling);
    apply_flags(city, x, y, 4, 0x40);
}

void apply_prefecture(model::CityMap& city, ServiceState& service, int x, int y) {
    apply_coverage(city, service, x, y, 1, 2, 8);
    apply_flags(city, x, y, 4, 0x20);
    apply_land_value(city, x, y, -2, 3, 48);
}

void apply_barracks(model::CityMap& city, ServiceState& service, int x, int y) {
    apply_coverage(city, service, x, y, 1, 3, 5);
    apply_land_value(city, x, y, -3, 5, 32);
}

void apply_theater(model::CityMap& city, ServiceState& service, int x, int y) {
    apply_coverage(city, service, x, y, 1, 3, kCommonCoverageCeiling);
    apply_flags(city, x, y, 4, 0x80);
}

void apply_coliseum(model::CityMap& city, ServiceState& service, int x, int y) {
    apply_coverage(city, service, x, y, 1, 4, kCommonCoverageCeiling);
    apply_flags(city, x, y, 6, 0x80);
}

void apply_hippodrome(model::CityMap& city, ServiceState& service, int x, int y) {
    apply_coverage(city, service, x, y, 1, 5, kCommonCoverageCeiling);
    apply_flags(city, x, y, 7, 0x80);
}

void apply_heavy_industry(model::CityMap& city, ServiceState& service, int x, int y) {
    apply_coverage(city, service, x, y, 1, 4, 2);
}

void apply_market(model::CityMap& city, ServiceState& service, int x, int y) {
    apply_coverage(city, service, x, y, 1, 1, 16);
    apply_flags(city, x, y, 6, 0x08);
}

void apply_tile_f5_f6(model::CityMap& city, ServiceState& service, int x, int y) {
    apply_coverage(city, service, x, y, 1, 3, 3);
}

void apply_housing_tier(model::CityMap& city, ServiceState& service, int x, int y) {
    const uint8_t t = city.tile[y][x];
    if (t < 0xC8 || t > 0xD7) return;
    int adj = 0, radius = 1, ceiling = kCommonCoverageCeiling;
    if (t <= 0xC9) {
        adj = -1;
        radius = 1;
        ceiling = 4;
    } else if (t <= 0xCC) {
        adj = 0;
        radius = 1;
        ceiling = 8;
    } else if (t <= 0xD0) {
        adj = 1;
        radius = 1;
    } else if (t <= 0xD4) {
        adj = 1;
        radius = 2;
    } else if (t <= 0xD6) {
        adj = 2;
        radius = 1;
    } else {
        adj = 2;
        radius = 2;
    }
    apply_coverage(city, service, x, y, service.housing_coverage_base + adj, radius, ceiling);
}

void apply_tile_94_95(model::CityMap& city, ServiceState& service, int x, int y) {
    apply_coverage(city, service, x, y, 1, 2, 8);
    apply_land_value(city, x, y, -2, 2, 64);
}

void apply_tile_a2_b2(model::CityMap& city, ServiceState& service, int x, int y) {
    apply_coverage(city, service, x, y, -2, 3, 8);
    apply_coverage(city, service, x, y, -2, 1, 8);
}

void apply_tile_b9_bb_bc(model::CityMap& city, ServiceState& service, int x, int y) {
    if (city.service_flags[y][x] & 0x01) apply_coverage(city, service, x, y, 1, 2, kCommonCoverageCeiling);
}

void apply_temple_stage(model::CityMap& city, ServiceState& service, int x, int y) {
    const uint8_t t = city.tile[y][x];
    if (t >= 0xD8 && t <= 0xDB) {
        apply_coverage(city, service, x, y, 1, 2, kCommonCoverageCeiling);
        apply_land_value(city, x, y, -2, 2, 53);
    } else if (t >= 0xDC && t <= 0xDF) {
        apply_coverage(city, service, x, y, 1, 3, kCommonCoverageCeiling);
        apply_land_value(city, x, y, -2, 3, 37);
    }
}

void reset_scan_counters(ServiceState& service) {
    service.road_count = 0;
    service.building_count = 0;
    service.market_count = 0;
    service.industry_count = 0;
    service.school_count = 0;
}

void dispatch_tile(model::CityMap& city, ServiceState& service, int x, int y) {
    const uint8_t tile = city.tile[y][x];
    if (tile <= 0x35) return;  // routine 0x2BBBB never dispatches these

    // What every counted handler does first (0x2BC26, 0x2BDA2 and their
    // siblings): bump its counters, then run a due event in place of its
    // usual effect.
    if (tile >= 0x36 && tile <= 0x40) {
        if (++service.road_count == service.road_wear_target) {
            city.tile[y][x] = 0x1D;  // 0x2C4EA
            city.operational_state[y][x] = 0;
            if (service.on_event) service.on_event(CityEvent::RoadWear, x, y);  // its message, 0x27CBA
            service.road_wear_target = -1;
            return;
        }
    } else if (tile == 0xF4) {
        ++service.market_count;
    } else if ((tile >= 0xC8 && tile <= 0xF3 && tile != 0xE9) || tile == 0xF5 || tile == 0xF6) {
        if (tile == 0xEC || tile == 0xED) ++service.school_count;
        if (tile == 0xF3) ++service.industry_count;
        const int n = ++service.building_count;
        if (service.on_event && n == service.collapse_target) {
            service.on_event(CityEvent::Collapse, x, y);
            service.collapse_target = -1;
            return;
        }
        if (service.on_event && n == service.fire_target) {
            service.on_event(CityEvent::Fire, x, y);
            service.fire_target = -1;
            return;
        }
    }
    switch (tile) {
        case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A: case 0x3B:
            apply_tile_36_3b(city, service, x, y);
            break;
        case 0x3C: case 0x3D: case 0x3E: case 0x3F:
            apply_tile_3c_3f(city, service, x, y);
            break;
        case 0x40:
            apply_tile_40(city, service, x, y);
            break;
        case 0x94: case 0x95:
            apply_tile_94_95(city, service, x, y);
            break;
        case 0xA2: case 0xA3: case 0xA7: case 0xA8: case 0xA9: case 0xAA: case 0xAB:
        case 0xAC: case 0xAD: case 0xAE: case 0xAF: case 0xB0: case 0xB1: case 0xB2:
            apply_tile_a2_b2(city, service, x, y);
            break;
        case 0xB9: case 0xBB: case 0xBC:
            apply_tile_b9_bb_bc(city, service, x, y);
            break;
        case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
            apply_temple_stage(city, service, x, y);
            break;
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF:
        case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD7:
            apply_housing_tier(city, service, x, y);
            break;
        case 0xE0: case 0xE1:
            apply_forum(city, service, x, y, ForumTier::Tier1);
            break;
        case 0xE2: case 0xE3:
            apply_forum(city, service, x, y, ForumTier::Tier2);
            break;
        case 0xE4: case 0xE5:
            apply_forum(city, service, x, y, ForumTier::Tier3);
            break;
        case 0xE6: case 0xE7:
            apply_forum(city, service, x, y, ForumTier::Tier4);
            break;
        case 0xE8: case 0xEA:
            apply_bath_houses(city, service, x, y);
            break;
        case 0xEB:
            apply_oracle(city, service, x, y);
            break;
        case 0xEC: case 0xED:
            apply_school_or_hospital(city, service, x, y);
            break;
        case 0xEE:
            apply_prefecture(city, service, x, y);
            break;
        case 0xEF:
            apply_barracks(city, service, x, y);
            break;
        case 0xF0:
            apply_theater(city, service, x, y);
            break;
        case 0xF1:
            apply_coliseum(city, service, x, y);
            break;
        case 0xF2:
            apply_hippodrome(city, service, x, y);
            break;
        case 0xF3:
            apply_heavy_industry(city, service, x, y);
            break;
        case 0xF4:
            apply_market(city, service, x, y);
            break;
        case 0xF5: case 0xF6:
            apply_tile_f5_f6(city, service, x, y);
            break;
        default:
            break;  // no-op in the engine, or a handler not transcribed yet -- see the header
    }
}

namespace {

// 3496:1BE0, the pipe class of every tile: 1-6 pipe pieces, 0xFF a water
// source (reservoir), 0x0B a fountain, 0 anything else.
uint8_t pipe_class(uint8_t t) {
    switch (t) {
        case 0x43: case 0x44: case 0x72: case 0x73: case 0x74: case 0x75:
        case 0x8E: case 0x8F: case 0x90: case 0x91: case 0xA0:
            return 1;
        case 0x42: case 0x45: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0xA1:
            return 2;
        case 0x46: return 3;
        case 0x47: return 4;
        case 0x48: return 5;
        case 0x49: return 6;
        case 0xA4: case 0xA5: case 0xA6: return 0xFF;
        case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: return 0x0B;
        default: return 0;
    }
}

// 3496:1BA0: for pipe class c entering a cell heading north/east/south/west,
// the direction it leaves in (0, 2, 4, 6), or 8 for a dead end.
constexpr std::array<std::array<int, 4>, 7> kPipeTurn = {{
    {8, 8, 8, 8}, {0, 8, 4, 8}, {8, 2, 8, 6}, {2, 8, 8, 4}, {6, 4, 8, 8}, {8, 0, 6, 8}, {8, 8, 2, 0},
}};

// 0x2CBF1: follows the pipe leaving (x, y) in `dir`. 1 on reaching a water
// source; a fountain with a higher level than the start passes on that level
// minus one; anything else ends the trace with 0.
int trace_pipe(const model::CityMap& city, int x, int y, int dir, int& fountains_reached) {
    const int start_level = city.operational_state[y][x];
    for (;;) {
        if ((dir == 0 && y <= 0) || (dir == 4 && y >= 99) || (dir == 2 && x >= 99) || (dir == 6 && x <= 0)) return 0;
        if (dir == 0) --y;
        else if (dir == 4) ++y;
        else if (dir == 6) --x;
        else if (dir == 2) ++x;
        const uint8_t c = pipe_class(city.tile[y][x]);
        if (c == 0xFF) return 1;
        if (c == 0x0B) {
            const int level = city.operational_state[y][x];
            if (level == 0) return 0;
            ++fountains_reached;
            return level > start_level ? level - 1 : 0;
        }
        dir = c < kPipeTurn.size() ? kPipeTurn[c][static_cast<size_t>(dir / 2)] : 8;
        if (dir == 8) return 0;
    }
}

}  // namespace

int fountain_supply(model::CityMap& city, int x, int y, bool working) {
    uint8_t& level = city.operational_state[y][x];
    if (level != 0) --level;
    int reached = 0, best = 0;
    for (int dir : {0, 2, 4, 6}) best = std::max(best, trace_pipe(city, x, y, dir, reached));
    if (best != 0) level = static_cast<uint8_t>(best);
    if (level != 0) return level;
    return (reached > 1 || (!working && reached == 1)) ? 1 : 0;
}

void apply_flags_clear_mode(model::CityMap& city, int x, int y, int radius, uint8_t mask) {
    const int y0 = std::max(0, y - radius), y1 = std::min(model::kCityH - 1, y + radius);
    const int x0 = std::max(0, x - radius), x1 = std::min(model::kCityW - 1, x + radius);
    bool first = true;
    for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
            if (first) {
                city.service_flags[cy][cx] &= mask;
                first = false;
            } else {
                city.service_flags[cy][cx] |= mask;
            }
        }
    }
}

void apply_water(model::CityMap& city) {
    // 0x2C93F, water half.
    for (int y = 0; y < model::kCityH; ++y) {
        for (int x = 0; x < model::kCityW; ++x) {
            uint8_t& t = city.tile[y][x];
            if (t == 0xA4) {
                apply_flags(city, x, y, 3, 0x01);
            } else if (t == 0xB8) {
                apply_flags(city, x, y, 1, 0x01);
            } else if (t >= 0xB9 && t <= 0xBD) {
                const bool working = t == 0xB9 || t == 0xBB;
                if (fountain_supply(city, x, y, working) != 0) {
                    apply_flags(city, x, y, 6, 0x01);
                    if (t == 0xBA) t = 0xB9;
                    else if (t == 0xBD) t = 0xBB;
                } else if (t == 0xB9) {
                    t = 0xBA;
                } else if (t == 0xBB || t == 0xBC) {
                    t = 0xBD;
                }
            }
        }
    }
}

void rebuild_services(model::CityMap& city, ServiceState& service) {
    reset_scan_counters(service);
    reset_tick(city, service);
    apply_water(city);
    for (int y = 0; y < model::kCityH; ++y)
        for (int x = 0; x < model::kCityW; ++x) dispatch_tile(city, service, x, y);
}

}  // namespace gaius::systems::service
