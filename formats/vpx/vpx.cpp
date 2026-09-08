#include "formats/vpx/vpx.hpp"

#include <cstdio>
#include <vector>

namespace gaius::formats::vpx {

namespace {

uint16_t read_u16le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

// Decode one RLE-compressed plane. Mirrors caesar_vpx.py's decode_plane
// exactly, including its strict "decoder overshot output size" and
// "trailing compressed bytes" checks.
std::vector<uint8_t> decode_plane(const uint8_t* payload, size_t payload_len,
                                   size_t output_size, uint8_t fill_a, uint8_t fill_b,
                                   size_t* consumed_out) {
    std::vector<uint8_t> out;
    out.reserve(output_size);
    size_t i = 0;

    while (out.size() < output_size) {
        if (i >= payload_len) throw FormatError("vpx: compressed stream ended early");
        uint8_t c = payload[i++];
        int n = (c & 0x3F) + 1;
        uint8_t typ = c & 0xC0;

        if (typ == 0x00) {  // repeat explicit byte
            if (i >= payload_len) throw FormatError("vpx: RLE byte missing");
            uint8_t v = payload[i++];
            out.insert(out.end(), static_cast<size_t>(n), v);
        } else if (typ == 0x40) {  // repeat header byte A
            out.insert(out.end(), static_cast<size_t>(n), fill_a);
        } else if (typ == 0x80) {  // repeat header byte B
            out.insert(out.end(), static_cast<size_t>(n), fill_b);
        } else {  // 0xC0: literal run
            if (i + static_cast<size_t>(n) > payload_len)
                throw FormatError("vpx: literal run exceeds stream");
            out.insert(out.end(), payload + i, payload + i + n);
            i += static_cast<size_t>(n);
        }
    }

    if (out.size() != output_size) throw FormatError("vpx: decoder overshot output size");
    *consumed_out = i;
    return out;
}

}  // namespace

DecodeResult decode(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw FormatError("vpx: cannot open " + path);

    std::fseek(f, 0, SEEK_END);
    long file_size_l = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (file_size_l < 0) {
        std::fclose(f);
        throw FormatError("vpx: cannot determine file size for " + path);
    }
    size_t file_size = static_cast<size_t>(file_size_l);

    std::vector<uint8_t> data(file_size);
    size_t read = std::fread(data.data(), 1, file_size, f);
    std::fclose(f);
    if (read != file_size) throw FormatError("vpx: short read on " + path);

    DecodeResult result;
    result.image.width = kWidth;
    result.image.height = kHeight;

    std::array<std::vector<uint8_t>, 4> planes;
    size_t off = 0;

    for (int p = 0; p < 4; ++p) {
        if (off + 8 > data.size()) throw FormatError("vpx: truncated header in " + path);

        BlockHeader h;
        h.packed_size = read_u16le(&data[off + 0]);
        h.decoded_size = read_u16le(&data[off + 2]);
        h.fill_pair = read_u16le(&data[off + 4]);
        h.field3 = read_u16le(&data[off + 6]);

        if (h.packed_size < 8 || off + h.packed_size > data.size())
            throw FormatError("vpx: invalid block size in " + path);
        if (h.decoded_size != kPlaneSize)
            throw FormatError("vpx: plane " + std::to_string(p) + " unexpected decoded size " +
                               std::to_string(h.decoded_size) + " in " + path);

        uint8_t fill_a = static_cast<uint8_t>(h.fill_pair & 0xFF);
        uint8_t fill_b = static_cast<uint8_t>((h.fill_pair >> 8) & 0xFF);

        const uint8_t* payload = &data[off + 8];
        size_t payload_len = h.packed_size - 8;
        size_t consumed = 0;
        planes[p] = decode_plane(payload, payload_len, h.decoded_size, fill_a, fill_b, &consumed);

        if (consumed != payload_len) {
            throw FormatError("vpx: plane " + std::to_string(p) + " has " +
                               std::to_string(payload_len - consumed) + " trailing compressed bytes in " + path);
        }

        result.headers[p] = h;
        off += h.packed_size;
    }

    if (off != data.size()) throw FormatError("vpx: trailing VPX data in " + path);

    // Interleave the four decoded 16000-byte streams into a 64000-byte
    // (320x200) framebuffer: image[i*4+p] = plane[p][i].
    result.image.pixels.resize(kWidth * kHeight);
    for (int i = 0; i < kPlaneSize; ++i) {
        int j = i * 4;
        result.image.pixels[j + 0] = planes[0][i];
        result.image.pixels[j + 1] = planes[1][i];
        result.image.pixels[j + 2] = planes[2][i];
        result.image.pixels[j + 3] = planes[3][i];
    }

    return result;
}

}  // namespace gaius::formats::vpx
