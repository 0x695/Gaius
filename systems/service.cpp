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

void apply_temple(model::CityMap& city, ServiceState& service, int x, int y, TempleVariant variant) {
    const int n = static_cast<int>(variant);                                // 1..4
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

void dispatch_tile(model::CityMap& city, ServiceState& service, int x, int y) {
    const uint8_t tile = city.tile[y][x];
    if (tile <= 0x35) return;  // routine 0x2BBBB never dispatches these
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
            apply_temple(city, service, x, y, TempleVariant::Variant1);
            break;
        case 0xE2: case 0xE3:
            apply_temple(city, service, x, y, TempleVariant::Variant2);
            break;
        case 0xE4: case 0xE5:
            apply_temple(city, service, x, y, TempleVariant::Variant3);
            break;
        case 0xE6: case 0xE7:
            apply_temple(city, service, x, y, TempleVariant::Variant4);
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

}  // namespace gaius::systems::service
