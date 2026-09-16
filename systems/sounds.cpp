// SPDX-License-Identifier: GPL-3.0-or-later
#include "systems/sounds.hpp"

#include <utility>

namespace gaius::systems::sounds {

namespace {
thread_local std::vector<int> queue;
}

void request(int effect) { queue.push_back(effect); }

std::vector<int> take() { return std::exchange(queue, {}); }

int city_sound_effect(int counter_512, const CitySoundFlags& f) {
    switch (counter_512) {
        case 0x000: return f.theatre ? 8 : -1;
        case 0x040: return f.coliseum ? 9 : -1;
        case 0x080: return f.hippodrome ? 10 : -1;
        case 0x0C0: return f.forum ? 14 : -1;
        case 0x100: return f.workshop ? 19 : -1;
        case 0x140: return f.market ? 14 : -1;
        case 0x170: return f.fountain ? 11 : -1;
        case 0x1A0: return f.romans ? 12 : -1;
        case 0x1D0: return f.barbarians ? 13 : -1;
        default: return -1;
    }
}

}  // namespace gaius::systems::sounds
