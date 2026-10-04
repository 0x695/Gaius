# Robustness: damaged files, odd folders, and how Gaius is checked

A player can point Gaius at a damaged download, a truncated save, a folder of another release of Caesar or a folder whose files are spelled in another letter case. Each of those must end in a message, never a crash. This page says what Gaius does about them and how that is checked.

## What the player sees

| Situation | What Gaius does |
|---|---|
| The folder is GOG's top folder (the international release) | Plays from the `US` folder inside it. If there is none: the setup screen says "That is the international release of Caesar. Gaius plays the US release: GOG keeps it in a folder called US." The browser page says the same when given such a folder or zip. |
| A folder with some of Caesar's files missing | The setup screen and the Settings screen's Files tab name the first three missing files. The files Gaius cannot play without are `platform::kEssentialGameFiles`. |
| Files named `houses.pl8`, `Houses.Pl8`... | Found: every loader goes through `formats::game_file_path`, which searches the folder for the name in any letter case. (A case-sensitive file system, Linux, needs this; Windows and macOS would have found them anyway.) |
| A damaged `.SAV`, `.VPX`, `.PL8`, ... | The decoders throw `formats::FormatError`, which the viewer reports ("unreadable" in a save slot, no art for a missing picture). Nothing else is ever thrown. |
| A save that loads but holds nonsense (a negative history index, a walker with a wild position) | The simulation, the Forum's pages and the renderer run on it without crashing. |
| A save written when the game was killed part-way | Cannot happen: saves and `gaius.cfg` are written to `<file>.tmp` and renamed. |

## How it is checked

**`tools/fuzz_formats`** feeds damaged files to every decoder and parser (the targets are listed in the file's header comment) and treats anything but acceptance or a `FormatError` as a failure: a crash, a hang, another exception, or an error a sanitizer reports. The inputs are real files when given `--assets` (the game's folder) and `--saves`, plus small synthetic ones that are always there (a VPX, a PL8, a VOC, an XMIDI file, an EXEPACK executable, a small city run for a month), mutated by bit flips, boundary words, truncation and chunk edits, deterministically from `--seed`. The `sim` target runs a mutated save for 500 frames (half of them across a year's end), fights battles on it, offers promotions, and builds every page the viewer builds from it; `render` draws it. A failure is reproduced with `--only TARGET --from N --cases 1` (and `--stack` on Windows prints where an exception was thrown).

```sh
cmake -S . -B build-san "-DGAIUS_SANITIZE=address;undefined"
cmake --build build-san -j --target gaius_tests fuzz_formats
./build-san/gaius_tests
./build-san/fuzz_formats --assets /path/to/Caesar/US --cases 30000 --seed 7
```

**CI** (`.github/workflows/build.yml`, the `sanitize` job) builds under AddressSanitizer and UndefinedBehaviorSanitizer on Linux, runs `gaius_tests` and two fuzz passes from the synthetic seeds. CI has no game files, so the corpus tests skip (2882 of 4647 checks run); the real-seed runs are local.

**On Windows**, MSVC has AddressSanitizer only: `-DGAIUS_SANITIZE=address` (the Visual Studio component "C++ AddressSanitizer"; put its `clang_rt.asan_dynamic-x86_64.dll`, in the compiler's `bin\Hostx64\x64`, on the PATH). For undefined behaviour without GCC or Clang there is a route through Emscripten and Node, which also tests the 32-bit build the browser runs:

```powershell
emcmake cmake -S . -B build-node -G Ninja -DGAIUS_NODE=ON -DGAIUS_SANITIZE=undefined
cmake --build build-node --target gaius_tests fuzz_formats
node build-node/fuzz_formats.js --assets E:/path/to/Caesar/US --cases 30000
```

`-DGAIUS_SANITIZE_RECOVER=ON` makes UBSan report every site and carry on instead of stopping at the first. Sanitized builds also turn on the standard library's own bounds checks (`_GLIBCXX_ASSERTIONS`, libc++'s hardening), which catch an index into a larger object that the sanitizers cannot see.

## What the first pass found (2026-10-03)

All of it in code the real files never exercised, and none on a real save:

- `systems::month::record_history`: a negative history index from a damaged save indexed far outside the table (an exception from a huge `resize`). Now wraps to 0.
- `systems::actors::table_word`: a workshop number past the table grew the record table (so the save could no longer be written). Now reads 0 and writes nothing.
- `audio::AilXmidi::note_on_event`: a note cut off by the end of an XMIDI file read past the buffer (found by AddressSanitizer). The event loop could also spin without waiting on a FOR/NEXT pair; it now gives up after 100000 events in one interval.
- The browser build is 32-bit (`size_t` is 32 bits), where `offset + 2 > size` on an offset read from a file can wrap and pass. Found by the Node build in `formats::vas`, `formats::gtl`, `formats::xmi` (also a `size_t{1} << 62`) and the AIL driver's chunk walk; the checks are written as remainders now.
- `systems::battle`: `x << 4` on a negative value (undefined in C++17), and slot arguments that were not checked; `formats::pl8`: `&data[size]` for an empty frame at the end of a file.

## Not covered

- The corpus tests (4647 checks with the game's files) do not run in CI, only locally.
- Gamepad, touch, window and audio-device code (`platform/`, the viewer's main loop) are not fuzzed; they take their input from the system, not from a file.
- The fuzzer starts from the files this project has: a different release's files are not among its seeds (the international release is detected before any file is read).
- The Cohort 2 hand-over (`csr0.dat` and `cohort.csr`, read back after an external program) is not fuzzed.
