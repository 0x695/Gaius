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

#include "apps/viewer/save_view.hpp"
#include "formats/empire2/empire2.hpp"
#include "formats/exepack/exepack.hpp"
#include "formats/p32/p32.hpp"
#include "formats/pal256/pal256.hpp"
#include "formats/pl8/pl8.hpp"
#include "formats/save/save.hpp"
#include "formats/vpx/vpx.hpp"
#include "model/city_state.hpp"
#include "stb_image.h"
#include "systems/construction.hpp"
#include "systems/housing.hpp"
#include "systems/service.hpp"
#include "ui/font.hpp"
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
    std::printf("test_service_bit_table_fixtures (CAESAR_CITY_STATE_v6.md \"Current C9D4 table\")\n");
    auto find = [](uint8_t mask) -> const C9D4BitInfo* {
        for (const auto& b : kC9D4BitTable)
            if (b.mask == mask) return &b;
        return nullptr;
    };
    const C9D4BitInfo* religious = find(0x20);
    CHECK(religious != nullptr);
    CHECK(religious && std::string(religious->name) == "religious");
    CHECK(religious && religious->confidence == Confidence::High);

    const C9D4BitInfo* entertainment = find(0x80);
    CHECK(entertainment != nullptr);
    CHECK(entertainment && std::string(entertainment->name) == "entertainment");
    CHECK(entertainment && entertainment->confidence == Confidence::High);

    const C9D4BitInfo* persistent_conn = find(0x10);
    CHECK(persistent_conn && persistent_conn->confidence == Confidence::High);
    const C9D4BitInfo* derived_net = find(0x02);
    CHECK(derived_net && derived_net->confidence == Confidence::High);

    // 0x08 had no documented producer/radius anywhere when Phase 3 wrote
    // this fixture (Unresolved); CAESAR_CONSTRUCTION_RE_v2.md's DS:153A
    // dispatch-table decode (Phase 5) found one (tile 0xF3, radius 6) --
    // a real upgrade backed by new evidence, not a silent one. Still only
    // StrongInference, not High: the *mechanism* is now confirmed but the
    // *building identity* (barracks) is still inferred, not proven.
    const C9D4BitInfo* barracks_bit = find(0x08);
    CHECK(barracks_bit && barracks_bit->confidence == Confidence::StrongInference);

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
    std::printf("test_service_apply_coverage (square radius, clamp to [0, ceiling])\n");
    CityMap city;
    apply_coverage(city, 50, 50, 10, 2, 15);

    CHECK(city.coverage[50][50] == 10);       // origin
    CHECK(city.coverage[50][52] == 10);       // exactly radius 2 away on an axis
    CHECK(city.coverage[52][52] == 10);       // radius 2 away on BOTH axes -- square (Chebyshev), not circular
    CHECK(city.coverage[50][53] == 0);        // just outside radius -- untouched
    CHECK(city.coverage[53][53] == 0);        // corner just outside the square -- untouched

    // Clamping: a second application should cap at `ceiling`, not overflow.
    apply_coverage(city, 50, 50, 10, 0, 15);
    CHECK(city.coverage[50][50] == 15);

    // Off-grid center must not crash and must still touch in-bounds cells.
    apply_coverage(city, 0, 0, 5, 1, 20);
    CHECK(city.coverage[0][0] == 5);
    CHECK(city.coverage[1][1] == 5);
}

void test_service_apply_land_value() {
    std::printf("test_service_apply_land_value (square radius, hard clamp -8..+50)\n");
    CityMap city;
    apply_land_value(city, 30, 30, 20, 1, 50);
    CHECK(city.land_value[30][30] == 20);
    CHECK(city.land_value[31][31] == 20);  // square radius, same as coverage
    CHECK(city.land_value[32][32] == 0);   // outside radius

    // Ceiling is never allowed above +50 regardless of what's passed in.
    apply_land_value(city, 30, 30, 1000, 0, 200);
    CHECK(city.land_value[30][30] == 50);

    // Floor is always -8, not caller-adjustable.
    apply_land_value(city, 60, 60, -1000, 0, 50);
    CHECK(city.land_value[60][60] == -8);
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
    std::printf(
        "test_service_building_handlers (temple/bath houses/hospital/theater/coliseum/hippodrome/"
        "plaza/barracks/prefecture/school/oracle -- CAESAR_CONSTRUCTION_RE_v2.md corrected parameters)\n");
    CityMap city;
    apply_temple(city, 50, 50, TempleVariant::Variant1);  // radius 6, no coverage (unconfirmed for this variant)
    CHECK((city.service_flags[50][56] & 0x20) != 0);      // exactly radius 6
    CHECK((city.service_flags[50][57] & 0x20) == 0);      // just outside
    CHECK(city.coverage[50][50] == 0);                    // variant 1's coverage isn't confirmed -- must NOT apply one

    CityMap city2;
    apply_temple(city2, 20, 20, TempleVariant::Variant4);  // radius 12 flags + radius 5 coverage (confirmed)
    CHECK((city2.service_flags[20][32] & 0x20) != 0);
    CHECK((city2.service_flags[20][33] & 0x20) == 0);
    CHECK(city2.coverage[20][25] == 1);  // exactly radius 5
    CHECK(city2.coverage[20][26] == 0);

    // Bath houses now require the 7BB4.10 "connected" gate -- CORRECTED
    // from Phase 3, which had no gate at all.
    CityMap city3;
    apply_bath_houses(city3, 50, 50);  // not connected -- must do nothing
    CHECK(city3.service_flags[50][50] == 0);
    CHECK(city3.coverage[50][50] == 0);

    CityMap city4;
    city4.operational_state[50][50] = 0x10;
    apply_bath_houses(city4, 50, 50);
    CHECK((city4.service_flags[50][54] & 0x04) != 0);  // radius 4 (was 3 in Phase 3 -- corrected)
    CHECK((city4.service_flags[50][55] & 0x04) == 0);
    CHECK(city4.coverage[50][53] == 1);  // radius 3 (was 2 in Phase 3 -- corrected)
    CHECK(city4.coverage[50][54] == 0);

    CityMap city5;
    apply_theater(city5, 50, 50);
    CHECK((city5.service_flags[50][54] & 0x80) != 0);  // radius 4
    CHECK((city5.service_flags[50][55] & 0x80) == 0);
    CHECK(city5.coverage[50][53] == 1);  // coverage now implemented (was missing in Phase 3)
    CHECK(city5.coverage[50][54] == 0);

    CityMap city6;
    apply_coliseum(city6, 50, 50);
    CHECK((city6.service_flags[50][56] & 0x80) != 0);  // radius 6
    CHECK(city6.coverage[50][54] == 1);                 // radius 4

    CityMap city7;
    apply_hippodrome(city7, 50, 50);
    CHECK((city7.service_flags[50][57] & 0x80) != 0);  // radius 7
    CHECK(city7.coverage[50][55] == 1);                 // radius 5

    CityMap city8;
    apply_hospital(city8, 50, 50);
    CHECK((city8.service_flags[50][54] & 0x40) != 0);  // radius 4
    CHECK(city8.coverage[50][53] == 1);                 // radius 3, now implemented

    // New in Phase 5 -- Plaza/Barracks/Prefecture/School/Oracle had zero
    // documented parameters in Phase 3 and were skipped entirely.
    CityMap city9;
    apply_plaza(city9, 50, 50);
    CHECK(city9.coverage[50][54] == 1);  // radius 4, no flag documented
    CHECK(city9.service_flags[50][50] == 0);

    CityMap city10;
    apply_barracks(city10, 50, 50);
    CHECK((city10.service_flags[50][56] & 0x08) != 0);  // radius 6
    CHECK(city10.coverage[50][51] == 1);                 // radius 1

    CityMap city11;
    apply_prefecture(city11, 50, 50);
    CHECK(city11.coverage[50][53] == 1);  // radius 3, no flag documented
    CHECK(city11.service_flags[50][50] == 0);

    CityMap city12;
    apply_school(city12, 50, 50);
    CHECK((city12.service_flags[50][54] & 0x20) != 0);  // radius 4 -- same mask as religious (0x20)
    CHECK(city12.coverage[50][52] == 1);                 // radius 2
    CHECK(city12.land_value[50][53] == -2);              // radius 3, negative -- an undesirable-neighbor effect

    CityMap city13;
    apply_oracle(city13, 50, 50);  // mechanically identical to apply_school -- see header comment
    CHECK((city13.service_flags[50][54] & 0x20) != 0);
    CHECK(city13.coverage[50][52] == 1);
    CHECK(city13.land_value[50][53] == -2);
}

// dispatch_tile (Phase 5): the actual DS:153A tile-ID dispatcher, routing
// city.tile[y][x] to the handler above matching that exact ID.
void test_service_dispatch_tile() {
    std::printf("test_service_dispatch_tile (DS:153A far-pointer dispatch, CAESAR_CONSTRUCTION_RE_v2.md)\n");

    // A temple-4 tile should produce exactly apply_temple's variant-4 effect.
    CityMap city;
    city.tile[50][50] = 0xE6;
    dispatch_tile(city, 50, 50);
    CHECK((city.service_flags[50][62] & 0x20) != 0);  // radius 12
    CHECK(city.coverage[50][55] == 1);                 // radius 5

    // 0x36-0x3B requires the connectivity gate, same as apply_tile_36_3b directly.
    CityMap city2;
    city2.tile[10][10] = 0x38;
    dispatch_tile(city2, 10, 10);
    CHECK(city2.coverage[10][10] == 0);  // not connected
    city2.operational_state[10][10] = 0x10;
    dispatch_tile(city2, 10, 10);
    CHECK(city2.coverage[10][10] == 1);

    // Housing tiles (0x00-0x15) are explicitly NOT this dispatcher's
    // concern -- systems::housing owns them. Must be a true no-op here.
    CityMap city3;
    city3.tile[5][5] = 0x00;
    city3.land_value[5][5] = 41;  // would trigger land_value_allows if this dispatcher touched it
    dispatch_tile(city3, 5, 5);
    CHECK(city3.tile[5][5] == 0x00);  // untouched

    // 0xE9 (documented default handler, no specifics) and a genuinely
    // unused ID must both be no-ops.
    CityMap city4;
    city4.tile[7][7] = 0xE9;
    dispatch_tile(city4, 7, 7);
    CHECK(city4.service_flags[7][7] == 0);
    CHECK(city4.coverage[7][7] == 0);

    CityMap city5;
    city5.tile[8][8] = 0x99;  // arbitrary ID with no documented handler
    dispatch_tile(city5, 8, 8);
    CHECK(city5.service_flags[8][8] == 0);
    CHECK(city5.coverage[8][8] == 0);
}

// systems::housing (Phase 4, Layer 3). land_value_allows and tick_tile_00
// transcribe CAESAR_CITY_STATE_v5.md routine 0x2DB49/0x297AC directly --
// these tests catch a transcription slip, same spirit as
// test_service_bit_table_fixtures. The population tests check the
// documented SHAPE only (monotonic + top-grade dip), not real numbers --
// see systems/housing.hpp for why no real numbers exist to check against.
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

void test_housing_tick_tile_00() {
    std::printf("test_housing_tick_tile_00 (0x297AC handler, threshold 0x28=40)\n");
    CityMap city;
    city.land_value[7][7] = 41;
    tick_tile_00(city, 7, 7);
    CHECK(city.tile[7][7] == 0xA7);

    CityMap city2;
    city2.land_value[7][7] = 40;  // exactly at threshold -- must not transition
    tick_tile_00(city2, 7, 7);
    CHECK(city2.tile[7][7] == 0);
}

void test_housing_population() {
    std::printf("test_housing_population (manual: monotonic density, slight drop at top grade)\n");
    // Monotonic increase from grade 1 to grade 15.
    for (int g = 1; g < 15; ++g) {
        int lower = provisional_density_per_grade(static_cast<HousingGrade>(g));
        int higher = provisional_density_per_grade(static_cast<HousingGrade>(g + 1));
        CHECK(higher > lower);
    }
    // "the fanciest houses actually have a slight drop in density"
    int grade15 = provisional_density_per_grade(HousingGrade::Grade15);
    int grade16 = provisional_density_per_grade(HousingGrade::Grade16);
    CHECK(grade16 < grade15);
    CHECK(grade16 > 0);  // a drop, not a collapse to zero/negative

    CHECK(population(10, HousingGrade::Grade1) == 10 * provisional_density_per_grade(HousingGrade::Grade1));
    CHECK(population(0, HousingGrade::Grade10) == 0);
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
    std::printf("test_construction_place (terrain gate 0x1D..0x35, footprint writes, 7BB4 clear)\n");
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
    CityMap city;
    for (int y = 40; y < 60; ++y)
        for (int x = 40; x < 60; ++x) city.tile[y][x] = 0x20;

    // Theater seeds 0xF0, which is exactly the tile DS:153A routes to the
    // theater simulation handler -- so placing it makes dispatch_tile do
    // theater work at that cell with no further wiring.
    CHECK(place(city, CommandId::Theater, 50, 50));
    CHECK(city.tile[50][50] == 0xF0);
    dispatch_tile(city, 50, 50);
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
    test_service_building_handlers();
    test_service_dispatch_tile();
    test_housing_land_value_allows();
    test_housing_tick_tile_00();
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
