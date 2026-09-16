// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/sounds.hpp
//
// The effects the simulation asks for. Where the original calls its effect
// routine (31E0:0634, findings section 45) -- a fire, a demolition, a fountain
// running dry, invaders, rioters, a battle -- the transcription calls
// request(effect number), and the application takes the requests and plays
// them (audio::GameAudio::play_effect, audio::kEffectFiles). Only one sound
// plays at a time, so when a step asks for several, the last one is heard.
// The queue is per thread; nothing is played without a caller taking it.

#pragma once

#include <vector>

namespace gaius::systems::sounds {

inline constexpr int kDemolish = 2, kFire = 3, kWater = 4, kWarCry = 5, kFanfare = 6, kSword = 7, kPutOut = 15,
                     kBarbarianHorn = 16, kWallCollapse = 17, kCheer = 18;

void request(int effect);
// The requests since the last call, oldest first.
std::vector<int> take();

// The city sounds. While it draws the city view the renderer sets a flag for
// each kind of sound source it draws (DS:0x6CE6-0x6CF6, render::
// city_sound_flags).
struct CitySoundFlags {
    bool theatre = false;     // DS:0x6CF6, tile 0xF0 (0x202D6)
    bool coliseum = false;    // DS:0x6CF4, tile 0xF1 (0x2054A)
    bool hippodrome = false;  // DS:0x6CF2, tile 0xF2 (0x202E2)
    bool forum = false;       // DS:0x6CF0, tiles 0xE0-0xE7 (0x202F4)
    bool workshop = false;    // DS:0x6CEE, tiles 0xF5 and 0xF6 (0x20613, 0x2080E)
    bool market = false;      // DS:0x6CEC, tile 0xF4 (0x205C9)
    bool fountain = false;    // DS:0x6CEA, tiles 0xB9 and 0xBB, working fountains (0x20025)
    bool romans = false;      // DS:0x6CE8, a walker of type 3-4 (0x06B15)
    bool barbarians = false;  // DS:0x6CE6, a walker of type 5-7 (0x06B3D)
};

// 0x0FFD4, on each frame that runs a step, unless the city sounds option is
// off (DS:0x529E): when the 512-step counter DS:0x6D36 (month::SimState::ticks
// % 512) is 0, 0x40, 0x80, 0xC0, 0x100, 0x140, 0x170, 0x1A0 or 0x1D0, the
// effect for that count if its flag is set -- MKT_THEA, COLISEUM, HORSES,
// HUBBUB, WORKSHOP, HUBBUB, FOUNTAIN, MARCHROM, MARCHBAR -- else -1. The
// game then clears every flag.
int city_sound_effect(int counter_512, const CitySoundFlags& flags);

}  // namespace gaius::systems::sounds
