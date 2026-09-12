// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/empire2/empire2.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace gaius::formats::empire2 {

EmpireMap load(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw FormatError("empire2: cannot open " + path);

    std::vector<uint8_t> buf(kFileSize + 1);  // +1 so a too-long file still shows up in the read count
    size_t n = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);

    if (n != kFileSize) {
        throw FormatError("empire2: expected exactly " + std::to_string(kFileSize) +
                           " bytes, got " + std::to_string(n) + " (" + path + ")");
    }

    EmpireMap map;
    map.prefix[0] = buf[0];
    map.prefix[1] = buf[1];
    std::copy(buf.begin() + 2, buf.begin() + 2 + kMapW * kMapH, map.cells.begin());
    return map;
}

void save(const EmpireMap& map, const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw FormatError("empire2: cannot create " + path);

    std::fwrite(map.prefix.data(), 1, map.prefix.size(), f);
    std::fwrite(map.cells.data(), 1, map.cells.size(), f);
    std::fclose(f);
}

}  // namespace gaius::formats::empire2
