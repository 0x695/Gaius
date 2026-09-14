# Caesar (1992) — Construction dispatch findings (Gaius session addendum)

Written in the same confidence-labeled style as the main RE corpus, from disassembly performed directly against the US-build `CSR.EXE` decompressed image (`formats::exepack::decode`, via a scratch tool — not committed) using `capstone` in 16-bit real mode. Builds on `CAESAR_CONSTRUCTION_RE_v1.md`/`v2.md`, which had not yet closed the construction-command-to-tile-write link at the time of writing; this addendum makes partial further progress and documents exactly where it stalls, so a future session doesn't have to retrace the same dead end.

**Not yet merged into `CAESAR_REVERSE_ENGINEERING_COMPLETE.md`** — same status as the other addenda in this directory.

---

## 1. Construction command string table — DEFINITIVE, exact byte layout

`CAESAR_CONSTRUCTION_RE_v1.md` established that 34 construction/action labels exist "in order" but didn't give exact offsets or confirm numeric IDs. Located and fully decoded:

- **Table base**: flat file offset `0x7659D` in the decompressed US-build image (`dest_len*16` = 497,568 bytes, per `docs/CAESAR_EXEPACK_AND_STRINGS_FINDINGS.md`).
- **Encoding**: plain ASCII, space-padded, **no null terminator anywhere in the table** (confirmed — zero `0x00` bytes across the whole table region). Whatever code reads these must know each entry's extent some other way than scanning for a terminator.
- **Stride is NOT a uniform 16 bytes** — this was this addendum's own first hypothesis (based on the first 14 entries, which are all exactly 16 bytes apart) and it's wrong: verifying it with a golden test against the real executable bytes (`tests/test_formats.cpp`'s `test_construction_command_table_corpus`) caught 8 of the 34 entries sitting 17 bytes apart instead. With no in-band delimiter, and padding on both sides of the visible text at some entries, the exact field-width rule (if there even is a single one — it may just be hand-typed literals of whatever length the original author chose, not a computed layout) isn't determined by this pass. What *is* solid: the exact flat offset each string's visible text starts at (found by direct search, not formula), and the order.
- **Index = command's ordinal position** in that order — well-evidenced (matches `CAESAR_CONSTRUCTION_RE_v1.md`'s listed order exactly) but **not independently proven against code that indexes this table by a numeric variable** — see section 3 for why that link wasn't closed this pass.

Full table (corrects two things from `v1`'s transcription: entry 0 was not documented at all, and `v1`'s "Reservoir/Pipe" is actually `Resevoir\pipe` in the real data — note the single backslash delimiter and the missing second "r" in "Reservoir", both literal, i.e. the original 1992 text has a typo). Offsets below are where each string's own visible text starts, verified by direct search against the real decompressed image — not a computed field boundary:

| ID | Text offset | String (exact) |
|---:|---|---|
| 0 | 0x7659D | `No action` |
| 1 | 0x765AD | `Main Toolbar` |
| 2 | 0x765BD | `Clear Area` |
| 3 | 0x765CD | `Go to Forum` |
| 4 | 0x765DF | `Road` |
| 5 | 0x765ED | `Resevoir\pipe` |
| 6 | 0x765FE | `Maps` |
| 7 | 0x7660E | `Wall` |
| 8 | 0x7661E | `Tower` |
| 9 | 0x7662E | `Well` |
| 10 | 0x7663D | `Fountain` |
| 11 | 0x7664D | `Housing` |
| 12 | 0x7665E | `Forum` |
| 13 | 0x7666E | `Temple` |
| 14 | 0x7667C | `Bath Houses` |
| 15 | 0x7668D | `Hospital` |
| 16 | 0x7669E | `School` |
| 17 | 0x766AD | `Oracle` |
| 18 | 0x766BD | `Career` |
| 19 | 0x766CC | `Go to Province` |
| 20 | 0x766DD | `Theater` |
| 21 | 0x766ED | `Coliseum` |
| 22 | 0x766FC | `Hippodrome` |
| 23 | 0x7670E | `Plaza` |
| 24 | 0x7671D | `Barracks` |
| 25 | 0x7672C | `Prefecture` |
| 26 | 0x7673C | `Heavy Industry` |
| 27 | 0x7674D | `Market` |
| 28 | 0x7675D | `Workshop` |
| 29 | 0x7676E | `Fort` |
| 30 | 0x7677E | `Halt` |
| 31 | 0x7678C | `Cohort Patrol` |
| 32 | 0x7679C | `Cohort Attack` |
| 33 | 0x767AC | `Cohort Go Home` |

## 2. `DS:153A` tile-simulation dispatcher — re-confirmed with exact code

`CAESAR_CONSTRUCTION_RE_v2.md` section 3 already established this; disassembling the actual dispatch loop (flat `0x2BB80`-`0x2BC26`) confirms it exactly, and adds one correction:

```asm
02bbe5  cmp byte ptr es:[bx], 0x35     ; tile id vs 0x35
02bbeb  jbe 0x2bc08                    ; <= 0x35 -> SKIP dispatch entirely
...
02bbf7  mov al, byte ptr es:[bx]       ; al = tile id
02bbfe  shl ax, 1
02bc00  shl ax, 1                      ; ax = tile_id * 4
02bc04  lcall [bx + 0x153a]            ; far call through the table
```

**Correction to `CAESAR_CITY_STATE_v5.md`**: that document says tiles "`<= 0x35` dispatch[] through the far-pointer table" — the actual `jbe` branch does the opposite: tiles `<= 0x35` **skip** the dispatch (the `jbe` jumps *past* the call). Only tiles `> 0x35` are ever routed through `DS:153A`. This is consistent with (and further confirms) `Gaius`'s own architecture split: `systems::housing` handles `0x00`-`0x15` through a completely separate mechanism, not through this table at all — the disassembly shows why: this specific dispatcher physically cannot reach those tile IDs.

## 3. The `0x36`-`0x40` placement/auto-tiling family — mechanism traced in detail; construction-vs-tick origin UNRESOLVED

`CAESAR_CITY_STATE_v5.md`/`v2` already identified that placement code around `0x17F00`-`0x1834C` writes tiles `36,38,39,3C,3E,3F,40`. Traced the actual branching:

- A mode selector at flat `0x57E2` (word) takes values `0`-`4`, each corresponding to one of five code blocks (`0x18008`, `0x18053`, `0x18140`, `0x1822D`, and one more past `0x18268`) that each check specific **existing neighbor tile values** at `es:[bx-0x64]` (i.e., the cell **directly above** the target, since the grid is 100-wide and `-0x64` = `-100`) against small constant sets (`{0x42,0x94,0x95,0x86,0x5E}` for case 1; `{0x3B,0x3F}` for case 2; `{0x3A,0x3E}` for case 3; `{0x3D,0x40,0x41}` for case 4) before choosing which of the `0x36`-`0x40` family to write. This is straightforwardly road-style auto-tiling (matching the manual: "the road should bend around the curves, and connect with itself"; "three and four-way intersections... will be built automatically").
- The exact write sites match `v2`'s table exactly: `0x1804A→36`, `0x180FD→3F`, `0x18137→38`, `0x181EA→3E`, `0x18224→39`, `0x1830D→40`, `0x18346→3C` (byte-for-byte confirmed, not just re-asserted).
- **`0x57E2` is fed by a previously-undocumented per-cell property table** at segment `0x3496` (the same runtime city-state segment as `C9D4`/`7BB4`/etc.), stride 16 bytes, base offset around `0xA9A`/`0x74A` depending on which of several near-identical sub-tables — iterated by two nested indices (`[0x6DCB]` outer, up to `0xA1`; `[0x6DC9]` inner, up to `8`). This resembles the masterplan's mention of a "128-entry static property table" for empire-map pathfinding, but the indexing bounds (`0xA1`/`8`) don't cleanly match 128 — **not identified**, flagged as an open item rather than guessed at.
- **The function containing all five cases (`0x17EC5`) is called from exactly one place** (`0x133A0`), itself reached via a 4-function chain (`push cs; call 0x17EC5 / 0x1834D / 0x1879C / 0x18C27`) that runs unconditionally, back-to-back — i.e. **not obviously gated on which construction command is active**. The one call site at `0x133A0` sits immediately after code that writes an arbitrary tile value from a variable at flat `0x6DDB` into the grid (`mov byte ptr es:[bx], dl` where `dl` was loaded from `[0x6DDB]`) and clears `7BB4` at that cell — a shape consistent with "generic tile placement, then refresh road connectivity nearby" — but **`[0x6DDB]` was a dead end to trace further**: it has 696 references across the whole binary, overwhelmingly arithmetic/scratch usage unrelated to tile placement, meaning it's a reused general-purpose global rather than a dedicated "selected construction tile" register. Isolating which of those 696 sites (if any) is reached specifically when a player selects a construction command would need call-graph/control-flow analysis this pass didn't do (each site's *caller chain* would need tracing individually — no shortcut via a single variable search).

## 4. What this does and doesn't close, against `GAIUS_ROADMAP.md` Phase 5's checklist

> **SUPERSEDED by sections 12-13 (sixth pass).** All three caveats below were resolved: item 2's IDs are now DEFINITIVE (confirmed against ~30 toolbar handlers that assign them as literals), and items 1 and 3 are closed for 17 of 23 placing commands. Kept as written because it's the honest record of what was known at the end of pass 2 — see section 14 for the current state.

- Checklist item 2 ("map every construction command string to its numeric ID"): **the table itself is now DEFINITIVE** (section 1). The *ID assignment* (ordinal position = numeric ID used by game code) is well-evidenced but not proven against an actual indexing instruction — no code was found that reads this table by a numeric variable the way `DS:153A` is read by `tile_id`. Treat IDs 0-33 above as HIGH CONFIDENCE, not DEFINITIVE, pending that.
- Checklist item 1 ("recover the construction/action far-pointer table") and item 3 ("trace each command through placement/validation to its tile write(s) and footprint"): **still open**, but with real new footprint detail — see section 7.
- Footprints: **now known for two building families** (section 7) — a first, real answer to this checklist item, though not yet general.

## 5. Two more angles tried this pass — both dead ends, ruled out with reasonable confidence

Before writing this off as "still open," two more concrete attempts were made, both worth recording so a future pass doesn't retry them blind:

**A pointer table indexing the string table (section 1) does not exist in any conventional form.** Searched exhaustively for: (a) 34 consecutive 16-bit near offsets matching the found text positions — not found for any candidate segment base; (b) a far-pointer (offset:segment) table for the first two entries specifically, across every segment value that could plausibly decompose the known flat offsets — not found; (c) a fully generic structural search across the *entire* 497,568-byte image for any two consecutive 4-byte far pointers with a matching segment and a 16-byte offset delta (the confirmed gap between entries 0 and 1) — **zero matches anywhere in the file**. This is fairly strong negative evidence that the game does not look these strings up via a stored pointer array at all — more likely each of the 34 commands has its own hardcoded code path referencing its own string directly (typical of `switch`-per-case or a long `if`/`else if` chain in the original source), which would mean there's no single table-driven mechanism to find, just up to 34 separate call sites.

**Mouse input handling was located but is a dead end at the depth this pass reached.** All three `int 0x33` (mouse driver) call sites in the whole binary are clustered in one small wrapper library at flat `0x301FD`-`0x3029A` (Microsoft mouse driver functions 0/3/4: detect driver, get button+position, set position). Function 3 ("get button state + position" — the one that would matter for click handling) has **exactly one caller in the entire binary**, and that caller is the wrapper library's own internal self-test during driver detection, not game logic. So the actual per-frame mouse polling the game uses for gameplay must go through a different mechanism entirely (a different `int 0x33` function not called from this wrapper, a BIOS/hardware-level poll, or an installed real-time event-handler callback via `int 0x33` function `0x0C`, which wasn't searched for this pass) — this library isn't the path into construction-command selection.

**Net effect: both of this section's originally-suggested angles are now ruled out, not just untried.** `int 0x33` function `0x0C` (install event handler) doesn't need separate searching either — the earlier whole-binary `int 0x33` sweep (this section, above) already found *every* `int 0x33` call site in the file (3 total, all already examined), so there is no missed function-`0x0C` call anywhere. Whatever mechanism the game actually uses for real-time mouse polling during gameplay is not `int 0x33` at all — a materially different question (raw hardware I/O, BIOS, or a custom interrupt vector) that this pass didn't chase further.

## 6. `bindiff_exe` against the international build — tried, too noisy to isolate the dispatcher

Ran `bindiff_exe` between the US build (`Caesar/US/CSR.EXE`) and the international build (`Caesar/CSR.EXE`, root — previously unanalyzed per `CAESAR_GOG_BUILD_FINDINGS.md`). Result: **45.9% of the common 497,568-byte region differs at the byte level** (228,392 differing bytes across 7,725 contiguous runs), confirming this isn't a lightly-localized variant of the same compiled output — it's a materially different compilation throughout (matching `CAESAR_GOG_BUILD_FINDINGS.md`'s own observation that the international build is smaller despite carrying three languages' worth of text, "worth confirming... whether this is a leaner asset set, different compression, or an actual code-level revision"; this run is further evidence toward "code-level revision," not just localization). That volume of undifferentiated diff noise makes byte-level bindiff impractical for isolating one specific subsystem (the construction dispatcher) without much more targeted filtering than a straight run-list — not attempted further this pass.

**Useful side effect**: the string-set diff surfaced a substantial amount of previously-undocumented text embedded in the US build specifically (developer credits — several individual names appear to be Impressions Games staff; the full 21-entry promotion-rank ladder confirmed complete; the full ~50-name province list confirmed complete; full cohort-status-word list; UI strings for game speed/sound/display options, forum-type selection, funds warnings, and more) and confirms the international build carries German and French UI strings alongside English throughout (not just in the areas already sampled by `CAESAR_GOG_BUILD_FINDINGS.md`). None of it is construction-dispatcher-specific, but it's real, new, previously-unextracted game text worth folding into the main RE corpus at some point — not done here since it's off this addendum's specific topic.

## 7. Building growth-stage footprints — DEFINITIVE, genuinely new (fourth session pass)

Searching directly for tile-grid writes of the confirmed civic-building values (`0xDF`-`0xF5`, section 2's `DS:153A` table) turned up real placement code, not just the simulation-side dispatch:

**Temple (tile family `0xDD`/`0xDE`/`0xDF`) has (at least) three growth stages**, each with a confirmed footprint, gated by thresholds against `A2C4` at the temple's own cell:

- `A2C4 < 0x15` (21): writes `0xDD` across a **2×2 footprint** (`[cell]`, `[cell+1]`, `[cell+100]`, `[cell+101]`).
- `A2C4 in [0x15,0x17]` (21-23): rejected (no placement/growth this tick).
- `A2C4 < 0x18` (24) in a separate check (a different function, same shape): writes `0xDE` across a mixed 5-cell footprint (`[cell]`, `[cell+1]`, `[cell+100]`, `[cell+101]`, plus `[cell+2]` written as tile `0x1D` — a plain terrain/no-op ID per the `DS:153A` table's own `16-1D → no-op` range, i.e. decorative filler, not part of the temple structure itself).
- Higher `A2C4`, subject to further validation against the *existing* tile at the footprint's far corner (must be in specific ranges — `0xD8`/`0xD9`, or `0xDA`-`0xDC` with a flag bit set — modeling "can only grow into compatible existing states"): writes `0xDF` across a **3×2 footprint** (`[cell]`, `[cell+1]`, `[cell+2]`, `[cell+100]`, `[cell+101]`, `[cell+102]`).

This matches the manual's own description exactly: *"Temples act like houses — under the right conditions, they will evolve from small huts to grand marble shrines... As a temple evolves, it will sometimes grow outwards as well as upwards."* The growing footprint (2×2 → 5-cell → 3×2) is that "growing outwards" literally, for the first time confirmed against real code rather than just the manual's prose.

**A second building (tile family `0xE8`→`0xEA`, likely the "Career"-labeled tile per `CAESAR_CONSTRUCTION_RE_v2.md`'s table, though that identity is itself hedged there) follows the exact same mechanism** — `A2C4 < 0xC` (12) writes `0xE8` across a **1×2 footprint**; a higher threshold (confirmed to exist, exact value not pinned this pass) writes `0xEA` across a **2×2 footprint**. Finding the *same* threshold-gated growth-stage shape on a second, unrelated building is good evidence this is a general Caesar mechanism, not something specific to temples.

**Every growth-stage function that writes a building tile also writes an equal-length sequence of small integers (0, 1, 2, 4, 5, [6]) into `7BB4` at the same cells** (segment `0x3496`, offsets `0x7BB4`+n and `0x7C18`+n — two rows of a footprint-shaped index grid, confirmed by the offset arithmetic: `0x7C18 - 0x7BB4 = 0x64 = 100`, the grid width). This looks like a per-cell "which part of this multi-cell building is this" index (skipping index 3/7 in the temple case — plausibly a reserved bounding-box column the 3-wide building doesn't use), not decoded further this pass. Likely relevant to how the renderer picks which section of a multi-cell sprite to draw at each cell — a lead for `GAIUS_ROADMAP.md` Phase 8's still-unresolved "renderer tile-lookup tables" item as much as for footprints.

## 8. The main simulation-tick dispatcher — found as a side effect, addresses a *different* open RE item

Found the caller of the `DS:153A` city-tile dispatcher's own entry point (flat `0x2BBBB`): exactly one function, at `0x293A2`-`0x29465`, structured as a **stage dispatcher** — a jump table (`jmp word ptr cs:[bx + 0x2466]`, keyed by a tick-stage variable at flat `0x6D9D`) selecting between: stage 0 (calls the confirmed `reset_tick` routine `0x2C8D3`, then the confirmed `7BB4.10`→`C9D4.02` derivation routine `0x2DA0D`), stage 1 (`0x2C93F`, `0x28215` — not traced further), and a multi-invocation stage that calls the `DS:153A`-driving entry (`0x2BBBB`) with row-base parameters `0, 0x19, 0x32, 0x4B` (0/25/50/75 — confirming `CAESAR_CITY_STATE_v5.md`'s "iterates exactly 25 rows... called repeatedly with different row bases" precisely), followed by population-stat arithmetic and six more subsystem calls (`0x2E0BE`, `0x2DC72`, `0x2DEC8`, `0x2DD21`, `0x2DE0F`, `0x2DF7D` — not traced further).

Not this addendum's topic (`GAIUS_ROADMAP.md` doesn't ask for it), but directly relevant to `CAESAR_REVERSE_ENGINEERING_COMPLETE.md` Appendix C's separate still-open "Reconstruct full simulation tick" item — recorded here rather than dropped, in case a future session finds it useful. **Confirms, usefully, that this dispatcher does NOT call either of the growth-stage placement functions in section 7** — those are reached some other way, consistent with being triggered by construction/growth events rather than running unconditionally every tick.

## 9. Why finding the dispatch table itself keeps failing — the actual root cause, not just another dead end

Both growth-stage functions in section 7 have **zero direct callers findable by searching for literal `call`/`jmp` instructions targeting their addresses** — the same symptom as every placement-adjacent function traced across this whole addendum. Section 5's pointer-table searches assumed table entries would store a *flat file offset* directly (since near, same-segment calls like `push cs; call 0x2BBBB` resolve that way in this disassembly). That assumption breaks for **far** pointers stored in *data* tables (like `DS:153A` itself): `CAESAR_CONSTRUCTION_RE_v2.md`'s own documented handler address for temple variant 4, `0x2C103`, is **180,483 in decimal — already too large to be a 16-bit offset on its own** (max 65,535). A far pointer necessarily splits this into a real segment value (which 64KB code segment) plus a small in-segment offset, and this pass has no way to know that segment value without either (a) a disassembler that tracks segments/relocations automatically (not available here), or (b) parsing the EXE's own relocation table, embedded in the EXEPACK stub alongside the header copy (`formats/exepack/` decodes the compressed *image* but does not currently expose the relocation table — extracting and using it would be genuinely new engineering, not another search variant). **This is the real, specific, previously-unstated reason all five pointer-table search variants in section 5 came back empty** — not that the search was insufficiently clever, but that it was searching for the wrong shape of value entirely.

*(There is no section 10 — a numbering slip during the fifth pass. Sections 11-14 are cited by number from `GAIUS_ROADMAP.md` and `CLAUDE.md`, so the numbers are left as-is rather than renumbered. Nothing is missing.)*

## 11. The full 256-entry `DS:153A` table — DEFINITIVE, located and extracted byte-exact (fifth session pass)

Section 9 identified the real blocker: pointer tables in this binary store **far** pointers (16-bit offset + 16-bit segment), and no segment value was known, so nothing could be searched for directly. That's now solved for the code segment housing all of `DS:153A`'s handlers.

**Calibration.** `v2`'s own documented handler addresses for adjacent `DS:153A` entries are evenly spaced by exactly `0x56` bytes (temple variants 1-4: `0x2C001`, `0x2C057`, `0x2C0AD`, `0x2C103` — each `+0x56` from the last). Searching the whole image for two 4-byte far pointers, 8 bytes apart (2 table slots), with matching segment and offset delta `0x56`, found **exactly 7 matches, all sharing segment `0x2700`**. Confirmed immediately: `0x2700 * 16 + 0x5001 = 0x2C001` — `v2`'s own temple-variant-1 address, exactly.

**Locating the table precisely.** Using segment `0x2700`, searching directly for the raw `(offset, segment)` bytes of three functions independently found by disassembly in section 7 (`0x2B1C4`, `0x2B551`, `0x2B95B`) found them all present as far pointers, closely spaced — a real dispatch table. Computing the exact table base from one confirmed slot and **verifying all 22 already-independently-published tile addresses (`0x00`-`0x21`) from `CAESAR_CITY_STATE_v5.md`'s own table against it produced a 22-for-22 exact byte match** (table base: flat file offset **`0x75D8A`** in the decompressed US-build image; 256 entries × 4 bytes = `0x400` bytes total, ending at `0x7618A`). This is about as strong a confirmation as this kind of reverse engineering gets — independent agreement between two different discovery methods (a prior RE pass's manual disassembly, and this session's structural search) on 22 data points.

**This corrects `CAESAR_CONSTRUCTION_RE_v2.md` section 5's own table.** Extracting the full table and comparing against `v2`'s published `0xDF`-`0xF5` range shows the *handler addresses* in `v2` are all exactly right, but **every tile ID `v2` attaches them to is off by exactly one** (too low). Corrected table (handler addresses unchanged from `v2`; tile IDs shifted):

| Tile(s) | Handler | `v2`'s claimed tile(s) | Identification (unchanged from `v2`) |
|---|---|---|---|
| `E0`,`E1` | `2C001` | `DF`,`E0` | Temple variant 1 |
| `E2`,`E3` | `2C057` | `E1`,`E2` | Temple variant 2 |
| `E4`,`E5` | `2C0AD` | `E3`,`E4` | Temple variant 3 |
| `E6`,`E7` | `2C103` | `E5`,`E6` | Temple variant 4 |
| `E8` | `2C159` | `E7` | Bath Houses |
| `E9` | `2BC25` | `E8` | Generic/default handler |
| `EA` | `2C159` | `E9` | Bath Houses alias |
| `EB` | `2C1B4` | `EA` | Career / specialized service |
| `EC`,`ED` | `2C20D` | `EB`,`EC` | Hospital |
| `EE` | `2C267` | `ED` | School-like |
| `EF` | `2C2D7` | `EE` | Oracle-like |
| `F0` | `2C330` | `EF` | Theater |
| `F1` | `2C386` | `F0` | Coliseum |
| `F2` | `2C3DC` | `F1` | Hippodrome |
| `F3` | `2C432` | `F2` | Plaza |
| `F4` | `2C475` | `F3` | Barracks |
| `F5`,`F6` | `2C4AB` | `F4`,`F5` | Prefecture |

`systems/service.hpp`/`.cpp` (and `kC9D4BitTable`'s evidence strings) are updated to the corrected tile IDs; behavior/radii/masks are unchanged since those came from `v2`'s handler addresses, which were already right.

**What this means for section 7's tile-`0x14`/`0x15`/`0x20` "growth-stage" functions**: with the table now fully mapped, tiles `0x14` and `0x15` are confirmed to be genuinely distinct from the `0xE0`-`0xF6` civic-building range — they're part of the `0x00`-`0x35` low range `CAESAR_CITY_STATE_v5.md` already covered, sitting right at the edge of (but per the roadmap's own scoping, just outside) the `0x00`-`0x15` "housing" range `GAIUS_ROADMAP.md` Phase 4 defined. `systems::housing` currently only implements tile `0x00`'s handler; this table extraction hands over ready-to-use, **verified-correct handler addresses for tiles `0x01`-`0x35`** (section 7's temple-growth-shaped functions at `0x14`/`0x15`/`0x20` being three of them, already disassembled) that Phase 4 work could pick up directly without re-deriving them.

**What this does NOT do**: it's still the *simulation* dispatch table (tile ID → per-tick behavior), not the *construction* dispatch (UI command → tile write) that is this document's actual subject and Phase 5's actual ask. That link remains exactly as open as section 9 described — this section closes out `v2`'s own stated "next reverse-engineering target" (decode the complete `DS:153A` table) as a valuable side effect, and meaningfully improves confidence in everything built on top of it (Phase 3's building table, Phase 4's housing tiles), without closing Phase 5's core question.

## 12. THE CONSTRUCTION DISPATCHER — FOUND. `DS:127C`, and the whole chain end to end (sixth pass)

Section 12 predicted the segment-calibration technique from section 11 would crack this. It did, via a better version of it: instead of calibrating one table, **scan the whole image for *every* far-pointer table generically** — runs of ≥8 consecutive 4-byte entries sharing a segment word. That found 19 tables. One immediately stood out: **44 entries at flat `0x75ACC`, segment `0x11C6`**, whose addressable window (`0x11C60`-`0x21C5F`) covers exactly the construction/placement code region sections 3 and 7 had been poking at.

Its DS offset is `0x75ACC - 0x74850` = **`0x127C`** — and the very first `lcall`-through-memory sweep this whole investigation ever ran (section 3's tooling) had already logged **four call sites through `[bx + 0x127c]`**, at `0x11DDA`, `0x11E8A`, `0x11FA1`, `0x1207A`. The table had been sitting in the earliest scan output the entire time, unrecognised because the segment convention wasn't understood yet.

(That subtraction also confirms **DS base = `0x74850`** exactly, since `DS:153A` resolves as `0x74850 + 0x153A = 0x75D8A`, the verified table base from section 11. `CAESAR_CONSTRUCTION_RE_v2.md` section 2's worked example "DS:6CA2 -> flat 0x7B4F4" is off by 2 — it should be `0x7B4F2`. Minor typo in that doc, now explained.)

**The dispatch code**, at `0x11DAC`:

```asm
011dac  cmp word ptr [0x6d0c], 0xc     ; command 12 == Forum: special-cased for grade+cost
011db3  mov bx, word ptr [0x6d89]      ;   selected forum grade
011dbe  mov ax, word ptr es:[bx+0x15a0];   grade cost table (segment 0x3496)
011dc3  cmp ax, word ptr [0x6ca2]      ;   vs available funds
011dc9  jmp 0x120b6                    ;   can't afford -> abort
011dcc  mov word ptr [0x6d0a], 0       ; clear failure flag
011dd2  mov bx, word ptr [0x6d0c]      ; bx = COMMAND ID
011dd6  shl bx, 1
011dd8  shl bx, 1                      ; * 4 (far pointer stride)
011dda  lcall [bx + 0x127c]            ; call construction_handler[command_id]
011dde  cmp word ptr [0x6d0a], 0       ; handler sets this to 1 on rejection
```

So: **`[0x6D0C]` = the selected construction command id**, **`DS:127C[id]` = its placement handler**, **`[0x6D0A]` = the handler's success/failure flag**, **`[0x6CA2]` = available funds**, **`3496:15A0[grade]` = the forum grade cost table** (8 grades, matching the manual's Aventine 60 / Caelian 100 / Esquiline 140 / Janiculan 200 / Regia 250 / Pincian 300 / Palatine 350 / Romanum 500 Dn).

**Command ids are now DEFINITIVE, not inferred.** ~30 toolbar-button handlers in `0x172CD`-`0x17BDA` each do `mov word ptr [0x6d0c], <literal>`, and every literal matches this document's section-1 string-table ordinal exactly: `0x02`=Clear Area, `0x03`=Go to Forum, `0x04`=Road, `0x05`=Resevoir\pipe, `0x07`=Wall, `0x08`=Tower, `0x09`=Well, `0x0A`=Fountain, `0x0B`=Housing, `0x0C`=Forum, `0x0D`=Temple, `0x0E`=Bath Houses, `0x0F`=Hospital, `0x10`=School, `0x11`=Oracle, `0x13`=Go to Province, `0x14`/`0x15`/`0x16`=Theater/Coliseum/Hippodrome, `0x17`=Plaza, `0x18`=Barracks, `0x19`=Prefecture, `0x1A`/`0x1B`/`0x1C`=Heavy Industry/Market/Workshop, `0x1D`=Fort, `0x1E`=Halt, `0x1F`/`0x20`/`0x21`=the three Cohort orders. Section 1's "HIGH CONFIDENCE, not DEFINITIVE" caveat is now lifted. (The dispatch table has 44 entries; ids 34-43 have handlers but no display string — unidentified, out of scope.)

## 13. Per-command placement: seed tiles, footprints, and the terrain gate — DEFINITIVE

Every placement handler reached through `DS:127C` follows one of a few shapes. Single-cell buildings inline the write; multi-cell ones set three globals and call a shared routine at `0x1232E`:

```asm
mov word ptr [0x6d08], 3      ; WIDTH
mov word ptr [0x6d06], 2      ; HEIGHT
mov word ptr [0x6d04], 0xf1   ; SEED TILE
push cs; call 0x1232e         ; generic multi-cell placement
```

**The terrain gate**, identical in every traced handler: the target cell's existing tile must satisfy `0x1D <= tile <= 0x35`, else the handler sets the failure flag and writes nothing. That range is *exactly* the span `DS:153A` fills with no-op handlers (section 11's table: `0x16`-`0x1D`, `0x21`-`0x35` all → the shared no-op `0x296D9`) — i.e. "you may only build on ground that has no simulation behaviour," enforced by the same numbers from both directions. It also explains the stray `0x1D` filler tile in section 7's temple growth footprint.

On success the handler writes the seed tile across the footprint and clears `7BB4` at each cell (`mov 3496:[bx+0x7bb4], 0`).

**Complete extracted table** (transcribed directly from the handlers; `systems/construction.cpp` encodes exactly this):

| Cmd | Name | Seed tile | Footprint | Kind |
|---:|---|---|---|---|
| 5 | Resevoir\pipe | `0xA4` | 1×1 | single-cell |
| 8 | Tower | `0x9E` | 1×1 | single-cell (also writes `0x9F`/`0x9A`/`0x9B` variants by orientation) |
| 9 | Well | `0xB8` | 1×1 | single-cell |
| 10 | Fountain | `0xBA` | 1×1 | single-cell |
| 11 | Housing | `0xC8` | 1×1 | single-cell |
| 13 | Temple | `0xD8` | 1×1 | single-cell (then grows — see section 7) |
| 14 | Bath Houses | `0xE8` | 1×1 | single-cell |
| 17 | Oracle | `0xEB` | 2×1 | multi-cell |
| 16 | School | `0xEC` | 2×2 | multi-cell |
| 15 | Hospital | `0xED` | 2×2 | multi-cell |
| 25 | Prefecture | `0xEE` | 1×1 | single-cell |
| 24 | Barracks | `0xEF` | 3×3 | multi-cell |
| 20 | Theater | `0xF0` | 2×1 | multi-cell |
| 21 | Coliseum | `0xF1` | 3×2 | multi-cell |
| 22 | Hippodrome | `0xF2` | 4×2 | multi-cell |
| 26 | Heavy Industry | `0xF3` | 4×4 | multi-cell |
| 27 | Market | `0xF4` | 2×2 | multi-cell |
| 12 | Forum | (per grade) | 4×4 | seed chosen from the 8-grade table at runtime — not decoded |
| 28 | Workshop | (per goods type) | 3×3 | seed chosen per type — not decoded |
| 2, 4, 7, 23 | Clear Area, Road, Wall, Plaza | n/a | drag | auto-tiled, tile depends on neighbours (section 3's `0x36`-`0x40` mechanism) |
| 0,1,3,6,18,19,29-33 | No action, Main Toolbar, Go to Forum, Maps, Career, Go to Province, Fort, Halt, Cohort ×3 | none | — | UI/mode actions, write no tile |

**This also corrects several building identifications `CAESAR_CONSTRUCTION_RE_v2.md` could only guess at**, because the construction side is independent evidence for what a tile *is*. Cross-referencing seeds against section 11's corrected `DS:153A` table: `0xF0`/`0xF1`/`0xF2` confirm as Theater/Coliseum/Hippodrome (exact agreement, both directions). But `0xEB` — which `v2` labelled "Career / specialized service" and couldn't identify — is **Oracle**. `0xEC` (which `v2` called a "Hospital alias") is **School**; `0xED` is Hospital; they merely share a simulation handler because their service behaviour is identical. And `0xEE`, which `v2`'s shifted table called "School-like", is **Prefecture**. Relative footprint sizes independently sanity-check against the manual (hippodrome largest of the entertainment trio; heavy industry and forum the largest overall at 4×4).

## 14. What's left, after all this

Phase 5's core RE question is **answered** (sections 12-13): the construction dispatcher, its command ids, its per-command seed tiles, footprints, and terrain gate are all recovered and encoded in `systems/construction.hpp`/`.cpp`. What remains genuinely open, in rough order of value:

1. **Drag-based auto-tiling** (Road, Wall, Plaza, Clear Area). Section 3 traced the `0x36`-`0x40` mechanism (neighbour checks, mode selector at `[0x57E2]`, an unidentified property table) but not to implementable precision. These are the four commands `systems::construction` deliberately refuses to place rather than faking. Now much more tractable than before: their handlers are at known addresses (`DS:127C[2]`, `[4]`, `[7]`, `[23]` → `0x12B79`, `0x131E7`, `0x1415E`, `0x15098`), and the whole segment/addressing puzzle that blocked earlier passes is solved.
2. **Forum (8 grades) and Workshop (8 goods types) seed tiles** — footprints are known (4×4, 3×3); the per-variant seed comes from a runtime table. The forum grade *cost* table was located in passing (`3496:15A0`, indexed by `[0x6D89]`); the seed table is presumably adjacent.
3. **Building costs generally** — the manual has all the prices (Appendix A) and the executable has at least the forum grade cost table at `3496:15A0`. Nothing has been transcribed into `systems::construction` yet because the roadmap ties costs to a treasury the engine doesn't have.
4. **The 10 unnamed dispatch entries** (command ids 34-43) — real handlers, no display strings, unidentified.
5. **`[0x6D0C]`'s toolbar plumbing** — which on-screen button maps to which command id is implied by the ~30 assignment sites in `0x172CD`-`0x17BDA`, but the button *geometry* (hit rectangles) wasn't extracted; Phase 5's toolbar-UI item would want it.

The technique that broke this open, for reuse: **scan the whole image for far-pointer tables generically** (runs of ≥8 consecutive 4-byte entries sharing a segment word), then identify tables by which code region their targets land in. That found all 19 tables in the binary in one pass, including `DS:134C`/`DS:1384` (the actor type/state tables `CAESAR_CITY_STATE_v9.md` wanted) which nobody has decoded yet — a ready-made next target.

## 15. The service layer, read directly -- and checked against the running game (2026-09-12/13)

Everything `systems::service` implements was previously transcribed from the RE corpus's parameter tables (`CAESAR_CONSTRUCTION_RE_v2.md`, `CAESAR_CITY_STATE_v5/v6.md`). Four real saves from one play session made it possible to check that against the game, and the check failed in ways that sent this pass back to the binary. Every routine below was disassembled from the decompressed US-build image; addresses are flat. Where this section contradicts sections 2-3, it wins -- those are kept as the record.

### 15.1 The tick

- **`0x293A2` is a phased tick**, a switch over tick-phase numbers. One phase calls the reset `0x2C8D3` and then `0x2DA0D`; four phases call the scan `0x2BBBB` with start rows **0, 25, 50, 75**; a later phase publishes per-scan counters into saved globals, dividing some by building footprint area (`/4`, `/16`).
- **`0x2BBBB`** loops 25 rows x 100 columns and does `cmp tile, 0x35; jbe skip; lcall [tile*4 + 0x153A]`. **It is the only instruction in the executable that references `0x153A` in any operand form** (exhaustive search). So DS:153A entries `0x00`-`0x35` never run: tile `0x00`'s handler (`0x297AC`, the one Phase 4 implemented from v5) is referenced only from its own table slot, with no direct callers. **Corrected in section 16:** it is not dead code. Those slots belong to the adjacent `DS:1212` table, and `0x297AC` is housing tile `0xCA`'s development handler.
- **`0x2C8D3`**, the reset, is exactly what Gaius already had: `C9D4 &= 0x12`, `A2C4 = 0`, `2D94 = 0x3F`, every cell. Land value is never reset.
- **`0x2DA0D`**: for every cell, if `C9D4.10` set `0x02` and clear `0x10`, else clear `0x02`. Skipped while `DS:0x6D9B` is nonzero. The source bit is `C9D4.10`, not `7BB4.10` as previously documented.

### 15.2 The propagators

All three clip a square (Chebyshev) radius to the grid with identical bounds code. Arguments are pushed ceiling, radius, delta, row, col (flags: radius, mask, row, col).

- **`0x2C577` coverage.** Per cell: `2D94 = min(2D94, ceiling)`; `A2C4 += delta` (byte, wraps); if `A2C4 > 2D94` (signed), `A2C4 = 2D94`. No lower clamp. The ceiling is a **per-cell running minimum** over every source in the tick, so a low-ceiling source caps all others in range. v5 described a ratchet that could only be raised; the code only ever lowers it.
- **`0x2C6AF` land value.** `54A4 += delta` (byte); if `54A4 > ceiling` (signed), `54A4 = ceiling`. **No floor** -- which is why a real save holds -43.
- **`0x2C7BB` flags.** ORs the mask. If `DS:0x6CFC` is nonzero, the *first* cell is ANDed instead and the global is cleared after that one cell.
- **`0x2DA7E` is not a propagator.** It is a per-cell step: `+growth` if `C9D4.20`, else -2, then clamp to -8..+50. This is where the corpus's -8..+50 lives. No DS:153A handler calls it; its caller is unidentified.

### 15.3 Every handler that calls them

Coverage is (delta, radius, ceiling); flags (radius, mask); land value (delta, radius, ceiling). **Bold** marks a value that differs from what Gaius previously implemented, or a handler it didn't have.

| tiles | handler | building | effect |
|---|---|---|---|
| `36`-`3B` | `2BC26` | road/wall family | if 7BB4.10: cov (+1, 1, 31) |
| `3C`-`3F` | `2BC6A` | road/wall family | cov (+2 if 7BB4.10 else +1, **1**, 31); **no land value** |
| `40` | `2BCB9` | road/wall family | cov (+3 if 7BB4.10 else +2, 1, 31) |
| `94`-`95` | `2BD08` | unidentified | **cov (+1, 2, 8); land (-2, 2, 64)** |
| `A2`-`A3`, `A7`-`B2` | `2BD3D` | unidentified | **cov (-2, 3, 8); cov (-2, 1, 8)** |
| `B9`, `BB`, `BC` | `2BD72` | unidentified | **if C9D4.01: cov (+1, 2, 31)** |
| `C8`-`D7` | `2BDA2`-`2BF06` | housing tiers | **cov (base + -1/0/+1/+1/+2/+2, radius 1/1/1/2/1/2, ceiling 4/8/31/31/31/31)**, base = `DS:0x6BF8` |
| `D8`-`DB` | `2BF4F` | temple stages | **cov (+1, 2, 31); land (-2, 2, 53)** |
| `DC`-`DF` | `2BFA8` | temple stages | **cov (+1, 3, 31); land (-2, 3, 37)** |
| `E0`-`E7` | `2C001` / `2C057` / `2C0AD` / `2C103` | temples v1-v4 | cov (+1, **2/3/4**/5, 31); flags (6/8/10/12, `20`) |
| `E8`, `EA` | `2C159` | Bath Houses | if 7BB4.10: cov (+1, **2**, 31); flags (**3**, `04`) |
| `EB` | `2C1B4` | Oracle | cov (+2, 8, 31); land (-2, 5, 32) |
| `EC`-`ED` | `2C20D` | School / Hospital | cov (+1, 3, 31); flags (4, `40`) |
| `EE` | `2C267` | Prefecture | cov (+1, 2, **8**); flags (4, `20`); land (-2, 3, 48) |
| `EF` | `2C2D7` | Barracks | cov (+1, **3**, **5**); land (**-3**, **5**, 32); **no flags** |
| `F0` | `2C330` | Theater | cov (+1, 3, 31); flags (4, `80`) |
| `F1` | `2C386` | Coliseum | cov (+1, 4, 31); flags (6, `80`) |
| `F2` | `2C3DC` | Hippodrome | cov (+1, 5, 31); flags (7, `80`) |
| `F3` | `2C432` | Heavy Industry | cov (+1, 4, **2**) |
| `F4` | `2C475` | Market | cov (+1, 1, **16**); flags (6, `08`) |
| `F5`-`F6` | `2C4AB` | unidentified, 3x3 | cov (+1, 3, **3**) |

A scan of the whole table, bounded at each handler's true end, found no other DS:153A handler that calls any of the three propagators. Building identities come from section 13's construction seeds and real-save footprints, not from v2's labels, which were wrong for `0xEB`-`0xF4`. And `CAESAR_CITY_STATE_v5.md`'s parameters for `0x3C`-`0x3F` (cov +1 r2, land -2 r2 ceiling `0x40`) are really the code at `0x2BD08`, which tiles `0x94`/`0x95` dispatch to.

### 15.4 Housing, located

- The six `0xC8`-`0xD7` handlers share one template and differ only in tier parameters that rise with the tile id and cap at 31. `0xC8` is Housing's construction seed (section 13), the range ends where the temple family begins, and in real saves these tiles line the roads. **STRONG INFERENCE** that they are the manual's sixteen grades. The handlers never change the tile id, so the grade-change routine is still unfound.
- **Corrected in section 16:** `0x297AC` is not tile `0x00`'s handler and is not unreachable -- it is `DS:1212`'s development handler for housing tile `0xCA`. Its decode was right; its attribution was wrong.
- In the saves, all 146 cells of `0x00`-`0x1C` form bordered blobs far from the city and are byte-identical across all four saves.

### 15.5 The proof

`tools/sim_check` and `test_save_corpus_simulation` run `reset_tick` plus one `dispatch_tile` pass over each save's own tile grid and compare the result against the layers the engine saved. With the five untranscribed handlers still missing, coverage matched 9988, 10000, 9856 and 9856 of 10000 cells, and every mismatch sat on or beside those handlers' tiles (saved bytes of 254/255 from negative coverage). With them added: **10000/10000 coverage, and 10000/10000 on each of C9D4 bits `04`, `08`, `20`, `40` and `80`, in all four saves.** Independently, the two scan counters the tick publishes -- `DS:0x6BF2` (handler tile set `C8`-`E8`, `EA`-`F3`, `F5`-`F6`) and `DS:0x6BF0` (`36`-`40`) -- equal the grids' own counts exactly: 20/191/251/251 and 40/90/141/153.

### 15.6 Still open

- Who calls `0x2DA7E` (the -8..+50 land-value step), and so how land value is actually bounded over time. -- answered in section 16: the housing development pass.
- The event routines behind the per-scan counter thresholds (`0x2C525`, `0x2C54E`, `0x2C4EA`), and where the thresholds come from. **The routines are read in section 21**; where the thresholds come from is still open.
- The housing grade-change routine; the identities of `0x94`/`95`, `0xA2`-`B2`, `0xB9`/`BB`/`BC` and `0xF5`/`F6`; what sets `C9D4.01` and `DS:0x6BF8`. -- the grade-change routine is answered in section 16.
- What reads the C9D4 service bits.

## 16. Housing, read directly -- and a correction to section 15 (2026-09-13)

**Correction first.** Sections 11 and 15 treated the far pointers from `DS:153A` onward as one table indexed by tile id, and 15.1/15.4 concluded that its entries `0x00`-`0x35` -- including `0x297AC`, "tile `0x00`'s handler" -- were dead code. They aren't. `DS:153A` and `DS:1212` are adjacent tables: the monthly service scan (`0x2BBBB`) indexes `DS:153A` only for tiles above `0x35`, the housing development pass (`0x294CF`) indexes `DS:1212` only for tiles `0xC8` and up, and `DS:1212 + 0xCA*4` *is* `DS:153A`. So the slots read as "tiles `0x00`-`0x35`" are `DS:1212`'s entries for tiles `0xCA`-`0xFF`, verified byte for byte, and `0x297AC` is the development handler for housing tile `0xCA`. `CAESAR_CITY_STATE_v5.md` made the same misreading, which is why it agreed with section 11 22-for-22. What remains true: tiles `0x00`-`0x35` are never simulated by either table, and in real saves they are static terrain.

### 16.1 The month

A month is 106 steps, counted in `DS:0x6D9D` (dispatcher at `0x2936A`):

- **Steps 0-99:** the housing development pass for row = step (`0x294CF`), then five other per-row routines (`0x2D2F5`, `0x2CE7C`, `0x2CD1C`, `0x2E209`, `0x2CD10`), read in sections 19.4 and 20.
- **Steps 100-105** (jump table at `0x29466`): 100, the reset `0x2C8D3` and `0x2DA0D`; 101, population and water (`0x2C93F`) and `0x28215`; 102-105, the four quarter scans (`0x2BBBB` from rows 0, 25, 50 and 75), the last also publishing the scan counters and calling six monthly routines.
- **Rollover** (`0x29476`): at step 106 the step resets and month `DS:0x6C1C` advances; at 12, month resets, year `DS:0x6C32` advances, `DS:0x6C7C` is set to 80 and `0x28238` runs. `DS:0x6D9B` counts months mod 18, and `0x2DA0D` only runs while it is 0.

Across the four saves the month reads 9, 1, 4, 6 and the year -11, -7, -2, -1 -- consistent with BC dates (STRONG INFERENCE). Because services are rebuilt at the end of a month and houses develop during the next, houses always react to last month's services.

### 16.2 Population and water (`0x2C93F`, step 101)

- **Population units** `DS:0x6C10` are the sum, over every tile `0xC8`-`0xD7`, of the per-cell table at `3496:007E`: `1 1 2 3 3 5 6 5 6 4 4 3 3 2 2 1`. `DS:0x6C0E` is four times that. It matches the saves exactly (0, 63, 146) except `CAESARVX.SAV`, which is 2 high -- that save holds exactly one `0xCF`, the +2 of a `0xCB` -> `0xCF` upgrade made after the count. The second session's saves (section 23) match too, except `CAESARXW.SAV` (1 low) and `CAESARXQ.SAV` (32 high). `0x2C93F` is the only routine that writes `DS:0x6C10`, and it counts every `0xC8`-`0xD7` cell with no condition (flat `0x2CA83`-`0x2CABA`), so those are houses that changed after the count as well.
- **Water, `C9D4.01`:** wells (`0xB8`) radius 1, reservoirs (`0xA4`) radius 3, and fountains (`0xB9`-`0xBD`) radius 6 when a supply check (`0x2CAE6`, which traces pipes through `0x2CBF1`) succeeds; the same check flips fountains between working `0xB9`/`0xBB` and dry `0xBA`/`0xBD`. It matches the saved `C9D4.01` 10000/10000 in all four saves -- which contain wells and dry fountains, but no reservoir or working fountain.

### 16.3 The development pass (`0x294CF`)

For each cell of its row:

- **Below `0xC8`:** land value is zeroed. A jump table at `0x295F8` sends `0xB9`/`0xBA` to `0x2BACE` (coverage > 10 and population > 50 -> `0xBD`, or `0xBB` if watered), `0xBB`-`0xBD` to `0x2BB48` (coverage < 10 -> `0xBA`, or `0xB9` if watered), and `0xA8`/`AB`/`AE`/`B1` to `0x29624`, which either decrements the tile or picks one of eight neighbours from a table at `3496:1DE4` and, if that neighbour is housing, calls `11C6:0A5A` -- both driven by the RNG in segment `2EF9`. Section 21.2 reads it: these are burning tiles.
- **`0xC8`-`0xD6`:** one `evolve_land_value` step with growth `DS:0x6BF6 + (random & 3) - 1`, drawn once per row. **`0xD7` and up:** land value zeroed.
- **Then**, if the cell's `7BB4 & 0x0F` is 0, the pass copies its C9D4 byte to `DS:0x6DC7` and calls `DS:1212[tile]`. Handlers that cover extra columns advance the column counter themselves.

### 16.4 Part indices

`7BB4`'s low nibble is each cell's position within its building, `4*dy + dx`. The shared construction footprint writer (`0x1232E`) writes it for every footprint cell, the development handlers write it when houses merge, and the pass only develops part 0. It holds on every multi-cell building in all four saves (heavy industry reads `0` through `F`). `systems::construction::place` had written 0 everywhere; fixed.

### 16.5 The development handlers (`DS:1212`)

A is the anchor's A2C4 (signed), P the population units, and the letters the anchor's C9D4 service bits: W water `01`, N `02`, M market `08`, B bath houses `04`, S school/hospital `40`, E entertainment `80`. The exact absorbable-cell test for each growth step is in `systems/housing.cpp`.

| tile | shape | land-value gate | demote | promote |
|---|---|---|---|---|
| `C8` | 1x1 | > 20 | A<0 -> `1D` | A>0 -> `C9` |
| `C9` | 1x1 | > 30 | A<1 -> `C8` | A>1, W -> `CA` |
| `CA` | 1x1 | > 40 | A<2 or no W -> `C9` | A>2 -> `CB` |
| `CB` | 1x1 | > 48 | A<3 or no W -> `CA` | A>4, N, P>=25: pair `CC` if the right cell is open ground or `C8`-`CB`, else `CF` |
| `CC` | pair | | A<5 or no W/N -> two `CB` | A>5, P>=50 -> `CD` |
| `CD` | pair | | A<6 or no W/N -> `CC` | A>6, M, P>=75 -> `CE` |
| `CE` | pair | | A<7 or no W/N/M -> `CD` | A>7, B, P>=100 -> `D1` |
| `CF` | 1x1 | | A<5 or no W/N -> `CB` | A>6, M, P>=75 -> `D0` |
| `D0` | 1x1 | | A<7 or no W/N/M -> `CF` | A>7, B, P>=100: pair `D1` over open ground, `C8`-`CB` or `CF`-`D0` |
| `D1` | pair | | A<8 or no W/N/M/B -> `CE` | A>10, P>=125 -> `D2` |
| `D2` | pair | | A<11 or no W/N/M/B -> `D1` | A>13, S, P>=150 -> `D3` |
| `D3` | pair | | A<14 or no W/N/M/B/S -> `D2` | A>16, P>=175 -> `D4` |
| `D4` | pair | | A<17 or no W/N/M/B/S -> `D3` | A>18, E, P>=200: 2x2 `D5` if the two cells below qualify |
| `D5` | 2x2 | | A<19 or any bit missing -> two `D4` pairs | A>20, P>=225 -> `D6` |
| `D6` | 2x2 | | A<21 or any bit missing -> `D5` | A>22, P>=250: 3x3 `D7` if the five new cells qualify |
| `D7` | 3x3 | | A<23 or any bit missing -> a `D6` 2x2, a `D4` pair and three `D0` | -- |
| `D8` | 1x1 | | -- | A>2, P>5 -> `D9` |
| `D9` | 1x1 | | A<3 -> `D8` | A>5, P>=15: 1x2 downward `DA` over open ground, `C8`-`CB` or `D8`-`D9` |
| `DA` | 1x2 | | A<6 -> `D9`, open ground below | A>10, P>=25 -> `DB` |
| `DB` | 1x2 | | A<11 -> `DA` | A>14, P>=40 -> `DC` |
| `DC` | 1x2 | | A<15 -> `DB` | A>17, P>=75: 2x2 `DD` |
| `DD` | 2x2 | | A<18 -> `DC`, open ground on the right | A>20, P>=125 -> `DE` |
| `DE` | 2x2 | | A<21 -> `DD` | A>23, P>=200: 3x2 `DF` |
| `DF` | 3x2 | | A<24 -> `DE`, open ground on the right | -- |
| `E8` | 1x1 | | -- | first sets 7BB4.10 = water; then A>12, P>=40: 2x2 `EA` |
| `EA` | 2x2 | | A<12 -> two `E8` (7BB4 `0x40`), open ground below | then sets 7BB4.10 = water on all four cells |

Every other id from `0xC8` up points at a bare `retf` (`0x296D9`).

### 16.6 `land_value_allows` (`0x2DB49`), in full

If the cell's land value exceeds the threshold: tile `0xA7`, and A2C4, 7BB4 and 54A4 zeroed. Then it calls `0334:28F9(10, col, row)`; if that returns nonzero it fills in an actor record (`DS:[0x6DBD]`, 50-byte records at `DS:0x5D86`) and calls `31E0:0634(5)` and `0x27A54`. Then `DS:0x6C3C` -= 2 (floored at 0), `DS:0x6C84` = 2, and it returns 1; otherwise it returns 0. `CAESAR_CITY_STATE_v5.md` had only the first sentence.

### 16.7 Validation, and what's still open

Validated against real saves: part indices on every building, population units (three exact, one explained), water (10000/10000), and -- through `rebuild_services` -- coverage and C9D4 bits `01`, `04`, `08`, `20`, `40` and `80` (10000/10000 each). The development handlers have no direct ground truth in four snapshots taken months apart, so the transcription is pinned by exact-write unit tests instead.

Still open: the RNG in segment `2EF9`, which drives land-value growth and the `0xA8`-`0xB1` routine; the pipe tracer `0x2CBF1`; `0334:28F9` and the actor spawn; the five other per-row routines and six monthly routines; what `DS:0x6C3C` measures.

## 17. The step dispatcher, the calendar and the random number generator (2026-09-13)

Read directly, and implemented in `systems/month.hpp`/`.cpp`.

### 17.1 The step dispatcher (`0x2936A`)

Every step starts by calling the calendar check (`0x29472` falls into `0x29476`). If `DS:0x6D97` is set, it clears it and runs `0x2D6F4`. After that:

- **Steps 0-99** (`0x29447`): `0x294CF` (housing, row = step), then `0x2D2F5`, `0x2CE7C`, `0x2CD1C`, `0x2E209` and `0x2CD10`.
- **Steps 100-105**, through a jump table at `0x29466`:
  - **100:** `0x2C8D3` (reset) and `0x2DA0D` (derive network flags, only while `DS:0x6D9B` is 0).
  - **101:** `0x2C93F` (population and water) and `0x28215`.
  - **102:** zero the five scan counters `DS:0x6E04`-`0x6E0C`, then scan from row 0.
  - **103** and **104:** scan from rows 25 and 50.
  - **105:** scan from row 75, then publish the counters to `DS:0x6BF0`, `0x6BF2`, `0x6BEE` (÷4), `0x6BEA` (÷16) and `0x6BEC` (÷4), then call `0x2E0BE`, `0x2DC72`, `0x2DEC8`, `0x2DD21`, `0x2DE0F` and `0x2DF7D`.

### 17.2 The calendar (`0x29476`)

When the step counter `DS:0x6D9D` reaches 106, it resets to 0 and the month `DS:0x6C1C` advances. At 12 the month resets, the year `DS:0x6C32` advances, `DS:0x6C7C` is set to 80 and `0x28238` runs. Then, every month, `DS:0x6D9B` advances; at 18 it resets, sets `DS:0x6D97` = 1 (so `0x2D6F4` runs on the next step) and advances `DS:0x6D99`, which wraps at 2 into `DS:0x6D95`. The step and 18-month counters aren't in the save.

### 17.3 The random number generator (`2EF9:1425`)

State at `2EF9:0286`-`028E`. The image's initial values are 55, 49, 12, 12, 55, and nothing reseeds it: `028E` is written only by the generator itself.

- `2EF9:13F0` steps a 16-bit shift register: with `s` = `028E`, feedback = bit 0 xor bit 7, and the new `s` = `((s & 0x7FFF) | feedback << 15) >> 1`.
- `2EF9:1425` copies `0288` into `028A`, steps the register, stores the result in `028C` and its low 7 bits in `0288`, then adds the low 3 bits to `0286`, subtracting 99 whenever that exceeds 99.

`0286` is therefore a random walk over 1-99. The housing pass never calls the generator; it reads `0286`, and each row's land-value growth is `DS:0x6BF6 + (0286 & 3) - 1` as a signed byte.

The generator has 45 call sites. In the monthly simulation path five draw each month (and the main loop draws once per frame besides -- section 20.1):
- `0x2E209`, only at step 80 — after that step's housing row, so rows 0-80 share one growth value and rows 81-99 the next.
- `0x2DF7D`, four draws at step 105. Each one rolls `0286` against a threshold (`DS:0x6BE0`, `0x6BE2`, `0x6BE4`, `0x6BDE`) and, if it passes and a count is nonzero, halves `028C` until it fits that count to pick a target. It is the monthly event roll, read in section 21.1.

The other call sites are terrain generation (`0x6F0D`-`0x7079`), UI screens (`0x29158`, `0x2922F`, `0x27DFB`) and similar. `0x2898E` is only reached from `0x290C1`, and whether the yearly routine `0x28238` draws is still unchecked. Because terrain generation and screens draw too, a real session's sequence can't be recovered from a save.

## 18. Roads, walls, plazas and Clear Area (2026-09-13)

Closes section 14's first open item. Road (command 4), Wall (7), Plaza (23) and Clear Area (2) are read from their `DS:127C` handlers, together with the helpers and the neighbour pattern table, and implemented in `systems::construction` (`place_road`, `place_wall`, `place_plaza`, `clear_area`). Every handler recomputes the cursor cell (row `[0x6CB4] + ([0x6D5C]+8)/16`, column `[0x6CB6] + ([0x6D5E]+8)/16`) before each tile access, so the code was lifted with a pattern matcher that folds that idiom into cursor-relative accesses. Dragging calls a handler once for every cell the cursor passes over.

Section 3's `0x17EC5` family is the second half of this mechanism: re-tiling the neighbours. Section 3 found it but couldn't place it.

### 18.1 Choosing a tile from the neighbours

- **`0334:4161` (flat `0x74A1`)** snapshots the eight neighbours into `3496:0328`, clockwise from north: N, NE, E, SE, S, SW, W, NW, confirmed from its jump table at flat `0x783B`. An off-grid neighbour reads 0. A neighbour whose tile *is* 0 skips the write, so the byte keeps the previous call's value. The buffer isn't in the executable image, so it starts at zero.
- **`0x17C08`** clears eight flags at `3496:14AA`. **`0x17C20(lo, hi)`** sets the flag for each snapshot byte in `[lo, hi]` and returns how many there were. The connectable ranges are `0x36`-`0x43`, `0x82`-`0x89`, `0x5E`-`0x61` and `0x94`-`0x95` for roads, and `0x92`-`0xA3` and `0xB3`-`0xB7` for walls.
- **`0x17CB9`** scans 161 sixteen-byte entries at `3496:0A9A`.
  - Bytes 0-7 are a pattern: 0 = neighbour must be unflagged, 1 = flagged, 2 = either.
  - The first entry that fits gives the tile (byte 8, always `0x36`-`0x40`) and a re-tiling mode 0-4 for each orthogonal neighbour (bytes 9-12, stored in `DS:0x57E2`, `0x57E0`, `0x57DE` and `0x57DC` for north, east, south and west).
  - If nothing fits, it returns 0, which refuses the cell, and leaves the modes unchanged.
  - The four single-neighbour entries fix which index is which direction.

### 18.2 Re-tiling the neighbours

- **Roads** use `0x17EB0`, which calls `0x17EC5` (north), `0x1834D` (east), `0x1879C` (south) and `0x18C27` (west).
- **Walls** use `0x1C119`, which calls `0x1C12E`, `0x1C622`, `0x1CB19` and `0x1D010`.

Each routine does nothing if the neighbour is off the grid, is in a protected set (crossings and gates) or has mode 0. Otherwise:
- **mode 1** writes the straight piece;
- **modes 2-4** keep a junction piece if the neighbour already has a compatible one, and otherwise write the corner, T or crossing piece.

The exact sets are data in `systems/construction.cpp` (`kRoadRules`, `kWallRules`).

### 18.3 The handlers

- **Road (`0x131E7`)** refuses anything below `0x1D`. On `0x1D`-`0x41` it places the pattern's tile, clears `7BB4` and re-tiles the neighbours. It also has special cases:
  - **Crossings:** water `0x4A`/`0x4E`/`0x52` → `0x82`, `0x56` → `0x5E`, `0x5A` → `0x86`, `0x45` → `0x42`, `0x44` → `0x43`. Each happens only if no neighbour already holds the result, and doesn't re-tile.
  - **Gates, where a road crosses a wall:** `0x93` → `0x95` and `0x92` → `0x94`. These do re-tile the neighbours, using the pattern's modes.
- **Wall (`0x1415E`)** covers open ground and existing wall pieces. It uses the same pattern table and converts the road tile to its wall form (`0x36`→`0x93`, `0x37`→`0x92`, `0x38`-`0x3B`→`0x96`-`0x99`, `0x3C`-`0x40`→`0xB3`-`0xB7`). Special cases: `0x37` → gate `0x95`, `0x36` → gate `0x94`, and `0x45`/`0x44` → `0xA1`/`0xA0`.
- **Plaza (`0x15098`)** works only on road pieces `0x36`-`0x43`: it sets `7BB4` bit `0x10`, and refuses if the bit is already set. That bit is what the renderer draws as frame `0x41` and what raises a road's coverage (section 15).
- **Clear Area (`0x12B79`)** reverts crossings to water (`0x82`/`0x8A` → `0x4A`, `0x5E`/`0x72` → `0x56`, `0x86`/`0x8E` → `0x5A`). It turns `0x27`-`0x49` into `0x1D` (clearing `7BB4`) and `0x92`-`0xC9` into `0x1D`, and restores a reservoir's (`0xA4`) stored tile from its `7BB4`. On buildings (`≥ 0xCA`) it calls **`0x124F8(col, row, 8)`**:
  - It walks to the building's anchor through the `7BB4` part bits.
  - It takes the footprint size from `3496:14B2`.
  - Each footprint cell, in row order, becomes rubble `0xA7 + 3 x (2EF9:0286 & 3)` -- `0xA7`, `0xAA`, `0xAD` or `0xB0` -- after one random draw, with `7BB4` zeroed. (Corrected in section 21.2: this first read `0xA7 + (0286 & 3)`, because the lifter used then hid an `imul`.)
  - Temples (`0xE0`-`0xE7`), `0xF5`/`0xF6` and `0xEF` are first removed from runtime tables of 16-, 24- and 12-byte records (`DS:5BA4`, `DS:585C`, `DS:5B2C`, 30 entries each) by `0x1287C`, `0x12963` and `0x12A94`.
  - Those record sizes × 30 are 480, 720 and 360 bytes. The first two match the save's unidentified `table_480` and `table_720`: very likely the same data (STRONG INFERENCE).
  - This isn't modeled.

### 18.4 Validation

Roads can be checked against the four real saves directly. Every road piece (`0x36`-`0x40`) is reset to `0x1D`, and `place_road` is called on those cells in row order (`test_construction_road_rebuild_real_saves`). That rebuilds 40/40, 90/90, 137/141 and 149/153 road tiles, with nothing refused and no other cell touched. The second session's seven saves (section 23) rebuild 63/76, 146/162, 146/162, 195/210, 198/213, 198/213 and 199/214, again with nothing refused.

The four misses are the same cells in `CAESARVX` and `CAESARUX`, and `CAESARWX` rebuilds that area exactly. In between, road cells next to those junctions were built over (cell (21,80) went from a road corner to a well), and Clear Area doesn't re-tile a cleared road's neighbours. The junction shapes are history that a rebuild from the final grid can't reproduce.

The second session's misses are the same kind. Most are one row of T pieces (`0x3C`) along row 38, columns 65-73, whose south arms point at houses: the road that ran south of them has been built over, so a rebuild lays straight pieces (`0x37`). The rest sit next to wells built over road cells (rows 42-44, columns 55-56 in `CAESARXQ.SAV`). Walls, plazas and clearing have no equivalent evidence in these saves (they contain no walls), so they're pinned by `test_construction_drag_rules` against the lifted code.

## 19. Forum, Workshop, water and the month's economy (2026-09-13)

### 19.1 Tiles `0xE0`-`0xE7` are the Forum, not temples

**Forum (command 12, `0x14D2F`):**
- It refuses once `DS:0x6CA0` reaches 30.
- It sizes the building from the chosen grade `DS:0x6D89`: 2×2 for grades 0-2, 3×3 for 3-6, 4×4 for 7.
- It places tile `0xE0` + grade through the shared footprint writer (`0x1232E`).
- It then fills the first free 16-byte record in `DS:0x5BA4`: +0 column, +2 row, +4 grade, +6 timer = 0, +8 active = 1, +A a 0-7 frame counter. It also increments the count.

Those sizes are exactly what the building table gives tiles `0xE0`-`0xE7`. So the "temple variants" of the inherited corpus, and of sections 11-15 here, are the eight Forum grades:
- `service::apply_temple` is now `apply_forum`;
- the save's `table_480` is the forum records;
- the walker spawner `0x2D2F5` (rows 0 and 50) spawns from forums.

Tiles `0xD8`-`0xDF` are still Temple's growth stages: Temple's seed is `0xD8`.

**Workshop (command 28, `0x15377`):**
- It refuses at `DS:0x6C9E` = 30.
- It's 3×3, with tile `0xF5` + (goods type `DS:0x6D87` ÷ 4): goods 0-3 give `0xF5`, 4-7 give `0xF6`.
- Its 24-byte record in `DS:0x585C` holds +0 column, +2 row, +4 goods, +6 timer, +8 active, +0x10 production level.
- It increments the byte counter `DS:0x5816`[goods].

So `0xF5`/`0xF6` are workshops (section 15's unidentified 3×3 building), `table_720` is their records and `table_8` the per-goods counts. By the same pattern, `0xEF` and `table_120` (10 × 12 bytes at `DS:0x5B2C`, active at +6) are barracks.

**Demolition** removes the record of what it demolishes:
- forums (`0x1287C`);
- workshops (`0x12963`), which also decrement their goods counter;
- barracks (`0x12A94`).

Each routine decrements its count before searching.

**The forum flag quirk.** Removing a forum's record calls the flag routine `0x2C7BB` with its clear-mode flag `DS:0x6CFC` set, for mask `0x02` and then `0x10`, radius 10. But the routine resets `DS:0x6CFC` after the first cell it visits. So the square's first cell gets `C9D4 &= mask`, and every other cell gets `|= mask`. Implemented as the engine behaves (`service::apply_flags_clear_mode`).

### 19.2 The water pass, exactly (`0x2C93F`, `0x2CAE6`, `0x2CBF1`)

**Every cell, in row order:**
- **Reservoir `0xA4`:** water at radius 3.
- **Well `0xB8`:** radius 1.
- **Fountain (`0xB9`-`0xBD`):** asks the supply check, with *working* = (`0xB9` or `0xBB`).
  - **Supplied:** water at radius 6; `0xBA` becomes `0xB9`, `0xBD` becomes `0xBB`.
  - **Not supplied:** `0xB9` becomes `0xBA`, `0xBB`/`0xBC` become `0xBD`, and a sound plays.
- **Houses `0xC8`-`0xD7`:** add to the population count in the same pass.

**The supply check (`0x2CAE6`).** A fountain's `7BB4` byte is its water level.
- The level drops by one.
- Pipes are traced north, east, south and west by `0x2CBF1`:
  - a reservoir (class `0xFF`) gives 1;
  - a fountain (class `0x0B`) with a higher level than this one gives its level − 1, and counts as reached if it has water;
  - anything else follows the turn table or stops.
- The best non-zero result becomes the new level.
- The check returns the level. If the level is 0, it returns 1 anyway when more than one watered fountain was reached, or exactly one and the fountain isn't working.

**The two tables:**
- **Pipe classes (`3496:1BE0`):**
  - class 1: `0x43`, `0x44`, `0x72`-`0x75`, `0x8E`-`0x91`, `0xA0`;
  - class 2: `0x42`, `0x45`, `0x8A`-`0x8D`, `0xA1`;
  - classes 3-6: `0x46`-`0x49`;
  - `0xFF`: `0xA4`-`0xA6`; `0x0B`: `0xB9`-`0xBD`.
- **Turns (`3496:1BA0`):** for each class, the direction leaving for each direction entering, or 8 to stop. Classes 1 and 2 are straight vertical and horizontal pieces; 3-6 are the four corners.

With this, the four real saves' coverage and service bits (including water) still match cell for cell (`tools/sim_check`). They hold only wells and dry fountains, so the tracer itself is pinned by `test_service_fountain_supply`.

**Validated with reservoirs (2026-09-14).** The second session's seven saves (section 23) hold two to four reservoirs and two to four working fountains. In all eleven saves one rebuild reproduces `C9D4.01`, every fountain tile and every fountain level in `7BB4`, cell for cell (`test_save_corpus_simulation`, `tools/sim_check`).

### 19.3 The economy at step 101 (`0x28215`)

After population and water, four routines turn the month's state into the next month's housing inputs. They're given as formulas in `systems/month.hpp`, with their five tables in segment `3496` (`006E`, `0136`, `014B`, `017E`, `01B1`) embedded.
- **`0x28621`** gives `DS:0x6BF4`, from population, the workshop count and `DS:0x6C36`.
- **`0x28694`** gives a 0-100 share `DS:0x6BCC`, and `DS:0x6BFA` = share ÷ 5. The share is what's left of the population after `DS:0x6C06` percent, workshops × 20, forums × 30 and the month's scan counts (`DS:0x6BEA` × 30 + `DS:0x6BEE` × 12, times `DS:0x6BE8` / 4 + 1). It uses the C runtime's 32-bit multiply and divide.
- **`0x28800`** gives `DS:0x6BF8`, the housing coverage base, from `DS:0x6C04` and `DS:0x6BFA`.
- **`0x28826`** gives `DS:0x6BF6`, the land-value growth base, from `DS:0x6C04` and `DS:0x6C06` / 10.
- Then `DS:0x6C00` += `DS:0x6C04`.

Recomputed from each real save's own inputs, all five outputs match in all four saves (`test_month_economy_matches_saves`). `systems::month::run_month` on a whole save now runs this, so the month no longer relies on the bases read from the save. Section 25 names them: `DS:0x6C04` is the population tax rate and `DS:0x6C06` the conscription rate.

### 19.4 The rest of the month, classified

- **The other per-row routines spawn walkers:**
  - `0x2D2F5` from forums;
  - `0x2CE7C` from workshops: one record per row, rows 25-54, computing a 0-7 production level from several city globals and spawning actor kind 8;
  - `0x2CD1C` from barracks: rows 75-84, kind 4.

  All go through the actor allocator `0334:28F9`.
- **Actor movement** is a two-level dispatch in `0x23C52`: actor type through `DS:0x134C` (14 handlers, types 0-13), then state (+0x31) through `DS:0x1384` (16 handlers). Transcribed in section 20.
- **Province-level events:** `0x2E249`/`0x2E220` (step 80) cycle province-map tiles `0x4C`/`0x79`/`0x7A`/`0x61`.
- **Ratings and messages:**
  - the step-105 routines `0x2E0BE`, `0x2DC72`, `0x2DEC8`, `0x2DD21` and `0x2DE0F` work on province, ratings and message globals;
  - so do the 18-month `0x2D6F4` and `0x27BA1` (population milestones, with flags in `table_10`).

  None touches the city grid layers; they belong to Phases 6-7.

## 20. Walkers (2026-09-13)

The actor table's records move by a type x state dispatch. This section reads everything in it that runs on the city map; `systems::actors` implements it.

### 20.1 Where walkers run: the main loop

- **Every frame**, the loop at flat `0xFA13`:
  1. advances the frame phase `DS:0x6DE3` (0-10) and calls `1F6F:1A08`;
  2. draws a random number (`2EF9:1425`);
  3. asks the speed gate `0xFAA2` whether this frame is a simulation step. The gate reads `3496:0000[speed / 10 x 10 + phase]`, with the speed in `DS:0x5292`; row 10 passes every frame, lower rows fewer.
- **On a step frame** it then runs:
  1. `0x1147E`, which steps the frame counters `DS:0x6D44`-`0x6D34` (periods 4, 8, 16 for `0x6D40`, 32 for `0x6D3E`, 64 for `0x6D3C`, ...);
  2. the actor update `0x23C4C`;
  3. one month step `0x2936A`.
- **A correction to 17.3.** The generator draws once per frame, on top of the five draws the steps make. At the fastest speed that's one extra draw per step, which is what Gaius does. Slower speeds also draw on the frames between steps; Gaius doesn't model that.
- **The dispatcher's order** for steps 0-99 is the housing row `0x294CF`, then forums `0x2D2F5`, workshops `0x2CE7C`, barracks `0x2CD1C`, `0x2E209` and `0x2CD10` (`0x27BA1` at step 80). Before any step it calls `0x29472`, and the 18-month routine `0x2D6F4` when `DS:0x6D97` is 1.

### 20.2 The record, the allocator and the update

The record is 50 bytes at `DS:0x5D84` + slot x 50. Renderer findings section 8 gives +0 to +9; the rest:

| Offset | Size | Field |
|---|---|---|
| `+0A` | byte | facing, 0-7 clockwise from north |
| `+0B` | byte | following an obstacle |
| `+0C` | byte | which hand keeps the obstacle: 0 turns clockwise, 1 anticlockwise |
| `+0D`/`+0E` | byte | the follow target cell |
| `+0F` | byte | pixels left before the next cell |
| `+10` | byte | steps until the hand flips |
| `+11` | byte | the flip interval |
| `+12`/`+13` | byte | the destination cell (city actors); the current cell (province actors) |
| `+14` | word | the home record's index, or the actor being chased |
| `+17` | byte | status: `1` stopped, `2` the cell was already occupied, `4` this tick began a cell |
| `+18` | word | the cell: row x 100 + column (row x 40 + column for types 11 and up) |
| `+1A` | word | the cell of the last blocked step |
| `+1E` | byte | that obstacle's class |
| `+1F` | byte | age |
| `+22`/`+24` | word | the column and row of the last blocked step |
| `+26` | word | the actor last touched |
| `+28` | word | a state timer |
| `+2C`/`+2D` | byte | a second destination (province states) |
| `+31` | byte | state |

- **Allocator `0x5C39`** (`0334:28F9`; type, column, row).
  - It scans for a slot with active = 0. At the first one it checks the type's counter, and fails if that's full: types 0-2 `DS:0x6C1A` (30), 3-4 `0x6C18` (8), 5-7 and 10-12 `0x6C16` (12), 8-9 `0x6C14` (10), 13 `0x6C12` (10); any other type fails.
  - Otherwise it increments the counter and sets the cell, type, destination (the spawn cell), `+2C`/`+2D` = 0, active = 1, x and y = the cell x 16, and the index. The slot is left in `DS:0x6DBD`.
  - It doesn't clear the other fields; `0x5DC7` zeroed them when the slot was freed.
- **Free `0x5DC7`** decrements the same counter, clears `7BB4` bit `0x40` at the cell for types below 11, and zeroes the record.
- **Update `0x23C4C`.** For each slot with active = 1, a negative type is freed; any other calls `[DS:0x134C + type x 4]`.

### 20.3 Movement (`0x254DF`)

The routine's argument is a 256-byte walkability table, indexed by tile id:

- **`3496:1E04`** passes road tiles `0x36`-`0x43` and the crossing tiles `0x5E`-`0x61` and `0x82`-`0x89`.
- **`3496:1F08`** also passes open ground `0x1D`-`0x2D` and rubble (`0xA2`, `0xA3`, `0xA7`, `0xAA`, `0xAD`, `0xB0`).
- **Blocking values:**
  - 1: no class;
  - 2: housing `0xC8`-`0xD7`;
  - 3: other buildings, `0xD8` and up;
  - 4: wall pieces `0x92`-`0x95` and `0x9E`-`0xA1`;
  - 5: `0x2E`-`0x35` and `0x44`-`0x49`, terrain the walkers clear to `0x1D` (probably trees; unconfirmed).
- **The obstacle class** is 1 << (value - 2), set by `0x26A8F`.

Each call moves the walker one pixel:

1. **Between cells.** It clears status bit 4 and counts down `+0F`. While that stays above 0, the walker steps a pixel along its facing (`0x26CEE`) and returns.
2. **At a cell.** When `+0F` reaches 0 it sets bit 4, clears the class and reloads 16. It releases `7BB4` bit `0x40`, recomputes the cell, and takes the direct facing toward the destination (`0x26B3C`, where 8 means arrived).
3. **Arrived.** `0x26BB0` snaps x and y to the cell, sets bit `0x40`, clears `+0F`, `+10`, `+11`, `+0C`, the status and the class, and sets status 1.
4. **Following an obstacle.** With `+0B` = 1 the walker aims at the follow target instead, and stops following once that facing matches the direct one.
5. **Trying a step.** Not following, it tries the direct facing with `0x25909`:
   - a next cell whose table value is 0 passes;
   - anything else fails and records the obstacle (`+22`/`+24`, `+0D`/`+0E`, `+1A`, `+1E`);
   - a step off the map fails without recording anything.
6. **Turning.** On a failure it starts following, with the destination as the target, and turns one facing at a time in its hand's direction, up to 8 times, until a step passes. Every failed try overwrites the follow target with its obstacle, so the next cell turns from there: the walker keeps the obstacle on one hand. If no step passes, `0x26BB0` stops it.
7. **Stepping.** It sets the facing, re-marks the cell (status bit 2 if bit `0x40` was already set there) and moves a pixel.
   - Within 45 degrees of the direct facing, following ends.
   - Otherwise `+10` counts down. At 0, `+11` grows by 2 (a signed byte with no cap), `+10` reloads from it, the hand flips and following ends.

`0x26059` is the province-map version: a 40-wide map, the layer at `3496:2754`, occupancy bit `0x80`, and `0x26C4F` in place of `0x26BB0`.

### 20.4 Types (`DS:0x134C`)

| Type | Handler | Walking frames (`MOREMEN.PL8`) | Ages every | Freed at age | What it is |
|---|---|---|---|---|---|
| 0 | `0x23CA1` | 0-11 | 16 ticks | 10 | forum citizen |
| 1 | `0x23CFE` | 12-23 | 16 | 20 | forum citizen |
| 2 | `0x23D5C` | 24-35 | 16 | 40 | forum citizen |
| 3 | `0x23DBA` | -- | -- | -- | (returns at once) |
| 4 | `0x23DBB` | 36-47 | 16 | 50 | barracks patrol |
| 5-7 | `0x23E19`, `0x23E77`, `0x23ED5` | 48-59, 60-71, 72-83 | 64 | 120 | invaders |
| 8 | `0x23F33` | 0-11 | 16 | 30 | workshop trader |
| 9 | `0x23FC5` | -- | -- | -- | (returns at once) |
| 10 | `0x23FC6` | 0-11 | 16 | 20 | rioter |
| 11 | `0x24023` | `DS:0x6BD8` | 64 | 120 | province army (probably hostile) |
| 12 | `0x24089` | `0x2C`-`0x2F` by facing | -- | -- | becomes type 11 on status bit 8 |
| 13 | `0x24133` | `+2A` x 4 + (32-period counter >> 1 & 3) | -- | -- | the player's army on the province map |

Every city type's handler does the same three things:

1. **Runs its state.**
2. **Sets the walking frame** (`0x25196`).
   - The base comes from the table, plus a facing set x 3: facings 0, 1 and 7 add 3; 2 adds 9; 3-5 add 0; 6 adds 6.
   - Then a stride from `+0F & 6`: 0 or 4 adds 1, 2 adds 0, 6 adds 2.
3. **Ages the walker.** When the period's counter is 0, age goes up by 1, and at the limit the state becomes 2.
   - Type 8 at its limit also lowers its workshop's sales, `+0E`, to no less than -2.
   - Steps 2 and 3 run even when the state has just freed the record, so an empty slot can carry a frame and an age of 1.

### 20.5 States (`DS:0x1384`)

| State | Handler | Table | Behaviour |
|---|---|---|---|
| 0 | `0x2417B` | -- | nothing |
| 1 | `0x2417C` | roads | skips odd ticks of the 32-period counter when the cell is shared; walks; at each cell inside the one-cell margin, C9D4 `|= 0x12` over the 3x3 |
| 2 | `0x242EF` | -- | free |
| 3 | `0x242FA` | ground | walks; when stopped, state 2; at a cell, by obstacle class: 1 or 2 demolish (`0x124F8`, direction 8), 4 breach a wall (`0x243B5`), 8 tile `0x1D` |
| 4 | `0x24563` | roads | skips odd ticks when the cell is **not** shared; walks; at a cell with C9D4 `0x08`, state 2 and the workshop's sales `+0E` + 1 (up to 2) |
| 5 | `0x245FF` | ground | walks; when stopped, state 6, timer 0, destination += `3496:1DF4`[random `0288` & 7] clamped to 0-99; at a cell, class 1 or 2 demolish, class 8 tile `0x1D` |
| 6 | `0x2478B` | -- | frame `0x2A`/`0x2B` (the type handler overwrites it); timer + 1, and past 8 state 5 with the path cleared |
| 7 | `0x247DA` | roads | walks; at each cell inside the margin, land value - 2 over the 3x3; every fifth cell looks for a hostile (types 5, 6, 7, 10) within 160 px (`0x2520E`), then state 8 |
| 8 | `0x24A6B` | ground | the target gone or not hostile: look again, or state 2; destination = the target's cell; walks; at a cell, any hostile within 16 px (`0x26D9D`) goes to state 2 |
| 9 | `0x24B66` | province | walks; arriving on province tile `0x4A` frees it, launches invaders (`0x2D891`) and lowers `DS:0x6C3C`; otherwise state 15. Random events on the way change tiles `0x4C`/`0x79`/`0x7A` and post messages |
| 10, 13 | `0x24ECE`, `0x2514C` | province | walks |
| 11 | `0x24EF4` | province | walks, swapping the destination with `+2C`/`+2D` on arrival; a type 11 within 64 px starts state 12 |
| 12 | `0x24FF7` | province | the battle with that type 11 (`0x26EF1`, `0x22116`) |
| 14 | `0x25172` | -- | frame = `+2A` x 4 |
| 15 | `0x24EA4` | -- | timer + 1, and past 16 free |

- **The wall breach (`0x243B5`)** only happens when the random walk `0286` is at most 6:
  - `0x92`, `0x94` and `0xA0` become `0xA2`, unless a `0x9A`-`0x9F` piece is left or right of them; then only when the walk is at most 1.
  - `0x93`, `0x95` and `0xA1` become `0xA3`, with the same test above and below.
  - `0x9E` and `0x9F` would need a walk below 1, which never happens.

### 20.6 Spawners

- **Forums, `0x2D2F5`** (steps 0 and 50).
  - Record at `DS:0x5BA4`, 16 bytes: +0 column, +2 row, +4 grade, +6 timer, +8 active, +A facing.
  - Each active forum's timer counts down. Below 0 it reloads from `3496:1878` by grade (7, 6, 5, 5, 4, 3, 2, 2), and the facing advances.
  - If the population units `DS:0x6C10` exceed 10, it looks for a road around the footprint: 2x2 for grades 0-2, 3x3 for 3-6, 4x4 for 7.
  - It spawns type 0 (random & 15 below 6), 1 (up to 12) or 2, aimed at the map-edge point for its facing, in state 1. The edge points (`3496:1A10`) are (50,0), (99,0), (99,50), (99,99), (50,99), (0,99), (0,50), (0,0).
- **Workshops, `0x2CE7C`** (step 25 + record).
  - Record at `DS:0x585C`, 24 bytes: +0 column, +2 row, +4 goods, +6 timer, +8 active, +A facing, +C population nearby, +E sales, +10 level, +12 last level, +14 industry.
  - First `0x2D19A` sums the population units of housing in the 9x9 window from (column - 3, row - 3), clipped to the map, and sets industry to 2 if heavy industry `0xF3` is in it.
  - **The level** is the sum of:
    - `3496:1880`[`DS:0x6CA6` x 8 + goods];
    - population / 16 - 4;
    - `DS:0x6BF4`, sales and industry;
    - a band of `DS:0x6BFC`: below -30 +2, below -10 +1, up to 0 nothing, then -1 to -8 at 1, 3, 5, 8, 12, 16, 20 and 30, up to 199;
    - -1 each for more than 1, 3 and 5 workshops of the same goods.

    It is clamped to 0-7 and stored, with the old level in +12.
  - When the timer passes 0 (reloading 4, so every fifth visit) the facing advances. With population units above 10, a trader (type 8, state 4) leaves from a road around the 3x3.
- **Barracks, `0x2CD1C`** (step 75 + record).
  - Record at `DS:0x5B2C`, 12 bytes: +0 column, +2 row, +4 timer, +6 active, +8 facing.
  - The timer reloads 6, so every seventh visit a patrol (type 4, state 7) leaves.
- **The road search** (`0x2D4BF`/`0x2D558`/`0x2D5F1`).
  - Rings of 12, 16 or 20 cells (tables `3496:1A20`, `1A90`, `1B10`), clockwise from the outer top-left corner, listed with a wrap so any start index works.
  - It starts at random & 15, and takes the first cell `0x2D68A` accepts.
  - That test accepts `0x36`-`0x43` and `0x5E`-`0x61`. Its `0x82`-`0x89` clause compares a sign-extended byte and never passes.
- **Collapsed houses, `0x2DB49`.** After the house becomes `0xA7`, a rioter (type 10) spawns on the cell, facing south with the cell below as its destination, in state 5. Then, only if the spawn succeeded: sound 5, a message, `DS:0x6C3C` -= 2 (not below 0), and `DS:0x6C84` = 2.
- **Invaders, `0x2D891`** (from state 9): type `DS:0x6BDA` + 5, state 3, aimed at (`DS:0x6DAF`, `DS:0x6DAD`).
- **Province actors.** The 18-month routine `0x2D6F4` places types 11 and 12 on province tiles `0x51` + r and `0x59` + r, in state 9. `0x1548A` places the player's army, type 13, in state 10.

### 20.7 In Gaius, and what's open

- **`systems::actors`** has the allocator, the release routine, the update, city states 1-8 and 15, the three spawners and the rioter spawn. `systems::month::run_step` on a whole save calls them in the engine's order. `gaius_viewer` steps the month so walkers move.
- **Not transcribed:**
  - the province types 11-13 and states 9-14, which move on the province map that `CityState` doesn't hold;
  - sounds and messages.
- **Tests** (`test_actors_*`): a road walk, a corner, spawn limits, the forum and workshop spawns with the level formula, a soldier removing a rioter, and a rioter demolishing a house. On the four real saves, three months of steps keep every type counter in step with its walkers and keep road walkers on roads.
- **Not yet checked against the engine.** Nothing in the four saves pins a walker's path; two saves a few steps apart would.
- **Open:**
  - what `DS:0x6CA6`, `DS:0x6BFC`, `DS:0x6C3C` and `DS:0x6C84` mean to the player;
  - what sends an invasion;
  - the province states.

## 21. Fire, collapse and road wear (2026-09-13)

### 21.1 The scan counters and the month's events

Every handler the service scan (`0x2BBBB`) reaches for a counted tile first bumps a counter. The dispatcher zeroes the counters at step 102 and publishes them at step 105:

| Counter | Tiles | Published as |
|---|---|---|
| `DS:0x6E0C` | road pieces `0x36`-`0x40` | `DS:0x6BF0` |
| `DS:0x6E0A` | building cells `0xC8`-`0xE8`, `0xEA`-`0xF3`, `0xF5`-`0xF6` | `DS:0x6BF2` |
| `DS:0x6E08` | markets `0xF4` | `DS:0x6BEE` = count / 4 |
| `DS:0x6E06` | heavy industry `0xF3` | `DS:0x6BEA` = count / 16 |
| `DS:0x6E04` | schools and hospitals `0xEC`/`0xED` | `DS:0x6BEC` = count / 4 |

- **They count cells, not buildings.** A 4x4 heavy industry adds 16, which is why the published words divide.
- **Checked against the saves.** One reset and scan over each real save reproduces all five published words, in all four saves (`test_save_corpus_simulation`). In `CAESARVX.SAV` that's 141 road pieces, 251 building cells, 3 markets, 2 heavy industries and 2 schools or hospitals.
- **The events.** Straight after the counter, the handler compares it with the month's targets. A handler that runs an event skips its usual effect.
  - **Road wear.** A road piece whose count equals `DS:0x6D02` runs `0x2C4EA`: tile `0x1D`, `7BB4` 0, message `0x27CBA`, target -1.
  - **Collapse.** A building cell whose count equals `DS:0x6D00` runs `0x2C525`: `0x124F8`(column, row, 8) demolishes the building, message `0x27D58`, target -1.
  - **Fire.** Otherwise, a count equal to `DS:0x6CFE` runs `0x2C54E`: `0x126BA`(column, row, 8) sets the building on fire, message `0x27D09`, target -1.
  - Markets are counted but never collapse or burn. Each message has its own cooldown of 5 (`DS:0x6C66`, `0x6C68`, `0x6C6A`).
- **The roll, `0x2DF7D`**, is the last routine of step 105.
  1. It sets the three targets and `DS:0x6C88` to -1.
  2. Then, four times, it draws. If the walk `0286` exceeds a threshold and the count is nonzero, it takes the draw `028C` and shifts it right (arithmetic) up to 16 times until it's no more than the count; that value is the target.
  3. The four rolls:
     - road wear: threshold `DS:0x6BE0`, count `0x6BF0`;
     - collapse: threshold `0x6BE2`, count `0x6BF2`;
     - fire: threshold `0x6BE4`, count `0x6BF2`;
     - the fourth: threshold `0x6BDE`, count `0x6C8A`, result in `0x6C88`.
  4. The first three thresholds are saved global words. What sets them, and what reads `0x6C88`, isn't traced.

### 21.2 Demolition, fire and burning tiles

- **`0x124F8` and `0x126BA` have the same shape.**
  1. Step one cell in the direction argument (facings 0-7 clockwise from north; 8 stays), clamped at 0.
  2. Walk to the building's anchor and remove its forum, workshop or barracks record.
  3. Draw once per footprint cell, and set the cell to base + 3 x (walk & 3), with `7BB4` zeroed.

  `0x124F8` uses base `0xA7`, giving rubble `0xA7`, `0xAA`, `0xAD` or `0xB0`. `0x126BA` uses base `0xA8`, giving fire `0xA8`, `0xAB`, `0xAE` or `0xB1`, and plays sound 3.
- **A correction to 18.3.** Section 18.3 read the rubble as `0xA7 + (walk & 3)`, because the lifter used then hid `imul dx`. Gaius's Clear Area had written `0xA7`-`0xAA`; it's fixed.
- **Burning tiles, `0x29624`,** are reached from the housing pass through the jump table at `0x295F8`.
  - **Burning out.** If `028A` (the previous draw's low 7 bits) exceeds 90, it adds 32 to `028A` (mod 128) and decrements the tile, so the fire drops to the rubble tile below it.
  - **Spreading.** Otherwise, unless the cell is on the map's outer rows or columns, it adds 32 to `028A` and picks the neighbour `3496:1DE4`[walk & 7] (north, then clockwise). If that holds a building (`0xC8` and up), `0x126BA`(column, row, direction) sets it on fire.
- **Rubble and fire tiles** (`0xA2`, `0xA3`, `0xA7`-`0xB2`) get the negative-coverage handler (section 15), and invaders and rioters can walk over the rubble (section 20.3).

### 21.3 In Gaius

- **`service::ServiceState`** holds the counters, the targets and an `on_event` callback; `dispatch_tile` bumps the counters and runs due events.
- **`construction::demolish` and `burn`** come in grid and whole-save versions, each with a direction.
- **`housing::DevelopmentContext`** carries `random` and `spread_fire`, which drive the burning tiles.
- **`systems::month`** publishes the counts and rolls the targets at step 105, and wires the events to `construction`.
- **Tests:** `test_service_events`, `test_month_event_roll`, and the scan counts in `test_save_corpus_simulation`.
- **Not modeled:** the messages, and the fourth roll's consumer.

## 22. The save's remaining tables (2026-09-13)

The five save blocks still carried as opaque bytes are identified from the code that writes them.

### 22.1 `table_10`: population milestones

- **What sets the flags.** `0x27BA1` runs at step 80 (through `0x2CD10`). The first time `DS:0x6C0E` (population, 4 × units) exceeds 200, 1000, 2000, 4000, 8000, 12000, 16000 or 20000, it sets that milestone's flag, `DS:0x580C` + 0 to 7.
- **What else it does.** It posts a message (`0x27ACE`, strings from `DS:0x6EA6`-`0x6EC4`). Bytes 8 and 9 are unused.
- **The saves agree.** Flag 0 (200 people) is set in the three saves above 200 people, and in none of them is flag 1.

### 22.2 `table_50` and the current province

- **Picking a province.** `0x2898E` (reached from `0x290C1`) draws random numbers until `walk >> 1`, an index from 0 to 49, has a clear flag in `DS:0x581E`, and stores it in `DS:0x6CA4`.
- **Making it current.** `0x289B0` (from `0x292D3`) copies it to `DS:0x6CA6` and sets its flag.
- **Listing them.** `0xD229` walks a list at `3496:172C`, shows each flagged entry, and highlights the current one.
- **In the saves.** All four have index 47 current and flag 47 set.
- **What the index is.** That it is the province is a strong inference, from the ~50 province names in the executable. The list routine's strings would confirm it.
- **A correction to 20.6.** The workshop base table `3496:1880` is 50 x 8 (it ends exactly where the edge-point table `3496:1A10` starts): one row of eight goods per province. `systems::actors` had embedded only the first 64 bytes, so with province 47 every workshop's base read 0. Fixed.

### 22.3 The yearly histories

The calendar (`0x29472`) calls the yearly routine `0x28238` when the year turns. That routine:

1. divides `DS:0x6C00` and `0x6BFE` by 12;
2. calls `0334:6CB1`, `0x282A1`, `0x283D3` and `0x284AA` (not read);
3. zeroes `0x6C00` and `0x6BFE`, and copies `0x6C56` to `0x6C54`;
4. writes the five histories below;
5. runs `0x289C0`, which moves `0x6C4E`, `0x6C4A` and `0x6C52` into `0x6C4C`, `0x6C48` and `0x6C50` and nudges `0x6C4A` toward a population-based figure;
6. calls `0x28C43`, `0x29023` and `0x2933B`.

Each history is a circular buffer of (year - 1, value) word pairs. Its index word is advanced before each write, so record 0 stays empty until the buffer wraps:

| Block | Writer | Records | Index | Value |
|---|---|---|---|---|
| `table_60_a` (`3496:01F0`) | `0x28853` | 15 | `DS:0x6B36` | `DS:0x6BC6` |
| `table_60_b` (`3496:022C`) | `0x28892` | 15 | `DS:0x6B34` | `DS:0x6BC4` |
| `table_60_c` (`3496:0268`) | `0x288D1` | 15 | `DS:0x6B32` | the treasury `DS:0x6CA2` |
| `table_60_d` (`3496:02A4`) | `0x28910` | 15 | `DS:0x6B30` | population units `DS:0x6C10` |
| `table_72` (`3496:02E0`) | `0x2894F` | 17 of 18 | `DS:0x6B38` | `DS:0x6BB6` |

- **In the saves.** `CAESARUX.SAV`'s index 12 holds year -2 in every buffer, with population units 146, treasury 3144, `0x6BC6` 71, `0x6BC4` 76 and `0x6BB6` -81.
- **Not established.** What `0x6BC6` and `0x6BC4` (both 0-100) and `0x6BB6` (negative) mean to the player. They are probably ratings and a balance, the data behind the Forum's history graphs.
- **In Gaius.** `systems::month` writes the five records when the year turns. The routines before them aren't transcribed, so a value one of those would change is recorded as it stood.

## 23. The second save session (2026-09-14)

Seven more saves of the same scenario (province 20, `EMPIRE2.047`), from a new city. In play order, with what each holds:

| Save | Month, year | Treasury | Population units | Reservoirs / working fountains | Highest housing grade |
|---|---|---|---|---|---|
| `CAESARXW.SAV` | 4, -8 | 6054 | 49 | 2 / 2 | `0xCC` |
| `CAESARXV.SAV` | 1, -1 | 2142 | 140 | 4 / 4 | `0xD0` |
| `CAESARXU.SAV` | 9, 0 | 992 | 301 | 4 / 4 | `0xD0` |
| `CAESARXT.SAV` | 7, 4 | 186 | 415 | 4 / 4 | `0xD2` |
| `CAESARXS.SAV` | 7, 7 | 96 | 505 | 4 / 4 | `0xD2` |
| `CAESARXR.SAV` | 10, 8 | 13 | 509 | 4 / 4 | `0xD1` |
| `CAESARXQ.SAV` | 6, 14 | 300 | 382 | 4 / 4 | `0xCB` |

No two are less than a year apart, so the month-apart check of `run_month` is still open. What they do show:

- **Round trip and economy.** All seven load and re-serialize byte-identical, and step 101's economy reproduces all five outputs in each.
- **Service and water.** Coverage, the six service bits, fountain tiles and fountain levels match cell for cell in five saves outright, which between them hold every housing grade up to `0xD2`, reservoirs and working fountains (section 19.2).
- **A save taken between steps 101 and 102.** `CAESARXS.SAV` has zero coverage everywhere and no service bits except water, while its population count matches the tiles exactly. That is the state after step 100's reset and step 101's population and water pass, before step 102's first scan; reset plus water alone reproduces it cell for cell. (The step counter isn't saved, section 17.)
- **A house changed after the scan.** `CAESARXW.SAV` misses 43 coverage cells around the houses south of the row-38 road and has one more building cell than the published count; its population is one unit off. That's a house that developed after the month's scans (STRONG INFERENCE: one save can't show which house).
- **Year 0 exists.** The year word `DS:0x6C32` runs -8, -1, 0, 4, ... 14, so it's a plain signed counter, not a BC/AD year with no zero.
- **`CAESARXQ.SAV` has rubble** (nine cells of `0xA7`/`0xAA`/`0xAD`/`0xB0`, section 21), and its tiles count 32 population units more than the saved count.
- **`DS:0x6BF8` isn't always 2.** It's -1 in `CAESARXW.SAV`, 2 in `XV`-`XT` and 0 from `XS` on; step 101's economy recomputes it correctly each time.

## 24. A month against the original: six consecutive saves (2026-09-14)

`CAESARUX.SAV` was loaded and played on, pausing to save six times in about 80 seconds of play:

| Save | Month (year -1) | Real time | Step it was saved at |
|---|---|---|---|
| `CAESARUX.SAV` | 6 | earlier session | 1-12 |
| `BAESARUX.SAV` | 7 | 01:33:21 | 76-83 |
| `AAESARUX.SAV` | 8 | 01:33:41 | 57-64 |
| `DAESARUX.SAV` | 9 | 01:33:59 | **4** |
| `EAESARUX.SAV` | 9 | 01:34:12 | 76-83 |
| `FAESARUX.SAV` | 10 | 01:34:26 | **31** |
| `GAESARUX.SAV` | 10 | 01:34:37 | 58-73 |

Nothing was built in between; the treasury stays 3144 throughout.

### 24.1 Finding the step without the step counter

Neither the step counter (`DS:0x6D9D`) nor the generator is saved. But several parts of the save change only on fixed steps and never draw:

- the forum, workshop and barracks records' timers (`table_480`/`720`/`120` +6), which count down as the spawners run -- a forum's timer runs 5, 2, 0, 7, 6, 5, 4 across the saves, and the workshops are served in two groups on different steps;
- the tile grid (housing development, one row per step): (17, 85-86) `0xCC` -> `0xCB` and (24, 74) `0xD0` -> `0xCF` between `UX` and `B`, then (24, 74) `0xCF` -> `0xCB` between `B` and `A`;
- `DS:0x6C00`, which grows by `DS:0x6C04` (6 here) at step 101, and the population words set there.

`tools/month_check` takes two saves, tries every start step for the first, runs `systems::month::run_step` forward and reports where the second save's tiles, those three record tables, month, population words and `DS:0x6C00` are reproduced exactly. Every pair has such windows. Chaining them -- one leg's end step, `(start + steps) % 106`, is the next leg's start -- and requiring land value to match too leaves the positions in the table: `D` is pinned to step 4 and `F` to step 31, the rest to windows of 8-16 steps.

### 24.2 What matches

Along that chain, for all six legs (`test_month_consecutive_saves`), `run_step` reproduces exactly:

- every tile, including the three housing changes above, on the right step;
- all three record tables, byte for byte;
- the month, `DS:0x6C10`/`0x6C0E` and `DS:0x6C00`;
- **all 10000 land-value cells**, where the saves themselves differ in 31-452.

The coverage and service layers match too (they're rebuilt each month and unchanged in these saves), and so does the population arithmetic: `BAESARUX` and `AAESARUX` are 1 and 2 units below their tiles because the house at (24, 74) dropped a grade at step 24, after the count at step 101 of the month before.

### 24.3 What this doesn't test

- **The generator.** The land-value match doesn't depend on the generator's state: starting its shift register from 1, 55, 12345, 40000 or 65000 gives the same exact match. The random ±1 in the growth term evidently never moves these cells (they sit at their clamps), so the random draws' timing is still unchecked.
- **The walkers.** The actor records never match (5-8 of 70 differ): walkers draw from the generator. Checking a walker's path needs a generator state, which saves don't carry.
- **Development under random pressure.** Nothing burned, collapsed or wore out in these months.

Also seen: `7BB4` bit `0x40` moves along the road at column 81 (rows 0-17) between every pair -- the mark a walker leaves on the cell it occupies.

## 25. The treasury: costs, taxes and the yearly accounts (2026-09-14)

The city's money runs through three places: the build routine charges construction, step 101 sums the tax rates, and the yearly routine `0x28238` settles the accounts. `systems::economy` implements all three.

### 25.1 Construction costs

- **The build routine** (`0x11D97`-`0x120BF`) calls a command's handler through `DS:127C` only when its cost is no more than the funds `DS:0x6CA2`. When the handler succeeds (`DS:0x6D0A` = 0) it takes the cost from the funds and adds it to the year's construction `DS:0x6BC0`.
- **The costs** are the 44 words at `3496:1548`, by command id; the Forum's is `3496:15A0`[grade]. They are the manual's prices: Clear Area 1, Road 3, Reservoir/pipe 3, Wall 5, Tower 10, Well 5, Fountain 10, Housing 2, Temple 20, Bath Houses 40, Hospital 60, School 60, Oracle 200, Theater 100, Coliseum 200, Hippodrome 300, Plaza 10, Barracks 80, Prefecture 25, Heavy Industry 300, Market 20, Workshop 50, Fort 500.
- **Road, pipe and wall** (commands 4, 5 and 7, `0x11E12`) charge each cell as the drag lays it and keep a running total; a right-click cancel gives it back (funds `+=`, `DS:0x6BC0` `-=`).
- **Province commands** (ids 36, 37 and 42) have their cost doubled or quadrupled by the province terrain under the cursor (`0x11CD4`-`0x11D86`). Not modeled.
- **Emergency funds, `0x120C0`.** When a placement can't be afforded, the first time in a game (`DS:0x6C9A`) and only up to rank 6 (`DS:0x6C30`), Rome sends 500 Dn with a message. The placement isn't retried.
- **Every other write to the funds:**
  - a new province (`0x57BE`, `0xF7C5`) sets them from `DS:0x6C0C`, then takes 1000 for each rank above 1, up to three times, while they're at least 5000, and above rank 4 another 200 x (rank - 4) while they're at least 4000, not below 4000;
  - the Forum's donation (`0xC26E`) moves `DS:0x6C28` from the governor's savings `DS:0x6C2E` and adds 90 % of it;
  - the toolbar's display routine (`0x212A1`) clamps them to 0-25000 whenever it draws.

### 25.2 The rates, and the loader

The Forum screens change these words with arrow buttons, each with its own limits. The names are the manual's (Treasurer, Military Advisor, Tribune of the Plebs, the Forum's own panel), matched by what each word does below; STRONG INFERENCE.

| Word | Limits | Name |
|---|---|---|
| `DS:0x6C04` | 0-25 (`0xE7AE`) | population tax rate |
| `DS:0x6C02` | 0-25 (`0xE7D2`) | industrial tax rate |
| `DS:0x6C06` | 0-50 (`0xE650`) | conscription rate ("up to fifty percent") |
| `DS:0x6C08` | 0-999 (`0xE637`) | army wages bill |
| `DS:0x6C46` | 0-9999 (`0xEB13`) | pleb welfare expenditure |
| `DS:0x6C2C` | 0-9999 (`0xC164`) | governor's salary, added yearly to the savings `DS:0x6C2E` |

- **Step 101** (`0x28215`) adds `DS:0x6C04` to `DS:0x6C00` and `DS:0x6C02` to `DS:0x6BFE`, so at the year's end each holds twelve months of its rate.
- **The loader** (`0x4612`-`0x56B7`) reads the save in the writer's order, `final_state` included, then sets `DS:0x6BFE` = `DS:0x6C02` x month. `0x6BFE` is the one sum the save doesn't keep; `0x6C00` and `0x6C02` it does, `0x6C02` in `final_state`.

### 25.3 The year's accounts (`0x28238`)

When the calendar turns the year it:

1. divides `DS:0x6C00` and `DS:0x6BFE` by 12, giving the year's average rates;
2. **`0334:6CB1` (flat `0x9FF1`)**: `DS:0x6BE8` = the active workshops' average production level (+0x10 & 7), 0 with none;
3. **`0x282A1`, population tax**:
   - each housing cell (`0xC8`-`0xD7`) whose C9D4 has bit `0x20` adds its tax units, `3496:008E`[tile - `0xC8`] (1, 2, 4, 6, 7, 10, 14, 9, 13, 15, 16, 17, 18, 20, 22, 25 -- the 16 bytes after the population table);
   - a house without the bit sets `DS:0x6C7E`, which the advisor text picker (`0xD007`) reads;
   - `DS:0x6BC6` = units x average rate / 20;
   - tax per head: x = `DS:0x6BC6` x 100 / (`DS:0x6C0E` + 1), `DS:0x6BCA` = x / 100 and `DS:0x6BC8` = x % 100, in denarii and hundredths.

   Bit `0x20` comes from forums and prefectures, the manual's administrative reach where "taxes will be automatically collected". `kC9D4BitTable` called it "religious" after the temple misreading; it's now "administration".
4. **`0x283D3`, industrial tax**:
   - `DS:0x6BC4` = the sum of each active workshop's level x 64, times the average industrial rate, / 25;
   - then the industrial tax pressure `DS:0x6BFC` (the workshop level's band term, section 20.6): with no forum it resets to -50; a rate of 5 leaves it, rates of 6-10 add rate - 5 (plus half again while it is below 3), and rates below 5 or above 10 add twice rate - 5; clamped to -50..24.
5. **`0x284AA`, the settlement**:
   - construction: `DS:0x6BC2` = `DS:0x6BC0`, which restarts at 0;
   - savings += salary; past 25000 they stay at 25000, the salary drops to 0 and a message is posted;
   - operating costs `DS:0x6BBE` = welfare + salary + army wages;
   - funds += both taxes - operating costs, not below 0;
   - the tribute due `DS:0x6BBA` grows by 1, up to 100 (it starts at 50, `0x5968`: the manual's "begins at fifty Denarii, and increases by one"). It is paid from the funds, or all of them if they're short, into `DS:0x6BAA`, and a payment resets the missed count `DS:0x6BB8`;
   - if the funds are still above 500 and the year made a profit (taxes - construction - operating costs), Rome also takes 60 % of that profit;
   - nothing paid: `DS:0x6BB8` + 1, the funds go to 0, and a message (`0x27AF3`) picks its text by the count; at 3 the game ends (`DS:0x6D6A` = `0x3C`);
   - profit or loss `DS:0x6BB6` = taxes - construction - operating costs - tribute paid, copied with the other four figures to `0x6BB4`-`0x6BAC`, the Treasurer's report;
   - welfare is cut to the funds, if it exceeds them;
6. zeroes both sums and copies the pleb count `DS:0x6C56` to last year's `DS:0x6C54`;
7. writes the histories (section 22.3), then runs the Legion (`0x289C0`, section 26), the ratings (`0x28C43`), promotion (`0x29023`) and `0x2933B`; the last three aren't transcribed.

### 25.4 Checked against the saves

- **Every year balances.** In every year of funds (`table_60_c`) and profit (`table_72`) the saves' histories hold -- years -13 to -2 of the first city, -13 to 13 of the second -- the year's change in funds is that year's profit, with two exceptions:
  - year -2 of `CAESARUX`'s city gained 18 Dn outside the accounts. The governor's savings would be 210 by then (`CAESARWX`'s 160 and five more years of salary 10) but are 190 in `CAESARVX`: a donation of 20, 90 % of it to the city;
  - year 3 of `CAESARXW`'s city gained 500: the emergency funds flag `DS:0x6C9A` is 0 in `CAESARXU` and 1 in `CAESARXT`.
- **Each save's last year is reproduced.** `test_economy_year_end_matches_saves` starts from the funds a year earlier, less that year's construction (plus the grant or donation above), and runs `settle_accounts` with the saved taxes, operating costs and tribute due. It lands on the saved funds, tribute paid and profit in all 17 saves, and the saved tax per head matches last year's population tax and population.
- **The taxes are consistent, not proven.** No save holds the city as it stood at a year's end.
  - `CAESARUX`'s 71 Dn of population tax is 284 tax units at an average rate of 5: `CAESARVX` shows the year's sum at 21 after four months, and 21 + 8 x 6 = 69, which is 5 after dividing by 12.
  - `CAESARXQ`'s 53 Dn of industrial tax is 3 levels at 7 %: 3 x 64 x 7 / 25 = 53, with its industrial rate 7.
  - The industrial tax pressure of `CAESARXS`, `CAESARXR` and `CAESARXQ`, all without a forum, is -49, -49 and -47: -50 plus one year at 6 %, 6 % and 7 %.

### 25.5 In Gaius, and what's open

- **`systems::economy`**: `construction_cost`, `can_afford`, `charge`/`refund`, `grant_emergency_funds`, `donate_savings`, `average_workshop_level`, `population_tax`, `industrial_tax`, `industrial_tax_pressure`, `settle_accounts` and `run_year`. `systems::month::run_step` runs `run_year` when the year turns, before the histories, and keeps `DS:0x6BFE` in `SimState`.
- **`gaius_viewer`** charges each placement, asks Rome for the emergency funds when it can't, shows the cost and current funds in the toolbar label (dropping the funds figure if the combined text wouldn't fit the panel), and prints the funds each month and the Treasurer's report each year. **2026-09-14:** a drag-built command (Road, Wall, Plaza, Clear Area) can be cancelled the manual's way -- right button while the left is still held (`platform::CommandType::CancelDrag`) -- which restores every cell the drag touched (a 7x7 box around each placed cell, wide enough to cover Clear Area's worst case of wrecking a building anchored up to 3 cells away) and refunds what it cost.
- **Not modeled:**
  - the messages;
  - the province commands' terrain multiplier;
  - the yearly routine's ratings, promotion and `0x2933B` (the Legion is section 26);
  - what pleb welfare does at step 105 (`0x2DF01`, `0x2DF40`).

## 26. The Legion: recruitment and Cohorts (2026-09-14)

After the histories the yearly routine `0x28238` calls `0x289C0`, which recruits the province's Legion and falls through (`0x28A8B`) into `0x28A8F`, which shares it among the Cohorts. `systems::military` implements both.

### 26.1 The words

The Military Advisor's screen (`0x0B1B7`-`0x0B436`) draws three pairs of figures, which the manual calls "the total number of regular, irregular and auxiliary Centuries in your entire Legion", with "the amounts you had last year" in brackets. The names follow the arithmetic below; STRONG INFERENCE.

| Word | Last year | Name | Set by |
|---|---|---|---|
| `DS:0x6C4E` | `DS:0x6C4C` | regular Centuries | the army wages `DS:0x6C08` |
| `DS:0x6C4A` | `DS:0x6C48` | irregular Centuries | population and the conscription rate `DS:0x6C06` |
| `DS:0x6C52` | `DS:0x6C50` | auxiliary Centuries | plebs on army duty `DS:0x6C5A` / 16 (`0x0E85A`, `0x2DDFC`) |

A new province starts with 2 regular Centuries and nothing else (`0x058C0`-`0x058DE`, and the Prima Cohors' +0x20 = 2 at `0x062E3`). The manual says one; all 17 saves have two.

### 26.2 Recruiting (`0x289C0`)

1. Last year's figures: `0x6C4C` = `0x6C4E`, `0x6C48` = `0x6C4A`, `0x6C50` = `0x6C52`.
2. The irregulars the city supplies: population units `DS:0x6C10` x (conscription x 4) / 10000, a long multiply (`0:0x3C1`) and signed long divide (`0:0x3DB`). Population is 4 per unit, so this is the population times the rate in hundreds of men.
3. Irregulars one Century up while below that, **four** down while above it.
4. Regulars one up if (regulars + 1) x 8 is no more than the wages, one down if regulars x 8 is more: 8 Dn a Century a year.
5. Neither below 0. The auxiliaries aren't changed here.

### 26.3 Assigning Centuries to Cohorts (`0x28A8F`)

- A Cohort is an active actor of type 13. The Prima Cohors is placed by `0x0621F`-`0x062E3` on the first province cell (`3496:2754`, 40 x 40) holding tile `0x4A`, most likely the starting fort, a new fort's Cohort by `0x1560A`-`0x15676`; both start in state 10 with morale 5 (+0x2B).
- A Cohort's Centuries are bytes in its record: regulars +0x20, irregulars +0x21, auxiliaries +0x1D. Battle casualties take one from the record and one from the Legion's word together (`0x22C16`-`0x22C87`), and the fort transfer screen moves them the same way (`0x23272`-`0x232E9`).
- The Military Advisor's centre button (`0x0E5D4`) switches the Cohort on display (`DS:0x6C0A`) between state 10 and state 14: the manual's mobilized and demobilized.
- The routine counts the mobilized Cohorts (type 13, state not 14), at least 1, and divides each pool by that count. For each Cohort in table order:
  - demobilized: all three fields become 0;
  - otherwise, per pool: a field at or above the quotient becomes the quotient, plus 1 while the remainder lasts (each use takes one from it); a field below the quotient gains 1.
- So men join a Cohort one Century of each kind a year, as the manual says, and leave one that has too many at once. The Legion's words aren't changed: Centuries not yet in a Cohort are the manual's men "en route to join a unit".

### 26.4 Checked against the saves

- **Recruiting.** Every save keeps last year's Legion beside this year's, and the population units at the year's end are the newest record of `table_60_d` (section 22.3). Recruiting from last year's regulars and irregulars with those units and the saved wages and conscription gives the saved regulars and irregulars in all 17 saves. `CAESARXT`-`XQ` exercise the rule: wages 15 dropped the city's regulars from 2 to 1, and irregulars rose to 2 at 496 units and 12 % (`CAESARXR`) and fell back to 1 by `CAESARXQ`.
- **Assigning.** Every save has one Cohort, holding the whole Legion; assigning the saved Legion leaves every actor record unchanged.
- `test_military_year_matches_saves`.

### 26.5 In Gaius, and what's open

- **`systems::military`**: `regulars_after_year`, `irregulars_target`, `irregulars_after_year`, `assign_centuries` and `run_year`. `systems::month::run_step` runs `run_year` when the year turns, after the histories.
- **Not modeled:**
  - the auxiliaries' source, the pleb routines (`0x2DD45`-`0x2DE0E` at step 105 and the Tribune's screen `0x0E85A`);
  - what conscription and wages do to the city besides (the manual: "your citizens don't like being drafted");
  - battles. Their code is located: `0x22000`-`0x23600`, with morale changes at `0x225B7`-`0x226F7`, the tactic comparisons at `0x22A02`-`0x22A4A` and casualties at `0x22C16`. It uses the "Cohort ?"/"Retreat ?" texts `DS:0x4599`/`0x45B4` (table at `0x27851`).
- **The `cohort` argument.** `CAESAR.BAT` runs `csr.exe cohort` after the external Cohort 2. `0x0F687` tests whether `cohort.exe` exists (`DS:0x6CF8`, which decides whether the battle screen offers it), and `0x03344`/`0x03388` write and read `cohort.csr`, 14 bytes, the battle's hand-over. With any argument (argc > 1, `0x0F6BD`) the start-up skips the title music and loads the province screen's sheets (`0x0FBBE`: `p_blocks.pl8`, `pointers.pl8`, `font1.pl8`) before running the game. So the argument most likely only resumes a game after Cohort 2 has fought the battle, and the internal battle screen runs from the province map itself (STRONG INFERENCE: the argv string isn't compared anywhere found, and the path from `0x0F6C7` into the resumed game isn't traced).
