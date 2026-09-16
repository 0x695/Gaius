// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/battle.hpp
//
// The battle screen: a Cohort against a barbarian army, round by round, each
// round a tactic -- Tortoise, Assault, Flank or Charge -- or a retreat.
// Transcribed from the US-build CSR.EXE (2026-09-14);
// docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 27.
//
// A Cohort attacking an army (province state 12) starts a battle when the
// army's pixel position is within 16 of its own (0x26EF1); 0x22116 runs the
// screen. The screen's buttons (0x2250F, by the click's x) run one of five
// routines; each ends with the Cohort's morale clamped to 0-9.
//
// The barbarians. Every province has a race (3496:175E, by province
// DS:0x6CA6), and each race a row of 3496:1790: its banner sprite
// (DS:0x6BD8), the city invader type it sends, less 5 (DS:0x6BDA, used by
// 0x2D891), and its strength against each tactic: DS:0x6BD4 Assault, 0x6BD2
// Flank, 0x6BD0 Charge, 0x6BCE Tortoise. The names are the manual's "sixteen
// types of barbarians", DS:0x2BAD in 16-character fields, in race order.
//
// One round (0x2308E/0x231E2/0x23212/0x23242, then 0x229A3):
//   barbarians = strength x 2 + the army's size (+0x30) + (walk & 7)
//   Romans     = regulars x 3 + irregulars x 2 + auxiliaries / 2 + (low7 & 3),
//                morale 0-1 -4, 2-3 -2, 6-7 +1, 8-9 +2
//   both / 4 (arithmetic shift), then
//   equal:           nothing happens
//   Romans stronger: half the time (walk & 1) morale +1; the army loses the
//                    difference in size; at 0 or less, victory (0x22D5D)
//   barbarians:      half the time morale -1; the Cohort loses one Century of
//                    each kind it has, auxiliaries first, and the Legion's
//                    words with it; with none left, defeat (0x22EDB)
// `walk` and `low7` are the generator's 2EF9:0286 and 0288 (month::Random),
// read as they stand -- the screen draws once a frame while it waits for a
// click, which no save can pin, so the caller supplies them.
//
// The names of the army fields and of the outcome routines are STRONG
// INFERENCE from the screen's texts and the manual; the arithmetic is
// transcribed.

#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "model/city_state.hpp"
#include "systems/month.hpp"

namespace gaius::systems::battle {

enum class Tactic { Tortoise, Assault, Flank, Charge };

struct Race {
    const char* name;      // DS:0x2BAD
    uint8_t banner;        // DS:0x6BD8, the army's province sprite
    uint8_t invader_type;  // DS:0x6BDA: the city invaders are type 5 + this
    uint8_t assault;       // DS:0x6BD4
    uint8_t flank;         // DS:0x6BD2
    uint8_t charge;        // DS:0x6BD0
    uint8_t tortoise;      // DS:0x6BCE
};

// 3496:1790, 6 of each 8 bytes (the last two are 0 in every row).
extern const std::array<Race, 16> kRaces;
// 3496:175E: the race of each of the 50 provinces.
extern const std::array<uint8_t, 50> kProvinceRace;

// Army record fields.
inline constexpr size_t kArmySize = 0x30;  // 1-8 when the 18-month routine places it

// Cohort record fields used here, besides systems::military's Centuries and
// morale: +0x2C/+0x2D the patrol's second point (0 = not patrolling),
// +0x22/+0x24 the patrol point to go back to after an attack, +0x2E/+0x2F the
// home fort's cell, +0x2A the Cohort's number.
inline constexpr size_t kCohortPatrolX = 0x2C;
inline constexpr size_t kCohortResumeX = 0x22;
inline constexpr size_t kCohortResumeY = 0x24;
inline constexpr size_t kCohortHomeX = 0x2E;
inline constexpr size_t kCohortHomeY = 0x2F;

// 0x22146: the race words DS:0x6BD6-0x6BD8 for the province DS:0x6CA6.
void load_race(model::CityState& state);

// The barbarians' strength against a tactic: DS:0x6BCE/0x6BD4/0x6BD2/0x6BD0.
int tactic_strength(const model::CityState& state, Tactic tactic);

enum class Outcome { Even, RomansWon, BarbariansWon };

struct Round {
    int romans = 0;      // DS:0x6DD9 after the shift
    int barbarians = 0;  // DS:0x6DDB after the shift
    Outcome outcome = Outcome::Even;
    bool victory = false;  // the army destroyed: 0x22D5D has run
    bool defeat = false;   // the Cohort destroyed: 0x22EDB has run
};

// One round of `tactic` between the Cohort in slot `cohort` and the army in
// slot `army`, with victory or defeat applied when it ends the battle.
Round fight_round(model::CityState& state, int cohort, int army, Tactic tactic, const month::Random& random);

// 0x230BE after the player confirms "Retreat ?": morale -2, and the Cohort
// stops where it stands (state 10). The army is untouched.
void retreat(model::CityState& state, int cohort);

// 0x22D5D: morale +2; the Cohort goes back to its patrol (state 11, towards
// +0x22/+0x24) or, if it had none, stops where it stands (state 10); the army
// is removed.
void win(model::CityState& state, int cohort, int army);

// 0x22EDB: morale -3, no Centuries left, and the Cohort's standard is back at
// its fort (state 10), clearing the province cell it stood on (bit 0x80). The
// Legion's words are already down by the Centuries lost in the rounds.
void lose(model::CityState& state, int cohort);

// ---------------------------------------------------------------------------
// The hand-over to Cohort 2 (findings section 44). With cohort.exe beside the
// game the battle screen first offers the separate Cohort 2 program. Taking
// it, the game writes the battle into saved words, saves itself as "csr0.dat",
// writes that name into cohort.csr and exits; CAESAR.BAT runs "cohort.exe
// caesar", then "csr.exe cohort", which loads the save cohort.csr names and
// takes the battle's result back.

// The words (DS:0x6CBE-0x6CE2), saved like every other global word.
inline constexpr uint16_t kHandoverPending = 0x6CE2;  // 1 while a battle is out
inline constexpr uint16_t kHandoverOutcome = 0x6CE0;  // 0 when handed over; Cohort 2's result
inline constexpr uint16_t kHandoverCohort = 0x6CDC, kHandoverArmy = 0x6CDE;  // the actor slots
inline constexpr uint16_t kHandoverRegularsBefore = 0x6CD8, kHandoverIrregularsBefore = 0x6CD6,
                          kHandoverAuxiliariesBefore = 0x6CD4;
inline constexpr uint16_t kHandoverRegulars = 0x6CD2, kHandoverIrregulars = 0x6CD0,
                          kHandoverAuxiliaries = 0x6CCE;  // after the battle, as Cohort 2 leaves them
inline constexpr uint16_t kHandoverMorale = 0x6CCC, kHandoverNumber = 0x6CDA;
inline constexpr uint16_t kHandoverArmyBefore = 0x6CC8, kHandoverArmySize = 0x6CC6;
inline constexpr uint16_t kHandoverRace = 0x6CCA, kHandoverYear = 0x6CC4, kHandoverMonth = 0x6CC2,
                          kHandoverProvince = 0x6CC0, kHandoverLanguage = 0x6CBE;
// The one outcome 0x23272 tells apart: the army destroyed. Anything else --
// including the 0 the hand-over leaves, if Cohort 2 never ran -- is taken as
// the Cohort beaten.
inline constexpr int kCohort2Victory = 2;

// 0x23493: the Cohort's Centuries (before and, until Cohort 2 changes them,
// after), morale and number, the army's size, the race, the date, the
// province and DS:0x6DE7 into the words; pending 1, outcome 0.
void hand_over(model::CityState& state, int cohort, int army);

// 0x23272, run by "csr.exe cohort" after loading the save: pending 0; the
// Cohort takes the Centuries and morale Cohort 2 left, the Legion's words
// lose what the Cohort lost, the army takes its size; then with outcome 2 the
// Cohort goes back to its patrol or stops where it stands and the army is
// removed (as win(), without the morale), and otherwise the Cohort's morale
// drops 3 and its standard goes back to its fort (as lose(), keeping the
// Centuries). Morale isn't clamped here.
void take_back(model::CityState& state);

// cohort.csr: 14 bytes copied from the save name's buffer, which in the US
// build is DS:0x0DEA "csr0.dat" followed by "starter.xmi" -- so the file is
// "csr0.dat\0start" (0x03344 writes it, 0x03388 reads it back into the buffer).
inline constexpr const char* kHandoverSaveName = "csr0.dat";
std::string handover_file_bytes();
bool write_handover_file(const std::string& path);
// The save name the file holds (up to its NUL), or "" if it can't be read.
std::string read_handover_file(const std::string& path);

}  // namespace gaius::systems::battle
