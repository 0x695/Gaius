// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/gtl/gtl.hpp"

#include <cstdio>

namespace gaius::formats::gtl {

std::vector<Timbre> parse(const std::vector<uint8_t>& d) {
    std::vector<Timbre> out;
    for (size_t i = 0;; i += 6) {
        if (i + 2 > d.size()) throw FormatError("gtl: the directory has no end entry");
        if (d[i + 1] == 0xFF) break;
        if (i + 6 > d.size()) throw FormatError("gtl: truncated directory entry");
        const size_t offset = d[i + 2] | (d[i + 3] << 8) | (d[i + 4] << 16) | (static_cast<size_t>(d[i + 5]) << 24);
        if (offset + 2 > d.size()) throw FormatError("gtl: a timbre offset is past the file");
        const size_t length = d[offset] | (d[offset + 1] << 8);
        if (length < 2 || offset + length > d.size()) throw FormatError("gtl: a timbre runs past the file");
        Timbre t;
        t.patch = d[i];
        t.bank = d[i + 1];
        t.data.assign(d.begin() + static_cast<long>(offset), d.begin() + static_cast<long>(offset + length));
        out.push_back(std::move(t));
    }
    return out;
}

std::vector<Timbre> load(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw FormatError("gtl: cannot open " + path);
    std::vector<uint8_t> data;
    uint8_t buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return parse(data);
}

}  // namespace gaius::formats::gtl
