// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius CLI tool: save_inspect
//
// Slice a CAESARxx.SAV file into its confirmed blocks (see
// formats/save/save.hpp) and print offset/size/name for each, plus the
// leading global-words section decoded per CAESAR_SAVE_FORMAT.md
// (128 x 16-bit words, the save writer's own DS-address order, which is NOT a simple descending
// range -- see formats/save/save.hpp kGlobalWordDsAddress), matching
// caesar_save_layout.py's output for direct cross-checking. Also loads the
// save through model::CityState (Phase 2) and prints the 70-record actor
// table -- see model/city_state.hpp for which fields are confirmed vs.
// still opaque.
//
// Usage:
//   save_inspect <CAESARxx.SAV>             everything: blocks, words, actors
//   save_inspect --summary <CAESARxx.SAV>   one "key: value" line per fact (the
//                                           governor, the date, funds, ratings,
//                                           the Legion), for scripts

#include <cstdio>
#include <exception>
#include <string>

#include "formats/save/save.hpp"
#include "model/city_state.hpp"
#include "tools/save_words.hpp"

using namespace gaius::formats;
using namespace gaius::model;

namespace {

// final_state bytes 12-23: the governor's name, padded with spaces
// (ui::governor_name; "  Octavian  " in every real save).
std::string governor(const CityState& state) {
    std::string name;
    for (size_t i = 12; i < 24 && i < state.final_state.size(); ++i) name += static_cast<char>(state.final_state[i]);
    const size_t a = name.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    return name.substr(a, name.find_last_not_of(' ') - a + 1);
}

// The words that describe the city; the rest are view and timer state.
bool in_summary(uint16_t ds) {
    if (ds == 0x6C1C || ds == 0x6C32 || ds == 0x6C30 || ds == 0x6CB8) return false;  // printed first, by name
    if (ds >= 0x6C66 && ds <= 0x6C7C) return false;
    if (ds >= 0x6CAE && ds <= 0x6CB6) return false;
    return ds != 0x6CE2 && ds != 0x6BE6 && ds != 0x6BE8;
}

int summary(const std::string& path) {
    const CityState state = load(save::load(path));
    const auto word = [&](uint16_t ds) { return global_word(state, ds); };
    std::printf("file: %s\n", path.c_str());
    std::printf("governor: %s\n", governor(state).c_str());
    std::printf("rank: %d\n", word(0x6C30));
    std::printf("rank_title: %s\n", gaius::tools::rank_title(word(0x6C30)).c_str());
    std::printf("difficulty: %d\n", word(0x6CB8));
    std::printf("year: %d\n", word(0x6C32));
    std::printf("month: %d\n", word(0x6C1C));
    int houses = 0;
    for (int y = 0; y < kCityH; ++y)
        for (int x = 0; x < kCityW; ++x) houses += state.city.tile[y][x] >= 0xC8 && state.city.tile[y][x] <= 0xD7;
    std::printf("house tiles: %d\n", houses);
    for (const gaius::tools::WordName& w : gaius::tools::kWordNames)
        if (in_summary(w.ds)) std::printf("%s: %d\n", w.name, word(w.ds));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s [--summary] <CAESARxx.SAV>\n", argv[0]);
        return 1;
    }

    if (std::string(argv[1]) == "--summary") {
        if (argc < 3) {
            std::fprintf(stderr, "usage: %s --summary <CAESARxx.SAV>\n", argv[0]);
            return 1;
        }
        try {
            return summary(argv[2]);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s: %s\n", argv[2], e.what());
            return 2;
        }
    }

    std::string path = argv[1];
    try {
        save::SaveFile sf = save::load(path);
        std::printf("%s: %zu bytes (expected %zu)\n", path.c_str(), sf.raw.size(), save::kSaveSize);

        std::printf("\nConfirmed blocks:\n");
        for (const auto& b : save::block_table()) {
            std::printf("  0x%04zX-0x%04zX  %5zu  %s\n", b.offset, b.offset + b.size - 1, b.size, b.name.c_str());
        }

        std::printf("\nFirst global words (save+0xNNNN -> DS:0xNNNN, per the save writer's address order):\n");
        auto [block_ptr, block_size] = sf.block("global_words_128");
        for (size_t i = 0; i + 1 < block_size; i += 2) {
            uint16_t ds_addr = gaius::formats::save::kGlobalWordDsAddress[i / 2];
            uint16_t value = static_cast<uint16_t>(block_ptr[i]) | (static_cast<uint16_t>(block_ptr[i + 1]) << 8);
            const char* name = gaius::tools::word_name(ds_addr);
            std::printf("  save+0x%04zX -> DS:0x%04X = 0x%04X (%u)%s%s\n", i, ds_addr, value, value, name ? "  " : "",
                        name ? name : "");
        }

        // Actor table (Phase 2, model::CityState) -- only the fields
        // confirmed in CAESAR_CITY_STATE_v9.md are printed by name;
        // everything else in each 50-byte record is still opaque (see
        // model/city_state.hpp). "active" rows only, to keep this
        // readable -- most of the 70 slots are typically unused.
        CityState state = load(sf);
        std::printf("\nActor table (active records only; %d slots total, type<11=city coords, type>=11=province):\n",
                    kActorCount);
        int active_count = 0;
        for (int i = 0; i < kActorCount; ++i) {
            const Actor& a = state.objects[i];
            if (a.active() == 0) continue;
            ++active_count;
            std::printf("  [%2d] type=%-3u %-8s state=%-3u screen=(%u,%u) raw=(%u,%u) packed_xy=%u\n", i, a.type(),
                        a.coord_space() == ActorCoordSpace::City ? "city" : "province", a.state(), a.screen_x(),
                        a.screen_y(), a.raw_x(), a.raw_y(), a.packed_xy());
        }
        std::printf("  (%d/%d slots active)\n", active_count, kActorCount);
        return 0;
    } catch (const FormatError& e) {
        std::fprintf(stderr, "format error: %s\n", e.what());
        return 2;
    }
}
