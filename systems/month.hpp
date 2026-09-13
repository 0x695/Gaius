// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/month.hpp
//
// The simulation clock: one game month, stepped the way the engine steps it.
// Transcribed from the US-build CSR.EXE (2026-09-13) -- the step dispatcher
// at flat 0x2936A, the calendar at 0x29476 and the random number generator
// at 2EF9:1425. See docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 17.
//
// A month is 106 steps (DS:0x6D9D):
//   0-99    systems::housing::develop_row for row = step. At step 80 the
//           per-row routine 0x2E209 draws one random number.
//   100     reset_tick, and derive_network_flags in one month of 18
//   101     population units (DS:0x6C10) and water
//   102-105 the four quarter scans (service dispatch, 25 rows each); step
//           105's last monthly routine, 0x2DF7D, draws four random numbers
// then the calendar advances: month DS:0x6C1C wraps at 12 into year
// DS:0x6C32, and the 18-month counter DS:0x6D9B wraps at 18.
//
// Not modeled, each for a stated reason: the other per-row routines
// (0x2D2F5, 0x2CE7C, 0x2CD1C, 0x2CD10) and monthly routines (0x2E0BE,
// 0x2DC72, 0x2DEC8, 0x2DD21, 0x2DE0F, and 0x2DF7D's event rolls) aren't
// read yet; nor are the step-101 routine 0x28215, the yearly routine 0x28238
// (not yet checked for random draws) or the 18-month routine 0x2D6F4. The
// housing pass's 0xA8-0xB1 decay-and-spread routine also isn't modeled.
//
// The random draws. The land-value growth each housing row applies is
// DS:0x6BF6 + (2EF9:0286 & 3) - 1, as a signed byte, and 2EF9:0286 only
// changes when the generator draws -- 5 times a month in the simulation
// path above, so rows 0-80 of a month share one value and rows 81-99 the
// next. The generator's state isn't in the save and is also advanced by
// terrain generation and UI screens, so a real session's sequence can't be
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

}  // namespace gaius::systems::month
