// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius CLI tool: bindiff_exe
//
// Decompress two EXEPACK'd DOS executables (via formats::exepack) and
// diff them: header fields, raw byte-level differences (as contiguous
// runs), and embedded-string differences. Built specifically to compare
// the two now-available CSR.EXE builds (US-region, already fully
// analyzed, vs. international/multi-language) per
// CAESAR_GOG_BUILD_FINDINGS.md and GAIUS_ROADMAP.md Phase 0 — but works
// on any two EXEPACK'd executables.
//
// Usage:
//   bindiff_exe <a.exe> <b.exe> [--strings] [--max-runs N]

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "formats/exepack/exepack.hpp"

using namespace gaius::formats;

namespace {

void print_header(const char* label, const exepack::DecodeResult& r) {
    std::printf("%s:\n", label);
    std::printf("  file_size          = %zu\n", r.file_size);
    std::printf("  header_offset      = 0x%zX\n", r.header_offset);
    std::printf("  compressed_start   = 0x%zX\n", r.compressed_start);
    std::printf("  compressed_end     = 0x%zX\n", r.compressed_end);
    std::printf("  raw_prefix_size    = %zu (0x%zX)\n", r.raw_prefix_size, r.raw_prefix_size);
    std::printf("  record_count       = %zu\n", r.record_count);
    std::printf("  decompressed_size  = %zu (0x%zX)\n", r.image.size(), r.image.size());
    const auto& h = r.header;
    std::printf("  header: real_IP=0x%04X real_CS=0x%04X mem_start=0x%04X exepack_size=0x%04X\n", h.real_IP,
                h.real_CS, h.mem_start, h.exepack_size);
    std::printf("          real_SP=0x%04X real_SS=0x%04X dest_len=0x%04X skip_len=0x%04X\n", h.real_SP, h.real_SS,
                h.dest_len, h.skip_len);
}

// Extract printable ASCII runs of at least `min_len` characters, the same
// way `strings` does. Good enough for cross-build string-set diffing.
std::set<std::string> extract_strings(const std::vector<uint8_t>& image, size_t min_len = 6) {
    std::set<std::string> out;
    std::string cur;
    auto flush = [&]() {
        if (cur.size() >= min_len) out.insert(cur);
        cur.clear();
    };
    for (uint8_t b : image) {
        if (b >= 0x20 && b < 0x7F) {
            cur.push_back(static_cast<char>(b));
        } else {
            flush();
        }
    }
    flush();
    return out;
}

void print_usage(const char* argv0) {
    std::fprintf(stderr, "usage: %s <a.exe> <b.exe> [--strings] [--max-runs N]\n", argv0);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }
    std::string path_a = argv[1];
    std::string path_b = argv[2];
    bool show_strings = false;
    size_t max_runs = 40;

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--strings") {
            show_strings = true;
        } else if (arg == "--max-runs" && i + 1 < argc) {
            max_runs = static_cast<size_t>(std::atoi(argv[++i]));
        }
    }

    try {
        std::printf("Decoding A: %s\n", path_a.c_str());
        exepack::DecodeResult a = exepack::decode(path_a);
        std::printf("Decoding B: %s\n\n", path_b.c_str());
        exepack::DecodeResult b = exepack::decode(path_b);

        print_header("A", a);
        std::printf("\n");
        print_header("B", b);
        std::printf("\n");

        if (a.image.size() != b.image.size()) {
            std::printf("Decompressed sizes DIFFER: A=%zu B=%zu (diff=%zd)\n", a.image.size(), b.image.size(),
                        static_cast<ptrdiff_t>(a.image.size()) - static_cast<ptrdiff_t>(b.image.size()));
        } else {
            std::printf("Decompressed sizes match: %zu bytes\n", a.image.size());
        }

        // --- byte-level diff over the common prefix ---
        size_t common = std::min(a.image.size(), b.image.size());
        std::printf("\nByte-level diff over common %zu bytes:\n", common);
        size_t i = 0, total_diff_bytes = 0, run_count = 0;
        while (i < common) {
            if (a.image[i] == b.image[i]) {
                ++i;
                continue;
            }
            size_t start = i;
            while (i < common && a.image[i] != b.image[i]) ++i;
            size_t len = i - start;
            total_diff_bytes += len;
            ++run_count;
            if (run_count <= max_runs) {
                std::printf("  run %4zu: offset 0x%06zX, length %zu\n", run_count, start, len);
            }
        }
        if (run_count > max_runs) std::printf("  ... and %zu more runs\n", run_count - max_runs);
        std::printf("Total: %zu differing bytes across %zu contiguous run(s) (%.2f%% of common region)\n",
                    total_diff_bytes, run_count, common ? (100.0 * total_diff_bytes / common) : 0.0);

        // --- string-set diff ---
        if (show_strings) {
            std::printf("\nExtracting printable strings (>=6 chars) for set comparison...\n");
            auto strings_a = extract_strings(a.image);
            auto strings_b = extract_strings(b.image);
            std::printf("A has %zu unique strings, B has %zu unique strings\n", strings_a.size(), strings_b.size());

            std::vector<std::string> only_a, only_b;
            std::set_difference(strings_a.begin(), strings_a.end(), strings_b.begin(), strings_b.end(),
                                 std::back_inserter(only_a));
            std::set_difference(strings_b.begin(), strings_b.end(), strings_a.begin(), strings_a.end(),
                                 std::back_inserter(only_b));

            std::printf("\nStrings only in A (%zu total, showing up to 60):\n", only_a.size());
            for (size_t k = 0; k < only_a.size() && k < 60; ++k) std::printf("  %s\n", only_a[k].c_str());

            std::printf("\nStrings only in B (%zu total, showing up to 60):\n", only_b.size());
            for (size_t k = 0; k < only_b.size() && k < 60; ++k) std::printf("  %s\n", only_b[k].c_str());
        }

        return 0;
    } catch (const FormatError& e) {
        std::fprintf(stderr, "format error: %s\n", e.what());
        return 2;
    }
}
