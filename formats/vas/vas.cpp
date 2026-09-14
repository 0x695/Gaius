// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/vas/vas.hpp"

#include <cstdio>

namespace gaius::formats::vas {

namespace {

uint16_t u16(const std::vector<uint8_t>& d, size_t i) {
    if (i + 2 > d.size()) throw FormatError("vas: read past the end");
    return static_cast<uint16_t>(d[i] | (d[i + 1] << 8));
}

uint32_t u32(const std::vector<uint8_t>& d, size_t i) {
    return static_cast<uint32_t>(u16(d, i)) | (static_cast<uint32_t>(u16(d, i + 2)) << 16);
}

// Walks one plane block starting at `start`; with `plane`, applies it.
// Returns the block's length.
size_t walk_block(const std::vector<uint8_t>& d, size_t start, std::vector<uint8_t>* plane) {
    const size_t length = u16(d, start);
    if (u16(d, start + 2) != kPlaneSize) throw FormatError("vas: a plane block isn't 16000 bytes");
    if (length < 8 || start + length > d.size()) throw FormatError("vas: a plane block runs past the file");
    size_t i = start + 8, count = 0;
    while (count < kPlaneSize) {
        const uint16_t w = u16(d, i);
        i += 2;
        const size_t n = static_cast<size_t>(w & 0x7FFF) + 1;
        if (count + n > kPlaneSize) throw FormatError("vas: a run passes the plane's end");
        if (w & 0x8000) {
            if (i + n > start + length) throw FormatError("vas: XOR bytes pass the block's end");
            if (plane)
                for (size_t k = 0; k < n; ++k) (*plane)[count + k] ^= d[i + k];
            i += n;
        }
        count += n;
    }
    if (i != start + length) throw FormatError("vas: a block's runs don't end at its length");
    return length;
}

}  // namespace

Animation parse(std::vector<uint8_t> data) {
    Animation a;
    a.data = std::move(data);
    // The players count from 2 up to this word, reading the offset at 0x10 + 4 x
    // (count - 1): one frame fewer than the word.
    const size_t word = u16(a.data, 2);
    if (word < 2) throw FormatError("vas: no frames");
    const size_t frames = word - 1;
    for (size_t f = 0; f < frames; ++f) a.frame_offsets.push_back(u32(a.data, 0x14 + 4 * f));
    for (size_t f = 0; f < frames; ++f) {
        size_t at = a.frame_offsets[f];
        for (int p = 0; p < 4; ++p) at += walk_block(a.data, at, nullptr);
        const size_t next = f + 1 < frames ? a.frame_offsets[f + 1] : a.data.size();
        if (at != next) throw FormatError("vas: frame " + std::to_string(f) + " doesn't end where the next begins");
    }
    return a;
}

Animation load(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw FormatError("vas: cannot open " + path);
    std::vector<uint8_t> data;
    uint8_t buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return parse(std::move(data));
}

void apply_frame(const Animation& animation, size_t frame, Planes& planes) {
    if (frame >= animation.frame_offsets.size()) throw FormatError("vas: no such frame");
    size_t at = animation.frame_offsets[frame];
    for (int p = 0; p < 4; ++p) {
        if (planes[static_cast<size_t>(p)].size() != kPlaneSize) throw FormatError("vas: a plane isn't 16000 bytes");
        at += walk_block(animation.data, at, &planes[static_cast<size_t>(p)]);
    }
}

Planes to_planes(const IndexedImage& image) {
    if (image.width != 320 || image.height != 200 || image.pixels.size() != 64000)
        throw FormatError("vas: planes are for a 320x200 image");
    Planes planes;
    for (auto& p : planes) p.assign(kPlaneSize, 0);
    for (size_t i = 0; i < kPlaneSize; ++i)
        for (size_t p = 0; p < 4; ++p) planes[p][i] = image.pixels[i * 4 + p];
    return planes;
}

IndexedImage to_image(const Planes& planes) {
    IndexedImage image;
    image.width = 320;
    image.height = 200;
    image.pixels.assign(64000, 0);
    for (size_t i = 0; i < kPlaneSize; ++i)
        for (size_t p = 0; p < 4; ++p) image.pixels[i * 4 + p] = planes[p].at(i);
    return image;
}

}  // namespace gaius::formats::vas
