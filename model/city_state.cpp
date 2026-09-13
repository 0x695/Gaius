// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/city_state.hpp"

#include <cstring>

namespace gaius::model {

namespace {

template <typename T>
void copy_grid(const uint8_t* block, CityGrid<T>& out) {
    for (int r = 0; r < kCityH; ++r) std::memcpy(out[r].data(), block + static_cast<size_t>(r) * kCityW, kCityW);
}

template <typename T>
void write_grid(const CityGrid<T>& in, uint8_t* block) {
    for (int r = 0; r < kCityH; ++r) std::memcpy(block + static_cast<size_t>(r) * kCityW, in[r].data(), kCityW);
}

std::vector<uint8_t> copy_block(const formats::save::SaveFile& sf, const std::string& name) {
    auto b = sf.block(name);
    return std::vector<uint8_t>(b.first, b.first + b.second);
}

void write_opaque_block(uint8_t* dst, const std::vector<uint8_t>& data, size_t expected_size, const std::string& name) {
    if (data.size() != expected_size) {
        throw formats::FormatError("model: block '" + name + "' has wrong size for serialize (" +
                                    std::to_string(data.size()) + ", expected " + std::to_string(expected_size) + ")");
    }
    std::memcpy(dst, data.data(), data.size());
}

}  // namespace

CityState load(const formats::save::SaveFile& sf) {
    CityState state;

    copy_grid(sf.block("city_tiles_100x100").first, state.city.tile);
    copy_grid(sf.block("cell_value_a2c4").first, state.city.coverage);
    copy_grid(sf.block("cell_flags_c9d4").first, state.city.service_flags);
    copy_grid(sf.block("cell_flags_7bb4").first, state.city.operational_state);
    copy_grid<int8_t>(sf.block("cell_value_54a4").first, state.city.land_value);

    auto empire_block = sf.block("empire2_1602");
    state.empire.prefix[0] = empire_block.first[0];
    state.empire.prefix[1] = empire_block.first[1];
    std::memcpy(state.empire.cells.data(), empire_block.first + 2,
                formats::empire2::kMapW * formats::empire2::kMapH);

    auto objects_block = sf.block("objects_70x50");
    for (int i = 0; i < kActorCount; ++i) {
        std::memcpy(state.objects[i].raw.data(), objects_block.first + static_cast<size_t>(i) * kActorRecordSize,
                    kActorRecordSize);
    }

    state.global_words_128 = copy_block(sf, "global_words_128");
    state.table_480 = copy_block(sf, "table_480");
    state.table_120 = copy_block(sf, "table_120");
    state.table_720 = copy_block(sf, "table_720");
    state.table_50 = copy_block(sf, "table_50");
    state.table_8 = copy_block(sf, "table_8");
    state.table_10 = copy_block(sf, "table_10");
    state.table_60_a = copy_block(sf, "table_60_a");
    state.table_60_b = copy_block(sf, "table_60_b");
    state.table_60_c = copy_block(sf, "table_60_c");
    state.table_60_d = copy_block(sf, "table_60_d");
    state.table_72 = copy_block(sf, "table_72");
    state.final_state = copy_block(sf, "final_state");

    return state;
}

formats::save::SaveFile serialize(const CityState& state) {
    formats::save::SaveFile sf;
    sf.raw.assign(formats::save::kSaveSize, 0);

    // Walk the confirmed block table so offsets/sizes are never
    // hand-duplicated here -- if that table ever changes, this adapts
    // instead of silently writing to stale offsets.
    for (const auto& b : formats::save::block_table()) {
        uint8_t* dst = sf.raw.data() + b.offset;

        if (b.name == "city_tiles_100x100") {
            write_grid(state.city.tile, dst);
        } else if (b.name == "cell_value_a2c4") {
            write_grid(state.city.coverage, dst);
        } else if (b.name == "cell_flags_c9d4") {
            write_grid(state.city.service_flags, dst);
        } else if (b.name == "cell_flags_7bb4") {
            write_grid(state.city.operational_state, dst);
        } else if (b.name == "cell_value_54a4") {
            write_grid(state.city.land_value, dst);
        } else if (b.name == "empire2_1602") {
            dst[0] = state.empire.prefix[0];
            dst[1] = state.empire.prefix[1];
            std::memcpy(dst + 2, state.empire.cells.data(), formats::empire2::kMapW * formats::empire2::kMapH);
        } else if (b.name == "objects_70x50") {
            for (int i = 0; i < kActorCount; ++i) {
                std::memcpy(dst + static_cast<size_t>(i) * kActorRecordSize, state.objects[i].raw.data(),
                            kActorRecordSize);
            }
        } else if (b.name == "global_words_128") {
            write_opaque_block(dst, state.global_words_128, b.size, b.name);
        } else if (b.name == "table_480") {
            write_opaque_block(dst, state.table_480, b.size, b.name);
        } else if (b.name == "table_120") {
            write_opaque_block(dst, state.table_120, b.size, b.name);
        } else if (b.name == "table_720") {
            write_opaque_block(dst, state.table_720, b.size, b.name);
        } else if (b.name == "table_50") {
            write_opaque_block(dst, state.table_50, b.size, b.name);
        } else if (b.name == "table_8") {
            write_opaque_block(dst, state.table_8, b.size, b.name);
        } else if (b.name == "table_10") {
            write_opaque_block(dst, state.table_10, b.size, b.name);
        } else if (b.name == "table_60_a") {
            write_opaque_block(dst, state.table_60_a, b.size, b.name);
        } else if (b.name == "table_60_b") {
            write_opaque_block(dst, state.table_60_b, b.size, b.name);
        } else if (b.name == "table_60_c") {
            write_opaque_block(dst, state.table_60_c, b.size, b.name);
        } else if (b.name == "table_60_d") {
            write_opaque_block(dst, state.table_60_d, b.size, b.name);
        } else if (b.name == "table_72") {
            write_opaque_block(dst, state.table_72, b.size, b.name);
        } else if (b.name == "final_state") {
            write_opaque_block(dst, state.final_state, b.size, b.name);
        } else {
            throw formats::FormatError("model: serialize() doesn't know how to write block '" + b.name + "'");
        }
    }

    return sf;
}

}  // namespace gaius::model

namespace gaius::model {

namespace {

// final_state's layout, from the save writer (flat 0x042EC-0x04516): six
// words, 12 bytes at DS:0x6B9E, then 22 words.
struct FinalStateWord {
    uint16_t ds;
    size_t offset;
};
constexpr FinalStateWord kFinalStateWords[] = {
    {0x6BB4, 0},  {0x6BB2, 2},  {0x6BB0, 4},  {0x6BAE, 6},  {0x6BAC, 8},  {0x6BAA, 10}, {0x6CB8, 24}, {0x6C02, 26},
    {0x6BFC, 28}, {0x6C98, 30}, {0x6C96, 32}, {0x6C94, 34}, {0x6C90, 36}, {0x6C8E, 38}, {0x6C8C, 40}, {0x6C8A, 42},
    {0x6C88, 44}, {0x6C86, 46}, {0x6C84, 48}, {0x6C82, 50}, {0x6C80, 52}, {0x6C7E, 54}, {0x6C5C, 56}, {0x6BEE, 58},
    {0x6BEC, 60}, {0x6BEA, 62}, {0x6BE8, 64}, {0x6BE6, 66},
};

uint8_t* word_slot(std::vector<uint8_t>& bytes, size_t offset) {
    return offset + 1 < bytes.size() ? &bytes[offset] : nullptr;
}

uint8_t* find_word(CityState& state, uint16_t ds) {
    for (int i = 0; i < 128; ++i) {
        if (formats::save::kGlobalWordDsAddress[i] == ds) return word_slot(state.global_words_128, static_cast<size_t>(i) * 2);
    }
    for (const FinalStateWord& w : kFinalStateWords) {
        if (w.ds == ds) return word_slot(state.final_state, w.offset);
    }
    return nullptr;
}

}  // namespace

int global_word(const CityState& state, uint16_t ds, int fallback) {
    const uint8_t* p = find_word(const_cast<CityState&>(state), ds);
    return p ? static_cast<int16_t>(p[0] | (p[1] << 8)) : fallback;
}

bool set_global_word(CityState& state, uint16_t ds, int value) {
    uint8_t* p = find_word(state, ds);
    if (!p) return false;
    p[0] = static_cast<uint8_t>(value & 0xFF);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    return true;
}

}  // namespace gaius::model
