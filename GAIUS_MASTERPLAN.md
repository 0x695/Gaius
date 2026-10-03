# Gaius — Master Plan

**Gaius** is an open-source reimplementation of *Caesar* (Impressions Games, 1992/93 DOS), in the same spirit as Julius/Augustus for Caesar III. It is a sibling project to the **IGA** (Impressions Games Archive).

> Naming note: "Gaius" is the engine/game reimplementation itself (praenomen convention, matching "Julius"/"Augustus"). It is not a mod tool — it *is* the game, rebuilt from clean-room reverse engineering, that reads original Caesar assets.

---

## 1. Relationship to the IGA

```text
IGA  (Impressions Games Archive)
  - reference hub: formats, mechanics, history for all 6 Tier-1 games
  - documents Gaius's findings as the Caesar-I engine reimplementation entry

Gaius (this project)
  - the actual Caesar (1992) engine reimplementation
  - consumes original CSR.EXE-era assets, reproduces simulation + rendering
```

Gaius is scoped, resourced, and versioned independently, and does not depend on the IGA to make progress. 1.0's tooling is a collection of scripts.

**Directory placement:** `gaius/` should sit *beside* `iga/`, never nested inside it, for the same `CLAUDE.md` context-bleed reasons already established for the IGA.

---

## 2. What "done" looks like

A playable, moddable, open-source Caesar (1992) that:

1. Loads the original game's assets (VPX/PL8/P32/256 graphics, EMPIRE2 scenarios, `EDATA.CSR`) — **never redistributed**, always supplied by the user's own legal copy.
2. Reproduces the simulation faithfully: city tile grid, service layers (A2C4/C9D4/54A4/7BB4), housing evolution, the 70-record object/actor system, the empire-map pathfinding/route model.
3. Can load and save the original `.SAV` format exactly (round-trip byte compatibility as a stretch goal, semantic compatibility as the baseline goal).
4. **Runs everywhere**: Windows, Linux, Steam Deck and Android — this is a first-class goal, not a stretch platform, and it shapes early architecture decisions (see section 5a). macOS, iOS and Raspberry Pi are out of scope for 1.0 (decided 2026-09-16).
5. Ships modern QoL expected of any 2020s remaster/port: arbitrary window sizes, fullscreen/borderless/windowed toggle, resolution-independent UI scaling, remappable input across mouse+keyboard/touch/gamepad, and sensible save/config file locations per platform.
6. Is legible enough — clean modern data structures, documented systems — that any contributor can pick up any subsystem without re-deriving it from the disassembly.
7. Ships simple tooling: a collection of scripts to export the game's assets and inspect, render and check saves.

Non-goals (for now): Caesar II support (different engine entirely — see IGA scope notes), touching Activision-held IP beyond what's needed for interoperability, network multiplayer, or matching 1993 performance constraints (we have vastly more headroom).

Not in 1.0: macOS, iOS and Raspberry Pi.

---

## 3. IP posture

Same rule as the rest of the project family: Activision holds the IP (Caesar 1992 is not currently under the same active commercial re-release as Pharaoh, but treat it identically). Gaius:

- Ships **no** original assets, code, or extracted resource files in its repo.
- Requires the user to point it at their own legally obtained copy of the game.
- Only distributes: the engine source code, format specifications (data *about* the format, not the data itself), and tooling to convert *the user's own* files into open formats (PNG, JSON, etc.) at runtime/install time, never redistributed further.
- Shows the original's art in one place only, by decision (2026-10-03): a handful of gameplay screenshots in `docs/images/` for the README, captured from Gaius running the author's own copy. No extracted file is ever committed.

---

## 4. Current state of knowledge (as of this plan)

> **Status (2026-10-03):** this section is the plan's starting snapshot, kept as written. Everything listed below as unresolved was resolved during Phases 3-8; each bullet now says where. The roadmap (`GAIUS_ROADMAP.md`) and `docs/` are the current record.

The reverse-engineering corpus is already substantial and has been consolidated into `CAESAR_REVERSE_ENGINEERING_COMPLETE.md` — **that document is the authoritative technical reference going forward.** This master plan does not repeat it; it builds the software project on top of it. Summary of what's proven vs. open (full detail + confidence labels are in that doc, section 73–74 and 80):

### Proven / high confidence
- Full container formats: `.VPX` (4-block RLE, interleaved planes → 320×200 indexed), `.P32`/`.256` palettes, `.PL8` sprite sheets, EMPIRE2 (1602-byte 40×40 strategic map + prefix).
- City map is a runtime-allocated **100×100** tile grid (`43A5` segment), with four parallel 10,000-byte simulation layers: `A2C4` (numeric land value), `C9D4` (mixed bitfield: persistent/derived/service/prerequisite), `54A4` (signed unrest, -8..+50), `7BB4` (operational/connection state).
- The **70×50-byte object/actor table** — now understood as an actor/walker system (types 0–10 = city-coordinate actors, 11–13 = province/empire-coordinate special actors), not a building table.
- The empire-map pathfinder: 128-entry static property table, N/S/E/W direction bits, route source/destination markers (`4A`/`4B` = destination/object type 13, `61`/`41` = route sources, `4C`/`79`/`7A` = generated route states).
- The full save-file layout (`0xDF26` bytes, every block's offset/size/source identified) — a save is essentially a direct dump of all the runtime state above.
- Construction/action menu strings and infrastructure/construction toolbar taxonomy (from manual + executable strings cross-check).

### Explicitly unresolved (tracked as blockers below)
- The **construction command → object type → city tile ID → footprint** pipeline is not fully decoded (this is the single highest-value remaining RE target). **Resolved (Phase 5):** the dispatcher `DS:127C` and every command's seed tile and footprint (`docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md` sections 12-13).
- City terrain *generation* (new-game/new-city procedural terrain) source is not yet located — the manual states terrain is randomly generated per province/city, but the generator routine hasn't been pinned down. **Resolved:** `campaign::generate_city` (dispatch findings section 31.2).
- Heavy Industry and Market **do** run through the same 43A5 tile-dispatch mechanism as civic buildings (tiles `0xF3` and `0xF4`), confirmed by disassembly and by reproducing real saves' land value layer exactly — see `docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md` section 15. That corrects this document's earlier claim that they don't. Those handlers only do service propagation, though: the *economic* behaviour (goods, labour, sales) isn't in them and is still suspected to run through the object/actor system. Workshop may be the unidentified 3×3 building on tiles `0xF5`/`0xF6`; Fort hasn't been located. **Resolved:** the economy is `systems::economy` (dispatch findings section 25), the Workshop is `0xF5`/`0xF6` with its record table (19.1) and the Fort is a province command (28.7).
- `EDATA.CSR`, `CONTFRM.GD8`, `P_BLOCKS.PL8`, `TEMPLBIT.PL8`, and the VAS win/lose animation format are undeciphered. **Resolved (Phase 8):** dispatch findings section 34; `P_BLOCKS.PL8` is the panel pieces (section 38.3).
- The save **loader** (as opposed to the serializer) hasn't been reverse engineered, so the save layout is currently proven one-directional. **Resolved:** the loader reads exactly the writer's records (dispatch findings section 33.1), and `formats::save::write` writes files the engine reads.
- Renderer tile-lookup tables (how a tile ID maps to a sprite frame) are not yet recovered. **Resolved:** `docs/CAESAR_CITY_RENDERER_FINDINGS.md`.
- 480/120/720-byte save tables are unidentified. **Resolved:** the forum, barracks and workshop records (dispatch findings section 22).

These gaps don't block starting the engine — they block *specific* systems, so the roadmap below sequences work to stay unblocked as long as possible.

### 4a. Update — GOG package findings (new source material)

A GOG digital distribution of Caesar Deluxe has since been supplied and inspected; full detail is in `CAESAR_GOG_BUILD_FINDINGS.md` (written in the same confidence-labeled style as the main RE corpus, meant to be folded into it). Headlines relevant to this plan:

- **Continuity confirmed:** the package's `US/CSR.EXE` is byte-identical to the build already fully analyzed — nothing already documented changes.
- **A second, distinct `CSR.EXE` build now exists** (international/multi-language: English+German+French, smaller and not yet decompressed/analyzed) — a genuinely new, unblocked RE opportunity: bindiffing two known builds of "the same" logic is a fast way to confirm or refute provisional findings.
- **The `COHORT.CSR` "missing file" is resolved but reclassified:** it's not shipped content, it's a 14-byte runtime handoff file CSR.EXE writes before exiting to a battle screen. `LOADER.EXE`, `HIRES!.COM`, `COHORT.EXE`, and the two `.MAP` files remain genuinely absent from every archive supplied so far.
- **A concrete, actionable entry point for the battle-resolution system was found:** `CAESAR.BAT`'s launch loop shows `CSR.EXE` accepts a `cohort` command-line argument that triggers its own internal battle resolution (the manual's Tortoise/Assault/Flank/Charge screen, used when the external Cohort 2 product isn't installed). This significantly de-risks Phase 6 (masterplan/roadmap previously treated battle resolution as having *no* RE corpus at all to build from). **Corrected (2026-09-17, dispatch findings section 44):** `csr.exe cohort` resumes the game after Cohort 2 fought the battle; the internal battle screen runs from the province map (sections 27 and 38).
- **`.MDI` files (international build) are standard MIDI (SMF)** — zero custom format work needed, and they're a straightforwardly better music source than the original `.XMI`/AIL pipeline unless bit-for-bit 1993 fidelity is specifically wanted. **Corrected (2026-09-15, dispatch findings section 34.5):** they are re-orchestrations for General MIDI, not the `.XMI` cues in another container. Gaius plays the `.XMI` files through the AIL driver, transcribed in `audio/` (section 45), and does not use the `.MDI` files.
- One format false-alarm resolved (`MINIFONT.PL1` is just PL8 under a different extension — importer should sniff structure, not trust extensions), and one new open question raised (`CONTFRM.GD8` content varies by build/region, still undeciphered but now known to be localizable rather than static).

See the roadmap for how these land in specific phases.

---

## 5. Architecture

Adopting the layered model already proposed during RE (COMPLETE.md sections 76-77), made concrete as an engineering plan:

```text
┌─────────────────────────────────────────────────────────┐
│ Layer 4 — Tooling                                        │
│   scripts over the tools/ CLIs                           │
├─────────────────────────────────────────────────────────┤
│ Layer 3 — Original-format export                         │
│   write-back to EMPIRE2 / .SAV / resource formats         │
├─────────────────────────────────────────────────────────┤
│ Layer 2 — Normalized engine model                         │
│   CityMap, EmpireMap, ActorTable, ServiceLayers,          │
│   Economy/Population state, explicit Systems              │
├─────────────────────────────────────────────────────────┤
│ Layer 1 — Exact import (read-only, format-faithful)       │
│   VPX/P32/256/PL8 decoders, EMPIRE2 reader, save reader   │
└─────────────────────────────────────────────────────────┘
```

*In the code (2026-10-03):* `formats/` is Layer 1; `model/` is Layer 2; `systems/` is the simulation (called Layer 3 in the README and `CLAUDE.md`); save write-back is `formats::save::write` over `model::serialize`; `render/`, `ui/` and `apps/viewer/` draw and run it; Layer 4's tooling is `tools/` and `scripts/`.

### Engine module breakdown (from the reconstructed simulation model)

```text
gaius/
  formats/         # Layer 1: VPX, P32, PL8, 256, EMPIRE2, SAV codecs (pure, no game logic)
  model/           # Layer 2: CityMap, EmpireMap, Actor, ServiceLayers, SaveState structs
  systems/         # one file per system, mirroring the RE-recovered architecture:
    construction.cpp
    housing.cpp
    service.cpp        # A2C4/C9D4 propagation primitives
    water.cpp
    roads.cpp
    administration.cpp
    entertainment.cpp
    religion.cpp
    military.cpp        # cohorts, battle resolution
    economy.cpp          # heavy industry / workshops / markets (unblocked once RE'd)
    population.cpp
    empire_route.cpp     # pathfinder + route normalization
  render/          # resolution-independent renderer: sprite atlas + tile dispatch → screen
                   # (logical-resolution framebuffer concept, NOT hardcoded 320x200 blit)
  platform/        # windowing, input abstraction, config/save-path resolution per OS
    input/            # mouse+kb, touch, gamepad — normalized into one command stream
    window.cpp         # window mode, scaling, safe-area handling
    paths.cpp           # per-OS config/save directories
  save/            # Layer 3: SAV writer, EMPIRE2 writer
  scripts/         # Layer 4 for 1.0: asset export, save inspection and checks over tools/
  tools/           # CLI utilities: dump_vpx, dump_pl8, decode_save, empire_view, etc.
docs/
  (FORMATS.md, the findings addenda and IGA_ENTRY.md; this plan and the roadmap sit at the root)
```

*The file names above are the plan's.* What exists is in the README's repository layout: `systems/` holds service, housing, month, actors, construction, economy, military, battle, province, plebs, administration, campaign, forum, messages and sounds. Water, roads, religion and entertainment turned out to be parts of `service`, `construction` and `housing`; there is no `empire_route`, `population` or `save/` module (the province map's road trace is in `province`, population in `housing`, saving in `formats/save`).

### Tech stack

- **C++17**, because a lot of the low-level bit-twiddling (RLE decode, packed structs) is exactly the kind of thing C++ handles cleanly and the Python prototypes (`caesar_vpx.py`, `empire2_tools.py`, `caesar_save_layout.py`) already validate the algorithms.
- **SDL2** (SDL3 if stable enough by the time Phase 1 starts) for windowing/rendering/input — chosen specifically *because* it already has first-class backends for Windows/Linux/Android, plus a known-good track record on Steam Deck (via Proton or native Linux build). No upstream engine to embed here — Gaius *is* the engine, so it owns its own rendering loop and must own cross-platform concerns directly rather than inheriting them.
- Rendering target: an internal logical-resolution framebuffer (period-accurate 320×200-style coordinate space for game logic and original sprite alignment) composited to an arbitrary physical window/display size at draw time. This is the single decision that makes resolution/windowed-mode/DPI-scaling/touch-hit-testing all tractable later instead of requiring a rewrite — see section 5a.
- Python prototypes remain the scratch/validation layer: fastest way to test a new RE hypothesis against real files before porting to C++. Every Python tool in the corpus should eventually have a C++ equivalent in `formats/`, but the Python stays in the repo as a `tools/re/` sandbox — it's cheap to keep and useful for future RE work. (As of 2026-10-03 there is no `tools/re/`; `scripts/` holds the Python this repo ships.)
- Data interchange for tooling: PNG for graphics, JSON/XML for map/scenario metadata.
- Build system: CMake with per-platform toolchain files (Android NDK, standard desktop) from the start, not bolted on later — this is what actually determines whether Phase 0's scaffolding pays off on mobile.

## 5a. Platform targets & QoL — designed in from Phase 1, not retrofitted

Cross-platform and QoL are treated as architecture, not polish, because retrofitting resolution-independence or input abstraction into a codebase that assumed one mouse and one fixed 320×200 canvas is exactly the kind of rewrite that kills reimplementation projects. Julius/Augustus already prove this genre of game runs comfortably on all of the targets below, so the ceiling is known — the discipline is just not painting ourselves into a desktop-only corner early.

### Target platform matrix

| Platform | Input | Notes |
|---|---|---|
| Windows | mouse+kb, optional gamepad | primary dev target |
| Linux | mouse+kb, optional gamepad | primary dev target; also the Steam Deck's native OS |
| Steam Deck | gamepad + touch, docked mouse+kb | needs a gamepad-navigable UI, not just "keyboard remapped to buttons"; Steam Input glyph awareness is a nice-to-have, not required at launch |
| Android | touch, optional gamepad/mouse (DeX-style) | needs a genuinely touch-first control scheme for build/scroll/select, not a mouse emulation layer |

### Cross-cutting requirements this implies

1. **Resolution independence.** The renderer (see tech stack above) separates *logical* game-space coordinates from *physical* pixels. Every place the original code does pixel-math against a fixed 320×200 (e.g. the empire-map screen-to-cell conversion `column = (screen_x + 8) / 16 + camera_x`) must be expressed in logical units and scaled at the presentation layer, never hardcoded against a physical resolution.
2. **Window modes.** Windowed / borderless-windowed / exclusive-fullscreen, arbitrary resize, and remembering the last-used mode+size per platform (desktop) vs. platform-dictated (mobile always effectively "fullscreen," Steam Deck has its own fixed panel).
3. **UI scaling.** A single scale factor (or a small number of breakpoints: phone / handheld / desktop / tv-distance) that resizes toolbar icons, text, and hit targets together — this is what makes the same UI usable on a 6" phone and a 34" monitor. Original toolbar icon sizes are a floor, not a ceiling, on touch targets.
4. **Input abstraction.** One internal "command stream" (`select`, `secondary-action`, `drag-path`, `scroll`, `context-menu-equivalent`) that mouse+keyboard, touch, and gamepad all feed into, rather than three parallel UI implementations. The original's command-mode/scroll-mode toggle (right-click) needs an explicit touch equivalent (e.g. two-finger tap or a dedicated mode button) and gamepad equivalent (shoulder button) decided once, here, rather than per-screen later.
5. **Performance budget for the low end.** Older Android devices are the floor. The original simulation tick operates on 100×100/40×40 byte arrays — trivially cheap by modern standards — so the actual risk is unoptimized rendering (full redraw per frame, uncompressed sprite atlases) rather than simulation logic. Budget: simulation tick negligible; rendering should target smooth performance on low-end phone GPUs via SDL2's GLES backend, with a battery/thermal-friendly capped frame rate option for mobile and Steam Deck.
6. **Save/config paths.** Per-platform conventions from day one (`%APPDATA%`/`~/.local/share`/Android app-private storage) via `platform/paths.cpp`, so save files and settings don't need a migration story later.
7. **Touch-first affordances**, not just "make touch technically work" (answered 2026-09-17, `platform/input.hpp`: one finger lays a drag-built command when one is selected and pans otherwise, two fingers always pan and pinch, a two-finger tap is the right button, and an Undo button replaces right-click-during-drag): drag-to-build (roads/walls/pipes) needs a touch gesture that doesn't fight with map-panning; a persistent zoom/scale control; confirm/cancel replacing the original's left-click-confirm/right-click-cancel convention in a way that's discoverable without a manual.

None of this requires new reverse-engineering — it's purely an engineering/UX layer sitting on top of the Layer 2 model, so it can proceed in parallel with RE-blocked phases (see roadmap Phase 5/6 blockers).

### Testing strategy

Because so much of this is "does our reimplementation produce byte-identical output to CSR.EXE for a given input," the test suite should be corpus-driven from day one:

- All 50 `EMPIRE2.0xx` files as round-trip fixtures (parse → re-serialize → byte-diff).
- The empire-map path-property table (already fully tabulated) as a direct data fixture — no reversing needed, just load-and-compare.
- Save files: once a save loader exists (even a partial one), diff every recovered block against `caesar_save_layout.py`'s block table.
- Golden-image tests for VPX decoding (`EMAP2.VPX` is already a known-good example with a rendered reference (`EMAP2_decoded.png`) in hand).

---

## 6. Immediate cross-references

- Full technical reference: `CAESAR_REVERSE_ENGINEERING_COMPLETE.md`
- GOG package findings addendum: `CAESAR_GOG_BUILD_FINDINGS.md` — fold into the main RE corpus; not yet merged as of this plan.
- Outstanding RE TODO (Appendix C of the main RE doc) — treat as a live backlog, not a one-time list.
- Original manual: `Caesar_1_Manual.pdf` — the semantic cross-check source for every system name (temples, bath houses, land value, etc.); never used to *override* executable evidence, only to name it once behavior is proven.
- See `GAIUS_ROADMAP.md` for the phased execution plan.
