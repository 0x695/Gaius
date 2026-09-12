// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius CLI tool: save_inspect
//
// Slice a CAESARxx.SAV file into its confirmed blocks (see
// formats/save/save.hpp) and print offset/size/name for each, plus the
// leading global-words section decoded per CAESAR_SAVE_FORMAT.md
// (128 x 16-bit words, descending DS address from DS:0x6CE2), matching
// caesar_save_layout.py's output for direct cross-checking. Also loads the
// save through model::CityState (Phase 2) and prints the 70-record actor
// table -- see model/city_state.hpp for which fields are confirmed vs.
// still opaque.
//
// Usage:
//   save_inspect <CAESARxx.SAV>

#include <cstdio>
#include <string>

#include "formats/save/save.hpp"
#include "model/city_state.hpp"

using namespace gaius::formats;
using namespace gaius::model;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <CAESARxx.SAV>\n", argv[0]);
        return 1;
    }

    std::string path = argv[1];
    try {
        save::SaveFile sf = save::load(path);
        std::printf("%s: %zu bytes (expected %zu)\n", path.c_str(), sf.raw.size(), save::kSaveSize);

        std::printf("\nConfirmed blocks:\n");
        for (const auto& b : save::block_table()) {
            std::printf("  0x%04zX-0x%04zX  %5zu  %s\n", b.offset, b.offset + b.size - 1, b.size, b.name.c_str());
        }

        std::printf("\nFirst global words (save+0xNNNN -> DS:0xNNNN, per the recovered descending-address mapping):\n");
        auto [block_ptr, block_size] = sf.block("global_words_128");
        for (size_t i = 0; i + 1 < block_size; i += 2) {
            uint16_t ds_addr = static_cast<uint16_t>(0x6CE2 - i);
            uint16_t value = static_cast<uint16_t>(block_ptr[i]) | (static_cast<uint16_t>(block_ptr[i + 1]) << 8);
            std::printf("  save+0x%04zX -> DS:0x%04X = 0x%04X (%u)\n", i, ds_addr, value, value);
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
