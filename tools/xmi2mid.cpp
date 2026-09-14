// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius CLI tool: xmi2mid
//
// Converts a Miles .XMI/.XM2 music file to a Standard MIDI File any player
// reads (formats/xmi/xmi.hpp).
//
// Usage:
//   xmi2mid <input.xmi> <output.mid> [sequence]

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "formats/xmi/xmi.hpp"

using namespace gaius::formats;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <input.xmi> <output.mid> [sequence]\n", argv[0]);
        return 1;
    }
    try {
        const std::vector<uint8_t> xmi = xmi::read_file(argv[1]);
        const size_t sequence = argc > 3 ? static_cast<size_t>(std::atoi(argv[3])) : 0;
        const std::vector<uint8_t> mid = xmi::to_midi(xmi, sequence);
        std::FILE* f = std::fopen(argv[2], "wb");
        if (!f || std::fwrite(mid.data(), 1, mid.size(), f) != mid.size()) {
            if (f) std::fclose(f);
            std::fprintf(stderr, "error: failed to write %s\n", argv[2]);
            return 1;
        }
        std::fclose(f);
        std::printf("%s: sequence %zu of %zu -> %s (%zu bytes)\n", argv[1], sequence, xmi::sequence_count(xmi), argv[2],
                    mid.size());
        return 0;
    } catch (const FormatError& e) {
        std::fprintf(stderr, "format error: %s\n", e.what());
        return 2;
    }
}
