// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — audio/game_audio.hpp
//
// The game's sound layer, segment 31E0 of CSR.EXE (findings section 45), on
// the hardware Gaius emulates: a Sound Blaster, CAESAR.INF's device 3 --
// SBFM.ADV for the music, SBDIG.ADV for the effects. Pure C++, no SDL: the
// caller asks for samples (render) from its audio thread and serializes the
// other calls against it.
//
// What the game does, and this does:
//   - Music (31E0:0430, 0x32230): only while "Allow tunes" (DS:0x5298) is on.
//     It stops the music and the effect playing, loads the .XMI, registers
//     sequence 0, installs every timbre the driver requests from SAMPLE.AD --
//     one the library doesn't have leaves the tune unstarted -- and starts
//     it. A tune plays once; nothing loops. Turning tunes off stops the music
//     (0x0F420); turning them on doesn't restart it.
//   - Effects (31E0:0634, 0x32434): only while "Allow effects" (DS:0x5296)
//     is on. Effect n is a file from a 20-entry table (kEffectFiles); playing
//     one stops the music and any effect playing -- one sound at a time --
//     and, on a Sound Blaster, first halves every sample's distance from 128
//     (0x325D1). The effect plays at the file's rate.
//   - City sounds (0x0FFD4): systems::sounds::city_sound_effect.
// The driver is serviced 120 times a second of rendered time; the chip runs at
// its own rate (3579545 / 72 Hz) and the output is resampled to the caller's.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio/ail_xmidi.hpp"

namespace gaius::audio {

// 0x32434's table: the effect numbers the game plays and their files.
inline constexpr const char* kEffectFiles[20] = {
    "RESPONSE.VOC",  // 0  a construction tool chosen (0x0FFA5)
    "RESPONSE.VOC",  // 1  (0x0FFC9, never called)
    "DEMOL.VOC",     // 2  a building demolished or collapsing (0x124F8)
    "FIRE.VOC",      // 3  a building on fire (0x126BA)
    "WATER.VOC",     // 4  a fountain running dry (0x2C93F)
    "WAR_CRY.VOC",   // 5  invaders entering (0x2D891), a rioter (0x2DC30), the battle's messages
    "FANFARE.VOC",   // 6  a message shown (0x279AC), the year 0 banner (0x278A8)
    "SWORD.VOC",     // 7  the battle's messages (0x22497)
    "MKT_THEA.VOC",  // 8  city sounds: a theatre in view
    "COLISEUM.VOC",  // 9  city sounds: a coliseum
    "HORSES.VOC",    // 10 city sounds: a hippodrome
    "FOUNTAIN.VOC",  // 11 city sounds: a working fountain
    "MARCHROM.VOC",  // 12 city sounds: walkers of type 3-4
    "MARCHBAR.VOC",  // 13 city sounds: walkers of type 5-7 (invaders)
    "HUBBUB.VOC",    // 14 city sounds: a forum, a market
    "PUTOUT.VOC",    // 15 clearing a burning cell (0x12B79)
    "BARBHORN.VOC",  // 16 a Cohort meeting an army (0x24FF7)
    "WALLCOLL.VOC",  // 17 a province road worn away (0x2E15E)
    "WINCHEER.VOC",  // 18 the battle won or lost (0x22D5D, 0x22EDB)
    "WORKSHOP.VOC",  // 19 city sounds: a workshop
};
inline constexpr int kEffectCount = 20;

class GameAudio {
public:
    GameAudio();
    ~GameAudio();

    // The game's folder: SAMPLE.AD, the .XMI and .VOC files. False without
    // SAMPLE.AD (the music driver doesn't load); effects still play.
    bool load(const std::string& game_dir);

    // 31E0:0430 and 31E0:0634. `name` is a file in the game's folder.
    void play_music(const std::string& name, bool tunes_on);
    void play_effect(int effect, bool effects_on);
    // 0x3219D: stop and release the tune.
    void stop_music();
    // 0x32200: stop the effect.
    void stop_effect();
    // Stop everything, as leaving the game does (0x32164).
    void stop_all();

    bool music_playing() const;
    bool effect_playing() const { return effect_pos_ < effect_.size(); }
    const std::string& music_name() const { return music_name_; }

    // Mono 16-bit samples at `rate`.
    void render(int16_t* out, size_t frames, int rate);

    static constexpr int kChipClock = 3579545;

private:
    struct Chip;
    std::unique_ptr<Chip> chip_;
    AilXmidi driver_;
    std::string dir_;
    std::vector<std::vector<uint8_t>> library_;  // SAMPLE.AD's timbres: patch, bank, then the timbre
    bool driver_loaded_ = false;
    std::string music_name_;

    std::vector<uint8_t> effect_;  // 8-bit unsigned samples
    int effect_rate_ = 0;
    double effect_pos_ = 0;

    // Time in chip samples, the driver's service and the resampler.
    double chip_phase_ = 0;
    double service_accum_ = 0;
    int32_t chip_prev_ = 0, chip_next_ = 0;
};

// 0x325D1 on one sample: its distance from 128 halved.
uint8_t halve_sample(uint8_t s);

}  // namespace gaius::audio
