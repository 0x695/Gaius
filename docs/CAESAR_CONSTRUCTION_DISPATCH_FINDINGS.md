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
- The event routines behind the per-scan counter thresholds (`0x2C525`, `0x2C54E`, `0x2C4EA`), and where the thresholds come from. **The routines are read in section 21**; the plebs set the thresholds (section 29).
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
  4. The first three thresholds are saved global words; the pleb routines set all four (section 29), and `0x6C88` is the province road the monthly pass wears away (section 28.6).

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
- **Province commands** have their cost, and the charge, shifted left by the terrain under the cursor while the province map is shown (`DS:0x6CAE` = 1, `0x11CD4`-`0x11D92`): by 2 on tiles `0x25`-`0x2C`, by 1 on `0x2D`-`0x35`, not at all elsewhere or for the Fort -- the manual's "15 Denarii to 60 Denarii to clear". `economy::province_cost_shift`.
- **Plebs.** With fewer than 50 pleb groups (`DS:0x6C56`) the build routine refuses every command but the Cohort orders 31-34, with a message (`0x11C72`). `economy::enough_plebs`.
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
   - a house without the bit sets `DS:0x6C7E`, which the ratings advice picker (`0x0CF12`, section 36.4) reads;
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
7. writes the histories (section 22.3), then runs the Legion (`0x289C0`, section 26), the ratings (`0x28C43`), promotion (`0x29023`) and the yearly notice `0x2933B` (section 30).

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

## 27. The battle screen (2026-09-14)

When a Cohort attacking an army (province state 12, `0x24FF7`) comes within 16 pixels of it in both directions (`0x26EF1`, which keeps the army's slot in the Cohort's +0x26), `0x25148` runs the battle screen `0x22116`. `systems::battle` implements the rounds and their outcomes; the screen itself (sheets, texts, the Cohort 2 hand-over `0x2357E`/`0x23493` when `cohort.exe` exists) is not modeled.

### 27.1 The barbarians

- **Race by province.** `0x22146` sets `DS:0x6BD6` = `3496:175E`[province `DS:0x6CA6`], one byte for each of the 50 provinces.
- **Race rows.** `3496:1790`[race x 8] gives, in order, the banner sprite `DS:0x6BD8` (40-43), the city invader type less 5 `DS:0x6BDA` (0-2, used by `0x2D891`), and the strength against each tactic: `DS:0x6BD4` Assault, `0x6BD2` Flank, `0x6BD0` Charge, `0x6BCE` Tortoise. The last two bytes of each row are 0.
- **Names.** `DS:0x2BAD`, 16 characters each, drawn by `0x2268A` as race x 16: Carthaginians, Mauri, Blemmyes, Sassanids, Huns, Ostrogoths, Visigoths, Alamanni, Saxons, Picts, Celts, Celtiberians, Helvetii, Ligurians, Illyrians, Volcae -- the manual's "sixteen types of barbarians".
- **Checked.** All 17 saves hold the race words the tables give for their province: 47 (Caeariensis) the Mauri, 20 (W. Britannia) the Picts.
- **The army.** A barbarian army is a province actor of type 11 (marching) or 12 (becoming 11 once blocked, `0x240A2`); its size is +0x30, 1-8 when placed.

### 27.2 A round

The screen's buttons (`0x2250F`, by the click's x) run Tortoise `0x2308E`, Assault `0x231E2`, Flank `0x23212`, Charge `0x23242` or Retreat `0x230BE`. Each tactic computes the barbarians' figure and calls `0x229A3`:

- barbarians = the race's strength against the tactic x 2 + the army's size + (`2EF9:0286` & 7);
- Romans = regulars x 3 + irregulars x 2 + auxiliaries / 2 + (`2EF9:0288` & 3), with morale 0-1 costing 4, 2-3 costing 2, 6-7 adding 1 and 8-9 adding 2;
- both are shifted right by 2 (arithmetic);
- **equal:** a text, nothing else;
- **Romans stronger:** if `0286` is odd, morale +1; the army's size loses the difference; at 0 or below, victory;
- **barbarians stronger:** if `0286` is odd, morale -1; the Cohort loses one Century of each kind it still has -- auxiliaries, irregulars, regulars -- and the Legion's words `DS:0x6C52`/`0x6C4A`/`0x6C4E` with them; with none left, defeat.

After the routine, morale is clamped to 0-9 (`0x225AD`). The difference in strength decides whether a side loses, but not how much the Cohort loses: at most three Centuries a round.

### 27.3 The outcomes

- **Victory, `0x22D5D`:** morale +2. With no patrol (+0x2C = 0) the Cohort stops where it stands (state 10, destination = position / 16); otherwise it resumes its patrol (state 11, destination +0x22/+0x24). The army is freed (`0x05DC7`).
- **Defeat, `0x22EDB`:** morale -3; all three Century counts 0; state 10; the province cell it stood on loses bit 0x80; the standard returns to the fort at +0x2E/+0x2F (position x 16, destination the same, +0x0F = 0). The army stays.
- **Retreat, `0x230BE`,** after "Retreat ?" is confirmed (`0x23708`): morale -2, and the Cohort stops where it stands.

### 27.4 In Gaius, and what's open

- **`systems::battle`**: `kRaces`, `kProvinceRace`, `load_race`, `tactic_strength`, `fight_round`, `win`, `lose`, `retreat`. The two random words are the caller's: between clicks the screen draws once a frame, which no save pins. `test_battle_rounds`, `test_battle_race_matches_saves`.
- **Not modeled:** the screen and its texts; the Cohort 2 hand-over.

## 28. The province map's actors (2026-09-14)

Barbarian armies and the player's Cohorts live in the same 70-record actor table as the city's walkers and run in the same per-frame loop `0x23C4C`, through the type table `DS:0x134C` (types 11-13) and the state table `DS:0x1384` (states 9-15). They move on the province map: the save's embedded EMPIRE2 block (`3496:2754`, 40 x 40). `systems::province` implements them; `systems::actors::update` hands types 11-13 over in their slot's turn.

### 28.1 Types and states

| Type | Handler | What it does |
|---|---|---|
| 11 barbarian army | `0x24023` | runs its state; frame = the race's banner `DS:0x6BD8`; while the 64-frame counter `DS:0x6D3C` is 0 it ages (+0x1F), and at 120 goes to state 2 |
| 12 army by sea | `0x24089` | runs its state; frame 0x2C-0x2F by facing; once a step is blocked (status bit 8) it becomes type 11 |
| 13 Cohort | `0x24133` | frame = its number (+0x2A) x 4 + (`DS:0x6D3E` / 2 & 3), the waving flag; runs its state |

| State | Handler | |
|---|---|---|
| 2 | `0x242EF` | freed (`0x05DC7`) |
| 9 | `0x24B66` | march (table `3496:2098` for type 12, else `3496:200C`). Stopped on the city tile `0x4A`: `DS:0x6DA7` = low7 & 7, the army is freed, `0x2D891` sends invaders into the city, and the Peace rating `DS:0x6C3C` drops by 4 at rank 1 or 10 above (not below 0), `DS:0x6C84` = 1. Stopped elsewhere: state 15. Blocked (bit 8) with walk < 0x14: by the obstacle's class (+0x1E, via the table at `0x24E88`), class 1-2 with low7 = 0x21, class 4 with low7 11-13, class 8 or 0x20 with low7 21-31, class 0x40 with low7 25-31 turn the blocked cell (+0x1A) into `0x1D`; class 0x10 pillages a town one grade (`0x79` -> `0x61`, `0x7A` -> `0x79`, `0x4C` -> `0x7A`) |
| 10 | `0x24ECE` | walk to the destination; stopped, `0x25484` |
| 11 | `0x24EF4` | walk; stopped, swap the destination with +0x2C/+0x2D (the patrol's other end); at a cell centre, an army (type 11) within 64 px (`0x2533E`) becomes the target (+0x14), the destination is kept in +0x22/+0x24 and the state becomes 12 |
| 12 | `0x24FF7` | the target not an active type 11: with no patrol (+0x2C = 0) state 10 where it stands, else state 11 back to +0x22/+0x24. Otherwise the destination is the army's cell; walk; at a centre, an army within 16 px (`0x26EF1`) starts the battle (section 27) |
| 13 | `0x2514C` | as state 10 |
| 14 | `0x25172` | frame = number x 4 |
| 15 | `0x24EA4` | +0x28 counts up; past 16 the army is freed |

States 10-13 are the toolbar's Halt, Patrol, Attack and Go Home, matched by what they do (STRONG INFERENCE; the command handlers aren't read).

### 28.2 Movement (`0x26059`)

The city walker's algorithm (section 20.3) with a 40-wide map, occupancy bit `0x80` and the stop routine `0x26C4F`. Differences:

- at a cell centre the obstacle class +0x1E isn't cleared;
- a successful step moves the first pixel **before** the occupied cell is recomputed, so a walker heading west or north already occupies the next cell; `CAESARXT`'s army at x = 272 facing east with cell 16 is this order;
- turning goes clockwise with hand 0 and anticlockwise otherwise (the city version tests hand 1 separately);
- the step test `0x26454` masks the occupancy bit, lets Cohorts through classes 7 and up, sets status bit 8 and keeps the blocked cell in +0x1A.

Table `3496:200C` blocks the sea (tiles 0-0x1C) and passes grass (`0x1D`-`0x24`), the city (`0x4A`-`0x4B`) and the spawn markers (`0x51`-`0x60`); table `3496:2098` is its complement for ships.

### 28.3 Where armies come from (`0x2D6F4`)

The calendar sets `DS:0x6D97` when the 18-month counter wraps; the next step starts with `0x2D6F4`:

- By the difficulty `DS:0x6CB8` (0, 1, 2 -- an options setting the save doesn't keep) a first year of 0, -6 or -11 and a yearly-from year of 30, 10 or 0, each less the rank `DS:0x6C30` - 1. Before the first year nothing comes; before the second, only in odd years.
- r = low7 & 7. The whole map is scanned row by row: tile `0x51` + r marks a sea landing (type 12), `0x59` + r a land border (type 11), `0x4A` the city. The last marker found wins.
- The army is placed (`0x05C39`) at the marker, heads for the city in state 9 with size (walk & 7) + 1, and a message names the place.

### 28.4 Invaders in the city (`0x2D891`)

- **Target:** `0x2D90F` walks the forum records (`table_480`): each active one's timer (+0x0C) counts down, and the first to fall below 0 is reset to 6 and becomes the target; with none, the map centre (50, 50).
- **Entry:** `0x2D966` tries five cells of `3496:1828`[direction x 10] (the city map's edges and corners for each of the eight directions); the first holding a tile above `0x1C` and below `0x4A` becomes `0x1D`, and an invader of type 5 + `DS:0x6BDA` (the race's) is placed there in state 3 heading for the target (section 20).

### 28.5 Checked against the saves

- In 16 of 17 saves every province actor's cell has bit `0x80`. In `CAESARXT` the marching army's cell doesn't: the monthly province routine `0x2E0BE` clears the bit over the whole map (`0x2E0F9`), and the army hasn't reached a new cell since.
- The three saves with a marching army (`CAESARXT`, `XS`, `XR`) all have it heading for the city tile, (28, 18).
- `test_province_movement`, `test_province_armies`, `test_province_matches_saves`.

### 28.6 Roads, towns and the Imperial Highway

- **Tiles.** Province roads are `0x36`-`0x41` and the highway `0x6D`-`0x78`, each run in the same 12 shapes; `0x44`/`0x45` are a road through a wall (section 28.8), `0x7B`/`0x7C` a road crossing a highway, `0x4D` a fort. Towns are `0x61` < `0x79` < `0x7A` < `0x4C`. When a province starts (`0x06326`), the map's `0x41` becomes `0x78`, the highway's entry, and its cell goes to `DS:0x6C90`/`0x6C8E` -- the "unexplained" `0x41` -> `0x78` of docs/FORMATS.md. Names STRONG INFERENCE.
- **The trace `0x2E377`** (x, y, class table) follows roads from a cell. A table gives each tile a class; `3496:1810`[class] gives the class's exits (N `0x80`, E `0x20`, S `0x08`, W `0x02`) and whether it's a branch piece (classes 7-11). Directions are tried N, E, S, W and round again while exits remain; a branch piece pushes its state (at most 50) before each step. A step onto a visited cell, a class-0 cell, or a piece without the matching entrance pops back; class `0xFF` is the goal. Visits are marked in `3496:2112`.
- **Towns, `0x2E249`,** at step 80 (after `0x2E209`'s draw): every town cell (compared with the occupancy bit) traced to the city through `3496:1CE0` -- roads, highway, gates, crossings, towns and forts pass, the city `0x4A`/`0x4B` is the goal -- counts in `DS:0x6C92` and, when `DS:0x6DFE` wraps (one month in six), grows a grade; a town not linked shrinks a grade every month.
- **The highway, `0x2E220`:** `DS:0x6C8C` = the trace from the entry through `3496:1D62` (highway, gates and crossings only) reaches the city.
- **The monthly pass, `0x2E0BE`,** the first of step 105's monthly routines: clears bit `0x80` over the map; counts the tiles `0x36`-`0x49` and `0x62`-`0x77` into `DS:0x6C8A`, and the one numbered `DS:0x6C88` turns to `0x1D` (with a message when `DS:0x6DFC`, cycling 0-3, is 0); then `DS:0x6C86` = straight pieces (`0x36`-`0x37`, `0x6D`-`0x6E`) less 4 per corner (`0x38`-`0x3B`, `0x6F`-`0x72`).
- **Wear.** `0x2DF7D` resets `DS:0x6C88` to -1 and its fourth roll picks the next month's worn road among `DS:0x6C8A` when the walk exceeds `DS:0x6BDE` (set by `0x2DE0F` from the plebs on construction, section 29).
- **Checked.** In every save the highway entry holds `0x78`, and the recomputed `DS:0x6C8C`, `0x6C8A` and `0x6C86` equal the saved ones -- all 0, as no save's province has a road yet, so this is consistent rather than a strong test. `test_province_towns`, `test_province_matches_saves`.

### 28.7 The Fort and the Cohort commands

The construction dispatcher's province ids (`DS:127C`) and their names (the string table continues past section 1's 34 entries): 29 Fort `0x1548A`, 30 Halt `0x1577C`, 31 Cohort Patrol `0x1586D`, 32 Cohort Attack `0x15A15`, 33 Cohort Go Home `0x15BB1`, 35 Clear Area `0x15C91`, 36 Provincial road `0x15EF0`, 37 Great Wall `0x169A9`, 41 Great Tower `0x17024`, 42 Highway `0x1645D`; 34 and 38-40 return at once (38 Infrastructure, 39 Construction and 40 Game Options are menus), 43 is Go to City. A click means the cell ((x + 8) / 16, (y + 8) / 16) plus the map's scroll `DS:0x6CB2`/`0x6CB0`.

- **Fort:** fails (`DS:0x6D0A` = 1) on the highway's entry, on any cell but grass `0x1D`-`0x35` or a land border marker `0x59`-`0x60` (the byte compared with its occupancy bit), and with 10 Cohorts (`DS:0x6C12`). Otherwise the cell becomes `0x4D`, a type 13 is placed there (`0x05C39`) with its fort +0x2E/+0x2F = its cell, state 10 and morale 5, and `0x156BD` numbers it (+0x2A) with the first of 0-9 no active Cohort has -- the new record, still 0, counts, so a fort's Cohort is never number 0 while the Prima Cohors (numbered `DS:0x6C12` - 1 at `0x062B3`) is.
- **Clearing a fort** (Clear Area on `0x4D`) frees the Cohort whose fort it was (`0x15710`).
- The four orders pick a Cohort (`0x0F4C1`) that isn't demobilized; Patrol and Attack also need at least one Century. **Halt:** state 10 where it stands. **Patrol:** the first click is the destination, the second +0x2C/+0x2D, state 11. **Attack:** the army's record index (+0x08) into +0x14, +0x2C = 0, state 12, and `0x25438`. **Go Home:** state 13 towards its fort.
- `test_province_commands`.

### 28.8 Province construction

Clear Area, Provincial road, Highway and Great Wall work like the city's drag-built commands (section 18): the eight-neighbour snapshot, now on the 40-wide map (`0334:450D`: off the map 0, a 0 byte leaves the slot as it was), flags for the connectable ranges (`0x17C20`), the first of the 161 patterns at `3496:0A9A` (`0x17CB9`), and a re-tiling of the four orthogonal neighbours by the pattern's modes. Each handler first clears the cell's occupancy bit and refuses the highway's entry and anything below `0x1D`. A handler that sets `DS:0x6D0A` isn't charged by the build routine -- which includes laying a piece again over one of its own, rewritten all the same.

| Command | Handler | Lays on | Connects to | Piece | Re-tiling |
|---|---|---|---|---|---|
| 36 Provincial road | `0x15EF0` | `<= 0x35`, roads `0x36`-`0x41` (free), markers `0x59`-`0x60` | roads, city and forts `0x4A`-`0x4D`, `0x61`, gates `0x44`-`0x45`, `0x79`-`0x7C` | the pattern's | `0x19073`: the city road's rules (section 18), skipping gates, city, forts, towns and crossings |
| 42 Highway | `0x1645D` | `<= 0x35`, highways `0x6D`-`0x78` (free), markers | highways and crossings `0x6D`-`0x7C`, `0x4A`-`0x4D`, `0x61`, gates | the pattern's + `0x37` (`0x41` unchanged) | `0x1A84E`: the road's rules + `0x37`, also skipping `0x78` |
| 37 Great Wall | `0x169A9` | `<= 0x35`, walls `0x42`-`0x43`, `0x46`-`0x49`, `0x62`-`0x6C` (free), markers | `0x42`-`0x49`, `0x62`-`0x6C` | `0x36`-`0x40` -> `0x43 0x42 0x46 0x47 0x48 0x49 0x68 0x69 0x6A 0x6B 0x6C` | `0x1D504` (below) |

- **Over another kind:** a road on a wall (`0x43`/`0x42`) makes a gate (`0x45`/`0x44`) and re-tiles; a road on a highway (`0x6D`/`0x6E`) makes `0x7B`/`0x7C`. A highway on a wall makes the gate unless a neighbour already is that gate, without re-tiling; on a road (`0x37`/`0x36`) `0x7B`/`0x7C`. A wall on a road or highway (`0x37`, `0x6E` / `0x36`, `0x6D`) makes `0x45`/`0x44` and re-tiles.
- **The wall's re-tiling `0x1D504`,** per neighbour N, E, S, W: skip `0x44`, `0x45` and two towers (N `0x62 0x63`, E `0x63 0x64`, S `0x64 0x65`, W `0x62 0x65`); mode 1 writes `0x43` (N, S) or `0x42` (E, W) unless the neighbour is the tower `0x67`/`0x66`; modes 2-4 keep a piece or write another -- N `{49 6B 65}`->`6B`/`46`, `{48 6A 64}`->`6A`/`47`, `{69 6C 41}`->`6C`/`68`; E `{46 68 62}`->`68`/`47`, `{49 69 65}`->`69`/`48`, `{6B 6C}`->`6C`/`6A`; S `{47 6A 63}`->`6A`/`48`, `{46 6B 62}`->`6B`/`49`, `{68 6C 41}`->`6C`/`69`; W `{48 69 64}`->`69`/`49`, `{47 68 63}`->`68`/`46`, `{6A 6C 41}`->`6C`/`6B`.
- **Great Tower `0x17024`:** `0x42` -> `0x66`, `0x43` -> `0x67`, `0x46`-`0x49` -> `0x62`-`0x65`, the byte compared as it stands (the occupancy bit isn't cleared); anything else refused.
- **Clear Area `0x15C91`:** a fort `0x4D` becomes `0x1D` and its Cohort is freed (`0x15710`); `<= 0x24`, `0x4A`-`0x61` and the towns `0x79`/`0x7A` are refused; anything else becomes `0x1D`.
- **Names.** `0x42`-`0x49` and `0x62`-`0x6C` are the wall because the wall writes them, `0x44`/`0x45` gates because road and wall both write them over each other, `0x62`-`0x67` towers because the Tower writes them over wall pieces. STRONG INFERENCE.
- **Checked** on made-up maps: the same run laid on the city map and the province gives the same road pieces, the highway's shifted, the wall's mapped; a road laid from the city to a town links it (`test_province_construction`).

### 28.9 Not modeled

- the pleb routines between the monthly pass and the rolls;
- messages and sounds.

## 29. The plebs (2026-09-14)

Four of step 105's monthly routines run between the province pass `0x2E0BE` and the event rolls `0x2DF7D`, in this order: `0x2DC72`, `0x2DEC8`, `0x2DD21`, `0x2DE0F`. They are the Tribune of the Plebs' model: what each duty needs, how welfare changes the pleb count, who is assigned where, and how well each duty is covered -- which is what sets the city's fire, collapse and road-wear thresholds (section 21) and the province's road wear (section 28.6). `systems::plebs` implements them.

### 29.1 The words

The manual's Tribune screen shows "five sets of numbers ... the numbers of plebs assigned to each of the five duties" with "the amounts of pleb groups needed" in parentheses; the names follow what each duty's shortfall does. STRONG INFERENCE for the names.

| Assigned | Needed | Duty | Its shortfall raises the chance of |
|---|---|---|---|
| `DS:0x6C62` | `DS:0x6C44` | fire prevention | fire, `DS:0x6BE4` |
| `DS:0x6C60` | `DS:0x6C42` | building maintenance | collapse, `DS:0x6BE2` |
| `DS:0x6C5E` | `DS:0x6C40` | road maintenance | city road wear, `DS:0x6BE0` |
| `DS:0x6C5C` | `DS:0x6C3E` (not saved) | construction (the manual says the Tribune handles it) | province road wear, `DS:0x6BDE` (not saved) |
| `DS:0x6C5A` | -- | army duty | -- (auxiliaries `DS:0x6C52` = army duty / 16) |

`DS:0x6C56` is the pleb groups, `DS:0x6C58` the unassigned, `DS:0x6C46` the welfare expenditure.

### 29.2 The routines

- **Needs, `0x2DC72`.** shift = 4 at rank (`DS:0x6C30`) 1 or below, 3 at ranks 2-3, 2 above, and 1 on the hard difficulty (`DS:0x6CB8` = 2); 16 more buildings are counted at rank 3. Fire prevention = (buildings `DS:0x6BF2` + extra) >> shift; building maintenance = (buildings + extra + 16) >> (shift + 1); road maintenance = (roads `DS:0x6BF0` + extra) >> shift; construction = province roads `DS:0x6C8A` >> (shift - 1). Each at least 1. The manual: "A harder game increases the number of plebs you need to maintain the city."
- **Welfare, `0x2DEC8`.** expected = (plebs - unassigned / 4 + (plebs / 20) x rank + 150) / 3. If the welfare paid is less, the plebs lose the difference, at most 10; if more, they gain half the difference, at most 5, up to 2000. (The routine also shifts `DS:0x6DD9` three times without using it.)
- **Assignment, `0x2DD21`.** With 50 plebs or fewer, every duty and the unassigned are 0 and `DS:0x6C64` = the plebs. Otherwise 50 are kept back and the rest go through the duties in the table's order: a duty assigned at least what's left is cut to it, the later duties and the unassigned become 0, and if the cut duty is army duty the auxiliaries are recomputed. What's left after army duty is unassigned. (The Tribune's screen sets the duties; `0x0E85A` also recomputes the auxiliaries.)
- **Thresholds, `0x2DE0F`.** For each of the four duties: assigned >= needed, or needed <= 0, gives 100 and resets that event's target (`DS:0x6CFE`, `0x6D00`, `0x6D02`, `0x6C88`); otherwise assigned x 100 / needed. `0x2DF7D` then rolls each event when the generator's walk (1-99) exceeds its threshold -- so a fully staffed duty never lets its event happen.

### 29.3 Checked against the saves

In all 17 saves, recomputing from the saved counts (difficulty 0) gives the saved needs, the assignment leaves every duty, the unassigned and the auxiliaries as saved, the three saved thresholds match -- including `CAESARWX`'s fire threshold of 90 (10 of 11) and `CAESARXT`/`XR`'s 86 (20 of 23) -- and welfare leaves the pleb count unchanged, each save sitting at the point where the expectation equals the welfare or falls short by one (half of which rounds to nothing). `test_plebs_duties`, `test_plebs_match_saves`.

### 29.4 In Gaius

`systems::plebs`: `set_needs`, `pay_welfare`, `assign`, `set_thresholds`; `systems::month` runs them at step 105 in the engine's order and feeds the thresholds to the rolls, which until now kept the saved values (or 99). The Tribune's screen and its messages aren't modeled.

## 30. Ratings, promotion and the yearly notice (2026-09-14)

The yearly routine `0x28238` ends with three calls after the Legion: the ratings `0x28C43`, promotion `0x29023` and a notice `0x2933B`. `systems::administration` implements them.

### 30.1 The ratings (`0x28C43`)

Four sub-routines, then `DS:0x6C34` = the four's sum / 4. Each rating is 0-100; the names are the manual's, matched by what each reads (STRONG INFERENCE).

- **Peace `DS:0x6C3C`, `0x28C70`:** +2. (Rioters take 2, `0x2DB49`; invasions 4 at rank 1 or 10, section 28.)
- **Culture `DS:0x6C3A`, `0x28C90`:** every city cell with a tile `0xD8`-`0xF2` adds its pair from `3496:0172`: religion 1 for the temple tiles `0xD8`-`0xDF` and 9 for the oracle `0xEB`; entertainment 2, 3, 4 for the theater, coliseum and hippodrome `0xF0`-`0xF2`. With d = units `DS:0x6C10` / 12 + 5 and each division a long one: `DS:0x6C82` = religion x 34 / d, at most 34 counted; `DS:0x6C80` = (entertainment / 2) x 34 / d; the sum at most 68; plus schools `DS:0x6BEC` x 34 / d; then the population cap with step 1.
- **Prosperity `DS:0x6C38`, `0x28E63`,** which accumulates: + `3496:00DE` (rank 1) or `3496:00F6` (other ranks) [(tax per head in hundredths) / 3, at most 23], + `3496:010E`[min(units, 1999) / 50], + 4 for a profit at rank 1, 2 at higher ranks, - 2 otherwise (`DS:0x6BB6`); then the population cap with step 2.
- **Empire `DS:0x6C36`, `0x28F3C`,** recomputed: -5 for a negative road score `DS:0x6C86`, +5 for a positive one and +10 more above 10, +20 for the highway linked (`DS:0x6C8C`), and +20, +10, +5 for each town `0x4C`, `0x7A`, `0x79` on the map.
- **The population cap `0x28FEF`**(value, step): the first b of 0-49 with b x step x 50 above the units limits the value to 2 x b. So a city of 146 units can't have Culture above 6.

### 30.2 Promotion (`0x29023`)

- `DS:0x6C26` = 0. While `DS:0x6C24` is nonzero it counts down and nothing else happens.
- `3496:01C6`[rank x 2] holds the rank's requirement: the average and the minimum for each rating -- (30, 10) at rank 0, (35, 12) at rank 1, rising to (90, 80) at rank 20. The average must reach the first, and Peace, Culture, Empire and Prosperity each the second.
- Earned: `0x2898E` draws until the generator's walk / 2 is a province not yet given (`table_50`), into `DS:0x6CA4`. At rank 19 the last screen `0x290DF` makes the rank 20; below it, the promotion screen `0x291C3` with three buttons (`DS:0x14E2`):
  - **accept `0x29280`:** `DS:0x6C24` = 0, `DS:0x6C26` = 1 (the main loop at `0x0F81B` starts the new province); savings `DS:0x6C2E` cut to 4000 from rank 9, else to rank x 200 + 2300; rank + 1; `DS:0x6C2A` + 5; `0x289B0` makes the picked province current (`DS:0x6CA6`) and marks it given; the difficulty `DS:0x6CB8` goes from 0 to 1;
  - **wait `0x292EA` / `0x292FD`:** `DS:0x6C24` = 9 or 24 years.
- The rank titles follow the province toolbar's names in the string table: Plebian, Citizen, Equitus, Taberllarius, Decurian, Iuridicus, Procurator, Magistrate, Logistas, Praefectus, Magister, Cubicularius, Legate, Quaestor, Senator, Praetor, Consul, Proconsul, Princeps, Imperator, Caesar. The saves' cities are rank 1.

### 30.3 The yearly notice (`0x2933B`)

When the year equals `DS:0x6C98`, that word grows by (walk & 7) + 1 and `0x09AFD` shows a screen by the last draw's low bit: news (`DS:0x6C96` += low7 & 4, back to 0 past 15) or advice (`DS:0x6C94` cycles 0-4, each topic with a second text when its condition holds: the highway linked, more than 2 linked towns, more than 5 Cohorts, construction staffed or no province roads, a positive road score; topic 4 with a road score of 0 becomes 1). Nothing else changes.

### 30.4 Checked against the saves

- The average, in all 17.
- Culture's raw scores `DS:0x6C82`/`0x6C80`, from the city and the year-end units (`table_60_d`), in 14 of 17; the other three changed since the year turned (`CAESARXX` gained a temple cell, `CAESARXS` lost entertainment, `CAESARXR` gained two temple cells).
- Peace and Prosperity across `CAESARXS` (year 7) -> `CAESARXR` (year 8), the one pair of consecutive years: 36 + 2 = 38, and 8 + 1 (0.11 Dn a head) + 0 (496 units) - 2 (a loss) = 7.
- No save earns a promotion (averages up to 16 at rank 1).
- Empire is 0 in every save, consistent (no linked towns or roads) but not a strong test.
- `test_administration_ratings`, `test_administration_promotion`, `test_administration_matches_saves`.

### 30.5 In Gaius, and what's open

`systems::month::run_step` runs the ratings, promotion (answered through `SimState::on_promotion`) and the notice when the year turns. Not modeled: the screens and their texts. Starting the new province is section 31.

## 31. A new game, a new province, and the city's terrain (2026-09-15)

`systems::campaign` implements them.

### 31.1 A new province

When a promotion is accepted (`DS:0x6C26` = 1) the main loop's `0x0F81B` calls `0x0FB34`: screens, `0x0FF1C` (loads `empire2.0NN` into `3496:2752`, the province map), and the reset `0x05730`:

- a draw; `DS:0x6C26`, `0x6C24`, `0x6C20`-`0x6C22`, `0x6C1E`, `0x6C66`-`0x6C6A`, `0x6C9A`, `0x6C7A`, `0x6C70`-`0x6C72`, the walker counters `0x6C12`-`0x6C1A`, `0x6C9C`-`0x6CA0` and the units `0x6C10` to 0; the month `DS:0x6C1C` to 1;
- the funds `DS:0x6CA2` = the starting funding `DS:0x6C0C` (the start screen's 500-8000, `0x27E1A`), less 1000 for each of ranks 2, 3 and 4 while they're at least 5000, and above rank 4 less (rank - 4) x 200 while at least 4000, not below 4000;
- the next notice year `DS:0x6C98` + 1;
- plebs 120, 50 kept back (`DS:0x6C64`), 10 on each duty, 20 unassigned, welfare 88; army wages 20, conscription 10; both tax rates 5; the ratings, Culture's raw scores, the population, the auxiliaries and irregulars, the needs to 0; regulars 2; the rate sums 5; the monthly counts, the accounts and the industrial tax pressure to 0; the tribute due 50;
- `0x05A9A` zeroes the four city layers other than the tiles; `0x05ADD` frees every actor; `0x05B17`, `0x05B33`, `0x05B4F` clear the forum, workshop and barracks records; `0x05B80`, `0x05B95` the goods and milestone tables; `0x05BAA` the five histories;
- `0x0621F` places the Prima Cohors on the first province cell holding `0x4A`: fort there, number `DS:0x6C12` - 1 (so 0), state 10, morale 5, 2 regular Centuries;
- the event targets to -1; the race words (section 27.1); the pleb needs, assignment and thresholds (section 29); and `0x06307`, which turns the first `0x41` on the province map (row by row) into `0x78` and keeps its column and row in `DS:0x6C90`/`0x6C8E` -- the highway's entry, and the "unexplained" `0x41` -> `0x78` of docs/FORMATS.md.

A new game (`0x056B8`) first sets the year to -13, rank 1, savings 100, salary 10, `DS:0x6C2A` 1, the donation 20, clears the provinces given, and schedules the first notice at year -12 with topics from the generator.

### 31.2 The city's terrain (`0x06F05`)

A draw, then seven passes, repeated from the start until the river fits (`DS:0x4F56`):

1. **`0x06F42`, lakes.** Every tile grass `0x1D`. For rows 1-99 and columns 1-98, a draw whose low 11 bits are 0 or 1 makes the 2x2 block from that cell water (0). Then for every cell of columns 1-99: a draw; if its low7 is below 80 and the cell is water, another draw: a walk of 50 or more makes the 2x2 block to its south-west water, and a low7 below 50 the block to its south-east. The writes follow the flat index, so a block at column 99 spills into the next row.
2. **`0x072DD`, three times.** A land cell becomes water when `0x074A1`'s count of its neighbours (edges count as land) finds no land above or below, none left or right, 2 or fewer land neighbours, a longest run of land around it of 2 or less, or 5 or more land neighbours with a diagonal pair and a run of exactly 3.
3. **`0x07228`, shores.** A grass cell with water among its eight neighbours takes the tile of the first of twelve patterns at `3496:15D8` its neighbours fit (0 water, 1 land or edge, 2 either): straight shores 1, 4, 7, 10 and the diagonals 13, 16, 19, 22, each alternating with the next tile (the static `DS:0x079C` toggling), and the corners 25-28 fixed.
4. **`0x07180`, grass.** Two draws per grass cell: an odd low7 gives `0x2E` + (low7 & 7), an even one `0x1E` + (low7 & 15).
5. **`0x07BBC`** keeps a copy of the tiles in the C9D4 layer.
6. **`0x07C12`, the river,** up to three tries, restoring the copy (`0x07BE7`) after each failure. The source is the first grass cell of row 0 at column low7 / 2 + 24 (drawing again while not grass), marked `0x4A`; the river then stands at row 1 heading south. Each step draws: low7 up to 60 goes on (`0x07CD9`), 61-90 prefers a turn west (`0x07FC1`), above 90 a turn east (`0x08161`). Going south it lays `0x4A` if the cell below is grass, else bends west (`0x6A`) or east (`0x6E`) onto grass; going west or east it lays `0x56` if the next cell is grass, else bends south (`0x62` from the west, `0x66` from the east); a turn step bends when it can and otherwise goes on. No grass to go to ends the try; leaving the map (a column past either edge, or the bottom rows) is success.

### 31.3 Checked

- **Against the saves:** every shore tile of all 17 saves' maps is the tile the pattern rule gives its neighbours -- the original generator's output, so the rule and the patterns are confirmed (`test_campaign_terrain_matches_saves`). The lakes, erosion, grass and river depend on the generator's state at the time, which no save keeps.
- **Generated maps** hold only water, shores, grass and river pieces, a river from row 0, and shores that obey the rule; the same generator state gives the same map (`test_campaign_terrain`). `test_campaign_start_province` checks the reset, the funds by rank, the Prima Cohors and the highway's entry.

### 31.4 Open

- Who calls `0x06F05` isn't traced, so where the terrain is made within a province's start is inferred; `start_province` makes it first.
- The start screen (funding, difficulty, name) and the first province's choice for a new game.



## 32. The screens: the Forum's buttons, the province view and the game's name tables (2026-09-15)

`systems::forum` transcribes what the Forum screens' buttons do; `render::render_province` draws the province view; `gaius_viewer` puts them on screen with the promotion, battle, maps and ending screens (section 32.4).

### 32.1 The Forum's buttons

Every arrow moves its word by one per click, and only while inside its limit (section 25.2):

| Word | Up / down | Limits |
|---|---|---|
| `DS:0x6C04` population tax | `0x0E7AE` / `0x0E7C0` | 0-25, then `DS:0x6D8B` = 4 |
| `DS:0x6C02` industrial tax | `0x0E7D2` / `0x0E7E4` | 0-25, then `DS:0x6D8B` = 4 |
| `DS:0x6C08` army wages | `0x0E637` / `0x0E644` | 0-999 |
| `DS:0x6C06` conscription | `0x0E650` / `0x0E65C` | 0-50 |
| `DS:0x6C46` welfare | `0x0EB13` / `0x0EB2C` | 0-9999 (clamped after the step), `DS:0x6D8B` = 6 |
| `DS:0x6C2C` salary | `0x0C164` / `0x0C171` | 0-9999 |
| `DS:0x6C28` donation | `0x0C2C3` / `0x0C2D1` | 0 up to the savings `DS:0x6C2E` |

`DS:0x6D8B` looks like the page to redraw (4 the Treasurer's, 6 the Tribune's; STRONG INFERENCE).

**The Tribune's duties.** A duty's up arrow takes one pleb group from the unassigned `DS:0x6C58`, or with none from the duties after it, last first:

- fire prevention (`0x0EB45`): unassigned, army duty, construction, road, building maintenance;
- building maintenance (`0x0EBA5`): unassigned, army, construction, road;
- road maintenance (`0x0EBF8`): unassigned, army, construction;
- construction (`0x0EC3E`): unassigned, army;
- army duty (`0x0EC77`): unassigned only.

Each down arrow (`0x0EB8F`, `0x0EBE2`, `0x0EC28`, `0x0EC61`, `0x0EC8D`) gives one back to the unassigned. The screen's draw routine (`0x0E85A`) sets the auxiliaries `DS:0x6C52` = army duty / 16 every time it draws.

**The Military Advisor's Cohort.** `DS:0x6C0A` is the number (+0x2A) of the Cohort on display. `0x0E54C` finds the active type-13 actor with that number. Next (`0x0E59C`): while the number is below the Cohort count `DS:0x6C12`, step it up until a Cohort has it (at most to 11), and 10 or more wraps to 0 -- so a number already at the count doesn't move. Previous (`0x0E60E`): step down until a Cohort has it, not below 0. The centre button (`0x0E5D5`) sets state 10 if the Cohort is in state 14, and 14 otherwise.

### 32.2 The name tables

Read from the decompressed image, each a run of fixed-width fields:

- **Provinces**, `DS:0x2CAE`, 50 fields of 16: Sicilia, Campania, Latium, ... Tingitania, Moesia -- the 50 `EMPIRE2.0NN` scenarios. The race names (section 27) end just before.
- **Cohort emblems**, `DS:0x45BE`, 10 fields of 12: Eagle, Rabbit, Snake, Fish, Horse, Pig, Wolf, Hero, Explorer, Protector.
- **Cohort states**, `DS:0x4772`, 16 fields of 16: "nothing" for 0-9, then waiting, patrolling, attacking, retiring, demobilized for states 10-14, then "nothing".

The Military Advisor capture shows the Prima Cohors (number 0, state 10) as "EAGLE" and "WAITING", which fits the emblem indexed by the number and the state word by the state; that each table is indexed so is STRONG INFERENCE.

**A new province's map file.** `0x0FF1C` loads `DS:0x0F4C` into `3496:2752`, then writes the province `DS:0x6CA6` as three decimal digits into offsets 8-10 of the name at `DS:0x0F58` and loads that: `EMPIRE2.0NN`.

### 32.3 The province view

With `DS:0x6CAE` = 1 the map draw (`0x06369` -> `0x065D9`) scrolls the province by whole cells, `DS:0x6CB2` and `DS:0x6CB0`, which `0x066FE` clamps to 0-20 and 0-29. The actor list builder `0x06834` takes the active actors of type 11 and up inside the window (the city's `0x06733` takes those below 11) and draws them through the same `0x06931` -> `0x06946`: frame +0x00 from `SPRITE2.PL8`, bottom edge at y + 8.

`FIXT3.PL8` has 125 frames of 16 x 16, one for each province tile 0x00-0x7C, and its frames run water, shores, grass, roads, walls, the city and towns in the province tiles' order. `render_province` draws frame = tile & 0x7F: STRONG INFERENCE, since no capture of the province view exists. `test_province_render_corpus` checks that every save's province tiles and province actors' frames exist in the two sheets.

### 32.4 In the viewer

`gaius_viewer` switches between the city, the province, the maps panel and the Forum (a strip of buttons along the top; M or gamepad Back). The layouts are Gaius's own, drawn in `FONT1.PL8` when the user's files provide it. `FONT1` has no colon or slash: the `DS:0F64` table draws ':' as '0' and '/' as nothing.

- **The Forum** has five pages -- the Treasurer, the Tribune of the Plebs, the Legion, the ratings and the governor -- with the arrows of section 32.1.
- **Promotion.** `SimState::on_promotion` opens the promotion page and stops time; its buttons call `accept_promotion`, `defer_promotion` or `become_caesar`. An accepted promotion loads the new `EMPIRE2.0NN` and runs `campaign::start_province`.
- **Battle.** `SimState::on_battle` opens the battle page and stops time. Each tactic runs `battle::fight_round` after one generator draw; the original draws once a frame while it waits.
- **The province view** has the province toolbar: Clear, Road, Wall, Tower, Highway, Fort, and the four Cohort orders. Each command is charged with the terrain's cost shift and refused below 50 pleb groups (section 25.1). For an order, click the Cohort, then a point or an army.
- **The maps panel** tints the city by one of the modeled layers: water, the administration's reach, land value, roads or housing.
- **Dismissal.** `SimState::dismissed` (the third missed tribute) opens an ending page, and so does becoming Caesar.

### 32.5 Open

- The original screens' art and layout (`FORUM32`, `P_BLOCKS.PL8`, the advisor text picker `0x0D007`), and the manual's Trouble overlay.
- The start screen (funding, difficulty, name) and a new game's first province.
- The construction need `DS:0x6C3E` on the Tribune's page, which the save doesn't keep.


## 33. Saving, loading and a new game (2026-09-15)

### 33.1 The loader reads what the writer writes

The writer (`0x033C8`, the calls from `0x033F2`) and the loader (`0x04537`, the calls from `0x04561` to `0x05685`) each make 175 calls to their file routine -- `0:32CC` writes, `0:2F78` reads -- and each call pushes a size and a DS or far address. Laid side by side the two sequences agree at every one of the 175: the same address, the same size, in the same order. (The loader's first call also pushes the file handle, which is the only textual difference.) So a save file is exactly those records end to end, and `formats::save::write` of a `model::serialize`d state is a file the engine reads. DEFINITIVE; `test_save_write_round_trip` writes every real save back and reads it byte-identical.

After the reads the loader (`0x05688`-`0x056A4`) closes the file, sets `DS:0x6BFE` = `DS:0x6C02` x `DS:0x6C1C` (section 25.2), calls `0x2C918` -- every cell of `3496:2D94`, the coverage ceiling (`service::ServiceState::coverage_ceiling`), to `0x3F` -- and sets `DS:0x0620` = `DS:0x6C78` xor 1, an options toggle `0x0F006` copies back.

**The difficulty is saved.** `DS:0x6CB8` is the seventh word of `final_state` (FORMATS.md); `systems::month::SimState` had said the save doesn't keep it. `sim_state_from_save` now reads it.

**The menus.** Load (`0x0EE07`) and Save (`0x0EE93`) each open the file dialog (`0x0C558`, an 18-character name at `DS:0x02E4`, the pattern `*.sav` at `DS:0x0D53`/`0x0D59`), confirm overwriting an existing file (`0x0F43F`), call `0x04537` or `0x033C8`, and report the result. The default name is `CaesarXX.sav` (`DS:0x0DDD`).

### 33.2 A new game

The main loop's new game (`0x0F74F`-`0x0F7EE`):

1. `DS:0x6C78` = 1 and `0x056B8`, the new game (section 31.1);
2. the governor's 12-character name is copied to the buffer at `DS:0x5858` ("Octavian" by default, `DS:0x0DD0`);
3. **the start screen `0x27DDF`**. Its arrows move the funding level `DS:0x6CBA` over 0-9 (`0x27F54`/`0x27F60`) and the difficulty `DS:0x6CB8` over 0-2 (`0x27F6C`/`0x27F78`). Each frame `DS:0x6C0C` = `3496:1718`[level]: 8000, 6000, 5000, 4000, 3000, 2000, 1500, 1000, 750, 250 Dn, named Trivial, Beginner, Easy, Simple, Medium, Testing, Challenging, Hard, Exacting, Impossible !! (the strings after the Cohort emblems). Above rank 1, Easy becomes Medium. Its Load button (`0x27FA9`) loads a game instead and sets `DS:0x6DE1`, which skips the rest;
4. `0x2898E` and `0x289B0`: the first province is drawn and made current, as for a promotion (section 22.2);
5. `0x0D1D3` (the empire's province list, `0x0D21E` over `3496:172C`), **`0x06F05` the city's terrain**, `0x09276`, `0x0D21E`, then `0x0FF1C` (the province's `EMPIRE2.0NN`) and `0x05730` (the reset);
6. the funds `DS:0x6CA2` = `DS:0x6C0C` again, a message (`0x27ACE`), and the game starts.

A promotion (`0x0FB34`) runs step 5 in the same order. That settles section 31.4's open question: the terrain is generated before the map loads and the city resets, as `campaign::start_province` does. DEFINITIVE.

`systems::campaign::begin_new_game` runs steps 1, 3 and 4; the caller loads the map and calls `start_province`. `test_campaign_new_game` checks it.

### 33.3 In the viewer

`gaius_viewer <game folder>` opens the start screen: the funding level and the difficulty with their arrows, Begin and Load. The Forum has Save and Load, eight slots `CAESAR01.SAV`-`CAESAR08.SAV` in the per-user data folder or `--save-dir`, each labelled with its province and year. A game saved and loaded goes through the same `model::serialize`/`load` the tests prove.

### 33.4 Open

- Typing the governor's name (the start screen shows the default) -- done in section 40.
- Whether a Gaius-written save of a Gaius-played city runs in the original: the format is proven, but a city Gaius built isn't checked in DOSBox.


## 34. The remaining files: animations, the Forum's click map, the empire markers, sound and music (2026-09-15)

`formats::vas`, `formats::screen_data`, `formats::voc` and `formats::xmi` implement them; `tools/dump_vas` and `tools/xmi2mid` exercise them.

### 34.1 `.VAS`: the battle screen's animations

- **Where they play** (triggers corrected in section 38.2). Three players (`0x23A06`, `0x23ACA`, `0x23B8A`) run while the counters `DS:0x5802`, `0x5806` and `0x5804` are 2 or more. The battle routines start them: a round (`0x23087`, `0x231D4`), the victory (`0x22D56`) and the defeat (`0x22ED4`). Each loads its file into `494C:0000` when its counter is 2: `LOSE0001.VAS` for `0x5802` and `0x5806`, `WINS0001.VAS` for `0x5804`.
- **Frames.** The counter runs from 2 up to the file's word at +2 (`0x5806` stops at 14), so a file has one frame fewer than that word: `LOSE0001.VAS` 21, `WINS0001.VAS` 20. Frame n - 1's 32-bit offset is at +0x10 + 4(n - 1), so the table starts at +0x14.
- **Drawing.** `100F:0851` selects each VGA plane in turn (sequencer map mask 1, 2, 4, 8 with read map 0-3) and calls `2EF9:111F` on that plane's block, stepping on by the block's length. It is called twice with a page flip between, so both display pages carry the frame.
- **The block, from `2EF9:111F`.** A u16 length, a u16 plane size (16000), 4 bytes skipped, then u16 runs until their counts reach the plane size. A run with bit 15 set XORs the next (w & 0x7FFF) + 1 bytes onto the plane; one without skips that many.
- **The planes** are the four interleaved 16000-byte streams of a `.VPX` picture (pixel 4i + p is plane p's byte i), so the runs step through 80-byte plane rows.
- **Checked.** Every block of both files ends exactly at its length, and every frame where the next begins (or the file ends). Played over `WAR2.VPX` with `WAR2.256`, the frames draw a legionary and a barbarian fighting above the battle screen's tactic buttons. DEFINITIVE.

### 34.2 `CONTFRM.GD8`: the Forum screen's click map

`0x0FD77` loads all 1000 bytes into `3496:0330`. The Forum screen (`0x0DF25`) draws `NEWFORUM.VPX`; on a click `0x0DF57` reads the cell (x / 8) + 40 (y / 8) and, when its value v isn't 0, calls the far pointer at `DS:0x0764` + 4v. The 40 × 25 grid's regions trace the picture's eight figures exactly:

| v | Figure | Handler | Music | What it opens (names STRONG INFERENCE) |
|---|---|---|---|---|
| 1 | the statue | `0x0DF9B` | -- | checks a key code first; unread |
| 2 | man in blue, far left | `0x0E0F4` -> `0x0BDD3` | `czarjina.xmi` | the governor's own affairs: salary (`0x0C164`) and donation (`0x0C26E`) |
| 3 | the legionary | `0x0E116` | `czarjin6.xmi` | loads `SPRITE2.PL8`: the Military Advisor |
| 4 | man in a blue robe | `0x0E668` -> `0x0ACA7` | `czarjin4.xmi` | panels over the city map; unread |
| 5 | bald man in orange | `0x0E6BC` | `czarjin9.xmi` | the Treasurer: the tax rates |
| 6 | man in the white toga | `0x0E105` -> `0x0CC21` | `czarjinb.xmi` | loads `TEMPLE.VPX` and draws three `TEMPLBIT.PL8` columns (`0x0D069`): the ratings |
| 7 | woman with fruit | `0x0E7F6` | `czarjin7.xmi` | the Tribune of the Plebs: the duties |
| 8 | man in green | `0x0E0A6` -> `0x09D22` | `czarjin5.xmi` | reads the workshops' level (`0x09FF1`): industry |

The international build's file differs because its picture does. The click map is DEFINITIVE; which advisor is which is from what each handler draws.

### 34.3 `EDATA.CSR`: the empire map's province markers

`0x0FD5B` loads the first 200 of the 320 bytes into `3496:1650`. The province list routine (`0x0D21E`) walks the 50 provinces in the order `3496:172C` lists them, and for each one given (`table_50`) draws a marker: sprite `0x30` for the current province, `0x31` for the others. It draws it at the two words `3496:1650` + 4 × province, byte-swapped, so big-endian in the file, less 8 in x and 32 in y; province 39 gets 10 more in x. Sicilia's marker is at (125, 115). The last 120 bytes aren't loaded by this build. DEFINITIVE for the 200 read.

### 34.4 `TEMPLBIT.PL8`

Three 32 × 10 frames: a column's capital, shaft and base. The ratings advisor draws them over `TEMPLE.VPX` (`0x0D069`, three calls), presumably as the ratings' columns; how tall each column is drawn isn't read.

### 34.5 Sound effects and music

- **`.VOC`** is Creative Labs' published Creative Voice File. All 23 effects are 8-bit unsigned mono PCM in sound blocks, and `formats::voc` decodes them all. Four are named for the battle tactics (`TORTOISE`, `ATTACK`, `FLANK`, `CHARGE`). Which events play which sound isn't traced.
- **`.XMI`/`.XM2`** is Miles Extended MIDI, also published. `formats::xmi` converts all 28 files to Standard MIDI at 120 ticks a second.
- **The international build's `.MDI` files** are not those files converted (correcting `docs/CAESAR_GOG_BUILD_FINDINGS.md` section 7's "same music, different container"). They are re-orchestrated for General MIDI: drums moved from channel 9 to other channels and pitches, channels renumbered, velocities changed. Some are also renamed: the US `CZARJIN1` is the international `CZARJIN2`. What survives is the note count and the timing. Every note onset of the US `CZARJIN1`, `6`, `A` and `B` lands within 0.017 quarter notes of the matching `.MDI`'s, which confirms the 120-ticks-a-second conversion (`test_xmi_matches_mdi`).
- **Playing either** needs an audio path Gaius doesn't have yet (Phase 9).


## 35. Messages, the game speed, and the last unnamed words (2026-09-15)

`systems::messages` and `systems::month::run_frame` implement the first two; the third names every saved word the code hadn't used.

### 35.1 Messages

**The board.** One message shows at a time:

- A poster stores the text's far pointer in `DS:0x6C74` and starts the 80-frame timer `DS:0x6C7A`, but only while that timer is 0, so a message posted during another is lost.
- Posters with a place set `DS:0x6C72` = 1 and `DS:0x6C70` (0 city, 1 province map), and keep the cell less 10 and 5 in `DS:0x6C6E`/`0x6C6C`.
- The display routine `0x279AC` runs each frame while the messages option `DS:0x6C78` is on (a new game sets it, `0x0F74F`). It plays sound 6 on the first frame, draws the text's two 28-character lines at (16, 14) and (16, 30), counts the timer down, and forgets the place at 0.
- A left click on the message area (x < 256, y < 48) with a place (`0x0F982`) switches to its view, scrolls it there and ends the message; a right click (`0x0F93A`) dismisses it.

**The posters:**

| Routine | Kind | Callers and texts |
|---|---|---|
| `0x27A54` | city place | `0x2D908` "Barbarians are entering the city" (the invader's entry cell); `0x2DC4B` "There is unrest in parts of the city" (a rioter's house) |
| `0x27A91` | province place | `0x24CE5` "approaching your roads", `0x24D42` "... highway", `0x24E22` "... towns" (the army's cell) |
| `0x27ACE` | plain | the population milestones; `0x284DA` "Your salary has been stopped"; `0x11C8F` "You must allocate more Plebs to construction work"; `0x14E1D`/`0x15252`/`0x15482` "no need for another forum/factory/fort"; `0x2E15E` "The provinces need more workers for maintenance" (timer then set to 79); `0x0F7D3` "13 BC - and as yet you have no city to govern !!." (timer then 78) |
| `0x27AF3` | tribute | by the missed count `DS:0x6BB8`: unpaid, again, the arrest |
| `0x27B3C` | province place | "Barbarians sighted." with characters 7-22 replaced by the province's 16-character name field |
| `0x27BA1` | milestones | at step 80: population above 200, 1000, 2000, 4000, 8000, 12000, 16000, 20000, each once (`table_10`), the flag set whether or not the message shows |
| `0x27CBA`, `0x27D58`, `0x27D09` | limited | road wear, collapse and fire (`0x2C4EA`, `0x2C525`, `0x2C54E`), each counting down its saved word `DS:0x6C66`/`0x6C68`/`0x6C6A` and posting at 0 (then 5): one in five |

- **When they fire.** An army's road message fires when the draw's low 7 bits are 0x15-0x18, within the wreck's 0x15-0x1F. Its highway message comes with every highway wreck (0x19-0x1F), and its towns message with every pillage.
- **The funds warning.** `0x0FAD3`, every frame: with the funds at or below 1000 and `DS:0x6BE6` clear, it sets the flag and shows the full-screen warning `0x084B1` (six lines at `DS:0x085F`), waiting for a click. It comes once a game.
- **The texts** are loaded as far pointers by `0x277xx` into `DS:0x6E2A`-`0x6EFE`. There are more of them -- the file dialogs, the Cohort disk prompts, the industry report -- used by screens Gaius draws its own way. `DS:0x6ECE` "You have insufficient funds for construction work" is stored but never posted.
- **Checked** by `test_messages_and_speed`: the board's rules, one in five, the milestones, the sighted substitution, the tribute count and the funds warning.
- **Not modeled:** the sounds.

### 35.2 The game speed

The main loop `0x0FA13`, once a frame:

1. steps the phase `DS:0x6DE3` (it becomes the old value + 1, or 0 after 10);
2. draws a random number;
3. asks the speed gate `0x0FAA2`, which reads byte `3496:0000`[`DS:0x5292` / 10 × 10 + phase] -- eleven rows of ten, from speed 0 (all 0) to speed 100 (all 1). Row k passes k of phases 0-9, and phase 10 reads the next row's first byte, so even speed 0 passes once in eleven frames;
4. a frame that passes runs the frame counters, the walkers and a step;
5. every frame runs the displays -- among them the message timer and the funds warning.

The speed `DS:0x5292` moves in tens between 0 and 100 on the options screen (`0x0F204`/`0x0F217`). It lies outside the save, in the uninitialized data. (Corrected in section 42.3: it is read from `CAESAR.INF`, not `csr0.dat`.)

`month::run_frame` is this frame, and at speed 100 it is exactly `run_step`. Eleven frames at speed 50 run six steps and draw eleven times (`test_messages_and_speed`). `gaius_viewer` now runs frames, with the speed on the governor's page (`--speed`).

### 35.3 The saved words that had no name

| Words | Writer and reader | Name |
|---|---|---|
| `DS:0x6CBE`-`0x6CE2` | written by `0x23493`-`0x2357D`, read back by `0x23272` | the battle handed to the separate Cohort 2 program (section 27): a flag `0x6CE2`, the Cohort's and army's slots `0x6CDC`/`0x6CDE`, their Centuries and morale `0x6CCC`-`0x6CD8`, number `0x6CDA`, army sizes `0x6CC6`/`0x6CC8`, the race `0x6CCA`, year `0x6CC4`, month `0x6CC2`, province `0x6CC0`, and `0x6CBE` from `DS:0x6DE7`. `0x6CBC` is never read or written |
| `DS:0x6CB6`, `0x6CB4` | `0x066C9` clamps them to 0-80 and 0-89 | the city view's scroll column and row |
| `DS:0x6CB2`, `0x6CB0` | `0x066FE`, 0-20 and 0-29 | the province view's (section 32.3) |
| `DS:0x6CAE` | | the view: 0 city, 1 province |
| `DS:0x6CAC` | `0x06369`: with 0 the pointer doesn't scroll the view; toggled by `0x0FB6C` | the original's command / scroll mode |
| `DS:0x6CAA` | `0x06369` narrows the scroll limits by 16 px per step, 0-6; the toolbar handlers set it | the build cursor's size (STRONG INFERENCE) |
| `DS:0x6CA8` | the Fort and Cohort order handlers (`0x1578B`-`0x15C89`) set 1 and 2 | how far an order's clicks have got |
| `DS:0x6C7C` | set to 80 by the calendar (`0x29498`), counted down by `0x278A8` | the new year's banner timer |
| `DS:0x6C78` | `0x279AC`; copied from and to `DS:0x0620` by the loader and options | the messages option |
| `DS:0x6C66`, `0x6C68`, `0x6C6A` | 35.1 | the message rate counters |
| `DS:0x6C6C`, `0x6C6E` (not saved), `0x6C70`, `0x6C72` | 35.1 | the message's place |
| `DS:0x6BE6` | `0x0FAD3` | the funds warning given |
| `DS:0x6BDC` | nothing | unused |

## 36. The Forum's other figures and the ratings advice (2026-09-16)

Three of the Forum picture's eight figures (`CONTFRM.GD8` regions, section 34.2) had no page in Gaius; the ratings screen's advice texts had been located (`0xD007`) but not read. `systems::forum` now transcribes all four, and `gaius_viewer`'s Forum opens them.

### 36.1 The statue (region 1, `0x0DF9B`) -- a rank cheat

- **The gate.** The page opens only when the keyboard handler's words `2EF9:0031` = `0x63` and `2EF9:002F` = `0x42`. `2EF9:002F` is the last typed character -- the text entry at `0x11093`-`0x112AF` compares it with Escape, Enter, backspace, digits and letters -- so the second key is "B". `2EF9:0031` keeps the character before it ("c"): DEFINITIVE since section 42.4. Leaving clears `2EF9:002F`.
- **The page.** The rank title (the 16-character table `DS:0x7102`) at (0x68, 0x40), with two arrows.
- **The arrows.** Up (`0x0E066`): the rank `DS:0x6C30` + 1, at most 19; then, above rank 1, difficulty `DS:0x6CB8` 0 becomes 1. Down (`0x0E086`): the rank - 1, at least 1; then, at rank 1, difficulty 1 becomes 0.
- `gaius_viewer` opens it after "c" then "B" (section 42.4), or with `--cheats`.

### 36.2 The histories (region 4, `0x0ACA7`)

Four panels (`1F6F:2008`) of bars (`1F6F:236C`), one per yearly history buffer (section 22; 15 records of year and value):

| Buffer | Value | Newest bar at | Base row | Scale | Max height | Captions (after 0, 1, 2 doublings) | Label |
|---|---|---|---|---|---|---|---|
| `3496:01F0`, index `DS:0x6B36` | `DS:0x6BC6` | x 0x80 | 0x4C | 25 | 36 | 0 - 750 / 1500 / 2250 dn | population tax |
| `3496:022C`, `DS:0x6B34` | `DS:0x6BC4` | 0x120 | 0x4C | 25 | 36 | the same | industry tax |
| `3496:0268`, `DS:0x6B32` | `DS:0x6CA2` | 0x80 | 0xAC | 200 | 68 | 0 - 12500 / 25000 / 37500 dn | city funds |
| `3496:02A4`, `DS:0x6B30` | `DS:0x6C10` | 0x120 | 0xAC | 25 | 68 | 0 - 6000 / 12000 / 18000 | population |

- **The graph routine.** It walks 14 records back from the newest (wrapping at 15). If any value / scale (signed) is taller than the maximum, it doubles the scale and starts again, counting the passes. Each bar is value / scale capped at the maximum, drawn only when positive, 8 pixels left of the one before. The pass count (1-3) picks the caption at (0x14 or 0xB4, 0x24 or 0x64) in `MINIFONT`; four or more passes show none. The captions don't match the scales (36 x 25 is 900, not 750) -- transcribed as they are.
- **The art.** The bars are `POINTERS.PL8` frames 0x36/0x37 (8x76; 0x37 where x is a multiple of 16), cut to the bar's height by `303E:15B7`; which rows of the frame show is INFERENCE. The panels come from `P_BLOCKS.PL8` (corrected in section 38.3): `1F6F:1EC1` tiles frames 0-8 as a nine-slice frame, with the interior drawing `0x13` + the 50-byte pattern `3496:0718`, and `1F6F:2008` draws frames 9-17 as an inset. The industry report's goods icons are frames 0x38-0x3F (16x11).
- **The caption years** (`0x0B04C`). "A.D.     -" or "B.C.     -" by the sign of year - 15, then |year - 15| at x 0x8A and |year - 1| at 0xB8. In year 14 that reads "B.C. 1 - 13".

### 36.3 The industry report (region 8, `0x09D22`)

- **The header.** "Industry Report on" plus the province's 16-character name (`DS:0x70F2`, index `DS:0x6CA6`).
- **Overall Industry Rating -.** `0x09FF1` averages `level & 7` (+0x10) over the workshop records whose +0x08 is set, and stores it in `DS:0x6BE8` -- the same word the yearly accounts compute, and all 17 saves hold exactly this average. The grade: 1 or less Terrible, 3 or less Poor, 5 or less Average, 6 Good, 7 Excellent.
- **Prospects for Expansion -.** `DS:0x6BF4` graded: -2 or less, 0 or less, 2 or less, 4 or less, 5 and up.
- **A row per goods** (`0x0A04E`, y = 12i + 0x28). The name (`DS:0x46F1`: Glass, Tin, Pottery, Copper, Wine, Ivory, Wheat, Spices), the icon 0x38 + i, then the suitability `3496:1880`[province × 8 + i] -- the base of a workshop's level (section 20) -- named -3 Terrible, -2 Poor, 0 Average, 1 Good, 2 Excellent, while -1 shows nothing. Last, the factories `DS:0x5816`[i].

### 36.4 The ratings advice (`0x0CC21`, picker `0x0CF12`)

A click with the pointer's y in 0x78-0xB3 advances `DS:0x6D2A` (0, 1, 2, 0...) and picks one of 14 texts (`DS:0x7142`...`0x712E`), shown for 90 frames in place of "Average Rating %":

| x | Rating below 100 | Cycle 0 | Cycle 1 | Otherwise |
|---|---|---|---|---|
| 1-79 | Peace | `DS:0x6C84` = 1: "Stop barbarians from reaching city" | `0x6C84` = 2: "Building temples helps prevent riots" | "Reduce taxes, keep everyone happy !!" |
| 81-159 | Culture | `DS:0x6C80` < 36: "Prehaps you need a grand spectacle" | `DS:0x6C82` < 36: "Is everyone served by religion ?" | random walk `2EF9:0286` < 50: "Increase your population", else "Are people healthy and educated ?" |
| 161-239 | Prosperity | `DS:0x6C10` < 1200: "Increase the population" | `DS:0x6C7E`: "Check everyone is paying you tax !" | "Upgrade slums to high grade housing" |
| 241-319 | Empire | `DS:0x6C8C` = 0: "Build an Imperial highway to Rome" | `DS:0x6C92` < 4: "Develop links with local villages" | "Straighten those roads !" |

Anything else -- a rating of 100, or x on 0, 80, 160 or 240 -- gives "you need no help here". `DS:0x6C84` is set to 1 by an army reaching the city (`0x24C01`) and to 2 by a collapse's rioter (`0x2DC56`). Earlier sections' "`0xD007` advisor text picker" is this routine's middle.

**Checked** by `test_forum_figures`: the statue's limits, the doubling scale, the caption years, every hint branch, and the industry average against all 17 saves.

## 37. The map of the Empire (2026-09-16)

`render::render_empire_map` draws the screen and `gaius_viewer` opens it from the governor's page. The screen:

- **The picture.** `0x09276` decodes `EMAP2.VPX` to the screen (palette `EMAP2.P32`), then loads `HOUSES2.PL8` back into the same buffer for the city. The round dots on the map belong to the picture.
- **The markers.** `0x0D21E` draws them from the interface sheet at `A000:8000` -- `POINTERS.PL8`, the sheet the Forum's panels come from (section 36.2) -- through `1F6F:1831`. It walks the provinces in the order `3496:172C` lists them (`23, 22, 21, 20, 24, 25, 19, 17, ...`, north-west first) and draws each given one (`table_50`): frame `0x30` (16x32) for the current province `DS:0x6CA6`, `0x31` for the others, at its `EDATA.CSR` position (section 34.3). With `DS:0x4F4A` set it draws none.
- **Checked against the capture.** The DOSBox capture `7489888-caesar-dos-map.png` is the new-province screen for Pamphylia (35). With its marker drawn, every pixel above the status line matches at 6-bit level. The marker explains all 240 pixels that differ from the bare picture, and frame `0x31` in its place leaves 201 wrong. `test_empire_map_screen` checks this.

Where the game shows it:

| Routine | When | What |
|---|---|---|
| `0x0D174` | the governor's screen (`0x0BDD3`), its third button | the map, until a click. The governor's buttons are the 16-byte records at `DS:0x0444` -- cell x, cell y, `P_BLOCKS.PL8` frame and pressed frame (section 38.3), handler far pointer, timer, mode (read by `0x0D521`/`0x0D41D`) -- at cells (18, 1), (18, 2), (18, 4), (18, 8), (18, 10); the map is (18, 4) -> `0x0C060` |
| `0x0D1D3` | a new game or a new province (`0x0F79D`, `0x0FB34`), before the terrain (`0x06F05`) | the map with "generating scrubland" in `FONT1` at (0x4E, 0xBB), drawn to both pages. After the terrain the map is redrawn without it, then the province file is loaded (`0x0FF1C`) and the province starts (`0x05730`), with no wait for a click |

Gaius's terrain generation takes no visible time, so the viewer doesn't show the second screen.


## 38. The battle screen's art (2026-09-16)

Section 27 transcribed the rounds; this section reads the screen `0x22116` draws them on. `ui::BattleScreen` implements it, and `gaius_viewer` shows it when the files are present (otherwise the plain page of section 32).

### 38.1 The frame

**The start.** The screen:

- loads `FONT2.PL8` over `FONT1` at `54E0:C648` and `SPRITE2X.PL8` at `630D:0000`;
- sets the race (section 27.1);
- keeps the starting figures:
  - `DS:0x57F0` the Cohort's standard = its number × 4;
  - `DS:0x57F2` the race's banner (40-43);
  - `DS:0x57FE` the army's size;
  - `DS:0x57FC` the Centuries × 2;
  - `DS:0x57F8`/`F6`/`F4` the regulars, irregulars and auxiliaries.

With `cohort.exe` present it first offers the Cohort 2 hand-over on `WARMESS.VPX` (`0x09611`: "Left click here for Cohort. Click / elsewhere or right-click to continue."). Gaius has no Cohort 2 and skips it.

**The background** (`0x0934B`):

- `WAR2.VPX` with `WAR2.256`. The tactic buttons are part of the picture.
- `100F:0EF9` (x, y, w, h, colour) clears each bar's starting height + 2 in black: 16 wide at x 0x10E and 0x124.
- It then fills colour 12, 14 wide, standing on y 0x62: the Romans' Centuries × 4 at 0x10F and the army's size × 4 at 0x125, each at most 64.

**Every frame** (`0x2244B`), in this order:

1. A random draw (`2EF9:1425`).
2. `0x225EE`: the message and animations (38.2). While no message shows, it draws the Cohort's emblem (`DS:0x45BE`, 12-character fields as stored) at (8, 0xA2) and the race (`DS:0x2BAD`, 16) at (0x7E, 0xA2), both in `FONT2`.
3. `0x226BC`: the figures in `MINIFONT`:
   - "Men" at (0x118, 0x68), with the Romans' Centuries at (0x110, 0x70) and the army at (0x128, 0x70);
   - "Morale" at (0x110, 0x8C), its value at (0x11E, 0x96);
   - "r   -", "i   -", "a   -" at x 0x10C, y 0xA0 / 0xAA / 0xB4, with the current count at x 0x118 and the starting count at 0x128;
   - then `0x23878` draws the standard at (0x10E, 0x12) and the banner at (0x124, 0x10) from `SPRITE2X.PL8`.
4. `0x2250F`: the buttons.
5. The closing count `DS:0x5800` goes down by one. The screen closes when it reaches 1.

**The buttons** (`0x2250F`). They answer only with no message and no closing count, and only at y 0xA8 and below. By x:

| x | Button |
|---|---|
| 0x10-0x2F | Tortoise |
| 0x40-0x5F | Assault |
| 0x70-0x8F | Flank |
| 0xA0-0xBF | Charge |
| 0xD0-0xF0 | Retreat |

The gaps between them do nothing.

### 38.2 Messages and animations

**Showing a message.** `0x0954F` redraws the background, grabs the stone at (4, 0xB8) (`1F6F:1E73`) and tiles it over x 8-247 from y 0xAA, hiding the buttons. The message's two lines go at (8, 0xAA) and (8, 0xBC) in `FONT2`, and `DS:0x57FA` counts the frames it stays.

| Outcome | Lines | Frames | Closing | Animation |
|---|---|---|---|---|
| even | "It is a tough battle and" / "still could go either way." | 0x82 | | |
| Romans stronger | "The battle goes well and" / "the enemy is weakening." | 0x82 | | |
| barbarians stronger | "Your soldiers are faltering." / "You should try a new tactic." | 0x82 | | `LOSE0001.VAS` to counter 14 (`DS:0x5806`) |
| victory | "Victory is yours. The enemy" / "scatters in disarray." | 0xAA | 0xA9 | `WINS0001.VAS` to 21 (`DS:0x5804`) |
| defeat | "Defeat and dishonor as the" / "barbarians sweep over you." | 0xAA | 0xA9 | `LOSE0001.VAS` to 22 (`DS:0x5802`) |
| retreat | "The barbarians press on ." / "Your troops are demoralized." | 0x82 | 0x78 | `LOSE0001.VAS` to 14 (`DS:0x5806`) |

This corrects section 34.1, which had the victory and defeat animations the wrong way round and called `0x23087` a round's.

**While a message shows** (`0x225EE`):

- A click stops all three animations. It then drops the closing count to 2, or, with no closing count, ends the message next frame.
- The timer counts down. At certain counts it plays sounds 5 and 7 (`0x22497`).
- Each running animation plays a frame (`0x239E1`): counter n draws file frame n - 2, and the animation stops past its limit.
- At 0 the background is redrawn.

**The retreat dialog** (`0x23708`). It reloads `FONT1`, switches the whole screen to the palette saved at `68F6:B6A8`, clears both pages (`100F:00EF` calls `2EF9:0C50` on each, STRONG INFERENCE), and draws a 14 × 5 panel at (0x30, 0x40). The text is "Retreat ?" at (0x5A, 0x4A) and "     Yes" / "      No" at (0x32, 0x62) / (0x32, 0x72), and the buttons are `DS:0x132C`: cells (12, 6) Yes -> `0x236EE` and (12, 7) No -> `0x236FB`. Afterwards it puts `WAR2`'s palette back. That the saved palette is `SHADE.256` is confirmed by the maps screen's capture (section 39.1).

### 38.3 Which sheet the interface routines draw from (correcting 36.2 and 37)

`1F6F:1831` (sprites), `1F6F:1EC1`/`2008` (panels) and `0x0D521` (buttons) all point `303E:0016` at `A000:8000`. The first calls `2EF9:0298` first, the others `2EF9:0290`.

- **`POINTERS.PL8` through `0298`:**
  - the map markers (frames 48/49), proven pixel-exact in section 37;
  - the histories' bars (54/55, 8 × 76);
  - the goods icons (56-63, 16 × 11).
- **`P_BLOCKS.PL8` through `0290`:**
  - the panels: frames 0-8 corners, edges and middle, 9-17 the inset, `0x14`-`0x19` the stone variants the pattern picks;
  - the buttons (29/30).

`POINTERS.PL8`'s frames 0-8 are icons, so section 36.2's panels and section 37's governor buttons are `P_BLOCKS.PL8`. That the two calls select the two sheets loaded as slots 1 and 2 (renderer findings) is STRONG INFERENCE from what the frames show.

**Checked** by `test_battle_screen`:

- the starting words;
- both bars, including the 64-pixel cap;
- every button's range and the gaps;
- a round's message locking the buttons, and the animation changing the picture;
- a click cutting the message short;
- the dialog's No and Yes;
- the 0x78-frame close after a retreat.

No DOSBox capture of this screen exists to compare pixels against.


## 39. The maps screen (2026-09-16)

The original's maps screen (`0x0B717`) is a small map of the whole city beside six buttons, not a tint over the city view. `ui::compose_maps_screen` draws it, and `gaius_viewer` shows it in place of its own overlay tints when the files are present. Against the DOSBox capture `2053402-caesar-dos-evaluating-your-infrastructure.png`, every pixel outside the map and the mouse pointer matches at 6-bit level (53,424 pixels, `test_maps_screen`).

### 39.1 The screen

- **Palette and frame.** The palette saved at `68F6:B6A8` -- `SHADE.256`, which the capture's match confirms (it was INFERENCE in section 38.2). `1F6F:1EC1` draws a 20 x 11 panel at (0, 0x10), and `1F6F:2008` a 7 x 7 inset at (0x10, 0x20).
- **Blocks draw opaque.** `303E:0BF0` draws a whole block, colour 0 included, so the inset's black edge frames the map. The sprite routine `303E:13D6` is the one that skips colour 0; drawing them transparent left 405 wrong pixels, the other 2 being the pointer's edge.
- **The labels,** in `FONT1` at x 0x86, y 0x24 + 16i: "   urbanization", "water distribution", "  administration", "   road layout", "    land value", "  trouble areas" (`DS:0x6FF6`, `0x6FF2`, `0x6FEE`, `0x6FEA`, `0x6FE6`, `0x6FE2`).
- **The buttons** are records at `DS:0x0704` (the format of section 37): `P_BLOCKS.PL8` frames 29/30 at cells (18, 2)-(18, 7).

| Cell | Handler | Does |
|---|---|---|
| (18, 2) | `0x0DEC2` | toggles the city, `DS:0x6D28` ("urbanization") |
| (18, 3) | `0x0DE6D` | water, `DS:0x6D26` = 1 |
| (18, 4) | `0x0DE8F` | administration, 3 |
| (18, 5) | `0x0DEB1` | roads, 5 |
| (18, 6) | `0x0DE7E` | land value, 2 |
| (18, 7) | `0x0DEA0` | trouble areas, 4 |

Mode 0 is the screen before any button is chosen.

- **The legend.** Plain stone (`1F6F:2115`, 10 x 3 at (0x90, 0x88)), then 12 x 12 boxes (`100F:0EF9`) with `MINIFONT` text in colour 0, black -- the capture's match settles the ink on this screen.
  - water: box `0xF`, "water";
  - administration: box `0x11`, "admin";
  - roads: box `0xA`, "roads";
  - the rest: box `0x1F`, "negative", and boxes `0x14`-`0x19` at x 0xD0-0x120, "low  -  high";
  - with the city shown, a box `0xB` at (0x90, 0xA0) and "city".
- **Clicks.** A click on the map (`0x0DCB4`, x 0x16-0x79, y 0x26-0x89) goes to the city view there.

### 39.2 The map's colours (`1F6F:0009`)

Pixel (0x16 + x, 0x26 + y) is cell (x, y). The layers are read through bases that land on row 0 once y's 0x26 × 100 is added: `43A5:F128` + 0xED8 wraps to the tile layer, `3496:BAFC` + 0xED8 = `C9D4`, `3496:93EC` + 0xED8 = `A2C4`, and `3496:45B6` + 0xED8 = `54A4`.

- **The ground** (`0x1FADA`), where a mode leaves a cell:
  - black when the city shows and the tile is 0x92-0xA1 or 0xC8 and up;
  - `0xF` for tile 0, 0xA4-0xA6 or 0x4A-0x91;
  - `0xC` for tiles up to 0x26;
  - 6 otherwise.
- **Water:** `C9D4` bit 0x01 -> `0xF`, or `0xE` on the city when it shows.
- **Administration:** `C9D4` bit 0x20 -> 9, or 8 on the city when it shows.
- **Roads:** tiles 0x5E, 0x82, 0x86, 0x36-0x43 and 0x94-0x95 -> `0xA`.
- **Land value** reads `A2C4` as signed: -4 or less `0x1F`, -3 `0x1D`, -2 `0x1B`, -1 `0x1A`, 0 the ground, 1-3 `0x16`, 4-7 `0x17`, 8-15 `0x18`, 16 and up `0x19`.
- **Trouble areas** read `54A4` on housing (tile above 0xC8): 1-7 `0x16`, 8-15 `0x17`, 16-23 `0x18`, 24 and up `0x19`; anything else is the ground.

**A naming question this raises.** The layer the original calls "land value" is the one `model::CityMap` names `coverage` (`A2C4`). What `CityMap::land_value` holds (`54A4`) is what the original maps as "trouble areas" on houses -- the value whose limit collapses a house (section 21). The code keeps its names; this is recorded here rather than renamed.

**Not compared:** the map's own pixels -- the capture's city isn't one of the saves -- and the frames `0x0DC18` loops over while the map draws in (`DS:0x6D24` counts three passes).


## 40. The governor's name (2026-09-16)

`ui::NameEntry` implements the name and its dialog. `gaius_viewer`'s start screen has a "Choose name" button and shows the name.

### 40.1 Where the name lives

- **In memory.** 12 characters at `DS:0x0DD0`, "  Octavian  " to begin with, reached through the far pointer `DS:0x5858` (set by `0x0F6FF`). It survives a new game.
- **In a save.** The writer copies it to the 12 bytes at `DS:0x6B9E` (`0x0434E`) and saves them as `final_state` bytes 12-23; the loader copies them back (`0x054CA`). All 17 saves hold "  Octavian  ".
- **On screen.** The governor's screen draws it in `FONT1` at (0x40 + 16i, 0x3C) (`0x0C328`), with the button at `DS:0x0444`'s first record (`0x0BE58`) opening the same dialog there.

### 40.2 The dialog (`0x0C41D`)

The start screen's "Choose name" button (`0x27F84`) calls it with the button records `DS:0x0164` (24 of them).

- **Drawing.** A 14 x 5 panel at (0x30, 0x40), drawn to both pages. Each frame: the records' buttons (`0x0D521`), the plain stone under the name (`0x0D623`), the 12 characters at (0x44 + 16i, 0x64) (`0x0C2DD`), and a '-' under the cursor `DS:0x6D32` at (0x44 + 16 × cursor, 0x69).
- **The arrows.** The records are an up arrow over each letter, cells (4-15, 5), `P_BLOCKS.PL8` frames 18/27, and a down arrow under it, cells (4-15, 7), frames 19/28. The handlers `0x0C902`-`0x0CB4E` pass the letter's index (`DS:0x6D30` - `DS:0x6D2E` + i, both 12, so i).
  - **Up** (`0x0CB68`): 'Z' becomes 'a'; anything below '@' becomes '@'; otherwise the next character, capped at 'z'. So a blank steps to '@' (which `FONT1` draws as nothing), then 'A'.
  - **Down** (`0x0CBD3`): the previous character, at least '@'. It means to turn 'a' back into 'Z', but it tests the name's first character plus the index against 'a' instead of the letter itself (`mov al, es:[bx]` before `bx` is indexed). Transcribed as it is.
- **A click on the name** (`0x0C859`, x 0x40-0x100, y 0x60-0x70) puts the cursor on that character.
- **The keyboard** (`100F:0F79` = `0x11069`, with both limits 12). It reads the typed character `2EF9:002F` and the scan code `2EF9:0033`:
  - Escape returns 1 and Enter 2, and either ends the dialog.
  - Backspace moves the cursor back and blanks that character.
  - Left and Right move the cursor. The arrow keys also move the mouse pointer 8 pixels.
  - Delete blanks the character under the cursor, and on the last character also steps back.
  - '_', digits and letters overwrite the character under the cursor and advance. The insert path (`0x11305`, which shifts the rest right) needs the cursor at the length and the length below the limit, which never holds here.
  - Anything else, space included, does nothing. The cursor stops on the last character.
- **Ending the dialog.** A right click (`DS:0x6D4C`) also ends it. The name is edited in place, so every way out keeps it.

### 40.3 In Gaius

- The platform layer gained `CommandType::TextKey` and `set_text_entry`: SDL's text input while a field is open. On a phone that brings up the on-screen keyboard, though the arrows make it unnecessary.
- The viewer keeps the name like `DS:0x0DD0`: across new games, written into each new game's state, and read back from a loaded game.
- **Checked** by `test_name_entry`: both arrows including the down arrow's quirk, every key, the clicks, and the name in all 17 saves.
- The viewer's governor page shows the name (2026-09-16). **Not modeled:** the governor's screen's own name button.

### 38.4 MINIFONT's colour (2026-09-16)

The text routine `100F:1583` sends a glyph from the sheet at `54E0:C254` (`MINIFONT`) to `1F6F:292B` rather than the sprite blitter. That routine walks the glyph's 16-bit rows and plots each set bit through `2EF9:11A4` with the colour word `2EF9:0039` set to 0, so every MINIFONT text is colour 0 of the palette on screen. That is black in both `SHADE.256` and `WAR2.256`. The battle screen's figures (38.1) are black, not colour 1 as first drawn; the maps screen's legend (section 39) matched its capture this way. DEFINITIVE.


## 41. The Forum's screens in the original's art (2026-09-16)

Every advisor the Forum picture opens now draws the original's screen. `ui/interface.hpp` holds the shared primitives and `ui/forum_screens.hpp` the screens. `gaius_viewer` shows them whenever the files are present, and keeps its own pages as the fallback. Four of the screens have DOSBox captures, and all four match on every pixel that doesn't depend on the capture's unknown city or the mouse pointer (`test_forum_screens_art`).

### 41.1 The shared drawing

- **Blocks and sprites.** `303E:0BF0` draws a block whole; `303E:13D6` draws a sprite with colour 0 transparent.
- **Panels.** `1F6F:1EC1` is the stone panel, `2008` the inset, `2115` plain stone, and `21ED` fills with the inset's middle (frame `0xD`). All use `P_BLOCKS.PL8` (section 38.3), with the stone pattern `3496:0718` restarting at each call.
- **Text** (`100F:1583`). `FONT1` glyphs go through the sprite routine and advance 8 pixels; `MINIFONT` glyphs are plotted in colour 0 (38.4) and advance 6.
- **Numbers** (`100F:17E2`: value, digits, buffer, x, y, font, mode). They write the value's last digits into the buffer and draw the whole buffer, so the text around them is the buffer's own ("      dn", "  %", "   )").
  - Mode 0 pads with zeros.
  - Modes 1 and 2 pad with spaces, keeping the last digit, so 0 shows as "0". Mode 1 also ends the string after the digits; mode 2 leaves the rest of the buffer.
  - Characters at x 0x139 and beyond aren't drawn.
- **Repainting.** A screen's stone under changing numbers (`0x0D623`) is repainted only while `DS:0x6D8B` counts down. Some screens set it every frame (the Legion's wages, the ratings' strip); others only after a click (the Treasurer's and Tribune's rates). At rest the latter's numbers sit on the panel's own stone, and the captures confirm both cases.

### 41.2 The screens

| Figure | Screen | Routines | Checked |
|---|---|---|---|
| man in blue | the histories | `0x0ACA7` (sections 36.2) | capture: 0 of 40,440 pixels outside the bars differ, and all 10 of its bars match a height exactly |
| Treasurer | last year's accounts | `0x0A7C3` once, `0x0E6E3` each frame | capture: 0 of 59,616 outside the funds history; its 8 loss bars each match a width |
| Military Advisor | the Legion | `0x0B151` once, `0x0E167` each frame | capture: 0 of 63,424 |
| — | the funds warning | `0x084B1` | capture: 0 of 62,400 |
| man in green | the industry report | `0x09D22` (36.3) | no capture |
| Tribune | the plebs | `0x0A57A` once, `0x0E822` each frame | no capture |
| ratings | the four columns | `0x096ED` once, `0x0CC21` each frame | no capture |
| governor | his own affairs | `0x098AB` once, `0x0BDD3` each frame | no capture |

- **The Treasurer.** A 20 x 12 panel with a 5 x 10 inset, and the accounts in `FONT1` at x 0x64 with their figures at 0x104.
  - The funds history (`1F6F:2292`, `table_72`'s 17 records from the newest, rows 8 apart from y 0x1C) draws one-pixel columns 6 high from x 0x50. A loss goes left in colour 8, a column per 40 Dn up to 28; a gain goes right in colour 0, per 80 Dn up to 12.
  - `0x0D361` labels each row "bc" or "ad" with the year's three digits in `MINIFONT`.
  - The tax rates go at (0xE4, 0x14) and (0x124, 0x14), with arrows `DS:0x0404` at cells (12-13, 1) and (16-17, 1).
  - "overall gain of" or "overall loss of" is chosen by `DS:0x6BB6`'s sign; the figure is `|DS:0x6BB4|`.
- **The Legion.**
  - **Once:** a 20 x 12 panel with a 16 x 5 inset; "legion" with the province number `DS:0x6CA6`; this year's and last year's regulars, irregulars and auxiliaries; the wages and conscription labels.
  - **Each frame:** the buttons `DS:0x0094` (next Cohort (18, 2), mobilize (18, 3), previous (18, 4), wages (16-17, 9), conscription (16-17, 10)). Then the areas `1F6F:21ED` clears, and, for the Cohort numbered `DS:0x6C0A` (`0x0E54C`):
    - "prima cohors" or its number with "cohors", its emblem and state (16-character fields);
    - unless demobilized, its morale and Centuries in `MINIFONT` mode 0;
    - its standard from `SPRITE2.PL8` (at `630D:0000`): frame number × 4, plus `(DS:0x6D3A >> 2) & 3` unless demobilized.
- **The funds warning.** A 20 x 12 panel and six 40-character lines (`DS:0x085F`) in `FONT1` from x 0.
- **The Tribune.**
  - **Once:** the plebs and last year's, "Denarii spent on / Pleb welfare", and the duty rows at x 0x10, y 0x44-0xA4.
  - **Each frame:** welfare at (0x100, 0x34); each duty's plebs at x 0xEA and need at 0x110; the auxiliaries `DS:0x6C5A` / 16 into "   )"; the buttons `DS:0x0494` (welfare (13-14, 3), each duty's up and down at (12-13, 5-9), and a plain block at (16, 1) whose handler just returns).
  - **The rows' words, by label:**
    - Construction work `DS:0x6C64` with a fixed "( 50)";
    - Fire prevention `6C62`/`6C44`;
    - Building upkeep `6C60`/`6C42`;
    - Road maintenance `6C5E`/`6C40`;
    - Province `6C5C`/`6C3E`;
    - Army `6C5A`;
    - Unused Plebs `6C58`.

    `systems::plebs` calls `DS:0x6C5C` `kConstruction`, but the original's label for it is "Province", and its "Construction work" row shows `DS:0x6C64`. Recorded here; the code keeps its names.
- **The ratings.** `TEMPLE.VPX` with `TEMPLE.256`.
  - For each rating (x 0x18, 0x68, 0xBA, 0x10C), `0x0D069` stacks `TEMPLBIT.PL8` pieces up from y 0x79, 10 apart, one for every 10 points rounded up: the base (frame 2), shafts (1), and the capital (0) as the tenth.
  - The percentages go at y 0xA0.
  - Each frame the stone strip `0x0D623` (18 x 1 at (0x10, 0xB4)) is repainted, with the average ("      Average Rating     %", the figure at 0xC6) or the advice (36.4) at y 0xB8.
- **The governor.** `C_VITAE.VPX` in the interface palette, and a 13 x 12 panel at (0x70, 0). On it: the name (8 px a character from (0xA4, 0x14), `0x0C373`); the rank and province (16-character fields at x 0x94); "of"; the savings into "      dn"; the Imperial favour into "  %"; the salary; the donation labels; and the five buttons `DS:0x0444`:
  - (18, 1) the name dialog (section 40);
  - (18, 2) the promotion requirements (`0x0BE7C`: "demotes to", "promotes to", "average rating of", "minimum ratings of");
  - (18, 4) the map (37);
  - (18, 8) and (18, 10) the salary and donation dialogs.

  The three dialogs are in section 41.4.

### 41.3 In Gaius

- **Clicks.** The viewer's screens answer their buttons as the original does. Any other click, or a right-click, goes back to the Forum picture; the original leaves on a right-click only.
- **Timing.** The ratings advice shows for 90 frames.
- **Not modeled:** the sounds. (The pressed frames came with section 42.1.)

### 41.4 The governor's three dialogs (2026-09-16)

Each is drawn over the governor's screen, twice (both pages), then waits: every frame it runs the input and page flip, and only a right click (`DS:0x6D4C`) ends it, after which `0x098AB` redraws the governor's screen. `ui::compose_governor_dialog` draws them; `test_governor_dialogs` checks that each draws only inside its panel.

- **The promotion requirements** (`0x0BE7C`, button (18, 2)). A 14 x 5 panel at (0x30, 0x40). In `FONT1` at x 0x42: "demotes to" (y 0x4C), "promotes to" (0x58), "          on" (0x64), "average rating of      %" (0x70), "minimum ratings of     %" (0x7C).
  - The ranks go at x 0xA2 through `100F:142C`, which copies a given number of characters from an offset into `DS:0x1124` and draws them up to the first NUL. Here that's 16 characters of the rank table `DS:0x7102` from (rank − 1) × 16 and (rank + 1) × 16. Neither end is guarded, so at rank 0 "demotes to" reads the 16 bytes before the table, the province toolbar's " Go to City    " (a NUL ends it), and at rank 20 "promotes to" starts on a NUL and shows nothing.
  - The figures are `3496:01C6 + rank × 2`, the average and the minimum (`administration::kPromotion`), 4 digits in mode 1 at x 0xDA. They are the current rank's requirements.
- **The salary** (`0x0C06A`, (18, 8)) and **the donation** (`0x0C17D`, (18, 10)). A 10 x 3 panel at (0x50, 0x50). Each frame:
  - the buttons, `DS:0x0144` for the salary and `DS:0x0124` for the donation: the up arrow (frame 0x12, pressed 0x1B) at cell (12, 6) and the down arrow (0x13, 0x1C) at (13, 6), momentary. The salary's are `0x0C164`/`0x0C171` (0-9999), the donation's `0x0C2C3`/`0x0C2D1` (0 up to the savings `DS:0x6C2E`), as `forum::adjust` already had them;
  - the stone under the figure, `0x0D623` 3 x 1 at (0x60, 0x60), with `DS:0x6D8B` = 2 so it repaints every frame;
  - "Dn" (`DS:0x0C43` / `0x0C46`) at (0x9C, 0x64);
  - the figure, `DS:0x6C2C` in 5 digits at (0x68, 0x64), or `DS:0x6C28` in 4 digits at (0x64, 0x64), mode 1.
- **Paying the donation** (`0x0C26E`). When its dialog ends, a donation no larger than the savings leaves them and adds 90% to the treasury (`economy::donate_savings`); a larger one does nothing. The amount stays set for next time.
- **In Gaius.** Any click that isn't an arrow ends a dialog too, so touch can leave it.

## 42. The Options screen, the buttons and the key words (2026-09-17)

### 42.1 How a button works (`0x0D41D`, `0x0D521`)

Every interface screen keeps its buttons as 16-byte records -- cell x, cell y, frame, pressed frame, a far pointer to the handler, a state word (+0x0C) and a mode (+0x0E) -- and each frame runs `0x0D41D` over the table, then draws it with `0x0D521`. `ui/buttons.hpp` transcribes both.

- **The mouse** (`0x113E1`, each frame, from `2EF9:002B`): `DS:0x6D56` the left button held, `DS:0x6D54` the right held, `DS:0x6D4A` / `0x6D48` either just pressed, `DS:0x6D4E` / `0x6D4C` either just let go. The dialogs that "end on a right click" wait for `DS:0x6D4C`, the right button let go.
- **Answering** (`0x0D41D`), for the button under the pointer:
  - **mode 0, momentary:** while the left button is held it acts at once, sets its state to `DS:0x6D62` (10; 25 on the start screen) and counts `DS:0x6D64`; after 12 held frames it acts again whenever its state has fallen below 2;
  - **mode 1, toggle:** acts once a press, flipping its state;
  - **mode 2, radio:** acts every held frame and becomes `DS:0x6D60`;
  - **mode 3, release:** acts when the left button is let go over it, with state 5.
  With no button held `DS:0x6D64` goes back to 0.
- **Drawing** (`0x0D521`), each button whole (`303E:0BF0`) from `P_BLOCKS.PL8`: a toggle pressed while its state is set, a radio button while it is the chosen one, and the others while their state counts down or while held over -- counting the state down each time.
- **The tables:** the Treasurer's `DS:0x0404`, the Tribune's `DS:0x0494`, the Legion's `DS:0x0094`, the governor's `DS:0x0444` and its dialogs' `DS:0x0144` / `0x0124` are all mode 0 (`ui::treasurer_buttons` and its siblings). The Options menu's are mode 3.
- **Checked** by `test_buttons` (each mode) and `test_options_screen`, which draws each advisor's table at rest over its composed screen and finds nothing changed, so each table is the buttons its screen draws.

### 42.2 The Options screen (`0x0ECA3`)

The control panel's "Game Options" (command 40, `0x17390`: `DS:0x6D0C` = 40, then `0x0ECA3`) plays `czarjin5.xmi` (`DS:0x0D46`), draws the screen once (`0x0B47D`) and runs the menu (`0x0ECDF`) until a right click or a handler ends it.

- **The screen** (`0x0B47D`): a 20 x 12 panel over the whole screen in the interface palette, "Caesar - Options screen" at (0x20, 0x0C), and the nine items at x 0x20 (`DS:0x708E`-`0x706A`).
- **The menu's buttons** (`DS:0x0564`, cells (15, 2)-(15, 10), frames 0x1D / 0x1E, mode 3):

| Item | Handler | What it does |
|---|---|---|
| Resume game | `0x0F0CC` | ends the menu |
| Game speed | `0x0F0D3` | the speed dialog |
| Sound effects | `0x0F257` | the sound dialog |
| Load a game | `0x0EE07` | the file dialog `0x0C558` on `*.sav`, then the loader `0x14537` |
| Save a game | `0x0EE93` | the file dialog, asking before overwriting (`DS:0x703A`), then the writer `0x133C8` |
| Display options | `0x0EF28` | the display dialog |
| Pause the game | `0x0F01E` | draws the view each frame without steps (`DS:0x6CAC` = 1) until either button is held or T or Escape is pressed (`2EF9:0033` = 1), then ends the menu |
| Restart the game | `0x0F0AD` | asks "Restart the game?" (`0x0F43F`); OK sets `DS:0x6D6F`, which ends the menu into a new game |
| Exit to DOS | `0x0ED2C` | asks "Are you sure ?!"; "Resume game" (`0x0EDE3`) ends the menu, "Exit to DOS" (`0x0EDF0`) also sets `DS:0x6D70` and `DS:0x6D6F` |

- **The dialogs.** The speed, sound, display and leaving dialogs start with `0x0F5AC` (a 16 x 5 panel at (0x20, 0x30)) or `0x0F5EA` (16 x 6), whose `100F:00EF` clears both pages to colour 0 (`2EF9:0C50`). Each redraws its text and buttons every frame and ends on its OK or a right click; then `0x0B47D` draws the menu again.
  - **Speed** (`DS:0x0634`): "Game speed" and "Scroll speed" at x 0x40; a 2 x 2 stone at (0xE0, 0x40); the figures in 3 digits, mode 2, into "    %" (`DS:0x0D5F` / `0x0D65`) at x 0xE8. The arrows at (12-13, 4) and (12-13, 5) move `DS:0x5292` and `DS:0x5294` in tens within 0-100.
  - **Sound** (`DS:0x0684`): "Allow effects", "Allow tunes", "City sounds"; a 2 x 3 stone at (0xE0, 0x40); "Yes" or "No" at x 0xE0. The buttons at (12, 4)-(12, 6) flip `DS:0x5296`, `DS:0x5298` (turning it off stops the music, `31E0:039D`) and `DS:0x529E`, which reads "No" when set.
  - **Display** (`DS:0x05F4`): "Cancel Position indicator" (from x 0x30), "Cancel icon name", "Cancel messages", with toggles at (16, 4)-(16, 6). Their states are kept in the records themselves (`DS:0x0600`, `0x0610`, `0x0620`): the handlers copy them to `DS:0x529A` (the minimap's position marker, `0x21EBF`), `DS:0x529C` (the control panel's name of the icon under the pointer, `0x21C49`) and, flipped, to the messages option `DS:0x6C78`.
  - **The restart question** (`0x0F43F`) is drawn over the menu: a 14 x 5 panel at (0x30, 0x70), the question at (0x50, 0x80), OK and Cancel at x 0x40 with buttons `DS:0x06E4` at (15, 9) and (15, 10). It waits for a button (`DS:0x6D46` 1 or 2) and ignores the right button.
  - **Leaving** (`0x0ED2C`, also Alt-X from the key handler `0x2EC8A`): "   Are you sure ?!" at (0x40, 0x40), "Resume game" and "Exit to DOS" with buttons `DS:0x06C4` at (15, 5) and (15, 6).

### 42.3 `CAESAR.INF`: where the options live

The start-up `0x0F628` reads 28 bytes of `caesar.inf` (`DS:0x0D6B`) into `DS:0x5288` and exits with "No installation information found." without it; `0x0F8E2` and `0x223B3` write them back. As 14 words:

| Word | DS | US release | Meaning |
|---|---|---|---|
| 0-4 | `0x5288`-`0x5290` | 0, 3, 0, 1, 0 | the installer's sound set-up; `DS:0x528A` is copied to `DS:0x6DE5` |
| 5 | `0x5292` | 100 | the game speed (section 35.2) |
| 6 | `0x5294` | 100 | the scroll speed |
| 7 | `0x5296` | 1 | allow effects |
| 8 | `0x5298` | 1 | allow tunes |
| 9 | `0x529A` | 0 | cancel the position indicator |
| 10 | `0x529C` | 0 | cancel the icon name |
| 11 | `0x529E` | 0 | city sounds off |
| 12-13 | `0x52A0`-`0x52A2` | 7, 0x220 | more of the sound set-up (0x220 is a Sound Blaster's port) |

This corrects section 35.2, which guessed `csr0.dat`.

### 42.4 The key words

`2EF9:029B` reads a key when one is waiting (`int 16h`): it moves the last character `2EF9:002F` to `2EF9:0031`, then stores the new character in `2EF9:002F` and its scan code in `2EF9:0033`. So `2EF9:0031` is the character typed before the last one, and the statue's gate (section 36.1) is "c" followed by "B".

The key handler `0x2EC40` also has: Alt-X leaving (`0x0ED2C`), "p" / "P" flipping the position indicator, "t" / "T" pausing (`0x0F01E`) and "s" / "S" a screen of its own (`0x0E7F6`, not read).

### 42.5 In Gaius

- **`ui::compose_options_screen`**, `ui::options_buttons` and `ui::options_dialog_button`; `ui::GameOptions` with `load_options` / `save_options` for `CAESAR.INF`.
- **`gaius_viewer`** has an Options tab beside City, Province, Maps and Forum.
  - Every button of the Options screen and of the Treasurer's, Tribune's, Legion's and governor's screens runs through `ui::process_buttons` once a frame, so buttons show their pressed frames and arrows repeat while held.
  - The settings are Gaius's own `caesar.inf` in the per-user data folder, first copied from the game's, so the original's file is never written. The game speed takes effect at once; `--speed` overrides it.
  - Load and Save open Gaius's slot page in place of the original's file dialog.
  - Pause stops time until a click or T. Restart goes to the start screen. Exit to DOS closes the viewer.
  - The statue opens after "c" then "B".
- **Stored but without effect yet:** the scroll speed, the sound switches (Gaius plays no sound yet), and the position indicator and icon name, which belong to the original's control panel.
