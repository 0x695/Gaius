// Gaius Phase 0 test harness — no external test framework, deliberately.
//
// Per GAIUS_MASTERPLAN.md's testing strategy, this is corpus-driven: the
// repo ships no game assets (IP posture — see masterplan section 3), so
// any test needing real Caesar files reads them from a directory named by
// the GAIUS_TEST_ASSETS environment variable and SKIPS (not fails) if
// that variable isn't set. Pure structural/synthetic tests always run.
//
// Run with:  GAIUS_TEST_ASSETS=/path/to/your/caesar/files ./gaius_tests

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "formats/empire2/empire2.hpp"
#include "formats/exepack/exepack.hpp"
#include "formats/p32/p32.hpp"
#include "formats/pal256/pal256.hpp"
#include "formats/pl8/pl8.hpp"
#include "formats/save/save.hpp"
#include "formats/vpx/vpx.hpp"
#include "stb_image.h"

namespace fs = std::filesystem;
using namespace gaius::formats;

namespace {

int g_failures = 0;
int g_ran = 0;
int g_skipped = 0;

#define CHECK(cond)                                                                         \
    do {                                                                                    \
        ++g_ran;                                                                            \
        if (!(cond)) {                                                                      \
            ++g_failures;                                                                   \
            std::fprintf(stderr, "  FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);        \
        }                                                                                    \
    } while (0)

void skip(const std::string& reason) {
    ++g_skipped;
    std::printf("  SKIP: %s\n", reason.c_str());
}

std::string test_assets_dir() {
    const char* env = std::getenv("GAIUS_TEST_ASSETS");
    return env ? std::string(env) : std::string();
}

std::vector<uint8_t> read_whole_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------------
// Pure / synthetic tests — no external assets needed.
// ---------------------------------------------------------------------

void test_save_block_table_contiguous() {
    std::printf("test_save_block_table_contiguous\n");
    auto blocks = save::block_table();
    size_t expected_offset = 0;
    for (const auto& b : blocks) {
        CHECK(b.offset == expected_offset);
        expected_offset += b.size;
    }
    CHECK(expected_offset == save::kSaveSize);
}

void test_p32_expand_math() {
    std::printf("test_p32_expand_math\n");
    // Two packed colors: value 0x0000 (all nibbles 0) and 0x0FFF is invalid
    // (16 bits total; layout is R=bits0-3, unused=4-7, B=8-11, G=12-15).
    // Build: color0 = R=0xF,unused=0,B=0,G=0 -> pure red at max.
    //        color1 = R=0,unused=0,B=0xF,G=0xF -> full blue+green (cyan).
    std::vector<uint8_t> buf(64, 0);
    uint16_t c0 = 0x000F;  // R=0xF
    uint16_t c1 = 0xF000;  // G=0xF (bits12-15), B=0
    uint16_t c2 = 0x0F00;  // B=0xF (bits8-11), G=0
    buf[0] = c0 & 0xFF; buf[1] = (c0 >> 8) & 0xFF;
    buf[2] = c1 & 0xFF; buf[3] = (c1 >> 8) & 0xFF;
    buf[4] = c2 & 0xFF; buf[5] = (c2 >> 8) & 0xFF;

    std::string tmp = (fs::temp_directory_path() / "gaius_test.p32").string();
    { std::ofstream out(tmp, std::ios::binary); out.write(reinterpret_cast<char*>(buf.data()), buf.size()); }

    Palette pal = p32::load(tmp);
    CHECK(pal.filled_count == 32);
    CHECK(pal.colors[0].r == 255 && pal.colors[0].g == 0 && pal.colors[0].b == 0);
    CHECK(pal.colors[1].r == 0 && pal.colors[1].g == 255 && pal.colors[1].b == 0);
    CHECK(pal.colors[2].r == 0 && pal.colors[2].g == 0 && pal.colors[2].b == 255);
    fs::remove(tmp);
}

void test_pal256_expand_math() {
    std::printf("test_pal256_expand_math\n");
    std::vector<uint8_t> buf(768, 0);
    // color0 = (63,0,0) -> should expand to (255,0,0) since (63<<2)|(63>>4) = 252|3 = 255
    buf[0] = 63; buf[1] = 0; buf[2] = 0;
    // color1 = (0,32,16)
    buf[3] = 0; buf[4] = 32; buf[5] = 16;

    std::string tmp = (fs::temp_directory_path() / "gaius_test.256").string();
    { std::ofstream out(tmp, std::ios::binary); out.write(reinterpret_cast<char*>(buf.data()), buf.size()); }

    Palette pal = pal256::load(tmp);
    CHECK(pal.filled_count == 256);
    CHECK(pal.colors[0].r == 255 && pal.colors[0].g == 0 && pal.colors[0].b == 0);
    uint8_t expected_g = static_cast<uint8_t>((32 << 2) | (32 >> 4));
    uint8_t expected_b = static_cast<uint8_t>((16 << 2) | (16 >> 4));
    CHECK(pal.colors[1].g == expected_g);
    CHECK(pal.colors[1].b == expected_b);
    fs::remove(tmp);
}

void test_pl8_synthetic() {
    std::printf("test_pl8_synthetic\n");
    // header: unknown_a=0, frame_count=2
    // frame0: pixel_offset=20 (BE), w=2, h=2, x=0, y=0 -> 4 pixel bytes at offset 20
    // frame1: pixel_offset=24 (BE), w=1, h=3, x=5, y=7 -> 3 pixel bytes at offset 24
    std::vector<uint8_t> buf;
    auto push_u16le = [&](uint16_t v) { buf.push_back(v & 0xFF); buf.push_back((v >> 8) & 0xFF); };
    auto push_u16be = [&](uint16_t v) { buf.push_back((v >> 8) & 0xFF); buf.push_back(v & 0xFF); };

    push_u16le(0);   // unknown_a
    push_u16le(2);   // frame_count

    push_u16be(20); buf.push_back(2); buf.push_back(2); push_u16le(0); push_u16le(0);   // frame0 descriptor
    push_u16be(24); buf.push_back(1); buf.push_back(3); push_u16le(5); push_u16le(7);   // frame1 descriptor

    while (buf.size() < 20) buf.push_back(0xAA);  // padding up to pixel_offset 20
    buf.insert(buf.end(), {11, 22, 33, 44});       // frame0 pixels (2x2)
    buf.insert(buf.end(), {55, 66, 77});           // frame1 pixels (1x3)

    std::string tmp = (fs::temp_directory_path() / "gaius_test.pl8").string();
    { std::ofstream out(tmp, std::ios::binary); out.write(reinterpret_cast<char*>(buf.data()), buf.size()); }

    PL8Sheet sheet = pl8::load(tmp);
    CHECK(sheet.frames.size() == 2);
    if (sheet.frames.size() == 2) {
        CHECK(sheet.frames[0].width == 2 && sheet.frames[0].height == 2);
        CHECK(sheet.frames[0].x == 0 && sheet.frames[0].y == 0);
        CHECK((sheet.frames[0].pixels == std::vector<uint8_t>{11, 22, 33, 44}));

        CHECK(sheet.frames[1].width == 1 && sheet.frames[1].height == 3);
        CHECK(sheet.frames[1].x == 5 && sheet.frames[1].y == 7);
        CHECK((sheet.frames[1].pixels == std::vector<uint8_t>{55, 66, 77}));
    }
    fs::remove(tmp);
}

void test_format_errors_are_thrown_not_swallowed() {
    std::printf("test_format_errors_are_thrown_not_swallowed\n");
    bool threw = false;
    try {
        p32::load("/nonexistent/path/does/not/exist.p32");
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

// ---------------------------------------------------------------------
// Corpus tests — require GAIUS_TEST_ASSETS, skip otherwise.
// ---------------------------------------------------------------------

void test_empire2_corpus_roundtrip() {
    std::printf("test_empire2_corpus_roundtrip (all supplied EMPIRE2.0xx)\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }

    int found = 0;
    for (int i = 0; i <= 49; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "EMPIRE2.%03d", i);
        fs::path p = fs::path(dir) / name;
        if (!fs::exists(p)) continue;
        ++found;

        std::vector<uint8_t> original = read_whole_file(p.string());
        CHECK(original.size() == empire2::kFileSize);

        empire2::EmpireMap map = empire2::load(p.string());
        std::string tmp_out = (fs::temp_directory_path() / (std::string(name) + ".roundtrip")).string();
        empire2::save(map, tmp_out);
        std::vector<uint8_t> roundtripped = read_whole_file(tmp_out);

        CHECK(roundtripped == original);
        fs::remove(tmp_out);
    }
    std::printf("  (%d/50 EMPIRE2 files found under %s)\n", found, dir.c_str());
    CHECK(found > 0);
}

void test_vpx_golden_image() {
    std::printf("test_vpx_golden_image (EMAP2.VPX + EMAP2.P32 vs EMAP2_decoded.png)\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }

    fs::path vpx_path = fs::path(dir) / "EMAP2.VPX";
    fs::path p32_path = fs::path(dir) / "EMAP2.P32";
    fs::path ref_path = fs::path(dir) / "EMAP2_decoded.png";

    if (!fs::exists(vpx_path) || !fs::exists(p32_path) || !fs::exists(ref_path)) {
        skip("EMAP2.VPX / EMAP2.P32 / EMAP2_decoded.png not all found under " + dir);
        return;
    }

    vpx::DecodeResult result = vpx::decode(vpx_path.string());
    Palette pal = p32::load(p32_path.string());
    CHECK(result.image.width == 320 && result.image.height == 200);
    for (const auto& h : result.headers) CHECK(h.decoded_size == vpx::kPlaneSize);

    int ref_w, ref_h, ref_channels;
    unsigned char* ref_pixels = stbi_load(ref_path.string().c_str(), &ref_w, &ref_h, &ref_channels, 3);
    CHECK(ref_pixels != nullptr);
    if (!ref_pixels) return;

    CHECK(ref_w == result.image.width);
    CHECK(ref_h == result.image.height);

    size_t mismatches = 0;
    size_t total = static_cast<size_t>(result.image.width) * result.image.height;
    for (size_t i = 0; i < total; ++i) {
        uint8_t idx = result.image.pixels[i];
        RGB c = pal.colors[idx];
        unsigned char rr = ref_pixels[i * 3 + 0];
        unsigned char rg = ref_pixels[i * 3 + 1];
        unsigned char rb = ref_pixels[i * 3 + 2];
        if (c.r != rr || c.g != rg || c.b != rb) ++mismatches;
    }
    stbi_image_free(ref_pixels);

    std::printf("  pixel mismatches: %zu / %zu\n", mismatches, total);
    CHECK(mismatches == 0);
}

void test_pl8_corpus_sanity() {
    std::printf("test_pl8_corpus_sanity (HOUSES.PL8 documented worked example)\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }

    fs::path p = fs::path(dir) / "HOUSES.PL8";
    if (!fs::exists(p)) { skip("HOUSES.PL8 not found under " + dir); return; }

    PL8Sheet sheet = pl8::load(p.string());
    // Per CAESAR_REVERSE_ENGINEERING_COMPLETE.md section 8.1:
    // 50 frames, frame 0 = offset 0x0194, w=16, h=16, x=0, y=0.
    CHECK(sheet.frames.size() == 50);
    if (!sheet.frames.empty()) {
        CHECK(sheet.frames[0].width == 16);
        CHECK(sheet.frames[0].height == 16);
        CHECK(sheet.frames[0].x == 0);
        CHECK(sheet.frames[0].y == 0);
        CHECK(!sheet.frames[0].pixels.empty());
    }
    // New finding (see formats/pl8/pl8.hpp): frames 44-49 are trailing
    // placeholders with no real pixel data (pixel_offset sits at EOF).
    if (sheet.frames.size() == 50) {
        for (int i = 44; i <= 49; ++i) CHECK(sheet.frames[i].pixels.empty());
        for (int i = 0; i <= 43; ++i) CHECK(!sheet.frames[i].pixels.empty());
    }
}

void test_save_corpus_size_only() {
    std::printf("test_save_corpus_size_only\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set (also: no real .SAV file has been supplied yet)"); return; }
    fs::path p = fs::path(dir) / "CAESARXX.SAV";
    if (!fs::exists(p)) { skip("no .SAV file found under " + dir + " — expected, none has been captured yet"); return; }
    save::SaveFile sf = save::load(p.string());
    CHECK(sf.raw.size() == save::kSaveSize);
}

void test_exepack_golden_decode() {
    std::printf("test_exepack_golden_decode (US-build CSR.EXE, known dest_len*16 + embedded strings)\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }

    fs::path p = fs::path(dir) / "CSR.EXE";
    if (!fs::exists(p)) { skip("CSR.EXE not found under " + dir); return; }

    exepack::DecodeResult r = exepack::decode(p.string());
    // Established in CAESAR_REVERSE_ENGINEERING_COMPLETE.md section 3.2:
    // decompressed image is exactly 0x797A0 (497568) bytes for the
    // already-analyzed US build. decode() itself already asserts
    // image.size() == dest_len*16, so this is really checking that
    // dest_len itself is the expected value for this specific file.
    CHECK(r.image.size() == 0x797A0);
    CHECK(r.header.signature == 0x4252);

    auto find = [&](const char* needle) {
        std::string hay(reinterpret_cast<const char*>(r.image.data()), r.image.size());
        return hay.find(needle) != std::string::npos;
    };
    CHECK(find("Borland C++ - Copyright 1991 Borland Intl."));
    CHECK(find("CaesarXX.sav"));
    CHECK(find("Caesar - Online help"));
}

}  // namespace

int main() {
    std::printf("=== Gaius Phase 0 format tests ===\n\n");

    test_save_block_table_contiguous();
    test_p32_expand_math();
    test_pal256_expand_math();
    test_pl8_synthetic();
    test_format_errors_are_thrown_not_swallowed();
    test_empire2_corpus_roundtrip();
    test_vpx_golden_image();
    test_pl8_corpus_sanity();
    test_save_corpus_size_only();
    test_exepack_golden_decode();

    std::printf("\n=== %d checks run, %d failed, %d test(s) skipped ===\n", g_ran, g_failures, g_skipped);
    return g_failures == 0 ? 0 : 1;
}
