# Gaius on the Steam Deck

Gaius needs your own copy of *Caesar* (1992, DOS; the GOG release works). It
ships no game files.

## Install

1. In Desktop Mode, unpack `gaius-linux-x86_64.tar.gz`, for example into
   `~/Games/gaius`.
2. Copy the Caesar game folder (the one holding `CSR.EXE`, `EMPIRE2.001`,
   `HOUSES.PL8` and the rest) to `~/.local/share/gaius/game`. Gaius also finds
   it beside itself, or wherever you point it from Settings > Files.
3. In Steam, *Add a Non-Steam Game*, browse to `~/Games/gaius/gaius`, and add
   it. Back in Gaming Mode it starts fullscreen.

Settings are kept in `~/.config/gaius`, saves in `~/.local/share/gaius/saves`.
Gaius never writes into the game's own folder, so the DOS game's saves are
safe.

## Controls

Steam's default layout for a non-Steam game, *Gamepad*, is the one Gaius
expects:

| Control | Does |
|---|---|
| Left stick | Move the pointer |
| A | Click (hold and move the pointer to lay roads and walls) |
| B | Back, the right mouse button |
| Right stick, d-pad | Scroll the map |
| Triggers | Zoom |
| X / LB | Next / previous building |
| RB | The building's variant (Forum grade, workshop goods) |
| Y | Pause or run time |
| View | Next screen: city, province, maps, Forum |
| Menu | Settings: speed, sound, controls, load, save, exit |

The touchscreen and trackpads work as a mouse too. Every button except the
sticks can be changed in Settings > Keys; the pointer can be switched off
there, which gives the left stick to scrolling.
