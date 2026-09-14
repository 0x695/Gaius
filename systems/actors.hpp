// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/actors.hpp
//
// The city's walkers: allocation, the per-tick update, movement, the city
// behaviour states and the spawners that create them. Transcribed from the
// US-build CSR.EXE (2026-09-13); derivation in
// docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 20.
//
// The main loop (flat 0xFA13) runs, once per frame: a random draw
// (2EF9:1425), then -- when the speed gate lets this frame be a simulation
// step -- the frame counters (0x1147E), this update (0x23C4C) and one step of
// the month (0x2936A). So walkers move one pixel per step.
//
// The actor table is DS:0x5D84, 70 records of 50 bytes (model::Actor::raw).
// Record fields (offsets into raw; w = word, b = byte):
//   +00 w  sprite frame            +12 b  destination column
//   +02 w  x, world pixels         +13 b  destination row
//   +04 w  y, world pixels         +14 w  home record, or the chased actor
//   +06 b  active                  +17 b  status bits (kStatus*)
//   +07 b  type                    +18 w  cell (row * 100 + column)
//   +08 w  own slot index          +1A w  cell of the last blocked step
//   +0A b  facing, 0-7 clockwise   +1E b  class of that obstacle (1 << (v - 2))
//        from north                +1F b  age, in counter periods
//   +0B b  following an obstacle   +22 w  column of the last blocked step
//   +0C b  which hand follows it   +24 w  row of the last blocked step
//   +0D b  follow target column    +26 w  actor last touched (0x26D9D)
//   +0E b  follow target row       +28 w  state timer
//   +0F b  pixels left in the cell +31 b  behaviour state
//   +10 b  steps until the hand flips
//   +11 b  the flip interval, +2 each flip
//
// Types (0x23C4C's table DS:0x134C) and the city states they run (DS:0x1384):
//   0-2  forum citizens    state 1  walk roads to a map edge, setting C9D4
//                                   bits 0x12 on the 3x3 around each cell
//   3, 9 (handlers do nothing)
//   4    barracks patrol   state 7  walk roads lowering land value by 2 around
//                                   each cell; every fifth cell look for a
//                                   hostile within 160 px -> state 8, chase it
//                                   over open ground and remove what it touches
//   5-7  invaders          state 3  cross open ground to a target, demolishing
//                                   buildings, clearing trees, breaching walls
//   8    workshop trader   state 4  walk roads; on a market cell (C9D4 0x08)
//                                   raise the workshop's sales, then leave
//   10   rioter            state 5/6 from a collapsed house: wander open
//                                   ground demolishing, pausing 8 ticks at each
//                                   stop
//   11-13 province-map actors (types 11 and 12 march, 13 is the player's
//        army) -- NOT transcribed: they move on the 40x40 province map, which
//        model::CityState doesn't carry. update() leaves them untouched.
// Each city type's handler runs its state, sets the walking frame
// (MOREMEN.PL8: base + facing set * 3 + stride), and ages the walker every 16
// ticks (types 5-7: 64); at its limit the walker goes to state 2 and is freed.
//
// Counters kept in the save (global words): DS:0x6C1A types 0-2 (max 30),
// 0x6C18 types 3-4 (8), 0x6C16 types 5-7 and 10-12 (12), 0x6C14 types 8-9
// (10), 0x6C12 type 13 (10). Cell layer 7BB4 bit 0x40 marks a cell a walker
// stands on.
//
// Not modeled: sounds (0x32434) and messages (0x27A54) the spawns trigger.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

#include "model/city_state.hpp"
#include "systems/month.hpp"

namespace gaius::systems::province {
struct Hooks;
}

namespace gaius::systems::actors {

namespace field {
inline constexpr int kFrame = 0x00, kX = 0x02, kY = 0x04, kActive = 0x06, kType = 0x07, kIndex = 0x08;
inline constexpr int kFacing = 0x0A, kFollowing = 0x0B, kHand = 0x0C, kFollowX = 0x0D, kFollowY = 0x0E;
inline constexpr int kPixelsLeft = 0x0F, kFlipCountdown = 0x10, kFlipInterval = 0x11;
inline constexpr int kDestX = 0x12, kDestY = 0x13, kHome = 0x14, kStatus = 0x17, kCell = 0x18;
inline constexpr int kBlockedCell = 0x1A, kBlockedClass = 0x1E, kAge = 0x1F, kBlockedX = 0x22, kBlockedY = 0x24;
inline constexpr int kContact = 0x26, kTimer = 0x28, kState = 0x31;
}  // namespace field

// Status bits (+0x17).
inline constexpr uint8_t kStatusStopped = 0x01;  // arrived, or boxed in (0x26BB0)
inline constexpr uint8_t kStatusShared = 0x02;   // another walker was already on the cell
inline constexpr uint8_t kStatusCentre = 0x04;   // this tick began a new cell

enum State : uint8_t {
    kIdle = 0,
    kCitizenWalk = 1,
    kRemove = 2,
    kInvade = 3,
    kTrade = 4,
    kRiot = 5,
    kRiotPause = 6,
    kPatrol = 7,
    kChase = 8,
};

// Walkability by tile id: 0 passes, anything else blocks and gives the
// obstacle class 1 << (value - 2). 3496:1E04 (roads and plazas only) is what
// citizens, traders and patrols walk; 3496:1F08 (open ground too) invaders,
// rioters and chasing soldiers.
const std::array<uint8_t, 256>& road_table();
const std::array<uint8_t, 256>& ground_table();

// 0x2D68A: whether a walker may be spawned on (x, y) -- tiles 0x36-0x43 and
// 0x5E-0x61. (Its test for 0x82-0x89 compares a signed byte and never passes.)
bool spawnable(const model::CityMap& city, int x, int y);

// 0x5C39: takes the first free slot for a walker of `type` on cell (x, y),
// or returns -1 when the type's counter is full (checked at the first free
// slot) or no slot is free.
int spawn(model::CityState& state, int type, int x, int y);

// 0x5DC7: frees a slot, decrementing its counter and clearing 7BB4 bit 0x40.
void release(model::CityState& state, int slot);

// 0x23C4C: one tick for every active walker. `tick` is the number of times
// the frame counters (0x1147E) have run, including this one; the 16-, 32- and
// 64-period counters the handlers test are its remainders. Province actors
// (types 11-13) run systems::province::update_actor in their slot's turn.
void update(model::CityState& state, month::Random& random, int tick, const province::Hooks* hooks = nullptr);

// The per-row spawners, run after the housing row of steps 0-99: forums
// (0x2D2F5, steps 0 and 50), workshops (0x2CE7C, step 25 + record) and
// barracks (0x2CD1C, step 75 + record). They read, but don't draw, the random
// number.
void run_spawners(model::CityState& state, const month::Random& random, int step);

// 0x2DB49's tail: a house whose land value passed its limit collapsed at
// (x, y); a rioter comes out of it heading south, and DS:0x6C3C drops by 2.
void spawn_rioter(model::CityState& state, int x, int y);

// 0x2D19A + 0x2CE7C's formula: a workshop record's production level, 0-7.
// Exposed for tests; run_spawners applies it.
int workshop_level(model::CityState& state, int record);

// One perimeter cell of the square ring around a size x size footprint, as
// the tables at 3496:1A20 / 1A90 / 1B10 list them: clockwise from the
// top-left corner outside the footprint.
std::pair<int, int> ring_offset(int size, int k);

}  // namespace gaius::systems::actors
