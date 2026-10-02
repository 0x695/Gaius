// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius CLI tool: dump_voc
//
// Converts a .VOC sound effect to a WAV file exactly as stored: 8-bit
// unsigned mono PCM at the file's own sample rate (formats::voc). This is the
// lossless export; render_audio plays an effect the way the game does (the
// Sound Blaster's channel, the volume rules) at a chosen rate.
//
// Usage: dump_voc <in.voc> <out.wav>

#include <cstdint>
#include <cstdio>
#include <exception>

#include "formats/voc/voc.hpp"

namespace {

void put16(std::FILE* f, uint32_t v) {
    std::fputc(static_cast<int>(v & 0xFF), f);
    std::fputc(static_cast<int>((v >> 8) & 0xFF), f);
}

void put32(std::FILE* f, uint32_t v) {
    put16(f, v & 0xFFFF);
    put16(f, v >> 16);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <in.voc> <out.wav>\n", argv[0]);
        return 2;
    }
    try {
        const auto sound = gaius::formats::voc::load(argv[1]);
        std::FILE* f = std::fopen(argv[2], "wb");
        if (!f) {
            std::fprintf(stderr, "can't write %s\n", argv[2]);
            return 1;
        }
        const uint32_t bytes = static_cast<uint32_t>(sound.samples.size());
        const uint32_t rate = static_cast<uint32_t>(sound.sample_rate);
        std::fwrite("RIFF", 1, 4, f);
        put32(f, 36 + bytes + (bytes & 1));
        std::fwrite("WAVEfmt ", 1, 8, f);
        put32(f, 16);
        put16(f, 1);  // PCM
        put16(f, 1);  // mono
        put32(f, rate);
        put32(f, rate);  // bytes a second: 8-bit mono
        put16(f, 1);
        put16(f, 8);
        std::fwrite("data", 1, 4, f);
        put32(f, bytes);
        std::fwrite(sound.samples.data(), 1, sound.samples.size(), f);
        if (bytes & 1) std::fputc(0, f);  // RIFF chunks are word-aligned
        std::fclose(f);
        std::printf("%s: %u Hz, %.2f s\n", argv[2], rate, rate ? static_cast<double>(bytes) / rate : 0.0);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s: %s\n", argv[1], e.what());
        return 2;
    }
}
