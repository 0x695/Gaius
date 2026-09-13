# Caesar city renderer — findings

How the original draws the city view, read from the decompressed US-build `CSR.EXE` on 2026-09-13 and checked pixel for pixel against DOSBox captures of the running game. Implemented in `render/city_render.hpp`/`.cpp`; `tools/render_city` draws a save with it.

Addresses are flat offsets into the decompressed image, or `segment:offset` for far calls. Confidence labels follow the rest of the RE corpus.

## 1. Sprite sheets and palette

**DEFINITIVE** (load calls traced; each sheet's frames found verbatim in the captures).

| Sheet | Loaded at | Used for |
|---|---|---|
| `FIXTS.PL8` | `68F6:0000` (call at `0xFEED`, when `DS:0x6CAE` is 0) | terrain and every tile below `0xC8` |
| `HOUSES.PL8` | `54E0:0000` (`0xFD0E`) | buildings: frame = tile − `0xC8` |
| `HOUSES2.PL8` | `494C:0000` (`0xFD2D`, reloaded at `0xFDDD`/`0xFE92`) | building states and animation frames |
| `MOREMEN.PL8` | `630D:0000` (`0xFF0C`) | not used by the tile renderer |
| `SHADE.256` | — | the city view's palette: a capture's DAC equals it in all 256 entries |

With `DS:0x6CAE` set, `FIXT3.PL8` and `SPRITE2.PL8` load into `68F6` and `630D` instead. `FIXT3.PL8` frames turn up in the map and management captures, so this is probably the province view (STRONG INFERENCE).

## 2. Frames are unchained-VGA planes

`.PL8` pixel data is four streams, one after another, and pixel *i* of a frame (row-major) is entry *i*/4 of stream *i* mod 4. The tile blitter `303E:0BF0` shows why:
- it looks up the frame's descriptor, byte-swaps the big-endian pixel offset (`rol bx, 1` eight times), and addresses video memory as 80 bytes per row;
- it then writes 64 bytes per 16×16 tile once for each plane, selecting the plane through the sequencer map mask (`out 0x3C4`: `0x102`, `0x202`, …).

The game runs unchained 320×200 VGA, where each of the four planes holds every fourth column; the streams are those planes. So a frame's width must divide by 4, and every non-empty frame in every shipped sheet's does. The text routines `100F:1583` and `100F:168E` compute the same thing when they set a frame's stream length to (width ÷ 4) × height (`303E:[0x1E]`).

## 3. The draw loop

`0x1FF39` → `0x1FFD7` draws the visible 20 × 11 cells. The row loop is at `0x1FF72`; it stops at y = 176, above the control panel. Screen position is `303E:[0x08]` (x) and `[0x0A]` (y), in 16-pixel steps.

For each cell (`0x1FFEF`), `303E:[0x0E]` is set to the tile id, then:

- **Tile ≥ `0xC8`** → the building path (section 4).
- **Tiles `0x4A`–`0x5D`, `0x62`–`0x75`, `0x8A`–`0x91`** add `DS:0x57EA`, a 0-2 counter advanced every other frame (water animation).
- **Tiles `0x36`–`0x43`** call `0x20156`: if the cell's `7BB4` has bit `0x10`, the frame becomes `0x41` (the routine also clears `7BB4` bit `0x80`).
- **Tiles `0xA8`/`0xAB`/`0xAE`/`0xB1`** add `(DS:0x6D3A >> 2) & 1`.
- Anything still below `0xC8` is drawn by `303E:0BF0` as **`FIXTS.PL8` frame = tile id**, opaque.

`0x1FB94` is a second loop, for the overlay map modes. It draws `SHADE.PL8` frames chosen from coverage, water and similar layers (`DS:0x6D26` selects which) and isn't implemented.

## 4. Buildings (`0x20204`)

The frame sheet defaults to `HOUSES.PL8`, frame = tile − `0xC8`, with size from the table at `3496:14B2`: three bytes per tile from `0xC8` (footprint width, footprint height, and `extra` rows the sprite rises above its footprint). `HOUSES.PL8` frame *i* is exactly width × (height + extra) of tile `0xC8` + *i* for all 43 frames, `0xC8`-`0xF2`. The housing footprints agree with `systems::housing`: `CC` pairs 32×16, `D5` 32×32, `D7` 48×48, `DA` 16×32, `DF` 48×32.

Every cell of a building is drawn separately, by `303E:11B4`. It copies a 16×16 window of the sprite, at column (part & 3) × 16 and row ((part & `0xC`) >> 2) × 16 + extra, where part is the cell's `7BB4` low nibble (4·dy + dx, see `docs/CAESAR_CONSTRUCTION_DISPATCH_FINDINGS.md` section 16.4). It's an opaque copy.

After that, a cell in the building's top row (part & `0xC` = 0) draws the sprite's `extra` rows above itself with `303E:105C`, over the cell already drawn there. Index 0 is transparent in those rows: a capture shows the ground through them.

Special cases that switch to `HOUSES2.PL8`:

| Tile | Frame |
|---|---|
| `0xE8` bath house | 0 if `7BB4` bit `0x10` (watered), else 1; frame 32 of `HOUSES.PL8` on alternate animation ticks |
| `0xEA` bath house 2×2 | 2 if watered, else 3 |
| `0xF3` | 4 |
| `0xF4` market | 5; `0x18`-`0x1A` while trading (`DS:0x6D3C & 0x30`, `DS:0x6BF8` > 0, population ≥ 30) |
| `0xF5` / `0xF6` | top two rows: 6 / 7 (48×34, height forced to 32, 2 extra rows) |
| `0xF5` / `0xF6` bottom row (`7BB4` bit `0x08`) | no extra rows. The cell looks for an actor in the 30-entry, 24-byte table at `DS:0x585C` standing two rows up and up to two columns left. Left two cells draw 32×16 frame 8 + (actor byte `+0x10` & 7); right cell (`7BB4` bit `0x02`) draws 16×16 frame 16 + actor word `+0x04`. With no actor both read 0: frames 8 and 16. |
| `0xF1` | `HOUSES2` `0x29`-`0x2B` while `DS:0x6D3A` ≥ 70 and population ≥ 200; otherwise the default |
| `0xEC`, `0xEE` | animated variants gated on `DS:0x6D3E & 6`; otherwise the default |

The `0xF5`/`0xF6` bottom row reads a runtime actor table that isn't in the save. The renderer draws the no-actor frames.

## 5. Proof against the running game

`tests/test_formats.cpp` checks the decoder and renderer against 18 DOSBox captures (320×200 indexed, from real play; kept under `GAIUS_TEST_ASSETS/gaius_test_screens/`, never in the repo), at 6-bit DAC level:

- `test_pl8_matches_real_screenshots`: `HOUSES.PL8` frame 1, `FIXTS.PL8` frame 55 and the 64×64 `HOUSES2.PL8` frame 4 appear in captures pixel for pixel.
- `test_render_city_matches_screenshots`: buildings rebuilt at the cells where captures show them, rendered by `render_city`, match exactly:
  - the 3×3 `0xF5`, with its actor-less bottom row (2329/2329 pixels);
  - the 2×2 `0xED` (1024/1024);
  - `0xC9` (256/256);
  - the `0xF4` market (1024/1024);
  - the `0xCD` house pair rising 8 rows (768/768).
- `test_render_building_metrics`: the `3496:14B2` table against `HOUSES.PL8`'s frame sizes, 43/43.

A search of the captures with the decoded sheets finds 13 `HOUSES`, 13 `HOUSES2` and 72 `FIXTS` frames on the 16-pixel grid; with row-major pixels it found none.

## 6. Open

- Animation timing: which frame counters advance when (`DS:0x57EA`, `0x6D3A`, `0x6D3C`, `0x6D3E`).
- The actor table at `DS:0x585C`, which drives the `0xF5`/`0xF6` bottom row and probably the walkers drawn over the map.
- The overlay map modes (`0x1FB94`, `SHADE.PL8`).
- `MOREMEN.PL8`/`SPRITE2.PL8` (people and walkers), `FIXT3.PL8` and the province view.
- `HOUSES.PL8` frame 43 (8×16) and `HOUSES2.PL8` frames not listed above.

## 7. Text and the control panel

Loaded alongside the city sheets (`0xFBD3`-`0xFC58`): `P_BLOCKS.PL8` and `POINTERS.PL8` through `100F:0A65` (slots 1 and 2), `FONT1.PL8` at `54E0:C648`, `MINIFONT.PL1` at `54E0:C254`.

- **`100F:1583`, the main text routine.**
  - Frame = `DS:0F64[char − 0x20] − 1`, where 0 means no glyph: a–z → frames 0–25, A–Z → 26–51, digits 1–9 → 52–60, and 0 → 61. `:` and `;` also map to 61, which is what the table holds.
  - It advances 8 px for `FONT1`, 6 px for `MINIFONT`, and draws through `303E:13D6`.
  - **DEFINITIVE:** glyphs found in the captures and read back through this table spell the game's messages ("The province's funds are now at or below 1,000 Denarii. Excessive spending may bankrupt you"). `test_ui_game_font_matches_screenshot` draws the building menu's "Housing" pixel for pixel. Implemented in `ui/game_font.hpp`.
- **`100F:168E`, a second text routine.** Frame = `DS:1044[char − 0x20] − 1`: letters with case folded to frames 0–25, everything else frame 26; it advances by each frame's width. That's `ROMFONT.PL8`'s 27 frames of 16×17. The credits capture's "PROGRAMMING" matches (STRONG INFERENCE on which screens use it).
- **Panel icons are `POINTERS.PL8` frames.** They're drawn at y = 180, every 24 px from x = 8. The main city panel shows frames 28, 7, 19, 8, 11, 12, 10, 16, 14, 17, 18; the building menu shows another set. The frame order isn't stored as a plain byte or word table, so mapping icons to commands needs the panel code, which isn't traced yet.
- **`P_BLOCKS.PL8`** holds the frame, border and button pieces of the advisor and management screens: 31 of its 40 frames are found in those captures.
