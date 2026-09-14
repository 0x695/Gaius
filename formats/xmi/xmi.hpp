// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — formats/xmi/xmi.hpp
//
// .XMI / .XM2 music: the Miles Sound System's Extended MIDI, a documented
// format, converted here to a Standard MIDI File. The US build ships both
// variants of each cue (.XM2 a sparser arrangement). The international build's
// .MDI files are standard MIDI but not the same files converted: they are
// re-orchestrated for General MIDI (drums moved off channel 9, velocities
// changed) and some renumbered -- the US CZARJIN1 is the international
// CZARJIN2. Their timing does match: every note onset of CZARJIN1/6/A/B lands
// within 0.02 quarter notes of the .MDI's at 120 ticks a second
// (test_xmi_matches_mdi), which is what confirms the tick rate below.
//
// The container is IFF (big-endian chunk lengths, padded to even):
//   FORM XDIR { INFO u16le sequence count }  CAT XMID { FORM XMID { [TIMB] [RBRN] EVNT } ... }
// EVNT differs from MIDI in three ways:
//   - a delay is a run of bytes below 0x80, summed, before the next event;
//   - there is no running status;
//   - a note-on carries its duration as a variable-length number after the
//     velocity, and there are no note-offs.
// Its ticks are 1/120 s. The conversion writes format 0 with 60 ticks a quarter
// note and a tempo of 500000 us, which is 120 ticks a second, and drops the
// file's own tempo events, as the Miles driver plays at its fixed rate.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "formats/common/types.hpp"

namespace gaius::formats::xmi {

// One sequence's events as a Standard MIDI File (format 0, one track).
std::vector<uint8_t> to_midi(const std::vector<uint8_t>& xmi, size_t sequence = 0);

// The number of sequences in the file.
size_t sequence_count(const std::vector<uint8_t>& xmi);

std::vector<uint8_t> read_file(const std::string& path);

}  // namespace gaius::formats::xmi
