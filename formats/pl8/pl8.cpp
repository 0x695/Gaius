// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/pl8/pl8.hpp"

#include <cstdio>
#include <vector>

namespace gaius::formats::pl8 {

namespace {
uint16_t read_u16le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
uint16_t read_u16be(const uint8_t* p) {
    return static_cast<uint16_t>(p[1]) | (static_cast<uint16_t>(p[0]) << 8);
}
}  // namespace

PL8Sheet load(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw FormatError("pl8: cannot open " + path);

    std::fseek(f, 0, SEEK_END);
    long size_l = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size_l < 4) {
        std::fclose(f);
        throw FormatError("pl8: file too small for header: " + path);
    }
    size_t size = static_cast<size_t>(size_l);

    std::vector<uint8_t> data(size);
    size_t n = std::fread(data.data(), 1, size, f);
    std::fclose(f);
    if (n != size) throw FormatError("pl8: short read on " + path);

    // header[0] = unknown_a (meaning not yet established), header[1] = frame_count.
    uint16_t frame_count = read_u16le(&data[2]);

    const size_t desc_table_start = 4;
    const size_t desc_stride = 8;

    if (desc_table_start + static_cast<size_t>(frame_count) * desc_stride > size) {
        throw FormatError("pl8: descriptor table (" + std::to_string(frame_count) +
                           " frames) runs past end of file: " + path);
    }

    PL8Sheet sheet;
    sheet.frames.reserve(frame_count);

    for (uint16_t i = 0; i < frame_count; ++i) {
        const uint8_t* d = &data[desc_table_start + static_cast<size_t>(i) * desc_stride];

        PL8Frame frame;
        uint16_t pixel_offset = read_u16be(d);
        frame.width = d[2];
        frame.height = d[3];
        frame.x = read_u16le(d + 4);
        frame.y = read_u16le(d + 6);

        size_t pixel_count = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);

        if (static_cast<size_t>(pixel_offset) > size) {
            throw FormatError("pl8: frame " + std::to_string(i) +
                               " pixel_offset itself is past end of file in " + path);
        }

        size_t available = size - pixel_offset;
        if (available < pixel_count) {
            // Observed pattern (HOUSES.PL8 frames 44-49): a run of trailing
            // descriptors whose pixel_offset sits EXACTLY at end-of-file
            // (available == 0) with a nonzero declared width/height that
            // was clearly never meant to be dereferenced -- an empty /
            // unused placeholder slot, not file corruption. Any other
            // "points inside the file but the declared size overruns it"
            // case is treated as a genuine error rather than silently
            // guessed around.
            if (available == 0) {
                frame.pixels.clear();
            } else {
                throw FormatError("pl8: frame " + std::to_string(i) +
                                   " pixel data runs past end of file in " + path +
                                   " (offset=" + std::to_string(pixel_offset) +
                                   ", needs " + std::to_string(pixel_count) +
                                   ", only " + std::to_string(available) + " bytes available)");
            }
        } else {
            frame.pixels.assign(&data[pixel_offset], &data[pixel_offset] + pixel_count);
        }

        sheet.frames.push_back(std::move(frame));
    }

    return sheet;
}

}  // namespace gaius::formats::pl8
