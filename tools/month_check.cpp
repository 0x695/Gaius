// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius CLI tool: month_check
//
// Validates systems::month against two saves of one session, the later one
// taken some steps (or a month) after the earlier. Neither save holds the step
// counter (DS:0x6D9D) or the random generator's state, so for every possible
// starting step of the earlier save it runs systems::month::run_step forward
// and, after each step, compares the parts of the later save that don't depend
// on the generator:
//   - the tile grid (housing development),
//   - the forum, barracks and workshop records (table_480/120/720), whose
//     timers count down on fixed steps,
//   - DS:0x6C1C (month), DS:0x6C10/0x6C0E (population, step 101) and
//     DS:0x6C00 (step 101's running sum).
// Unrest (54A4) and the walkers depend on the generator, so they're only
// counted, not required to match.
//
// Usage: month_check <earlier.SAV> <later.SAV> [max_steps [--all]]
// Exit status: 0 if some start step reproduces the later save, 1 if none does
// (the saves may be in the wrong order, from different sessions, or too far
// apart), 2 if a save can't be loaded.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

#include "formats/save/save.hpp"
#include "model/city_state.hpp"
#include "systems/month.hpp"

using namespace gaius;

namespace {

int diff_bytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    int n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) n += a[i] != b[i];
    return n;
}

struct Diff {
    int tiles = 0, records = 0, globals = 0, unrest = 0, actors = 0;
    bool deterministic_match() const { return tiles == 0 && records == 0 && globals == 0; }
};

Diff compare(const model::CityState& a, const model::CityState& b) {
    Diff d;
    for (int y = 0; y < model::kCityH; ++y) {
        for (int x = 0; x < model::kCityW; ++x) {
            d.tiles += a.city.tile[y][x] != b.city.tile[y][x];
            d.unrest += a.city.unrest[y][x] != b.city.unrest[y][x];
        }
    }
    d.records = diff_bytes(a.table_480, b.table_480) + diff_bytes(a.table_120, b.table_120) +
                diff_bytes(a.table_720, b.table_720);
    for (uint16_t ds : {uint16_t{0x6C1C}, uint16_t{0x6C10}, uint16_t{0x6C0E}, uint16_t{0x6C00}})
        d.globals += model::global_word(a, ds) != model::global_word(b, ds);
    for (size_t i = 0; i < a.objects.size(); ++i) d.actors += a.objects[i].raw != b.objects[i].raw;
    return d;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <earlier.SAV> <later.SAV> [max_steps]\n", argv[0]);
        return 1;
    }
    const int max_steps = argc > 3 ? std::atoi(argv[3]) : 2 * systems::month::kStepsPerMonth;
    // --all as a fourth argument prints every matching (start step, steps,
    // unrest cells differing, actor records differing) as a MATCH line.
    const bool all = argc > 4 && std::string(argv[4]) == "--all";

    std::unique_ptr<model::CityState> from, to;
    try {
        from = std::make_unique<model::CityState>(model::load(formats::save::load(argv[1])));
        to = std::make_unique<model::CityState>(model::load(formats::save::load(argv[2])));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "failed to load: %s\n", e.what());
        return 2;
    }

    const Diff before = compare(*from, *to);
    std::printf("%s -> %s\n  unchanged: tiles %d, record bytes %d, globals %d, unrest %d, actor records %d\n",
                argv[1], argv[2], before.tiles, before.records, before.globals, before.unrest, before.actors);

    int windows = 0;
    for (int start = 0; start < systems::month::kStepsPerMonth; ++start) {
        auto st = std::make_unique<model::CityState>(*from);
        auto sim = systems::month::sim_state_from_save(*st);
        sim.step = start;
        // GAIUS_SEED, if set, replaces the generator's shift register: a check
        // of how much a match depends on the unknown generator state.
        if (const char* seed = std::getenv("GAIUS_SEED")) sim.random.lfsr = static_cast<uint16_t>(std::atoi(seed));
        int first = -1, last = -1;
        Diff best;
        for (int k = 1; k <= max_steps; ++k) {
            systems::month::run_step(*st, sim);
            const Diff d = compare(*st, *to);
            if (all && d.deterministic_match())
                std::printf("MATCH %d %d %d %d\n", start, k, d.unrest, d.actors);
            if (d.deterministic_match()) {
                if (first < 0) {
                    first = k;
                    best = d;
                }
                last = k;
            } else if (first >= 0) {
                break;
            }
        }
        if (first < 0) continue;
        ++windows;
        std::printf("  start step %3d: matches after %d-%d steps (unrest differs in %d cells, actor records %d)\n",
                    start, first, last, best.unrest, best.actors);
    }
    if (windows == 0) std::printf("  no start step reproduces the later save's tiles, records and globals\n");
    return windows == 0 ? 1 : 0;
}
