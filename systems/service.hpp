// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius — systems/service.hpp
//
// Layer 3 (per GAIUS_MASTERPLAN.md section 5's engine module breakdown):
// the A2C4/C9D4/54A4 service-propagation primitives, the per-tick reset,
// building handlers, and (new in Phase 5) the city-tile simulation
// dispatcher itself.
//
// Sources, in order of authority for anything they overlap on:
//   - This project's own direct extraction of the full 256-entry DS:153A
//     table (docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 11,
//     flat file offset 0x75D8A in the decompressed US-build image,
//     verified byte-exact against 22 independently-published tile
//     addresses from CAESAR_CITY_STATE_v5.md) -- authoritative for tile
//     IDs specifically. It CORRECTS CAESAR_CONSTRUCTION_RE_v2.md section
//     5's own table: v2's handler addresses are all exactly right, but
//     every tile ID it attaches them to is off by one (v2 says temple
//     variant 1 is tiles 0xDF/E0; it's actually 0xE0/E1, and so on down
//     the whole list). v2 in turn already corrected an earlier assumption
//     (still present, uncorrected, in CAESAR_REVERSE_ENGINEERING_COMPLETE.md
//     section 72 / CAESAR_CITY_STATE_v6.md) that these handlers lived at
//     0x43-0x50 -- they don't, they're in the 0xE0-0xF6 range. Phase 3 was
//     built against that oldest, wrong assumption; this file was corrected
//     once already for the v2 tile IDs, and now again for the off-by-one.
//   - CAESAR_CITY_STATE_v5.md -- the 0x36-0x40 construction/placement-state
//     family (distinct from the civic buildings) and the
//     apply_coverage/apply_land_value/apply_flags call-order/mechanism.
//   - CAESAR_CITY_STATE_v6.md -- the C9D4 bit table and per-tick reset;
//     still authoritative for what v2 doesn't touch.
//
// Scope discipline unchanged from Phase 3: only handlers with an actual
// documented radius/mask/delta get implemented. v2 newly documents Plaza,
// Barracks, Prefecture, School, Oracle (all skipped in Phase 3 for lack of
// data) and adds a previously-undocumented coverage component to
// Hospital/Theater/Coliseum/Hippodrome. One tile (0xEA, "Career /
// specialized service") has a concrete mechanism but no confident building
// identity even in v2's own text -- implemented under its tile ID, not a
// building name, for exactly that reason. 0xE8 ("generic/default handler
// -- generic object processing") has no specifics beyond that description
// and is left as the dispatcher's default case.

#pragma once

#include <array>
#include <cstdint>

#include "model/city_state.hpp"

namespace gaius::systems::service {

// CAESAR_REVERSE_ENGINEERING_COMPLETE.md's own confidence scale (section
// ~79), reused here rather than inventing a parallel one.
enum class Confidence { Definitive, High, StrongInference, Unresolved };

struct C9D4BitInfo {
    uint8_t mask;
    const char* name;
    Confidence confidence;
    const char* evidence;
};

// CAESAR_CITY_STATE_v6.md's "Current C9D4 table", updated with
// CAESAR_CONSTRUCTION_RE_v2.md's newer findings where v2 adds a radius/
// building attribution v6 didn't have (0x08 in particular: v6 found no
// producer at all; v2's dispatch-table decode found one).
inline const std::array<C9D4BitInfo, 8> kC9D4BitTable = {{
    {0x01, "service_prerequisite", Confidence::StrongInference,
     "CAESAR_CITY_STATE_v6.md: consumed by housing transitions; exact semantic name still open"},
    {0x02, "derived_network_availability", Confidence::High,
     "CAESAR_CITY_STATE_v6.md: generated directly from 7BB4.10 by routine 0x2DA0D"},
    {0x04, "localized_service_coverage", Confidence::StrongInference,
     "CAESAR_CONSTRUCTION_RE_v2.md: tile 0xE8/EA handler (2C159), requires 7BB4.10, "
     "apply_flags(radius=4, mask=0x04) + coverage+1@radius3; building identity (bath houses) still inferred, not proven. "
     "Tile IDs corrected from v2's own (off by one) -- see docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 11."},
    {0x08, "localized_service_coverage_c", Confidence::StrongInference,
     "CAESAR_CONSTRUCTION_RE_v2.md: tile 0xF4 handler (2C475), apply_flags(radius=6, mask=0x08) + "
     "coverage+1@radius1; building identity (barracks) inferred from section-72-style provisional table, not proven. "
     "CAESAR_CITY_STATE_v6.md had found no producer for this bit at all -- v2 supersedes that."},
    {0x10, "persistent_connection_state", Confidence::High,
     "CAESAR_CITY_STATE_v6.md: survives the &=0x12 per-tick reset, drives 0x02 generation"},
    {0x20, "religious", Confidence::High,
     "CAESAR_REVERSE_ENGINEERING_COMPLETE.md HIGH CONFIDENCE example; CAESAR_CONSTRUCTION_RE_v2.md: "
     "four temple variants (tiles 0xE0-0xE7) confirmed via DS:153A dispatch decode, radii 6/8/10/12"},
    {0x40, "localized_service_coverage_b", Confidence::StrongInference,
     "CAESAR_CONSTRUCTION_RE_v2.md: tile 0xEC/ED handler (2C20D), apply_flags(radius=4, mask=0x40) + "
     "coverage+1@radius3; building identity (hospital) inferred, not proven (v6 called it \"anonymous\")"},
    {0x80, "entertainment", Confidence::High,
     "CAESAR_REVERSE_ENGINEERING_COMPLETE.md HIGH CONFIDENCE example; CAESAR_CONSTRUCTION_RE_v2.md: "
     "theater/coliseum/hippodrome (tiles 0xF0/F1/F2) confirmed via DS:153A dispatch decode, radii 4/6/7"},
}};

// Runtime-only per-tick scratch state -- NOT part of model::CityState / the
// save format. "2D94" resets to 0x3F (63) every tick (CAESAR_CITY_STATE_v6.md
// routine 0x2C8D3). CAESAR_CITY_STATE_v5.md additionally describes a
// ratchet-style interaction between this array and apply_coverage's
// `ceiling` argument (the limit can only be *raised*, never lowered, when
// A2C4 already exceeds it) -- but every worked disassembly example found
// so far (CAESAR_CONSTRUCTION_RE_v2.md's temple-variant-4 decode, and
// v5/v6's 0x36-0x40 family) passes a literal constant (31, or 0x40 for
// land value) as the ceiling and clamps directly against it, with no
// observed behavior that depends on the ratchet. So: this array's
// reset-to-63 behavior is still modeled (it's confirmed), but no handler
// below reads from it -- none has been found to. The ratchet mechanic
// itself is not implemented; flagged as an open question rather than
// guessed at.
struct ServiceState {
    model::CityGrid<uint8_t> coverage_ceiling{};
};

// Per-tick reset, exactly matching routine 0x2C8D3
// (CAESAR_CITY_STATE_v6.md): C9D4 &= 0x12 (only bits 0x02/0x10 survive;
// see kC9D4BitTable), A2C4 = 0, coverage_ceiling = 0x3F (63) everywhere.
void reset_tick(model::CityMap& city, ServiceState& service);

// apply_coverage(x, y, delta, radius, ceiling) -- adds `delta` to A2C4 for
// every cell within a SQUARE radius of (x,y) -- Chebyshev distance <=
// radius, per CAESAR_REVERSE_ENGINEERING_COMPLETE.md section 73's "service
// propagation uses square-radius routines" (not a circle) -- clamped to
// [0, ceiling]. `ceiling` is used as a direct clamp bound here, matching
// every worked disassembly example (see ServiceState's comment on why the
// alternative "read from 2D94" interpretation was dropped). Out-of-bounds
// cells are silently skipped (city edges are a normal case, not an error).
void apply_coverage(model::CityMap& city, int x, int y, int delta, int radius, int ceiling);

// apply_land_value(x, y, delta, radius, ceiling) -- same square radius.
// The absolute range -8..+50 is DEFINITIVE (CAESAR_CITY_STATE_v6.md
// routine 0x2DA7E) and is always enforced regardless of `ceiling`;
// `ceiling` additionally caps the result below +50 if the caller supplies
// something lower (CAESAR_CITY_STATE_v5.md's 0x3C-0x3F family does exactly
// this, with ceiling=0x40=64 -- above +50, so it has no additional effect
// there; no call site with a ceiling *below* 50 has turned up yet). The
// floor is always -8, not caller-adjustable.
void apply_land_value(model::CityMap& city, int x, int y, int delta, int radius, int ceiling = 50);

// apply_flags(x, y, radius, mask) -- same square radius. ORs `mask` into
// C9D4. Pure set, no clamping.
void apply_flags(model::CityMap& city, int x, int y, int radius, uint8_t mask);

// The only concrete ceiling value ever observed across every traced
// coverage-applying handler so far (temple variant 4, all of 0x36-0x40,
// and every civic-building tile below) -- used here as a shared constant
// rather than re-deriving "31" at each call site. This is a pattern
// (every +1/+2/+3-delta handler found uses it), not something independently
// confirmed per building -- if a future trace finds a handler using a
// different ceiling, that handler's number wins, not this one.
constexpr int kObservedCoverageCeiling = 31;

// --- Construction/placement-state tile family 0x36-0x40 ---
// (CAESAR_CITY_STATE_v5.md; distinct from the civic buildings below --
// these are written directly by construction/placement code, per v5's
// trace of the placement routine at 0x17F00-0x1834C.)

// Tiles 0x36-0x3B (6 IDs, all dispatch to the same handler): if
// 7BB4[cell] & 0x10, apply_coverage(+1, radius=1, ceiling=31); otherwise
// no effect.
void apply_tile_36_3b(model::CityMap& city, int x, int y);
// Tiles 0x3C-0x3F (4 IDs): apply_coverage(+1, radius=2, ceiling=31) +
// apply_land_value(-2, radius=2, ceiling=0x40) -- service-positive,
// land-value-limiting.
void apply_tile_3c_3f(model::CityMap& city, int x, int y);
// Tile 0x40: apply_coverage(+2 normally, or +3 if 7BB4[cell] & 0x10 is
// set, radius=1, ceiling=31) -- a stronger variant of 0x36-0x3B.
void apply_tile_40(model::CityMap& city, int x, int y);

// --- Confirmed civic-building handlers, tiles 0xE0-0xF6 ---
// (CAESAR_CONSTRUCTION_RE_v2.md section 5's dispatch-table decode, itself
// corrected by this project: v2's own published tile IDs for this range
// are each off by exactly one, discovered by directly locating and
// extracting the full 256-entry DS:153A table from the real executable
// (flat file offset 0x75D8A, confirmed byte-exact against 22 independently
// -published tile-0x00-0x21 addresses from CAESAR_CITY_STATE_v5.md -- see
// docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md section 11). The HANDLER
// ADDRESSES below match v2's exactly; only the TILE IDs they're attached
// to are corrected. SUPERSEDES Phase 3's original tile-ID assumption of
// 0x43-0x50 too, same as v2 already did.)

// Which deity a temple honors isn't identified anywhere in the corpus --
// only that there are four variants with these radii (tiles
// 0xE0/E1=1, 0xE2/E3=2, 0xE4/E5=3, 0xE6/E7=4).
enum class TempleVariant { Variant1 = 1, Variant2, Variant3, Variant4 };

// apply_flags(x, y, radius, 0x20), radius per variant (6/8/10/12).
// CORRECTED from Phase 3: only variant 4's coverage component is actually
// confirmed (v2 section 7's worked decode of handler 0x2C103: coverage+1
// @ radius=5, ceiling=31). Variant 1's table entry marks its coverage
// radius with a "?" (uncertain); variants 2-3 have none documented at all.
// Rather than assume they share variant 4's numbers, only variant 4 gets a
// coverage call here -- an inconsistency in the *code*, honestly, but not
// in the *evidence*.
void apply_temple(model::CityMap& city, int x, int y, TempleVariant variant);

// Tiles 0xE8/EA. CORRECTED from Phase 3 (was radius=3/mask=0x04 +
// coverage radius=2, no gate -- all three numbers were wrong, sourced from
// v6's less precise pass). Now: requires 7BB4[cell] & 0x10 (same gating
// pattern as apply_tile_36_3b) to do anything at all; when set,
// apply_flags(radius=4, mask=0x04) + apply_coverage(+1, radius=3,
// ceiling=31).
void apply_bath_houses(model::CityMap& city, int x, int y);

// Tile 0xEC/ED. CORRECTED: added a coverage component v6 didn't document
// (+1, radius=3, ceiling=31); flags unchanged (radius=4, mask=0x40).
void apply_hospital(model::CityMap& city, int x, int y);

// Tile 0xF0. CORRECTED: added coverage (+1, radius=3, ceiling=31); flags
// unchanged (radius=4, mask=0x80).
void apply_theater(model::CityMap& city, int x, int y);
// Tile 0xF1. CORRECTED: added coverage (+1, radius=4, ceiling=31); flags
// unchanged (radius=6, mask=0x80).
void apply_coliseum(model::CityMap& city, int x, int y);
// Tile 0xF2. CORRECTED: added coverage (+1, radius=5, ceiling=31); flags
// unchanged (radius=7, mask=0x80).
void apply_hippodrome(model::CityMap& city, int x, int y);

// Tile 0xF3. NEW in Phase 5 (no data existed for this in Phase 3):
// apply_coverage(+1, radius=4, ceiling=31). No C9D4 flag documented.
void apply_plaza(model::CityMap& city, int x, int y);

// Tile 0xF4. NEW: apply_flags(radius=6, mask=0x08) +
// apply_coverage(+1, radius=1, ceiling=31).
void apply_barracks(model::CityMap& city, int x, int y);

// Tiles 0xF5/F6. NEW: apply_coverage(+1, radius=3, ceiling=31). No C9D4
// flag documented.
void apply_prefecture(model::CityMap& city, int x, int y);

// Tile 0xEE (School) and 0xEF (Oracle). NEW. Both dispatch to
// mechanically-IDENTICAL parameters per v2's table (apply_coverage(+1,
// radius=2, ceiling=31) + apply_flags(radius=4, mask=0x20) +
// apply_land_value(-2, radius=3)) -- v1 itself says distinguishing them
// "requires caller/placement correlation" that hasn't been done. Exposed
// as two identically-implemented functions (not one shared name) so a
// future correction to just one of them doesn't require restructuring
// callers -- but don't read the duplication as two independently-confirmed
// mechanisms; it's one mechanism attributed to two possible buildings.
void apply_school(model::CityMap& city, int x, int y);
void apply_oracle(model::CityMap& city, int x, int y);

// Tile 0xEB. v2 itself hedges the identity ("Career / specialized
// service") -- named after its tile ID, not a building, for exactly that
// reason. Mechanism: apply_coverage(+2, radius=8, ceiling=31) +
// apply_land_value(-2, radius=5).
void apply_tile_ea(model::CityMap& city, int x, int y);

// Dispatches city.tile[y][x] to the handler above matching that exact
// tile ID, per the definitive DS:153A far-pointer table structure
// (CAESAR_CONSTRUCTION_RE_v2.md section 3: "handler = table[city_tile];
// call_far(handler)"; this project's own full 256-entry extraction is the
// authority for which tile ID maps to which handler -- see this file's
// top comment). Deliberately does NOT handle tiles 0x00-0x15 (that's
// systems::housing's concern, kept separate rather than duplicating
// dispatch logic across two systems) or tile 0xE9 (documented only as
// "generic/default handler -- generic object processing", with no
// specifics to implement). Any tile ID not listed above (including 0xE9)
// is a no-op here, matching CAESAR_CITY_STATE_v5.md's own dispatch table
// listing most IDs outside the known families as no-ops/returns.
void dispatch_tile(model::CityMap& city, int x, int y);

}  // namespace gaius::systems::service
