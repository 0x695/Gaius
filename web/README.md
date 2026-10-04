# Gaius in the browser

The desktop game, compiled to WebAssembly with [Emscripten](https://emscripten.org) and wrapped in one page. Open the page, give it your own copy of *Caesar*, press Play. Nothing is installed and nothing is uploaded: the page keeps your game files and your saves in the browser's own storage (IndexedDB) on your machine.

It is the same `apps/viewer/main.cpp` as the desktop and Android builds, not a second game. The simulation, the screens, the sound driver and the save format are the code you can read in the rest of this repository.

## What the player sees

1. **Start.** The page loads the engine (about 2.2 MB of WebAssembly) and mounts the browser's storage at `/persist`.
2. **Bring the game.** The first time, a card asks for Caesar's files: drop the folder on the page, *Choose the folder*, or *Choose a .zip or files*. The page looks for a folder that holds every file Gaius cannot play without (`EMPIRE2.001`, `HOUSES.PL8`, `HOUSES2.PL8`, `FIXTS.PL8`, `MOREMEN.PL8`, `SHADE.256`, `FONT1.PL8`: the US build; a folder called `US` wins when a selection holds more than one), copies its files into `/persist/game` and remembers them. GOG's top folder is the international release and lacks some of them: the page says so and asks for the `US` folder (a zip of the top folder works, since it holds one), and keeps the files it already had. Names in any letter case are fine. Nothing is built into the page: no game file is part of this repository or of the site.
3. **Play.** The button starts the game at once (a click is what lets a page make sound). The game's own Settings screen works as everywhere; Escape opens it.
4. **Saves.** They live in `/persist/saves` as the original's `.SAV` files. *Saves* in the bar downloads them as a zip (a backup; the page puts a gold dot on the button when saves exist and none has been downloaded for two weeks), and adds `.SAV` files, or that zip, back: files with Gaius's own names (`CAESAR01`-`08`, `QUICKSAV`, `AUTOSAV1`-`3`, `AWAY`) return to where they were, any other (the original's, say) takes a free slot among `CAESAR01` to `CAESAR08`, which is what the Forum's load page lists. The game also writes `QUICKSAV.SAV` (F5, which the page keeps from reloading the tab), three rotating autosaves and `AWAY.SAV` (the Load page leads with the newest two, and its *Autosaves* button lists them all; the first autosave comes when the game's year turns, about a minute and a half into a game at the original's pace); each save goes into IndexedDB at once, not on the next four-second flush. *Game data* replaces or removes the imported game files.

The page works on a phone too (touch gestures are the Android build's: tap, drag, pinch, two-finger tap for the right button). A zip is the way to bring the files there, since phones cannot choose folders.

## Building

Install the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (the build was made with 6.0.11), activate it, and configure with its toolchain file:

```sh
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j
python -m http.server 8080 --directory build-web     # then open http://localhost:8080
```

On Windows, PowerShell, with `emsdk_env.ps1` run and a Ninja on `PATH`, add `-G Ninja`. The result in `build-web/` is `index.html` (the shell, `shell.html`), `gaius.js`, `gaius.wasm` and the page's own `gaius-web.js`, `gaius-web.css` and `banner.svg`. Any static host serves it; there are **no special headers** to set, because the build uses no threads (below).

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
- Browsers may clear a site's storage when disk space is short or on request, and Safari does after about a week without a visit. The page asks for persistent storage (`navigator.storage.persist()`, when you press Play: Chrome grants it to sites it sees used, Firefox asks you), shows a note on the start card when the browser has not granted it, and reminds you to download a backup from the *Saves* menu.
- The game stops its clock when the tab loses the focus (Settings > Game > Pause when away) and, if the city changed, writes `AWAY.SAV`. A browser throttles a hidden tab's timers, so the write can come a second or two after you switch away.
- The engine is not restarted inside one page: quitting the game and playing again reloads the page.
