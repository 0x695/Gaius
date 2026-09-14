// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/voc/voc.hpp"

#include <cstdio>
#include <cstring>

namespace gaius::formats::voc {

namespace {

constexpr char kMagic[] = "Creative Voice File\x1A";

}  // namespace

Sound parse(const std::vector<uint8_t>& d) {
    constexpr size_t kMagicLen = sizeof(kMagic) - 1;
    if (d.size() < 26 || std::memcmp(d.data(), kMagic, kMagicLen) != 0) throw FormatError("voc: not a Creative Voice File");
    const uint16_t first = static_cast<uint16_t>(d[20] | (d[21] << 8));
    const uint16_t version = static_cast<uint16_t>(d[22] | (d[23] << 8));
    const uint16_t check = static_cast<uint16_t>(d[24] | (d[25] << 8));
    if (static_cast<uint16_t>(~version + 0x1234) != check) throw FormatError("voc: header check word is wrong");
    Sound s;
    size_t i = first;
    while (true) {
        if (i >= d.size()) throw FormatError("voc: no end block");
        const uint8_t type = d[i];
        if (type == 0) break;
        if (i + 4 > d.size()) throw FormatError("voc: truncated block header");
        const size_t length = d[i + 1] | (d[i + 2] << 8) | (d[i + 3] << 16);
        const size_t body = i + 4;
        if (body + length > d.size()) throw FormatError("voc: block runs past the file");
        switch (type) {
            case 1: {
                if (length < 2) throw FormatError("voc: short sound block");
                const int rate = 1000000 / (256 - d[body]);
                if (d[body + 1] != 0) throw FormatError("voc: only 8-bit PCM is supported");
                if (s.sample_rate != 0 && s.sample_rate != rate) throw FormatError("voc: the rate changes mid-file");
                s.sample_rate = rate;
                s.samples.insert(s.samples.end(), d.begin() + static_cast<long>(body + 2),
                                 d.begin() + static_cast<long>(body + length));
                break;
            }
            case 2:
                s.samples.insert(s.samples.end(), d.begin() + static_cast<long>(body),
                                 d.begin() + static_cast<long>(body + length));
                break;
            case 3: {
                if (length < 3) throw FormatError("voc: short silence block");
                const size_t n = static_cast<size_t>(d[body] | (d[body + 1] << 8)) + 1;
                if (s.sample_rate == 0) s.sample_rate = 1000000 / (256 - d[body + 2]);
                s.samples.insert(s.samples.end(), n, 128);
                break;
            }
            case 4:
            case 5:
            case 6:
            case 7: break;  // markers, text and repeats carry no samples
            default: throw FormatError("voc: block type " + std::to_string(type) + " isn't supported");
        }
        i = body + length;
    }
    if (s.sample_rate == 0) throw FormatError("voc: no sound");
    return s;
}

Sound load(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw FormatError("voc: cannot open " + path);
    std::vector<uint8_t> data;
    uint8_t buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return parse(data);
}

}  // namespace gaius::formats::voc
