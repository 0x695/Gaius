// Gaius — formats/exepack/exepack.hpp
//
// Microsoft EXEPACK decompressor for CSR.EXE-style DOS executables.
//
// This is NOT an Impressions/Caesar-specific format — EXEPACK is a generic
// DOS linker compression scheme used by countless programs of the era. It's
// implemented here because static analysis of CSR.EXE must be performed
// against the DECOMPRESSED image, not the packed file on disk (per
// CAESAR_REVERSE_ENGINEERING_COMPLETE.md section 3.1's "Important
// consequence"). The existing RE corpus had already established that a
// decompressed image was reconstructed and gave several header field
// values as ground truth, but only vaguely described the compression
// scheme itself ("B0/B2-style command structure... decoder works
// backwards through the packed stream"). This implementation was derived
// by disassembling the actual embedded decompression stub (via ndisasm)
// rather than guessing, and is validated to byte-exact correctness below.
//
// --- File layout ---
//
//   [MZ header, e_cparhdr*16 bytes]
//   [load-module image: partly-or-fully compressed data]
//   [18-byte EXEPACK header: 8x uint16 LE fields + 2-byte "RB" signature]
//   [decompression stub code + embedded header copy + relocation table]
//     (total stub block size == the header's exepack_size field; the
//      header is found by locating the unique "RB" (0x52,0x42) signature
//      bytes and reading the 16 bytes immediately before it)
//
// EXEPACK header fields, in file order:
//   uint16 real_IP, real_CS, mem_start, exepack_size,
//          real_SP, real_SS, dest_len, skip_len
//   uint16 signature   ("RB" = bytes 0x52,0x42 read as LE 0x4252)
//
// --- Compression scheme (reverse engineered from the stub disassembly) ---
//
// The load-module image consists of an uncompressed leading prefix
// (copied verbatim) followed by a backward-RLE-compressed suffix. The
// suffix is a sequence of variable-length records; per-record layout in
// NORMAL ascending memory order is:
//
//   [payload...][length_lo][length_hi][command]
//
// i.e. the command byte is the LAST byte of a record, not the first —
// because decompression processes records back-to-front (starting near
// the end of the compressed data, moving toward its start), since output
// is generated back-to-front too (the decompressed image is always
// larger than its compressed source, so writing "backward into a bigger
// buffer" is what makes safe in-place decompression possible on 1990s
// hardware; this implementation just builds an output byte vector, no
// aliasing concerns).
//
// Command byte semantics (mask with 0xFE to ignore the "last record" bit):
//   masked == 0xB0 : FILL — one payload byte, repeated `length` times.
//   masked == 0xB2 : COPY — `length` raw payload bytes, copied verbatim.
//   bit 0 (cmd & 1): if set, this is the LAST record — stop after it.
//
// Before the first record, the decompressor scans backward through the
// final 16-byte paragraph (governed by the header's `skip_len`, always
// observed as 1 paragraph = 16 bytes) skipping any trailing 0xFF filler
// bytes, landing on the true first command byte.
//
// Validated against the already fully-analyzed US-build CSR.EXE: parses
// cleanly to a proper terminal ("last record") flag with zero errors,
// reproduces the exact three known embedded strings intact
// ("Borland C++ - Copyright 1991 Borland Intl.", "CaesarXX.sav",
// "Caesar - Online help"), and the reconstructed image size (raw prefix +
// decompressed suffix) matches the header's declared `dest_len * 16`
// EXACTLY: 497568 bytes (0x797A0) — the same figure already recorded in
// the main RE corpus as the expected decompressed size.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "formats/common/types.hpp"

namespace gaius::formats::exepack {

struct Header {
    uint16_t real_IP = 0;
    uint16_t real_CS = 0;
    uint16_t mem_start = 0;
    uint16_t exepack_size = 0;
    uint16_t real_SP = 0;
    uint16_t real_SS = 0;
    uint16_t dest_len = 0;   // destination size, in paragraphs (x16 = bytes)
    uint16_t skip_len = 0;   // trailing filler paragraphs to skip, typically 1
    uint16_t signature = 0;  // must be 0x4252 ("RB")
};

struct DecodeResult {
    Header header;
    size_t file_size = 0;
    size_t header_offset = 0;      // flat file offset of the 18-byte EXEPACK header
    size_t compressed_start = 0;   // flat file offset where the load-module image starts
    size_t compressed_end = 0;     // == header_offset
    size_t raw_prefix_size = 0;    // bytes of the load-module image that were stored uncompressed
    size_t record_count = 0;
    std::vector<uint8_t> image;    // the full decompressed image; image.size() == header.dest_len * 16
};

// Throws FormatError if the file isn't a valid EXEPACK'd MZ executable, if
// the "RB" signature can't be found uniquely, or if record parsing hits an
// unrecognized command byte or runs out of input before finding a
// terminal ("last record") flag.
DecodeResult decode(const std::string& path);

}  // namespace gaius::formats::exepack
