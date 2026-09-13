// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/service.hpp
//
// Layer 3 (GAIUS_MASTERPLAN.md section 5): the A2C4/C9D4/54A4 propagation
// primitives, the per-tick reset, and the city-tile simulation dispatcher
// with every handler it routes to.
//
// SOURCE OF AUTHORITY: direct disassembly of the decompressed US-build
// CSR.EXE (2026-09-13). Every routine and every handler parameter below is
// transcribed from the machine code -- see
// docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 15. That supersedes
// the parameter tables in CAESAR_CONSTRUCTION_RE_v2.md and
// CAESAR_CITY_STATE_v5/v6.md, which this file used to follow and which were
// wrong in ways only reading the code could reveal:
//
//   - Five handlers had wrong parameters as well as wrong building names.
//     v2 called tile 0xEE "School" and 0xEF "Oracle" and gave them one shared
//     mechanism; they are Prefecture and Barracks (Phase 5's construction
//     seeds, confirmed by nine 1x1 and one 3x3 footprint in a real save) and
//     share nothing.
//   - Coverage ceilings are NOT a universal 31: heavy industry 2, the
//     0xF5/F6 building 3, barracks 5, prefecture 8, market 16. Because the
//     ceiling is a per-cell running minimum (see apply_coverage), those low
//     values cap coverage from every other source in range -- which is how
//     industry and the military suppress their surroundings.
//   - Tiles 0x3C-0x3F use radius 1 and have no land-value effect.
//   - Temple variants 1-3 do apply coverage (radius 2/3/4).
//   - Bath houses use flags radius 3 and coverage radius 2 -- what Phase 3
//     originally had, before a "correction" from v2 swapped them.
//   - The per-tick land-value propagator has no -8 floor. The -8..+50 range
//     is real but belongs to a separate per-cell routine (evolve_land_value).
//
// Engine behaviour to know before calling anything here:
//
//   - The main tick scans the 100x100 grid a quarter at a time (25 rows per
//     tick phase, routine 0x2BBBB) and dispatches a cell ONLY if its tile id
//     is greater than 0x35. Tiles 0x00-0x35 are never simulated. That scan is
//     the only code in the executable that reads DS:153A.
//   - Most handlers first bump a per-scan counter (which the tick later
//     publishes as a city statistic) and, when it equals one of two runtime
//     thresholds, call an event routine INSTEAD of their normal effect for
//     that one cell. The thresholds are runtime-only globals and the event
//     routines aren't decoded, so that branch is not modeled: every handler
//     here always performs its normal effect.

#pragma once

#include <array>
#include <cstdint>

#include "model/city_state.hpp"

namespace gaius::systems::service {

// CAESAR_REVERSE_ENGINEERING_COMPLETE.md's own confidence scale (section
// ~79), reused rather than inventing a parallel one.
enum class Confidence { Definitive, High, StrongInference, Unresolved };

struct C9D4BitInfo {
    uint8_t mask;
    const char* name;
    Confidence confidence;
    const char* evidence;
};

// The C9D4 bitfield. Names are the RE corpus's; producers and radii are from
// the disassembly. A name is only as strong as its confidence -- several are
// about who SETS a bit, not what reads it.
inline const std::array<C9D4BitInfo, 8> kC9D4BitTable = {{
    {0x01, "service_prerequisite", Confidence::StrongInference,
     "CAESAR_CITY_STATE_v6.md: consumed by housing transitions; exact semantic name still open"},
    {0x02, "derived_network_availability", Confidence::Definitive,
     "Routine 0x2DA0D (disassembled), run at tick start: for every cell, if C9D4.10 is set it sets 0x02 and "
     "clears 0x10, otherwise it clears 0x02. The source is C9D4.10 -- an earlier evidence line said 7BB4.10, "
     "which is wrong. See derive_network_flags."},
    {0x04, "localized_service_coverage", Confidence::High,
     "Set only by the Bath Houses handler (tiles 0xE8/EA, 0x2C159) at radius 3, and only when 7BB4.10 is set. "
     "0xE8 is Bath Houses' construction seed and real saves show it as 1x1 buildings. What reads the bit is not traced."},
    {0x08, "localized_service_coverage_c", Confidence::High,
     "Set by the Market handler (tile 0xF4, 0x2C475) at radius 6. 0xF4 is Market's construction seed, real saves "
     "show 2x2 buildings, and the engine counts 0xF4 cells /4. Previously attributed to barracks, which was wrong."},
    {0x10, "persistent_connection_state", Confidence::High,
     "Survives the &=0x12 reset (0x2C8D3), but routine 0x2DA0D then converts it into 0x02 and clears it -- so it "
     "only persists from when it is set until the next tick start."},
    {0x20, "religious", Confidence::StrongInference,
     "Temples (tiles 0xE0-0xE7) set it at radius 6/8/10/12, but the Prefecture handler (tile 0xEE, 0x2C267) also "
     "sets it at radius 4. So it is not exclusively temple-produced, and the corpus's 'religious' label is "
     "unverified against any consumer. Downgraded from High on that evidence."},
    {0x40, "localized_service_coverage_b", Confidence::High,
     "Set at radius 4 by the handler shared by tiles 0xEC (School) and 0xED (Hospital), 0x2C20D. Both are "
     "construction seeds, and the engine also counts 0xEC-0xED cells together (/4)."},
    {0x80, "entertainment", Confidence::High,
     "Theater/Coliseum/Hippodrome (tiles 0xF0/F1/F2) set it at radius 4/6/7, confirmed by disassembly."},
}};

// Runtime-only per-tick state -- not part of model::CityState or the save.
struct ServiceState {
    ServiceState();

    // "2D94": the per-cell coverage ceiling. reset_tick sets every cell to
    // 0x3F; apply_coverage lowers a cell to the smallest ceiling applied to it
    // this tick and clamps A2C4 against it. Constructed at 0x3F, i.e. the
    // state immediately after a reset.
    model::CityGrid<uint8_t> coverage_ceiling{};

    // DS:0x6BF8 -- the base delta the housing tiers add to coverage (see
    // apply_housing_tier). It is a saved global (global_words_128 index 105,
    // save+0xD2) and holds 2 in all four real saves captured so far; what sets
    // it is not traced. The default is that observed value, not a derived one.
    int housing_coverage_base = 2;
};

// Routine 0x2C8D3 (disassembled). For every cell: C9D4 &= 0x12, A2C4 = 0,
// coverage_ceiling = 0x3F. Land value (54A4) is NOT touched -- it is never
// reset per tick.
void reset_tick(model::CityMap& city, ServiceState& service);

// Routine 0x2DA0D (disassembled), called straight after reset_tick. For every
// cell: if C9D4.10 is set, set C9D4.02 and clear C9D4.10; otherwise clear
// C9D4.02. The engine skips the whole pass while the runtime global DS:0x6D9B
// is nonzero; that global's meaning isn't traced, so the caller decides.
void derive_network_flags(model::CityMap& city);

// Routine 0x2C577 (disassembled). For every cell within a square (Chebyshev)
// radius of (x,y), clipped to the grid:
//   1. coverage_ceiling = min(coverage_ceiling, ceiling)   (signed 8-bit)
//   2. A2C4 += delta                                        (8-bit, wraps)
//   3. if A2C4 > coverage_ceiling, A2C4 = coverage_ceiling  (signed 8-bit)
// No lower clamp. Step 1 is the point: the ceiling is a running minimum over
// every source that touched the cell this tick, so a low-ceiling source caps
// coverage for all others in range, whichever is applied first.
void apply_coverage(model::CityMap& city, ServiceState& service, int x, int y, int delta, int radius, int ceiling);

// Routine 0x2C6AF (disassembled) -- the square-radius land-value propagator
// the handlers call. For each cell in range: 54A4 += delta (8-bit, wraps);
// if 54A4 > ceiling (signed), 54A4 = ceiling. NO floor: repeated negative
// deltas accumulate without limit (a real save holds -43). The -8..+50 range
// belongs to evolve_land_value, not here.
void apply_land_value(model::CityMap& city, int x, int y, int delta, int radius, int ceiling);

// Routine 0x2DA7E (disassembled) -- a per-cell land-value step, distinct from
// the propagator. If the cell has C9D4.20 set, 54A4 += growth; otherwise
// 54A4 -= 2. Then clamp to -8..+50. This is where the documented -8..+50 range
// actually lives. Its caller is not yet identified -- no DS:153A handler calls
// it -- so nothing in this file invokes it.
void evolve_land_value(model::CityMap& city, int x, int y, int growth);

// Routine 0x2C7BB (disassembled). ORs `mask` into C9D4 over a square radius.
// The engine also has a one-shot mode: if the runtime global DS:0x6CFC is
// nonzero, the FIRST cell visited is ANDed with the mask instead and the global
// is cleared -- after one cell, not after the call. Nothing in the dispatcher
// sets it, so that mode isn't modeled.
void apply_flags(model::CityMap& city, int x, int y, int radius, uint8_t mask);

// The ceiling most civic handlers pass. Not universal -- see the handlers.
constexpr int kCommonCoverageCeiling = 31;

// ---- Tile handlers. Coverage is (delta, radius, ceiling); flags (radius,
// mask); land value (delta, radius, ceiling). All from the disassembly. ----

// Tiles 0x36-0x3B, handler 0x2BC26 (road/wall family): if 7BB4.10
// ("connected") is set, coverage (+1, r1, c31). Otherwise nothing.
void apply_tile_36_3b(model::CityMap& city, ServiceState& service, int x, int y);

// Tiles 0x3C-0x3F, handler 0x2BC6A: coverage (+2 if 7BB4.10 is set, else +1,
// r1, c31). No land-value effect -- v5's -2 land value and radius 2 are not in
// the code.
void apply_tile_3c_3f(model::CityMap& city, ServiceState& service, int x, int y);

// Tile 0x40, handler 0x2BCB9: coverage (+3 if 7BB4.10 is set, else +2, r1, c31).
void apply_tile_40(model::CityMap& city, ServiceState& service, int x, int y);

// Which deity each variant honours isn't identified. Two tile ids per
// variant: 0xE0/E1, 0xE2/E3, 0xE4/E5, 0xE6/E7.
enum class TempleVariant { Variant1 = 1, Variant2, Variant3, Variant4 };

// Temples, handlers 0x2C001 / 0x2C057 / 0x2C0AD / 0x2C103: coverage (+1,
// radius 2/3/4/5, c31), then flags (radius 6/8/10/12, 0x20).
void apply_temple(model::CityMap& city, ServiceState& service, int x, int y, TempleVariant variant);

// Bath Houses, tiles 0xE8/EA, handler 0x2C159. Only if 7BB4.10 is set:
// coverage (+1, r2, c31), then flags (r3, 0x04).
void apply_bath_houses(model::CityMap& city, ServiceState& service, int x, int y);

// Oracle, tile 0xEB, handler 0x2C1B4: coverage (+2, r8, c31), then land value
// (-2, r5, c32). Identity from Phase 5's construction seed and a 2x1 footprint
// in a real save. (Was apply_tile_ea, named before the off-by-one fix.)
void apply_oracle(model::CityMap& city, ServiceState& service, int x, int y);

// School (0xEC) and Hospital (0xED) share handler 0x2C20D: coverage (+1, r3,
// c31), then flags (r4, 0x40). The engine counts the two together as well.
void apply_school_or_hospital(model::CityMap& city, ServiceState& service, int x, int y);

// Prefecture, tile 0xEE, handler 0x2C267: coverage (+1, r2, c8), flags
// (r4, 0x20), land value (-2, r3, c48).
void apply_prefecture(model::CityMap& city, ServiceState& service, int x, int y);

// Barracks, tile 0xEF, handler 0x2C2D7: coverage (+1, r3, c5), then land value
// (-3, r5, c32). No flags.
void apply_barracks(model::CityMap& city, ServiceState& service, int x, int y);

// Theater 0xF0 (0x2C330): coverage (+1, r3, c31), flags (r4, 0x80).
void apply_theater(model::CityMap& city, ServiceState& service, int x, int y);
// Coliseum 0xF1 (0x2C386): coverage (+1, r4, c31), flags (r6, 0x80).
void apply_coliseum(model::CityMap& city, ServiceState& service, int x, int y);
// Hippodrome 0xF2 (0x2C3DC): coverage (+1, r5, c31), flags (r7, 0x80).
void apply_hippodrome(model::CityMap& city, ServiceState& service, int x, int y);

// Heavy Industry, tile 0xF3, handler 0x2C432: coverage (+1, r4, c2).
void apply_heavy_industry(model::CityMap& city, ServiceState& service, int x, int y);

// Market, tile 0xF4, handler 0x2C475: coverage (+1, r1, c16), then flags
// (r6, 0x08).
void apply_market(model::CityMap& city, ServiceState& service, int x, int y);

// Tiles 0xF5/F6, handler 0x2C4AB: coverage (+1, r3, c3). Named by tile id
// because the building is unidentified: no construction seed maps to it, and
// in a real save it appears as 3x3 blocks. Workshop (3x3) is a candidate only.
void apply_tile_f5_f6(model::CityMap& city, ServiceState& service, int x, int y);

// Tiles 0xC8-0xD7, six handlers (0x2BDA2..0x2BF06) sharing one template:
// coverage (base + adj, radius, ceiling), base = service.housing_coverage_base.
//   0xC8-C9: base-1, r1, c4     0xCA-CC: base, r1, c8     0xCD-D0: base+1, r1, c31
//   0xD1-D4: base+1, r2, c31    0xD5-D6: base+2, r1, c31  0xD7:    base+2, r2, c31
// DEFINITIVE as code. That these sixteen ids are the manual's sixteen housing
// grades is STRONG INFERENCE: 0xC8 is Housing's construction seed, the
// parameters rise in tiers with the id, the range ends where the temple family
// begins (0xD8), and in real saves these tiles line the roads. Reads the tile
// id itself; a no-op for any other tile.
void apply_housing_tier(model::CityMap& city, ServiceState& service, int x, int y);

// ---- Handlers for tiles no construction command seeds directly. Identities
// go only as far as the evidence does. ----

// Tiles 0x94/95, handler 0x2BD08: coverage (+1, r2, c8), then land value
// (-2, r2, c64). Unidentified. (CAESAR_CITY_STATE_v5.md attributed exactly these
// parameters to tiles 0x3C-0x3F, which run different code.)
void apply_tile_94_95(model::CityMap& city, ServiceState& service, int x, int y);

// Tiles 0xA2/A3 and 0xA7-0xB2, handler 0x2BD3D: coverage (-2, r3, c8), then
// coverage (-2, r1, c8) -- NEGATIVE coverage. With no lower clamp the byte
// wraps, which is why real saves hold A2C4 values of 254/255 near these tiles.
// Unidentified. 0xA7 is also the tile land_value_allows writes when land value
// exceeds its threshold (systems::housing).
void apply_tile_a2_b2(model::CityMap& city, ServiceState& service, int x, int y);

// Tiles 0xB9/BB/BC, handler 0x2BD72: coverage (+1, r2, c31), only if C9D4.01 is
// set. reset_tick clears that bit and no handler in this file sets it, so within
// one tick this fires only if some untraced producer set 0x01 first.
// Unidentified; the ids sit between the Well (0xB8) and Fountain (0xBA) seeds.
void apply_tile_b9_bb_bc(model::CityMap& city, ServiceState& service, int x, int y);

// Tiles 0xD8-0xDF, two handlers: 0xD8-DB (0x2BF4F) coverage (+1, r2, c31) and
// land value (-2, r2, c53); 0xDC-DF (0x2BFA8) coverage (+1, r3, c31) and land
// value (-2, r3, c37). 0xD8 is Temple's construction seed, the range sits
// directly below the finished temples (0xE0+), and Phase 5 traced growth stages
// through 0xDD-0xDF -- so "temple under construction" is STRONG INFERENCE.
// Reads the tile id itself; a no-op for any other tile.
void apply_temple_stage(model::CityMap& city, ServiceState& service, int x, int y);

// One cell of the DS:153A dispatch. Tiles <= 0x35 are a no-op, exactly as at
// the engine's only dispatch site (routine 0x2BBBB). Every DS:153A handler that
// calls one of the three propagation routines (0x2C577 / 0x2C6AF / 0x2C7BB) is
// dispatched here; the remaining ids' handlers call none of them, so they are
// no-ops for A2C4, C9D4 and 54A4. (They may still do other work -- tile or
// actor changes, say -- that isn't modeled.) Verified against real saves by
// tools/sim_check.
void dispatch_tile(model::CityMap& city, ServiceState& service, int x, int y);

}  // namespace gaius::systems::service
