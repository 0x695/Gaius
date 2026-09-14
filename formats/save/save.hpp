// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — formats/save/save.hpp
//
// CAESARxx.SAV block table, reader and writer — Layer 1.
//
// The writer (0x033C8) and the loader (0x04537) are both read (2026-09-15,
// docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 33): the loader reads
// the same 175 records the writer writes, in the same order, each at the same
// address and size, so a file is nothing but those records laid end to end and
// a SaveFile written back is one the engine reads. DEFINITIVE. What each field
// means is model::CityState's business, not this layer's.
//
// Total size is DEFINITIVE: exactly 0xDF26 (57126) bytes.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "formats/common/types.hpp"

namespace gaius::formats::save {

constexpr size_t kSaveSize = 0xDF26;  // 57126

// DS address of each of global_words_128's 128 two-byte words, in save order:
// element i is the word stored at save+(2*i).
//
// DEFINITIVE -- read from the save writer itself, not inferred. Starting at
// flat 0x033F2 in the decompressed US-build image, the writer makes exactly 128
// calls to its file-write routine (far 0:0x32CC), each pushing one DS address
// and a length of 2. This replaces the "descending from DS:0x6CE2" rule the
// project used until real saves arrived, which is wrong for 107 of the 128
// words: the writer's order has 15 discontinuities, skips DS:0x6CB8 entirely,
// and even steps upward in places.
//
// Two checks tie this to real data rather than to the disassembly alone:
//   - index 31 (save+0x3E) is DS:0x6CA2, the treasury. Four construction
//     handlers do `cmp ax,[0x6ca2]` then `sub [0x6ca2],ax`, and in four real
//     saves of one session that word falls 7182 -> 4692 -> 3210 -> 3144 as the
//     player builds.
//   - DS:0x6C0E appears twice (indices 97 and 110, save+0xC2 and save+0xDC):
//     the writer saves that one variable twice, and in every real save the two
//     copies hold identical values.
inline constexpr uint16_t kGlobalWordDsAddress[128] = {
    0x6CE2, 0x6CE0, 0x6CDE, 0x6CDC, 0x6CDA, 0x6CD8, 0x6CD6, 0x6CD4,
    0x6CD2, 0x6CD0, 0x6CCE, 0x6CCC, 0x6CCA, 0x6CC8, 0x6CC6, 0x6CC4,
    0x6CC2, 0x6CC0, 0x6CBE, 0x6CBC, 0x6CBA, 0x6CB6, 0x6CB4, 0x6CB2,
    0x6CB0, 0x6CAE, 0x6CAC, 0x6CAA, 0x6CA8, 0x6CA6, 0x6CA4, 0x6CA2,
    0x6CA0, 0x6C9E, 0x6C9C, 0x6C7C, 0x6C7A, 0x6C78, 0x6C72, 0x6C70,
    0x6C9A, 0x6C6C, 0x6C6A, 0x6C68, 0x6C66, 0x6C64, 0x6C62, 0x6C60,
    0x6C5E, 0x6C5A, 0x6C58, 0x6C56, 0x6C54, 0x6C52, 0x6C50, 0x6C4E,
    0x6C4C, 0x6C4A, 0x6C48, 0x6C46, 0x6C44, 0x6C42, 0x6C40, 0x6BE4,
    0x6BE2, 0x6BE0, 0x6C3C, 0x6C3A, 0x6C38, 0x6C36, 0x6C34, 0x6BDC,
    0x6BDA, 0x6BD8, 0x6BD6, 0x6BD4, 0x6BD2, 0x6BD0, 0x6BCE, 0x6C32,
    0x6C30, 0x6C2E, 0x6C2C, 0x6C2A, 0x6C28, 0x6C26, 0x6C24, 0x6C22,
    0x6C20, 0x6C1E, 0x6C1C, 0x6C1A, 0x6C18, 0x6C16, 0x6C14, 0x6C12,
    0x6C10, 0x6C0E, 0x6C0C, 0x6C0A, 0x6C08, 0x6C06, 0x6C04, 0x6C00,
    0x6BFA, 0x6BF8, 0x6BF6, 0x6BF4, 0x6BF2, 0x6BF0, 0x6C0E, 0x6BCC,
    0x6BCA, 0x6BC8, 0x6BC6, 0x6BC4, 0x6BC2, 0x6BC0, 0x6BBE, 0x6BBC,
    0x6BBA, 0x6BB8, 0x6BB6, 0x6B38, 0x6B36, 0x6B34, 0x6B32, 0x6B30,
};

struct Block {
    size_t offset;
    size_t size;
    std::string name;  // matches caesar_save_layout.py's BLOCKS table naming
};

// The confirmed block table, in file order. Source: CAESAR_SAVE_FORMAT.md /
// caesar_save_layout.py. Kept as a function (not a static global) so it's
// trivial to unit-test that offsets are contiguous and sum to kSaveSize.
std::vector<Block> block_table();

struct SaveFile {
    std::vector<uint8_t> raw;  // the full 57126-byte file, verbatim

    // Returns a view into `raw` for the named block. Throws FormatError if
    // the name isn't in block_table().
    std::pair<const uint8_t*, size_t> block(const std::string& name) const;
};

// Throws FormatError if the file isn't exactly kSaveSize bytes.
SaveFile load(const std::string& path);

// Writes the kSaveSize bytes of `sf` to `path`. Throws FormatError if `sf`
// isn't that size or the file can't be written.
void write(const SaveFile& sf, const std::string& path);

}  // namespace gaius::formats::save
