// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — tools/render_audio.cpp
//
// Plays a tune or an effect the way the game does -- audio::GameAudio, the
// AIL driver on an emulated YM3812, the Sound Blaster's digital channel --
// and writes what comes out to a WAV file (mono, 16-bit).
//
//   render_audio <game folder> <NAME.XMI | effect number 0-19> <out.wav> [rate]
//
// It stops when the tune or effect has finished and a second of silence
// has followed.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "audio/game_audio.hpp"

namespace {

void put16(std::FILE* f, int v) {
    std::fputc(v & 0xFF, f);
    std::fputc((v >> 8) & 0xFF, f);
}

void put32(std::FILE* f, uint32_t v) {
    put16(f, static_cast<int>(v & 0xFFFF));
    put16(f, static_cast<int>(v >> 16));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: render_audio <game folder> <NAME.XMI | effect 0-19> <out.wav> [rate]\n");
        return 2;
    }
    const std::string what = argv[2];
    const int rate = argc > 4 ? std::atoi(argv[4]) : 44100;
    gaius::audio::GameAudio audio;
    const bool music = what.find('.') != std::string::npos;
    if (!audio.load(argv[1]) && music) {
        std::fprintf(stderr, "no SAMPLE.AD in %s\n", argv[1]);
        return 1;
    }
    if (music) {
        audio.play_music(what, true);
        if (!audio.music_playing()) {
            std::fprintf(stderr, "%s didn't start\n", what.c_str());
            return 1;
        }
    } else {
        audio.play_effect(std::atoi(what.c_str()), true);
        if (!audio.effect_playing()) {
            std::fprintf(stderr, "effect %s didn't start\n", what.c_str());
            return 1;
        }
    }
    std::vector<int16_t> samples;
    std::vector<int16_t> block(static_cast<size_t>(rate / 100));
    int quiet_blocks = 0;
    const size_t limit = static_cast<size_t>(rate) * 600;
    while (samples.size() < limit) {
        audio.render(block.data(), block.size(), rate);
        samples.insert(samples.end(), block.begin(), block.end());
        const bool busy = audio.music_playing() || audio.effect_playing();
        quiet_blocks = busy ? 0 : quiet_blocks + 1;
        if (quiet_blocks >= 100) break;
    }
    std::FILE* f = std::fopen(argv[3], "wb");
    if (!f) {
        std::fprintf(stderr, "can't write %s\n", argv[3]);
        return 1;
    }
    const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * 2);
    std::fwrite("RIFF", 1, 4, f);
    put32(f, 36 + data_bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 1);
    put32(f, static_cast<uint32_t>(rate));
    put32(f, static_cast<uint32_t>(rate * 2));
    put16(f, 2);
    put16(f, 16);
    std::fwrite("data", 1, 4, f);
    put32(f, data_bytes);
    for (int16_t s : samples) put16(f, s);
    std::fclose(f);
    std::printf("%s: %.1f s\n", argv[3], static_cast<double>(samples.size()) / rate);
    return 0;
}
