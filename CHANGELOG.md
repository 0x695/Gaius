# Changelog

All notable changes to Gaius. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
version numbers are `x.y.z` (the `VERSION.txt` file). A release is the commit tagged `v<version>`; its notes are the
section below that carries its number (`scripts/release_notes.py`). How a release is made: [`docs/RELEASING.md`](docs/RELEASING.md).

## [Unreleased]

## [0.9.0] - unreleased

The first public beta: Gaius plays a whole career of Caesar, from the start screen to Caesar or dismissal, on Windows, Linux, the Steam Deck, Android and in a browser. It needs your own copy of the game; none is included.

### The game
- A new career from the start screen (funding, difficulty, the governor's name), then the city: roads, water, housing that grows and merges, services, industry, markets, temples, the Forum, walkers, fire, collapse and the barbarians' raids.
- The province: forts, Cohort orders (patrol, attack, go home), the Imperial Highway, towns, the Great Wall, and battles on the original battle screen with its four tactics.
- The Forum and its advisors (the Treasurer, the Tribune, the Legion, ratings, histories, industry, the governor), the maps screen, the map of the Empire, the yearly notices from Rome, and promotion through the Empire's ranks to Caesar, or dismissal if the tribute is missed.
- Saving and loading in the original's `.SAV` format, eight slots. Gaius writes to its own folder, never over the DOS game's saves.

### Faithful to the original
- The simulation is transcribed from the original executable and checked against real saves: the service and land-value layers match all 17 saves on every cell, a month of steps reproduces six consecutive saves, and the yearly accounts match each save's last year.
- The screens are drawn from the game's own files as the original draws them: the city view, the control bar, the message box, the Forum, the maps screen and the battle screen (several checked pixel for pixel against captures), and the original opening.
- Music and effects play through a transcription of the original's own sound driver on an emulated YM3812.

### Safety nets
- The game saves itself: at each year's end (or every 3 or 5 years, or never: Settings > Game > Autosave) and whenever a promotion is offered, into three rotating autosaves apart from your eight slots.
- F5 quick saves and F9 quick loads (rebindable). The Load page's *Autosaves* button lists the quicksave, the autosaves and the *Away* save, newest first.
- Time stops while the window or browser tab is not in front, and runs again when you return (Settings > Game > Pause when away). If the city has changed since it was last saved, leaving the window or closing the game saves it as the *Away* save, so building done while the clock was stopped is not lost.
- Saves and settings are written whole: a crash or a closed tab part-way through leaves the old file, not a broken one.
- In the browser: the page asks the browser to keep your saves, says so when it will not (Safari clears a site's data after about a week away), reminds you to download a backup, and takes that backup's zip back through *Saves > Add*.

### Robustness
- A damaged file gives a message, not a crash: the file decoders, the simulation, the Forum's pages and the city view are fuzzed under AddressSanitizer and UndefinedBehaviorSanitizer (`docs/ROBUSTNESS.md`), and a handful of problems that only a damaged file could reach were fixed.
- GOG's top folder (the international release beside a `US` folder) plays from the `US` folder; a folder with only the international release, or with files missing, is explained on the setup screen, the Settings screen and the browser page instead of starting a game without pictures.
- File names in any letter case work (`houses.pl8`, `Houses.PL8`), which a case-sensitive file system such as Linux's needs.

### Android
- Import finds Caesar's US folder inside the folder you pick (GOG's top folder works) and copies only that; the picker opens in Download; if the folder holds no complete US release it says so. A GOG top folder copied by hand into Gaius's game folder plays too, on every platform.
- The toolbar is the original's one-row bar on a phone, not a two-row list filling a third of the screen, and a button answers a finger anywhere in its slot.
- A tap on the map shows the building and its cost where you tapped (a finger cannot hover); tapping it builds it. A road, wall, plaza or clearing laid by a finger puts the tool away, so the next drag scrolls instead of laying more.
- Settings can always be left: a Back button on every tab, and Back or a two-finger tap leave them even when they were opened from the start screen.

### Comforts
- The Tribune of the Plebs looks after itself (Settings > Game > Tribune: Automatic, the default). The original leaves it to you: a new game starts with 10 pleb groups on each duty, which covers a city of about 160 buildings, and past that the shortfall is the chance, every month, of a fire, a collapse or worn roads. Left alone, a real 166-house city loses about half its houses in four years. Automatic does what a careful player does with the Tribune's page, once a month: it staffs the fire, building and road duties to what the city needs (from plebs nobody has given work, never from army duty) and sets the welfare so the plebs keep coming. *By hand* is the original's rule.
- Windows is one `gaius.exe`: the language files are inside it, so nothing else is needed. The release has it alone and in a zip with the readme and the licences.
- Any window size, fullscreen, UI scale 1 to 4, edge, key and drag scrolling, remappable keys and buttons, gamepad play (Steam Deck), touch gestures (Android and phones in the browser), a German translation of Gaius's own texts.
- Your choice of Gaius's gold pointer or the original's arrow, and of the original's game pace or a faster one.
- Game files are found without being told (GOG, Steam, the usual folders) or chosen; a browser version imports them from a dropped folder or zip.

### Tools
- Command-line tools for every file format and Python scripts to export the game's pictures, sprites, sounds, tunes and maps, and to list, render, compare and check saves.

### Known limitations
- The US release of Caesar is the one supported; the international release (a different executable) is not, and Gaius says so when it is given that folder.
- The Tower command is not available (its button is dimmed). The original's start-screen layout, the Golden Sector logo in the opening and a few hover marks are not reproduced.
- Not yet tried on a Steam Deck or a physical Android phone. The browser version has been tried in Chromium only.
- The Cohort 2 hand-over (the optional external battle program) has not been tried with the real program.
