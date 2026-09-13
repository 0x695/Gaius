// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/month.hpp"

#include <algorithm>
#include <array>

#include "formats/save/save.hpp"
#include "systems/housing.hpp"

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
    return sim;
}

void run_step(model::CityMap& city, SimState& sim) {
    const int step = sim.step;
    if (step < kHousingSteps) {
        // 0x294CF: one growth value per row, a signed byte.
        housing::DevelopmentContext ctx;
        ctx.population_units = sim.population_units;
        ctx.land_value_growth = static_cast<int8_t>(
            static_cast<uint8_t>(sim.land_value_growth_base + (sim.random.walk & 3) - 1));
        housing::develop_row(city, step, ctx);
        if (step == kDrawStep) sim.random.advance();
    } else if (step == 100) {
        service::reset_tick(city, sim.service);
        if (sim.month_counter_18 == 0) service::derive_network_flags(city);
    } else if (step == 101) {
        sim.population_units = housing::population_units(city);
        service::apply_water(city);
    } else if (step <= 105) {
        const int first_row = (step - 102) * 25;
        for (int y = first_row; y < first_row + 25; ++y) {
            for (int x = 0; x < model::kCityW; ++x) service::dispatch_tile(city, sim.service, x, y);
        }
        if (step == 105) {
            for (int i = 0; i < 4; ++i) sim.random.advance();  // 0x2DF7D
        }
    }

    // 0x29476, which the engine runs before each step: the same order of work.
    if (++sim.step >= kStepsPerMonth) {
        sim.step = 0;
        if (++sim.month >= 12) {
            sim.month = 0;
            ++sim.year;
        }
        if (++sim.month_counter_18 >= 18) sim.month_counter_18 = 0;
    }
}

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

void run_step(model::CityState& state, SimState& sim) {
    const int step = sim.step;
    run_step(state.city, sim);
    if (step == 101) {
        model::set_global_word(state, 0x6C10, sim.population_units);
        model::set_global_word(state, 0x6C0E, 4 * sim.population_units);
        run_economy(state);
        sim.land_value_growth_base = model::global_word(state, 0x6BF6);
        sim.service.housing_coverage_base = model::global_word(state, 0x6BF8);
    }
    model::set_global_word(state, 0x6C1C, sim.month);
    model::set_global_word(state, 0x6C32, sim.year);
}

void run_month(model::CityState& state, SimState& sim) {
    do {
        run_step(state, sim);
    } while (sim.step != 0);
}

}  // namespace gaius::systems::month
