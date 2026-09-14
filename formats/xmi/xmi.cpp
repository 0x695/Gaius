// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/xmi/xmi.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace gaius::formats::xmi {

namespace {

uint32_t be32(const std::vector<uint8_t>& d, size_t i) {
    if (i + 4 > d.size()) throw FormatError("xmi: truncated chunk");
    return (static_cast<uint32_t>(d[i]) << 24) | (d[i + 1] << 16) | (d[i + 2] << 8) | d[i + 3];
}

bool tag(const std::vector<uint8_t>& d, size_t i, const char* t) {
    return i + 4 <= d.size() && std::memcmp(&d[i], t, 4) == 0;
}

// The EVNT chunks, in file order.
std::vector<std::pair<size_t, size_t>> event_chunks(const std::vector<uint8_t>& d) {
    std::vector<std::pair<size_t, size_t>> out;
    // Walk every chunk recursively: FORM and CAT hold a type then sub-chunks.
    std::vector<std::pair<size_t, size_t>> stack{{0, d.size()}};
    while (!stack.empty()) {
        auto [pos, end] = stack.back();
        stack.pop_back();
        std::vector<std::pair<size_t, size_t>> children;
        while (pos + 8 <= end) {
            const size_t length = be32(d, pos + 4);
            const size_t body = pos + 8;
            // CZARTIT.XMI has 12 bytes of padding after its last chunk: a
            // "chunk" that doesn't fit its container ends the walk.
            if (body + length > end) break;
            if (tag(d, pos, "FORM") || tag(d, pos, "CAT ")) {
                children.push_back({body + 4, body + length});
            } else if (tag(d, pos, "EVNT")) {
                children.push_back({body, body + length | (size_t{1} << 62)});
            }
            pos = body + length + (length & 1);
        }
        // Keep file order: push in reverse so the first child is handled first.
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if (it->second & (size_t{1} << 62)) {
                out.push_back({it->first, it->second & ~(size_t{1} << 62)});
            } else {
                stack.push_back(*it);
            }
        }
    }
    // The stack visits containers after their EVNT siblings; sort by position.
    std::sort(out.begin(), out.end());
    return out;
}

size_t read_vlq(const std::vector<uint8_t>& d, size_t& i, size_t end) {
    size_t v = 0;
    for (int k = 0; k < 4; ++k) {
        if (i >= end) throw FormatError("xmi: truncated number");
        const uint8_t b = d[i++];
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) return v;
    }
    throw FormatError("xmi: number too long");
}

void write_vlq(std::vector<uint8_t>& out, size_t v) {
    uint8_t buf[5];
    int n = 0;
    buf[n++] = static_cast<uint8_t>(v & 0x7F);
    while (v >>= 7) buf[n++] = static_cast<uint8_t>((v & 0x7F) | 0x80);
    while (n) out.push_back(buf[--n]);
}

struct Event {
    size_t time;
    int order;  // note-offs before other events at the same time
    size_t seq;
    std::vector<uint8_t> bytes;
};

}  // namespace

size_t sequence_count(const std::vector<uint8_t>& xmi) { return event_chunks(xmi).size(); }

std::vector<uint8_t> to_midi(const std::vector<uint8_t>& d, size_t sequence) {
    const auto chunks = event_chunks(d);
    if (sequence >= chunks.size()) throw FormatError("xmi: no such sequence");
    auto [i, end] = chunks[sequence];

    std::vector<Event> events;
    size_t now = 0, seq = 0;
    while (i < end) {
        uint8_t b = d[i];
        if (b < 0x80) {
            while (i < end && d[i] < 0x80) now += d[i++];
            continue;
        }
        ++i;
        const uint8_t kind = b & 0xF0;
        if (kind >= 0x80 && kind <= 0xE0) {
            const int size = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
            if (i + size > end) throw FormatError("xmi: truncated channel event");
            Event e{now, 1, seq++, {b, d[i]}};
            if (size == 2) e.bytes.push_back(d[i + 1]);
            i += size;
            if (kind == 0x90) {
                const size_t duration = read_vlq(d, i, end);
                events.push_back({now + duration, 0, seq++, {static_cast<uint8_t>(0x80 | (b & 0x0F)), e.bytes[1], 0}});
            }
            events.push_back(std::move(e));
        } else if (b == 0xF0 || b == 0xF7) {
            const size_t length = read_vlq(d, i, end);
            if (i + length > end) throw FormatError("xmi: truncated sysex");
            Event e{now, 1, seq++, {b}};
            write_vlq(e.bytes, length);
            e.bytes.insert(e.bytes.end(), d.begin() + static_cast<long>(i), d.begin() + static_cast<long>(i + length));
            events.push_back(std::move(e));
            i += length;
        } else if (b == 0xFF) {
            if (i >= end) throw FormatError("xmi: truncated meta event");
            const uint8_t type = d[i++];
            const size_t length = read_vlq(d, i, end);
            if (i + length > end) throw FormatError("xmi: truncated meta event");
            if (type == 0x2F) break;  // end of track
            if (type != 0x51) {       // the driver plays at its fixed rate
                Event e{now, 1, seq++, {0xFF, type}};
                write_vlq(e.bytes, length);
                e.bytes.insert(e.bytes.end(), d.begin() + static_cast<long>(i),
                               d.begin() + static_cast<long>(i + length));
                events.push_back(std::move(e));
            }
            i += length;
        } else {
            throw FormatError("xmi: unexpected status byte");
        }
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        if (a.time != b.time) return a.time < b.time;
        if (a.order != b.order) return a.order < b.order;
        return a.seq < b.seq;
    });

    std::vector<uint8_t> track = {0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20};  // 500000 us a quarter note
    size_t last = 0;
    for (const Event& e : events) {
        write_vlq(track, e.time - last);
        last = e.time;
        track.insert(track.end(), e.bytes.begin(), e.bytes.end());
    }
    track.insert(track.end(), {0x00, 0xFF, 0x2F, 0x00});

    std::vector<uint8_t> out = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 60, 'M', 'T', 'r', 'k'};
    const size_t n = track.size();
    out.insert(out.end(), {static_cast<uint8_t>(n >> 24), static_cast<uint8_t>(n >> 16), static_cast<uint8_t>(n >> 8),
                           static_cast<uint8_t>(n)});
    out.insert(out.end(), track.begin(), track.end());
    return out;
}

std::vector<uint8_t> read_file(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw FormatError("xmi: cannot open " + path);
    std::vector<uint8_t> data;
    uint8_t buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return data;
}

}  // namespace gaius::formats::xmi
