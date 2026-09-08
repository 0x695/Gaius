#include "formats/exepack/exepack.hpp"

#include <cstdio>

namespace gaius::formats::exepack {

namespace {

uint16_t read_u16le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

std::vector<uint8_t> read_whole_file(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw FormatError("exepack: cannot open " + path);
    std::fseek(f, 0, SEEK_END);
    long size_l = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size_l < 0) {
        std::fclose(f);
        throw FormatError("exepack: cannot determine size of " + path);
    }
    std::vector<uint8_t> buf(static_cast<size_t>(size_l));
    size_t n = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    if (n != buf.size()) throw FormatError("exepack: short read on " + path);
    return buf;
}

}  // namespace

DecodeResult decode(const std::string& path) {
    std::vector<uint8_t> data = read_whole_file(path);
    if (data.size() < 0x20 || data[0] != 'M' || data[1] != 'Z') {
        throw FormatError("exepack: not an MZ executable: " + path);
    }

    uint16_t e_cparhdr = read_u16le(&data[8]);
    size_t mz_header_size = static_cast<size_t>(e_cparhdr) * 16;
    if (mz_header_size >= data.size()) throw FormatError("exepack: invalid MZ header size in " + path);

    // Find the unique "RB" signature (bytes 0x52, 0x42) — the EXEPACK
    // header's last field. Require exactly one match so we don't silently
    // pick up a coincidental byte pair elsewhere in the file.
    std::vector<size_t> candidates;
    for (size_t i = 0; i + 1 < data.size(); ++i) {
        if (data[i] == 0x52 && data[i + 1] == 0x42) candidates.push_back(i);
    }
    if (candidates.empty()) throw FormatError("exepack: 'RB' signature not found in " + path);
    if (candidates.size() > 1) {
        throw FormatError("exepack: 'RB' signature found " + std::to_string(candidates.size()) +
                           " times in " + path + " — cannot uniquely locate the header");
    }
    size_t sig_pos = candidates[0];
    if (sig_pos < 16) throw FormatError("exepack: signature too close to start of file in " + path);

    size_t header_offset = sig_pos - 16;
    Header h;
    h.real_IP = read_u16le(&data[header_offset + 0]);
    h.real_CS = read_u16le(&data[header_offset + 2]);
    h.mem_start = read_u16le(&data[header_offset + 4]);
    h.exepack_size = read_u16le(&data[header_offset + 6]);
    h.real_SP = read_u16le(&data[header_offset + 8]);
    h.real_SS = read_u16le(&data[header_offset + 10]);
    h.dest_len = read_u16le(&data[header_offset + 12]);
    h.skip_len = read_u16le(&data[header_offset + 14]);
    h.signature = read_u16le(&data[sig_pos]);

    if (h.signature != 0x4252) throw FormatError("exepack: signature mismatch in " + path);

    // Sanity cross-check found empirically: header_offset should sit
    // exactly exepack_size bytes before end-of-file (the stub block —
    // header + decompression code + its own relocations — is exactly
    // exepack_size bytes long and runs to EOF).
    if (data.size() < header_offset || data.size() - header_offset != h.exepack_size) {
        throw FormatError("exepack: exepack_size does not match (header_offset..EOF) span in " + path +
                           " — file layout assumption may not hold for this build");
    }

    size_t compressed_start = mz_header_size;
    size_t compressed_end = header_offset;
    if (compressed_end <= compressed_start) throw FormatError("exepack: empty/invalid compressed region in " + path);

    const uint8_t* comp = &data[compressed_start];
    size_t n = compressed_end - compressed_start;

    // Skip trailing 0xFF filler within the final `skip_len` paragraphs
    // (per the disassembled stub: scans a fixed 16-byte window per
    // paragraph). skip_len has only ever been observed as 1; support
    // larger values defensively by scanning skip_len*16 bytes.
    size_t window = static_cast<size_t>(h.skip_len) * 16;
    if (window == 0 || window > n) window = 16;  // defensive fallback, matches the one observed case
    size_t window_start = n - window;
    size_t cmd_pos = n - 1;
    size_t scanned = 0;
    while (cmd_pos >= window_start && scanned < window && comp[cmd_pos] == 0xFF) {
        if (cmd_pos == 0) break;
        --cmd_pos;
        ++scanned;
    }

    // Parse backward-RLE records. Record layout in ascending memory order:
    // [payload...][length_lo][length_hi][command]. Collected in
    // processing order (high address first); reversed at the end.
    std::vector<std::vector<uint8_t>> chunks;
    size_t record_count = 0;
    ptrdiff_t P = static_cast<ptrdiff_t>(cmd_pos);
    ptrdiff_t stop_at = -1;  // set to new_P of the terminal record

    while (true) {
        if (P < 2) throw FormatError("exepack: ran out of input before a terminal record in " + path);
        ++record_count;
        uint8_t cmd = comp[P];
        uint8_t masked = cmd & 0xFE;
        bool is_last = (cmd & 1) != 0;
        uint16_t length = static_cast<uint16_t>(comp[P - 2]) | (static_cast<uint16_t>(comp[P - 1]) << 8);

        ptrdiff_t new_P;
        if (masked == 0xB0) {  // FILL
            if (P - 3 < 0) throw FormatError("exepack: fill record underflows input in " + path);
            uint8_t fill_byte = comp[P - 3];
            chunks.emplace_back(static_cast<size_t>(length), fill_byte);
            new_P = P - 4;
        } else if (masked == 0xB2) {  // COPY
            ptrdiff_t payload_start = P - 2 - static_cast<ptrdiff_t>(length);
            if (payload_start < 0) throw FormatError("exepack: copy record underflows input in " + path);
            chunks.emplace_back(comp + payload_start, comp + (P - 2));
            new_P = payload_start - 1;
        } else {
            throw FormatError("exepack: unrecognized command byte 0x" + std::to_string(cmd) +
                               " at record " + std::to_string(record_count) + " in " + path);
        }

        if (is_last) {
            stop_at = new_P;
            break;
        }
        P = new_P;
    }

    size_t raw_prefix_size = static_cast<size_t>(stop_at + 1);

    std::vector<uint8_t> image;
    image.reserve(static_cast<size_t>(h.dest_len) * 16);
    image.assign(comp, comp + raw_prefix_size);
    for (auto it = chunks.rbegin(); it != chunks.rend(); ++it) {
        image.insert(image.end(), it->begin(), it->end());
    }

    size_t expected_size = static_cast<size_t>(h.dest_len) * 16;
    if (image.size() != expected_size) {
        throw FormatError("exepack: reconstructed image size " + std::to_string(image.size()) +
                           " does not match header dest_len*16 = " + std::to_string(expected_size) +
                           " in " + path + " — decompression is likely wrong for this build");
    }

    DecodeResult result;
    result.header = h;
    result.file_size = data.size();
    result.header_offset = header_offset;
    result.compressed_start = compressed_start;
    result.compressed_end = compressed_end;
    result.raw_prefix_size = raw_prefix_size;
    result.record_count = record_count;
    result.image = std::move(image);
    return result;
}

}  // namespace gaius::formats::exepack
