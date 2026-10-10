# Scripts

Small Python scripts over the command-line tools in `tools/`, for players,
modders and anyone contributing saves. Each does one job on **your own copy**
of Caesar; no game file is part of this project.

Python 3.8 or later, standard library only. Build the project first
(`cmake --build build`): the scripts find the tools in `build/<Config>/` or
`build/`, in `$GAIUS_TOOLS`, or on `PATH`.

Where the game is: `--game FOLDER`, else `$GAIUS_GAME`, else
`$GAIUS_TEST_ASSETS`, else the folder Gaius itself uses
(`%APPDATA%\Gaius\game`, or `~/.local/share/gaius/game`). It is the folder
that holds `CSR.EXE`.

| Script | Does |
|---|---|
| [`export_assets.py`](export_assets.py) | every picture, sprite sheet, battle animation, sound effect, tune and scenario map to PNG, WAV and MIDI |
| [`list_saves.py`](list_saves.py) | one line a save: governor, date, rank, funds, people |
| [`render_saves.py`](render_saves.py) | the city in each save, drawn with the game's sprites |
| [`compare_saves.py`](compare_saves.py) | what differs between two saves, briefly or cell by cell |
| [`check_saves.py`](check_saves.py) | Gaius's simulation against saves; for contributors |
| [`record_session.py`](record_session.py) | while you play: every save either game writes, and what was on screen |
| [`extract_strings.py`](extract_strings.py) | Gaius's own texts for translation (`lang/template.txt`) |

## Exporting the game's files

```sh
python scripts/export_assets.py ~/caesar-export --game "/path/to/Caesar"
python scripts/export_assets.py export/assets --only pictures,sounds
```

Everything lands in one folder, with a `MANIFEST.txt` saying what each file is:

| Folder | Holds | From |
|---|---|---|
| `pictures/` | the 20 full-screen pictures, 320 x 200 | `.VPX` with the palette the game loads for it |
| `sheets/` | a contact sheet per sprite sheet, and `sheets/NAME/frame_NNN.png` for every frame (colour 0 transparent) | `.PL8`, `.PL1` |
| `animations/` | the battle screen's win and loss animations, a PNG a frame | `.VAS` |
| `sounds/` | the 23 effects, as stored (8-bit mono) | `.VOC` |
| `music/xmi/`, `music/xm2/` | the 28 tunes as Standard MIDI | `.XMI`, `.XM2` |
| `music/wav/` | with `--wav-music`: each `.XMI` as the game plays it, on an emulated AdLib | `.XMI` |
| `maps/` | the 50 provinces' maps | `EMPIRE2.000`-`049` |

A picture carries no palette of its own; the game loads one before it shows
each. The manifest says which was used and how sure that is. Most are
**definitive** (the engine's own load, or a capture of the real screen). A few
are **inference** (`LOGO`, `ROME`, `IMPRSEN`, `ROME2`) and two panel variants,
`PANEL1B` and `PANEL1C`, are **unresolved** (the executable never loads them):
they decode either way, but their colours are a best guess. `PANEL1A` and
`PANEL1D`, which the control bar uses, are in the city palette and match the
captures. The palette table is `PICTURES` in `export_assets.py`.

**The output is the original game's property.** The script refuses a folder
inside the Gaius repository unless git ignores it (`export/` is ignored), so it
can't be committed by accident. Don't share what it writes.

## Saves

```sh
python scripts/list_saves.py "/path/to/saves"
python scripts/render_saves.py "/path/to/saves" --out export/cities
python scripts/compare_saves.py CAESAR01.SAV CAESAR02.SAV
python scripts/compare_saves.py A.SAV B.SAV --full --cells 5
```

`list_saves.py` sorts by the game's own date, which is a signed year counter
(a new game begins below zero) and a month. `compare_saves.py` exits 0 if two
saves are identical, 1 if not.

## Contributing saves

Real saves are the cheapest validation Gaius has: each one is checked cell by
cell against the engine's own result. `check_saves.py` runs the checks.

```sh
python scripts/check_saves.py "/path/to/saves"
```

- **rebuild** rebuilds the service layers from a save's own city and compares
  them with what the engine saved. Seventeen real saves, all but two match
  exactly. The two that don't (`CAESARXS`, `CAESARXW`) were written *between* a
  month's steps, which a full month-end rebuild can't reproduce. A difference on
  a save you took isn't proof of a bug until you've ruled that out.
- **month** steps one save forward to another of the same city, up to two months
  later, and compares the tiles, records and population. A save has no step
  counter, so every start step is tried; the report says how many fit and how
  many steps apart the saves are.

Both are also the tools underneath: `sim_check` and `month_check` (exit 0 on a
match, 1 on a difference, 2 if a file can't be read), `save_inspect --summary`
(one `key: value` line a fact) and `save_diff`.

## Recording a play session

```sh
python scripts/record_session.py "/path/outside/the/repository/sessions"
python scripts/record_session.py --play-gaius
```

Start it, play the original in its DOSBox or Gaius (or one after the other),
and stop it with Ctrl+C. It keeps, in a folder a day:

| Folder | Holds |
|---|---|
| `original/saves/`, `gaius/saves/` | a copy of every save as it was written, named by the time (`213045_CAESARXX.SAV`). The original has few save names and Gaius's automatic saves rotate; here nothing is overwritten |
| `original/screens/` | the original's frame, 320 x 200, every five seconds and after each save: DOSBox's own screenshots (its Ctrl+F5, pressed for you while DOSBox is the window in front; Windows only) |
| `gaius/screens/` | Gaius's frame every five seconds, when it was started with `--capture-dir` (`--play-gaius` does) |
| `session.csv` | each save: the time, the game, and with the tools built its date, people and funds |
| `video/` | with `--video`: the session filmed by [OBS Studio](https://obsproject.com) (Windows), as `.mkv` |

The original's folder is `--original FOLDER`, else `$GAIUS_ORIGINAL`, else
where GOG installs it; its saves are looked for under `cloud_saves/US` first
(GOG's DOSBox writes them there) and then in the game's own folders. `--every
SECONDS` changes the pace of the pictures (0: the original's only after a
save).

The screenshot key is only ever sent to DOSBox, and not while you hold a key.
It reaches the game as a tap on Ctrl, which Caesar ignores. It is not sent
until DOSBox's title names the game (`Program: CSR`): DOSBox 0.74 ends with a
libpng error if it is asked for a screenshot before it has drawn its first
frame, so don't press Ctrl+F5 yourself while DOSBox is starting either.

`--video` runs OBS Studio for the length of the session, on a profile and a
scene collection of its own (`Gaius-recorder`, written again at each start):
the two games' windows and nothing else of the desktop, 1920 x 1080 at 30
frames a second, with the sound the PC plays and no microphone. OBS starts
already recording, in the tray, and is stopped and closed when the script is.
Eight seconds after a game first comes to the front the script asks OBS for a
still and says whether it has the game's picture (three beeps if it has not).
OBS films DOSBox both full screen and in a window; it films Gaius while Gaius
is the window in front.

Saves seconds apart are what `check_saves.py`'s month check wants, and the
pictures are the ground truth Gaius's screens are compared with. Like exports,
**a session is the original game's property**: the default folder,
`export/sessions`, is ignored by git, and a folder git would commit is refused.

## The tools behind them

| Tool | Does |
|---|---|
| `dump_vpx IN.VPX OUT.png [PALETTE]` | a picture |
| `dump_pl8 IN.PL8 OUT.png [PALETTE] [--cols N] [--frames DIR]` | a sprite sheet or font (`.PL1`) |
| `dump_vas IN.VAS BASE.VPX PALETTE PREFIX` | an animation, a PNG a frame |
| `dump_voc IN.VOC OUT.wav` | an effect, lossless |
| `xmi2mid IN.XMI OUT.mid [SEQUENCE]` | a tune as MIDI |
| `render_audio FOLDER NAME.XMI\|EFFECT OUT.wav [RATE]` | a tune or effect as the game plays it |
| `empire_view FILE --png OUT.png` | a province's map |
| `render_city SAVE FOLDER OUT.png` | a save's city |
| `save_inspect SAVE` | a save's blocks, named global words and walkers |
| `save_diff A B [--cells N]` | the difference between two saves |
| `sim_check SAVE`, `month_check A B` | the simulation against a save, or against two |
| `bindiff_exe A B` | two `CSR.EXE` builds, byte for byte |

## Tests

`python scripts/test_scripts.py` checks the scripts' own logic. It runs the
built tools too when they exist, and your game files when `GAIUS_GAME` or
`GAIUS_TEST_ASSETS` names them.
