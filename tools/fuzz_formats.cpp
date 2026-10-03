// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius CLI tool: fuzz_formats
//
// Feeds damaged files to every decoder and parser and checks that each either accepts the input or says so with a
// formats::FormatError. Anything else is a bug: a crash, a hang, an exception of another kind, or (under the sanitizers,
// `-DGAIUS_SANITIZE=address;undefined`) an out-of-range access or undefined behaviour. A player who points Gaius at a
// damaged download, a truncated save or a folder of another release must get a message, not a crash.
//
// Each target starts from seed files -- real ones from the game's folder (--assets) and from a folder of saves
// (--saves), plus small synthetic ones that are always there, so the tool means something in CI where no game files
// are -- and mutates them: bit flips, boundary values in 16- and 32-bit words, truncation, deleted, inserted and
// duplicated chunks. The mutations are deterministic from --seed, the target and the case number, so a failure is
// reproduced with `--only TARGET --from N --cases 1`; `--out DIR` writes each failing input.
//
// The targets:
//   vpx pl8 pl1 p32 pal256 empire2 exepack vas voc xmi gtl click_map markers    the file decoders (formats/)
//   save        a save: formats::save::parse, model::load, model::serialize
//   ail         an XMIDI file through the AIL music driver, a few seconds of it
//   settings catalog   gaius.cfg and a language file (text)
//   sim         a mutated save run for a few game months (the simulation), then every Forum and page the viewer
//               builds from it -- a corrupt save that loads must not crash the game that plays it
//   render      (with --assets) the same state drawn: the city view and the walkers
//
// Usage: fuzz_formats [--assets DIR] [--saves DIR] [--cases N] [--seed N] [--only NAME] [--from N] [--trace]
//                     [--stack] [--out DIR] [--hang-ms N] [--list]
// Exit status 0 when every case passed, 1 when any failed, 3 on a hang.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#undef min
#undef max
#undef RGB
#endif

#include "apps/viewer/screens.hpp"
#include "audio/ail_xmidi.hpp"
#include "formats/empire2/empire2.hpp"
#include "formats/exepack/exepack.hpp"
#include "formats/gtl/gtl.hpp"
#include "formats/p32/p32.hpp"
#include "formats/pal256/pal256.hpp"
#include "formats/pl8/pl8.hpp"
#include "formats/save/save.hpp"
#include "formats/screen_data/screen_data.hpp"
#include "formats/vas/vas.hpp"
#include "formats/voc/voc.hpp"
#include "formats/vpx/vpx.hpp"
#include "formats/xmi/xmi.hpp"
#include "model/city_state.hpp"
#include "render/city_render.hpp"
#include "systems/administration.hpp"
#include "systems/battle.hpp"
#include "systems/campaign.hpp"
#include "systems/construction.hpp"
#include "systems/forum.hpp"
#include "systems/month.hpp"
#include "ui/settings.hpp"
#include "ui/strings.hpp"

using namespace gaius;
namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<uint8_t>;

const char* g_phase = "";  // what a multi-step target was doing, for the failure line
int g_frame = 0;
uint64_t g_battles = 0, g_promotions = 0, g_notices = 0, g_year_ends = 0;  // the sim target's hooks, to show they run

// --- random numbers ----------------------------------------------------------------------------------------------

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next() {  // splitmix64
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    uint32_t below(uint32_t n) { return n ? static_cast<uint32_t>(next() % n) : 0; }
};

uint64_t hash_name(const std::string& name) {
    uint64_t h = 1469598103934665603ull;
    for (const char c : name) h = (h ^ static_cast<uint8_t>(c)) * 1099511628211ull;
    return h;
}

// --- mutation ----------------------------------------------------------------------------------------------------

enum class Style { Binary, Text, Save };

constexpr size_t kMaxBytes = size_t{8} << 20;
constexpr size_t kSaveSize = formats::save::kSaveSize;
constexpr size_t kSaveHeader = 0x13D4;  // the global words, walkers and record tables: where the structure is

void put_word(Bytes& v, size_t at, uint32_t value, int bytes) {
    for (int i = 0; i < bytes && at + static_cast<size_t>(i) < v.size(); ++i)
        v[at + static_cast<size_t>(i)] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t interesting(Rng& rng, size_t size, int bytes) {
    const uint32_t limit = bytes == 2 ? 0xFFFFu : 0xFFFFFFFFu;
    switch (rng.below(10)) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 0x7F;
        case 3: return 0x80;
        case 4: return limit;
        case 5: return limit / 2;
        case 6: return limit / 2 + 1;
        case 7: return static_cast<uint32_t>(size) & limit;
        case 8: return static_cast<uint32_t>(size + (rng.below(2) ? 1 : -1)) & limit;
        default: return static_cast<uint32_t>(rng.next()) & limit;
    }
}

void mutate(Bytes& v, Rng& rng, Style style) {
    static const char* const kText[] = {"=", "\n", "\r\n", "#", "key.", "button.", "# language = ", "999999999999", "-1",
                                        "0", "autosave_years", "config_version", "\xEF\xBB\xBF", " = ", "\t"};
    const int ops = 1 + static_cast<int>(rng.below(4));
    for (int k = 0; k < ops; ++k) {
        if (v.empty()) {
            v.push_back(static_cast<uint8_t>(rng.next()));
            continue;
        }
        // A save keeps its size (the loader takes exactly one size) and is mutated mostly where its structure is.
        const size_t span = style == Style::Save && rng.below(10) < 7 ? std::min(v.size(), kSaveHeader) : v.size();
        const size_t at = rng.below(static_cast<uint32_t>(span));
        uint32_t op = rng.below(style == Style::Save ? 7 : 10);
        if (style == Style::Save && op >= 5) {
            // Havoc, where the structure is: a whole walker record, or a window of the header, filled with noise.
            if (op == 5) {
                const size_t record = 0x100 + 50 * rng.below(70);
                for (size_t i = 0; i < 50; ++i) v[record + i] = static_cast<uint8_t>(rng.next());
            } else {
                const size_t window = rng.below(static_cast<uint32_t>(kSaveHeader - 64));
                for (uint32_t i = 0, n = 4 + rng.below(28); i < n; ++i) v[window + rng.below(64)] = static_cast<uint8_t>(rng.next());
            }
            continue;
        }
        switch (op) {
            case 0: v[at] ^= static_cast<uint8_t>(1u << rng.below(8)); break;
            case 1: v[at] = static_cast<uint8_t>(interesting(rng, v.size(), 2)); break;
            case 2: put_word(v, at, interesting(rng, v.size(), 2), 2); break;
            case 3: put_word(v, at, interesting(rng, v.size(), 4), 4); break;
            case 4: std::swap(v[at], v[rng.below(static_cast<uint32_t>(v.size()))]); break;
            case 5: v.resize(at ? at : 1); break;  // truncate
            case 6: {                               // delete a chunk
                const size_t n = std::min<size_t>(1 + rng.below(64), v.size() - at);
                v.erase(v.begin() + static_cast<long>(at), v.begin() + static_cast<long>(at + n));
                break;
            }
            case 7: {  // insert random bytes, or a text fragment
                Bytes ins;
                if (style == Style::Text) {
                    const std::string t = kText[rng.below(sizeof kText / sizeof kText[0])];
                    ins.assign(t.begin(), t.end());
                } else {
                    for (uint32_t i = 0, n = 1 + rng.below(32); i < n; ++i) ins.push_back(static_cast<uint8_t>(rng.next()));
                }
                if (v.size() + ins.size() <= kMaxBytes) v.insert(v.begin() + static_cast<long>(at), ins.begin(), ins.end());
                break;
            }
            case 8: {  // duplicate a chunk
                const size_t n = std::min<size_t>(1 + rng.below(256), v.size() - at);
                Bytes copy(v.begin() + static_cast<long>(at), v.begin() + static_cast<long>(at + n));
                if (v.size() + n <= kMaxBytes) v.insert(v.begin() + static_cast<long>(rng.below(static_cast<uint32_t>(v.size()))), copy.begin(), copy.end());
                break;
            }
            default: {  // a long run of one byte (an absurd count, a missing terminator)
                const size_t n = std::min<size_t>(1 + rng.below(512), v.size() - at);
                std::fill(v.begin() + static_cast<long>(at), v.begin() + static_cast<long>(at + n), static_cast<uint8_t>(rng.below(2) ? 0xFF : 0x00));
                break;
            }
        }
    }
    if (style == Style::Save) v.resize(kSaveSize);
}

// --- seeds ---------------------------------------------------------------------------------------------------------

Bytes read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Up to `limit` files of `dir` whose names end in one of `exts` (upper case, with the dot), the smallest first.
std::vector<Bytes> files_with(const std::string& dir, std::initializer_list<const char*> exts, size_t limit,
                              size_t max_size = size_t{1} << 20) {
    std::vector<std::pair<uintmax_t, fs::path>> found;
    std::error_code ec;
    if (dir.empty()) return {};
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string name = upper(it->path().filename().string());
        for (const char* ext : exts) {
            const std::string e = ext;
            if (name.size() > e.size() && name.compare(name.size() - e.size(), e.size(), e) == 0) {
                const uintmax_t size = it->file_size(ec);
                if (size <= max_size) found.push_back({size, it->path()});
                break;
            }
        }
    }
    std::sort(found.begin(), found.end());
    std::vector<Bytes> out;
    // Spread the pick over the sorted sizes rather than taking only the smallest.
    for (size_t i = 0; i < limit && !found.empty(); ++i) out.push_back(read_file(found[i * found.size() / limit].second));
    return out;
}

void be32(Bytes& v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<uint8_t>(x >> s));
}
void append(Bytes& v, const char* tag) { v.insert(v.end(), tag, tag + 4); }

Bytes synthetic_vpx() {
    Bytes out;
    for (int plane = 0; plane < 4; ++plane) {
        Bytes payload;
        for (int i = 0; i < 250; ++i) payload.push_back(i % 3 == 0 ? 0x7F : i % 3 == 1 ? 0xBF : 0x40 | 63);  // fills A and B, 64 each
        // 250 ops x 64 = 16000; the first two kinds are fills too
        const uint16_t packed = static_cast<uint16_t>(8 + payload.size());
        for (const uint16_t w : {packed, uint16_t{16000}, uint16_t{0x0201}, uint16_t{0}}) {
            out.push_back(static_cast<uint8_t>(w));
            out.push_back(static_cast<uint8_t>(w >> 8));
        }
        out.insert(out.end(), payload.begin(), payload.end());
    }
    return out;
}

Bytes synthetic_pl8(bool one_bit) {
    // Two 8x6 frames (the size of MINIFONT's), pixels after the descriptors.
    const int frames = 2, w = 8, h = 6;
    const size_t pixel_bytes = one_bit ? static_cast<size_t>(h) : static_cast<size_t>(w) * h;
    Bytes out = {0, 0, static_cast<uint8_t>(frames), 0};
    const size_t first = 4 + 8 * frames;
    for (int f = 0; f < frames; ++f) {
        const size_t at = first + f * pixel_bytes;
        out.push_back(static_cast<uint8_t>(at >> 8));
        out.push_back(static_cast<uint8_t>(at));
        out.push_back(w);
        out.push_back(h);
        for (int b : {0, 0, 0, 0}) out.push_back(static_cast<uint8_t>(b));
    }
    for (size_t i = 0; i < frames * pixel_bytes; ++i) out.push_back(static_cast<uint8_t>(i * 7));
    return out;
}

Bytes synthetic_palette(size_t n, int limit) {
    Bytes out(n);
    for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(i % limit);
    return out;
}

Bytes synthetic_voc() {
    const char magic[] = "Creative Voice File\x1A";
    Bytes out(magic, magic + 20);
    const uint16_t first = 26, version = 0x010A, check = static_cast<uint16_t>(~version + 0x1234);
    for (const uint16_t w : {first, version, check}) {
        out.push_back(static_cast<uint8_t>(w));
        out.push_back(static_cast<uint8_t>(w >> 8));
    }
    out.insert(out.end(), {1, 12, 0, 0, 0xA6, 0});  // a sound block: 10 samples at a rate
    for (int i = 0; i < 10; ++i) out.push_back(static_cast<uint8_t>(120 + i));
    out.insert(out.end(), {3, 3, 0, 0, 9, 0, 0xA6, 0});  // silence
    return out;
}

Bytes synthetic_vas() {
    Bytes out(0x18, 0);
    out[2] = 2;                 // two: one frame
    out[0x14] = 0x18;           // its offset
    for (int plane = 0; plane < 4; ++plane) {
        // length 10, 16000 bytes, then one run of 16000 (no XOR bytes)
        for (const uint8_t b : {10, 0, 0x80, 0x3E, 0, 0, 0, 0, 0x7F, 0x3E}) out.push_back(b);
    }
    return out;
}

Bytes synthetic_gtl() {
    Bytes out = {0, 0, 8, 0, 0, 0,  // patch 0, bank 0, the timbre at offset 8
                 0, 0xFF};          // the end entry
    out.push_back(14);
    out.push_back(0);
    for (int i = 0; i < 12; ++i) out.push_back(static_cast<uint8_t>(0x20 + i));
    return out;
}

Bytes synthetic_xmi() {
    // A note (duration 0x10), a delay, a controller, a delay, a program change, the end of the track.
    const Bytes events = {0x90, 60, 100, 0x10, 0x20, 0xB0, 7, 90, 0x10, 0xC0, 3, 0xFF, 0x2F, 0};
    Bytes timb = {1, 0, 3, 0};          // one timbre: patch 3, bank 0
    Bytes out;
    append(out, "FORM");
    be32(out, static_cast<uint32_t>(4 + 8 + timb.size() + 8 + events.size()));
    append(out, "XMID");
    append(out, "TIMB");
    be32(out, static_cast<uint32_t>(timb.size()));
    out.insert(out.end(), timb.begin(), timb.end());
    append(out, "EVNT");
    be32(out, static_cast<uint32_t>(events.size()));
    out.insert(out.end(), events.begin(), events.end());
    return out;
}

// A stub-less EXEPACK file that decodes: a 32-byte MZ header, 16 bytes of compressed image (4 raw, a 5-byte copy, a
// 7-byte fill), the 16-byte EXEPACK header and the "RB" signature. The decoded image is one paragraph.
Bytes synthetic_exepack() {
    Bytes out(32, 0);
    out[0] = 'M';
    out[1] = 'Z';
    out[8] = 2;  // two paragraphs of header
    const Bytes body = {'A', 'B', 'C', 'D',                                // raw prefix
                        'h', 'e', 'l', 'l', 'o', 5, 0, 0xB3,              // copy 5, the last record
                        0x00, 7, 0, 0xB0};                                // fill 7 with 0, not the last: processed first
    out.insert(out.end(), body.begin(), body.end());
    const uint16_t header[8] = {0, 0, 0, 18, 0, 0, 1, 1};  // real IP/CS, mem_start, exepack_size, SP/SS, dest_len, skip_len
    for (const uint16_t w : header) {
        out.push_back(static_cast<uint8_t>(w));
        out.push_back(static_cast<uint8_t>(w >> 8));
    }
    out.push_back('R');
    out.push_back('B');
    return out;
}

// A small city to start from: a new game, a province map of grass, roads and houses laid on open ground, a forum
// and a few services, run for a month (and again for six).
std::vector<Bytes> synthetic_cities() {
    auto state = std::make_unique<model::CityState>(model::blank_state());
    systems::month::Random random;
    systems::campaign::begin_new_game(*state, random, 0, 0);
    formats::empire2::EmpireMap map;
    int shore = 0;
    systems::campaign::start_province(*state, map, random, 0, shore);
    namespace construction = systems::construction;
    using construction::CommandId;
    // The widest open window of the city: scan for 28 x 14 cells every road and house can go on.
    int best_x = 30, best_y = 30, best_score = -1;
    for (int y = 10; y + 14 < 90; y += 4)
        for (int x = 10; x + 28 < 90; x += 4) {
            int score = 0;
            for (int dy = 0; dy < 14; dy += 2)
                for (int dx = 0; dx < 28; dx += 2) score += construction::can_place(state->city, CommandId::Housing, x + dx, y + dy);
            if (score > best_score) best_score = score, best_x = x, best_y = y;
        }
    construction::DragState drag;
    for (const int row : {0, 5, 10}) {
        drag = construction::DragState{};
        for (int dx = 0; dx < 28; ++dx) construction::place_road(state->city, drag, best_x + dx, best_y + row);
    }
    for (const int row : {1, 6})
        for (int dx = 0; dx < 28; ++dx)
            for (const int dy : {0, 1, 2}) construction::place(state->city, CommandId::Housing, best_x + dx, best_y + row + dy);
    for (int dx = 2; dx < 28; dx += 6) construction::place(state->city, CommandId::Well, best_x + dx, best_y + 4);
    construction::place_forum(*state, 1, best_x + 4, best_y + 11);
    construction::place_workshop(*state, 0, best_x + 12, best_y + 11);
    construction::place(state->city, CommandId::Market, best_x + 20, best_y + 11);
    construction::place(state->city, CommandId::Temple, best_x + 24, best_y + 11);
    systems::month::SimState sim = systems::month::sim_state_from_save(*state);
    sim.random = random;
    const auto census = [&](const char* when) {
        if (!std::getenv("GAIUS_FUZZ_DEBUG")) return;
        int houses = 0, roads = 0;
        for (const auto& row : state->city.tile)
            for (const uint8_t tile : row) {
                houses += tile >= 0xC8 && tile <= 0xD7;
                roads += tile >= 0x36 && tile < 0x8A;
            }
        std::printf("city %s: %d house cells, %d road-ish cells, population units %d, funds %d\n", when, houses, roads,
                    model::global_word(*state, 0x6C10), model::global_word(*state, 0x6CA2));
    };
    census("placed");
    // The houses a month in (tents with people), and the same city half a year on, when they have all been given up.
    systems::month::run_month(*state, sim);
    census("after a month");
    std::vector<Bytes> out{model::serialize(*state).raw};
    for (int m = 0; m < 5; ++m) systems::month::run_month(*state, sim);
    census("after six");
    out.push_back(model::serialize(*state).raw);
    return out;
}

// --- the targets -----------------------------------------------------------------------------------------------------

struct Target {
    std::string name;
    Style style = Style::Binary;
    std::vector<Bytes> seeds;
    std::function<void(const Bytes&)> run;
    bool needs_assets = false;
};

struct Options {
    std::string assets, saves;
};

// What a screen can be built from a state: every Forum page and the other pages the viewer shows.
void build_pages(const model::CityState& state) {
    for (int t = 0; t < viewer::kForumTabCount; ++t) {
        if (t == viewer::kIndustry) {
            model::CityState copy = state;
            systems::forum::open_industry_report(copy);
            (void)viewer::forum_page(copy, static_cast<viewer::ForumTab>(t), 100, 0);
        } else {
            (void)viewer::forum_page(state, static_cast<viewer::ForumTab>(t), 100, 0);
        }
    }
    (void)viewer::promotion_page(state, false);
    (void)viewer::promotion_page(state, true);
    (void)viewer::ending_page(state, true);
    (void)viewer::ending_page(state, false);
}

std::vector<Target> make_targets(const Options& opt, std::string& synthetic_note) {
    std::vector<Target> t;
    const auto add = [&](std::string name, Style style, std::vector<Bytes> seeds, std::function<void(const Bytes&)> run,
                         bool needs_assets = false) {
        Target x;
        x.name = std::move(name);
        x.style = style;
        x.seeds = std::move(seeds);
        x.run = std::move(run);
        x.needs_assets = needs_assets;
        t.push_back(std::move(x));
    };
    const auto with = [&](std::vector<Bytes> real, Bytes synthetic) {
        real.push_back(std::move(synthetic));
        return real;
    };
    const std::string& a = opt.assets;
    add("vpx", Style::Binary, with(files_with(a, {".VPX"}, 6, size_t{1} << 16), synthetic_vpx()),
        [](const Bytes& b) { (void)formats::vpx::parse(b); });
    add("pl8", Style::Binary, with(files_with(a, {".PL8"}, 6), synthetic_pl8(false)),
        [](const Bytes& b) { (void)formats::pl8::parse(b); });
    add("pl1", Style::Binary, with(files_with(a, {".PL1"}, 2), synthetic_pl8(true)),
        [](const Bytes& b) { (void)formats::pl8::parse_pl1(b); });
    add("p32", Style::Binary, with(files_with(a, {".P32"}, 2), synthetic_palette(64, 256)),
        [](const Bytes& b) { (void)formats::p32::parse(b); });
    add("pal256", Style::Binary, with(files_with(a, {".256"}, 3), synthetic_palette(768, 64)),
        [](const Bytes& b) { (void)formats::pal256::parse(b); });
    add("empire2", Style::Binary, with(files_with(a, {".001", ".002"}, 2), Bytes(formats::empire2::kFileSize, 1)),
        [](const Bytes& b) { (void)formats::empire2::parse(b); });
    add("exepack", Style::Binary, with(files_with(a, {".EXE"}, 1, size_t{2} << 20), synthetic_exepack()),
        [](const Bytes& b) { (void)formats::exepack::parse(b); });
    add("vas", Style::Binary, with(files_with(a, {".VAS"}, 2, size_t{4} << 20), synthetic_vas()), [](const Bytes& b) {
        const formats::vas::Animation anim = formats::vas::parse(b);
        formats::vas::Planes planes;
        for (auto& p : planes) p.assign(formats::vas::kPlaneSize, 0);
        for (size_t f = 0; f < anim.frame_offsets.size() && f < 4; ++f) formats::vas::apply_frame(anim, f, planes);
    });
    add("voc", Style::Binary, with(files_with(a, {".VOC"}, 4), synthetic_voc()),
        [](const Bytes& b) { (void)formats::voc::parse(b); });
    add("xmi", Style::Binary, with(files_with(a, {".XMI", ".XM2"}, 4), synthetic_xmi()), [](const Bytes& b) {
        const size_t n = formats::xmi::sequence_count(b);
        for (size_t i = 0; i < n && i < 4; ++i) (void)formats::xmi::to_midi(b, i);
    });
    add("gtl", Style::Binary, with(files_with(a, {".AD"}, 1), synthetic_gtl()),
        [](const Bytes& b) { (void)formats::gtl::parse(b); });
    add("click_map", Style::Binary, with(files_with(a, {".GD8"}, 1), Bytes(1000, 3)),
        [](const Bytes& b) { (void)formats::screen_data::parse_click_map(b); });
    add("markers", Style::Binary, with(files_with(a, {".CSR"}, 2), Bytes(320, 7)),
        [](const Bytes& b) { (void)formats::screen_data::parse_province_markers(b); });
    add("ail", Style::Binary, with(files_with(a, {".XMI", ".XM2"}, 4), synthetic_xmi()), [](const Bytes& b) {
        audio::AilXmidi driver([](uint8_t, uint8_t) {});
        driver.init();
        if (!driver.register_sequence(b)) return;
        const Bytes timbre = {14, 0, 0x21, 0x21, 0x20, 0x20, 0xF0, 0xF0, 0x77, 0x77, 0, 0, 0, 0};
        for (int guard = 0, r = driver.request(); r != -1 && guard < 300; r = driver.request(), ++guard)
            driver.install_timbre(static_cast<uint8_t>(r >> 8), static_cast<uint8_t>(r & 0xFF), timbre);
        driver.start();
        for (int i = 0; i < 1200; ++i) driver.serve();
        driver.stop();
        driver.release();
    });

    // Text.
    {
        ui::Settings s;
        s.keys["cycle_tool"] = "Q";
        s.buttons["menu"] = "guide";
        s.unknown["future"] = "1";
        const std::string text = ui::settings_text(s);
        add("settings", Style::Text, {Bytes(text.begin(), text.end())}, [](const Bytes& b) {
            const ui::Settings p = ui::parse_settings(std::string(b.begin(), b.end()));
            (void)ui::settings_text(p);
        });
        // The shipped language files, when the tool runs from a checkout.
        std::vector<Bytes> catalogs =
            files_with((fs::path(__FILE__).parent_path().parent_path() / "lang").string(), {"DE.TXT"}, 1, size_t{1} << 18);
        const std::string small = "# language = Test\nSave = Speichern\nLoad = Laden\nEmpty =  \nbroken\n";
        catalogs.push_back(Bytes(small.begin(), small.end()));
        add("catalog", Style::Text, catalogs, [](const Bytes& b) {
            const ui::Catalog c = ui::parse_catalog("xx", std::string(b.begin(), b.end()));
            ui::set_catalog(c);
            (void)ui::tr("Save");
            ui::set_catalog(ui::Catalog{"en", "English", {}});
        });
    }

    // Saves: real ones from --saves (and --assets/gaius_test_saves), else the synthetic city.
    std::vector<Bytes> saves = files_with(opt.saves, {".SAV"}, 6);
    if (saves.empty() && !opt.assets.empty()) saves = files_with((fs::path(opt.assets) / "gaius_test_saves").string(), {".SAV"}, 6);
    const std::vector<Bytes> cities = synthetic_cities();
    {
        const model::CityState probe = model::load(formats::save::parse(cities.front()));
        int houses = 0, built = 0;
        for (const auto& row : probe.city.tile)
            for (const uint8_t tile : row) {
                houses += tile >= 0xC8 && tile <= 0xD7;
                built += tile >= 0x36;
            }
        synthetic_note = "the synthetic city: " + std::to_string(4 * model::global_word(probe, 0x6C10)) + " people, " +
                         std::to_string(houses) + " house cells, " + std::to_string(built) + " built cells";
    }
    saves.insert(saves.end(), cities.begin(), cities.end());
    add("save", Style::Save, saves, [](const Bytes& b) {
        auto state = std::make_unique<model::CityState>(model::load(formats::save::parse(b)));
        (void)model::serialize(*state);
    });
    add("sim", Style::Save, saves, [](const Bytes& b) {
        g_phase = "load";
        auto state = std::make_unique<model::CityState>(model::load(formats::save::parse(b)));
        // Half the cases start in December, so the run crosses a year's end: the yearly accounts, the histories, the
        // ratings, promotion, the Legion and the notice all run.
        if (b[0x2000] & 1) model::set_global_word(*state, 0x6C1C, 11);
        systems::month::SimState sim = systems::month::sim_state_from_save(*state);
        sim.speed = 100;
        // The hooks the viewer installs, doing what it does: a battle is fought through its four tactics and its
        // screen built, a promotion is accepted, deferred or taken to Caesar in turn, a notice is read.
        int answers = 0;
        sim.on_battle = [&](model::CityState& st, int cohort, int army) {
            ++g_battles;
            viewer::BattleView view;
            view.cohort = cohort;
            view.army = army;
            for (const auto tactic : {systems::battle::Tactic::Tortoise, systems::battle::Tactic::Assault,
                                      systems::battle::Tactic::Flank, systems::battle::Tactic::Charge}) {
                view.last = systems::battle::fight_round(st, cohort, army, tactic, sim.random);
                view.has_round = true;
                (void)viewer::battle_page(st, view);
            }
            if (answers++ % 2) {
                systems::battle::retreat(st, cohort);
                view.retreated = true;
                (void)viewer::battle_page(st, view);
            }
        };
        sim.on_promotion = [&](model::CityState& st, bool to_caesar) {
            ++g_promotions;
            namespace admin = systems::administration;
            switch (answers++ % 3) {
                case 0: if (to_caesar) admin::become_caesar(st); else admin::accept_promotion(st, sim.difficulty); break;
                case 1: admin::defer_promotion(st, answers % 2 ? 9 : 24); break;
                default: break;
            }
            return 0;
        };
        sim.on_notice = [&](int, int, bool) {
            ++answers;
            ++g_notices;
        };
        g_phase = "frames";
        const int year_before = sim.year;
        for (int frame = 0; frame < 500; ++frame) {
            g_frame = frame;
            if (!systems::month::run_frame(*state, sim)) break;
        }
        if (sim.year != year_before) ++g_year_ends;
        // Battles are rare in a few months of play; fight some on the state as it stands: the first Cohort (type 13)
        // and army (type 11) in the walker table, else slots the file's bytes name -- possibly none at all.
        g_phase = "battle";
        {
            int cohort = b[0x2001] % 72 - 1, army = b[0x2002] % 72 - 1;
            for (int slot = 0; slot < model::kActorCount; ++slot) {
                const model::Actor& a = state->objects[static_cast<size_t>(slot)];
                if (a.active() && a.type() == 13 && (b[0x2003] & 1)) cohort = slot;
                if (a.active() && a.type() == 11 && (b[0x2003] & 2)) army = slot;
            }
            for (const auto tactic : {systems::battle::Tactic::Tortoise, systems::battle::Tactic::Assault,
                                      systems::battle::Tactic::Flank, systems::battle::Tactic::Charge})
                (void)systems::battle::fight_round(*state, cohort, army, tactic, sim.random);
            systems::battle::hand_over(*state, cohort, army);
            systems::battle::take_back(*state);
            systems::battle::retreat(*state, cohort);
        }
        // A promotion on the state as it stands (the yearly routine reaches this only when the ratings earn it), at a
        // rank the file's bytes name -- possibly one the game has no requirements for.
        g_phase = "promotion";
        {
            namespace admin = systems::administration;
            for (const uint16_t ds : {admin::kPeace, admin::kCulture, admin::kProsperity, admin::kEmpire, admin::kAverage})
                model::set_global_word(*state, ds, 100);
            model::set_global_word(*state, admin::kRank, b[0x2004] % 24);
            const admin::Offer offer = admin::check_promotion(*state, sim.random);
            if (offer == admin::Offer::Promotion) admin::accept_promotion(*state, sim.difficulty);
            else if (offer == admin::Offer::Caesar) admin::become_caesar(*state);
            (void)admin::yearly_notice(*state, sim.random, 0, b[0x2005] % 6);
            ++g_year_ends;
        }
        g_phase = "pages";
        build_pages(*state);
        g_phase = "serialize";
        (void)model::serialize(*state);
    });
    if (!opt.assets.empty()) {
        auto sprites = std::make_shared<render::CitySprites>();
        bool ok = true;
        try {
            *sprites = render::load_city_sprites(opt.assets);
        } catch (const std::exception&) {
            ok = false;
        }
        if (ok)
            add("render", Style::Save, saves, [sprites](const Bytes& b) {
                auto state = std::make_unique<model::CityState>(model::load(formats::save::parse(b)));
                systems::month::SimState sim = systems::month::sim_state_from_save(*state);
                for (int frame = 0; frame < 60; ++frame) systems::month::run_frame(*state, sim);
                render::RenderPhase phase;
                phase.ticks = sim.ticks;
                phase.population_units = model::global_word(*state, 0x6C10);
                phase.housing_land_value_base = model::global_word(*state, 0x6BF8);
                phase.workshop_records = &state->table_720;
                phase.barracks_records = &state->table_120;
                formats::IndexedImage out;
                for (const int c0 : {0, 40, 70})
                    render::render_city(state->city, *sprites, c0, c0 / 2, 30, 20, out, phase, &state->objects);
            }, true);
    }
    return t;
}


// --stack: print where every C++ exception is thrown (Windows; the debugging aid for a case that fails with an exception
// of the wrong kind -- run it with --only TARGET --from N --cases 1). Elsewhere a debugger's "break on throw" does it.
#if defined(_WIN32)
LONG WINAPI print_throw_site(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != 0xE06D7363) return EXCEPTION_CONTINUE_SEARCH;
    void* frames[40];
    const USHORT n = CaptureStackBackTrace(0, 40, frames, nullptr);
    HANDLE process = GetCurrentProcess();
    char buffer[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    std::fprintf(stderr, "  thrown at:\n");
    for (USHORT i = 0; i < n && i < 14; ++i) {
        if (SymFromAddr(process, reinterpret_cast<DWORD64>(frames[i]), nullptr, symbol))
            std::fprintf(stderr, "    %s\n", symbol->Name);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
void enable_throw_stacks() {
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    AddVectoredExceptionHandler(1, print_throw_site);
}
#else
void enable_throw_stacks() {}
#endif

// --- the run -----------------------------------------------------------------------------------------------------------

std::atomic<uint64_t> g_case_started_ms{0};
std::string g_where;

uint64_t now_ms() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch()).count());
}

void write_input(const std::string& dir, const std::string& target, uint64_t index, const Bytes& b) {
    if (dir.empty()) return;
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::ofstream f(fs::path(dir) / (target + "_" + std::to_string(index) + ".bin"), std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    uint64_t cases = 1500, seed = 1, from = 0;
    uint64_t hang_ms = 30000;
    std::string only, out_dir;
    bool trace = false, list = false, stack = false;
    for (int i = 1; i < argc; ++i) {
        const auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (!std::strcmp(argv[i], "--assets")) opt.assets = value();
        else if (!std::strcmp(argv[i], "--saves")) opt.saves = value();
        else if (!std::strcmp(argv[i], "--cases")) cases = std::strtoull(value(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--seed")) seed = std::strtoull(value(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--from")) from = std::strtoull(value(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--only")) only = value();
        else if (!std::strcmp(argv[i], "--out")) out_dir = value();
        else if (!std::strcmp(argv[i], "--hang-ms")) hang_ms = std::strtoull(value(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--trace")) trace = true;
        else if (!std::strcmp(argv[i], "--stack")) stack = true;
        else if (!std::strcmp(argv[i], "--list")) list = true;
        else {
            std::fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }

    std::string note;
    std::vector<Target> targets = make_targets(opt, note);
    if (stack) enable_throw_stacks();

    // The synthetic seeds are the ones CI relies on: each must be accepted, or the mutations never get past the header.
    int broken = 0;
    for (const Target& t : targets) {
        if (t.seeds.empty() || t.needs_assets) continue;
        try {
            t.run(t.seeds.back());
        } catch (const std::exception& e) {
            std::printf("synthetic seed for %s is not accepted: %s\n", t.name.c_str(), e.what());
            ++broken;
        }
    }
    if (broken) return 2;

    if (list) {
        std::printf("%s\n", note.c_str());
        for (const Target& t : targets) std::printf("%-10s %zu seeds%s\n", t.name.c_str(), t.seeds.size(), t.needs_assets ? " (needs --assets)" : "");
        return 0;
    }

    // A watchdog: one case that runs past --hang-ms is a hang; say which and stop. (Not under Emscripten, which has
    // no threads here: a hang there is the run's timeout.)
    std::atomic<bool> done{false};
#if !defined(__EMSCRIPTEN__)
    std::thread watchdog([&] {
        while (!done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const uint64_t started = g_case_started_ms.load();
            if (started && now_ms() - started > hang_ms) {
                std::fprintf(stderr, "HANG: %s ran longer than %llu ms\n", g_where.c_str(), static_cast<unsigned long long>(hang_ms));
                std::fflush(stderr);
                std::_Exit(3);
            }
        }
    });
#endif

    uint64_t failures = 0, accepted = 0, rejected = 0, slow = 0;
    for (Target& t : targets) {
        if (!only.empty() && t.name != only) continue;
        if (t.seeds.empty()) continue;
        uint64_t t_failures = 0, t_accepted = 0;
        const uint64_t t_start = now_ms();
        for (uint64_t c = from; c < from + cases; ++c) {
            Rng rng(seed * 0x9E3779B97F4A7C15ull ^ hash_name(t.name) ^ (c * 0xD1B54A32D192ED03ull));
            Bytes input = t.seeds[c % t.seeds.size()];
            if (c >= t.seeds.size()) mutate(input, rng, t.style);  // the first cases are the seeds themselves
            g_where = t.name + " case " + std::to_string(c);
            if (trace) {
                std::fprintf(stderr, "%s\n", g_where.c_str());
                std::fflush(stderr);
            }
            g_case_started_ms = now_ms();
            const uint64_t began = g_case_started_ms.load();
            std::string problem;
            try {
                t.run(input);
                ++t_accepted;
            } catch (const formats::FormatError&) {
                ++rejected;
            } catch (const std::exception& e) {
                problem = std::string("unexpected ") + typeid(e).name() + ": " + e.what() + " [" + g_phase + " " + std::to_string(g_frame) + "]";
            } catch (...) {
                problem = "unexpected exception of unknown type";
            }
            g_case_started_ms = 0;
            if (now_ms() - began > 3000) {
                ++slow;
                std::printf("SLOW  %s: %llu ms\n", g_where.c_str(), static_cast<unsigned long long>(now_ms() - began));
            }
            if (!problem.empty()) {
                ++t_failures;
                std::printf("FAIL  %s: %s\n", g_where.c_str(), problem.c_str());
                write_input(out_dir, t.name, c, input);
                if (t_failures >= 10) {
                    std::printf("      (stopping %s after 10 failures)\n", t.name.c_str());
                    break;
                }
            }
        }
        accepted += t_accepted;
        failures += t_failures;
        std::printf("%-10s %6llu cases, %llu accepted, %llu failed  (%.1f s)\n", t.name.c_str(), static_cast<unsigned long long>(cases),
                    static_cast<unsigned long long>(t_accepted), static_cast<unsigned long long>(t_failures), (now_ms() - t_start) / 1000.0);
        std::fflush(stdout);
    }
    done = true;
#if !defined(__EMSCRIPTEN__)
    watchdog.join();
#endif
    std::printf("sim hooks run: %llu battles, %llu promotion offers, %llu notices; %llu years ended\n", static_cast<unsigned long long>(g_battles),
                static_cast<unsigned long long>(g_promotions), static_cast<unsigned long long>(g_notices),
                static_cast<unsigned long long>(g_year_ends));
    std::printf("%s\n%llu failed, %llu slow (%llu accepted, %llu rejected with a FormatError)\n", note.c_str(),
                static_cast<unsigned long long>(failures), static_cast<unsigned long long>(slow),
                static_cast<unsigned long long>(accepted), static_cast<unsigned long long>(rejected));
    return failures ? 1 : 0;
}
