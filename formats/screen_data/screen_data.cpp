// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/screen_data/screen_data.hpp"

#include <cstdio>

namespace gaius::formats::screen_data {

namespace {

std::vector<uint8_t> read_all(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw FormatError("screen_data: cannot open " + path);
    std::vector<uint8_t> data;
    uint8_t buf[1024];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return data;
}

}  // namespace

ClickMap load_click_map(const std::string& path) {
    const std::vector<uint8_t> d = read_all(path);
    if (d.size() != kGridW * kGridH) throw FormatError("screen_data: CONTFRM.GD8 is 1000 bytes, not " + std::to_string(d.size()));
    ClickMap map;
    for (size_t i = 0; i < d.size(); ++i) {
        if (d[i] > 8) throw FormatError("screen_data: a click region above 8");
        map.cells[i] = d[i];
    }
    return map;
}

std::array<Marker, 50> load_province_markers(const std::string& path) {
    const std::vector<uint8_t> d = read_all(path);
    if (d.size() < 200) throw FormatError("screen_data: EDATA.CSR is shorter than 200 bytes");
    std::array<Marker, 50> markers{};
    for (size_t p = 0; p < 50; ++p) {
        const int x = (d[4 * p] << 8) | d[4 * p + 1];
        const int y = (d[4 * p + 2] << 8) | d[4 * p + 3];
        markers[p] = {x - 8 + (p == 39 ? 10 : 0), y - 32};
    }
    return markers;
}

}  // namespace gaius::formats::screen_data
