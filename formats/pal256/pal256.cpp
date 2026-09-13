// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/pal256/pal256.hpp"

#include <cstdio>
#include <vector>

namespace gaius::formats::pal256 {

namespace {
uint8_t expand6(uint8_t v6) {
    return static_cast<uint8_t>((v6 << 2) | (v6 >> 4));
}
}  // namespace

Palette load(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw FormatError("pal256: cannot open " + path);

    std::vector<uint8_t> buf(768);
    size_t n = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);

    if (n != 768) {
        throw FormatError("pal256: expected exactly 768 bytes, got " + std::to_string(n) +
                           " (" + path + ")");
    }

    Palette pal;
    for (int i = 0; i < 256; ++i) {
        for (int c = 0; c < 3; ++c) {
            if (buf[i * 3 + c] > 63) {
                throw FormatError("pal256: color " + std::to_string(i) + " has channel value " +
                                  std::to_string(buf[i * 3 + c]) + ", above the 6-bit DAC range (" + path + ")");
            }
        }
        pal.colors[i] = RGB{expand6(buf[i * 3 + 0]), expand6(buf[i * 3 + 1]), expand6(buf[i * 3 + 2])};
    }
    pal.filled_count = 256;
    return pal;
}

}  // namespace gaius::formats::pal256
