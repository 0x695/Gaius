# Gaius in the browser

The desktop game, compiled to WebAssembly with [Emscripten](https://emscripten.org) and wrapped in one page. Open the page, give it your own copy of *Caesar*, press Play. Nothing is installed and nothing is uploaded: the page keeps your game files and your saves in the browser's own storage (IndexedDB) on your machine.

It is the same `apps/viewer/main.cpp` as the desktop and Android builds, not a second game. The simulation, the screens, the sound driver and the save format are the code you can read in the rest of this repository.

## What the player sees

1. **Start.** The page loads the engine (about 2.2 MB of WebAssembly) and mounts the browser's storage at `/persist`.
2. **Bring the game.** The first time, a card asks for Caesar's files: drop the folder on the page, *Choose the folder*, or *Choose a .zip or files*. The page looks for the folder that holds both `HOUSES.PL8` and `EMPIRE2.001` (the US build; a folder called `US` wins when a selection holds more than one), copies its files into `/persist/game` and remembers them. Nothing is built into the page: no game file is part of this repository or of the site.
3. **Play.** The button starts the game at once (a click is what lets a page make sound). The game's own Settings screen works as everywhere; Escape opens it.
4. **Saves.** They live in `/persist/saves` as the original's `.SAV` files. *Saves* in the bar downloads them as a zip, and adds `.SAV` files from the original game or another copy of Gaius (as slots `CAESAR01` to `CAESAR08`, which is what the Forum's load page lists). *Game data* replaces or removes the imported game files.

The page works on a phone too (touch gestures are the Android build's: tap, drag, pinch, two-finger tap for the right button). A zip is the way to bring the files there, since phones cannot choose folders.

## Building

Install the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (the build was made with 6.0.11), activate it, and configure with its toolchain file:

```sh
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j
python -m http.server 8080 --directory build-web     # then open http://localhost:8080
```

On Windows, PowerShell, with `emsdk_env.ps1` run and a Ninja on `PATH`, add `-G Ninja`. The result in `build-web/` is `index.html` (the shell, `shell.html`), `gaius.js`, `gaius.wasm`, `gaius.data` (the language files) and the page's own `gaius-web.js`, `gaius-web.css` and `banner.svg`. Any static host serves it; there are **no special headers** to set, because the build uses no threads (below).

## How it differs from the desktop build

| | Desktop | Browser |
|---|---|---|
| Main loop | A blocking `while (running)`. | The same loop, run with Emscripten's **Asyncify**: `SDL_Delay` and `SDL_RenderPresent` hand control back to the browser each frame (`-sASYNCIFY`). No `SharedArrayBuffer`, so no cross-origin isolation, and GitHub Pages can host it as it is. |
| Files | Per-OS folders (`platform/paths.hpp`). | `/persist/{settings,saves,game}` on **IDBFS**. The page loads it before the game starts; the game asks the page to write it back every few seconds (`platform::web::tick`) and the page does so when it is hidden or closed. |
| Game files | Found on disk, or chosen in a dialog. | Imported by the page (`gaius-web.js`) from what the player drops or chooses, validated, and stored. |
| Window | SDL's window, letterboxed. | The canvas is sized by the page (CSS, times `devicePixelRatio`), and `Window::present_rgb24` follows it, so the picture stays sharp at any zoom and on high-density screens (`platform/web.cpp`). |
| Sound | SDL audio, on its own thread. | SDL audio through Web Audio on the main thread. Browsers start it after the Play click. |
| Quit | Closes the program. | *Gaius has closed* with a Play again button (the page reloads: the engine is not restarted inside one page). |

The platform shims are `platform/web.hpp` / `web.cpp` (the canvas size, the storage flush), `Os::Web` in `platform/paths.hpp`, and `web/` here. `GAIUS_WEB` is set by CMake when the compiler is Emscripten; the tools and the tests are not built then.

## Developer switches

Both are read from the page's address and are for hosting and for tests:

- `?args=--mute%20--no-intro` passes arguments to the game (the same ones as the desktop, see `apps/viewer/main.cpp`).
- `?data=URL&autoplay=1` fetches a **zip** of the game folder from `URL` (same origin, or a host that allows it), imports it as if it had been dropped, and starts. Use it with your own copy only; do not host game files.

## Publishing

`.github/workflows/pages.yml` builds the page on every push and pull request (so the web target is checked like the others) and publishes it with GitHub Pages. Two switches turn publishing on, once: in the repository's *Settings > Pages* set the source to *GitHub Actions*, and under *Settings > Secrets and variables > Actions > Variables* add `GAIUS_PAGES` = `true`. After that every push to `master` publishes the site at `https://<owner>.github.io/<repo>/`.

## Limits

- Chrome, Edge, Firefox and recent Safari are the targets; only Chromium has been used so far. Compressed zips need `DecompressionStream`; the folder picker (`webkitdirectory`) is missing on phones, which is why zips are accepted.
- Chromium logs a one-time notice that SDL 2's Web Audio backend uses the deprecated `ScriptProcessorNode`. It is SDL's, not the game's, and it still works.
- Browsers may clear a site's storage when disk space is short or on request; download your saves from the *Saves* menu to keep them safe.
- The engine is not restarted inside one page: quitting the game and playing again reloads the page.
