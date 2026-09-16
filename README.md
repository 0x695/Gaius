# Gaius

An open-source reimplementation of *Caesar* (Impressions Games, 1992/93 DOS), in the spirit of Julius/Augustus for Caesar III. Sibling project to **IGDK** and **IGA**.

See `GAIUS_MASTERPLAN.md` and `GAIUS_ROADMAP.md` for scope, architecture, and the phased plan. **Phases 0-5 are complete**: the Layer 1 format library and CLI tools, the SDL2 platform layer and viewer, the Layer 2 data model, the service/housing/construction simulation systems, and a scalable build toolbar. See `docs/FORMATS.md` for per-format status and `docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md` for the reverse engineering behind the construction system.

## IP posture — read before doing anything else

This repo ships **no original game assets**, ever. Every decoder and every test that needs a real Caesar file reads it from a directory you point at via the `GAIUS_TEST_ASSETS` environment variable — supply your own legally-obtained copy of the game (e.g. from GOG). See `GAIUS_MASTERPLAN.md` section 3.

## Building

Requires CMake 3.16+, a C++17 compiler, and SDL2 development libraries (`libsdl2-dev` on Debian/Ubuntu, `sdl2` via vcpkg on Windows, etc. — needed for `gaius_viewer` and the `platform/` layer; the CLI tools and format library have no SDL2 dependency).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

## Running the tests

```sh
GAIUS_TEST_ASSETS=/path/to/your/caesar/files ./build/gaius_tests
```

Pure/synthetic tests always run. Corpus tests (round-tripping all 50 `EMPIRE2.0xx` files, the VPX golden-image pixel-diff against a reference render, the PL8 worked-example check, the EXEPACK golden decode) skip cleanly — not fail — if `GAIUS_TEST_ASSETS` isn't set or doesn't contain the relevant files.

## CLI tools

Built alongside the library in `build/`:

```sh
./build/dump_vpx   <in.vpx>  <out.png> [palette.p32|palette.256]
./build/dump_pl8   <in.pl8>  <out.png> [palette.p32|palette.256]
./build/render_city <CAESARxx.SAV> <game dir> <out.png> [col row cols rows]
./build/empire_view <EMPIRE2.0xx> --ascii | --summary | --png <out.png>
./build/save_inspect <CAESARxx.SAV>
./build/bindiff_exe <a.exe> <b.exe> [--strings]
```

## Viewer app (Phase 1)

```sh
./build/gaius_viewer <EMPIRE2.0xx>
```

Controls: middle-mouse-drag / single-finger touch-drag / gamepad left stick to pan, scroll wheel / gamepad triggers to zoom, F11 to cycle window mode, Escape to quit.

For headless/CI verification (no real display): `SDL_VIDEODRIVER=dummy ./build/gaius_viewer <EMPIRE2.0xx> --screenshot out.png --frames 3`, optionally with `--test-pan X Y` / `--test-zoom Z` to exercise the camera without real input devices.

## Repo layout

```text
formats/    Layer 1 — exact import (VPX, P32, .256, PL8, EMPIRE2, SAV, EXEPACK)
model/      Layer 2 — normalized data model (Phase 2): CityState/CityMap/Actor — see GAIUS_ROADMAP.md
systems/    Layer 3 — simulation systems: service.hpp/.cpp (Phase 3/5, A2C4/C9D4/54A4 propagation + DS:153A tile dispatch), housing.hpp/.cpp (Phase 4, land-value gate + population), construction.hpp/.cpp (Phase 5, DS:127C command dispatch + placement)
ui/         scalable toolbar (Phase 5) — metrics/font/toolbar, no SDL dependency
platform/   window/input/paths abstraction (Phase 1) — see GAIUS_MASTERPLAN.md section 5a
apps/       gaius_viewer (EMPIRE2 + .SAV) and android_hello (Android platform-layer smoke test)
android/    Android target — see android/README.md
tools/      CLI utilities built on formats/ and model/
tests/      dependency-free test harness (see above)
docs/       FORMATS.md and findings addenda
third_party/stb/   vendored stb_image / stb_image_write (public domain)
```

Later work (platform packaging, and `scripts/` for tooling) lands in later roadmap phases — see `GAIUS_ROADMAP.md`.

## Licensing

**Code: [GNU GPL v3.0 or later](LICENSE).** Chosen to match the convention of the
reimplementation community Gaius belongs to (OpenRCT2, OpenRA, OpenMW and OpenXcom
are all GPL-3.0), and so that the reverse-engineering work in this repo stays
available to that community rather than being absorbable into a closed product.

Note the one-way compatibility with the projects named above as inspiration:
Julius and Augustus are both **AGPL-3.0** (verified, not assumed), and AGPL-3.0
permits combining GPL-3.0 work — so they can adopt code from Gaius. The reverse
does not hold, which is deliberate: that's the direction that helps the wider
community. AGPL itself was considered and rejected, since its distinguishing
network-use clause is inert for a local single-player game.

**Documentation: [CC BY-SA 4.0](docs/LICENSE)**, covering `docs/`, `GAIUS_MASTERPLAN.md`
and `GAIUS_ROADMAP.md`. The reverse-engineering findings are arguably this repo's most
valuable output and a code licence fits prose badly; CC BY-SA keeps the share-alike
intent while making the findings cleanly quotable back into the wider preservation
corpus.

**Third-party components** keep their own licences and are not covered by the above:
`third_party/stb/` is public domain, and the vendored SDL2 Android glue under
`android/` is zlib-licensed — see [`android/README.md`](android/README.md) for the
attribution detail.

### This is not a licence for the game

The licences above cover **our own code and documentation only**. *Caesar* itself,
its assets, and its trademarks remain the property of their respective rights
holders; nothing here grants any right to them, and this repo distributes none of
them. That's a statement of fact about someone else's intellectual property, kept
deliberately separate from our copyright in our own work — see the IP posture
section above, and `GAIUS_MASTERPLAN.md` section 3.
