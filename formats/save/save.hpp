// Gaius — formats/save/save.hpp
//
// CAESARxx.SAV block-table reader — Layer 1, READ-ONLY.
//
// Per GAIUS_ROADMAP.md Phase 0/2: the save SERIALIZER is fully reconstructed
// (CAESAR_SAVE_FORMAT.md / COMPLETE.md sections 64-68) but the LOADER is not
// yet reverse engineered. This reader therefore only slices a save file into
// the already-confirmed byte ranges — it does not claim to interpret every
// field's meaning, and it must not be used to *write* a save file that the
// original engine would accept until the loader work in roadmap Phase 8
// closes that gap. Layer 3 (formats/save write-back) is a separate,
// later concern.
//
// Total size is DEFINITIVE: exactly 0xDF26 (57126) bytes.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "formats/common/types.hpp"

namespace gaius::formats::save {

constexpr size_t kSaveSize = 0xDF26;  // 57126

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

}  // namespace gaius::formats::save
