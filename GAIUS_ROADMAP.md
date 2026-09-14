# Gaius — Roadmap

The plan for building Gaius, phase by phase, against the state of the reverse engineering (RE). The scope and architecture are in `GAIUS_MASTERPLAN.md`; the RE sources are `CAESAR_REVERSE_ENGINEERING_COMPLETE.md` (outside this repo) and the findings in `docs/`.

RE and engine work run in parallel: when a phase waits on an open RE question, the next phase's unblocked parts should already be underway.

**How each phase reads:** a status line, the goal, what's done, what's open, the deliverable, and the **RE blockers** — RE questions that must close before the phase can *finish* (not before it can start). History, corrections and dead ends sit in a collapsed **History** block at the end of each phase.

---

## At a glance

*As of 2026-09-14.*

| Phase | Area | Status | What's left |
|---|---|---|---|
| 0 | Repo & format library | **Done** | — |
| 1 | Viewer & platform skeleton | **Done** | Android: real-device ABI, viewer port |
| 2 | Data model & save round trip | **Done** | Meaning of some global words |
| 3 | Service propagation | **Done**, validated | — |
| 4 | Housing & population | **Done**, validated | — |
| 5 | Construction & build mode | **Done** | Touch drag gesture; see [Still open in Phases 0-5](#still-open-in-phases-0-5) |
| 6 | Economy & military | **Systems done** | Province view and battle screen in the viewer |
| 7 | Forum, advisors, ratings | **In progress** — plebs, ratings, promotion done | New-province start, Forum UI, maps panel |
| 8 | Format completeness & save write-back | Started | Save loader, formats, music, terrain generation |
| 9 | Packaging & polish | Not started | Everything |
| 10 | Editor & IGDK integration | Not started | Everything |

**Validation so far:** 17 real saves from three play sessions. The simulation reproduces the engine's saved layers cell for cell, a month of steps reproduces six consecutive saves, and the yearly accounts reproduce every save's last year. `gaius_tests`: 3344 checks.

**Critical path now:** Phase 7's new-province start, then the Forum UI and a province view, which together make the single-player loop playable. Phase 9 is unblocked alongside.

---

## Phase 0 — Repo scaffolding & format library

**Status: Done.** See `docs/FORMATS.md` for what each decoder covers and how it's tested.

**Goal:** stand up the repo and port the validated Python prototypes into a C++ library, as IGDK's Phase 0 did for `sgformat`.

**Done**
- [x] **Repo** — `gaius/`, CMake build, VS Code config (mirroring IGDK's setup).
- [x] **`formats/vpx`** — ported from `caesar_vpx.py`. `EMAP2.VPX` matches `EMAP2_decoded.png`: 0/64000 pixels differ (golden-image test).
- [x] **`formats/p32`, `formats/pal256`** — palettes. `.P32` checked through the VPX golden test; `.256` against all eight real files, their `.P32` pairs, and `CSR.EXE`, which sends the bytes straight to the VGA DAC (2026-09-13).
- [x] **`formats/pl8`** — sprite sheets. Checked against the documented `HOUSES.PL8` example; found which header word is the frame count and the trailing placeholder frames.
- [x] **`formats/empire2`** — all 50 `EMPIRE2.0xx` files round-trip byte-identical.
- [x] **`formats/save`** — block table, contiguous and size-correct. Validated against real saves since 2026-09-12 (`test_save_corpus_real`); skips cleanly without them.
- [x] **CLI tools** — `dump_vpx`, `dump_pl8`, `empire_view`, `save_inspect`, smoke-tested against real files.
- [x] **`bindiff_exe`** — run against both `CSR.EXE` builds. Along the way: the EXEPACK decompressor reverse engineered from its stub and checked byte-exact on both builds, plus new game text (the 21-rank promotion ladder, ~60 province names, cohort status words, the workshop goods list). See `docs/CAESAR_EXEPACK_AND_STRINGS_FINDINGS.md`.

**Deliverable:** the library, CLI tools and docs (`FORMATS.md`, this roadmap). **Met.**

**RE blockers:** none — everything here was already proven.

---

## Phase 1 — Static content viewer & platform skeleton

**Status: Done.** Code in `platform/`, `apps/viewer/` and `android/`.

**Goal:** prove the asset pipeline end to end with SDL2 — an EMPIRE2 scenario and a save's city on screen — on more than one platform from the start, so cross-platform assumptions are tested before game logic depends on them.

**Done**
- [x] **Rendering through a logical framebuffer** (masterplan 5a), never a fixed-pixel blit; letterboxing in `platform/window.cpp`. The game's own city sprites draw since 2026-09-13 (`render::render_city`), once screenshots showed the city palette is `SHADE.256` and PL8 pixels are four interleaved streams.
- [x] **Window modes** — windowed, borderless and fullscreen switching; resize; letterbox coordinate mapping. Verified under `SDL_VIDEODRIVER=dummy`.
- [x] **Per-OS paths** — `platform/paths.cpp` via `SDL_GetPrefPath`.
- [x] **Android build** (2026-09-08) — see `android/README.md`. The SDL2 `android-project` template builds `apps/android_hello/` against the unmodified platform layer; runs on the `Medium_Phone` emulator (x86_64), confirmed by logcat and `adb screencap`.
- [x] **Empire-map viewer** — all 50 scenarios, with the same terrain classification as `empire_view`; checked at three zoom levels (`--test-zoom`/`--test-pan`).
- [x] **Save viewer** (2026-09-08) — `apps/viewer/save_view.hpp`: the 100×100 tile grid and four service layers as heatmaps; the file size picks the mode (1602 bytes EMPIRE2, 57126 `.SAV`). Real saves open since 2026-09-12, and the first view is the city in the game's own sprites, with walkers.
- [x] **Input abstraction** (masterplan 5a point 4) — `platform/input`: mouse, touch and gamepad all become one `Command` stream.

**Open**
- [ ] **Android on a real device** — only the `x86_64` emulator ABI is built; widen `abiFilters`.
- [ ] **`gaius_viewer` on Android** — asset loading needs `AAssetManager` (an APK can't be `fopen`ed).
- [ ] **Touch gestures** beyond single-finger drag and tap (pinch zoom, two-finger secondary) — undesigned.

**Deliverable:** a "look at your data" tool, the base of the later editor, and the first proof that the resolution and input abstractions hold cross-platform. **Met**, with Android limited to the emulator.

**RE blockers:** none. The tile→sprite rules, once tracked as tech debt, were transcribed on 2026-09-13 and match DOSBox captures pixel for pixel (`docs/CAESAR_CITY_RENDERER_FINDINGS.md`).

<details>
<summary>History</summary>

- The Android SDK was thought unavailable at handoff; that was the previous environment. This machine had Android Studio; the NDK came from `sdkmanager`.
- Before real saves existed, the save viewer was verified against a synthetic in-memory `SaveFile` (`test_save_view_render_synthetic`) and showed raw-byte heat gradients only.
- PL8 atlas assembly waited on the in-city palette until the 2026-09-13 screenshots.

</details>

---

## Phase 2 — Normalized data model

**Status: Done** (2026-09-08); validated against real saves from 2026-09-12. Code in `model/city_state.hpp`/`.cpp`.

**Goal:** the in-memory model from masterplan section 5 (`CityMap`, `EmpireMap`, `Actor[70]`, service layers), with a save fully loaded into it.

**Done**
- [x] **`model::CityState`** — `CityMap` holds the five save-backed grids (tiles and four layers); `EmpireMap` is reused from `formats::empire2`. The corpus sketch's `coverage_limit` grid is left out: no save block backs it.
- [x] **Loading** (`model::load`) — all 20 blocks: 7 into typed fields (5 grids, actors, the embedded EMPIRE2 map), 13 kept as byte vectors whose contents are identified (dispatch findings section 22) and read through `model::global_word` and record offsets.
- [x] **Round trip** (`model::serialize`) — byte-identical on all 17 real saves (`test_save_corpus_real`), and on a synthetic non-repeating buffer that also spot-checks positions against the original bytes (`test_model_city_state_round_trip`).
- [x] **Actor table** — `model::Actor` wraps the 50-byte record (every field laid out in `systems/actors.hpp`, dispatch findings section 20); `coord_space()` splits city types (< 11, `row*100+col`) from province types. For city actors use `packed_xy`: `raw_x/raw_y` hold the walker's destination.

**Open**
- [ ] **What some global words mean to the player** — a few words are read and written correctly but not yet named.

**Deliverable:** a save editor's backend — load, inspect every field, re-save byte-identical. **Met**; `save_inspect` prints blocks, global words with their DS addresses (`formats::save::kGlobalWordDsAddress`) and actors.

**RE blockers (soft — fidelity, not the round trip):**
- **The save loader** isn't fully read. Its read order matches the writer's, and it rebuilds `DS:0x6BFE` (2026-09-14, dispatch findings section 25.2); what else it does after reading isn't traced. Tracked in Phase 8.

<details>
<summary>History</summary>

- 2026-09-12: the save *writer* was read. `global_words_128` isn't a descending DS range — the old formula was wrong for 107 of 128 words (`docs/FORMATS.md`).
- 2026-09-13: every block's runtime address mapped. `table_480` forums, `table_120` barracks, `table_720` workshops, `table_8` workshops per goods, `table_10` population milestones, `table_50` provinces given, `table_60_a-d`/`table_72` yearly histories, `final_state` named words.
- 2026-09-14: the loader's read order, and the economy words named (findings section 25).

</details>

---

## Phase 3 — Simulation core: service propagation

**Status: Done and validated against the original engine** (2026-09-13). Code in `systems/service.hpp`/`.cpp`; derivation in dispatch findings section 15.

**Goal:** the service layers at algorithm level — `A2C4` coverage, `54A4` land value, `C9D4` flags, `7BB4` derived state — and every building handler that feeds them.

**Done**
- [x] **Propagators** — `apply_coverage` (square radius; the ceiling is a per-cell running minimum in `ServiceState`), `apply_land_value` (no floor; clamps only to the caller's ceiling), `apply_flags`. The −8..+50 clamp is a separate per-cell routine, `evolve_land_value` (`0x2DA7E`).
- [x] **`reset_tick`** — `C9D4 &= 0x12`, `A2C4 = 0`, and the per-cell coverage ceiling back to 63. Runtime-only state, not saved.
- [x] **The C9D4 bit table** — `kC9D4BitTable`, each bit with its producers and a confidence label (`test_service_bit_table_fixtures`).
- [x] **Every `DS:153A` handler** — transcribed from the disassembly, with `dispatch_tile()` and the engine's ≤ `0x35` gate.
- [x] **Proof** — `test_save_corpus_simulation`: one reset and one dispatch pass over each real save reproduce its A2C4 layer and every C9D4 service bit, **10000/10000 cells**.

**Deliverable:** given a city grid with buildings, compute coverage, land value and flags exactly as the executable does. **Met and proven.**

**RE blockers:** none left.
- C9D4 producers are confirmed by disassembly: `0x04` Bath Houses, `0x08` Market, `0x40` School and Hospital, `0x20` Forum and Prefecture (renamed "administration" on 2026-09-14, when the population tax turned out to read it); `0x02` is derived from `0x10` by `0x2DA0D`.
- Heavy Industry (`0xF3`) and Market (`0xF4`) run through this dispatcher; their economic side is Phase 6's.

<details>
<summary>History</summary>

- The first version was built from RE-corpus parameter tables and was wrong in several places, though every unit test passed — the tests encoded the same wrong values. `tools/sim_check` against real saves found all of it in one run.
- It skipped Barracks, School, Oracle, Plaza and Prefecture for lack of documented parameters rather than invent numbers. All four real ones are now transcribed; "Plaza" was never a `DS:153A` handler (its supposed tile, `0xF3`, is Heavy Industry).
- Five handlers had wrong parameters *and* names (`0xEE` Prefecture, `0xEF` Barracks, `0xF3` Heavy Industry, `0xF4` Market, `0xEB` Oracle); nine handlers were missing entirely.
- `0x20` was first "religious" (HIGH CONFIDENCE in the corpus), downgraded when Prefecture was found setting it, renamed when its consumer was found.

</details>

---

## Phase 4 — Housing & population

**Status: Done and validated** (2026-09-13). Code in `systems/housing.hpp`/`.cpp`, `systems/month.hpp`/`.cpp`, `systems/actors.hpp`/`.cpp`; derivation in dispatch findings sections 16-21.

**Goal:** the residential state machine, land-value gating, and population from housing.

**Done**
- [x] **Sixteen housing grades** — tiles `0xC8`-`0xD7`, every promotion, demotion and merge (pairs, 2×2, 3×3) in `develop_building`. That these are the manual's sixteen grades is strong inference; the rules are exact.
- [x] **`land_value_allows`** (`0x2DB49`) — the collapse into `0xA7`, and its rioter and globals (`actors::spawn_rioter`).
- [x] **Population** — the per-cell table at `3496:007E`; `DS:0x6C10` holds the sum, `DS:0x6C0E` four times it.
- [x] **The month** — `systems::month`: the 106-step dispatcher (`0x2936A`), the calendar (`0x29476`), and the engine's random number generator (`2EF9:1425`) with its draws. `gaius_viewer` runs it.
- [x] **Water and step 101's economy** — `service::apply_water` with the fountain pipe tracer; `month::run_economy` sets the housing coverage and land-value growth bases (findings section 19).
- [x] **Walkers** — `systems::actors`: allocator, movement around obstacles, city behaviour states, and the forum, workshop and barracks spawners (findings section 20).
- [x] **Fire, collapse and road wear** — the scan counters, the step-105 roll (`0x2DF7D`), `construction::demolish`/`burn`, burning tiles `0xA8`-`0xB1` (findings section 21).
- [x] **A month against the original** (2026-09-14) — six saves a month or less apart; `run_step` reproduces each next save's tiles, record tables, population and every land-value cell (`test_month_consecutive_saves`, findings section 24).

**Deliverable:** houses that grow, merge, shrink and split exactly as the engine's handlers dictate, with real population numbers. **Met and proven.**

**RE blockers:** none left.

<details>
<summary>History</summary>

- The first version rested on a misread table: tiles `0x00`-`0x15` looked like housing but the engine never dispatches them. `DS:1212`'s entries for `0xCA`-`0xFF` had been read as `DS:153A` tiles `0x00`-`0x35` (findings section 16).
- Blockers that closed on 2026-09-13: the tile-to-grade mapping (`0xC8`-`0xD7`), the population table (a placeholder until `3496:007E` was found), and the "21 untraced handlers" (obsolete — those tiles are never dispatched).
- The last untranscribed piece, routine `0x29624`, turned out to be fire.

</details>

---

## Phase 5 — Construction system & the dispatcher

**Status: Done** (2026-09-12; its last limits closed 2026-09-13/14). Code in `systems/construction.hpp`/`.cpp`, `ui/`, `apps/viewer/`; derivation in dispatch findings sections 1-14 and 18-19.

**Goal:** close the highest-value RE question — construction command → tile write → footprint — and wire it into a build mode.

**Done**
- [x] **The construction dispatcher** — `DS:127C` (flat `0x75ACC`), 44 far pointers in segment `0x11C6`, called as `lcall [bx + 0x127c]` with `bx = command_id * 4` (findings section 12). `[0x6D0C]` holds the selected command, `[0x6D0A]` the handler's success flag.
- [x] **Command IDs** — definitive: the 34-entry string table decoded byte-exact, and ~30 toolbar handlers assign the same IDs as literals (`CommandId`/`kCommandNames`, golden test against the executable).
- [x] **Placement** — seed tile, footprint and terrain gate (`0x1D` ≤ existing tile ≤ `0x35`) for every placing command (`placement_spec`, `can_place`, `place`, which writes each cell's part index to `7BB4`).
- [x] **Drag-built commands** — Road, Wall, Plaza, Clear Area with their neighbour rules; every real save's road network rebuilt cell by cell (findings section 18).
- [x] **Variant commands** — Forum grades and Workshop goods (`place_forum`/`place_workshop`, findings section 19.1).
- [x] **Building table corrections** — the civic tile IDs fixed against the real table, and `0xEB` Oracle, `0xEC` School, `0xEE` Prefecture identified.
- [x] **Build mode in `gaius_viewer`** — a tool ring (Tab / gamepad X), placing by left-click / tap / gamepad A through one `Select` command, mouse drag for drag-built commands, and the original's drag cancel with a refund (right button while the left is held). Headless hook: `--test-build T X Y`.
- [x] **Toolbar** (masterplan 5a point 3) — four breakpoints derived from one `scale`; `render()` and `hit_test()` share `button(i)`, so a drawn button and its hit target can't drift apart (a test hits every button's centre and corners at every breakpoint). Game icons (`POINTERS.PL8`) and font (`FONT1.PL8`); the label shows the cost and current funds.
- [x] **Construction costs** (2026-09-14) — `systems::economy`, charged by the viewer (Phase 6).

**Deliverable:** a playable slice — build roads, water, a forum, housing and civic buildings, and watch Phases 3-4 respond, on desktop, touch and gamepad alike. **Met for all 23 placing commands**, through one input path, verified end to end by a pipeline test.

**RE blockers:** none left. The four Appendix C items this phase existed to close (construction far-pointer table, command IDs, command → handler, footprints) are all closed.

<details>
<summary>History: six RE passes to find the dispatcher</summary>

Kept because *why* the dead ends failed is the lesson for the next table anyone hunts in this binary. Full detail: dispatch findings sections 1-14.

1. **Pass 1 — the corpus's own construction notes.** `CAESAR_CONSTRUCTION_RE_v1.md`/`v2.md` decoded the *simulation* dispatcher `DS:153A` and moved the civic handlers from `0x43`-`0x50` to `0xDF`-`0xF5`, correcting Phase 3 — but not the construction side.
2. **Pass 2 — direct disassembly** with `capstone`. Decoded the command string table byte-exact (an early "16-byte stride" guess was wrong: 8 of 34 entries are 17 bytes, caught by the golden test). Traced the `0x36`-`0x40` auto-tiling mechanism. Dead end: the placement call site went through `[0x6DDB]`, a scratch global with 696 references.
3. **Pass 3 — four more angles, all ruled out:** a near-pointer table, two far-pointer searches, and tracing forward from `int 0x33` mouse input. `bindiff_exe` against the international build: 45.9% differs, too noisy.
4. **Passes 4-5 — the root cause.** Pointer tables here store *far* pointers (offset + segment); every search had looked for flat offsets. Calibrating the code segment (`0x2700`) extracted the full `DS:153A` table at flat `0x75D8A` (22-for-22 against published addresses) and exposed an off-by-one in `v2`'s whole civic tile table. Also found growth-stage footprints (`0xDD`→`0xDF`, `0xE9`→`0xEB`).
5. **Pass 6 — solved.** Scanning the whole image for runs of ≥8 four-byte entries sharing a segment word found 19 tables; one was `DS:127C`. Its call sites had been in the very first `lcall` sweep, unrecognised.

Limits carried at the time, all since closed: the six drag and variant commands (2026-09-13), placeholder font and generated footprint icons (2026-09-13), costs not charged (2026-09-14), drag cancel (2026-09-14).

</details>

### Still open in Phases 0-5

Every checklist item in Phases 0-5 is done. What remains is validation, a few untranscribed pieces and polish.

- **Needs new saves**
  - A walker's path, and the random draws' timing. Saves don't carry the generator's state; checking needs it, or an event (a fire, a collapse) caught between two saves.
- **Simulation**
  - The random draws on frames between steps at slower game speeds (Gaius draws once per step).
  - Every routine the calendar calls is transcribed (Phases 6-7). What starting a new province does is Phase 7.
- **Save model**
  - What the loader does after reading, beyond rebuilding `DS:0x6BFE`.
- **Build mode**
  - A touch drag gesture for roads and walls.
- **Platform**
  - Android: real-device ABI and a `gaius_viewer` port (Phase 1).
  - Touch gestures beyond single-finger drag and tap.
  - The toolbar can't scale past 2× in the 320-wide logical framebuffer (Phase 9).

---

## Phase 6 — Economy & military

**Status: Systems done** (2026-09-14). Every system the phase names is transcribed and, where a save can show it, checked against real saves; what's left is presenting the province and the battle in `gaius_viewer`.

**Goal:** Heavy Industry, Workshops and Markets, and the Cohort battle system.

**Done**
- [x] **Economic actors** (2026-09-13, findings section 20) — type 8 workshop traders sell on market cells and feed their workshop's 0-7 level; 4 barracks patrols; 5-7 invaders; 10 rioters; 0-2 forum citizens.
- [x] **`systems::economy`** (2026-09-14, findings section 25) — the loop as the engine has it: heavy industry in reach adds 2 to a workshop's level, markets let its traders sell, levels become the industrial tax, housing grades the population tax, and the year's settlement pays operating costs and the tribute. Also construction costs, emergency funds and donations. Every save's funds history balances year by year; each save's last year is reproduced exactly.
- [x] **`systems::military`, the Legion** (2026-09-14, findings section 26) — the yearly recruitment (`0x289C0`: regular Centuries from wages, irregular from population x conscription) and the assignment of Centuries to Cohorts (`0x28A8F`, mobilized/demobilized). All 17 saves' regulars, irregulars and Cohorts reproduced.
- [x] **`systems::battle`** (2026-09-14, findings section 27) — the four tactics against the province's race (16 races, strengths per tactic), rounds, casualties, morale, victory, defeat and retreat. All 17 saves' race words reproduced. Not modeled: the screen itself and the Cohort 2 hand-over.
- [x] **Auxiliaries and the plebs** (2026-09-14, findings section 29) — army duty's plebs / 16 are the auxiliaries (`0x2DD21`), in the full pleb model below. `0x23272`, once read as a fort transfer, applies Cohort 2's battle result from `cohort.csr`: not needed.
- [x] **`systems::province`, the province actors** (2026-09-14, findings section 28) — the province walker, barbarian armies (the 18-month spawner, marching, wrecking, pillaging towns, invading the city), and Cohorts halting, patrolling, attacking and going home into battle. Save actors checked against the map's occupancy bits.
- [x] **Towns, the highway and road wear** (2026-09-14, findings section 28.6) — the road trace `0x2E377`, towns growing when linked to the city and shrinking when not, the Imperial Highway link, the monthly province pass.
- [x] **Fort and the Cohort orders** (2026-09-14, findings section 28.7) — placing forts and their Cohorts, Halt, Patrol, Attack, Go Home.
- [x] **Province construction** (2026-09-14, findings section 28.8) — Clear Area, Provincial road, Highway, Great Wall and Great Tower on the city's drag auto-tiling, with the province's pieces, gates and crossings.
- [x] **The DOS process juggling isn't needed** (findings sections 26.5, 27) — `csr.exe cohort` resumes a game after the external Cohort 2 fought the battle, and `cohort.csr` (read back by `0x23272`) carries the result; Gaius resolves battles itself (`systems::battle`).
- [x] **Province costs** (findings section 25.1) — the terrain under the cursor shifts a province command's cost and charge; the build routine refuses construction under 50 pleb groups.

**Open**
- [ ] **A province view and the battle screen in `gaius_viewer`** — drawing the province map (`SPRITE2.PL8` actors, the map tiles) with its toolbar, and the battle screen's rounds, on the systems above.

**Deliverable:** the full economic loop (industry → workshop → market → taxes) and a province level with combat. **Met in the simulation**; not yet on screen.

**RE blockers:** none left. Still unread and not needed for the simulation: the battle and Tribune screens' drawing, the messages, and the Cohort 2 hand-over.

---

## Phase 7 — Forum, advisors, ratings & win/loss

**Status: In progress.** The simulation side is done -- the plebs (with Phase 6), the ratings and promotion (2026-09-14); the rest is the new-province start and the screens.

**Goal:** the administrative layer — seven advisors, four ratings (Peace, Culture, Prosperity, Empire), promotion, the annual tribute, plebs.

**Open**
- [x] **`systems::plebs`** (2026-09-14, done in Phase 6, findings section 29) — each duty's need, welfare growing or shrinking the pleb count, the assignment, and the fire, collapse and road-wear thresholds their coverage sets. Every save reproduced.
- [x] **`systems::administration`** (2026-09-14, findings section 30) — Peace, Culture, Prosperity and Empire, their population caps and average; promotion's requirements, the new province's pick, accepting or waiting 9 or 24 years, Caesar; the yearly notice. The average in all 17 saves, Culture in 14, Peace and Prosperity across the one pair of consecutive years.
- [ ] **Starting a new province** — after a promotion (`DS:0x6C26`, `0x0F81B`): the new map and empty city, the starting funds (`0x57BE`, findings section 25.1), the Legion and counters reset (`0x056BE`-`0x05740`).
- [ ] **Forum UI** — seven advisors, promotion flow, tribute, game over after three missed tributes (the settlement already counts them: `economy::settle_accounts`).
- [ ] **Maps panel** — Urbanization, Water, Administration, Road, Land Value, Trouble overlays, drawn from the modeled layers.

**Deliverable:** the full single-player loop — build, grow, get promoted or fail the tribute.

**RE blockers:** none expected to be hard. Flag any manual formula the executable contradicts back into RE.

---

## Phase 8 — Format completeness & save write-back

**Status: Started** (city renderer tables done).

**Goal:** write valid EMPIRE2 and `.SAV` files the original engine could read, close the remaining format gaps, and support "New Game".

**Done**
- [x] **Renderer tile tables** (2026-09-13) — `render::render_city`: `FIXTS.PL8` below `0xC8`, `HOUSES.PL8` above, the `3496:14B2` building metrics, per-cell slices, `HOUSES2.PL8` special cases, water and fire animation, walkers. Pixel-exact against captures (`docs/CAESAR_CITY_RENDERER_FINDINGS.md`).

**Open**
- [ ] **Save loader RE** — read order and `DS:0x6BFE` done (2026-09-14, findings section 25.2); still the biggest asymmetry: the block table is written correctly, but reading unknown fields the way the original does isn't proven.
- [ ] **Remaining formats** — `CONTFRM.GD8` (varies by build, so probably localizable layout data), `P_BLOCKS.PL8`, `TEMPLBIT.PL8`, the VAS win/lose animation. `EDATA.CSR` is stable across builds.
- [ ] **Music** — `.MDI` is standard MIDI and needs no decoder; `.XMI`/`.XM2` only for bit-exact 1993 audio.
- [ ] **Overlay map modes and the province view** in the renderer.
- [ ] **City terrain generation** — for "New Game", matching the original's random terrain.
- [ ] **Bindiff the two `CSR.EXE` builds** whenever an item here stalls; differences are often faster to read than one disassembly.

**Deliverable:** format parity, "New Game", pixel-accurate rendering.

---

## Phase 9 — Platform packaging & polish

**Status: Not started.** Pure engineering; can run alongside Phases 6-8.

**Goal:** turn "runs on the platform" into "ships on the platform" — packaging, input polish and settings, not new architecture.

**Open**
- [ ] **Settings screen** — resolution and window mode, UI scale, input remapping, frame-rate cap, audio.
- [ ] **Steam Deck** — gamepad-only navigation end to end, Steam Input glyphs if feasible, a Deck control layout.
- [ ] **Android / iOS** — touch-first onboarding (masterplan 5a point 7), on-screen build and cancel controls, safe areas, store packaging (APK/AAB, IPA), storage permissions for the user's game files.
- [ ] **Raspberry Pi** — measure frame time on Pi 4/5, confirm the GLES/KMSDRM path, document a minimum model.
- [ ] **Save and config paths** — confirm each OS's conventions, and no collision with the original's saves.
- [ ] **Localization readiness** — externalize UI strings before packaging multiplies the surface.
- [ ] **Toolbar past 2×** — a logical framebuffer that grows with the display, or a paged toolbar.

**Deliverable:** installable builds for every target platform (masterplan 5a), each with settings and no input dead ends.

**RE blockers:** none.

---

## Phase 10 — Editor tooling & IGDK integration

**Status: Not started.**

**Goal:** expose Gaius as an embeddable engine and build the editor tooling IGDK needs.

**Open**
- [ ] **Embedding API** — stable, zero-FFI where possible (as IGDK plans for Augustus).
- [ ] **Editors** — city/empire map, scenario, save, resource viewer: thin UI over the Layer 2 model, starting from Phase 1's viewer.
- [ ] **Live preview hook** for IGDK's webview UI.
- [ ] **IGA entry** — document Gaius as the Caesar I engine reimplementation.

**Deliverable:** Gaius usable standalone and embedded in IGDK.

---

## Cross-cutting: the RE backlog

`CAESAR_REVERSE_ENGINEERING_COMPLETE.md` Appendix C is a **live backlog shared by RE and engine work**. When implementation finds a discrepancy — a byte diff, behaviour that doesn't match the manual, a tile ID that doesn't fit — it becomes a new RE item, never a silent guess. The confidence labels (DEFINITIVE / HIGH CONFIDENCE / STRONG INFERENCE / UNRESOLVED) carry into code comments for anything built on less than definitive findings.

## Sequencing

```text
Phase 0  Repo + format library                    done
Phase 1  Static viewer + platform skeleton        done (Android: emulator only)
Phase 2  Normalized model + save round trip       done
Phase 3  Service propagation                      done, validated on 17 saves
Phase 4  Housing / population / the month         done, validated on 17 saves
Phase 5  Construction dispatcher + build mode     done
Phase 6  Economy + military                       city economy done; battles next
Phase 7  Forum / ratings / win-loss               unblocked
Phase 8  Format completeness + save loader        started
Phase 9  Platform packaging + polish              unblocked; parallel to 6-8
Phase 10 Editor + IGDK integration                last
```

Cross-platform work (masterplan 5a) is spread across Phase 1 (foundations), Phase 5 (input parity in build mode) and Phase 9 (packaging), not saved for the end — by Phase 9, every platform should already have run real, if incomplete, gameplay many times.
