// Gaius CLI tool: save_inspect
//
// Slice a CAESARxx.SAV file into its confirmed blocks (see
// formats/save/save.hpp) and print offset/size/name for each, plus the
// leading global-words section decoded per CAESAR_SAVE_FORMAT.md
// (128 x 16-bit words, descending DS address from DS:0x6CE2), matching
// caesar_save_layout.py's output for direct cross-checking.
//
// Usage:
//   save_inspect <CAESARxx.SAV>

#include <cstdio>
#include <string>

#include "formats/save/save.hpp"

using namespace gaius::formats;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <CAESARxx.SAV>\n", argv[0]);
        return 1;
    }

    std::string path = argv[1];
    try {
        save::SaveFile sf = save::load(path);
        std::printf("%s: %zu bytes (expected %zu)\n", path.c_str(), sf.raw.size(), save::kSaveSize);

        std::printf("\nConfirmed blocks:\n");
        for (const auto& b : save::block_table()) {
            std::printf("  0x%04zX-0x%04zX  %5zu  %s\n", b.offset, b.offset + b.size - 1, b.size, b.name.c_str());
        }

        std::printf("\nFirst global words (save+0xNNNN -> DS:0xNNNN, per the recovered descending-address mapping):\n");
        auto [block_ptr, block_size] = sf.block("global_words_128");
        for (size_t i = 0; i + 1 < block_size; i += 2) {
            uint16_t ds_addr = static_cast<uint16_t>(0x6CE2 - i);
            uint16_t value = static_cast<uint16_t>(block_ptr[i]) | (static_cast<uint16_t>(block_ptr[i + 1]) << 8);
            std::printf("  save+0x%04zX -> DS:0x%04X = 0x%04X (%u)\n", i, ds_addr, value, value);
        }
        return 0;
    } catch (const FormatError& e) {
        std::fprintf(stderr, "format error: %s\n", e.what());
        return 2;
    }
}
