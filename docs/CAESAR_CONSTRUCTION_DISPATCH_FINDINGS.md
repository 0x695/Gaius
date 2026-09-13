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
- The event routines behind the per-scan counter thresholds (`0x2C525`, `0x2C54E`, `0x2C4EA`), and where the thresholds come from.
- The housing grade-change routine; the identities of `0x94`/`95`, `0xA2`-`B2`, `0xB9`/`BB`/`BC` and `0xF5`/`F6`; what sets `C9D4.01` and `DS:0x6BF8`. -- the grade-change routine is answered in section 16.
- What reads the C9D4 service bits.

## 16. Housing, read directly -- and a correction to section 15 (2026-09-13)

**Correction first.** Sections 11 and 15 treated the far pointers from `DS:153A` onward as one table indexed by tile id, and 15.1/15.4 concluded that its entries `0x00`-`0x35` -- including `0x297AC`, "tile `0x00`'s handler" -- were dead code. They aren't. `DS:153A` and `DS:1212` are adjacent tables: the monthly service scan (`0x2BBBB`) indexes `DS:153A` only for tiles above `0x35`, the housing development pass (`0x294CF`) indexes `DS:1212` only for tiles `0xC8` and up, and `DS:1212 + 0xCA*4` *is* `DS:153A`. So the slots read as "tiles `0x00`-`0x35`" are `DS:1212`'s entries for tiles `0xCA`-`0xFF`, verified byte for byte, and `0x297AC` is the development handler for housing tile `0xCA`. `CAESAR_CITY_STATE_v5.md` made the same misreading, which is why it agreed with section 11 22-for-22. What remains true: tiles `0x00`-`0x35` are never simulated by either table, and in real saves they are static terrain.

### 16.1 The month

A month is 106 steps, counted in `DS:0x6D9D` (dispatcher at `0x2936A`):

- **Steps 0-99:** the housing development pass for row = step (`0x294CF`), then five other per-row routines (`0x2D2F5`, `0x2CE7C`, `0x2CD1C`, `0x2E209`, `0x2CD10`) that haven't been read.
- **Steps 100-105** (jump table at `0x29466`): 100, the reset `0x2C8D3` and `0x2DA0D`; 101, population and water (`0x2C93F`) and `0x28215`; 102-105, the four quarter scans (`0x2BBBB` from rows 0, 25, 50 and 75), the last also publishing the scan counters and calling six monthly routines.
- **Rollover** (`0x29476`): at step 106 the step resets and month `DS:0x6C1C` advances; at 12, month resets, year `DS:0x6C32` advances, `DS:0x6C7C` is set to 80 and `0x28238` runs. `DS:0x6D9B` counts months mod 18, and `0x2DA0D` only runs while it is 0.

Across the four saves the month reads 9, 1, 4, 6 and the year -11, -7, -2, -1 -- consistent with BC dates (STRONG INFERENCE). Because services are rebuilt at the end of a month and houses develop during the next, houses always react to last month's services.

### 16.2 Population and water (`0x2C93F`, step 101)

- **Population units** `DS:0x6C10` are the sum, over every tile `0xC8`-`0xD7`, of the per-cell table at `3496:007E`: `1 1 2 3 3 5 6 5 6 4 4 3 3 2 2 1`. `DS:0x6C0E` is four times that. It matches the saves exactly (0, 63, 146) except `CAESARVX.SAV`, which is 2 high -- that save holds exactly one `0xCF`, the +2 of a `0xCB` -> `0xCF` upgrade made after the count.
- **Water, `C9D4.01`:** wells (`0xB8`) radius 1, reservoirs (`0xA4`) radius 3, and fountains (`0xB9`-`0xBD`) radius 6 when a supply check (`0x2CAE6`, which traces pipes through `0x2CBF1`) succeeds; the same check flips fountains between working `0xB9`/`0xBB` and dry `0xBA`/`0xBD`. It matches the saved `C9D4.01` 10000/10000 in all four saves -- which contain wells and dry fountains, but no reservoir or working fountain.

### 16.3 The development pass (`0x294CF`)

For each cell of its row:

- **Below `0xC8`:** land value is zeroed. A jump table at `0x295F8` sends `0xB9`/`0xBA` to `0x2BACE` (coverage > 10 and population > 50 -> `0xBD`, or `0xBB` if watered), `0xBB`-`0xBD` to `0x2BB48` (coverage < 10 -> `0xBA`, or `0xB9` if watered), and `0xA8`/`AB`/`AE`/`B1` to `0x29624`, which either decrements the tile or picks one of eight neighbours from a table at `3496:1DE4` and, if that neighbour is housing, calls `11C6:0A5A` -- both driven by the RNG in segment `2EF9`.
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

The generator has 45 call sites. In the monthly simulation path exactly five draw each month:
- `0x2E209`, only at step 80 — after that step's housing row, so rows 0-80 share one growth value and rows 81-99 the next.
- `0x2DF7D`, four draws at step 105. Each one rolls `0286` against a threshold (`DS:0x6BE0`, `0x6BE2`, `0x6BE4`, `0x6BDE`) and, if it passes and a count is nonzero, halves `028C` until it fits that count to pick a target. That looks like the monthly event roll, but it isn't modeled.

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
  - Each footprint cell, in row order, becomes rubble `0xA7 + (2EF9:0286 & 3)` after one random draw, with `7BB4` zeroed.
  - Temples (`0xE0`-`0xE7`), `0xF5`/`0xF6` and `0xEF` are first removed from runtime tables of 16-, 24- and 12-byte records (`DS:5BA4`, `DS:585C`, `DS:5B2C`, 30 entries each) by `0x1287C`, `0x12963` and `0x12A94`.
  - Those record sizes × 30 are 480, 720 and 360 bytes. The first two match the save's unidentified `table_480` and `table_720`: very likely the same data (STRONG INFERENCE).
  - This isn't modeled.

### 18.4 Validation

Roads can be checked against the four real saves directly. Every road piece (`0x36`-`0x40`) is reset to `0x1D`, and `place_road` is called on those cells in row order (`test_construction_road_rebuild_real_saves`). That rebuilds 40/40, 90/90, 137/141 and 149/153 road tiles, with nothing refused and no other cell touched.

The four misses are the same cells in `CAESARVX` and `CAESARUX`, and `CAESARWX` rebuilds that area exactly. In between, road cells next to those junctions were built over (cell (21,80) went from a road corner to a well), and Clear Area doesn't re-tile a cleared road's neighbours. The junction shapes are history that a rebuild from the final grid can't reproduce. Walls, plazas and clearing have no equivalent evidence in these saves (they contain no walls), so they're pinned by `test_construction_drag_rules` against the lifted code.

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

### 19.3 The economy at step 101 (`0x28215`)

After population and water, four routines turn the month's state into the next month's housing inputs. They're given as formulas in `systems/month.hpp`, with their five tables in segment `3496` (`006E`, `0136`, `014B`, `017E`, `01B1`) embedded.
- **`0x28621`** gives `DS:0x6BF4`, from population, the workshop count and `DS:0x6C36`.
- **`0x28694`** gives a 0-100 share `DS:0x6BCC`, and `DS:0x6BFA` = share ÷ 5. The share is what's left of the population after `DS:0x6C06` percent, workshops × 20, forums × 30 and the month's scan counts (`DS:0x6BEA` × 30 + `DS:0x6BEE` × 12, times `DS:0x6BE8` / 4 + 1). It uses the C runtime's 32-bit multiply and divide.
- **`0x28800`** gives `DS:0x6BF8`, the housing coverage base, from `DS:0x6C04` and `DS:0x6BFA`.
- **`0x28826`** gives `DS:0x6BF6`, the land-value growth base, from `DS:0x6C04` and `DS:0x6C06` / 10.
- Then `DS:0x6C00` += `DS:0x6C04`.

Recomputed from each real save's own inputs, all five outputs match in all four saves (`test_month_economy_matches_saves`). `systems::month::run_month` on a whole save now runs this, so the month no longer relies on the bases read from the save. What `DS:0x6C04` and `DS:0x6C06` are to the player isn't established.

### 19.4 The rest of the month, classified

- **The other per-row routines spawn walkers:**
  - `0x2D2F5` from forums;
  - `0x2CE7C` from workshops: one record per row, rows 25-54, computing a 0-7 production level from several city globals and spawning actor kind 8;
  - `0x2CD1C` from barracks: rows 75-84, kind 4.

  All go through the actor allocator `0334:28F9`.
- **Actor movement** is a two-level dispatch in `0x23C52`: actor type through `DS:0x134C` (30 handlers), then state (+0x31) through `DS:0x1384` (16 handlers). It isn't transcribed yet.
- **Province-level events:** `0x2E249`/`0x2E220` (step 80) cycle province-map tiles `0x4C`/`0x79`/`0x7A`/`0x61`.
- **Ratings and messages:**
  - the step-105 routines `0x2E0BE`, `0x2DC72`, `0x2DEC8`, `0x2DD21` and `0x2DE0F` work on province, ratings and message globals;
  - so do the 18-month `0x2D6F4` and `0x27BA1` (population milestones, with flags in `table_10`).

  None touches the city grid layers; they belong to Phases 6-7.
