// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/p32/p32.hpp"

#include <cstdio>
#include <vector>

namespace gaius::formats::p32 {

namespace {
uint8_t expand4(uint8_t nibble) {
    // 0x0 -> 0, 0xF -> 255, per the recovered expansion rule.
    return static_cast<uint8_t>(nibble * 17);
}
}  // namespace

Palette load(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw FormatError("p32: cannot open " + path);

    std::vector<uint8_t> buf(64);
    size_t n = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);

    if (n != 64) {
        throw FormatError("p32: expected exactly 64 bytes, got " + std::to_string(n) +
                           " (" + path + ")");
    }

    Palette pal;
    for (int i = 0; i < 32; ++i) {
        uint16_t v = static_cast<uint16_t>(buf[i * 2]) | (static_cast<uint16_t>(buf[i * 2 + 1]) << 8);
        uint8_t r_nibble = v & 0x000F;
        uint8_t b_nibble = (v >> 8) & 0x000F;
        uint8_t g_nibble = (v >> 12) & 0x000F;
        pal.colors[i] = RGB{expand4(r_nibble), expand4(g_nibble), expand4(b_nibble)};
    }
    pal.filled_count = 32;
    return pal;
}

}  // namespace gaius::formats::p32
