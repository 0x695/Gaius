# CLAUDE.md — context for Claude Code sessions on this repo

This file exists so a fresh Claude Code session has full context immediately. Read `GAIUS_MASTERPLAN.md` and `GAIUS_ROADMAP.md` in full before doing anything else — this file is a pointer/summary, not a replacement for them.

## What this project is

**Gaius**: an open-source reimplementation of *Caesar* (Impressions Games, 1992/93 DOS), in the spirit of Julius/Augustus for Caesar III. Sibling project to **IGDK** (Impression Games Dev Kit, a modding IDE) and **IGA** (Impressions Games Archive, a reference hub) — those live in sibling directories, not nested inside this one, and vice versa (avoids `CLAUDE.md` context bleed across projects, per prior convention established for that project family).

## Critical rule before touching anything

**No original game assets ever get committed to this repo.** See `GAIUS_MASTERPLAN.md` section 3 (IP posture). Every test/tool that needs a real Caesar file reads it from a directory named by the `GAIUS_TEST_ASSETS` environment variable, which points at the developer's own legally-obtained copy (e.g. from GOG). Never add `.VPX`/`.PL8`/`.P32`/`.256`/`EMPIRE2.*`/`.SAV`/`CSR.EXE` files to git — `.gitignore` already blocks the common ones, but check before any `git add`.

## Current state (as of 2026-09-08)

**Phases 0-4 are complete relative to what the RE corpus documents. Phase 5 is 6 of 7 checklist items done** — its core RE question is answered; the one open item (toolbar UI) is blocked on the sprite/palette gap, not on disassembly. Read `GAIUS_ROADMAP.md`'s Phase 5 status block for the full account.

- **Phase 5, six passes, same day — SOLVED.** Started by finding `CAESAR_CONSTRUCTION_RE_v1.md`/`v2.md` (unread until now), which corrected Phase 3's building-handler tile IDs (`0xDF`-`0xF5`, not `0x43`-`0x50`). Then direct `capstone` disassembly of the real US-build `CSR.EXE`. Passes 2-3 ruled out five search strategies; **pass 4-5 diagnosed why they all failed** — pointer tables here store **far** pointers (offset + segment), so every search had been looking for the wrong shape of value — and proved the fix by extracting the complete real 256-entry `DS:153A` simulation-dispatch table (verified 22-for-22 against independently-published data), which revealed **`v2`'s own published tile IDs for every civic building were off by exactly one** (fixed in `systems/service.hpp`/`.cpp` — temple variant 1 is really `0xE0`/`0xE1`, not `0xDF`/`0xE0`). **Pass 6 generalized that into a whole-image scan for every far-pointer table** (runs of ≥8 entries sharing a segment word), which found the construction dispatcher: **44 entries at flat `0x75ACC`, segment `0x11C6`, DS offset `0x127C`**, dispatched by `lcall [bx + 0x127c]` with `bx = command_id * 4`. From it: definitive command IDs (~30 toolbar handlers assign them as literals), per-command seed tiles and footprints for 17 commands, and the shared terrain gate (`0x1D <= existing tile <= 0x35`). Full derivation in `docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md` (14 sections), including all the dead ends — the failed passes are kept because the reason they failed is the transferable lesson.
- `systems::construction` (Phase 5): `placement_spec`, `can_place`, `place` — real seed tiles, real footprints, real terrain gate. **Deliberately refuses** the four drag-auto-tiled commands (Road/Wall/Plaza/Clear Area — neighbour rules not recovered) and the two variant-selected ones (Forum/Workshop — footprint known, seed tile not decoded) rather than inventing values. Costs are not wired (no treasury exists yet; that's Phase 7).
- Build mode in `gaius_viewer`: Tab / gamepad X cycles a tool ring, left-click / tap / gamepad A places, all through the one `platform::input` `Select` command. Mutates a live `model::CityState` and re-renders it. Headless hook: `--test-build T X Y`.
- `systems::housing` (Phase 4): `land_value_allows` (the confirmed land-value gate) plus `tick_tile_00` — the *only* one of the 22 `0x00`-`0x15` tile handlers actually traced in the RE corpus. **This is a real, newly-found scope gap, not a formality**: the roadmap's original Phase 4 text assumed "tile-state transitions... as recovered in `CAESAR_CITY_STATE_v5.md`" meant several were recovered; only tile 00 was. Population derivation uses the manual's confirmed *shape* (monotonic density, top-grade dip) with placeholder numbers — no numeric density table exists anywhere in this project's sources. Grade-to-tile mapping is untouched, per the roadmap's own Phase-5 deferral (still deferred).
- Save-file heatmap viewer: built (`apps/viewer/save_view.hpp`, wired into `gaius_viewer`), verified against a synthetic `SaveFile` since no real `.SAV` has ever turned up.
- Android NDK toolchain: built and verified end-to-end on an emulator (`android/`, see `android/README.md`) — a real gap in the original handoff's environment, not a permanent blocker; this machine already had Android Studio/SDK installed.
- `model::CityState` (Phase 2): typed `CityMap`/`Actor[70]`/reused `EmpireMap`, `load()`/`serialize()` round-trip verified byte-identical against synthetic data (`model/city_state.hpp`/`.cpp`), with `save_inspect` extended to print the active-actor table as a first real consumer.

Still open, unchanged from handoff: no real `.SAV` file has ever been supplied to this project (checked `E:\dev_res\ig_res` thoroughly, including `cloud_saves/`), and the Android build only targets the `x86_64` emulator ABI so far, not a real device. This means Phase 2's round-trip is only proven self-consistent, not proven against what the original engine's save loader actually produces — see roadmap "RE blockers."

Build and test to confirm the baseline before starting new work:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
GAIUS_TEST_ASSETS=/path/to/your/caesar/files ./build/gaius_tests
```

Expect 463/463 checks passing, 1 skip (the `.SAV` corpus test, for the reason above). If this doesn't pass cleanly on a fresh clone, something regressed — investigate before adding new code.

### What's implemented

- `formats/` — VPX, P32, .256, PL8, EMPIRE2, SAV (read-only), EXEPACK decoders. All tested against real files where available; see `docs/FORMATS.md` for exact status per format, including two small novel RE findings made while implementing (PL8 container header, PL8 trailing-placeholder-frame pattern).
- `model/` — `city_state.hpp`/`.cpp`: `CityState`/`CityMap`/`Actor` (Layer 2, Phase 2). Reshapes a loaded `SaveFile` into typed fields for the parts of the save format that are understood (5 city grids, actor table, embedded EMPIRE2 map); the other 13 of 20 confirmed blocks are carried through as opaque byte vectors since nothing in the RE corpus proposes a structure for them yet.
- `systems/` — Layer 3. `service.hpp`/`.cpp` (Phase 3, corrected/extended in Phase 5): `apply_coverage`/`apply_land_value`/`apply_flags`, the per-tick reset, the confirmed C9D4 bit table (`kC9D4BitTable`, confidence-labeled), 11 building handlers (tiles `0xDF`-`0xF5`) plus the `0x36`-`0x40` construction-state family, and `dispatch_tile()` — the actual `DS:153A` tile-ID dispatcher. Runtime-only simulation scratch state (the per-cell coverage ceiling, "`2D94`" in the disassembly) lives in `ServiceState`, not `model::CityState` — it isn't part of the save format, and as of Phase 5 no handler actually reads from it either (see the struct's comment for why). `housing.hpp`/`.cpp` (Phase 4): `land_value_allows`, `tick_tile_00` (the only traced `0x00`-`0x15` handler), and population derivation with placeholder density numbers (real shape, fake magnitude — see the file's header comment before trusting a number out of it). `construction.hpp`/`.cpp` (Phase 5): the 34-entry command-ID/name table (`CommandId`, `kCommandNames`, verified against real `CSR.EXE` bytes by a golden test) plus `placement_spec`/`can_place`/`place` — per-command seed tile, footprint and the shared terrain gate, all transcribed from the real handlers reached via `DS:127C`. Six of the 23 placing commands are `DragAutoTiled`/`VariantSelected` and `place` returns false for them by design; read the header comment before assuming a missing command is a bug.
- `platform/` — `window.cpp` (SDL2, logical-resolution framebuffer + letterboxing, windowed/borderless/fullscreen), `input.cpp` (unified mouse/touch/gamepad command stream), `paths.cpp` (per-OS pref path via SDL).
- `apps/viewer/` — `gaius_viewer`, an interactive pannable/zoomable viewer that handles both EMPIRE2 scenarios and `.SAV` files (dispatched by exact file size), plus `apps/android_hello/` — a minimal platform-layer smoke test for the Android target (not a viewer port, see `android/README.md`).
- `tools/` — `dump_vpx`, `dump_pl8`, `empire_view`, `save_inspect` (now also prints the actor table via `model::CityState`), `bindiff_exe`.
- `tests/test_formats.cpp` — dependency-free test harness (no framework), corpus tests skip cleanly without `GAIUS_TEST_ASSETS`.

### Notable RE findings made while building this (not just implementing already-known specs)

Addenda in `docs/` document genuine reverse-engineering work done as a side effect of implementation, not just coding against a finished spec:

- `docs/CAESAR_EXEPACK_AND_STRINGS_FINDINGS.md` — the EXEPACK decompression algorithm was fully derived from disassembling the actual embedded stub (`ndisasm`), since the main RE corpus only vaguely described it. Validated against two independent real `CSR.EXE` builds. Also documents a large set of previously-unextracted game text (full promotion-rank ladder, province names, cohort status words) found via `bindiff_exe`.
- `docs/CAESAR_GOG_BUILD_FINDINGS.md` — findings from a GOG digital distribution package (a second, international/multi-language `CSR.EXE` build exists; `COHORT.CSR` resolved as a runtime handoff artifact, not shipped content; a concrete entry point for the battle-resolution system was found via `CAESAR.BAT`'s launch loop).
- `docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md` (Phase 5, 14 sections) — direct `capstone`-based disassembly of the decompressed US-build image, and the largest single block of novel RE this project has produced. **Both dispatch tables are now fully recovered**: `DS:153A` (flat `0x75D8A`, simulation — tile ID → per-tick behaviour) and `DS:127C` (flat `0x75ACC`, construction — command ID → placement handler). Also: the 34-entry command string table byte-exact, per-command seed tiles/footprints/terrain gate, the `0x36`-`0x40` auto-tiling mechanism traced deeper than `v1`/`v2` had, an off-by-one correction to `v2`'s entire published tile-ID table, three building re-identifications (`0xEB` Oracle, `0xEC` School, `0xEE` Prefecture), and the DS base (`0x74850`) and main simulation-tick dispatcher (`0x293A2`) found as byproducts. **The general technique is the reusable part**: scan for runs of ≥8 consecutive 4-byte entries sharing a segment word to find far-pointer tables, then `flat = segment*16 + offset + DS_base_correction`. Sections 3-6 keep every ruled-out approach on the record.

**These should eventually be folded into the main RE corpus** (`CAESAR_REVERSE_ENGINEERING_COMPLETE.md`, maintained outside this repo) — they haven't been merged there yet as of this handoff.

## Next up

**Phase 5's one open item is a product decision, not an RE task.** The toolbar UI (checklist item 7) is blocked on the sprite/font pipeline: `docs/FORMATS.md` records that no correct in-game palette has been identified for city-content PL8 sheets, so the real toolbar icons and text can't be drawn yet. Two ways to close it — find that palette (a Phase 1/8 follow-up), or ship a placeholder vector/geometric toolbar now. **Ask before picking**; build mode currently reports the selected tool to stdout, which is honest but obviously not shippable.

Three concrete RE residues, each small and well-bounded. The first two are Phase 5's own, listed with their handler addresses in `docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md` section 14; the third is Phase 4's gap, newly unblocked by section 11:

1. **Drag auto-tiling neighbour rules** (Road/Wall/Plaza/Clear Area). The `0x36`-`0x40` mechanism is traced; what's missing is the neighbour-pattern → tile-variant mapping. Blocks 4 of the 23 placing commands, including roads — arguably the most-used building in the game.
2. **Forum/Workshop variant-selection tables.** Footprints are known (4×4 / 3×3); the seed tile comes from a per-variant runtime table that wasn't decoded. The forum grade cost table is already located (`3496:15A0`, 8 grades) and is probably adjacent.
3. **The other 21 of 22 `0x00`-`0x15` housing handlers** (Phase 4's gap — only tile 00 is traced). Not mentioned in section 14, but section 11's full `DS:153A` extraction means every one of these handlers' addresses is now *known*; they just haven't been disassembled and read. Probably the cheapest of the three, and it's what would make housing actually grow and shrink.

Then Phase 6 (economy/military), which has its own concrete lead: `CSR.EXE`'s `cohort` command-line argument triggers internal battle resolution — see `docs/CAESAR_GOG_BUILD_FINDINGS.md` section 4.

Before starting any of these: re-check `E:\dev_res\ig_res` for newer RE docs — `CAESAR_CONSTRUCTION_RE_v1.md`/`v2.md` sat unread there for a while before this project found them, and they turned out to be load-bearing.

## Working conventions established so far

- Confidence-labeling discipline carries from the RE corpus into code comments: anything built on less-than-DEFINITIVE findings should say so in a comment, not present false certainty.
- Format decoders throw on any structural inconsistency rather than guessing/silently producing garbage (see `FormatError` usage throughout `formats/`).
- Corpus tests (needing real assets) skip, never fail, when assets aren't available — keep this pattern for new tests.
- Cross-platform/QoL concerns (resolution independence, input abstraction) are architecture decisions made early (Phase 1), not deferred polish — see `GAIUS_MASTERPLAN.md` section 5a. Don't add mouse-only or fixed-resolution code paths "temporarily."

## This machine's environment (Windows dev box, set up 2026-09-08)

This repo was handed off from an environment that assumed a Unix-like toolchain (plain `cmake --build`, gcc/clang, `.vscode/` configured only for a "Linux" IntelliSense target). This machine is native Windows with no gcc/clang/ninja on PATH — here's what's actually wired up, so a fresh session doesn't have to re-discover it:

- **Compiler:** MSVC via Visual Studio 2022 Build Tools (`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`). No vcvars/Developer Prompt needed — CMake's default generator on this machine is the Visual Studio generator, which locates `cl.exe` itself. That means it's a **multi-config** generator: pass `--config RelWithDebInfo` (or `Debug`) to `cmake --build`, and binaries land in `build/<Config>/`, not `build/`.
- **SDL2:** not present as a system library — installed via vcpkg (`C:\dev\tools\vcpkg`, package `sdl2:x64-windows`). Any configure needs `-DCMAKE_TOOLCHAIN_FILE=C:/dev/tools/vcpkg/scripts/buildsystems/vcpkg.cmake`. `.vscode/settings.json` and `.vscode/tasks.json`'s Windows-specific command overrides already do this; a bare manual `cmake` invocation from a terminal needs the flag added by hand.
- **`GAIUS_TEST_ASSETS`:** wired to `E:\dev_res\ig_res\Caesar\US` (a legally-obtained GOG copy, extracted outside this repo — see IP posture rule above). Set via `.vscode/settings.json`'s `terminal.integrated.env.windows`/`cmake.testEnvironment`, and as a fallback env override on the Windows `tasks.json` test task. **Use `Caesar/US/`, not the `Caesar/` root** — the root holds a second, newer international/multi-language `CSR.EXE` build that `docs/CAESAR_GOG_BUILD_FINDINGS.md` explicitly flags as *not yet analyzed*; `US/` is the build every existing finding and the `test_exepack_golden_decode` fixture was validated against (it checks for the `CaesarXX.sav` string specific to that build). A copy of `EMAP2_decoded.png` (the golden-image reference, not a game asset) was placed in `Caesar/US/` alongside it since the golden-image test expects it next to `EMAP2.VPX`.
- **Full verified command sequence on this machine:**
  ```powershell
  cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/dev/tools/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
  cmake --build build --config RelWithDebInfo -j
  $env:GAIUS_TEST_ASSETS = "E:\dev_res\ig_res\Caesar\US"
  .\build\RelWithDebInfo\gaius_tests.exe
  ```
- **Android:** this machine already had Android Studio + an SDK at `%LOCALAPPDATA%\Android\Sdk` (build-tools, platform-tools, and a `Medium_Phone` AVD were all pre-existing) — only `cmdline-tools` and the NDK (`ndk;29.0.14206865`) needed installing, both via `sdkmanager`. **Gradle needs a JDK 17 on `JAVA_HOME`** — Android Studio's bundled JBR is JDK 25, too new for this project's Gradle 8.1.1/AGP 8.1.1; installed Eclipse Temurin 17 (`C:\Program Files\Eclipse Adoptium\jdk-17.0.20.101-hotspot`) specifically for this. See `android/README.md` for the full build/run sequence.
