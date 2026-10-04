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
| B | The right mouse button: back out; with a building chosen, switches between placing it and the toolbar |
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

Gaius shows the buttons as the Deck's own pictures: A B X Y as discs in green,
red, blue and yellow, the bumpers and triggers as L1 R1 L2 R2, View and Menu
with their marks, the d-pad with the arm that is bound, and the back grips as
L4 L5 R4 R5. They appear in Settings > Keys, next to each key, and on the
*Playing with a controller* page, which opens by itself the first time Gaius
starts with a controller connected and again from Settings > Keys >
*Controller help*; it lists what the buttons do now, whatever they are bound to.
A PlayStation or Nintendo controller keeps its own names (Cross, Circle, Plus)
in text.

Quick save and quick load (F5 and F9 on a keyboard) have no button by default,
since each pad has its own spare ones. Bind them in Settings > Keys, for
example to the back grips (L4 / R4) or the stick clicks (L3 / R3), or give the
grips the keyboard keys F5 and F9 in Steam Input.

## Saves

Besides the eight slots, the game saves itself at the end of each year (three
files in turn; Settings > Game > Autosave changes how often or turns it off)
and keeps the last city you left. The Load page puts the newest two at the top;
Load > *Autosaves* lists all of them. When Gaius is
not the window in front (another game, the Steam menu's task switcher) time
stops and runs again when you come back (Settings > Game > Pause when away).
