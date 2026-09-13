// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius Phase 0 test harness — no external test framework, deliberately.
//
// Per GAIUS_MASTERPLAN.md's testing strategy, this is corpus-driven: the
// repo ships no game assets (IP posture — see masterplan section 3), so
// any test needing real Caesar files reads them from a directory named by
// the GAIUS_TEST_ASSETS environment variable and SKIPS (not fails) if
// that variable isn't set. Pure structural/synthetic tests always run.
//
// Run with:  GAIUS_TEST_ASSETS=/path/to/your/caesar/files ./gaius_tests

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "apps/viewer/save_view.hpp"
#include "formats/empire2/empire2.hpp"
#include "formats/exepack/exepack.hpp"
#include "formats/p32/p32.hpp"
#include "formats/pal256/pal256.hpp"
#include "formats/pl8/pl8.hpp"
#include "formats/save/save.hpp"
#include "formats/vpx/vpx.hpp"
#include "model/city_state.hpp"
#include "render/city_render.hpp"
#include "stb_image.h"
#include "systems/construction.hpp"
#include "systems/housing.hpp"
#include "systems/month.hpp"
#include "systems/service.hpp"
#include "ui/font.hpp"
#include "ui/game_font.hpp"
#include "ui/metrics.hpp"
#include "ui/toolbar.hpp"

namespace fs = std::filesystem;
using namespace gaius::formats;
using namespace gaius::model;
using namespace gaius::systems::service;
using namespace gaius::systems::housing;
using namespace gaius::systems::construction;

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

// A fresh city plus its per-tick service state, heap-allocated: a CityMap
// is 50 KB, and the handler tests below build dozens of independent cases.
struct SimFixture {
    CityMap city;
    ServiceState svc;
};
std::unique_ptr<SimFixture> fresh() { return std::make_unique<SimFixture>(); }

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

// Save-file heatmap viewer (apps/viewer/save_view.hpp), Phase 1's other
// previously-deferred item. No real .SAV file has ever been supplied (see
// CLAUDE.md), so this exercises the sampling/color math against an
// in-memory synthetic SaveFile -- built directly (no file I/O needed;
// SaveFile::block() only ever indexes into `raw`) rather than a golden
// comparison against real save data.
void test_save_view_render_synthetic() {
    std::printf("test_save_view_render_synthetic (synthetic SaveFile, no real .SAV needed)\n");
    using gaius::viewer::SaveLayer;

    save::SaveFile sf;
    sf.raw.assign(save::kSaveSize, 0);

    auto block_offset = [](const std::string& name) -> size_t {
        for (const auto& b : save::block_table())
            if (b.name == name) return b.offset;
        throw std::runtime_error("no such block: " + name);
    };

    size_t tiles_off = block_offset("city_tiles_100x100");
    sf.raw[tiles_off + 0] = 0;                                    // cell (0,0)
    sf.raw[tiles_off + 99 * gaius::viewer::kCityW + 99] = 255;    // cell (99,99)

    size_t lv_off = block_offset("cell_value_54a4");
    sf.raw[lv_off + 0] = static_cast<uint8_t>(int8_t(-8));  // cell (0,0): confirmed floor
    sf.raw[lv_off + 1] = 50;                                 // cell (1,0): confirmed ceiling

    // cell_px=1, 100x100 view, camera at origin -> output pixel (x,y) maps
    // 1:1 onto grid cell (x,y), making the indexing math directly checkable.
    std::vector<uint8_t> out;
    gaius::viewer::render_city_layer(sf, SaveLayer::Tiles, /*cell_px=*/1, /*cam_x=*/0, /*cam_y=*/0, /*zoom=*/1.0,
                                      /*view_w=*/100, /*view_h=*/100, out);
    CHECK(out.size() == 100u * 100 * 3);
    auto px = [&](int x, int y) {
        size_t i = (static_cast<size_t>(y) * 100 + x) * 3;
        return RGB{out[i], out[i + 1], out[i + 2]};
    };
    RGB c00 = px(0, 0);
    RGB expect00 = gaius::viewer::color_for_layer(SaveLayer::Tiles, 0);
    CHECK(c00.r == expect00.r && c00.g == expect00.g && c00.b == expect00.b);
    RGB c9999 = px(99, 99);
    RGB expect9999 = gaius::viewer::color_for_layer(SaveLayer::Tiles, 255);
    CHECK(c9999.r == expect9999.r && c9999.g == expect9999.g && c9999.b == expect9999.b);
    // heat_color runs dark->blue->green->yellow->red; raw 0 and 255 should
    // land at opposite, clearly distinct ends, not collapse to the same color.
    CHECK(c00.r != c9999.r || c00.g != c9999.g || c00.b != c9999.b);

    gaius::viewer::render_city_layer(sf, SaveLayer::LandValue54A4, 1, 0, 0, 1.0, 100, 100, out);
    RGB low = px(0, 0);   // land value -8 (confirmed floor)
    RGB high = px(1, 0);  // land value +50 (confirmed ceiling)
    CHECK(low.r > high.r);   // negative land value should read redder...
    CHECK(high.g > low.g);   // ...positive should read greener
    RGB zero = gaius::viewer::land_value_color(0);
    CHECK(zero.r == zero.g && zero.g == zero.b);  // 0 is neutral gray by construction

    // Out-of-range camera positions must background-fill, not read
    // out-of-bounds or wrap -- same boundary contract as the EMPIRE2
    // renderer this was modeled on.
    gaius::viewer::render_city_layer(sf, SaveLayer::Tiles, 1, /*cam_x=*/-50, 0, 1.0, 10, 1, out);
    RGB oob = RGB{out[0], out[1], out[2]};
    CHECK(oob.r == 0 && oob.g == 0 && oob.b == 0);

    // next_layer() cycles through all five and back to the start.
    SaveLayer l = SaveLayer::Tiles;
    for (int i = 0; i < gaius::viewer::kSaveLayerCount; ++i) l = gaius::viewer::next_layer(l);
    CHECK(l == SaveLayer::Tiles);
}

// model::CityState (Phase 2, Layer 2). No real .SAV file exists yet (see
// CLAUDE.md), so this builds a synthetic-but-correctly-sized SaveFile
// in-memory and round-trips it through load()/serialize(). Per
// GAIUS_ROADMAP.md Phase 2, a round trip like this validates that the
// serializer's block table is complete and correctly ordered, independent
// of whether the *meaning* of any field is understood -- exactly what's
// being checked here.
void test_model_city_state_round_trip() {
    std::printf("test_model_city_state_round_trip (synthetic SaveFile, no real .SAV needed)\n");

    save::SaveFile sf;
    sf.raw.resize(save::kSaveSize);
    for (size_t i = 0; i < sf.raw.size(); ++i) sf.raw[i] = static_cast<uint8_t>((i * 197 + 13) % 256);

    auto block_offset = [](const std::string& name) -> size_t {
        for (const auto& b : save::block_table())
            if (b.name == name) return b.offset;
        throw std::runtime_error("no such block: " + name);
    };

    CityState state = load(sf);

    // Spot-check specific positions against the ORIGINAL bytes directly --
    // not just the round-tripped output below -- so a bug shared between
    // load() and serialize() (e.g. a row/col swap present in both) can't
    // hide behind a trivially-passing round trip.
    size_t tiles_off = block_offset("city_tiles_100x100");
    CHECK(state.city.tile[0][0] == sf.raw[tiles_off + 0]);
    CHECK(state.city.tile[0][1] == sf.raw[tiles_off + 1]);
    CHECK(state.city.tile[1][0] == sf.raw[tiles_off + 100]);  // catches a row/col transposition bug
    CHECK(state.city.tile[99][99] == sf.raw[tiles_off + 99 * 100 + 99]);

    size_t lv_off = block_offset("cell_value_54a4");
    CHECK(static_cast<uint8_t>(state.city.land_value[3][7]) == sf.raw[lv_off + 3 * 100 + 7]);

    size_t empire_off = block_offset("empire2_1602");
    CHECK(state.empire.prefix[0] == sf.raw[empire_off + 0]);
    CHECK(state.empire.prefix[1] == sf.raw[empire_off + 1]);
    CHECK(state.empire.at(2, 5) == sf.raw[empire_off + 2 + 2 * 40 + 5]);

    size_t obj_off = block_offset("objects_70x50");
    CHECK(state.objects[0].type() == sf.raw[obj_off + 0 * 50 + 0x07]);
    CHECK(state.objects[5].type() == sf.raw[obj_off + 5 * 50 + 0x07]);
    uint16_t expect_packed_xy =
        static_cast<uint16_t>(sf.raw[obj_off + 5 * 50 + 0x18] | (sf.raw[obj_off + 5 * 50 + 0x19] << 8));
    CHECK(state.objects[5].packed_xy() == expect_packed_xy);
    CHECK((state.objects[0].type() < 11 ? ActorCoordSpace::City : ActorCoordSpace::Province) ==
          state.objects[0].coord_space());

    // Full round trip: all 20 confirmed blocks, at the correct offset,
    // nothing missing/misordered/overlapping -- byte-identical to the input.
    save::SaveFile round_tripped = serialize(state);
    CHECK(round_tripped.raw.size() == sf.raw.size());
    CHECK(round_tripped.raw == sf.raw);
}

// systems::service (Phase 3, Layer 3). Every value here is transcribed
// directly from CAESAR_CITY_STATE_v6.md / CAESAR_REVERSE_ENGINEERING_
// COMPLETE.md section 71 -- these tests exist to catch a transcription
// slip against the RE corpus, not to validate a design choice.
void test_service_bit_table_fixtures() {
    std::printf("test_service_bit_table_fixtures (C9D4 bits, producers confirmed by disassembly)\n");
    auto find = [](uint8_t mask) -> const C9D4BitInfo* {
        for (const auto& b : kC9D4BitTable)
            if (b.mask == mask) return &b;
        return nullptr;
    };
    const C9D4BitInfo* religious = find(0x20);
    CHECK(religious != nullptr);
    CHECK(religious && std::string(religious->name) == "religious");
    // Downgraded from High (2026-09-13): the Prefecture handler (tile 0xEE)
    // sets 0x20 too, so it isn't exclusively temple-produced and "religious"
    // is unverified against any consumer. Pinned here so it can't silently
    // drift back up without new evidence.
    CHECK(religious && religious->confidence == Confidence::StrongInference);

    const C9D4BitInfo* entertainment = find(0x80);
    CHECK(entertainment != nullptr);
    CHECK(entertainment && std::string(entertainment->name) == "entertainment");
    CHECK(entertainment && entertainment->confidence == Confidence::High);

    const C9D4BitInfo* persistent_conn = find(0x10);
    CHECK(persistent_conn && persistent_conn->confidence == Confidence::High);
    // 0x02's generator, routine 0x2DA0D, is disassembled: Definitive.
    const C9D4BitInfo* derived_net = find(0x02);
    CHECK(derived_net && derived_net->confidence == Confidence::Definitive);

    // 0x08 is set by tile 0xF4, the Market (its construction seed, 2x2
    // footprints in real saves, counted /4 by the engine) -- not the barracks
    // it was attributed to before. Upgraded to High on that evidence.
    const C9D4BitInfo* market_bit = find(0x08);
    CHECK(market_bit && market_bit->confidence == Confidence::High);

    CHECK(kC9D4BitTable.size() == 8);
}

void test_service_reset_tick() {
    std::printf("test_service_reset_tick (routine 0x2C8D3: C9D4 &= 0x12, A2C4 = 0, ceiling = 0x3F)\n");
    CityMap city;
    ServiceState svc;
    city.service_flags[10][20] = 0xFF;  // all bits set -- only 0x02/0x10 should survive
    city.coverage[10][20] = 200;
    svc.coverage_ceiling[10][20] = 5;  // stale from a previous tick

    reset_tick(city, svc);

    CHECK(city.service_flags[10][20] == 0x12);
    CHECK(city.coverage[10][20] == 0);
    CHECK(svc.coverage_ceiling[10][20] == 0x3F);
    // Spot-check the reset actually swept every cell, not just the one
    // touched above. [0][0] was never set to anything, so &= 0x12 leaves
    // it at 0 (AND only *preserves* bits that were already set -- it
    // can't set a bit from zero).
    CHECK(city.service_flags[0][0] == 0x00);
    CHECK(svc.coverage_ceiling[99][99] == 0x3F);
}

void test_service_apply_coverage() {
    std::printf("test_service_apply_coverage (routine 0x2C577: square radius, running-minimum ceiling)\n");
    {
        auto f = fresh();
        CityMap& city = f->city;
        ServiceState& svc = f->svc;
        apply_coverage(city, svc, 50, 50, 10, 2, 15);
        CHECK(city.coverage[50][50] == 10);           // origin
        CHECK(city.coverage[50][52] == 10);           // exactly radius 2 on an axis
        CHECK(city.coverage[52][52] == 10);           // radius 2 on BOTH axes -- square, not circular
        CHECK(city.coverage[50][53] == 0);            // just outside
        CHECK(city.coverage[53][53] == 0);
        CHECK(svc.coverage_ceiling[50][50] == 15);    // lowered from 0x3F to this call's ceiling
        CHECK(svc.coverage_ceiling[50][53] == 0x3F);  // untouched outside the radius

        apply_coverage(city, svc, 50, 50, 10, 0, 15);
        CHECK(city.coverage[50][50] == 15);  // capped

        apply_coverage(city, svc, 0, 0, 5, 1, 20);  // off-grid centre must clip, not crash
        CHECK(city.coverage[0][0] == 5);
        CHECK(city.coverage[1][1] == 5);
    }

    // The engine's defining behaviour: the ceiling is a per-cell running
    // minimum over every source this tick, so a low ceiling caps a high one
    // in range whichever is applied first.
    {
        auto f = fresh();
        apply_coverage(f->city, f->svc, 10, 10, 10, 0, 31);  // high ceiling first
        apply_coverage(f->city, f->svc, 10, 10, 1, 0, 4);    // then a low one
        CHECK(f->city.coverage[10][10] == 4);
    }
    {
        auto f = fresh();
        apply_coverage(f->city, f->svc, 10, 10, 1, 0, 4);    // low ceiling first
        apply_coverage(f->city, f->svc, 10, 10, 10, 0, 31);  // a higher one can't lift it back
        CHECK(f->city.coverage[10][10] == 4);
        CHECK(f->svc.coverage_ceiling[10][10] == 4);
        reset_tick(f->city, f->svc);  // the next tick starts over
        CHECK(f->svc.coverage_ceiling[10][10] == 0x3F);
    }
}

void test_service_apply_land_value() {
    std::printf("test_service_apply_land_value (routine 0x2C6AF: square radius, ceiling only, no floor)\n");
    auto f = fresh();
    CityMap& city = f->city;
    apply_land_value(city, 30, 30, 20, 1, 32);
    CHECK(city.land_value[30][30] == 20);
    CHECK(city.land_value[31][31] == 20);  // square radius
    CHECK(city.land_value[32][32] == 0);   // outside

    apply_land_value(city, 30, 30, 20, 0, 32);  // 40 -> capped at this call's ceiling
    CHECK(city.land_value[30][30] == 32);

    for (int i = 0; i < 3; ++i) apply_land_value(city, 40, 40, 20, 0, 64);
    CHECK(city.land_value[40][40] == 60);  // no +50 cap in the propagator

    // No floor: the barracks' -3 accumulates straight past -8. A real save
    // holds -43 right next to one.
    for (int i = 0; i < 15; ++i) apply_land_value(city, 60, 60, -3, 0, 32);
    CHECK(city.land_value[60][60] == -45);
}

void test_service_evolve_land_value() {
    std::printf("test_service_evolve_land_value (routine 0x2DA7E: per-cell step, clamp -8..+50)\n");
    auto f = fresh();
    CityMap& city = f->city;
    city.service_flags[5][5] = 0x20;  // bit 0x20 set: grows by the argument
    city.land_value[5][5] = 45;
    evolve_land_value(city, 5, 5, 3);
    CHECK(city.land_value[5][5] == 48);
    evolve_land_value(city, 5, 5, 3);
    CHECK(city.land_value[5][5] == 50);  // capped at +50

    city.land_value[6][6] = -7;  // no 0x20: decays by 2, floored at -8
    evolve_land_value(city, 6, 6, 3);
    CHECK(city.land_value[6][6] == -8);

    city.land_value[7][7] = -43;  // propagated below the floor: pulled back up
    evolve_land_value(city, 7, 7, 3);
    CHECK(city.land_value[7][7] == -8);
}

void test_service_derive_network_flags() {
    std::printf("test_service_derive_network_flags (routine 0x2DA0D: C9D4.10 -> C9D4.02)\n");
    auto f = fresh();
    CityMap& city = f->city;
    city.service_flags[1][1] = 0x10;         // pending: becomes 0x02, 0x10 cleared
    city.service_flags[2][2] = 0x02;         // stale 0x02 with no 0x10: cleared
    city.service_flags[3][3] = 0x10 | 0x80;  // unrelated bits survive
    derive_network_flags(city);
    CHECK(city.service_flags[1][1] == 0x02);
    CHECK(city.service_flags[2][2] == 0x00);
    CHECK(city.service_flags[3][3] == (0x02 | 0x80));
}

void test_service_apply_flags() {
    std::printf("test_service_apply_flags (square radius, OR-only, no clamping)\n");
    CityMap city;
    city.service_flags[40][40] = 0x01;  // pre-existing bit must survive an OR
    apply_flags(city, 40, 40, 1, 0x20);
    CHECK(city.service_flags[40][40] == 0x21);
    CHECK(city.service_flags[41][41] == 0x20);
    CHECK(city.service_flags[42][42] == 0x00);  // outside radius
}

void test_service_building_handlers() {
    std::printf("test_service_building_handlers (every handler parameter, transcribed from disassembly)\n");
    auto cov = [](const CityMap& c, int x, int y) { return static_cast<int>(c.coverage[y][x]); };

    // Temples: coverage radius 2/3/4/5 (variants 1-3 were previously
    // missing), flags radius 6/8/10/12.
    for (int v = 1; v <= 4; ++v) {
        auto f = fresh();
        apply_temple(f->city, f->svc, 50, 50, static_cast<TempleVariant>(v));
        int cr = v + 1, fr = 4 + 2 * v;
        CHECK(cov(f->city, 50 + cr, 50) == 1);
        CHECK(cov(f->city, 51 + cr, 50) == 0);
        CHECK((f->city.service_flags[50][50 + fr] & 0x20) != 0);
        CHECK((f->city.service_flags[50][51 + fr] & 0x20) == 0);
    }

    {  // Bath houses: nothing unless connected
        auto f = fresh();
        apply_bath_houses(f->city, f->svc, 50, 50);
        CHECK(f->city.coverage[50][50] == 0);
        CHECK(f->city.service_flags[50][50] == 0);
    }
    {  // ...then coverage r2 and flags 0x04 r3
        auto f = fresh();
        f->city.operational_state[50][50] = 0x10;
        apply_bath_houses(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 52, 50) == 1);
        CHECK(cov(f->city, 53, 50) == 0);
        CHECK((f->city.service_flags[50][53] & 0x04) != 0);
        CHECK((f->city.service_flags[50][54] & 0x04) == 0);
    }
    {  // Oracle: coverage +2 r8, land value -2 r5, no flags
        auto f = fresh();
        apply_oracle(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 58, 50) == 2);
        CHECK(cov(f->city, 59, 50) == 0);
        CHECK(f->city.land_value[50][55] == -2);
        CHECK(f->city.land_value[50][56] == 0);
        CHECK(f->city.service_flags[50][50] == 0);
    }
    {  // School/Hospital: coverage r3, flags 0x40 r4
        auto f = fresh();
        apply_school_or_hospital(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 53, 50) == 1);
        CHECK(cov(f->city, 54, 50) == 0);
        CHECK((f->city.service_flags[50][54] & 0x40) != 0);
        CHECK((f->city.service_flags[50][55] & 0x40) == 0);
    }
    {  // Prefecture: coverage r2 at ceiling 8, flags 0x20 r4, land value -2 r3
        auto f = fresh();
        apply_prefecture(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 52, 50) == 1);
        CHECK(cov(f->city, 53, 50) == 0);
        CHECK(f->svc.coverage_ceiling[50][52] == 8);
        CHECK((f->city.service_flags[50][54] & 0x20) != 0);
        CHECK((f->city.service_flags[50][55] & 0x20) == 0);
        CHECK(f->city.land_value[50][53] == -2);
        CHECK(f->city.land_value[50][54] == 0);
    }
    {  // Barracks: coverage r3 at ceiling 5, land value -3 r5, no flags at all
        auto f = fresh();
        apply_barracks(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 53, 50) == 1);
        CHECK(cov(f->city, 54, 50) == 0);
        CHECK(f->svc.coverage_ceiling[50][53] == 5);
        CHECK(f->city.land_value[50][55] == -3);
        CHECK(f->city.land_value[50][56] == 0);
        CHECK(f->city.service_flags[50][50] == 0);
    }
    {  // Theater: coverage r3, flags 0x80 r4
        auto f = fresh();
        apply_theater(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 53, 50) == 1);
        CHECK(cov(f->city, 54, 50) == 0);
        CHECK((f->city.service_flags[50][54] & 0x80) != 0);
        CHECK((f->city.service_flags[50][55] & 0x80) == 0);
    }
    {  // Coliseum: coverage r4, flags 0x80 r6
        auto f = fresh();
        apply_coliseum(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 54, 50) == 1);
        CHECK(cov(f->city, 55, 50) == 0);
        CHECK((f->city.service_flags[50][56] & 0x80) != 0);
        CHECK((f->city.service_flags[50][57] & 0x80) == 0);
    }
    {  // Hippodrome: coverage r5, flags 0x80 r7
        auto f = fresh();
        apply_hippodrome(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 55, 50) == 1);
        CHECK(cov(f->city, 56, 50) == 0);
        CHECK((f->city.service_flags[50][57] & 0x80) != 0);
        CHECK((f->city.service_flags[50][58] & 0x80) == 0);
    }
    {  // Heavy industry: coverage r4 at ceiling 2, no flags
        auto f = fresh();
        apply_heavy_industry(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 54, 50) == 1);
        CHECK(cov(f->city, 55, 50) == 0);
        CHECK(f->svc.coverage_ceiling[50][54] == 2);
        CHECK(f->city.service_flags[50][50] == 0);
    }
    {  // Market: coverage r1 at ceiling 16, flags 0x08 r6
        auto f = fresh();
        apply_market(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 51, 50) == 1);
        CHECK(cov(f->city, 52, 50) == 0);
        CHECK(f->svc.coverage_ceiling[50][51] == 16);
        CHECK((f->city.service_flags[50][56] & 0x08) != 0);
        CHECK((f->city.service_flags[50][57] & 0x08) == 0);
    }
    {  // 0xF5/F6: coverage r3 at ceiling 3
        auto f = fresh();
        apply_tile_f5_f6(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 53, 50) == 1);
        CHECK(cov(f->city, 54, 50) == 0);
        CHECK(f->svc.coverage_ceiling[50][53] == 3);
    }
    {  // 0x36-0x3B: nothing unless connected, then +1 r1
        auto f = fresh();
        apply_tile_36_3b(f->city, f->svc, 50, 50);
        CHECK(f->city.coverage[50][50] == 0);
        f->city.operational_state[50][50] = 0x10;
        apply_tile_36_3b(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 51, 50) == 1);
        CHECK(cov(f->city, 52, 50) == 0);
    }
    {  // 0x3C-0x3F: +1 r1 unconnected, +2 connected, and no land-value effect
        auto f = fresh();
        apply_tile_3c_3f(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 51, 50) == 1);
        CHECK(cov(f->city, 52, 50) == 0);
        CHECK(f->city.land_value[50][50] == 0);
        f->city.operational_state[50][50] = 0x10;
        apply_tile_3c_3f(f->city, f->svc, 50, 50);
        CHECK(f->city.coverage[50][50] == 3);
    }
    {  // 0x40: +2 unconnected, +3 connected
        auto f = fresh();
        apply_tile_40(f->city, f->svc, 50, 50);
        CHECK(f->city.coverage[50][50] == 2);
        f->city.operational_state[50][50] = 0x10;
        apply_tile_40(f->city, f->svc, 50, 50);
        CHECK(f->city.coverage[50][50] == 5);
    }
    {  // What the low ceilings are for: a heavy industry beside a temple caps
       // the temple's coverage in their overlap at 2.
        auto f = fresh();
        for (int i = 0; i < 5; ++i) apply_temple(f->city, f->svc, 50, 50, TempleVariant::Variant4);
        CHECK(f->city.coverage[50][50] == 5);  // alone, it accumulates
        apply_heavy_industry(f->city, f->svc, 52, 50);
        CHECK(f->city.coverage[50][50] == 2);  // capped inside industry's radius
        CHECK(f->city.coverage[50][45] == 5);  // untouched outside it
    }
    {  // 0x94/95: coverage +1 r2 at ceiling 8, land value -2 r2
        auto f = fresh();
        apply_tile_94_95(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 52, 50) == 1);
        CHECK(cov(f->city, 53, 50) == 0);
        CHECK(f->svc.coverage_ceiling[50][52] == 8);
        CHECK(f->city.land_value[50][52] == -2);
        CHECK(f->city.land_value[50][53] == 0);
    }
    {  // 0xA2/A3, 0xA7-B2: NEGATIVE coverage, -2 r3 then -2 r1, ceiling 8. No
       // lower clamp, so the byte wraps -- real saves hold 254/255 near these.
        auto f = fresh();
        apply_tile_a2_b2(f->city, f->svc, 50, 50);
        CHECK(f->city.coverage[50][50] == 252);  // -4 within radius 1
        CHECK(f->city.coverage[50][52] == 254);  // -2 out to radius 3
        CHECK(f->city.coverage[50][53] == 254);
        CHECK(f->city.coverage[50][54] == 0);
        CHECK(f->svc.coverage_ceiling[50][53] == 8);
    }
    {  // 0xB9/BB/BC: coverage +1 r2, only when C9D4.01 is set
        auto f = fresh();
        apply_tile_b9_bb_bc(f->city, f->svc, 50, 50);
        CHECK(f->city.coverage[50][50] == 0);
        f->city.service_flags[50][50] = 0x01;
        apply_tile_b9_bb_bc(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 52, 50) == 1);
        CHECK(cov(f->city, 53, 50) == 0);
    }
    {  // Temple stages 0xD8-DB: coverage +1 r2, land value -2 r2
        auto f = fresh();
        f->city.tile[50][50] = 0xD9;
        apply_temple_stage(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 52, 50) == 1);
        CHECK(cov(f->city, 53, 50) == 0);
        CHECK(f->city.land_value[50][52] == -2);
        CHECK(f->city.land_value[50][53] == 0);
    }
    {  // Temple stages 0xDC-DF: coverage +1 r3, land value -2 r3
        auto f = fresh();
        f->city.tile[50][50] = 0xDE;
        apply_temple_stage(f->city, f->svc, 50, 50);
        CHECK(cov(f->city, 53, 50) == 1);
        CHECK(cov(f->city, 54, 50) == 0);
        CHECK(f->city.land_value[50][53] == -2);
        CHECK(f->city.land_value[50][54] == 0);
    }
}

// dispatch_tile (Phase 5): the actual DS:153A tile-ID dispatcher, routing
// city.tile[y][x] to the handler above matching that exact ID.
void test_service_dispatch_tile() {
    std::printf("test_service_dispatch_tile (DS:153A dispatch, incl. the <=0x35 gate and housing tiers)\n");
    {  // A temple-4 tile produces exactly apply_temple's variant-4 effect
        auto f = fresh();
        f->city.tile[50][50] = 0xE6;
        dispatch_tile(f->city, f->svc, 50, 50);
        CHECK((f->city.service_flags[50][62] & 0x20) != 0);  // radius 12
        CHECK(f->city.coverage[50][55] == 1);                // radius 5
    }
    {  // 0x36-0x3B keeps its connectivity gate through dispatch
        auto f = fresh();
        f->city.tile[10][10] = 0x38;
        dispatch_tile(f->city, f->svc, 10, 10);
        CHECK(f->city.coverage[10][10] == 0);
        f->city.operational_state[10][10] = 0x10;
        dispatch_tile(f->city, f->svc, 10, 10);
        CHECK(f->city.coverage[10][10] == 1);
    }
    {  // The engine's scan (0x2BBBB) never dispatches tiles <= 0x35, and it is
       // the only reader of DS:153A. So tile 0x00 -- whose table entry would
       // turn it into housing -- is inert, which is why 0x00 terrain sits
       // unchanged through a whole play session in real saves.
        auto f = fresh();
        f->city.tile[5][5] = 0x00;
        f->city.land_value[5][5] = 41;
        dispatch_tile(f->city, f->svc, 5, 5);
        CHECK(f->city.tile[5][5] == 0x00);
        f->city.tile[6][6] = 0x35;  // the boundary is skipped too
        f->city.operational_state[6][6] = 0x10;
        dispatch_tile(f->city, f->svc, 6, 6);
        CHECK(f->city.coverage[6][6] == 0);
    }
    {  // Housing tier 0xC8: base-1, radius 1, ceiling 4
        auto f = fresh();
        f->city.tile[20][20] = 0xC8;
        dispatch_tile(f->city, f->svc, 20, 20);
        CHECK(f->city.coverage[20][21] == 1);
        CHECK(f->city.coverage[20][22] == 0);
        CHECK(f->svc.coverage_ceiling[20][20] == 4);
    }
    {  // Housing tier 0xD7: base+2, radius 2, ceiling 31
        auto f = fresh();
        f->city.tile[20][20] = 0xD7;
        dispatch_tile(f->city, f->svc, 20, 20);
        CHECK(f->city.coverage[20][22] == 4);
        CHECK(f->city.coverage[20][23] == 0);
    }
    {  // The base really is a parameter (DS:0x6BF8)
        auto f = fresh();
        f->svc.housing_coverage_base = 5;
        f->city.tile[20][20] = 0xCA;  // base, radius 1, ceiling 8
        dispatch_tile(f->city, f->svc, 20, 20);
        CHECK(f->city.coverage[20][20] == 5);
    }
    {  // 0xF4 routes to the Market (flags 0x08)
        auto f = fresh();
        f->city.tile[30][30] = 0xF4;
        dispatch_tile(f->city, f->svc, 30, 30);
        CHECK((f->city.service_flags[30][36] & 0x08) != 0);
    }
    {  // 0xEF routes to the Barracks (no flags, land value -3)
        auto f = fresh();
        f->city.tile[30][30] = 0xEF;
        dispatch_tile(f->city, f->svc, 30, 30);
        CHECK(f->city.service_flags[30][30] == 0);
        CHECK(f->city.land_value[30][35] == -3);
    }
    {  // 0xE9 and an id with no transcribed handler are no-ops
        auto f = fresh();
        f->city.tile[7][7] = 0xE9;
        dispatch_tile(f->city, f->svc, 7, 7);
        CHECK(f->city.service_flags[7][7] == 0);
        CHECK(f->city.coverage[7][7] == 0);
        f->city.tile[8][8] = 0x99;
        dispatch_tile(f->city, f->svc, 8, 8);
        CHECK(f->city.service_flags[8][8] == 0);
        CHECK(f->city.coverage[8][8] == 0);
    }
}

// systems::housing (Phase 4, Layer 3), transcribed from the disassembly:
// land_value_allows (0x2DB49), the DS:1212 development handlers and the row
// pass (0x294CF), and the population table (3496:007E). Each handler case pins
// one transition exactly -- tiles, 7BB4 part indices and the column skip -- so a
// transcription slip fails loudly. Real-save checks: test_save_corpus_population.
void test_housing_land_value_allows() {
    std::printf("test_housing_land_value_allows (CAESAR_CITY_STATE_v5.md routine 0x2DB49)\n");
    CityMap city;
    city.land_value[10][10] = 41;
    city.coverage[10][10] = 5;
    city.operational_state[10][10] = 7;
    bool result = land_value_allows(city, 10, 10, 40);
    CHECK(result == true);
    CHECK(city.tile[10][10] == 0xA7);
    CHECK(city.coverage[10][10] == 0);
    CHECK(city.operational_state[10][10] == 0);
    CHECK(city.land_value[10][10] == 0);

    // Strictly greater-than, per the doc ("if (lv > threshold)") -- exactly
    // at the threshold must NOT pass.
    CityMap city2;
    city2.land_value[5][5] = 40;
    bool result2 = land_value_allows(city2, 5, 5, 40);
    CHECK(result2 == false);
    CHECK(city2.tile[5][5] == 0);  // untouched

    CityMap city3;
    city3.land_value[5][5] = -3;
    CHECK(land_value_allows(city3, 5, 5, 40) == false);
    CHECK(city3.tile[5][5] == 0);
}

void test_housing_development() {
    std::printf("test_housing_development (DS:1212 handlers and row pass 0x294CF, exact writes)\n");
    const uint8_t all = 0x01 | 0x02 | 0x04 | 0x08 | 0x40 | 0x80;
    auto put = [](CityMap& c, int x, int y, uint8_t tile, int coverage, uint8_t flags) {
        c.tile[y][x] = tile;
        c.coverage[y][x] = static_cast<uint8_t>(coverage);
        c.service_flags[y][x] = flags;
    };
    auto T = [](const CityMap& c, int x, int y) { return static_cast<int>(c.tile[y][x]); };
    auto P = [](const CityMap& c, int x, int y) { return static_cast<int>(c.operational_state[y][x]); };
    auto at_pop = [](int units) {
        DevelopmentContext c;
        c.population_units = units;
        return c;
    };
    const DevelopmentContext none;

    {  // C8: coverage below 0 -> open ground 0x1D; above 0 -> C9; exactly 0 -> stays
        auto f = fresh();
        put(f->city, 10, 10, 0xC8, -1, 0);
        put(f->city, 11, 10, 0xC8, 1, 0);
        put(f->city, 12, 10, 0xC8, 0, 0);
        CHECK(develop_building(f->city, 10, 10, 0, none) == 0);
        develop_building(f->city, 11, 10, 0, none);
        develop_building(f->city, 12, 10, 0, none);
        CHECK(T(f->city, 10, 10) == 0x1D);
        CHECK(T(f->city, 11, 10) == 0xC9);
        CHECK(T(f->city, 12, 10) == 0xC8);
    }
    {  // C8 with land value above 20: land_value_allows fires first
        auto f = fresh();
        put(f->city, 10, 10, 0xC8, 5, 0);
        f->city.land_value[10][10] = 21;
        develop_building(f->city, 10, 10, 0, none);
        CHECK(T(f->city, 10, 10) == 0xA7);
    }
    {  // C9 promotes only with water
        auto f = fresh();
        put(f->city, 10, 10, 0xC9, 2, 0);
        develop_building(f->city, 10, 10, 0, none);
        CHECK(T(f->city, 10, 10) == 0xC9);
        develop_building(f->city, 10, 10, 0x01, none);
        CHECK(T(f->city, 10, 10) == 0xCA);
    }
    {  // CB merges rightwards over open ground into a CC pair
        auto f = fresh();
        put(f->city, 10, 10, 0xCB, 5, 0x03);
        f->city.tile[10][11] = 0x20;
        CHECK(develop_building(f->city, 10, 10, 0x03, at_pop(25)) == 1);
        CHECK(T(f->city, 10, 10) == 0xCC && P(f->city, 10, 10) == 0);
        CHECK(T(f->city, 11, 10) == 0xCC && P(f->city, 11, 10) == 1);
    }
    {  // ...becomes a one-cell CF when the cell to its right is taken
        auto f = fresh();
        put(f->city, 10, 10, 0xCB, 5, 0x03);
        f->city.tile[10][11] = 0xF0;
        CHECK(develop_building(f->city, 10, 10, 0x03, at_pop(25)) == 0);
        CHECK(T(f->city, 10, 10) == 0xCF);
        CHECK(T(f->city, 11, 10) == 0xF0);
    }
    {  // ...and does nothing below the population gate
        auto f = fresh();
        put(f->city, 10, 10, 0xCB, 5, 0x03);
        f->city.tile[10][11] = 0x20;
        develop_building(f->city, 10, 10, 0x03, at_pop(24));
        CHECK(T(f->city, 10, 10) == 0xCB);
    }
    {  // CC demotion splits the pair into two independent CB houses
        auto f = fresh();
        put(f->city, 10, 10, 0xCC, 4, 0x03);
        f->city.tile[10][11] = 0xCC;
        f->city.operational_state[10][11] = 1;
        CHECK(develop_building(f->city, 10, 10, 0x03, none) == 1);
        CHECK(T(f->city, 10, 10) == 0xCB && P(f->city, 10, 10) == 0);
        CHECK(T(f->city, 11, 10) == 0xCB && P(f->city, 11, 10) == 0);
    }
    {  // CF -> D0 needs the market bit and 75 population units
        auto f = fresh();
        put(f->city, 10, 10, 0xCF, 7, 0x0B);
        develop_building(f->city, 10, 10, 0x0B, at_pop(74));
        CHECK(T(f->city, 10, 10) == 0xCF);
        develop_building(f->city, 10, 10, 0x0B, at_pop(75));
        CHECK(T(f->city, 10, 10) == 0xD0);
    }
    {  // D0 forms a pair by absorbing a neighbouring CF
        auto f = fresh();
        put(f->city, 10, 10, 0xD0, 8, 0x0F);
        f->city.tile[10][11] = 0xCF;
        CHECK(develop_building(f->city, 10, 10, 0x0F, at_pop(100)) == 1);
        CHECK(T(f->city, 10, 10) == 0xD1 && T(f->city, 11, 10) == 0xD1 && P(f->city, 11, 10) == 1);
    }
    {  // D4 grows into a 2x2 D5 over open ground below
        auto f = fresh();
        put(f->city, 10, 10, 0xD4, 19, all);
        f->city.tile[10][11] = 0xD4;
        f->city.operational_state[10][11] = 1;
        f->city.tile[11][10] = 0x20;
        f->city.tile[11][11] = 0x20;
        CHECK(develop_building(f->city, 10, 10, all, at_pop(200)) == 1);
        CHECK(T(f->city, 10, 10) == 0xD5 && P(f->city, 10, 10) == 0);
        CHECK(T(f->city, 11, 10) == 0xD5 && P(f->city, 11, 10) == 1);
        CHECK(T(f->city, 10, 11) == 0xD5 && P(f->city, 10, 11) == 4);
        CHECK(T(f->city, 11, 11) == 0xD5 && P(f->city, 11, 11) == 5);
    }
    {  // ...but not over a civic building
        auto f = fresh();
        put(f->city, 10, 10, 0xD4, 19, all);
        f->city.tile[11][10] = 0xF0;
        f->city.tile[11][11] = 0x20;
        CHECK(develop_building(f->city, 10, 10, all, at_pop(200)) == 0);
        CHECK(T(f->city, 10, 10) == 0xD4);
    }
    {  // D5 demotes into two stacked D4 pairs
        auto f = fresh();
        put(f->city, 10, 10, 0xD5, 18, all);
        CHECK(develop_building(f->city, 10, 10, all, none) == 1);
        CHECK(T(f->city, 10, 10) == 0xD4 && P(f->city, 11, 10) == 1);
        CHECK(T(f->city, 10, 11) == 0xD4 && P(f->city, 10, 11) == 0 && P(f->city, 11, 11) == 1);
    }
    {  // D6 grows into a 3x3 D7 and skips two columns
        auto f = fresh();
        put(f->city, 10, 10, 0xD6, 23, all);
        for (int y = 10; y < 13; ++y)
            for (int x = 10; x < 13; ++x)
                if (x > 11 || y > 11) f->city.tile[y][x] = 0x20;
        CHECK(develop_building(f->city, 10, 10, all, at_pop(250)) == 2);
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                CHECK(T(f->city, 10 + x, 10 + y) == 0xD7);
                CHECK(P(f->city, 10 + x, 10 + y) == 4 * y + x);
            }
        }
    }
    {  // D7 demotion breaks the 3x3 into a D6 2x2, a D4 pair and three D0 singles
        auto f = fresh();
        put(f->city, 10, 10, 0xD7, 22, all);
        CHECK(develop_building(f->city, 10, 10, all, none) == 2);
        CHECK(T(f->city, 10, 10) == 0xD6 && T(f->city, 11, 11) == 0xD6 && P(f->city, 11, 11) == 5);
        CHECK(T(f->city, 12, 10) == 0xD0 && T(f->city, 12, 11) == 0xD0 && T(f->city, 12, 12) == 0xD0);
        CHECK(T(f->city, 10, 12) == 0xD4 && T(f->city, 11, 12) == 0xD4 && P(f->city, 11, 12) == 1);
    }
    {  // Temple stages: D8 -> D9 -> DA, growing downwards
        auto f = fresh();
        put(f->city, 10, 10, 0xD8, 3, 0);
        develop_building(f->city, 10, 10, 0, at_pop(15));
        CHECK(T(f->city, 10, 10) == 0xD9);
        f->city.coverage[10][10] = 6;
        f->city.tile[11][10] = 0x20;
        CHECK(develop_building(f->city, 10, 10, 0, at_pop(15)) == 0);
        CHECK(T(f->city, 10, 10) == 0xDA && T(f->city, 10, 11) == 0xDA && P(f->city, 10, 11) == 4);
    }
    {  // DA shrinks back, leaving open ground below
        auto f = fresh();
        put(f->city, 10, 10, 0xDA, 5, 0);
        f->city.tile[11][10] = 0xDA;
        f->city.operational_state[11][10] = 4;
        develop_building(f->city, 10, 10, 0, none);
        CHECK(T(f->city, 10, 10) == 0xD9 && T(f->city, 10, 11) == 0x1D && P(f->city, 10, 11) == 0);
    }
    {  // DC widens into a 2x2 DD
        auto f = fresh();
        put(f->city, 10, 10, 0xDC, 18, 0);
        f->city.tile[10][11] = 0x20;
        f->city.tile[11][11] = 0x20;
        CHECK(develop_building(f->city, 10, 10, 0, at_pop(75)) == 1);
        CHECK(T(f->city, 11, 11) == 0xDD && P(f->city, 11, 11) == 5);
    }
    {  // DE widens into a 3x2 DF; DF shrinks back with open ground on the right
        auto f = fresh();
        put(f->city, 10, 10, 0xDE, 24, 0);
        f->city.tile[10][12] = 0x20;
        f->city.tile[11][12] = 0x20;
        CHECK(develop_building(f->city, 10, 10, 0, at_pop(200)) == 1);
        CHECK(T(f->city, 12, 11) == 0xDF && P(f->city, 12, 11) == 6);
        f->city.coverage[10][10] = 23;
        CHECK(develop_building(f->city, 10, 10, 0, none) == 1);
        CHECK(T(f->city, 10, 10) == 0xDE && T(f->city, 12, 10) == 0x1D && T(f->city, 12, 11) == 0x1D);
    }
    {  // E8 grows into a 2x2 EA
        auto f = fresh();
        put(f->city, 10, 10, 0xE8, 13, 0x01);
        f->city.tile[10][11] = 0x20;
        f->city.tile[11][10] = 0x20;
        f->city.tile[11][11] = 0x20;
        CHECK(develop_building(f->city, 10, 10, 0x01, at_pop(40)) == 1);
        CHECK(T(f->city, 11, 11) == 0xEA && P(f->city, 11, 11) == 5);
    }
    {  // E8's connected bit (7BB4.10) simply records whether it has water
        auto f = fresh();
        put(f->city, 10, 10, 0xE8, 0, 0x01);
        develop_building(f->city, 10, 10, 0x01, none);
        CHECK((P(f->city, 10, 10) & 0x10) != 0);
        f->city.service_flags[10][10] = 0;
        develop_building(f->city, 10, 10, 0, none);
        CHECK((P(f->city, 10, 10) & 0x10) == 0);
    }
    {  // EA shrinks to two E8 on the top row, with open ground below
        auto f = fresh();
        put(f->city, 10, 10, 0xEA, 11, 0);
        CHECK(develop_building(f->city, 10, 10, 0, none) == 1);
        CHECK(T(f->city, 10, 10) == 0xE8 && T(f->city, 11, 10) == 0xE8 && P(f->city, 11, 10) == 0x40);
        CHECK(T(f->city, 10, 11) == 0x1D && T(f->city, 11, 11) == 0x1D);
    }
    {  // The row pass: land value, fountains, anchors only, and column skipping
        auto f = fresh();
        CityMap& c = f->city;
        DevelopmentContext rc;
        rc.population_units = 60;
        rc.land_value_growth = 3;
        c.tile[5][0] = 0x20;
        c.land_value[5][0] = -5;  // below 0xC8: zeroed
        put(c, 1, 5, 0xC8, 0, 0x20);
        c.land_value[5][1] = 4;  // housing with C9D4.20: grows by 3
        put(c, 2, 5, 0xD7, 30, all);
        c.land_value[5][2] = 9;         // 0xD7 and above: zeroed
        put(c, 3, 5, 0xCC, 4, 0x03);   // this pair demotes...
        put(c, 4, 5, 0xCC, 2, 0x03);   // ...and its right half, now a CB that would demote, is skipped
        c.operational_state[5][4] = 1;
        put(c, 5, 5, 0xC9, 2, 0x01);   // a non-anchor cell is never developed
        c.operational_state[5][5] = 1;
        put(c, 6, 5, 0xB9, 11, 0x01);  // small fountain grows; watered -> BB
        put(c, 7, 5, 0xBB, 9, 0x00);   // big fountain shrinks; dry -> BA
        develop_row(c, 5, rc);
        CHECK(c.land_value[5][0] == 0);
        CHECK(c.land_value[5][1] == 7);
        CHECK(c.land_value[5][2] == 0);
        CHECK(c.tile[5][3] == 0xCB && c.tile[5][4] == 0xCB);
        CHECK(c.tile[5][5] == 0xC9);
        CHECK(c.tile[5][6] == 0xBB);
        CHECK(c.tile[5][7] == 0xBA);
    }
}

void test_housing_population() {
    std::printf("test_housing_population (units per cell from 3496:007E; population = 4 x units)\n");
    CHECK(kPopulationUnitsPerCell.size() == 16);
    CHECK(kPopulationUnitsPerCell[0] == 1 && kPopulationUnitsPerCell[6] == 6 && kPopulationUnitsPerCell[15] == 1);
    auto f = fresh();
    f->city.tile[0][0] = 0xC9;  // 1
    f->city.tile[0][1] = 0xCC;  // 3
    f->city.tile[0][2] = 0xCC;  // 3 -- a pair counts once per cell
    f->city.tile[1][0] = 0xD4;  // 3
    f->city.tile[1][1] = 0xE0;  // a temple: not housing
    CHECK(population_units(f->city) == 10);
    CHECK(population(f->city) == 40);
}

// systems::construction (Phase 5). kCommandNames is a transcription of a
// real, located table (docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md
// section 1) -- this checks the transcription itself (structure/count/a
// few spot values), not the RE claim. See
// test_construction_command_table_corpus below for the actual golden
// check against real executable bytes.
void test_construction_command_table_fixtures() {
    std::printf("test_construction_command_table_fixtures (transcription sanity, no assets needed)\n");
    CHECK(kCommandNames.size() == static_cast<size_t>(kCommandCount));
    CHECK(kCommandCount == 34);
    CHECK(std::string(command_name(CommandId::NoAction)) == "No action");
    CHECK(std::string(command_name(CommandId::Road)) == "Road");
    CHECK(std::string(command_name(CommandId::ReservoirPipe)) == "Resevoir\\pipe");  // the original's own typo, verbatim
    CHECK(std::string(command_name(CommandId::Temple)) == "Temple");
    CHECK(std::string(command_name(CommandId::CohortGoHome)) == "Cohort Go Home");
    CHECK(static_cast<int>(CommandId::CohortGoHome) == 33);
}

// Golden-data check: decodes the real US-build CSR.EXE and confirms all 34
// command strings are present, in order, starting from the documented
// table base -- i.e. this is checking the *RE finding itself* against real
// bytes, not just the C++ transcription of it. Skips cleanly without
// GAIUS_TEST_ASSETS, same as every other corpus test.
//
// Deliberately does NOT assert a fixed per-entry byte stride: an earlier
// version of this test assumed a uniform 16-byte field width (based on
// the first 14 entries, which happen to all be exactly 16 bytes apart)
// and the golden check against real bytes caught it -- 8 of the 34
// entries are actually 17 bytes wide, not 16, so a formula-based offset
// is wrong for them. There's no null terminator or other in-band
// delimiter (checked -- zero 0x00 bytes across the whole table region),
// so the true field-width rule (if a single rule exists at all) isn't
// determined; searching for each string in sequence, rather than at a
// computed offset, is what's actually verified here.
void test_construction_command_table_corpus() {
    std::printf("test_construction_command_table_corpus (order + presence vs. real US-build CSR.EXE bytes)\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }
    fs::path p = fs::path(dir) / "CSR.EXE";
    if (!fs::exists(p)) { skip("CSR.EXE not found under " + dir); return; }

    exepack::DecodeResult r = exepack::decode(p.string());
    std::string image(reinterpret_cast<const char*>(r.image.data()), r.image.size());
    constexpr size_t kTableBase = 0x7659D;

    size_t search_from = kTableBase;
    for (int i = 0; i < kCommandCount; ++i) {
        std::string name = command_name(static_cast<CommandId>(i));
        size_t found = image.find(name, search_from);
        CHECK(found != std::string::npos);
        if (found == std::string::npos) break;  // avoid a cascade of failures if one entry is genuinely wrong
        CHECK(found - search_from < 20);         // each entry should follow closely, not be found far downstream
        search_from = found + 1;
    }
}

// systems::construction placement (Phase 5). Every seed tile and footprint
// below is transcribed from the real per-command handlers reached through
// the DS:127C dispatch table -- see
// docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md sections 13-14. These
// tests catch a transcription slip, same spirit as the C9D4 bit fixtures.
void test_construction_placement_specs() {
    std::printf("test_construction_placement_specs (seed tiles + footprints from the real handlers)\n");
    auto spec = [](CommandId id) { return placement_spec(id); };

    // Single-cell seeds.
    CHECK(spec(CommandId::Housing).kind == PlacementKind::SingleCell);
    CHECK(spec(CommandId::Housing).seed_tile == 0xC8);
    CHECK(spec(CommandId::Temple).seed_tile == 0xD8);
    CHECK(spec(CommandId::BathHouses).seed_tile == 0xE8);
    CHECK(spec(CommandId::Well).seed_tile == 0xB8);
    CHECK(spec(CommandId::Fountain).seed_tile == 0xBA);
    CHECK(spec(CommandId::Prefecture).seed_tile == 0xEE);

    // Multi-cell footprints -- note these are genuinely different sizes,
    // matching the manual's relative building scale (hippodrome largest).
    CHECK(spec(CommandId::Theater).kind == PlacementKind::MultiCell);
    CHECK(spec(CommandId::Theater).seed_tile == 0xF0);
    CHECK(spec(CommandId::Theater).width == 2 && spec(CommandId::Theater).height == 1);
    CHECK(spec(CommandId::Coliseum).width == 3 && spec(CommandId::Coliseum).height == 2);
    CHECK(spec(CommandId::Hippodrome).width == 4 && spec(CommandId::Hippodrome).height == 2);
    CHECK(spec(CommandId::Barracks).width == 3 && spec(CommandId::Barracks).height == 3);
    CHECK(spec(CommandId::HeavyIndustry).width == 4 && spec(CommandId::HeavyIndustry).height == 4);
    CHECK(spec(CommandId::Hospital).seed_tile == 0xED && spec(CommandId::Hospital).width == 2);
    CHECK(spec(CommandId::School).seed_tile == 0xEC);
    CHECK(spec(CommandId::Oracle).seed_tile == 0xEB);

    // Forum/Workshop: footprint known, seed selected per variant at
    // runtime -- must NOT claim a seed tile.
    CHECK(spec(CommandId::Forum).kind == PlacementKind::VariantSelected);
    CHECK(spec(CommandId::Forum).width == 4 && spec(CommandId::Forum).height == 4);
    CHECK(spec(CommandId::Workshop).kind == PlacementKind::VariantSelected);

    // Drag-based auto-tiling -- mechanism not implementable yet, must not
    // be given a fabricated seed.
    CHECK(spec(CommandId::Road).kind == PlacementKind::DragAutoTiled);
    CHECK(spec(CommandId::Wall).kind == PlacementKind::DragAutoTiled);

    // Pure UI/mode actions place nothing.
    CHECK(spec(CommandId::NoAction).kind == PlacementKind::NonPlacing);
    CHECK(spec(CommandId::Maps).kind == PlacementKind::NonPlacing);
    CHECK(spec(CommandId::GoToProvince).kind == PlacementKind::NonPlacing);
}

void test_construction_place() {
    std::printf("test_construction_place (terrain gate 0x1D..0x35, footprint writes, 7BB4 part index)\n");
    // Fill a patch with buildable terrain; leave the rest at 0x00 (not buildable).
    CityMap city;
    for (int y = 10; y < 30; ++y)
        for (int x = 10; x < 30; ++x) city.tile[y][x] = 0x20;  // inside 0x1D..0x35

    // Single-cell: Temple seeds 0xD8 and clears 7BB4.
    city.operational_state[12][12] = 0xFF;
    CHECK(can_place(city, CommandId::Temple, 12, 12));
    CHECK(place(city, CommandId::Temple, 12, 12));
    CHECK(city.tile[12][12] == 0xD8);
    CHECK(city.operational_state[12][12] == 0);

    // The seed tile itself is NOT buildable terrain, so you can't build on top of it.
    CHECK(can_place(city, CommandId::Temple, 12, 12) == false);
    CHECK(place(city, CommandId::Temple, 12, 12) == false);

    // Multi-cell: Coliseum is 3 wide x 2 tall -- exactly 6 cells written.
    CHECK(place(city, CommandId::Coliseum, 20, 20));
    for (int y = 20; y < 22; ++y)
        for (int x = 20; x < 23; ++x) CHECK(city.tile[y][x] == 0xF1);
    CHECK(city.tile[20][23] == 0x20);  // one past the width -- untouched
    CHECK(city.tile[22][20] == 0x20);  // one past the height -- untouched
    // Each footprint cell records its part index, 4*dy + dx (engine routine 0x1232E).
    for (int y = 20; y < 22; ++y)
        for (int x = 20; x < 23; ++x) CHECK(city.operational_state[y][x] == 4 * (y - 20) + (x - 20));

    // Terrain gate: a footprint that overlaps non-buildable ground is rejected
    // wholesale, and must not partially write.
    CityMap city2;
    for (int y = 5; y < 8; ++y)
        for (int x = 5; x < 8; ++x) city2.tile[y][x] = 0x20;
    city2.tile[5][6] = 0x00;  // one bad cell inside a 2x2 at (5,5)
    CHECK(can_place(city2, CommandId::Hospital, 5, 5) == false);
    CHECK(place(city2, CommandId::Hospital, 5, 5) == false);
    CHECK(city2.tile[5][5] == 0x20);  // nothing written

    // Grid bounds: a 4-wide building at the right edge must be rejected.
    CityMap city3;
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 100; ++x) city3.tile[y][x] = 0x20;
    CHECK(can_place(city3, CommandId::Hippodrome, 96, 50));           // occupies 96..99 -- the last legal spot
    CHECK(can_place(city3, CommandId::Hippodrome, 97, 50) == false);  // would need cell 100

    // Non-placing and drag-based commands never place.
    CHECK(place(city3, CommandId::Maps, 50, 50) == false);
    CHECK(place(city3, CommandId::Road, 50, 50) == false);
    CHECK(place(city3, CommandId::Forum, 50, 50) == false);  // VariantSelected: seed unknown, must refuse
}

// The full pipeline, end to end: a construction command seeds a tile, and
// the simulation dispatcher then recognises that tile's grown form.
void test_construction_to_simulation_pipeline() {
    std::printf("test_construction_to_simulation_pipeline (place -> seed tile -> dispatch_tile)\n");
    auto f = fresh();
    CityMap& city = f->city;
    for (int y = 40; y < 60; ++y)
        for (int x = 40; x < 60; ++x) city.tile[y][x] = 0x20;

    // Theater seeds 0xF0, which is exactly the tile DS:153A routes to the
    // theater simulation handler -- so placing it makes dispatch_tile do
    // theater work at that cell with no further wiring.
    CHECK(place(city, CommandId::Theater, 50, 50));
    CHECK(city.tile[50][50] == 0xF0);
    dispatch_tile(city, f->svc, 50, 50);
    CHECK((city.service_flags[50][54] & 0x80) != 0);  // entertainment flag, radius 4
    CHECK(city.coverage[50][53] == 1);                 // coverage radius 3
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

    // A full nibble is DAC 60 (15 * 4), which widens to 243 -- not 255.
    Palette pal = p32::load(tmp);
    CHECK(pal.filled_count == 32);
    CHECK(pal.colors[0].r == 243 && pal.colors[0].g == 0 && pal.colors[0].b == 0);
    CHECK(pal.colors[1].r == 0 && pal.colors[1].g == 243 && pal.colors[1].b == 0);
    CHECK(pal.colors[2].r == 0 && pal.colors[2].g == 0 && pal.colors[2].b == 243);
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

    // A channel byte above 63 isn't a 6-bit DAC value: rejected, not masked.
    buf[767] = 64;
    { std::ofstream out(tmp, std::ios::binary); out.write(reinterpret_cast<char*>(buf.data()), buf.size()); }
    bool threw = false;
    try {
        pal256::load(tmp);
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
    fs::remove(tmp);
}

void test_pl8_synthetic() {
    std::printf("test_pl8_synthetic\n");
    // header: unknown_a=0, frame_count=2
    // frame0: pixel_offset=20 (BE), w=4, h=2, x=0, y=0 -> 8 pixel bytes at offset 20
    // frame1: pixel_offset=28 (BE), w=4, h=1, x=5, y=7 -> 4 pixel bytes at offset 28
    // Pixels are stored as four streams (see formats/pl8/pl8.hpp): row-major
    // pixel i is entry i/4 of stream i%4.
    std::vector<uint8_t> buf;
    auto push_u16le = [&](uint16_t v) { buf.push_back(v & 0xFF); buf.push_back((v >> 8) & 0xFF); };
    auto push_u16be = [&](uint16_t v) { buf.push_back((v >> 8) & 0xFF); buf.push_back(v & 0xFF); };
    auto write = [&](const std::string& path) {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<char*>(buf.data()), buf.size());
    };

    push_u16le(0);   // unknown_a
    push_u16le(2);   // frame_count

    push_u16be(20); buf.push_back(4); buf.push_back(2); push_u16le(0); push_u16le(0);   // frame0 descriptor
    push_u16be(28); buf.push_back(4); buf.push_back(1); push_u16le(5); push_u16le(7);   // frame1 descriptor

    while (buf.size() < 20) buf.push_back(0xAA);  // padding up to pixel_offset 20
    // frame0, image rows {1,2,3,4} / {5,6,7,8}: streams {1,5} {2,6} {3,7} {4,8}
    buf.insert(buf.end(), {1, 5, 2, 6, 3, 7, 4, 8});
    // frame1, 4x1: one entry per stream, so stored order equals image order
    buf.insert(buf.end(), {55, 66, 77, 88});

    std::string tmp = (fs::temp_directory_path() / "gaius_test.pl8").string();
    write(tmp);

    PL8Sheet sheet = pl8::load(tmp);
    CHECK(sheet.frames.size() == 2);
    if (sheet.frames.size() == 2) {
        CHECK(sheet.frames[0].width == 4 && sheet.frames[0].height == 2);
        CHECK(sheet.frames[0].x == 0 && sheet.frames[0].y == 0);
        CHECK((sheet.frames[0].pixels == std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}));

        CHECK(sheet.frames[1].width == 4 && sheet.frames[1].height == 1);
        CHECK(sheet.frames[1].x == 5 && sheet.frames[1].y == 7);
        CHECK((sheet.frames[1].pixels == std::vector<uint8_t>{55, 66, 77, 88}));
    }

    // A frame whose width isn't a multiple of 4 can't be split into planes:
    // rejected, not guessed (no shipped frame has one).
    buf[14] = 2;  // frame1's width byte
    buf[15] = 2;  // and height: now 2x2, still 4 pixels but only 2 wide
    write(tmp);
    bool threw = false;
    try {
        pl8::load(tmp);
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
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

    // EMAP2_decoded.png was generated by a tool that widened nibbles as n * 17,
    // not the game's n * 4 (see formats/p32/p32.hpp), so compare nibbles: it
    // still pins the VPX decode and the P32 channel layout exactly.
    auto nibble = [](uint8_t ours) { return (ours >> 2) / 4; };
    size_t mismatches = 0;
    size_t total = static_cast<size_t>(result.image.width) * result.image.height;
    for (size_t i = 0; i < total; ++i) {
        uint8_t idx = result.image.pixels[i];
        RGB c = pal.colors[idx];
        if (nibble(c.r) != ref_pixels[i * 3 + 0] / 17 || nibble(c.g) != ref_pixels[i * 3 + 1] / 17 ||
            nibble(c.b) != ref_pixels[i * 3 + 2] / 17) {
            ++mismatches;
        }
    }
    stbi_image_free(ref_pixels);

    std::printf("  pixel mismatches vs EMAP2_decoded.png (nibble level): %zu / %zu\n", mismatches, total);
    CHECK(mismatches == 0);

    // And against the running game: a DOSBox screenshot of the map screen, at
    // 6-bit DAC level. The game draws province markers and a status line over
    // the map inside x 79..237, y 86..196; everywhere else must match exactly.
    fs::path shot_path = fs::path(dir) / "gaius_test_screens" / "7489888-caesar-dos-map.png";
    if (!fs::exists(shot_path)) {
        skip("map screenshot not found under " + dir + "/gaius_test_screens");
        return;
    }
    int sw, sh, sc;
    unsigned char* shot = stbi_load(shot_path.string().c_str(), &sw, &sh, &sc, 3);
    CHECK(shot != nullptr && sw == 320 && sh == 200);
    if (!shot) return;
    size_t outside = 0, outside_bad = 0, inside_bad = 0;
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 320; ++x) {
            const RGB c = pal.colors[result.image.pixels[static_cast<size_t>(y) * 320 + x]];
            const unsigned char* s = shot + (y * 320 + x) * 3;
            const bool same = (c.r >> 2) == (s[0] >> 2) && (c.g >> 2) == (s[1] >> 2) && (c.b >> 2) == (s[2] >> 2);
            const bool overlay = x >= 79 && x <= 237 && y >= 86 && y <= 196;
            if (overlay) {
                inside_bad += !same;
            } else {
                ++outside;
                outside_bad += !same;
            }
        }
    }
    stbi_image_free(shot);
    std::printf("  vs map screenshot (6-bit): %zu / %zu mismatches outside the overlay, %zu inside it\n", outside_bad,
                outside, inside_bad);
    CHECK(outside_bad == 0);
}

// .256 against real files. There's no independent golden image for a
// .256-paired picture, so this checks the decoder against the one palette
// format that has one: PANEL1 and SHADE ship as both .256 and .P32 (proven by
// test_vpx_golden_image). Compared at 6-bit DAC level -- a .P32 nibble times 4
// -- since that's what the VGA card receives.
void test_pal256_corpus() {
    std::printf("test_pal256_corpus (all .256 files; PANEL1/SHADE vs .P32; PANEL1.VPX through both)\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }

    int found = 0;
    for (const char* name : {"IMPRLOGO", "NEWFORUM", "PANEL1", "ROME1", "SHADE", "TEMPLE", "TITLE3", "WAR2"}) {
        fs::path p = fs::path(dir) / (std::string(name) + ".256");
        if (!fs::exists(p)) continue;
        ++found;
        bool loaded = true;
        try {
            CHECK(pal256::load(p.string()).filled_count == 256);
        } catch (const FormatError& e) {
            std::fprintf(stderr, "  %s\n", e.what());
            loaded = false;
        }
        CHECK(loaded);
    }
    std::printf("  (%d/8 .256 files found)\n", found);
    CHECK(found > 0);

    // Both decoders widen the same 6-bit DAC value the same way, so >> 2
    // recovers it for either.
    auto dac6 = [](const RGB& c) { return std::array<int, 3>{c.r >> 2, c.g >> 2, c.b >> 2}; };
    auto p32_dac6 = dac6;

    // PANEL1: all 32 entries agree. SHADE: entries 0-19 agree; 20-31 are a
    // different colour ramp in each file.
    for (const auto& [name, agree] : {std::pair<const char*, int>{"PANEL1", 32}, {"SHADE", 20}}) {
        fs::path p256 = fs::path(dir) / (std::string(name) + ".256");
        fs::path p32p = fs::path(dir) / (std::string(name) + ".P32");
        if (!fs::exists(p256) || !fs::exists(p32p)) continue;
        Palette a = pal256::load(p256.string());
        Palette b = p32::load(p32p.string());
        int same = 0;
        for (int i = 0; i < agree; ++i) same += dac6(a.colors[i]) == p32_dac6(b.colors[i]);
        std::printf("  %s: %d/%d entries match the .P32\n", name, same, agree);
        CHECK(same == agree);
    }

    // PANEL1.VPX, the real bottom control panel, rendered through both palettes.
    fs::path vpx_path = fs::path(dir) / "PANEL1.VPX";
    if (fs::exists(vpx_path) && fs::exists(fs::path(dir) / "PANEL1.P32") && fs::exists(fs::path(dir) / "PANEL1.256")) {
        vpx::DecodeResult img = vpx::decode(vpx_path.string());
        Palette a = pal256::load((fs::path(dir) / "PANEL1.256").string());
        Palette b = p32::load((fs::path(dir) / "PANEL1.P32").string());
        size_t total = static_cast<size_t>(img.image.width) * img.image.height, mismatches = 0;
        for (size_t i = 0; i < total; ++i) {
            const uint8_t idx = img.image.pixels[i];
            if (idx >= 32 || dac6(a.colors[idx]) != p32_dac6(b.colors[idx])) ++mismatches;
        }
        std::printf("  PANEL1.VPX pixel mismatches, .256 vs .P32: %zu / %zu\n", mismatches, total);
        CHECK(mismatches == 0);
    }
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

// Sprites and palette against the running game. DOSBox screenshots of the city
// view (320x200, from a real play session) live in
// GAIUS_TEST_ASSETS/gaius_test_screens/, never in the repo. Each case is a PL8
// frame found verbatim in a screenshot, drawn with SHADE.256 -- the palette
// the city view runs with (the screenshots' own palettes equal it in all 256
// entries). Compared at 6-bit DAC level, since screenshot tools differ in how
// they widen 6-bit colour to 8-bit.
void test_pl8_matches_real_screenshots() {
    std::printf("test_pl8_matches_real_screenshots (PL8 frames + SHADE.256 vs DOSBox captures)\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }
    const fs::path screens = fs::path(dir) / "gaius_test_screens";
    const fs::path shade = fs::path(dir) / "SHADE.256";
    if (!fs::exists(screens) || !fs::exists(shade)) {
        skip("gaius_test_screens/ or SHADE.256 not found under " + dir);
        return;
    }
    const Palette pal = pal256::load(shade.string());

    struct Case {
        const char* sheet;
        int frame;
        const char* screenshot;
        int x, y;
    };
    const Case cases[] = {
        {"HOUSES.PL8", 1, "2049596-caesar-dos-main-game-screen.png", 144, 48},
        {"FIXTS.PL8", 55, "2049596-caesar-dos-main-game-screen.png", 32, 16},
        {"HOUSES2.PL8", 4, "2053010-caesar-dos-building-your-city.png", 64, 64},
    };
    for (const Case& c : cases) {
        const fs::path sheet_path = fs::path(dir) / c.sheet;
        const fs::path shot_path = screens / c.screenshot;
        if (!fs::exists(sheet_path) || !fs::exists(shot_path)) {
            skip(std::string(c.sheet) + " or " + c.screenshot + " not found");
            continue;
        }
        const PL8Sheet sheet = pl8::load(sheet_path.string());
        CHECK(c.frame < static_cast<int>(sheet.frames.size()));
        if (c.frame >= static_cast<int>(sheet.frames.size())) continue;
        const PL8Frame& fr = sheet.frames[c.frame];

        int w, h, channels;
        unsigned char* shot = stbi_load(shot_path.string().c_str(), &w, &h, &channels, 3);
        CHECK(shot != nullptr && w == 320 && h == 200);
        if (!shot) continue;
        CHECK(c.x + fr.width <= w && c.y + fr.height <= h);

        int mismatches = 0;
        for (int y = 0; y < fr.height; ++y) {
            for (int x = 0; x < fr.width; ++x) {
                const RGB col = pal.colors[fr.pixels[static_cast<size_t>(y) * fr.width + x]];
                const unsigned char* s = shot + ((c.y + y) * w + (c.x + x)) * 3;
                if ((col.r >> 2) != (s[0] >> 2) || (col.g >> 2) != (s[1] >> 2) || (col.b >> 2) != (s[2] >> 2)) ++mismatches;
            }
        }
        stbi_image_free(shot);
        std::printf("  %s frame %d (%dx%d) at (%d,%d): %d mismatched pixels\n", c.sheet, c.frame, fr.width, fr.height,
                    c.x, c.y, mismatches);
        CHECK(mismatches == 0);
    }
}

// Real save files -- the first this project has ever had (2026-09-12): four
// saves of one city from a single play session in the US build. They live in
// GAIUS_TEST_ASSETS/gaius_test_saves/ rather than the game's own directory, so
// playing the original again can't silently overwrite a fixture.
//
// Two things are checked here that no synthetic buffer could ever prove:
//
//  1. model::load -> model::serialize is byte-identical against saves the
//     ORIGINAL ENGINE wrote. Phase 2's round trip was previously proven only
//     against bytes this project made up.
//
//  2. systems::construction's footprints -- transcribed from the executable's
//     placement handlers in Phase 5 -- match what the real game actually wrote
//     into the tile grid. In CAESARUX.SAV, every connected component of each
//     building's seed tile must be a full rectangle of exactly its spec's
//     width x height, read row-major. That also confirms the row-major grid
//     layout the model and viewer have assumed since Phase 1: the Hippodrome
//     (4x2) and Coliseum (3x2) are non-square, so a column-major reading
//     would show them transposed.
//
// Golden against these specific fixtures, the same way test_exepack_golden_
// decode is golden against the analyzed US-build CSR.EXE.
// The drag-built commands on small synthetic grids, against the lifted
// handlers (findings section 18).
void test_construction_drag_rules() {
    std::printf("test_construction_drag_rules (Road/Wall/Plaza/Clear Area handlers)\n");
    using namespace gaius::systems::construction;
    auto f = fresh();
    for (auto& row : f->city.tile) row.fill(0x1D);
    DragState drag;

    // A lone road piece, then one to its east: both horizontal.
    f->city.operational_state[10][10] = 0x0F;
    CHECK(place_road(f->city, drag, 10, 10));
    CHECK(f->city.tile[10][10] == 0x37);
    CHECK(f->city.operational_state[10][10] == 0);
    CHECK(place_road(f->city, drag, 11, 10));
    CHECK(f->city.tile[10][11] == 0x37 && f->city.tile[10][10] == 0x37);

    // A road south of a lone piece turns both vertical.
    CHECK(place_road(f->city, drag, 30, 30));
    CHECK(place_road(f->city, drag, 30, 31));
    CHECK(f->city.tile[31][30] == 0x36 && f->city.tile[30][30] == 0x36);

    // Refused: below the buildable range.
    f->city.tile[40][40] = 0x10;
    CHECK(!place_road(f->city, drag, 40, 40));

    // A road over water builds a crossing.
    f->city.tile[50][50] = 0x4A;
    CHECK(place_road(f->city, drag, 50, 50));
    CHECK(f->city.tile[50][50] == 0x82);

    // A wall across a vertical road makes a gate; walls tile like roads.
    CHECK(place_wall(f->city, drag, 30, 30));
    CHECK(f->city.tile[30][30] == 0x94);
    CHECK(place_wall(f->city, drag, 60, 60));
    CHECK(f->city.tile[60][60] == 0x92);

    // Plaza paves a road piece once.
    CHECK(place_plaza(f->city, 11, 10));
    CHECK((f->city.operational_state[10][11] & 0x10) != 0);
    CHECK(!place_plaza(f->city, 11, 10));
    CHECK(!place_plaza(f->city, 12, 10));  // open ground

    // Clear Area: a road to open ground, a crossing back to water, and a
    // market demolished whole into rubble, one random draw per cell.
    gaius::systems::month::Random random;
    CHECK(clear_area(f->city, random, 10, 10));
    CHECK(f->city.tile[10][10] == 0x1D);
    CHECK(clear_area(f->city, random, 50, 50));
    CHECK(f->city.tile[50][50] == 0x4A);
    CHECK(place(f->city, CommandId::Market, 70, 70));
    gaius::systems::month::Random expect = random;
    CHECK(clear_area(f->city, random, 71, 71));  // any cell of the building
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            expect.advance();
            CHECK(f->city.tile[70 + dy][70 + dx] == 0xA7 + (expect.walk & 3));
            CHECK(f->city.operational_state[70 + dy][70 + dx] == 0);
        }
    }
    CHECK(random.walk == expect.walk && random.lfsr == expect.lfsr);
    CHECK(!clear_area(f->city, random, 12, 10));  // open ground: nothing to clear
}

// Every road network in four real saves, rebuilt cell by cell with
// place_road: road pieces (0x36-0x40) are reset to open ground and placed
// again in row order.
void test_construction_road_rebuild_real_saves() {
    std::printf("test_construction_road_rebuild_real_saves (place_road vs four real saves)\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }
    using namespace gaius::systems::construction;
    struct Case {
        const char* name;
        int roads, identical;
    };
    const Case cases[] = {{"CAESARXX.SAV", 40, 40}, {"CAESARWX.SAV", 90, 90}, {"CAESARVX.SAV", 141, 137}, {"CAESARUX.SAV", 153, 149}};
    for (const Case& c : cases) {
        fs::path p = fs::path(dir) / "gaius_test_saves" / c.name;
        if (!fs::exists(p)) {
            skip(std::string(c.name) + " not found");
            continue;
        }
        CityState st = load(save::load(p.string()));
        const auto saved = std::make_unique<CityMap>(st.city);
        std::vector<std::pair<int, int>> roads;
        for (int y = 0; y < kCityH; ++y) {
            for (int x = 0; x < kCityW; ++x) {
                if (saved->tile[y][x] >= 0x36 && saved->tile[y][x] <= 0x40) {
                    roads.push_back({x, y});
                    st.city.tile[y][x] = 0x1D;
                }
            }
        }
        DragState drag;
        int refused = 0;
        for (const auto& [x, y] : roads) refused += !place_road(st.city, drag, x, y);
        int identical = 0, others_changed = 0;
        for (int y = 0; y < kCityH; ++y) {
            for (int x = 0; x < kCityW; ++x) {
                const bool road = saved->tile[y][x] >= 0x36 && saved->tile[y][x] <= 0x40;
                if (road) identical += st.city.tile[y][x] == saved->tile[y][x];
                else others_changed += st.city.tile[y][x] != saved->tile[y][x];
            }
        }
        std::printf("  %s: %d/%zu road tiles rebuilt identical (refused %d, other cells changed %d)\n", c.name,
                    identical, roads.size(), refused, others_changed);
        CHECK(static_cast<int>(roads.size()) == c.roads);
        CHECK(identical == c.identical);
        CHECK(refused == 0 && others_changed == 0);
    }
}

// 2EF9:1425 from the image's initial state, computed independently.
void test_month_random_sequence() {
    std::printf("test_month_random_sequence (2EF9:1425 from the image's initial state)\n");
    struct Expect {
        int walk, low7, prev_low7, draw;
    };
    const Expect expected[] = {
        {58, 27, 49, 16411}, {63, 13, 27, 24589}, {69, 6, 13, 28678}, {72, 3, 6, 14339}, {73, 1, 3, 23553},
    };
    gaius::systems::month::Random r;
    for (const Expect& e : expected) {
        r.advance();
        CHECK(r.walk == e.walk && r.low7 == e.low7 && r.prev_low7 == e.prev_low7 && r.draw == e.draw);
        CHECK(r.lfsr == e.draw);
    }
}

// 106 steps a month; month wraps into year; the 18-month counter; and
// exactly five random draws a month (step 80, and four at step 105).
void test_month_calendar_and_draws() {
    std::printf("test_month_calendar_and_draws (0x29476 calendar, 5 draws a month)\n");
    using namespace gaius::systems::month;
    auto f = fresh();
    SimState sim;
    sim.month = 11;
    sim.year = -1;
    Random expect = sim.random;
    for (int i = 0; i < 5; ++i) expect.advance();

    int steps = 0;
    do {
        run_step(f->city, sim);
        ++steps;
    } while (sim.step != 0);
    CHECK(steps == kStepsPerMonth);
    CHECK(sim.month == 0 && sim.year == 0);
    CHECK(sim.month_counter_18 == 1);
    CHECK(sim.random.walk == expect.walk && sim.random.lfsr == expect.lfsr);

    for (int m = 1; m < 18; ++m) run_month(f->city, sim);
    CHECK(sim.month_counter_18 == 0);
    CHECK(sim.month == 5 && sim.year == 1);
}

// Rows 0-80 share one growth value, rows 81-99 the next: the draw at step 80
// happens after that step's housing row.
void test_month_growth_changes_after_step_80() {
    std::printf("test_month_growth_changes_after_step_80 (0x294CF growth vs the step-80 draw)\n");
    using namespace gaius::systems::month;
    auto f = fresh();
    for (int row : {80, 81}) {
        f->city.tile[row][5] = 0xC8;
        f->city.service_flags[row][5] = 0x20;  // growth applies only with C9D4.20
    }
    SimState sim;
    sim.land_value_growth_base = 3;
    sim.step = 80;
    Random after = sim.random;
    after.advance();

    run_step(f->city, sim);  // row 80, then the draw
    run_step(f->city, sim);  // row 81
    const int growth_80 = 3 + (Random{}.walk & 3) - 1;
    const int growth_81 = 3 + (after.walk & 3) - 1;
    CHECK(growth_80 != growth_81);
    CHECK(f->city.land_value[80][5] == growth_80);
    CHECK(f->city.land_value[81][5] == growth_81);
    CHECK(sim.random.walk == after.walk);
}

void test_month_state_from_saves() {
    std::printf("test_month_state_from_saves (calendar globals from four real saves)\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }
    struct Case {
        const char* name;
        int month, year;
    };
    const Case cases[] = {{"CAESARXX.SAV", 9, -11}, {"CAESARWX.SAV", 1, -7}, {"CAESARVX.SAV", 4, -2}, {"CAESARUX.SAV", 6, -1}};
    for (const Case& c : cases) {
        fs::path p = fs::path(dir) / "gaius_test_saves" / c.name;
        if (!fs::exists(p)) {
            skip(std::string(c.name) + " not found");
            continue;
        }
        const CityState st = load(save::load(p.string()));
        auto sim = gaius::systems::month::sim_state_from_save(st);
        std::printf("  %s: month %d, year %d, population units %d\n", c.name, sim.month, sim.year, sim.population_units);
        CHECK(sim.month == c.month && sim.year == c.year);
        CHECK(sim.population_units == gaius::systems::housing::population_units(st.city) ||
              std::string(c.name) == "CAESARVX.SAV");  // VX: one upgrade after the count
    }
}

// Walkers (0x6733/0x6946): an active actor's MOREMEN frame lands with its
// bottom edge at y + 8, an inactive one isn't drawn, and a building in the
// next row is drawn over it -- including the rows it rises above its
// footprint.
void test_render_walkers() {
    std::printf("test_render_walkers (actor list and draw order, 0x6733 / 0x6946)\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }
    gaius::render::CitySprites sprites;
    try {
        sprites = gaius::render::load_city_sprites(dir);
    } catch (const FormatError& e) {
        skip(std::string("city sprites not loadable: ") + e.what());
        return;
    }
    const int frame = 2;
    if (static_cast<int>(sprites.people.frames.size()) <= frame || sprites.people.frames[frame].pixels.empty()) {
        skip("MOREMEN.PL8 frame 2 missing");
        return;
    }
    const PL8Frame& sprite = sprites.people.frames[frame];

    auto f = fresh();
    for (auto& row : f->city.tile) row.fill(0xFF);  // draws nothing
    std::array<Actor, kActorCount> actors{};
    auto set = [](Actor& a, int fr, int x, int y, int active, int type) {
        a.raw[0] = static_cast<uint8_t>(fr);
        a.raw[2] = static_cast<uint8_t>(x & 0xFF);
        a.raw[3] = static_cast<uint8_t>(x >> 8);
        a.raw[4] = static_cast<uint8_t>(y & 0xFF);
        a.raw[5] = static_cast<uint8_t>(y >> 8);
        a.raw[6] = static_cast<uint8_t>(active);
        a.raw[7] = static_cast<uint8_t>(type);
    };
    const int ax = 80, ay = 64;  // feet at y 72: row 4
    set(actors[3], frame, ax, ay, 1, 8);

    IndexedImage walker;
    gaius::render::render_city(f->city, sprites, 0, 0, 20, 11, walker, {}, &actors);
    int drawn = 0, wrong = 0;
    for (int y = 0; y < sprite.height; ++y) {
        for (int x = 0; x < sprite.width; ++x) {
            const uint8_t p = sprite.pixels[static_cast<size_t>(y) * sprite.width + x];
            if (p == 0) continue;
            ++drawn;
            const int oy = ay + 8 - sprite.height + y, ox = ax + x;
            wrong += walker.pixels[static_cast<size_t>(oy) * walker.width + ox] != p;
        }
    }
    CHECK(drawn > 0);
    CHECK(wrong == 0);

    actors[3].raw[6] = 0;  // inactive
    IndexedImage none;
    gaius::render::render_city(f->city, sprites, 0, 0, 20, 11, none, {}, &actors);
    CHECK(std::all_of(none.pixels.begin(), none.pixels.end(), [](uint8_t p) { return p == 0; }));
    actors[3].raw[6] = 1;

    // A house (0xCF: 16x16, rising 12 rows) in row 5, below the walker's row.
    f->city.tile[5][5] = 0xCF;
    f->city.operational_state[5][5] = 0;
    IndexedImage both, building;
    gaius::render::render_city(f->city, sprites, 0, 0, 20, 11, both, {}, &actors);
    gaius::render::render_city(f->city, sprites, 0, 0, 20, 11, building);
    int covered = 0, mismatches = 0;
    for (size_t i = 0; i < both.pixels.size(); ++i) {
        const uint8_t expect = building.pixels[i] ? building.pixels[i] : walker.pixels[i];
        mismatches += both.pixels[i] != expect;
        covered += building.pixels[i] != 0 && walker.pixels[i] != 0;
    }
    std::printf("  walker pixels %d; covered by the house in front %d; composite mismatches %d\n", drawn, covered,
                mismatches);
    CHECK(covered > 0);
    CHECK(mismatches == 0);
}

// The building metrics table (3496:14B2) against HOUSES.PL8: frame i is
// width x (height + extra) of tile 0xC8 + i, for tiles 0xC8-0xF2. (Frame 43,
// an 8x16 sprite, isn't one: tile 0xF3 draws from HOUSES2.PL8.)
void test_render_building_metrics() {
    std::printf("test_render_building_metrics (3496:14B2 table vs HOUSES.PL8 frame sizes)\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }
    fs::path p = fs::path(dir) / "HOUSES.PL8";
    if (!fs::exists(p)) { skip("HOUSES.PL8 not found under " + dir); return; }
    const PL8Sheet sheet = pl8::load(p.string());
    int checked = 0, matched = 0;
    for (size_t i = 0; i < 43 && i < sheet.frames.size(); ++i) {
        if (sheet.frames[i].pixels.empty()) continue;
        const auto& m = gaius::render::kBuildingMetrics[i];
        ++checked;
        matched += sheet.frames[i].width == m.width && sheet.frames[i].height == m.height + m.extra;
    }
    std::printf("  %d/%d frames match\n", matched, checked);
    CHECK(checked == 43);
    CHECK(matched == checked);
}

// render_city against DOSBox captures of the running game. The screenshots'
// own cities aren't available as saves, so each case rebuilds just one
// building at the cell where the capture shows it, renders the view, and
// compares that building's pixels at 6-bit DAC level. Footprints are copied
// opaque and must match exactly; the rows a sprite rises above its footprint
// are transparent, so only the pixels the render actually draws there are
// compared.
void test_render_city_matches_screenshots() {
    std::printf("test_render_city_matches_screenshots (render_city vs DOSBox captures)\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }
    const fs::path screens = fs::path(dir) / "gaius_test_screens";
    if (!fs::exists(screens)) { skip("gaius_test_screens/ not found under " + dir); return; }
    gaius::render::CitySprites sprites;
    try {
        sprites = gaius::render::load_city_sprites(dir);
    } catch (const FormatError& e) {
        skip(std::string("city sprites not loadable: ") + e.what());
        return;
    }

    struct Case {
        const char* screenshot;
        int tile;
        int col, row, w_cells, h_cells;
        int extra;  // rows above the footprint to compare
    };
    const Case cases[] = {
        {"2049596-caesar-dos-main-game-screen.png", 0xF5, 14, 6, 3, 3, 2},  // top rows + actor-less bottom row
        {"2049596-caesar-dos-main-game-screen.png", 0xED, 15, 3, 2, 2, 0},
        {"2049596-caesar-dos-main-game-screen.png", 0xC9, 9, 3, 1, 1, 0},
        {"2049596-caesar-dos-main-game-screen.png", 0xF4, 10, 3, 2, 2, 0},  // market, HOUSES2 frame 5
        {"7910232-caesar-dos-forum-romanum.png", 0xCD, 7, 3, 2, 1, 8},      // house pair rising 8 rows
    };
    for (const Case& c : cases) {
        const fs::path shot_path = screens / c.screenshot;
        if (!fs::exists(shot_path)) {
            skip(std::string(c.screenshot) + " not found");
            continue;
        }
        auto f = fresh();
        // Everything else is tile 0xFF, which draws nothing, so the rows a
        // sprite rises into stay index 0 wherever it's transparent.
        for (auto& row : f->city.tile) row.fill(0xFF);
        for (int dy = 0; dy < c.h_cells; ++dy) {
            for (int dx = 0; dx < c.w_cells; ++dx) {
                f->city.tile[c.row + dy][c.col + dx] = static_cast<uint8_t>(c.tile);
                f->city.operational_state[c.row + dy][c.col + dx] = static_cast<uint8_t>(4 * dy + dx);
            }
        }
        IndexedImage view;
        gaius::render::render_city(f->city, sprites, 0, 0, 20, 11, view);

        int w, h, channels;
        unsigned char* shot = stbi_load(shot_path.string().c_str(), &w, &h, &channels, 3);
        CHECK(shot != nullptr && w == 320 && h == 200);
        if (!shot) continue;
        int compared = 0, mismatches = 0;
        const int x0 = c.col * 16, y_foot = c.row * 16;
        for (int y = y_foot - c.extra; y < y_foot + c.h_cells * 16; ++y) {
            for (int x = x0; x < x0 + c.w_cells * 16; ++x) {
                const uint8_t idx = view.pixels[static_cast<size_t>(y) * view.width + x];
                if (y < y_foot && idx == 0) continue;  // transparent above the footprint
                const RGB col = sprites.palette.colors[idx];
                const unsigned char* s = shot + (y * w + x) * 3;
                ++compared;
                if ((col.r >> 2) != (s[0] >> 2) || (col.g >> 2) != (s[1] >> 2) || (col.b >> 2) != (s[2] >> 2)) ++mismatches;
            }
        }
        stbi_image_free(shot);
        std::printf("  tile 0x%02X at cell (%d,%d): %d/%d pixels match\n", c.tile, c.col, c.row, compared - mismatches,
                    compared);
        CHECK(compared > 0);
        CHECK(mismatches == 0);
    }
}

// DS:0F64, the engine's character -> FONT1.PL8 frame table.
void test_ui_game_font_table() {
    std::printf("test_ui_game_font_table (DS:0F64 char -> frame)\n");
    CHECK(gaius::ui::game_glyph_frame('a') == 0);
    CHECK(gaius::ui::game_glyph_frame('z') == 25);
    CHECK(gaius::ui::game_glyph_frame('A') == 26);
    CHECK(gaius::ui::game_glyph_frame('Z') == 51);
    CHECK(gaius::ui::game_glyph_frame('1') == 52);
    CHECK(gaius::ui::game_glyph_frame('0') == 61);
    CHECK(gaius::ui::game_glyph_frame('.') == 68);
    CHECK(gaius::ui::game_glyph_frame(' ') == -1);
    CHECK(gaius::ui::game_glyph_frame('\x01') == -1);
    CHECK(gaius::ui::game_text_width("Housing", 1) == 56);
}

// The game's font against a DOSBox capture of the building menu, whose title
// reads "Housing" at (204,12) in FONT1.PL8. Drawn over a sentinel colour so
// only the pixels the glyphs cover are compared, at 6-bit DAC level.
void test_ui_game_font_matches_screenshot() {
    std::printf("test_ui_game_font_matches_screenshot (FONT1.PL8 via DS:0F64 vs DOSBox capture)\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }
    const fs::path shot_path = fs::path(dir) / "gaius_test_screens" / "2053010-caesar-dos-building-your-city.png";
    const fs::path shade = fs::path(dir) / "SHADE.256";
    if (!fs::exists(shot_path) || !fs::exists(shade)) {
        skip("building-menu screenshot or SHADE.256 not found under " + dir);
        return;
    }
    gaius::ui::GameFont font;
    try {
        font = gaius::ui::load_game_font(dir, pal256::load(shade.string()));
    } catch (const FormatError& e) {
        skip(std::string("FONT1.PL8 not loadable: ") + e.what());
        return;
    }

    const int w = 320, h = 200;
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    for (size_t i = 0; i < rgb.size(); i += 3) {
        rgb[i] = 1;
        rgb[i + 1] = 2;
        rgb[i + 2] = 3;
    }
    gaius::ui::draw_game_text(rgb, w, h, 204, 12, "Housing", 1, font);

    int sw, sh, sc;
    unsigned char* shot = stbi_load(shot_path.string().c_str(), &sw, &sh, &sc, 3);
    CHECK(shot != nullptr && sw == w && sh == h);
    if (!shot) return;
    int drawn = 0, mismatches = 0;
    for (int y = 12; y < 12 + gaius::ui::kGameGlyphPx; ++y) {
        for (int x = 204; x < 204 + 7 * gaius::ui::kGameGlyphPx; ++x) {
            const uint8_t* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
            if (p[0] == 1 && p[1] == 2 && p[2] == 3) continue;
            const unsigned char* s = shot + (y * w + x) * 3;
            ++drawn;
            if ((p[0] >> 2) != (s[0] >> 2) || (p[1] >> 2) != (s[1] >> 2) || (p[2] >> 2) != (s[2] >> 2)) ++mismatches;
        }
    }
    stbi_image_free(shot);
    std::printf("  \"Housing\": %d/%d glyph pixels match\n", drawn - mismatches, drawn);
    CHECK(drawn > 50);
    CHECK(mismatches == 0);
}

void test_save_corpus_real() {
    std::printf("test_save_corpus_real (four saves from a real play session)\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }
    fs::path saves = fs::path(dir) / "gaius_test_saves";
    const char* names[] = {"CAESARXX.SAV", "CAESARWX.SAV", "CAESARVX.SAV", "CAESARUX.SAV"};
    for (const char* n : names) {
        if (!fs::exists(saves / n)) {
            skip("real save fixture missing: " + (saves / n).string());
            return;
        }
    }

    for (const char* n : names) {
        save::SaveFile sf = save::load((saves / n).string());
        CHECK(sf.raw.size() == save::kSaveSize);
        CityState st = gaius::model::load(sf);
        save::SaveFile out = gaius::model::serialize(st);
        CHECK(out.raw == sf.raw);
    }

    save::SaveFile ux = save::load((saves / "CAESARUX.SAV").string());
    CityState st = gaius::model::load(ux);

    struct Component { int cells, w, h, r0, c0; };
    auto components = [&](uint8_t tid) {
        std::vector<Component> out;
        std::vector<uint8_t> seen(10000, 0);
        for (int r = 0; r < 100; ++r) {
            for (int c = 0; c < 100; ++c) {
                if (seen[r * 100 + c] || st.city.tile[r][c] != tid) continue;
                std::vector<std::pair<int, int>> stack{{r, c}};
                seen[r * 100 + c] = 1;
                int n = 0, r0 = r, r1 = r, c0 = c, c1 = c;
                while (!stack.empty()) {
                    auto [y, x] = stack.back();
                    stack.pop_back();
                    ++n;
                    r0 = y < r0 ? y : r0;
                    r1 = y > r1 ? y : r1;
                    c0 = x < c0 ? x : c0;
                    c1 = x > c1 ? x : c1;
                    const int dy[] = {1, -1, 0, 0};
                    const int dx[] = {0, 0, 1, -1};
                    for (int k = 0; k < 4; ++k) {
                        int ny = y + dy[k], nx = x + dx[k];
                        if (ny < 0 || ny >= 100 || nx < 0 || nx >= 100) continue;
                        if (seen[ny * 100 + nx] || st.city.tile[ny][nx] != tid) continue;
                        seen[ny * 100 + nx] = 1;
                        stack.push_back({ny, nx});
                    }
                }
                out.push_back({n, c1 - c0 + 1, r1 - r0 + 1, r0, c0});
            }
        }
        return out;
    };

    // Every placing command whose seed tile the player actually built in this
    // session. (Temple is absent on purpose: its 0xD8 seed had already grown
    // into its 0xE0 stage by this save.)
    const CommandId built[] = {CommandId::BathHouses, CommandId::Oracle,   CommandId::School,
                               CommandId::Hospital,   CommandId::Prefecture, CommandId::Barracks,
                               CommandId::Theater,    CommandId::Coliseum, CommandId::Hippodrome,
                               CommandId::HeavyIndustry, CommandId::Market};
    for (CommandId id : built) {
        PlacementSpec spec = placement_spec(id);
        int w = spec.kind == PlacementKind::SingleCell ? 1 : spec.width;
        int h = spec.kind == PlacementKind::SingleCell ? 1 : spec.height;
        auto comps = components(spec.seed_tile);
        CHECK(!comps.empty());
        for (const auto& cp : comps) {
            CHECK(cp.w == w);
            CHECK(cp.h == h);
            CHECK(cp.cells == w * h);
            // ...and the engine wrote each cell's part index, 4*dy + dx.
            for (int dy = 0; dy < cp.h; ++dy)
                for (int dx = 0; dx < cp.w; ++dx)
                    CHECK((st.city.operational_state[cp.r0 + dy][cp.c0 + dx] & 0x0F) == 4 * dy + dx);
        }
    }
}

// formats/save's kGlobalWordDsAddress (the save writer's address order),
// checked against real saves three ways that don't depend on each other.
void test_save_corpus_globals() {
    std::printf("test_save_corpus_globals (writer-order DS table vs real saves)\n");
    using save::kGlobalWordDsAddress;
    CHECK(kGlobalWordDsAddress[0] == 0x6CE2);
    CHECK(kGlobalWordDsAddress[0x3E / 2] == 0x6CA2);  // the treasury
    CHECK(kGlobalWordDsAddress[0xC2 / 2] == 0x6C0E);  // one variable written twice...
    CHECK(kGlobalWordDsAddress[0xDC / 2] == 0x6C0E);  // ...here too
    bool has_6cb8 = false;
    for (uint16_t a : kGlobalWordDsAddress) has_6cb8 = has_6cb8 || a == 0x6CB8;
    CHECK(!has_6cb8);  // never saved

    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }
    fs::path saves = fs::path(dir) / "gaius_test_saves";
    const char* names[] = {"CAESARXX.SAV", "CAESARWX.SAV", "CAESARVX.SAV", "CAESARUX.SAV"};
    for (const char* n : names) {
        if (!fs::exists(saves / n)) {
            skip("real save fixture missing: " + (saves / n).string());
            return;
        }
    }

    auto word_at_ds = [](const save::SaveFile& sf, uint16_t ds, int copy) -> int {
        const uint8_t* blk = sf.block("global_words_128").first;
        int seen = 0;
        for (size_t i = 0; i < 128; ++i) {
            if (kGlobalWordDsAddress[i] != ds) continue;
            if (seen++ != copy) continue;
            return blk[2 * i] | (blk[2 * i + 1] << 8);
        }
        return -1;
    };

    const int funds[] = {7182, 4692, 3210, 3144};
    for (int k = 0; k < 4; ++k) {
        save::SaveFile sf = save::load((saves / names[k]).string());
        const uint8_t* tiles = sf.block("city_tiles_100x100").first;

        // 1. The treasury, falling as the player built.
        CHECK(word_at_ds(sf, 0x6CA2, 0) == funds[k]);
        // 2. The variable saved twice holds one value in both copies.
        CHECK(word_at_ds(sf, 0x6C0E, 0) == word_at_ds(sf, 0x6C0E, 1));
        // 3. Two statistics the tick publishes equal the grid's own counts, using
        //    the tile sets read off the handlers that feed each counter.
        int buildings = 0, roads = 0;
        for (int i = 0; i < 10000; ++i) {
            const uint8_t t = tiles[i];
            if ((t >= 0xC8 && t <= 0xE8) || (t >= 0xEA && t <= 0xF3) || t == 0xF5 || t == 0xF6) ++buildings;
            if (t >= 0x36 && t <= 0x40) ++roads;
        }
        CHECK(word_at_ds(sf, 0x6BF2, 0) == buildings);
        CHECK(word_at_ds(sf, 0x6BF0, 0) == roads);
    }
}

// The strongest check in this suite. The engine rebuilds A2C4 coverage and
// C9D4's service bits from scratch every tick (routine 0x2C8D3 resets them,
// then every dispatched tile adds to them), so one reset_tick plus one full
// dispatch pass over a real save's own tile grid must reproduce the layers the
// ORIGINAL ENGINE saved -- every cell, every service bit. It does, in all four
// saves: systems::service checked against the game itself, not against
// transcribed parameters. tools/sim_check prints the same comparison with
// per-quarter and per-tile breakdowns when something doesn't match.
void test_save_corpus_simulation() {
    std::printf("test_save_corpus_simulation (reset_tick + dispatch reproduces saved A2C4 and C9D4 service bits)\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }
    fs::path saves = fs::path(dir) / "gaius_test_saves";
    const char* names[] = {"CAESARXX.SAV", "CAESARWX.SAV", "CAESARVX.SAV", "CAESARUX.SAV"};
    for (const char* n : names) {
        if (!fs::exists(saves / n)) {
            skip("real save fixture missing: " + (saves / n).string());
            return;
        }
    }

    const uint8_t bits[6] = {0x01, 0x04, 0x08, 0x20, 0x40, 0x80};
    for (const char* n : names) {
        save::SaveFile sf = save::load((saves / n).string());
        auto saved = std::make_unique<CityState>(gaius::model::load(sf));
        auto f = fresh();
        f->city = saved->city;
        const uint8_t* globals = sf.block("global_words_128").first;
        for (int i = 0; i < 128; ++i)
            if (save::kGlobalWordDsAddress[i] == 0x6BF8)
                f->svc.housing_coverage_base = globals[2 * i] | (globals[2 * i + 1] << 8);

        rebuild_services(f->city, f->svc);

        int coverage_match = 0;
        int bit_match[6] = {0, 0, 0, 0, 0, 0};
        for (int y = 0; y < gaius::model::kCityH; ++y) {
            for (int x = 0; x < gaius::model::kCityW; ++x) {
                if (f->city.coverage[y][x] == saved->city.coverage[y][x]) ++coverage_match;
                for (int b = 0; b < 6; ++b) {
                    if ((f->city.service_flags[y][x] & bits[b]) == (saved->city.service_flags[y][x] & bits[b]))
                        ++bit_match[b];
                }
            }
        }
        CHECK(coverage_match == 10000);
        for (int b = 0; b < 6; ++b) CHECK(bit_match[b] == 10000);
    }
}

// population_units against the engine's own count, DS:0x6C10 (stored x4 at
// DS:0x6C0E), in four real saves. The engine counts at step 101 of each month
// and houses change during steps 0-99 of the next, so a save taken mid-month
// can hold tiles that changed after the count. Three saves match exactly.
// CAESARVX.SAV is 2 units higher and holds exactly one 0xCF: the +2 of a single
// 0xCB -> 0xCF upgrade made after that month's count.
void test_save_corpus_population() {
    std::printf("test_save_corpus_population (population_units vs saved DS:0x6C10)\n");
    std::string dir = test_assets_dir();
    if (dir.empty()) { skip("GAIUS_TEST_ASSETS not set"); return; }
    fs::path saves = fs::path(dir) / "gaius_test_saves";
    const char* names[] = {"CAESARXX.SAV", "CAESARWX.SAV", "CAESARVX.SAV", "CAESARUX.SAV"};
    for (const char* n : names) {
        if (!fs::exists(saves / n)) {
            skip("real save fixture missing: " + (saves / n).string());
            return;
        }
    }
    auto word_at_ds = [](const save::SaveFile& sf, uint16_t ds) -> int {
        const uint8_t* blk = sf.block("global_words_128").first;
        for (size_t i = 0; i < 128; ++i)
            if (save::kGlobalWordDsAddress[i] == ds) return blk[2 * i] | (blk[2 * i + 1] << 8);
        return -1;
    };
    for (const char* n : names) {
        save::SaveFile sf = save::load((saves / n).string());
        auto st = std::make_unique<CityState>(gaius::model::load(sf));
        const int units = population_units(st->city);
        const int saved = word_at_ds(sf, 0x6C10);
        CHECK(word_at_ds(sf, 0x6C0E) == 4 * saved);
        if (std::string(n) == "CAESARVX.SAV") {
            int cf_cells = 0;
            for (int y = 0; y < gaius::model::kCityH; ++y)
                for (int x = 0; x < gaius::model::kCityW; ++x)
                    if (st->city.tile[y][x] == 0xCF) ++cf_cells;
            CHECK(cf_cells == 1);
            CHECK(units - saved == kPopulationUnitsPerCell[0xCF - 0xC8] - kPopulationUnitsPerCell[0xCB - 0xC8]);
        } else {
            CHECK(units == saved);
        }
    }
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

// --- Phase 5: toolbar UI (ui/metrics, ui/font, ui/toolbar) -------------
//
// All headless: ui/ deliberately has no SDL dependency, so layout and
// hit-testing are testable as pure functions. The invariant these exist
// to protect is the one in ui/toolbar.hpp's header -- a drawn button and
// its touch target can never drift apart -- which is checked directly by
// test_ui_hit_test_matches_drawn_buttons below.

void test_ui_metrics_scale_together() {
    std::printf("test_ui_metrics_scale_together\n");
    using namespace gaius::ui;

    // The masterplan's floor clause: "Original toolbar icon sizes are a
    // floor, not a ceiling, on touch targets." Every breakpoint must be
    // at least the original 16px, and the touch ones strictly above it.
    for (auto bp : {Breakpoint::Desktop, Breakpoint::Handheld, Breakpoint::Phone, Breakpoint::Tv}) {
        Metrics m = metrics_for(bp);
        CHECK(m.scale >= 1);
        CHECK(m.icon_px >= kOriginalIconPx);
        CHECK(m.button_px() > m.icon_px);  // padding is real, not zero
    }
    // Desktop reproduces the original exactly; touch goes above it.
    CHECK(metrics_for(Breakpoint::Desktop).icon_px == kOriginalIconPx);
    CHECK(metrics_for(Breakpoint::Phone).icon_px > kOriginalIconPx);
    CHECK(metrics_for(Breakpoint::Handheld).icon_px > kOriginalIconPx);

    // "resizes toolbar icons, text, and hit targets together" -- text
    // and icon must move in lockstep, not independently.
    Metrics d = metrics_for(Breakpoint::Desktop);
    Metrics p = metrics_for(Breakpoint::Phone);
    CHECK(p.icon_px == d.icon_px * (p.scale / d.scale));
    CHECK(p.glyph_scale == d.glyph_scale * (p.scale / d.scale));
    CHECK(p.label_h == d.label_h * (p.scale / d.scale));

    // The measured constants: PANEL1.VPX's panel is rows 176..199.
    CHECK(kOriginalPanelTop + kOriginalPanelH == kOriginalScreenH);

    // Touch presence, not window size, decides. The regression this
    // guards: a small *window* on a desktop must stay Desktop, or a mouse
    // user gets 36px buttons and a panel filling half the screen.
    CHECK(breakpoint_for(960, 600, false) == Breakpoint::Desktop);
    CHECK(breakpoint_for(320, 200, false) == Breakpoint::Desktop);
    CHECK(breakpoint_for(3840, 2160, false) == Breakpoint::Desktop);  // Tv is opt-in, never inferred
    CHECK(breakpoint_for(960, 440, true) == Breakpoint::Phone);
    CHECK(breakpoint_for(1280, 800, true) == Breakpoint::Handheld);
    // Orientation must not change the answer.
    CHECK(breakpoint_for(440, 960, true) == breakpoint_for(960, 440, true));
}

void test_ui_toolbar_layout() {
    std::printf("test_ui_toolbar_layout\n");
    using namespace gaius::ui;
    using gaius::systems::construction::CommandId;

    const CommandId tools[] = {CommandId::Housing, CommandId::Well,     CommandId::Temple,
                               CommandId::Theater, CommandId::Coliseum, CommandId::Hippodrome};
    const int n = 6;

    for (auto bp : {Breakpoint::Desktop, Breakpoint::Handheld, Breakpoint::Phone, Breakpoint::Tv}) {
        Metrics m = metrics_for(bp);
        Toolbar bar(tools, n, m, 320, 200);

        CHECK(bar.count() == n);
        // Panel is anchored to the bottom of the screen and fits in it.
        Rect p = bar.panel();
        CHECK(p.x == 0);
        CHECK(p.w == 320);
        CHECK(p.y + p.h == 200);
        CHECK(p.h > 0 && p.h < 200);
        CHECK(bar.rows() * bar.columns() >= n);

        // Every button lies inside the panel, and none overlap.
        for (int i = 0; i < n; ++i) {
            Rect b = bar.button(i);
            CHECK(b.w == m.button_px() && b.h == m.button_px());
            CHECK(b.y >= p.y && b.y + b.h <= p.y + p.h);
            CHECK(b.x >= 0 && b.x + b.w <= 320);
            for (int j = i + 1; j < n; ++j) {
                Rect o = bar.button(j);
                bool disjoint = b.x + b.w <= o.x || o.x + o.w <= b.x || b.y + b.h <= o.y || o.y + o.h <= b.y;
                CHECK(disjoint);
            }
        }
    }

    // Out-of-range indices give an empty rect, not UB -- button() is
    // reachable from input handling.
    Toolbar bar(tools, n, metrics_for(Breakpoint::Desktop), 320, 200);
    CHECK(bar.button(-1).w == 0);
    CHECK(bar.button(n).w == 0);

    // The real viewer ring must fit in one row at desktop scale, the way
    // the original's 16 icons did in its measured 24px bar.
    const CommandId ring[] = {CommandId::Housing,  CommandId::Well,       CommandId::Fountain, CommandId::ReservoirPipe,
                              CommandId::Temple,   CommandId::BathHouses, CommandId::Hospital, CommandId::School,
                              CommandId::Oracle,   CommandId::Theater,    CommandId::Coliseum, CommandId::Hippodrome,
                              CommandId::Barracks, CommandId::Prefecture, CommandId::Market,   CommandId::HeavyIndustry};
    Toolbar full(ring, 16, metrics_for(Breakpoint::Desktop), 320, 200);
    CHECK(full.rows() == 1);
    CHECK(full.panel().h <= 32);  // comparable to the original's 24px bar
}

void test_ui_hit_test_matches_drawn_buttons() {
    std::printf("test_ui_hit_test_matches_drawn_buttons\n");
    using namespace gaius::ui;
    using gaius::systems::construction::CommandId;

    const CommandId ring[] = {CommandId::Housing,  CommandId::Well,       CommandId::Fountain, CommandId::ReservoirPipe,
                              CommandId::Temple,   CommandId::BathHouses, CommandId::Hospital, CommandId::School,
                              CommandId::Oracle,   CommandId::Theater,    CommandId::Coliseum, CommandId::Hippodrome,
                              CommandId::Barracks, CommandId::Prefecture, CommandId::Market,   CommandId::HeavyIndustry};
    const int n = 16;

    // THE invariant: for every button, at every breakpoint, hit-testing
    // its drawn rectangle returns that button -- centre and all four
    // corners. If rendering and hit-testing ever computed geometry
    // separately, this is what would catch it.
    for (auto bp : {Breakpoint::Desktop, Breakpoint::Handheld, Breakpoint::Phone, Breakpoint::Tv}) {
        Toolbar bar(ring, n, metrics_for(bp), 320, 200);
        for (int i = 0; i < n; ++i) {
            Rect b = bar.button(i);
            CHECK(bar.hit_test(b.x + b.w / 2, b.y + b.h / 2) == i);
            CHECK(bar.hit_test(b.x, b.y) == i);
            CHECK(bar.hit_test(b.x + b.w - 1, b.y) == i);
            CHECK(bar.hit_test(b.x, b.y + b.h - 1) == i);
            CHECK(bar.hit_test(b.x + b.w - 1, b.y + b.h - 1) == i);
            // Just outside the drawn rect must not hit this button.
            CHECK(bar.hit_test(b.x - 1, b.y) != i);
            CHECK(bar.hit_test(b.x, b.y - 1) != i);
            CHECK(bar.hit_test(b.x + b.w, b.y) != i);
            CHECK(bar.hit_test(b.x, b.y + b.h) != i);
            // Every button is a real touch target of at least the
            // original icon size.
            CHECK(b.w >= kOriginalIconPx && b.h >= kOriginalIconPx);
        }
        // Above the panel is map, not toolbar -- this is what stops a
        // toolbar click from also placing a building.
        Rect p = bar.panel();
        CHECK(bar.hit_test(160, p.y - 1) == -1);
        CHECK(!bar.contains(160, p.y - 1));
        CHECK(bar.contains(160, p.y));
        CHECK(bar.contains(0, 199));
    }
}

void test_ui_font_rendering() {
    std::printf("test_ui_font_rendering\n");
    using namespace gaius::ui;

    CHECK(text_width("", 1) == 0);
    CHECK(text_width("A", 1) == kGlyphW);
    CHECK(text_width("AB", 1) == kGlyphW + kGlyphAdvance);
    // Text scales with the same factor as icons.
    CHECK(text_width("HOUSING", 2) == 2 * text_width("HOUSING", 1));

    const int w = 64, h = 16;
    std::vector<uint8_t> buf(static_cast<size_t>(w) * h * 3, 0);
    draw_text(buf, w, h, 1, 1, "AB", 1, RGB{255, 255, 255});
    size_t lit = 0;
    for (size_t i = 0; i < buf.size(); i += 3)
        if (buf[i] != 0) ++lit;
    CHECK(lit > 0);  // something was actually drawn

    // Lowercase is upcased rather than dropped.
    std::vector<uint8_t> lower(static_cast<size_t>(w) * h * 3, 0);
    draw_text(lower, w, h, 1, 1, "ab", 1, RGB{255, 255, 255});
    CHECK(lower == buf);

    // Clipping: drawing far off every edge must not write out of bounds
    // or crash. (Buffer contents are checked for no growth/corruption.)
    std::vector<uint8_t> edge(static_cast<size_t>(w) * h * 3, 7);
    draw_text(edge, w, h, -100, -100, "CLIP", 1, RGB{1, 2, 3});
    draw_text(edge, w, h, 1000, 1000, "CLIP", 1, RGB{1, 2, 3});
    draw_text(edge, w, h, -2, -2, "CLIP", 3, RGB{1, 2, 3});
    CHECK(edge.size() == static_cast<size_t>(w) * h * 3);
    // A too-small buffer is refused rather than overrun.
    std::vector<uint8_t> tiny(10, 0);
    draw_text(tiny, w, h, 0, 0, "X", 1, RGB{255, 255, 255});
    CHECK(tiny.size() == 10);
}

void test_ui_toolbar_render() {
    std::printf("test_ui_toolbar_render\n");
    using namespace gaius::ui;
    using gaius::systems::construction::CommandId;

    const CommandId tools[] = {CommandId::Housing, CommandId::Hippodrome, CommandId::HeavyIndustry};
    Toolbar bar(tools, 3, metrics_for(Breakpoint::Desktop), 320, 200);

    const int w = 320, h = 200;
    std::vector<uint8_t> frame(static_cast<size_t>(w) * h * 3, 0x11);
    render(bar, 0, -1, gaius::viewer::heat_color, frame, w, h);

    Rect p = bar.panel();
    // The row directly above the panel is untouched map...
    for (int x = 0; x < w; ++x) {
        size_t i = (static_cast<size_t>(p.y - 1) * w + x) * 3;
        CHECK(frame[i] == 0x11);
    }
    // ...and the panel itself was painted over.
    size_t any_changed = 0;
    for (int y = p.y; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            size_t i = (static_cast<size_t>(y) * w + x) * 3;
            if (frame[i] != 0x11) ++any_changed;
        }
    CHECK(any_changed > 0);

    // A too-small buffer is refused rather than overrun.
    std::vector<uint8_t> tiny(10, 0);
    render(bar, 0, -1, gaius::viewer::heat_color, tiny, w, h);
    CHECK(tiny.size() == 10);
}

}  // namespace

int main() {
    std::printf("=== Gaius Phase 0 format tests ===\n\n");

    test_save_block_table_contiguous();
    test_save_view_render_synthetic();
    test_model_city_state_round_trip();
    test_service_bit_table_fixtures();
    test_service_reset_tick();
    test_service_apply_coverage();
    test_service_apply_land_value();
    test_service_apply_flags();
    test_service_evolve_land_value();
    test_service_derive_network_flags();
    test_service_building_handlers();
    test_service_dispatch_tile();
    test_housing_land_value_allows();
    test_housing_development();
    test_housing_population();
    test_construction_command_table_fixtures();
    test_construction_command_table_corpus();
    test_construction_placement_specs();
    test_construction_place();
    test_construction_to_simulation_pipeline();
    test_ui_metrics_scale_together();
    test_ui_toolbar_layout();
    test_ui_hit_test_matches_drawn_buttons();
    test_ui_font_rendering();
    test_ui_toolbar_render();
    test_ui_game_font_table();
    test_ui_game_font_matches_screenshot();
    test_p32_expand_math();
    test_pal256_expand_math();
    test_pl8_synthetic();
    test_format_errors_are_thrown_not_swallowed();
    test_empire2_corpus_roundtrip();
    test_vpx_golden_image();
    test_pal256_corpus();
    test_pl8_corpus_sanity();
    test_pl8_matches_real_screenshots();
    test_construction_drag_rules();
    test_construction_road_rebuild_real_saves();
    test_month_random_sequence();
    test_month_calendar_and_draws();
    test_month_growth_changes_after_step_80();
    test_month_state_from_saves();
    test_render_building_metrics();
    test_render_walkers();
    test_render_city_matches_screenshots();
    test_save_corpus_real();
    test_save_corpus_globals();
    test_save_corpus_simulation();
    test_save_corpus_population();
    test_exepack_golden_decode();

    std::printf("\n=== %d checks run, %d failed, %d test(s) skipped ===\n", g_ran, g_failures, g_skipped);
    return g_failures == 0 ? 0 : 1;
}
