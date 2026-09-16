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
// read yet. Step 101's routine 0x28215 is run_economy below; of the yearly
// routine 0x28238 the accounts (systems::economy), the history writes and the
// Legion (systems::military) are modeled, and none of them draws; the 18-month routine 0x2D6F4 is systems::province::spawn_army.
//
// The random draws. The land-value growth each housing row applies is
// DS:0x6BF6 + (2EF9:0286 & 3) - 1, as a signed byte, and 2EF9:0286 only
// changes when the generator draws: once per frame in the main loop, and
// five more times a month in the steps above (step 80, four at step 105).
// At the fastest speed every frame is a step (row 10 of the speed gate at
// 3496:0000 passes every frame); at slower speeds the frames between steps
// draw too, which run_frame models (run_step alone draws once per step). The
// generator's state isn't in the save and is also advanced by terrain
// generation and UI screens, so a real session's sequence can't be
// recovered; Gaius starts it from the values in the executable image.

#pragma once

#include <cstdint>
#include <functional>

#include "model/city_state.hpp"
#include "systems/messages.hpp"
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
    // DS:0x6BFE: the industrial tax rate DS:0x6C02 summed at each step 101, for
    // the yearly accounts (systems::economy::run_year). The save doesn't keep
    // it; like the loader (0x0568F), sim_state_from_save sets rate x month.
    int industrial_rate_sum = 0;
    int ticks = 0;                   // the frame counters DS:0x6D34-0x6D44, as one count
    // 0x2DF7D rolls an event when the generator's walk exceeds its threshold:
    // DS:0x6BE0 road wear, 0x6BE2 collapse, 0x6BE4 fire (saved global words).
    // Step 105 sets them from the plebs assigned to each duty (systems::plebs:
    // the share of the need covered, 100 when covered); 99, never, until then
    // unless read from a save.
    int road_wear_threshold = 99;
    int collapse_threshold = 99;
    int fire_threshold = 99;
    // DS:0x6D97: the 18-month counter wrapped, and the next step starts with
    // the army spawner (systems::province::spawn_army).
    bool army_spawn_pending = false;
    // DS:0x6CB8, the difficulty (0-2), saved in final_state (sim_state_from_save
    // reads it); set on the start screen (0x27F6C) and raised from Easy by a
    // promotion. The note below predates finding it in the save: it comes
    // from the options screen (0x27F26). 0 unless the caller sets it.
    int difficulty = 0;
    // Province counters the save doesn't keep: DS:0x6DFE (towns grow one month
    // in six) and DS:0x6DFC (the worn-road message, one month in four).
    int town_counter = 0;
    int province_wear_counter = 0;
    // DS:0x6C92, the towns linked to the city at the last step 80 (not saved).
    int linked_towns = 0;
    // DS:0x6BDE: the walk must exceed it for a province road to wear. Set at
    // step 105 from the plebs on construction (systems::plebs::set_thresholds);
    // the save doesn't keep it, so 100, never, until then.
    int province_wear_threshold = 100;
    // Called when a Cohort reaches the army it attacks (systems::province::
    // Hooks::battle); without it the battle doesn't start.
    std::function<void(model::CityState& state, int cohort, int army)> on_battle;
    // Called when the year's ratings earn a promotion (systems::administration::
    // check_promotion); `to_caesar` at rank 19. Return 1 to accept, 2 to wait 9
    // years, 3 to wait 24; anything else leaves it unanswered, offered again
    // next year. Without it nothing is answered.
    std::function<int(model::CityState& state, bool to_caesar)> on_promotion;
    // Set when a year's settlement dismisses the governor: the third missed
    // tribute in a row (economy::Settlement::dismissed, DS:0x6D6A = 0x3C, the
    // game's end). The caller clears it.
    bool dismissed = false;
    // The message along the top of the view (systems::messages): posted by the
    // events a step runs, counted down by run_frame.
    messages::Board messages;
    // Set when run_frame gives the funds warning (0x0FAD3); the caller clears it.
    bool funds_warning = false;
    // DS:0x5292, the game speed, 0-100 in tens (the options screen's arrows,
    // 0x0F204/0x0F217), and DS:0x6DE3, the frame's phase in the speed gate.
    int speed = 100;
    int speed_phase = 0;
    // DS:0x6C7C: the new year's banner, set to 80 frames when the year turns
    // (0x29498) and counted down by run_frame (0x278A8). Not saved.
    int year_banner = 0;
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
// recomputing each real save's outputs from its own inputs.
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
// doesn't keep: SimState::industrial_rate_sum). DS:0x6C04 is the population
// tax rate and DS:0x6C06 the conscription rate (systems/economy.hpp).
void run_economy(model::CityState& state);

// A month on a whole save: as above, plus at step 101 the save's population
// words (DS:0x6C10, DS:0x6C0E) are stored and run_economy() runs, and its
// growth and coverage bases feed the months after; month and year are written
// back to DS:0x6C1C / DS:0x6C32.
//
// When the year turns it runs the year's accounts (systems::economy::run_year:
// taxes, operating costs, the tribute), appends last year's values to the
// save's five history buffers (table_60_a-d, table_72), then recruits the
// Legion and assigns it to the Cohorts (systems::military::run_year; findings
// sections 22, 25 and 26).
void run_step(model::CityState& state, SimState& sim);
void run_month(model::CityState& state, SimState& sim);

// 0x0FAA2, the speed gate: whether a frame of phase `phase` (0-10) at `speed`
// runs a step -- 3496:0000[speed / 10 x 10 + phase], an 11-row table from never
// (speed 0) to every frame (100). Phase 10 reads the next row's first byte.
bool speed_gate(int speed, int phase);

// One frame of the main loop (0x0FA13). The phase DS:0x6DE3 steps before the
// gate reads it (1-10, then 0). A frame that passes runs run_step, whose first
// act is the frame's random draw; one that doesn't only draws. Every frame then
// counts the message down (0x279AC, while the messages option DS:0x6C78 is on)
// and checks the funds warning (0x0FAD3). At speed 100 every frame is a step,
// which is what run_step alone models. Returns whether a step ran.
bool run_frame(model::CityState& state, SimState& sim);

}  // namespace gaius::systems::month
