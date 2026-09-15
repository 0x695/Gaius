// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/messages.hpp
//
// The messages the game shows along the top of the city and province views:
// fires, collapses, barbarians, tribute, population milestones. Transcribed
// from the US-build CSR.EXE (2026-09-15);
// docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 35.
//
// One message shows at a time. A poster stores the text's far pointer
// (DS:0x6C74) and starts the 80-frame timer DS:0x6C7A -- but only when the
// timer is 0, so a message posted while another shows is lost. Posters with a
// place set DS:0x6C72 = 1, DS:0x6C70 = 0 for the city or 1 for the province
// map, and keep the cell less 10 in x and 5 in y in DS:0x6C6E/0x6C6C, where a
// click on the message moves the view (0x0F982). The display routine (0x279AC)
// runs each frame while the messages option DS:0x6C78 is on: a sound on the
// first frame, the text's two 28-character lines at (16, 14) and (16, 30), and
// the timer counts down.
//
// The texts are the executable's (the pointers 0x277xx store in DS:0x6E2A-
// 0x6ED4), each two lines of 28 characters.

#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "model/city_state.hpp"

namespace gaius::systems::messages {

enum class Id {
    None,
    RoadMaintenance,      // DS:0x6E86, 0x27CBA: a road wore away
    BuildingMaintenance,  // DS:0x6E8A, 0x27D58: a building collapsed
    FirePrevention,       // DS:0x6E8E, 0x27D09: a fire
    BarbariansEntering,   // DS:0x6E92, 0x2D908
    BarbariansSighted,    // DS:0x6E96, 0x27B3C, with the province's name
    ArrestOrdered,        // DS:0x6E9A, the third missed tribute
    TributeAgain,         // DS:0x6E9E, the second
    TributeUnpaid,        // DS:0x6EA2, the first
    Population20000,      // DS:0x6EA6 ... 0x6EC2, the milestones 0x27BA1
    Population16000,
    Population12000,
    Population8000,
    Population4000,
    Population2000,
    Population1000,
    Population200,
    Unrest,                   // DS:0x6EC6, 0x2DC4B: rioters
    ConstructionPlebs,        // DS:0x6ECA, 0x11C8F: fewer than 50 pleb groups
    SalaryStopped,            // DS:0x6E82, 0x284DA
    NoFort,                   // DS:0x6E2A, 0x15482
    NoForum,                  // DS:0x6E2E, 0x14E1D
    NoFactory,                // DS:0x6E32, 0x15252
    ProvinceWorkers,          // DS:0x6E72, 0x2E15E: a province road wore away
    ApproachingHighway,       // DS:0x6E76
    ApproachingRoads,         // DS:0x6E7A, 0x24CE5
    ApproachingTowns,         // DS:0x6E7E, 0x24E22
    NoCity,                   // DS:0x6ED2, 0x0F7D3: a new game's first message
};

// The executable's text: two lines of 28 characters (padded with spaces).
std::string text(Id id);

enum class Place { None, City, Province };

struct Message {
    Id id = Id::None;
    Place place = Place::None;
    int x = 0, y = 0;  // the cell
    std::string text;  // two lines of 28 characters
};

Message plain(Id id);
Message at(Id id, Place place, int x, int y);
// 0x27B3C: "Barbarians sighted." with characters 7-22 of the first line
// replaced by the province's 16-character name field (DS:0x2CAE).
Message sighted(int province, int x, int y);

// The province names as the executable stores them, 16 characters each.
extern const std::array<const char*, 50> kProvinceNameFields;

struct Board {
    int timer = 0;    // DS:0x6C7A
    Message current;  // DS:0x6C74, 0x6C72/0x6C70, 0x6C6E/0x6C6C

    // A poster: only while nothing shows. Returns whether it was posted.
    bool post(const Message& message, int frames = 80);
    // 0x279D1, once a drawn frame (while the messages option is on).
    void tick();
    bool showing() const { return timer > 0; }
};

// 0x27CBA, 0x27D09, 0x27D58: the counter word counts down on each call and
// the message posts when it reaches 0 while nothing shows, when it starts
// again at 5. DS:0x6C66 road wear, DS:0x6C6A fire, DS:0x6C68 collapse -- saved
// words.
inline constexpr uint16_t kRoadWearCounter = 0x6C66, kFireCounter = 0x6C6A, kCollapseCounter = 0x6C68;
bool post_limited(model::CityState& state, Board& board, uint16_t counter, const Message& message);

// 0x27BA1, at step 80: the first time the population DS:0x6C0E exceeds 200,
// 1000, 2000, 4000, 8000, 12000, 16000 and 20000, its flag in table_10 is set
// and its message posted (the flag is set whether or not the message shows).
void check_milestones(model::CityState& state, Board& board);

// 0x27AF3, after a year with no tribute paid: by the missed count DS:0x6BB8,
// 1 unpaid, 2 again, 3 the arrest.
void tribute_missed(const model::CityState& state, Board& board);

// 0x0FAD3, every frame: with the funds at or below 1000 and the warning not yet
// given (DS:0x6BE6), it is given -- a full-screen warning (0x084B1, text
// DS:0x085F). Returns whether it was given now.
bool funds_warning(model::CityState& state);
extern const std::array<const char*, 6> kFundsWarning;

}  // namespace gaius::systems::messages
