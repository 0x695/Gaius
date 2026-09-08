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
