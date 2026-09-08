# CLAUDE.md — context for Claude Code sessions on this repo

This file exists so a fresh Claude Code session has full context immediately. Read `GAIUS_MASTERPLAN.md` and `GAIUS_ROADMAP.md` in full before doing anything else — this file is a pointer/summary, not a replacement for them.

## What this project is

**Gaius**: an open-source reimplementation of *Caesar* (Impressions Games, 1992/93 DOS), in the spirit of Julius/Augustus for Caesar III. Sibling project to **IGDK** (Impression Games Dev Kit, a modding IDE) and **IGA** (Impressions Games Archive, a reference hub) — those live in sibling directories, not nested inside this one, and vice versa (avoids `CLAUDE.md` context bleed across projects, per prior convention established for that project family).

## Critical rule before touching anything

**No original game assets ever get committed to this repo.** See `GAIUS_MASTERPLAN.md` section 3 (IP posture). Every test/tool that needs a real Caesar file reads it from a directory named by the `GAIUS_TEST_ASSETS` environment variable, which points at the developer's own legally-obtained copy (e.g. from GOG). Never add `.VPX`/`.PL8`/`.P32`/`.256`/`EMPIRE2.*`/`.SAV`/`CSR.EXE` files to git — `.gitignore` already blocks the common ones, but check before any `git add`.

## Current state (as of this handoff)

**Phase 0 (format library) and Phase 1 (platform skeleton + viewer) are complete and verified** — see `GAIUS_ROADMAP.md` for exact checklist status per item, including two honestly-incomplete items (Android NDK toolchain untested — no Android SDK was available in the environment this was built in; save-file heatmap viewer unbuilt — no real `.SAV` file has ever been supplied in any archive so far).

Build and test to confirm the baseline before starting new work:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
GAIUS_TEST_ASSETS=/path/to/your/caesar/files ./build/gaius_tests
```

Expect 208/208 checks passing, 1 skip (the `.SAV` test, for the reason above). If this doesn't pass cleanly on a fresh clone, something regressed — investigate before adding new code.

### What's implemented

- `formats/` — VPX, P32, .256, PL8, EMPIRE2, SAV (read-only), EXEPACK decoders. All tested against real files where available; see `docs/FORMATS.md` for exact status per format, including two small novel RE findings made while implementing (PL8 container header, PL8 trailing-placeholder-frame pattern).
- `platform/` — `window.cpp` (SDL2, logical-resolution framebuffer + letterboxing, windowed/borderless/fullscreen), `input.cpp` (unified mouse/touch/gamepad command stream), `paths.cpp` (per-OS pref path via SDL).
- `apps/viewer/` — `gaius_viewer`, an interactive pannable/zoomable EMPIRE2 map viewer exercising the platform layer.
- `tools/` — `dump_vpx`, `dump_pl8`, `empire_view`, `save_inspect`, `bindiff_exe`.
- `tests/test_formats.cpp` — dependency-free test harness (no framework), corpus tests skip cleanly without `GAIUS_TEST_ASSETS`.

### Notable RE findings made while building this (not just implementing already-known specs)

Two addenda in `docs/` document genuine reverse-engineering work done as a side effect of implementation, not just coding against a finished spec:

- `docs/CAESAR_EXEPACK_AND_STRINGS_FINDINGS.md` — the EXEPACK decompression algorithm was fully derived from disassembling the actual embedded stub (`ndisasm`), since the main RE corpus only vaguely described it. Validated against two independent real `CSR.EXE` builds. Also documents a large set of previously-unextracted game text (full promotion-rank ladder, province names, cohort status words) found via `bindiff_exe`.
- `docs/CAESAR_GOG_BUILD_FINDINGS.md` — findings from a GOG digital distribution package (a second, international/multi-language `CSR.EXE` build exists; `COHORT.CSR` resolved as a runtime handoff artifact, not shipped content; a concrete entry point for the battle-resolution system was found via `CAESAR.BAT`'s launch loop).

**These should eventually be folded into the main RE corpus** (`CAESAR_REVERSE_ENGINEERING_COMPLETE.md`, maintained outside this repo) — they haven't been merged there yet as of this handoff.

## Next up: Phase 2

Per `GAIUS_ROADMAP.md`: the normalized data model (`model::CityState` etc.) and getting a save file to round-trip load→save byte-identical. Soft-blocked on the save loader not being reverse engineered yet (we're trusting the serializer's block table is symmetric — unconfirmed). Start there, or check the roadmap for whether priorities have shifted.

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
- **Not yet done:** `git init` was run but nothing has been committed yet (no commit was made without being asked). Stage/commit when ready — `.gitignore` was authored fresh for this handoff (the zip didn't include one) and should be reviewed before the first `git add`.
- Android NDK toolchain remains untested here too (same gap the roadmap already flags) — no Android SDK on this machine either.
