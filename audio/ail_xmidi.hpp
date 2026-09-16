// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — audio/ail_xmidi.hpp
//
// The game's music driver: the Miles Audio Interface Library 2.14's Sound
// Blaster FM driver, SBFM.ADV. Caesar's SBFM.ADV, ADLIB.ADV and SBDIG.ADV are
// byte-identical to the AIL 2.14 release, whose source John Miles released as
// freeware in 2000 (DEFINITIVE, findings section 45). SBFM.ADV is XMIDI.ASM
// with YAMAHA.INC assembled for SBSTD (one YM3812, no stereo); this file
// transcribes the parts of both that a Caesar tune reaches. It produces the
// register writes the driver makes; audio/game_audio sends them to an
// emulated YM3812.
//
// The synthesizer (YAMAHA.INC):
//   - 16 "slots" (virtual voices) share the 9 hardware voices. A note takes the
//     first free slot; the slot takes the next free voice after the last one
//     given (a rover), or, with none free, update_priority robs the voiced
//     slot of lowest priority -- 32767 less the voices its channel holds, or
//     0xFFFF under voice protection -- which frees that note.
//   - Notes play on MIDI channels 2-10 only (1-based). Channel 10 is the
//     rhythm channel: its timbre is bank 127, patch = key, and the timbre's
//     transpose byte is the note played. Other channels use the program and
//     bank (controller 114) of the channel, transposed by the timbre's byte.
//   - The carrier's level (and the modulator's, for additive timbres) is the
//     timbre's scaled by volume x expression x velocity, where the velocity
//     goes through a 16-step table that narrows its range (vel_graph).
//   - Pitch: the note in 1/16 semitones plus the bend at a fixed range of 12
//     semitones, looked up in a 192-entry F-number table. Modulation >= 64
//     sets the vibrato bit.
// The sequencer (XMIDI.ASM), called 120 times a second (serve):
//   - first every queued note's duration counts down, and those below zero
//     get their note-off;
//   - then, when the interval count runs out, events play until the next
//     interval byte. A note-on queues its duration (32 notes; a full queue
//     overwrites entry 0). Controllers 116/117 loop, others go to the
//     synthesizer. The end-of-track event resets the sequence (sustain and
//     voice protection released) and marks it done, leaving queued notes on.
//
// Not transcribed, because no Caesar tune uses them (every .XMI scanned:
// controllers 7 and 10 only, channels 2-10) and the game never calls them:
// channel locking and protection (110, 111), indirect controllers (115),
// callbacks (119), branches, the tempo and volume ramps, the beat and bar
// counts, OPL3 and TVFX timbres. The timbre cache holds 192 timbres in 3584
// bytes; SAMPLE.AD has 162 of 14 bytes, so the least-recently-used eviction
// can't happen and isn't transcribed either.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace gaius::audio {

class AilXmidi {
public:
    using RegisterWrite = std::function<void(uint8_t reg, uint8_t value)>;

    enum Status { kStopped = 0, kPlaying = 1, kDone = 2 };  // AIL's SEQ_ values

    explicit AilXmidi(RegisterWrite write);

    // init_driver: reset_synth (every register from array0_init), init_synth,
    // and the default controllers, pitch and programs on channels 2-10.
    void init();

    // The timbre cache. `data` is the timbre as the library stores it, its
    // u16 length first. install_timbre also points every channel whose program
    // and bank match at it.
    void install_timbre(uint8_t bank, uint8_t patch, const std::vector<uint8_t>& data);
    bool timbre_installed(uint8_t bank, uint8_t patch) const;

    // register_seq: sequence `number` of an XMIDI file. False if there is none.
    bool register_sequence(const std::vector<uint8_t>& xmi, int number = 0);
    // get_request: the first timbre the sequence's TIMB chunk lists that isn't
    // installed, as (bank << 8) | patch; -1 when all are.
    int request() const;
    void start();
    void stop();     // stop_seq: only a playing sequence; its queued notes end
    void release();  // release_seq
    Status status() const { return registered_ ? status_ : kStopped; }
    bool registered() const { return registered_; }

    // serve_driver: one 1/120 s interval.
    void serve();

    // send_MIDI_message: one channel voice message to the synthesizer.
    void send(uint8_t status, uint8_t d1, uint8_t d2);

    static constexpr int kServiceRate = 120;  // QUANT_RATE

    // For tests: the hardware voice a slot holds (-1 none), its MIDI channel,
    // and whether the slot is free.
    int slot_voice(int slot) const { return S_voice[static_cast<size_t>(slot)]; }
    bool slot_free(int slot) const { return S_status[static_cast<size_t>(slot)] == kFree; }
    int queued_notes() const { return note_count; }

private:
    static constexpr int kVoices = 9, kSlots = 16, kChans = 16, kMaxTimbs = 192, kMaxNotes = 32, kForNest = 4;
    enum { kFree = 0, kKeyOn = 1, kKeyOff = 2 };

    void update_reg(uint8_t reg, uint8_t value) { write_(reg, value); }
    void write_register(int op_cell, uint8_t base, uint8_t value);
    void send_byte(int voice, uint8_t base, uint8_t value);

    int index_timbre(uint8_t bank, uint8_t patch) const;
    void assign_voice(int slot);
    void release_voice(int slot);
    void update_voice(int slot);
    void update_priority();
    void bnk_phase(int slot);
    void note_off(int chan, int note);
    void note_on(int chan, int note, int velocity);
    void release_sustain(int chan);

    void rewind();
    void reset_sequence();
    void flush_note_queue();
    int control(int chan, int con, int val);  // XMIDI_control
    size_t note_on_event();                   // XMIDI_note_on: the event's length

    RegisterWrite write_;

    // YAMAHA.INC's tables and state.
    struct Timbre {
        bool in_use = false;
        uint8_t bank = 0, patch = 0;
        std::vector<uint8_t> data;
    };
    std::array<Timbre, kMaxTimbs> timbres_{};
    std::array<int, kSlots> S_timbre{};  // index into timbres_
    std::array<int, kSlots> S_status{}, S_voice{}, S_channel{}, S_note{}, S_keynum{}, S_transpose{}, S_velocity{},
        S_sustain{}, S_update{}, S_KBF_shadow{}, S_BLOCK{}, S_FBC{}, S_KSLTL_0{}, S_KSLTL_1{}, S_AVEKM_0{},
        S_AVEKM_1{}, S_AD_0{}, S_AD_1{}, S_SR_0{}, S_SR_1{}, S_scale_01{};
    std::array<uint16_t, kSlots> S_ws_val{}, S_m1_val{}, S_m0_val{}, S_fb_val{}, S_p_val{}, S_v1_val{}, S_v0_val{},
        S_V_priority{};
    std::array<uint8_t, kChans> MIDI_vol{}, MIDI_pan{}, MIDI_pitch_l{}, MIDI_pitch_h{}, MIDI_express{}, MIDI_mod{},
        MIDI_sus{}, MIDI_vprot{}, MIDI_voices{};
    std::array<int, kChans> MIDI_timbre{}, MIDI_bank{}, MIDI_program{};
    std::array<int, kVoices> V_channel{};
    int rover_2op = -1;

    // XMIDI.ASM's sequence state (Caesar plays one sequence at a time).
    bool registered_ = false;
    Status status_ = kStopped;
    std::vector<uint8_t> evnt_;             // the EVNT chunk's events
    std::vector<std::pair<uint8_t, uint8_t>> timb_;  // TIMB: (patch, bank)
    size_t ptr_ = 0;
    int interval_cnt = 0;
    int note_count = 0;
    std::array<size_t, kForNest> for_ptrs{};
    std::array<int, kForNest> for_loop_cnt{};
    std::array<int, kChans> chan_sus{}, chan_vprot{};  // chan_controls.SUS / V_PROT, -1 unset
    std::array<int, kMaxNotes> note_chan{}, note_num{};
    std::array<int32_t, kMaxNotes> note_time{};
};

}  // namespace gaius::audio
