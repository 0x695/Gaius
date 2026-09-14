// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/province.hpp
//
// The province map's actors: barbarian armies marching on the city, and the
// player's Cohorts halting, patrolling, attacking and going home. Transcribed
// from the US-build CSR.EXE (2026-09-14);
// docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 28.
//
// The province map is the save's embedded EMPIRE2 block (model::CityState::
// empire, 3496:2754): 40 x 40 terrain bytes, bit 0x80 set where a province
// actor stands. Province actors share the city's actor table and its per-frame
// update (0x23C4C); systems::actors::update hands types 11-13 to
// update_actor() in slot order, as the engine's type table DS:0x134C does.
//
// Types and the states they run (DS:0x1384):
//   11  barbarian army     0x24023: its state; frame = the race's banner
//                          (DS:0x6BD8); ages every 64 frames, at 120 state 2
//   12  army by sea        0x24089: its state; frame by facing; once blocked
//                          (it has reached land) it becomes type 11
//   13  Cohort             0x24133: frame = number x 4 + the flag's waving
//                          (frame counter / 2 & 3); its state
//   state 2   0x242EF  freed
//   state 9   0x24B66  march on the city (sea table for type 12): arrived on
//                      the city cell (0x4A), the army is freed, invades the
//                      city (invade_city) and costs Peace; arrived elsewhere,
//                      state 15. Blocked, it now and then wrecks what's in the
//                      way (by obstacle class, below) or pillages a town
//   state 10  0x24ECE  halt: walk to the destination and stand
//   state 11  0x24EF4  patrol: walk to one point, swap it with the other
//                      (+0x2C/+0x2D); an army within 64 px -> state 12
//   state 12  0x24FF7  attack the army in +0x14; gone, back to the patrol or
//                      halt; within 16 px, the battle (systems::battle)
//   state 13  0x2514C  go home: walk to the fort and stand
//   state 14  0x25172  demobilized: the flag without waving
//   state 15  0x24EA4  an army that reached its target elsewhere: gone after
//                      16 frames
// The names of states 10-13 are the manual's Cohort commands (Halt, Patrol,
// Attack, Go Home) matched to what each does: STRONG INFERENCE.
//
// Movement (0x26059) is the city walker's (systems::actors, findings section
// 20.3) on the 40-wide map with bit 0x80, with three differences transcribed
// here: the step onto the next cell happens before the occupied cell is
// recomputed, a blocked step also sets status bit 8 and keeps the blocked
// cell in +0x1A, and Cohorts (type 13) cross terrain classes 7 and up.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

#include "model/city_state.hpp"
#include "systems/month.hpp"

namespace gaius::systems::province {

inline constexpr int kMapW = 40;
inline constexpr uint8_t kArmyType = 11;
inline constexpr uint8_t kSeaArmyType = 12;
inline constexpr uint8_t kCohortType = 13;

inline constexpr uint8_t kMarch = 9, kHalt = 10, kPatrol = 11, kAttack = 12, kGoHome = 13, kDemobilized = 14,
                         kLinger = 15;

inline constexpr uint8_t kCityTile = 0x4A;       // the player's city on the province map
inline constexpr uint8_t kOccupied = 0x80;       // a province actor stands here
inline constexpr uint16_t kPeaceRating = 0x6C3C;  // Phase 7's Peace rating, lowered by an invasion

// 3496:200C: the walk classes of province terrain (index = tile & 0x7F) for
// armies on land and Cohorts. 0 passes; 1 and 9 block without a class; 2-8
// block with class 1 << (v - 2) (0x26A8F), which a marching army may wreck.
// Cohorts also pass 7, 8 and 9.
extern const std::array<uint8_t, 140> kLandClass;
// 3496:2098: for type 12, water passes and land blocks.
extern const std::array<uint8_t, 140> kSeaClass;

// 3496:1828: the city-map cells invaders enter by, five for each of the
// eight directions an army arrives from (chosen by the generator).
struct Cell {
    uint8_t x, y;
};
extern const std::array<Cell, 40> kCityEntry;

struct Hooks {
    // A Cohort in state 12 has reached the army it attacks (0x25148): the
    // battle screen runs now. Without a hook nothing happens, and the Cohort
    // meets the army again on its next cell.
    std::function<void(model::CityState& state, int cohort, int army)> battle;
};

// One frame of the province actor in `slot` (types 11-13). `tick` is the
// frame counter the city walkers use (DS:0x6D3C = tick % 64, DS:0x6D3E =
// tick % 32).
void update_actor(model::CityState& state, month::Random& random, int tick, int slot, const Hooks* hooks = nullptr);

// 0x26059: one pixel of walking on the province map with a class table.
void walk(model::CityState& state, int slot, const std::array<uint8_t, 140>& classes);

// 0x2D6F4, run after the 18-month counter wraps: from a year that depends on
// the difficulty (DS:0x6CB8, 0-2) and the rank (DS:0x6C30) -- every other
// year at first, then every time -- an army of (walk & 7) + 1 appears on one
// of the map's spawn markers (tiles 0x51-0x58 by sea, 0x59-0x60 by land,
// chosen by low7 & 7; the last match on the map wins) and marches on the city.
// Returns the army's slot, or -1.
int spawn_army(model::CityState& state, const month::Random& random, int difficulty);

// 0x2D891: invaders enter the city from direction `direction` (0-7). The target
// is the first forum whose attack timer (record +0x0C) runs out, or the map's
// centre; the first of the direction's five entry cells that is open land
// (tiles 0x1D-0x49) becomes 0x1D and a type 5 + DS:0x6BDA invader starts
// there in state 3. Returns its slot, or -1.
int invade_city(model::CityState& state, int direction);

}  // namespace gaius::systems::province
