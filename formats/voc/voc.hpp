// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — formats/voc/voc.hpp
//
// .VOC sound effects: Creative Labs' Creative Voice File, a published format
// (no reverse engineering needed). All 25 of the US build's effects are
// 8-bit unsigned mono PCM in type-1 sound blocks. The header:
//   "Creative Voice File" 0x1A, u16 offset of the first block, u16 version,
//   u16 check = ~version + 0x1234
// then blocks of u8 type and a 24-bit length:
//   0 end, 1 sound (u8 time constant, u8 codec, samples), 2 more samples,
//   3 silence (u16 length - 1, u8 time constant), 4 marker, 5 text,
//   6/7 repeat, 8 extended, 9 new-format sound.
// The rate is 1000000 / (256 - time constant). Codecs other than 8-bit PCM,
// stereo and new-format blocks are refused.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "formats/common/types.hpp"

namespace gaius::formats::voc {

struct Sound {
    int sample_rate = 0;             // Hz
    std::vector<uint8_t> samples;    // 8-bit unsigned, 128 = silence
};

Sound load(const std::string& path);
Sound parse(const std::vector<uint8_t>& data);

}  // namespace gaius::formats::voc
