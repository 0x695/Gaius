// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/ail_xmidi.hpp"

namespace gaius::audio {

namespace {

// YAMAHA.INC's tables.
constexpr uint16_t kFreqTable[192] = {
    0x02b2, 0x02b4, 0x02b7, 0x02b9, 0x02bc, 0x02be, 0x02c1, 0x02c3, 0x02c6, 0x02c9, 0x02cb, 0x02ce, 0x02d0, 0x02d3,
    0x02d6, 0x02d8, 0x02db, 0x02dd, 0x02e0, 0x02e3, 0x02e5, 0x02e8, 0x02eb, 0x02ed, 0x02f0, 0x02f3, 0x02f6, 0x02f8,
    0x02fb, 0x02fe, 0x0301, 0x0303, 0x0306, 0x0309, 0x030c, 0x030f, 0x0311, 0x0314, 0x0317, 0x031a, 0x031d, 0x0320,
    0x0323, 0x0326, 0x0329, 0x032b, 0x032e, 0x0331, 0x0334, 0x0337, 0x033a, 0x033d, 0x0340, 0x0343, 0x0346, 0x0349,
    0x034c, 0x034f, 0x0352, 0x0356, 0x0359, 0x035c, 0x035f, 0x0362, 0x0365, 0x0368, 0x036b, 0x036f, 0x0372, 0x0375,
    0x0378, 0x037b, 0x037f, 0x0382, 0x0385, 0x0388, 0x038c, 0x038f, 0x0392, 0x0395, 0x0399, 0x039c, 0x039f, 0x03a3,
    0x03a6, 0x03a9, 0x03ad, 0x03b0, 0x03b4, 0x03b7, 0x03bb, 0x03be, 0x03c1, 0x03c5, 0x03c8, 0x03cc, 0x03cf, 0x03d3,
    0x03d7, 0x03da, 0x03de, 0x03e1, 0x03e5, 0x03e8, 0x03ec, 0x03f0, 0x03f3, 0x03f7, 0x03fb, 0x03fe, 0xfe01, 0xfe03,
    0xfe05, 0xfe07, 0xfe08, 0xfe0a, 0xfe0c, 0xfe0e, 0xfe10, 0xfe12, 0xfe14, 0xfe16, 0xfe18, 0xfe1a, 0xfe1c, 0xfe1e,
    0xfe20, 0xfe21, 0xfe23, 0xfe25, 0xfe27, 0xfe29, 0xfe2b, 0xfe2d, 0xfe2f, 0xfe31, 0xfe34, 0xfe36, 0xfe38, 0xfe3a,
    0xfe3c, 0xfe3e, 0xfe40, 0xfe42, 0xfe44, 0xfe46, 0xfe48, 0xfe4a, 0xfe4c, 0xfe4f, 0xfe51, 0xfe53, 0xfe55, 0xfe57,
    0xfe59, 0xfe5c, 0xfe5e, 0xfe60, 0xfe62, 0xfe64, 0xfe67, 0xfe69, 0xfe6b, 0xfe6d, 0xfe6f, 0xfe72, 0xfe74, 0xfe76,
    0xfe79, 0xfe7b, 0xfe7d, 0xfe7f, 0xfe82, 0xfe84, 0xfe86, 0xfe89, 0xfe8b, 0xfe8d, 0xfe90, 0xfe92, 0xfe95, 0xfe97,
    0xfe99, 0xfe9c, 0xfe9e, 0xfea1, 0xfea3, 0xfea5, 0xfea8, 0xfeaa, 0xfead, 0xfeaf,
};

constexpr uint8_t kVelGraph[16] = {82, 85, 88, 91, 94, 97, 100, 103, 106, 109, 112, 115, 118, 121, 124, 127};
constexpr uint8_t kOp0[9] = {0, 1, 2, 6, 7, 8, 12, 13, 14};
constexpr uint8_t kOp1[9] = {3, 4, 5, 9, 10, 11, 15, 16, 17};
constexpr uint8_t kOpIndex[18] = {0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 16, 17, 18, 19, 20, 21};

// array0_init: registers 0x01-0xF5 at reset.
uint8_t array0_init(int reg) {
    if (reg == 0x01) return 0x20;
    if (reg == 0x04) return 0x60;
    if (reg >= 0x20 && reg <= 0x35) return 1;
    if (reg >= 0x40 && reg <= 0x55) return 63;
    if (reg >= 0x60 && reg <= 0x75) return 255;
    if (reg >= 0x80 && reg <= 0x95) return 15;
    if (reg == 0xBD) return 0xC0;  // DEF_AV_DEPTH
    return 0;
}

constexpr int kDefPitchRange = 12;
constexpr int U_ALL_REGS = 0xF9, U_AVEKM = 0x80, U_KSLTL = 0x40, U_ADSR = 0x20, U_WS = 0x10, U_FBC = 0x08,
              U_FREQ = 0x01;
constexpr int kMinTrueChan = 2, kMaxRecChan = 10;  // 1-based
constexpr int kModulation = 1, kPartVolume = 7, kPanpot = 10, kExpression = 11, kSustain = 64, kChanLock = 110,
              kChanProtect = 111, kVoiceProtect = 112, kTimbreProtect = 113, kPatchBankSel = 114,
              kIndirect = 115, kForLoop = 116, kNextLoop = 117, kClearBeatBar = 118, kCallbackTrig = 119,
              kResetAllCtrls = 121, kAllNotesOff = 123;

// The +1 unless 0 rounding of update_voice's volume products.
int scale_round(int a, int b) {
    int v = ((a * b) << 1) >> 8 & 0xFF;
    return v == 0 ? 0 : v + 1;
}

uint32_t be32(const std::vector<uint8_t>& d, size_t i) {
    return (static_cast<uint32_t>(d[i]) << 24) | (d[i + 1] << 16) | (d[i + 2] << 8) | d[i + 3];
}

bool tag(const std::vector<uint8_t>& d, size_t i, const char* t) {
    return i + 4 <= d.size() && d[i] == t[0] && d[i + 1] == t[1] && d[i + 2] == t[2] && d[i + 3] == t[3];
}

}  // namespace

AilXmidi::AilXmidi(RegisterWrite write) : write_(std::move(write)) {}

// ---------------------------------------------------------------------------
// YAMAHA.INC

void AilXmidi::write_register(int op_cell, uint8_t base, uint8_t value) {
    update_reg(static_cast<uint8_t>(base + kOpIndex[op_cell]), value);
}

void AilXmidi::send_byte(int voice, uint8_t base, uint8_t value) {
    update_reg(static_cast<uint8_t>(base + voice), value);
}

int AilXmidi::index_timbre(uint8_t bank, uint8_t patch) const {
    for (int i = 0; i < kMaxTimbs; ++i)
        if (timbres_[static_cast<size_t>(i)].in_use && timbres_[static_cast<size_t>(i)].bank == bank &&
            timbres_[static_cast<size_t>(i)].patch == patch)
            return i;
    return -1;
}

bool AilXmidi::timbre_installed(uint8_t bank, uint8_t patch) const { return index_timbre(bank, patch) >= 0; }

void AilXmidi::install_timbre(uint8_t bank, uint8_t patch, const std::vector<uint8_t>& data) {
    int index = index_timbre(bank, patch);
    if (index < 0) {
        if (data.empty()) return;
        for (int i = 0; i < kMaxTimbs && index < 0; ++i)
            if (!timbres_[static_cast<size_t>(i)].in_use) index = i;
        if (index < 0) return;  // (delete_LRU: unreachable with SAMPLE.AD, see the header)
        Timbre& t = timbres_[static_cast<size_t>(index)];
        t.in_use = true;
        t.bank = bank;
        t.patch = patch;
        t.data = data;
    }
    // __set_patch: every channel that asked for this program and bank.
    for (int c = 0; c < kChans; ++c)
        if (MIDI_program[static_cast<size_t>(c)] == patch && MIDI_bank[static_cast<size_t>(c)] == bank)
            MIDI_timbre[static_cast<size_t>(c)] = index;
}

void AilXmidi::assign_voice(int slot) {
    const size_t si = static_cast<size_t>(slot);
    int bx = rover_2op;
    for (int dx = 0; dx < kVoices; ++dx) {
        if (++bx == kVoices) bx = 0;
        rover_2op = bx;
        if (V_channel[static_cast<size_t>(bx)] != -1) continue;
        S_voice[si] = bx;
        ++MIDI_voices[static_cast<size_t>(S_channel[si])];
        V_channel[static_cast<size_t>(bx)] = S_channel[si];
        S_update[si] = U_ALL_REGS;
        update_voice(slot);
        return;
    }
    update_priority();
}

void AilXmidi::release_voice(int slot) {
    const size_t si = static_cast<size_t>(slot);
    if (S_voice[si] == -1) return;
    S_BLOCK[si] &= 0xDF;
    S_update[si] |= U_FREQ;
    update_voice(slot);
    --MIDI_voices[static_cast<size_t>(S_channel[si])];
    V_channel[static_cast<size_t>(S_voice[si])] = -1;
    S_voice[si] = -1;
    S_status[si] = kFree;  // a .BNK slot is released with its voice
}

void AilXmidi::update_voice(int slot) {
    const size_t si = static_cast<size_t>(slot);
    if (S_voice[si] == -1) return;
    const size_t chan = static_cast<size_t>(S_channel[si] & 0x0F);
    int vol = 0;
    if (S_update[si] & U_KSLTL) vol = scale_round(scale_round(MIDI_vol[chan], MIDI_express[chan]), S_velocity[si]);
    const int voice = S_voice[si];
    const int voice0 = kOp0[voice], voice1 = kOp1[voice];

    if (S_update[si] & U_AVEKM) {
        const int vib = MIDI_mod[chan] >= 64 ? 0x40 : 0x00;
        write_register(voice0, 0x20, static_cast<uint8_t>(((S_m0_val[si] >> 12) & 0xFF) | vib | S_AVEKM_0[si]));
        write_register(voice1, 0x20, static_cast<uint8_t>(((S_m1_val[si] >> 12) & 0xFF) | vib | S_AVEKM_1[si]));
        S_update[si] &= ~U_AVEKM;
    }
    if (S_update[si] & U_KSLTL) {
        int bl = (S_v0_val[si] >> 10) & 0xFF;
        if (S_scale_01[si] & 1) bl = (bl * vol / 127) & 0xFF;
        write_register(voice0, 0x40, static_cast<uint8_t>((~bl & 0x3F) | S_KSLTL_0[si]));
        bl = (S_v1_val[si] >> 10) & 0xFF;
        if (S_scale_01[si] & 2) bl = (bl * vol / 127) & 0xFF;
        write_register(voice1, 0x40, static_cast<uint8_t>((~bl & 0x3F) | S_KSLTL_1[si]));
        S_update[si] &= ~U_KSLTL;
    }
    if (S_update[si] & U_ADSR) {
        write_register(voice0, 0x60, static_cast<uint8_t>(S_AD_0[si]));
        write_register(voice1, 0x60, static_cast<uint8_t>(S_AD_1[si]));
        write_register(voice0, 0x80, static_cast<uint8_t>(S_SR_0[si]));
        write_register(voice1, 0x80, static_cast<uint8_t>(S_SR_1[si]));
        S_update[si] &= ~U_ADSR;
    }
    if (S_update[si] & U_WS) {
        write_register(voice1, 0xE0, static_cast<uint8_t>(S_ws_val[si] & 0xFF));
        write_register(voice0, 0xE0, static_cast<uint8_t>(S_ws_val[si] >> 8));
        S_update[si] &= ~U_WS;
    }
    if (S_update[si] & U_FBC) {
        const int ah = (S_fb_val[si] >> 12) & 0x0E;
        send_byte(voice, 0xC0, static_cast<uint8_t>((S_FBC[si] & 1) | ah));
        S_update[si] &= ~U_FBC;
    }
    if (S_update[si] & U_FREQ) {
        if (!(S_BLOCK[si] & 0x20)) {
            send_byte(voice, 0xB0, static_cast<uint8_t>(S_KBF_shadow[si] & 0xDF));
        } else {
            const size_t c = static_cast<size_t>(S_channel[si]);
            int bend = static_cast<int16_t>(static_cast<uint16_t>(((MIDI_pitch_h[c] << 7) | MIDI_pitch_l[c]) - 0x2000));
            bend >>= 5;  // sar
            int ax = static_cast<int16_t>(static_cast<uint16_t>(bend * kDefPitchRange));
            int bx = S_note[si] + static_cast<int8_t>(static_cast<uint8_t>(S_transpose[si]));
            bx -= 24;
            do bx += 12; while (bx < 0);
            bx += 12;
            do bx -= 12; while (bx > 95);
            ax = static_cast<int16_t>(static_cast<uint16_t>(ax + (bx << 8) + 8));
            ax >>= 4;
            ax -= 12 * 16;
            do ax += 12 * 16; while (ax < 0);
            ax += 12 * 16;
            do ax -= 12 * 16; while (ax > 96 * 16 - 1);
            const int note = ax >> 4;
            int freq = static_cast<int16_t>(kFreqTable[(note % 12) * 16 + (ax & 0x0F)]);
            int block = note / 12 - 1;
            if (freq < 0) ++block;
            if (block < 0) {
                ++block;
                freq >>= 1;  // sar
            }
            const int f_num = (freq & 0xFF) | ((((freq >> 8) & 0x03) | (block << 2)) << 8);
            send_byte(voice, 0xA0, static_cast<uint8_t>(f_num & 0xFF));
            const int kbf = ((f_num >> 8) | S_BLOCK[si]) & 0xFF;
            S_KBF_shadow[si] = kbf;
            send_byte(voice, 0xB0, static_cast<uint8_t>(kbf));
        }
        S_update[si] &= ~U_FREQ;
    }
}

void AilXmidi::update_priority() {
    int slot_cnt = 0;
    for (int s = 0; s < kSlots; ++s) {
        const size_t si = static_cast<size_t>(s);
        if (S_status[si] == kFree) continue;
        ++slot_cnt;
        const size_t chan = static_cast<size_t>(S_channel[si] & 0x0F);
        int p = MIDI_vprot[chan] >= 64 ? 0xFFFF : S_p_val[si];
        p -= MIDI_voices[chan];
        S_V_priority[si] = static_cast<uint16_t>(p < 0 ? 0 : p);
    }
    while (true) {
        int ax = 0, dx = 0xFFFF, high_p = -1, low_p = -1;
        for (int s = 0; s < kSlots; ++s) {
            const size_t si = static_cast<size_t>(s);
            if (S_status[si] == kFree) continue;
            const int di = S_V_priority[si];
            if (S_voice[si] == -1) {
                if (di >= ax) {
                    ax = di;
                    high_p = s;
                }
            } else if (di <= dx) {
                dx = di;
                low_p = s;
            }
        }
        if (ax < dx || ax == 0 || high_p < 0 || low_p < 0) return;
        const int voice = S_voice[static_cast<size_t>(low_p)];
        release_voice(low_p);
        const size_t hi = static_cast<size_t>(high_p);
        S_voice[hi] = voice;
        ++MIDI_voices[static_cast<size_t>(S_channel[hi])];
        V_channel[static_cast<size_t>(voice)] = S_channel[hi];
        S_update[hi] = U_ALL_REGS;
        update_voice(high_p);
        if (--slot_cnt == 0) return;
    }
}

void AilXmidi::bnk_phase(int slot) {
    const size_t si = static_cast<size_t>(slot);
    const std::vector<uint8_t>& t = timbres_[static_cast<size_t>(S_timbre[si])].data;
    S_BLOCK[si] = 0x20;
    S_p_val[si] = 32767;
    S_FBC[si] = t[8] & 1;
    S_fb_val[si] = static_cast<uint16_t>(t[8] << 12);
    S_KSLTL_0[si] = t[4] & 0xC0;
    S_v0_val[si] = static_cast<uint16_t>(((~t[4]) & 0x3F) << 10);
    S_KSLTL_1[si] = t[10] & 0xC0;
    S_v1_val[si] = static_cast<uint16_t>(((~t[10]) & 0x3F) << 10);
    S_AVEKM_0[si] = t[3] & 0xF0;
    S_m0_val[si] = static_cast<uint16_t>(t[3] << 12);
    S_AVEKM_1[si] = t[9] & 0xF0;
    S_m1_val[si] = static_cast<uint16_t>(t[9] << 12);
    S_AD_0[si] = t[5];
    S_SR_0[si] = t[6];
    S_AD_1[si] = t[11];
    S_SR_1[si] = t[12];
    S_ws_val[si] = static_cast<uint16_t>(t[13] | (t[7] << 8));
    S_scale_01[si] = S_FBC[si] | 2;
    S_update[si] = U_ALL_REGS;
}

void AilXmidi::note_off(int chan, int note) {
    for (int s = 0; s < kSlots; ++s) {
        const size_t si = static_cast<size_t>(s);
        if (S_status[si] != kKeyOn || S_keynum[si] != note || S_channel[si] != chan) continue;
        if (MIDI_sus[static_cast<size_t>(chan)] >= 64) {
            S_sustain[si] = 1;
            continue;
        }
        release_voice(s);
        S_status[si] = kFree;
    }
}

void AilXmidi::note_on(int chan, int note, int velocity) {
    int timbre = MIDI_timbre[static_cast<size_t>(chan)];
    if (chan == 9) timbre = index_timbre(127, static_cast<uint8_t>(note));  // RBS_timbres, see the header
    if (timbre < 0) return;
    const std::vector<uint8_t>& t = timbres_[static_cast<size_t>(timbre)].data;
    int slot = -1;
    for (int s = 0; s < kSlots && slot < 0; ++s)
        if (S_status[static_cast<size_t>(s)] == kFree) slot = s;
    if (slot < 0) return;
    const size_t si = static_cast<size_t>(slot);
    S_channel[si] = chan;
    S_keynum[si] = note;
    // Channel 10 plays the timbre's note untransposed; the others the key,
    // transposed by the timbre.
    if (chan == 9) {
        S_note[si] = t[2];
        S_transpose[si] = 0;
    } else {
        S_note[si] = note;
        S_transpose[si] = t[2];
    }
    S_velocity[si] = kVelGraph[(velocity & 0xFF) >> 3 & 0x0F];
    S_timbre[si] = timbre;
    S_status[si] = kKeyOn;
    S_sustain[si] = 0;
    const int size = t[0] | (t[1] << 8);
    if (size != 14) return;  // OPL3 and TVFX timbres: not in SAMPLE.AD (header)
    bnk_phase(slot);
    S_voice[si] = -1;
    assign_voice(slot);
}

void AilXmidi::release_sustain(int chan) {
    for (int s = 0; s < kSlots; ++s) {
        const size_t si = static_cast<size_t>(s);
        if (S_status[si] == kFree || S_channel[si] != chan || S_sustain[si] == 0) continue;
        // The driver passes S_note, not the key: on channel 10 it doesn't match.
        note_off(chan, S_note[si]);
    }
}

void AilXmidi::send(uint8_t stat, uint8_t d1, uint8_t d2) {
    const int chan = stat & 0x0F;
    const int kind = stat & 0xF0;
    const size_t c = static_cast<size_t>(chan);
    int flags = 0;
    switch (kind) {
        case 0x90:
            if (chan < kMinTrueChan - 1 || chan > kMaxRecChan - 1) return;
            if (d2 == 0) {
                note_off(chan, d1);
                return;
            }
            note_on(chan, d1, d2);
            return;
        case 0x80: note_off(chan, d1); return;
        case 0xC0:
            MIDI_program[c] = d1;
            MIDI_timbre[c] = index_timbre(static_cast<uint8_t>(MIDI_bank[c]), d1);
            return;
        case 0xE0:
            MIDI_pitch_l[c] = d1;
            MIDI_pitch_h[c] = d2;
            flags = U_FREQ;
            break;
        case 0xB0:
            switch (d1) {
                case kPatchBankSel: MIDI_bank[c] = d2; return;
                case kVoiceProtect: MIDI_vprot[c] = d2; return;
                case kTimbreProtect: return;  // the cache never evicts (header)
                case kModulation:
                    MIDI_mod[c] = d2;
                    flags = U_AVEKM;
                    break;
                case kPartVolume:
                    MIDI_vol[c] = d2;
                    flags = U_KSLTL;
                    break;
                case kExpression:
                    MIDI_express[c] = d2;
                    flags = U_KSLTL;
                    break;
                case kPanpot:
                    MIDI_pan[c] = d2;
                    flags = U_KSLTL;
                    break;
                case kSustain:
                    MIDI_sus[c] = d2;
                    if (d2 < 64) release_sustain(chan);
                    return;
                case kResetAllCtrls:
                    MIDI_sus[c] = 0;
                    release_sustain(chan);
                    MIDI_mod[c] = 0;
                    MIDI_express[c] = 127;
                    MIDI_pitch_l[c] = 0x00;
                    MIDI_pitch_h[c] = 0x40;
                    flags = U_AVEKM | U_KSLTL | U_FREQ;
                    break;
                case kAllNotesOff:
                    for (int s = 0; s < kSlots; ++s)
                        if (S_status[static_cast<size_t>(s)] == kKeyOn && S_channel[static_cast<size_t>(s)] == chan)
                            note_off(chan, S_note[static_cast<size_t>(s)]);
                    return;
                default: return;
            }
            break;
        default: return;
    }
    // __flag_updates
    for (int s = 0; s < kSlots; ++s) {
        const size_t si = static_cast<size_t>(s);
        if (S_status[si] == kFree || S_channel[si] != chan) continue;
        S_update[si] |= flags;
        update_voice(s);
    }
}

// ---------------------------------------------------------------------------
// XMIDI.ASM

void AilXmidi::init() {
    registered_ = false;
    status_ = kStopped;
    for (int reg = 0x01; reg <= 0xF5; ++reg) update_reg(static_cast<uint8_t>(reg), array0_init(reg));
    // init_synth
    for (Timbre& t : timbres_) t = Timbre{};
    MIDI_timbre.fill(-1);
    MIDI_voices.fill(0);
    MIDI_program.fill(-1);
    MIDI_bank.fill(0);
    S_status.fill(kFree);
    V_channel.fill(-1);
    rover_2op = -1;
    // The controllers' defaults on channels 2-10: volume 127, modulation 0,
    // pan 64, expression 127, sustain 0, bank 0, lock / protection 0.
    constexpr uint8_t kLogged[9] = {kPartVolume, kModulation, kPanpot, kExpression, kSustain,
                                    kPatchBankSel, kChanLock, kChanProtect, kVoiceProtect};
    constexpr uint8_t kDefault[9] = {127, 0, 64, 127, 0, 0, 0, 0, 0};
    for (int k = 0; k < 9; ++k)
        for (int ch = kMinTrueChan - 1; ch <= kMaxRecChan - 1; ++ch)
            send(static_cast<uint8_t>(0xB0 | ch), kLogged[k], kDefault[k]);
    constexpr int kPrgDefault[9] = {68, 48, 95, 78, 41, 3, 110, 122, -1};  // Roland defaults
    for (int ch = kMinTrueChan - 1; ch <= kMaxRecChan - 1; ++ch) {
        send(static_cast<uint8_t>(0xE0 | ch), 0x00, 0x40);
        const int prg = kPrgDefault[ch - (kMinTrueChan - 1)];
        if (prg >= 0) send(static_cast<uint8_t>(0xC0 | ch), static_cast<uint8_t>(prg), 0);
    }
}

bool AilXmidi::register_sequence(const std::vector<uint8_t>& d, int number) {
    // find_seq: the (number + 1)th FORM XMID, in a FORM or a CAT XMID. The
    // chunk walk adds length + 8, without IFF padding, as the driver does.
    size_t p = 0;
    size_t form = SIZE_MAX;
    while (true) {
        if (!(tag(d, p, "CAT ") || tag(d, p, "FORM")) || p + 12 > d.size()) return false;
        if (tag(d, p + 8, "XMID")) break;
        p += static_cast<size_t>(be32(d, p + 4)) + 8;
    }
    if (tag(d, p, "FORM")) {
        if (number != 0) return false;
        form = p;
    } else {
        int64_t remaining = static_cast<int64_t>(be32(d, p + 4)) - 5;
        size_t q = p + 12;
        int count = number + 1;
        while (true) {
            if (q + 12 > d.size()) return false;
            if (tag(d, q + 8, "XMID") && --count == 0) {
                form = q;
                break;
            }
            const int64_t len = static_cast<int64_t>(be32(d, q + 4)) + 8;
            remaining -= len;
            if (remaining < 0) return false;
            q += static_cast<size_t>(len);
        }
    }
    // register_seq's chunk log: TIMB, RBRN, and EVNT, which ends it.
    timb_.clear();
    evnt_.clear();
    size_t c = form + 12;
    while (true) {
        if (c + 8 > d.size()) return false;
        const size_t len = be32(d, c + 4);
        if (tag(d, c, "TIMB") && c + 10 <= d.size()) {
            const int n = d[c + 8] | (d[c + 9] << 8);
            for (int i = 0; i < n && c + 12 + 2 * static_cast<size_t>(i) <= d.size(); ++i)
                timb_.push_back({d[c + 10 + 2 * static_cast<size_t>(i)], d[c + 11 + 2 * static_cast<size_t>(i)]});
        } else if (tag(d, c, "EVNT")) {
            if (c + 8 + len > d.size()) return false;
            evnt_.assign(d.begin() + static_cast<long>(c + 8), d.begin() + static_cast<long>(c + 8 + len));
            break;
        }
        c += len + 8;
    }
    registered_ = true;
    status_ = kStopped;
    rewind();
    return true;
}

int AilXmidi::request() const {
    if (!registered_) return -1;
    for (const auto& [patch, bank] : timb_)
        if (index_timbre(bank, patch) < 0) return (bank << 8) | patch;
    return -1;
}

void AilXmidi::rewind() {
    for_loop_cnt.fill(-1);
    chan_sus.fill(-1);
    chan_vprot.fill(-1);
    note_chan.fill(-1);
    interval_cnt = 0;
    note_count = 0;
    ptr_ = 0;
}

void AilXmidi::flush_note_queue() {
    for (int i = 0; i < kMaxNotes; ++i) {
        const size_t n = static_cast<size_t>(i);
        if (note_chan[n] == -1) continue;
        const int ch = note_chan[n];
        note_chan[n] = -1;
        send(static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(note_num[n]), 0);
    }
    note_count = 0;
}

void AilXmidi::reset_sequence() {
    for (int ch = 0; ch < kChans; ++ch) {
        const size_t c = static_cast<size_t>(ch);
        if (chan_sus[c] >= 64) send(static_cast<uint8_t>(0xB0 | ch), kSustain, 0);
        if (chan_vprot[c] >= 64) send(static_cast<uint8_t>(0xB0 | ch), kVoiceProtect, 0);
    }
}

void AilXmidi::start() {
    if (!registered_) return;
    if (status_ == kPlaying) stop();
    rewind();
    status_ = kPlaying;
}

void AilXmidi::stop() {
    if (!registered_ || status_ != kPlaying) return;
    flush_note_queue();
    reset_sequence();
    status_ = kStopped;
}

void AilXmidi::release() {
    // A playing sequence would be released when it ends (post_release); the
    // game stops it first.
    registered_ = false;
}

int AilXmidi::control(int chan, int con, int val) {
    const size_t c = static_cast<size_t>(chan);
    if (con == kSustain) chan_sus[c] = val;
    if (con == kVoiceProtect) chan_vprot[c] = val;
    switch (con) {
        case kClearBeatBar:
        case kCallbackTrig:
        case kChanProtect:
        case kChanLock:
        case kIndirect:
            return 3;  // not sent to the synthesizer (not transcribed further, header)
        case kForLoop:
            for (int i = 0; i < kForNest; ++i) {
                if (for_loop_cnt[static_cast<size_t>(i)] != -1) continue;
                for_loop_cnt[static_cast<size_t>(i)] = val;
                for_ptrs[static_cast<size_t>(i)] = ptr_;  // the NEXT jumps back onto this event
                break;
            }
            return 3;
        case kNextLoop: {
            if (val < 64) return 3;  // BREAK
            for (int i = kForNest - 1; i >= 0; --i) {
                const size_t n = static_cast<size_t>(i);
                if (for_loop_cnt[n] == -1) continue;
                if (for_loop_cnt[n] != 0 && --for_loop_cnt[n] == 0) {
                    for_loop_cnt[n] = -1;
                    return 3;
                }
                ptr_ = for_ptrs[n];
                return 3;
            }
            return 3;
        }
        default: break;
    }
    send(static_cast<uint8_t>(0xB0 | chan), static_cast<uint8_t>(con), static_cast<uint8_t>(val));
    return 3;
}

size_t AilXmidi::note_on_event() {
    const size_t start = ptr_;
    const int chan = evnt_[start] & 0x0F;
    const int note = evnt_[start + 1];
    const int vel = evnt_[start + 2];
    size_t i = start + 3;
    uint32_t duration = 0;
    while (i < evnt_.size()) {
        const uint8_t b = evnt_[i++];
        duration = (duration << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    size_t slot = 0;
    bool found = false;
    for (size_t n = 0; n < static_cast<size_t>(kMaxNotes); ++n) {
        if (note_chan[n] == -1) {
            slot = n;
            found = true;
            break;
        }
    }
    if (found) ++note_count;  // a full queue overwrites entry 0
    note_chan[slot] = chan;
    note_num[slot] = note;
    note_time[slot] = static_cast<int32_t>(duration) - 1;
    send(static_cast<uint8_t>(0x90 | chan), static_cast<uint8_t>(note), static_cast<uint8_t>(vel));
    return i - start;
}

void AilXmidi::serve() {
    if (!registered_ || status_ != kPlaying) return;
    if (note_count != 0) {
        for (size_t n = 0; n < static_cast<size_t>(kMaxNotes) && note_count != 0; ++n) {
            if (note_chan[n] == -1) continue;
            if (--note_time[n] >= 0) continue;
            const int ch = note_chan[n];
            note_chan[n] = -1;
            send(static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(note_num[n]), 0);
            --note_count;
        }
    }
    if (--interval_cnt > 0) return;
    while (status_ == kPlaying) {
        if (ptr_ >= evnt_.size()) {  // no end-of-track event: treat the end as one
            reset_sequence();
            status_ = kDone;
            return;
        }
        const uint8_t b = evnt_[ptr_];
        if (b < 0x80) {
            interval_cnt = b;
            ++ptr_;
            return;
        }
        const int kind = b & 0xF0;
        const int chan = b & 0x0F;
        const uint8_t d1 = ptr_ + 1 < evnt_.size() ? evnt_[ptr_ + 1] : 0;
        const uint8_t d2 = ptr_ + 2 < evnt_.size() ? evnt_[ptr_ + 2] : 0;
        size_t length = 0;
        if (kind == 0xF0) {
            size_t i = ptr_ + (chan == 0x0F ? 2 : 1);
            uint32_t len = 0;
            while (i < evnt_.size()) {
                const uint8_t v = evnt_[i++];
                len = (len << 7) | (v & 0x7F);
                if (!(v & 0x80)) break;
            }
            length = i - ptr_ + len;
            if (chan == 0x0F && d1 == 0x2F) {
                reset_sequence();
                status_ = kDone;
            }
        } else if (kind == 0xE0) {
            send(b, d1, d2);
            length = 3;
        } else if (kind == 0xD0) {
            send(b, d1, 0);
            length = 2;
        } else if (kind == 0xC0) {
            send(b, d1, 0);
            length = 2;
        } else if (kind == 0xB0) {
            length = static_cast<size_t>(control(chan, d1, d2));
        } else if (kind == 0xA0) {
            send(b, d1, d2);
            length = 3;
        } else {
            length = note_on_event();  // 0x80 too: XMIDI has no note-offs
        }
        ptr_ += length;
    }
}

}  // namespace gaius::audio
