// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/forum.hpp
//
// What the Forum screens' buttons do: the arrow buttons on the rates, wages,
// welfare, salary and donation, the Tribune of the Plebs' duty arrows, and the
// Military Advisor's Cohort buttons. Transcribed from the US-build CSR.EXE
// (2026-09-15); docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 32.
//
// Every arrow moves its word by one per click, within the limits of section
// 25.2. The screens themselves (layout, texts, art) are Gaius's own; which
// screen shows which word is STRONG INFERENCE from the manual and from the
// words each screen's draw routine reads (0x0E6E3 the Treasurer's rates,
// 0x0E822 the Tribune's duties, 0x0B345 the Legion).

#pragma once

#include "model/city_state.hpp"

namespace gaius::systems::forum {

enum class Control {
    PopulationTax,  // DS:0x6C04, 0-25 (0x0E7AE / 0x0E7C0)
    IndustrialTax,  // DS:0x6C02, 0-25 (0x0E7D2 / 0x0E7E4)
    Conscription,   // DS:0x6C06, 0-50 (0x0E650 / 0x0E65C)
    ArmyWages,      // DS:0x6C08, 0-999 (0x0E637 / 0x0E644)
    Welfare,        // DS:0x6C46, 0-9999 (0x0EB13 / 0x0EB2C)
    Salary,         // DS:0x6C2C, 0-9999 (0x0C164 / 0x0C171)
    Donation,       // DS:0x6C28, 0 up to the savings DS:0x6C2E (0x0C2C3 / 0x0C2D1)
};

uint16_t control_word(Control control);

// One click on the control's up (`delta` > 0) or down arrow. Returns the new
// value.
int adjust(model::CityState& state, Control control, int delta);

// The Tribune's five duties, in the table order of systems::plebs.
enum class Duty { FirePrevention, BuildingMaintenance, RoadMaintenance, Construction, ArmyDuty };

uint16_t duty_word(Duty duty);

// 0x0EB45-0x0EC88, a duty's up arrow: one pleb group from the unassigned, or,
// with none unassigned, from the last duty after this one that has any (army
// duty first, then construction, ...). Army duty takes only unassigned plebs.
// Returns whether a group moved. Army duty's arrows recompute the auxiliaries
// (DS:0x6C52 = army duty / 16), as the screen's draw routine does (0x0E85A).
bool raise_duty(model::CityState& state, Duty duty);
// 0x0EB8F-0x0EC8D, the down arrow: one group back to the unassigned.
bool lower_duty(model::CityState& state, Duty duty);

// The Military Advisor's Cohort on display, DS:0x6C0A (its number, 0-9).
inline constexpr uint16_t kSelectedCohort = 0x6C0A;

// 0x0E54C: the actor slot of the active Cohort numbered DS:0x6C0A, or -1.
int selected_cohort_slot(const model::CityState& state);

// 0x0E59C, the next Cohort: while the number is below the Cohort count
// (DS:0x6C12), step it up until a Cohort has it (at most to 11); 10 or more
// wraps to 0.
void next_cohort(model::CityState& state);
// 0x0E60E, the previous one: step down until a Cohort has it, not below 0.
void previous_cohort(model::CityState& state);
// 0x0E5D5: the Cohort on display switches between mobilized (state 10) and
// demobilized (state 14). Returns whether there was one.
bool toggle_mobilized(model::CityState& state);

}  // namespace gaius::systems::forum
