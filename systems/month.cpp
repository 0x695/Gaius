// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/month.hpp"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include "formats/save/save.hpp"
#include "systems/actors.hpp"
#include "systems/construction.hpp"
#include "systems/economy.hpp"
#include "systems/housing.hpp"
#include "systems/military.hpp"
#include "systems/province.hpp"

namespace gaius::systems::month {

namespace {

constexpr int kDrawStep = 80;  // 0x2E209 draws only at this step

int saved_word(const model::CityState& state, uint16_t ds) {
    for (int i = 0; i < 128; ++i) {
        if (formats::save::kGlobalWordDsAddress[i] != ds) continue;
        const size_t o = static_cast<size_t>(i) * 2;
        if (o + 1 >= state.global_words_128.size()) return 0;
        return static_cast<int16_t>(state.global_words_128[o] | (state.global_words_128[o + 1] << 8));
    }
    return 0;
}

}  // namespace

void Random::advance() {
    // 2EF9:1425 -> 2EF9:13F0
    prev_low7 = low7;
    const uint16_t feedback = static_cast<uint16_t>((lfsr & 1) ^ ((lfsr >> 7) & 1));
    lfsr = static_cast<uint16_t>(((lfsr & 0x7FFF) | (feedback << 15)) >> 1);
    draw = lfsr;
    low7 = draw & 0x7F;
    walk += draw & 7;
    if (walk > 99) walk -= 99;
}

SimState sim_state_from_save(const model::CityState& state) {
    SimState sim;
    sim.month = saved_word(state, 0x6C1C);
    sim.year = saved_word(state, 0x6C32);
    sim.land_value_growth_base = saved_word(state, 0x6BF6);
    sim.population_units = saved_word(state, 0x6C10);
    sim.service.housing_coverage_base = saved_word(state, 0x6BF8);
    sim.road_wear_threshold = saved_word(state, 0x6BE0);
    sim.collapse_threshold = saved_word(state, 0x6BE2);
    sim.fire_threshold = saved_word(state, 0x6BE4);
    sim.industrial_rate_sum = model::global_word(state, 0x6C02) * sim.month;
    return sim;
}

namespace {

// 0x2DF7D's pick: draw; if the walk beats the threshold and there's anything
// counted, halve the draw (a signed word, arithmetic shift) up to 16 times
// until it is no more than the count. A negative result never matches.
int roll_target(Random& random, int threshold, int count) {
    random.advance();
    if (random.walk <= threshold || count == 0) return -1;
    int v = static_cast<int16_t>(static_cast<uint16_t>(random.draw));
    for (int i = 0; i < 16; ++i) {
        v = v >= 0 ? v / 2 : -((-v + 1) / 2);
        if (v <= count) return v;
    }
    return -1;
}

// Step 105: publish the scan counts (0x2936A), then 0x2DF7D.
void finish_scans(SimState& sim, model::CityState* state) {
    service::ServiceState& s = sim.service;
    if (state) {
        model::set_global_word(*state, 0x6BF0, s.road_count);
        model::set_global_word(*state, 0x6BF2, s.building_count);
        model::set_global_word(*state, 0x6BEE, s.market_count / 4);
        model::set_global_word(*state, 0x6BEA, s.industry_count / 16);
        model::set_global_word(*state, 0x6BEC, s.school_count / 4);
        // 0x2E0BE, the first monthly routine: the province pass, which wears
        // away the road last month's fourth roll picked. The four pleb and
        // welfare routines after it (0x2DC72, 0x2DEC8, 0x2DD21, 0x2DE0F) aren't
        // modeled.
        province::monthly_pass(*state, sim.province_wear_counter);
        model::set_global_word(*state, 0x6C88, -1);  // 0x2DF8F
    }
    s.road_wear_target = roll_target(sim.random, sim.road_wear_threshold, s.road_count);
    s.collapse_target = roll_target(sim.random, sim.collapse_threshold, s.building_count);
    s.fire_target = roll_target(sim.random, sim.fire_threshold, s.building_count);
    // The fourth roll picks next month's worn province road (DS:0x6C88) among
    // DS:0x6C8A, against DS:0x6BDE.
    const int province_road = roll_target(sim.random, sim.province_wear_threshold,
                                          state ? model::global_word(*state, 0x6C8A) : 0);
    if (state && province_road >= 0) model::set_global_word(*state, 0x6C88, province_road);
}

void run_step_impl(model::CityMap& city, SimState& sim, model::CityState* state) {
    // The main loop (0xFA13): a random draw every frame, then the frame
    // counters (0x1147E), the walkers (0x23C4C) and the step (0x2936A).
    sim.random.advance();
    ++sim.ticks;
    if (state) {
        province::Hooks hooks;
        hooks.battle = sim.on_battle;
        actors::update(*state, sim.random, sim.ticks, &hooks);
        // 0x2936B: the step starts with the army spawner when the 18-month
        // counter has wrapped (DS:0x6D97).
        if (sim.army_spawn_pending) province::spawn_army(*state, sim.random, sim.difficulty);
    }
    sim.army_spawn_pending = false;

    const int step = sim.step;
    if (step < kHousingSteps) {
        // 0x294CF: one growth value per row, a signed byte.
        std::vector<std::pair<int, int>> collapsed;
        housing::DevelopmentContext ctx;
        ctx.population_units = sim.population_units;
        ctx.land_value_growth = static_cast<int8_t>(
            static_cast<uint8_t>(sim.land_value_growth_base + (sim.random.walk & 3) - 1));
        if (state) ctx.collapsed = &collapsed;
        ctx.random = &sim.random;
        ctx.spread_fire = [&](int x, int y, int direction) {
            if (state) construction::burn(*state, sim.random, x, y, direction);
            else construction::burn(city, sim.random, x, y, direction);
        };
        housing::develop_row(city, step, ctx);
        if (state) {
            // The engine spawns each rioter inside the row; nothing in the row
            // reads the actor table, so spawning after it is the same.
            for (const auto& [x, y] : collapsed) actors::spawn_rioter(*state, x, y);
            actors::run_spawners(*state, sim.random, step);
        }
        if (step == kDrawStep) {
            // 0x2E209: a draw, then the province's towns and the highway.
            sim.random.advance();
            if (state) {
                province::develop_towns(*state, sim.town_counter);
                province::connect_highway(*state);
            }
        }
    } else if (step == 100) {
        service::reset_tick(city, sim.service);
        if (sim.month_counter_18 == 0) service::derive_network_flags(city);
    } else if (step == 101) {
        sim.population_units = housing::population_units(city);
        service::apply_water(city);
    } else if (step <= 105) {
        if (step == 102) service::reset_scan_counters(sim.service);
        sim.service.on_event = [&](service::CityEvent event, int x, int y) {
            if (event == service::CityEvent::Collapse) {
                if (state) construction::demolish(*state, sim.random, x, y);
                else construction::demolish(city, sim.random, x, y);
            } else {
                if (state) construction::burn(*state, sim.random, x, y);
                else construction::burn(city, sim.random, x, y);
            }
        };
        const int first_row = (step - 102) * 25;
        for (int y = first_row; y < first_row + 25; ++y) {
            for (int x = 0; x < model::kCityW; ++x) service::dispatch_tile(city, sim.service, x, y);
        }
        sim.service.on_event = nullptr;  // it refers to this call's arguments
        if (step == 105) finish_scans(sim, state);
    }

    // 0x29476, which the engine runs before each step: the same order of work.
    if (++sim.step >= kStepsPerMonth) {
        sim.step = 0;
        if (++sim.month >= 12) {
            sim.month = 0;
            ++sim.year;
        }
        if (++sim.month_counter_18 >= 18) {
            sim.month_counter_18 = 0;
            sim.army_spawn_pending = true;  // 0x294B3
        }
    }
}

}  // namespace

void run_step(model::CityMap& city, SimState& sim) { run_step_impl(city, sim, nullptr); }

void run_month(model::CityMap& city, SimState& sim) {
    do {
        run_step(city, sim);
    } while (sim.step != 0);
}

namespace {

// Signed tables in segment 3496, read from the executable image.
constexpr std::array<int8_t, 13> kTable006E = {-9, -7, -6, -5, -4, -3, -2, -1, -1, 0, 0, 1, 2};
constexpr std::array<int8_t, 21> kTable0136 = {2, 2, 1, 0, 0, -1, -1, -1, -2, -2, -3, -3, -4, -5, -6, -7, -7, -7, -7, -7, -7};
constexpr std::array<int8_t, 51> kTable014B = {3,  2,  1,  1,  0,  0,  0,  0,  0,  -1, -1, -2, -2, -2, -3, -3, -3,
                                               -3, -4, -4, -4, -4, -4, -4, -5, -5, -5, -5, -5, -5, -6, -6, -6, -7,
                                               -7, -7, -7, -7, -7, -7, -8, -8, -8, -8, -8, -9, -9, -9, -9, -10, -11};
constexpr std::array<int8_t, 51> kTable017E = {-2, -2, -1, -1, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5,
                                               5,  6,  6,  6,  6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 10, 10, 10};
constexpr std::array<int8_t, 11> kTable01B1 = {-2, -1, 0, 1, 2, 2, 3, 3, 4, 4, 5};

// The engine indexes these without bounds checks; outside the table it would
// read neighbouring data, which isn't modeled -- 0 here.
template <size_t N>
int at(const std::array<int8_t, N>& table, int i) {
    return (i >= 0 && i < static_cast<int>(N)) ? table[static_cast<size_t>(i)] : 0;
}

}  // namespace

void run_economy(model::CityState& state) {
    auto g = [&](uint16_t ds) { return model::global_word(state, ds); };
    const int units = g(0x6C10);
    const int workshops = g(0x6C9E), forums = g(0x6CA0);

    // 0x28621
    int v6bf4;
    if (units <= 100) {
        v6bf4 = at(kTable006E, units / 8);
    } else {
        int v = units / 20;
        if (workshops > 0) v /= workshops * 2;
        v += g(0x6C36) / 20 - 2;
        v6bf4 = std::clamp(v, 0, 6);
    }
    model::set_global_word(state, 0x6BF4, v6bf4);

    // 0x28694 (32-bit arithmetic where the engine uses its long-math helpers)
    int band = 0, share = 0;
    if (units > 0) {
        int32_t rem = units - (static_cast<int32_t>(units) * g(0x6C06)) / 100;
        const int16_t jobs = static_cast<int16_t>((workshops * 20 + g(0x6BEA) * 30 + g(0x6BEE) * 12) * (g(0x6BE8) / 4 + 1));
        rem -= jobs;
        rem -= static_cast<int16_t>(forums * 30);
        if (rem < 0) rem = 0;
        rem = rem * 100 / units;
        share = std::clamp(rem, int32_t{0}, int32_t{100});
        band = share / 5;
    }
    model::set_global_word(state, 0x6BCC, share);
    model::set_global_word(state, 0x6BFA, band);

    // 0x28800, 0x28826
    model::set_global_word(state, 0x6BF8, at(kTable014B, g(0x6C04)) + at(kTable0136, band));
    model::set_global_word(state, 0x6BF6, at(kTable017E, g(0x6C04)) + at(kTable01B1, g(0x6C06) / 10));

    // 0x28215's tail
    model::set_global_word(state, 0x6C00, g(0x6C00) + g(0x6C04));
}

namespace {

// 0x28853-0x2894F, in the yearly routine 0x28238: each history buffer's index
// advances (wrapping at 15, or 17 for table_72) and the record there becomes
// (last year, the value). Findings section 22.
void record_history(model::CityState& state, int year) {
    struct History {
        std::vector<uint8_t>* table;
        uint16_t index_ds;
        int records;
        uint16_t value_ds;
    };
    const History histories[] = {
        {&state.table_60_a, 0x6B36, 15, 0x6BC6}, {&state.table_60_b, 0x6B34, 15, 0x6BC4},
        {&state.table_60_c, 0x6B32, 15, 0x6CA2}, {&state.table_60_d, 0x6B30, 15, 0x6C10},
        {&state.table_72, 0x6B38, 17, 0x6BB6},
    };
    for (const History& h : histories) {
        int i = model::global_word(state, h.index_ds) + 1;
        if (i >= h.records) i = 0;
        model::set_global_word(state, h.index_ds, i);
        const size_t o = static_cast<size_t>(i) * 4;
        if (h.table->size() < o + 4) h.table->resize(o + 4, 0);
        const int value = model::global_word(state, h.value_ds);
        (*h.table)[o] = static_cast<uint8_t>((year - 1) & 0xFF);
        (*h.table)[o + 1] = static_cast<uint8_t>(((year - 1) >> 8) & 0xFF);
        (*h.table)[o + 2] = static_cast<uint8_t>(value & 0xFF);
        (*h.table)[o + 3] = static_cast<uint8_t>((value >> 8) & 0xFF);
    }
}

}  // namespace

void run_step(model::CityState& state, SimState& sim) {
    const int step = sim.step;
    const int year_before = sim.year;
    run_step_impl(state.city, sim, &state);
    if (step == 101) {
        model::set_global_word(state, 0x6C10, sim.population_units);
        model::set_global_word(state, 0x6C0E, 4 * sim.population_units);
        run_economy(state);
        sim.industrial_rate_sum += model::global_word(state, 0x6C02);  // 0x28230
        sim.land_value_growth_base = model::global_word(state, 0x6BF6);
        sim.service.housing_coverage_base = model::global_word(state, 0x6BF8);
    }
    model::set_global_word(state, 0x6C1C, sim.month);
    model::set_global_word(state, 0x6C32, sim.year);
    // The calendar (0x29472) calls the yearly routine 0x28238 when the year
    // turns: the accounts, the history writes, then the Legion. The routines
    // after it (the ratings 0x28C43, promotion 0x29023, 0x2933B) aren't
    // transcribed.
    if (sim.year != year_before) {
        economy::run_year(state, sim.industrial_rate_sum);
        record_history(state, sim.year);
        military::run_year(state);
    }
}

void run_month(model::CityState& state, SimState& sim) {
    do {
        run_step(state, sim);
    } while (sim.step != 0);
}

}  // namespace gaius::systems::month
