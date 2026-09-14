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
#include "systems/construction.hpp"
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

// ---------------------------------------------------------------------------
// Roads, towns and the Imperial Highway
//
// Province roads are tiles 0x36-0x41 and the highway 0x6D-0x78 (0x78 is where
// it enters the map, DS:0x6C90/0x6C8E, set when the province starts at
// 0x06331); the towns are 0x61 < 0x79 < 0x7A < 0x4C, smallest to largest. The
// names are STRONG INFERENCE from the traces below and the manual's "connect
// the capital city to those small towns".

// 3496:1810: the exits of a road class -- N 0x80, E 0x20, S 0x08, W 0x02 --
// and whether the trace keeps a branch point on it (the T and X pieces).
struct Exits {
    uint8_t exits;
    uint8_t branch;
};
extern const std::array<Exits, 12> kRoadExits;
// 3496:1CE0: road classes for a town's link to the city: roads, highways,
// wall gates (0x44/0x45), crossings, towns and forts pass; the city
// (0x4A/0x4B) is 0xFF.
extern const std::array<uint8_t, 128> kTownRoads;
// 3496:1D62: the Imperial Highway's: highway pieces, gates and crossings.
extern const std::array<uint8_t, 128> kHighwayRoads;

// 0x2E377: follows the road from (x, y) through `classes`, keeping up to 50
// branch points, and returns whether it reaches a class-0xFF tile. Each cell
// is visited once (3496:2112).
bool connected(const formats::empire2::EmpireMap& map, int x, int y, const std::array<uint8_t, 128>& classes);

// 0x2E249, at step 80: every town linked to the city (kTownRoads) grows a
// grade one month in six (`counter`, DS:0x6DFE); every town not linked shrinks
// a grade. Returns the linked towns' count, DS:0x6C92, which the save doesn't
// keep (the advisor's text 0x0D049 and the ratings read it).
int develop_towns(model::CityState& state, int& counter);

// 0x2E220, at step 80 after the towns: DS:0x6C8C = whether the highway from its
// entry point reaches the city.
void connect_highway(model::CityState& state);

// 0x2E0BE, first of step 105's monthly routines: clears every occupancy bit;
// counts the road, wall and highway tiles (0x36-0x49, 0x62-0x77) into
// DS:0x6C8A, and the one numbered DS:0x6C88 (the last month's roll) wears away
// to 0x1D; then DS:0x6C86 = straight pieces (0x36-0x37, 0x6D-0x6E) less 4 per
// corner (0x38-0x3B, 0x6F-0x72). `counter` (DS:0x6DFC, 0-3) gates only the
// message. Returns whether a road wore away.
bool monthly_pass(model::CityState& state, int& counter);

// ---------------------------------------------------------------------------
// The province toolbar's Fort and Cohort commands (construction dispatcher
// DS:127C, ids 29-33). The engine's handlers wait for the player's clicks; the
// cell a click means is (x + 8) / 16 plus the map's scroll (DS:0x6CB2,
// DS:0x6CB0). These take the clicked cells directly. Each order refuses a
// demobilized Cohort; the build routine charges the cost (systems::economy).

// 0x1548A, Fort: on grass (0x1D-0x35) or a land border marker (0x59-0x60),
// compared with the occupancy bit, not on the highway's entry, and with fewer
// than 10 Cohorts (DS:0x6C12): the cell becomes 0x4D and a Cohort stands there
// in state 10 with morale 5, its fort at the cell, numbered by the first of
// 0-9 no active Cohort has -- the new one, still 0, counts too. Returns its
// slot, or -1.
int place_fort(model::CityState& state, int x, int y);

// 0x15710: the Cohort whose fort is at (x, y) is freed (clearing a fort).
void disband_fort(model::CityState& state, int x, int y);

// 0x1577C, Halt: the Cohort stops where it stands (state 10).
bool order_halt(model::CityState& state, int cohort);
// 0x1586D, Cohort Patrol: a Cohort with any Centuries walks to the first point
// and patrols between it and the second (state 11).
bool order_patrol(model::CityState& state, int cohort, int x1, int y1, int x2, int y2);
// 0x15A15, Cohort Attack: a Cohort with any Centuries goes for the army
// (state 12), forgetting its patrol.
bool order_attack(model::CityState& state, int cohort, int army);
// 0x15BB1, Cohort Go Home: back to its fort (state 13).
bool order_go_home(model::CityState& state, int cohort);

// ---------------------------------------------------------------------------
// Province construction: Clear Area (id 35), Provincial road (36), Great Wall
// (37) and Highway (42). The pieces come from the city's drag auto-tiling --
// the same eight-neighbour snapshot on the 40-wide map (0334:450D), the same
// 161 patterns (construction::kRoadPatterns, 0x17CB9) -- with the province's
// own connectable ranges and neighbour re-tiling (0x19073, 0x1A84E, 0x1D504).
// Every handler first clears the cell's occupancy bit, and refuses the
// highway's entry and anything below 0x1D.
//
// Tiles: roads 0x36-0x41 and highways 0x6D-0x78 in the city road's twelve
// shapes (highway = road + 0x37); walls 0x42/0x43 straight, 0x46-0x49 and
// 0x68-0x6C the other shapes; 0x44/0x45 a road or highway through a wall;
// 0x7B/0x7C a road crossing a highway; 0x62-0x67 towers on walls. STRONG
// INFERENCE for the names, from which pieces each handler writes.

// What the build routine does with the cost: a refusal (DS:0x6D0A = 1) isn't
// charged, and neither is a road, highway or wall laid again over one of its
// own pieces, which the handlers rewrite but also flag with DS:0x6D0A = 1.
enum class Built { Refused, Charged, Free };

// 0x15C91: a fort (0x4D) is razed and its Cohort freed; grass (<= 0x24), the
// city, spawn markers and towns (0x4A-0x61, 0x79, 0x7A) are refused; anything
// else becomes 0x1D. Neighbours aren't re-tiled.
Built clear_province(model::CityState& state, int x, int y);

// 0x15EF0: on open land, a road, or a spawn marker: the pattern's piece among
// roads, the city and forts (0x4A-0x4D), small towns (0x61), gates and
// crossings. On a wall (0x42/0x43) a gate; on a highway (0x6D/0x6E) a crossing.
Built place_province_road(model::CityState& state, construction::DragState& drag, int x, int y);

// 0x1645D: as the road, among highways, the city, forts, small towns and
// gates, with the road piece shifted to the highway's. On a wall a gate unless
// a neighbour is already that gate; on a road (0x36/0x37) a crossing.
Built place_highway(model::CityState& state, construction::DragState& drag, int x, int y);

// 0x169A9: on open land, a wall or tower piece, or a spawn marker: the
// pattern's piece among walls, gates and towers, in the wall's shapes. On a
// road or highway a gate.
Built place_great_wall(model::CityState& state, construction::DragState& drag, int x, int y);

// 0x17024, Great Tower (id 41): a tower on a wall piece -- 0x42 -> 0x66, 0x43 ->
// 0x67, 0x46-0x49 -> 0x62-0x65 -- compared with the occupancy bit, and not
// cleared first; anything else is refused. Neighbours aren't re-tiled.
Built place_great_tower(model::CityState& state, int x, int y);

}  // namespace gaius::systems::province
