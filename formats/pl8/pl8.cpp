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

namespace {

PL8Sheet load_impl(const std::string& path, bool one_bit) {
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
        const size_t row_bytes = (static_cast<size_t>(frame.width) + 7) / 8;
        const size_t stored_count = one_bit ? row_bytes * static_cast<size_t>(frame.height) : pixel_count;

        if (static_cast<size_t>(pixel_offset) > size) {
            throw FormatError("pl8: frame " + std::to_string(i) +
                               " pixel_offset itself is past end of file in " + path);
        }

        size_t available = size - pixel_offset;
        if (available < stored_count) {
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
                                   ", needs " + std::to_string(stored_count) +
                                   ", only " + std::to_string(available) + " bytes available)");
            }
        } else if (one_bit) {
            const uint8_t* stored = &data[pixel_offset];
            frame.pixels.resize(pixel_count);
            for (int y = 0; y < frame.height; ++y) {
                for (int x = 0; x < frame.width; ++x) {
                    const uint8_t byte = stored[static_cast<size_t>(y) * row_bytes + static_cast<size_t>(x) / 8];
                    frame.pixels[static_cast<size_t>(y) * frame.width + x] = (byte >> (7 - x % 8)) & 1;
                }
            }
        } else {
            // Stored as four streams, one after another; pixel i (row-major)
            // is entry i/4 of stream i%4 -- the same interleave as .VPX. Each
            // stream is one plane of unchained VGA (every 4th column), so
            // the width must divide by 4.
            if (frame.width % 4 != 0) {
                throw FormatError("pl8: frame " + std::to_string(i) + " is " + std::to_string(frame.width) +
                                   " pixels wide, not a multiple of 4, so its four streams can't be split (" + path +
                                   ")");
            }
            const uint8_t* stored = &data[pixel_offset];
            const size_t stream_len = pixel_count / 4;
            frame.pixels.resize(pixel_count);
            for (size_t p = 0; p < pixel_count; ++p) frame.pixels[p] = stored[(p % 4) * stream_len + p / 4];
        }

        sheet.frames.push_back(std::move(frame));
    }

    return sheet;
}

}  // namespace

PL8Sheet load(const std::string& path) { return load_impl(path, false); }

PL8Sheet load_pl1(const std::string& path) { return load_impl(path, true); }

}  // namespace gaius::formats::pl8
