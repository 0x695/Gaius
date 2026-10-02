// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius CLI tool: save_diff
//
// Compares two CAESARxx.SAV files and says what changed, in the terms the
// findings use rather than as a hex dump:
//   - each saved block: how many bytes differ,
//   - each saved global word that changed, by its DS address and (where the
//     findings name it) its name, with both values,
//   - each of the five city grids: how many cells changed, and with --cells N
//     the first N of them (column, row, old, new),
//   - the actor table: which slots changed.
//
// Usage: save_diff <a.SAV> <b.SAV> [--cells N]
// Exit status: 0 the files are identical, 1 they differ, 2 an error (like diff).

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

#include "formats/save/save.hpp"
#include "model/city_state.hpp"
#include "tools/save_words.hpp"

using namespace gaius;

namespace {

template <typename T>
int diff_grid(const char* name, const model::CityGrid<T>& a, const model::CityGrid<T>& b, int show) {
    int changed = 0, shown = 0;
    for (int y = 0; y < model::kCityH; ++y) {
        for (int x = 0; x < model::kCityW; ++x) {
            if (a[y][x] == b[y][x]) continue;
            ++changed;
            if (shown < show) {
                std::printf("    %s (%d,%d): %d -> %d\n", name, x, y, static_cast<int>(a[y][x]),
                            static_cast<int>(b[y][x]));
                ++shown;
            }
        }
    }
    std::printf("  %-18s %5d of %d cells differ\n", name, changed, model::kCityW * model::kCityH);
    return changed;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <a.SAV> <b.SAV> [--cells N]\n", argv[0]);
        return 2;
    }
    int show = 0;
    for (int i = 3; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--cells") show = std::atoi(argv[i + 1]);

    formats::save::SaveFile fa, fb;
    model::CityState sa, sb;
    try {
        fa = formats::save::load(argv[1]);
        fb = formats::save::load(argv[2]);
        sa = model::load(fa);
        sb = model::load(fb);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "failed to load: %s\n", e.what());
        return 2;
    }

    int total = 0;
    std::printf("a: %s\nb: %s\n\nBlocks:\n", argv[1], argv[2]);
    for (const auto& blk : formats::save::block_table()) {
        int n = 0;
        for (size_t i = blk.offset; i < blk.offset + blk.size; ++i) n += fa.raw[i] != fb.raw[i];
        total += n;
        if (n) std::printf("  %-22s %5d of %zu bytes differ\n", blk.name.c_str(), n, blk.size);
    }
    if (total == 0) std::printf("  (identical)\n");

    std::printf("\nGlobal words:\n");
    int words = 0;
    for (uint32_t ds = 0x6B00; ds < 0x6E00; ds += 2) {
        const int a = model::global_word(sa, static_cast<uint16_t>(ds), INT_MIN);
        const int b = model::global_word(sb, static_cast<uint16_t>(ds), INT_MIN);
        if (a == INT_MIN || a == b) continue;
        const char* name = tools::word_name(static_cast<uint16_t>(ds));
        std::printf("  DS:0x%04X %-32s %6d -> %-6d (%+d)\n", ds, name ? name : "", a, b, b - a);
        ++words;
    }
    if (!words) std::printf("  (none changed)\n");

    std::printf("\nCity grids:\n");
    int cells = 0;
    cells += diff_grid("tile (43A5)", sa.city.tile, sb.city.tile, show);
    cells += diff_grid("land value (A2C4)", sa.city.land_value, sb.city.land_value, show);
    cells += diff_grid("service (C9D4)", sa.city.service_flags, sb.city.service_flags, show);
    cells += diff_grid("state (7BB4)", sa.city.operational_state, sb.city.operational_state, show);
    cells += diff_grid("unrest (54A4)", sa.city.unrest, sb.city.unrest, show);

    std::printf("\nActors:\n");
    int actors = 0;
    for (size_t i = 0; i < sa.objects.size(); ++i) {
        if (sa.objects[i].raw == sb.objects[i].raw) continue;
        ++actors;
        int bytes = 0;
        for (size_t k = 0; k < sa.objects[i].raw.size(); ++k) bytes += sa.objects[i].raw[k] != sb.objects[i].raw[k];
        std::printf("  slot %2zu: type %u -> %u, state %u -> %u (%d of %zu bytes differ)\n", i, sa.objects[i].type(),
                    sb.objects[i].type(), sa.objects[i].state(), sb.objects[i].state(), bytes,
                    sa.objects[i].raw.size());
    }
    if (!actors) std::printf("  (none changed)\n");

    return total == 0 ? 0 : 1;
}
