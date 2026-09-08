# Gaius — Roadmap

This roadmap sequences engine work against the current reverse-engineering state (see `GAIUS_MASTERPLAN.md` section 4 and `CAESAR_REVERSE_ENGINEERING_COMPLETE.md`). Each phase lists: goal, concrete deliverables, dependencies, and **RE blockers** — items from the RE TODO that must close before that phase can finish (not necessarily before it can start).

Phases are designed so that RE work and engine work can proceed in parallel: whenever a phase is blocked on an open RE question, the next phase's *non-blocked* parts should already be underway.

---

## Phase 0 — Repo scaffolding & format library (no blockers)

> **Status: substantially complete.** Implementation lives in the `gaius/` repo (delivered alongside this roadmap). See `docs/FORMATS.md` for exactly what's implemented, what's tested against real files, and two small new findings that came out of building it (PL8 container header, PL8 trailing-placeholder-frame pattern). `bindiff_exe` is also done — see `docs/CAESAR_EXEPACK_AND_STRINGS_FINDINGS.md` for what it found. Phase 0 is fully complete.

**Goal:** stand up the repo skeleton and port the already-validated Python prototypes into a real C++ library, exactly like IGDK's Phase 0 did for `sgformat`.

- [x] `gaius/` repo, CMake build, VS Code config (mirror IGDK's Phase 0 setup).
- [x] `formats/vpx` — port `caesar_vpx.py` decode logic to C++. Validated against `EMAP2.VPX` → **0/64000 pixel mismatches** against `EMAP2_decoded.png` in an automated golden-image test.
- [x] `formats/p32`, `formats/pal256` — palette decoders. (`.p32` validated end-to-end via the VPX golden-image test; `.256` unit-tested but not yet golden-image-validated against a real file — see `docs/FORMATS.md`.)
- [x] `formats/pl8` — sprite sheet reader. Verified against the documented `HOUSES.PL8` worked example. Along the way, resolved which container header word is the frame count (previously undocumented) and discovered/handled a trailing-placeholder-frame pattern — see `docs/FORMATS.md`.
- [x] `formats/empire2` — ported. All 50 supplied `EMPIRE2.0xx` round-trip load→save byte-identical.
- [x] `formats/save` — block table ported and verified contiguous/size-correct against the Python reference. Not yet validated against a real `.SAV` file — none has been captured yet; corpus test skips cleanly rather than failing.
- [x] CLI tools: `dump_vpx`, `dump_pl8`, `empire_view` (ASCII/PNG render of an EMPIRE2 file), `save_inspect`. All smoke-tested against real files.
- [x] `bindiff_exe` — built and run against both real `CSR.EXE` builds. Along the way, fully reverse engineered the EXEPACK decompression algorithm from the actual embedded stub disassembly (previously only vaguely described), validated byte-exact against both builds independently, and surfaced a large set of previously-unextracted game text (full 21-rank promotion ladder, ~60 province names, cohort status words, cross-language-confirmed workshop goods list). See `docs/CAESAR_EXEPACK_AND_STRINGS_FINDINGS.md`.

**Deliverable:** `gaius-phase0.zip`-equivalent — library + CLI tools + docs (`FORMATS.md` covering everything in section 4 of the masterplan, `ROADMAP.md` = this file).

**RE blockers:** none. Everything here is already "proven"/"definitive" per the confidence table.

---

## Phase 1 — Static content viewer + platform skeleton (no blockers)

> **Status: core deliverables complete and verified; two items deferred with reasons below.** Implementation lives in `platform/` (window, input, paths) and `apps/viewer/` in the `gaius` repo. See the verification notes under each item.

**Goal:** prove the asset pipeline end-to-end with a real (if non-interactive) visual: render an EMPIRE2 scenario and the raw decoded city-terrain values for a save file, using SDL2 — and prove it on more than one platform immediately, so cross-platform assumptions get validated before any game logic is built on top of them.

- [x] SDL2 window + basic sprite atlas assembly from PL8 + palette, drawn through the **logical-resolution framebuffer** from day one (masterplan section 5a) — never a direct fixed-pixel blit, even though this is "just" a viewer. *(PL8 atlas assembly deferred to when a real in-city sprite palette is found — see `docs/FORMATS.md`'s "known gap"; the logical-framebuffer + letterbox architecture itself is implemented and verified in `platform/window.cpp`.)*
- [x] `platform/window.cpp`: windowed/borderless/fullscreen switching, arbitrary resize, remembered window state on desktop. Verified: mode cycling (windowed→borderless→fullscreen→windowed), resize, and letterbox coordinate mapping (window center maps to logical center exactly) all confirmed working under `SDL_VIDEODRIVER=dummy`.
- [x] `platform/paths.cpp`: per-OS config/output directories wired up even though there's nothing to save yet. Verified: resolves correctly via `SDL_GetPrefPath` (e.g. `~/.local/share/Gaius/Gaius/...` on Linux).
- [ ] CMake toolchain files for Android (NDK) and confirm a clean build + "hello sprite" run on at least one non-desktop target. **Not done — no Android SDK/NDK is available in this environment**, so this couldn't be verified rather than being skipped by choice. Flagging honestly rather than claiming it works: the CMake structure doesn't yet have an Android toolchain file, and `platform/window.cpp`/`input.cpp` haven't been exercised against Android's SDL2 backend at all. Real risk area for whoever picks this up next.
- [x] Empire-map viewer: renders any of the 50 EMPIRE2 scenarios with terrain family colors, using the same classification as Phase 0's `empire_view` tool. Verified visually at three zoom levels (see `apps/viewer/main.cpp`'s `--test-zoom`/`--test-pan` headless hooks) — zoomed out reproduces the exact same rendering as Phase 0's static `empire_view --png` output.
- [ ] Save file viewer (100×100 tile grid + four service-layer heatmaps). **Not built** — no real `.SAV` file has ever been supplied (same gap noted since Phase 0), so there's nothing to point it at. The `formats::save` reader from Phase 0 is ready; only the rendering path is missing. Low priority until a save file exists.
- [x] Basic input abstraction (masterplan section 5a point 4): implemented in `platform/input.hpp/cpp` — mouse-drag (middle button), touch-drag (single finger), and gamepad (left stick + triggers) all translate into the same `Command` stream (`PanMove`/`Zoom`/etc.). Verified via headless `--test-pan`/`--test-zoom` flags (real device input obviously can't be exercised in this environment) plus a standalone harness confirming `Window::window_to_logical` and mode-switching behave correctly. Gesture design (tap-vs-drag, pinch-to-zoom, two-finger-secondary) is explicitly deferred — single-finger-drag-only is a documented simplification, not a finished touch UX.

**Deliverable:** a "look at your data" tool — valuable on its own for continued RE work, it's the foundation of the eventual editor (Layer 4), and it's also the project's first proof that the resolution/input abstractions actually hold up cross-platform (modulo the Android gap above, which is a real open item, not a formality).

**RE blockers:** none functionally, but tile→sprite mapping is guesswork until the renderer lookup tables (RE TODO item) are found. Track this as tech debt, not a blocker.

---

## Phase 2 — Normalized data model (Layer 2) (partial blocker)

**Goal:** build the real in-memory model described in masterplan section 5 (`CityMap`, `EmpireMap`, `Actor[70]`, service layers) and get a save file fully loaded into it.

- [ ] `model::CityState` struct exactly matching the proposed structs in `CAESAR_REVERSE_ENGINEERING_COMPLETE.md`'s reimplementation-architecture section.
- [ ] Load a `.SAV` into `CityState` using the confirmed block table (Phase 0's reader).
- [ ] Round-trip: `CityState` → re-serialize → byte-diff against the original save. This validates the serializer's block table is complete and correctly ordered, independent of whether we understand every field's *meaning*.
- [ ] Object/actor table normalized per the type-0..13 split (city-coordinate vs. province-coordinate) from `CAESAR_CITY_STATE_v9.md`.

**Deliverable:** a save file editor's actual backend — load, inspect every field, re-save, confirmed byte-identical for untouched files.

**RE blockers (soft — affect fidelity, not the round-trip):**
- Save **loader** not yet reverse engineered — we're relying on the serializer's block table being symmetric, which is a reasonable but unconfirmed assumption. Flag any byte-diff mismatches back into RE work immediately.
- 480/120/720-byte save tables are unidentified — they round-trip as opaque blobs for now.

---

## Phase 3 — Simulation core: service propagation (blocked on partial RE)

**Goal:** implement the systems that are already fully specified at the algorithm level: `A2C4` coverage, `54A4` land value, `C9D4` flag propagation, `7BB4` derived state — the primitives, not yet the building-specific callers.

- [ ] `systems::service` — implement `apply_coverage`, `apply_land_value`, `apply_flags` exactly per the recovered signatures (masterplan/RE doc have exact parameter lists and clamping behavior).
- [ ] `systems::service` reset-per-tick behavior (`C9D4 &= 0x12` preserving bits 0x02/0x10, per `CAESAR_CITY_STATE_v6.md`).
- [ ] Unit tests directly encoding the confirmed bit table (0x20 = religious, 0x80 = entertainment, etc.) as fixtures — these are "HIGH CONFIDENCE" per the RE doc's own labeling, safe to build on.
- [ ] Confirmed building handlers only (temples, bath houses, hospital, school/oracle-candidate, theater/coliseum/hippodrome, plaza, barracks, prefecture) wired to these primitives with their known radii.

**Deliverable:** a system that, given a city tile grid with civic buildings placed, correctly computes coverage/land-value/flag propagation matching the executable's behavior.

**RE blockers:**
- `C9D4.01`, `C9D4.04`, `C9D4.08` exact consumers are still "medium confidence" — implement per current best guess, tag with `// TODO(RE): confirm consumer` comments, and keep the RE TODO item open in parallel.
- Do **not** implement Heavy Industry/Market/Workshop/Fort here — confirmed out of scope for this dispatch mechanism (see masterplan section 4).

---

## Phase 4 — Housing & population (blocked on RE construction dispatcher, partially)

**Goal:** the residential development state machine (`43A5` tile IDs `0x00–0x15`), land-value gating, and population derivation from housing.

- [ ] `systems::housing` — implement the land-value threshold gate (`land_value_allows`) and tile-state transitions exactly as recovered in `CAESAR_CITY_STATE_v5.md`.
- [ ] Population derivation from housing squares × grade (per manual: density increases with grade, highest grades have a slight density drop).
- [ ] Sixteen housing grades per the manual; map grade → tile ID range once the construction dispatcher work below clarifies IDs.

**Deliverable:** houses that grow/shrink/evolve in response to the service layers from Phase 3, matching the manual's documented behavior (water, road-to-forum, markets, amenities, land value).

**RE blockers (hard):**
- The exact `43A5` tile-ID-to-housing-grade mapping is still provisional. This phase can start against the *provisional* IDs but should not be considered "done" until the construction dispatcher (Phase 5) confirms them.

---

## Phase 5 — Construction system & the dispatcher (the critical-path RE target)

**Goal:** close the single highest-value open RE question — construction command → object type → city tile ID → footprint — and wire it into an actual build-mode UI.

This phase is explicitly **RE-heavy, not just engineering**. Treat it as a joint RE+implementation sprint:

- [ ] Recover the relocated construction/action far-pointer table directly from the executable (per `CAESAR_REVERSE_ENGINEERING_COMPLETE.md` Appendix section on the reimplementation sequence step 2).
- [ ] Map every construction command string (already enumerated) to its numeric ID.
- [ ] Trace each command through placement/validation to its final `43A5` tile write(s) and footprint size.
- [ ] Confirm/correct the provisional building table from Phase 3/4 against this ground truth.
- [ ] Implement `systems::construction`: road/plaza, water (reservoir/pipe/well/fountain), walls/towers/gates, prefecture, barracks, forum (8 grades), and all Construction-toolbar buildings, each with real costs from the manual (Appendix A pricing — already fully transcribed in the manual text) and real footprints from this RE pass.
- [ ] Minimal interactive build mode: place buildings on a city grid with drag-to-build for roads/walls/pipes per the manual's UI description — implemented against the Phase 1 input abstraction, so mouse-drag, touch-drag, and a gamepad cursor-equivalent all drive the same drag-to-build command from the start rather than mouse-only with touch/gamepad bolted on later.
- [ ] Toolbar UI built at the scalable-UI-factor from masterplan section 5a point 3, sized for the smallest target touch surface (phone) and verified it still reads fine at desktop scale — easier to scale a touch-sized UI up than to retrofit touch targets onto a mouse-sized one.

**Deliverable:** a genuinely playable slice — build roads, water, a forum, housing, and civic buildings, and watch the Phase 3/4 systems respond correctly, **on desktop, touch, and gamepad input alike**.

**RE blockers (this phase *is* the blocker-closer):** this is Appendix C items "Recover construction far-pointer table," "Identify construction command numeric IDs," "Map all construction commands to placement handlers," "Recover exact footprints."

---

## Phase 6 — Economy & military (blocked on RE, now with a concrete lead)

**Goal:** Heavy Industry / Workshops / Markets (suspected to run through the object/actor system, not the tile dispatcher) and the Cohort/battle system.

- [ ] RE: trace object allocator calls for economic actors (per `CAESAR_CITY_STATE_v9.md`'s actor-type work — types 4, 5, 8, 10 are already promising leads for walkers/economic/disruptive actors).
- [ ] `systems::economy` — workshops (8 goods types per manual), heavy industry feeding workshops, markets enabling sales, once the above is resolved.
- [ ] **Battle resolution — no longer a from-scratch RE target.** `CAESAR_GOG_BUILD_FINDINGS.md` section 4 identified a concrete entry point: `CSR.EXE` accepts a `cohort` command-line argument (discovered via `CAESAR.BAT`'s launch loop) that triggers its own internal battle resolution — this is the code path implementing the manual's Tortoise/Assault/Flank/Charge screen for players without the external Cohort 2 product. Next step is locating the argument-parsing branch in the disassembly and tracing forward from there, rather than searching blind.
- [ ] `systems::military` — Legion/Cohort/Century structure, the four battle tactics and their resolution rules, implemented once the above trace confirms the actual math (the manual only describes it qualitatively; the executable is the source of truth once found).
- [ ] Provincial level: forts, cohort patrol/attack/go-home, barbarian armies, small towns, Imperial Highway.
- [ ] Confirm Gaius does **not** need to reproduce the original's process-juggling architecture (batch file alternating `csr.exe`/`cohort.exe` via DOS errorlevels) — that's a DOS-era limitation; the equivalent in Gaius is just an internal screen/state transition. `COHORT.CSR`'s role (a save-handoff filename written before the transition) only needs its *behavior* reproduced, not its file format treated as a spec to satisfy.

**Deliverable:** full economic loop (industry → workshop → market → tax revenue) and a functioning provincial level with combat.

**RE blockers:** economic actor tracing (Appendix C "Finish Heavy Industry/Market/Workshop/Fort") remains open. Battle resolution is now a **located-but-not-yet-traced** target rather than a **net-new, no-leads** one — meaningfully lower risk than previously assessed.

---

## Phase 7 — Forum, advisors, ratings & win/loss conditions (mostly engineering)

**Goal:** the administrative layer — seven advisors, four ratings (Peace/Culture/Prosperity/Empire), promotion, annual tribute, plebs.

- [ ] `systems::population` (pleb groups, welfare expenditure, task assignment) — fully specified qualitatively in the manual; needs no new RE, just implementation against the already-modeled population/housing state.
- [ ] `systems::administration` — ratings computation per manual's four-category formulas (population tax, industrial tax, growth rate, etc.).
- [ ] Forum UI: seven advisors, promotion flow, tribute deduction, game-over conditions (three missed tributes).
- [ ] Maps panel: Urbanization/Water/Administration/Road/Land Value/Trouble overlays — these map directly onto already-modeled layers (`54A4`→Land Value, `A2C4`/`C9D4`→ derived overlays once semantics are locked from Phase 3–6).

**Deliverable:** the full single-player loop: build → grow → get promoted or fail the tribute.

**RE blockers:** none expected to be hard-blocking — this phase is mostly translating manual-documented formulas into code once the underlying state (Phases 2–6) exists. Flag any formula that doesn't match observed executable behavior back into RE.

---

## Phase 8 — Format completeness & save write-back (Layer 3)

**Goal:** finish Layer 3 — write valid EMPIRE2 and `.SAV` files the original engine (or Julius-style compatibility layers) could theoretically still read, and close remaining format gaps.

- [ ] Save **loader** reverse engineering (currently the biggest asymmetry — we can write the block table but haven't proven we can *read* it the way the original does for the fields we don't yet understand).
- [ ] `EDATA.CSR` confirmed stable across builds (no further work needed there). `CONTFRM.GD8`, `P_BLOCKS.PL8`, `TEMPLBIT.PL8`, VAS win/lose animation — closeout pass on the remaining unresolved formats. Note `CONTFRM.GD8` is now known to vary by build/region (differs between the two `CSR.EXE` builds — see `CAESAR_GOG_BUILD_FINDINGS.md` section 5), which narrows the hypothesis toward localizable string/layout data.
- [ ] Music: `.MDI` files (where present) are standard MIDI and need no decoder — treat as the preferred playback path; `.XMI`/`.XM2` only needed if bit-for-bit 1993 audio fidelity is a goal.
- [ ] Renderer tile-lookup tables — needed for pixel-accurate rendering rather than Phase 1's approximations.
- [ ] City scenario/terrain generation — needed for "New Game" to procedurally generate terrain matching the original's distribution, per the manual's "terrain generated randomly each time" behavior.
- [ ] Bindiff pass between the two now-available `CSR.EXE` builds (Phase 0's `bindiff_exe` tool) — run this whenever a remaining unresolved item in this phase stalls; differences between builds are often faster to interpret than a single disassembly in isolation.

**Deliverable:** full format parity, "New Game" support (not just loading existing saves), and pixel-accurate rendering.

---

## Phase 9 — Platform packaging & QoL polish (mostly engineering)

**Goal:** turn "runs on the platform" (validated incrementally since Phase 1) into "ships properly on the platform" — this is packaging, input polish, and the QoL settings surface, not new engine architecture.

- [ ] Settings screen: resolution/window mode, UI scale, input remapping, frame-rate cap (battery/thermal-friendly on mobile/Deck), audio — surfacing the `platform/` layer's existing capabilities rather than building new ones.
- [ ] Steam Deck: verify gamepad-only navigation end-to-end (no screen requires a keyboard/mouse fallback), Steam Input glyph mapping if feasible, and a Deck-specific control layout entry in Steam's UI (once distributed via Steam).
- [ ] Android/iOS: touch-first onboarding for the command-mode/scroll-mode equivalent (masterplan section 5a point 7), on-screen build/cancel affordances, safe-area handling for notches/rounded corners, app store packaging (APK/AAB, IPA) and appropriate storage permissions for user-supplied original game assets.
- [ ] Raspberry Pi: profile actual frame time on target hardware (Pi 4/5 class), confirm the GLES/KMSDRM path, and document a minimum supported Pi model rather than assuming "it'll be fine."
- [ ] Cross-platform save/config path QA: confirm the Phase 1 `platform/paths.cpp` choices actually match each OS's conventions and don't collide with the original game's own save format expectations.
- [ ] Localization-readiness pass (not full localization) — if UI strings aren't already externalized by this point, do it now, since it's far cheaper before packaging multiplies the surface area.

**Deliverable:** installable/packaged builds for every platform in the target matrix (masterplan section 5a), each with a working settings screen and no input dead-ends.

**RE blockers:** none — this phase is pure engineering/UX and can run in parallel with any still-open RE work from Phase 6/8.

---

## Phase 10 — Editor tooling & IGDK integration (Layer 4)

**Goal:** expose Gaius as an embeddable engine and build the editor tooling IGDK needs.

- [ ] Stable embedding API (mirroring how IGDK plans to embed Augustus as a submodule, zero-FFI where possible).
- [ ] City/empire map editor, scenario editor, save editor, resource viewer — thin UI over the Layer 2 model, reusing Phase 1's viewer as a base.
- [ ] Live-preview hook for IGDK's native webview UI.
- [ ] Document Gaius in IGA as the Caesar-I engine-reimplementation entry (community "wanted board" gap this project closes).

**Deliverable:** Gaius is usable both standalone and as an IGDK-embedded engine.

---

## Cross-cutting: the RE backlog

Every phase above references specific items from `CAESAR_REVERSE_ENGINEERING_COMPLETE.md` Appendix C. That list should be treated as a **live, shared backlog** between RE work and engine work — as engine implementation surfaces a discrepancy (a byte-diff failure, a behavior that doesn't match the manual, a tile ID that doesn't fit the provisional table), it should generate a new RE TODO item rather than being guessed around silently. The confidence-labeling discipline already established (DEFINITIVE / HIGH CONFIDENCE / STRONG INFERENCE / UNRESOLVED) should carry over into code comments for any system built on less-than-DEFINITIVE findings.

## Suggested sequencing summary

```text
Phase 0  Repo + format library                    (unblocked)
Phase 1  Static viewer + platform skeleton        (unblocked)
Phase 2  Normalized model + save round-trip       (soft-blocked: save loader)
Phase 3  Service propagation primitives           (mostly unblocked)
Phase 4  Housing/population                       (partially blocked: tile IDs)
Phase 5  Construction dispatcher + build-mode UI  (THE critical RE+eng sprint)
Phase 6  Economy + military                       (hard-blocked: needs new RE)
Phase 7  Forum/ratings/win-loss                   (mostly unblocked once 2-6 exist)
Phase 8  Format completeness + save loader        (closes remaining format debt)
Phase 9  Platform packaging + QoL polish          (unblocked; can run parallel to 6/8)
Phase 10 Editor + IGDK integration                (final)
```

Phases 0–3 can start immediately in parallel with continued RE work on Phase 5's dispatcher (the highest-leverage next RE target per the existing corpus's own recommendation). Cross-platform/QoL work (masterplan section 5a) is deliberately spread across Phase 1 (foundations), Phase 5 (build-mode input parity), and Phase 9 (packaging/polish) rather than concentrated at the end — the goal is that by the time Phase 9 starts, every platform has already been run against real, if incomplete, gameplay many times over.
