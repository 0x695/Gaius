#include "formats/save/save.hpp"

#include <cstdio>

namespace gaius::formats::save {

std::vector<Block> block_table() {
    // Verbatim port of caesar_save_layout.py's BLOCKS table.
    return {
        {0x0000, 0x0100, "global_words_128"},
        {0x0100, 0x0DAC, "objects_70x50"},
        {0x0EAC, 0x01E0, "table_480"},
        {0x108C, 0x0078, "table_120"},
        {0x1104, 0x02D0, "table_720"},
        {0x13D4, 0x2710, "city_tiles_100x100"},
        {0x3AE4, 0x2710, "cell_flags_c9d4"},
        {0x61F4, 0x2710, "cell_value_a2c4"},
        {0x8904, 0x2710, "cell_flags_7bb4"},
        {0xB014, 0x2710, "cell_value_54a4"},
        {0xD724, 0x0642, "empire2_1602"},
        {0xDD66, 0x0032, "table_50"},
        {0xDD98, 0x0008, "table_8"},
        {0xDDA0, 0x000A, "table_10"},
        {0xDDAA, 0x003C, "table_60_a"},
        {0xDDE6, 0x003C, "table_60_b"},
        {0xDE22, 0x003C, "table_60_c"},
        {0xDE5E, 0x003C, "table_60_d"},
        {0xDE9A, 0x0048, "table_72"},
        {0xDEE2, 0x0044, "final_state"},
    };
}

SaveFile load(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw FormatError("save: cannot open " + path);

    std::vector<uint8_t> buf(kSaveSize + 1);
    size_t n = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);

    if (n != kSaveSize) {
        throw FormatError("save: expected exactly " + std::to_string(kSaveSize) +
                           " bytes, got " + std::to_string(n) + " (" + path + ")");
    }
    buf.resize(kSaveSize);

    SaveFile sf;
    sf.raw = std::move(buf);
    return sf;
}

std::pair<const uint8_t*, size_t> SaveFile::block(const std::string& name) const {
    for (const auto& b : block_table()) {
        if (b.name == name) {
            if (b.offset + b.size > raw.size())
                throw FormatError("save: block '" + name + "' runs past end of loaded file");
            return {raw.data() + b.offset, b.size};
        }
    }
    throw FormatError("save: no such block '" + name + "'");
}

}  // namespace gaius::formats::save
