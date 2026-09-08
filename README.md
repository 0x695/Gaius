# Gaius

An open-source reimplementation of *Caesar* (Impressions Games, 1992/93 DOS), in the spirit of Julius/Augustus for Caesar III. Sibling project to **IGDK** and **IGA**.

See `GAIUS_MASTERPLAN.md` and `GAIUS_ROADMAP.md` for scope, architecture, and the phased plan. This repo currently implements **Phase 0**: the Layer 1 format library and CLI tools. See `docs/FORMATS.md` for exactly what's done and tested.

## IP posture — read before doing anything else

This repo ships **no original game assets**, ever. Every decoder and every test that needs a real Caesar file reads it from a directory you point at via the `GAIUS_TEST_ASSETS` environment variable — supply your own legally-obtained copy of the game (e.g. from GOG). See `GAIUS_MASTERPLAN.md` section 3.

## Building

Requires CMake 3.16+, a C++17 compiler, and SDL2 development libraries (`libsdl2-dev` on Debian/Ubuntu, `sdl2` via Homebrew on macOS, etc. — needed for `gaius_viewer` and the `platform/` layer; the CLI tools and format library have no SDL2 dependency).

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
platform/   window/input/paths abstraction (Phase 1) — see GAIUS_MASTERPLAN.md section 5a
apps/       gaius_viewer and future interactive apps
tools/      CLI utilities built on formats/
tests/      dependency-free test harness (see above)
docs/       FORMATS.md and findings addenda
third_party/stb/   vendored stb_image / stb_image_write (public domain)
```

Future layers (model/, systems/, render/, save/ write-back, editor/) land in later roadmap phases — see `GAIUS_ROADMAP.md`.
