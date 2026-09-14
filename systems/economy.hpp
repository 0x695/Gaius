// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/economy.hpp
//
// The city's money: what building costs, and the yearly accounts -- the two
// taxes, operating costs and the tribute to Rome. Transcribed from the
// US-build CSR.EXE (2026-09-14); docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md
// section 25.
//
// The words involved, with what the manual's Treasurer and Forum screens call
// them. The names are STRONG INFERENCE: each word's arithmetic is transcribed,
// and each name matches the manual's description of that figure and the UI
// limits (0x0E650-0x0EB37) on the value.
//   DS:0x6CA2  city funds (the toolbar's Available Funds; the display routine
//              0x212A1 clamps it to 0-25000)
//   DS:0x6C04  population tax rate, 0-25 %   DS:0x6C02  industrial tax rate, 0-25 %
//   DS:0x6C06  conscription rate, 0-50 %     DS:0x6C08  army wages bill, 0-999
//   DS:0x6C2C  governor's salary, 0-9999     DS:0x6C2E  personal savings
//   DS:0x6C46  pleb welfare expenditure, 0-9999
//   DS:0x6C00  the population tax rate summed each month (step 101), and
//   DS:0x6BFE  the industrial one, which isn't saved: the loader rebuilds it
//              as rate x month (0x0568F)
//   DS:0x6BC0  construction spent this year      DS:0x6BBA  the tribute due
//   DS:0x6BB8  tributes missed in a row          DS:0x6BFC  industrial tax pressure
// and last year's accounts, the Treasurer's report: DS:0x6BC6 population tax,
// 0x6BC4 industrial tax, 0x6BC2 construction, 0x6BBE operating costs, 0x6BAA
// tribute paid, 0x6BB6 profit or loss (copied to 0x6BB4-0x6BAC), 0x6BCA/0x6BC8
// tax per head in whole and hundredths of a denarius.
//
// Checked against real saves: every year of every save's funds and profit
// histories satisfies the settlement below, and each save's own report is
// reproduced from the year before it (test_economy_year_end_matches_saves).
// The two tax sums need the city as it stood at the year's end, which no save
// holds, so they're tested on built cities only.

#pragma once

#include <array>
#include <cstdint>

#include "model/city_state.hpp"
#include "systems/construction.hpp"

namespace gaius::systems::economy {

inline constexpr uint16_t kFunds = 0x6CA2;

// 3496:1548, by command id: what one handler call costs. Commands that don't
// place anything cost 0; ids 34-43 are the province map's commands, whose cost
// the handler doubles or quadruples by the province terrain under the cursor
// (0x11CD4-0x11D86), which Gaius doesn't model.
inline constexpr std::array<int16_t, 44> kConstructionCost = {
    0,  0,   1,   0,   3,  3,   0,  5,  10, 5, 10, 2, 0, 20, 40, 60, 60, 200, 0, 0, 100, 200,
    300, 10, 80, 25, 300, 20, 50, 500, 0, 0, 0,  0,  0, 15, 30, 40, 0,   0, 0, 90, 50,  0};

// 3496:008E: population tax units per housing cell for tiles 0xC8-0xD7 (the
// population table at 3496:007E is the 16 bytes before it).
inline constexpr std::array<uint8_t, 16> kTaxUnitsPerCell = {1, 2, 4, 6, 7, 10, 14, 9, 13, 15, 16, 17, 18, 20, 22, 25};

// What one placement of `id` costs: 3496:1548[id], or for the Forum the grade
// cost 3496:15A0[grade] (0x11DAC).
int construction_cost(construction::CommandId id, int forum_grade = 0);

// 0x11CD4-0x11D92: on the province map (DS:0x6CAE = 1) a command's cost, and
// the charge, are shifted left by the terrain under the cursor -- 2 on tiles
// 0x25-0x2C, 1 on 0x2D-0x35, else 0 -- except the Fort (id 29), never shifted.
// The manual's "15 Denarii to 60 Denarii to clear" is Clear Area's 15 << 0-2.
int province_cost_shift(int command_id, uint8_t tile);

// 0x11C72: with fewer than 50 pleb groups (DS:0x6C56) the build routine refuses
// every command but the Cohort orders (ids 31-34), with a message.
bool enough_plebs(const model::CityState& state, int command_id);

// The handlers' test before placing (0x11DC3, 0x11E3A, 0x12066): cost <= funds.
bool can_afford(const model::CityState& state, int cost);

// After a handler succeeds: funds -= cost, and the year's construction
// DS:0x6BC0 += cost. A drag the player cancels gives it all back (0x11EFE).
void charge(model::CityState& state, int cost);
void refund(model::CityState& state, int amount);

// 0x120C0, run when a placement can't be afforded: the first time only
// (DS:0x6C9A), and only up to rank 6 (DS:0x6C30), Rome sends 500 Dn. Returns
// whether it did. The placement itself isn't retried.
bool grant_emergency_funds(model::CityState& state);

// 0x0C26E, the Forum's "donate personal savings": if the amount is no more
// than the savings, they lose it and the city gains 90 % of it. Returns
// whether the donation happened.
bool donate_savings(model::CityState& state, int amount);

// 0x09FF1 (0334:6CB1): DS:0x6BE8, the active workshops' average production
// level, 0 with no workshops.
int average_workshop_level(const model::CityState& state);

struct PopulationTax {
    int tax = 0;              // DS:0x6BC6
    int per_head = 0;         // DS:0x6BCA, whole denarii
    int per_head_cents = 0;   // DS:0x6BC8, hundredths
    bool uncollected = false; // DS:0x6C7E: a house outside every forum's and prefecture's reach
};

// 0x282A1: housing cells whose C9D4 bit 0x20 is set (the reach of forums and
// prefectures, where the manual says taxes are collected) add their tax units;
// the tax is units x rate / 20, with the rate the year's average.
PopulationTax population_tax(const model::CityMap& city, int rate, int population);

// 0x283D3: each active workshop's level x 64, times the year's average
// industrial rate, / 25.
int industrial_tax(const model::CityState& state, int rate);

// 0x283D3's second half: the industrial tax pressure DS:0x6BFC. It is reset
// to -50 in a year that ends with no forum; then a rate of 5 % leaves it,
// rates above move it up (more slowly while it is below 3) and rates of 11 %
// and up, or below 5 %, move it twice as fast; clamped to -50..24. Workshops
// lose production as it rises (systems::actors, the workshop level).
int industrial_tax_pressure(int pressure, int rate, int forums);

struct Settlement {
    bool savings_capped = false;  // savings reached 25000: salary stops (message 0x6E82)
    bool tribute_missed = false;  // nothing paid to Rome (message 0x27AF3)
    bool dismissed = false;       // the third missed tribute in a row: the game ends (DS:0x6D6A = 0x3C)
};

// 0x284AA, the year's settlement:
//   savings += salary (capped at 25000, which stops the salary)
//   operating costs = welfare + salary + army wages
//   funds += both taxes - operating costs, not below 0
//   the tribute due grows by 1 (to 100), and is paid from what's left; if
//   funds are still over 500 after it and the year made a profit (taxes -
//   construction - operating costs > 0), Rome takes 60 % of that profit too
//   nothing paid: a missed tribute, three in a row and the governor is dismissed
//   profit or loss = taxes - construction - operating costs - tribute paid
//   welfare is cut to the funds left, if it exceeds them
// Reads last year's taxes from DS:0x6BC6/0x6BC4.
Settlement settle_accounts(model::CityState& state);

// 0x28238's accounts, run when the year turns: the rates' monthly sums become
// averages, then 0x09FF1, 0x282A1, 0x283D3 and 0x284AA; the sums restart and
// last year's pleb count DS:0x6C54 is kept from DS:0x6C56. `industrial_rate_sum`
// is DS:0x6BFE, which the caller keeps. The yearly routine's other calls
// (0x289C0 the army, 0x28C43 the ratings, 0x29023 promotion, 0x2933B) and the
// history writes aren't here.
Settlement run_year(model::CityState& state, int& industrial_rate_sum);

}  // namespace gaius::systems::economy
