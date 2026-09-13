// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius CLI tool: sim_check
//
// Validates Gaius's simulation against a real save. The engine rebuilds A2C4
// (coverage) and C9D4's service bits from nothing at the end of every month --
// steps 100-105 of its 106-step month: reset, water, then the four quarter
// scans -- and a save carries the result. So systems::service::rebuild_services
// over a save's own tile grid should reproduce those layers cell for cell, to
// the extent that every handler that writes them has been transcribed.
// Mismatches are broken down by row band and by tile id, so a missing handler
// shows up as a cluster.
//
// It also compares systems::housing::population_units with the engine's own
// count (DS:0x6C10). That count is taken at step 101, and housing changes during
// steps 0-99 of the next month, so a save taken mid-month can differ by the
// population of whatever changed since.
//
// The water pass rewrites fountain tiles and their levels (7BB4), so those are
// compared too.
//
// Some saves are written mid-month. One written between steps 101 and 102 (the
// reset and water have run, none of the scans) shows as zero saved coverage and
// service bits other than water; a house that changed after the last scan shows
// as a small cluster of coverage mismatches around it.
//
// Land value is deliberately not compared: the engine never resets it, so it
// carries history a single pass can't reproduce.
//
// Usage: sim_check <CAESARxx.SAV>

#include <cstdio>
#include <exception>
#include <map>
#include <string>
#include <vector>

#include "formats/save/save.hpp"
#include "model/city_state.hpp"
#include "systems/housing.hpp"
#include "systems/service.hpp"

using namespace gaius;

namespace {

int saved_word(const formats::save::SaveFile& sf, uint16_t ds) {
    const uint8_t* globals = sf.block("global_words_128").first;
    for (int i = 0; i < 128; ++i) {
        if (formats::save::kGlobalWordDsAddress[i] == ds) return globals[2 * i] | (globals[2 * i + 1] << 8);
    }
    return -1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <CAESARxx.SAV>\n", argv[0]);
        return 1;
    }

    formats::save::SaveFile sf;
    try {
        sf = formats::save::load(argv[1]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "failed to load %s: %s\n", argv[1], e.what());
        return 2;
    }

    const model::CityState st = model::load(sf);
    model::CityMap sim = st.city;
    systems::service::ServiceState svc;
    svc.housing_coverage_base = saved_word(sf, 0x6BF8);
    systems::service::rebuild_services(sim, svc);

    std::printf("%s\n  housing_coverage_base (DS:0x6BF8) = %d\n", argv[1], svc.housing_coverage_base);

    // --- Coverage (A2C4) ---
    int match = 0, saved_nonzero = 0, sim_nonzero = 0, saved_higher = 0, sim_higher = 0;
    int band_match[4] = {0, 0, 0, 0};
    std::map<int, int> mismatch_by_tile;
    std::vector<std::string> examples;
    for (int y = 0; y < model::kCityH; ++y) {
        for (int x = 0; x < model::kCityW; ++x) {
            const int saved = st.city.coverage[y][x];
            const int ours = sim.coverage[y][x];
            if (saved) ++saved_nonzero;
            if (ours) ++sim_nonzero;
            if (saved == ours) {
                ++match;
                ++band_match[y / 25];
                continue;
            }
            (saved > ours ? saved_higher : sim_higher) += 1;
            ++mismatch_by_tile[st.city.tile[y][x]];
            if (examples.size() < 10) {
                char line[96];
                std::snprintf(line, sizeof line, "    (row %2d, col %2d) tile 0x%02X  saved %3d  sim %3d", y, x,
                              st.city.tile[y][x], saved, ours);
                examples.push_back(line);
            }
        }
    }
    std::printf("  coverage A2C4: %d/10000 cells match  (nonzero: saved %d, sim %d; saved higher %d, sim higher %d)\n",
                match, saved_nonzero, sim_nonzero, saved_higher, sim_higher);
    std::printf("    by scan quarter: rows 0-24 %d/2500, 25-49 %d/2500, 50-74 %d/2500, 75-99 %d/2500\n", band_match[0],
                band_match[1], band_match[2], band_match[3]);
    if (!mismatch_by_tile.empty()) {
        std::printf("    mismatched cells by the tile they sit on:");
        for (const auto& [tile, n] : mismatch_by_tile) std::printf(" %02X:%d", tile, n);
        std::printf("\n");
        for (const auto& e : examples) std::printf("%s\n", e.c_str());
    }

    // --- C9D4 bits the month-end rebuild sets (01 is water) ---
    std::printf("  C9D4 bits:");
    for (uint8_t bit : {uint8_t{0x01}, uint8_t{0x04}, uint8_t{0x08}, uint8_t{0x20}, uint8_t{0x40}, uint8_t{0x80}}) {
        int bit_match = 0, saved_only = 0, sim_only = 0;
        for (int y = 0; y < model::kCityH; ++y) {
            for (int x = 0; x < model::kCityW; ++x) {
                const bool a = (st.city.service_flags[y][x] & bit) != 0;
                const bool b = (sim.service_flags[y][x] & bit) != 0;
                if (a == b) {
                    ++bit_match;
                } else if (a) {
                    ++saved_only;
                } else {
                    ++sim_only;
                }
            }
        }
        std::printf("  0x%02X %d/10000 (saved-only %d, sim-only %d)", bit, bit_match, saved_only, sim_only);
    }
    std::printf("\n");

    // --- Tiles and 7BB4 the water pass rewrites (fountain states and levels) ---
    int tile_diff = 0, level_diff = 0;
    for (int y = 0; y < model::kCityH; ++y) {
        for (int x = 0; x < model::kCityW; ++x) {
            if (sim.tile[y][x] != st.city.tile[y][x]) {
                ++tile_diff;
                std::printf("    tile (row %2d, col %2d) saved 0x%02X  sim 0x%02X\n", y, x, st.city.tile[y][x],
                            sim.tile[y][x]);
            }
            if (sim.operational_state[y][x] != st.city.operational_state[y][x]) ++level_diff;
        }
    }
    std::printf("  tiles changed by the rebuild: %d, 7BB4 bytes changed: %d\n", tile_diff, level_diff);

    // --- Population ---
    const int units = systems::housing::population_units(st.city);
    std::printf("  population units: computed %d, saved DS:0x6C10 %d (saved population DS:0x6C0E %d)\n", units,
                saved_word(sf, 0x6C10), saved_word(sf, 0x6C0E));
    return 0;
}
