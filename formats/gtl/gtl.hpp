// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — formats/gtl/gtl.hpp
//
// SAMPLE.AD: the Miles Audio Interface Library's Global Timbre Library for the
// YM3812 (OPL2), a published format (AIL 2.14's source, released as freeware
// in 2000). The game's FM driver asks for the timbres a tune lists and the
// game reads them from this file (0x31E03, findings section 45).
//
// A directory of 6-byte entries: u8 patch, u8 bank, u32 offset into the file,
// ending with an entry whose bank is 0xFF (the game stops there). At each
// offset a timbre: u16 length (itself included), then for a 14-byte .BNK
// timbre
//   s8 transpose, then modulator AVEKM, KSLTL, AD, SR, WS, the feedback /
//   connection byte, then carrier AVEKM, KSLTL, AD, SR, WS
// (YAMAHA.INC's BNK structure). Bank 127 holds the rhythm timbres, one per
// key; for those `transpose` is the note the drum plays.
// The US build's SAMPLE.AD has 162 timbres, all 14 bytes.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "formats/common/types.hpp"

namespace gaius::formats::gtl {

struct Timbre {
    uint8_t patch = 0;
    uint8_t bank = 0;
    std::vector<uint8_t> data;  // the timbre as stored, its u16 length first
};

std::vector<Timbre> parse(const std::vector<uint8_t>& file);
std::vector<Timbre> load(const std::string& path);

}  // namespace gaius::formats::gtl
