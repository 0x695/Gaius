// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/month.hpp"

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

}  // namespace gaius::systems::month
