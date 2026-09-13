// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/month.hpp
//
// The simulation clock: one game month, stepped the way the engine steps it.
// Transcribed from the US-build CSR.EXE (2026-09-13) -- the step dispatcher
// at flat 0x2936A, the calendar at 0x29476 and the random number generator
// at 2EF9:1425. See docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 17.
//
// A month is 106 steps (DS:0x6D9D):
//   0-99    systems::housing::develop_row for row = step, then the walker
//           spawners (systems::actors::run_spawners); at step 80 the
//           per-row routine 0x2E209 draws one random number.
//   100     reset_tick, and derive_network_flags in one month of 18
//   101     population units (DS:0x6C10) and water
//   102-105 the four quarter scans (service dispatch, 25 rows each; the scan
//           counters zero at 102). At step 105 the counts are published
//           (DS:0x6BF0, 0x6BF2, 0x6BEA, 0x6BEC, 0x6BEE) and 0x2DF7D rolls next
//           month's road-wear, collapse and fire targets with four random
//           draws (see service::ServiceState).
// then the calendar advances: month DS:0x6C1C wraps at 12 into year
// DS:0x6C32, and the 18-month counter DS:0x6D9B wraps at 18.
//
// Before every step the main loop (0xFA13) draws a random number and, on a
// whole save, ticks the walkers (systems::actors::update); the grid-only
// run_step has no actor table and only draws.
//
// Not modeled, each for a stated reason: the per-row routine 0x2CD10 and
// the monthly routines (0x2E0BE,
// 0x2DC72, 0x2DEC8, 0x2DD21, 0x2DE0F) aren't
// read yet; nor are the step-101 routine 0x28215, the yearly routine 0x28238
// (not yet checked for random draws) or the 18-month routine 0x2D6F4. 
//
// The random draws. The land-value growth each housing row applies is
// DS:0x6BF6 + (2EF9:0286 & 3) - 1, as a signed byte, and 2EF9:0286 only
// changes when the generator draws: once per frame in the main loop, and
// five more times a month in the steps above (step 80, four at step 105).
// At the fastest speed every frame is a step (row 10 of the speed gate at
// 3496:0000 passes every frame); at slower speeds the frames between steps
// draw too, which Gaius doesn't model -- it draws once per step. The
// generator's state isn't in the save and is also advanced by terrain
// generation and UI screens, so a real session's sequence can't be
// recovered; Gaius starts it from the values in the executable image.

#pragma once

#include <cstdint>

#include "model/city_state.hpp"
#include "systems/service.hpp"

namespace gaius::systems::month {

// The generator at 2EF9:1425: a 16-bit shift register (2EF9:13F0) whose
// feedback is bit 0 xor bit 7, plus a random walk over 1-99 stepped by the
// low 3 bits of each draw. Defaults are the image's initial values.
struct Random {
    int walk = 55;       // 2EF9:0286
    int low7 = 49;       // 2EF9:0288, the last draw & 0x7F
    int prev_low7 = 12;  // 2EF9:028A
    int draw = 12;       // 2EF9:028C, the last draw
    uint16_t lfsr = 55;  // 2EF9:028E

    void advance();
};

inline constexpr int kStepsPerMonth = 106;
inline constexpr int kHousingSteps = 100;

struct SimState {
    int step = 0;                    // DS:0x6D9D
    int month = 0;                   // DS:0x6C1C, 0-11
    int year = 0;                    // DS:0x6C32 (negative before the common era)
    int month_counter_18 = 0;        // DS:0x6D9B
    int land_value_growth_base = 0;  // DS:0x6BF6
    int population_units = 0;        // DS:0x6C10
    int ticks = 0;                   // the frame counters DS:0x6D34-0x6D44, as one count
    // 0x2DF7D rolls an event when the generator's walk exceeds its threshold:
    // DS:0x6BE0 road wear, 0x6BE2 collapse, 0x6BE4 fire (saved global words).
    // 99, never, unless read from a save. What sets them isn't traced.
    int road_wear_threshold = 99;
    int collapse_threshold = 99;
    int fire_threshold = 99;
    Random random;
    service::ServiceState service;
};

// Month, year, growth base, population units and the housing coverage base
// (DS:0x6BF8) from a save's global words. The step and 18-month counters
// aren't saved; they start at 0.
SimState sim_state_from_save(const model::CityState& state);

// Runs one step of the month, then advances the step counter and, past step
// 105, the calendar.
void run_step(model::CityMap& city, SimState& sim);

// Runs steps until the calendar next advances.
void run_month(model::CityMap& city, SimState& sim);

// Routine 0x28215, run at step 101 after population and water: four routines
// that turn the city's state into the inputs the next month's housing uses.
// Reads and writes the save's global words (model::global_word). Checked by
// recomputing each of four real saves' outputs from its own inputs.
//   0x28621  DS:0x6BF4 = population units <= 100 ? table 3496:006E[units / 8]
//            : clamp(units / 20 / (2 * workshops, if any) + DS:0x6C36 / 20 - 2, 0, 6)
//   0x28694  DS:0x6BCC (a 0-100 percentage) = the population share left over
//            once DS:0x6C06 percent, workshops x 20, forums x 30 and the monthly
//            scan counts (DS:0x6BEA x 30 + DS:0x6BEE x 12, times DS:0x6BE8 / 4 + 1)
//            are taken out; DS:0x6BFA = DS:0x6BCC / 5
//   0x28800  DS:0x6BF8 (the housing coverage base) = 3496:014B[DS:0x6C04]
//            + 3496:0136[DS:0x6BFA]
//   0x28826  DS:0x6BF6 (the land-value growth base) = 3496:017E[DS:0x6C04]
//            + 3496:01B1[DS:0x6C06 / 10]
// then DS:0x6C00 += DS:0x6C04 (and DS:0x6BFE += DS:0x6C02, which the save
// doesn't keep). What DS:0x6C04 and DS:0x6C06 mean to the player -- tax rate,
// wages? -- isn't established; the arithmetic is.
void run_economy(model::CityState& state);

// A month on a whole save: as above, plus at step 101 the save's population
// words (DS:0x6C10, DS:0x6C0E) are stored and run_economy() runs, and its
// growth and coverage bases feed the months after; month and year are written
// back to DS:0x6C1C / DS:0x6C32.
//
// When the year turns it also appends last year's values to the save's five
// history buffers (table_60_a-d, table_72; findings section 22).
void run_step(model::CityState& state, SimState& sim);
void run_month(model::CityState& state, SimState& sim);

}  // namespace gaius::systems::month
