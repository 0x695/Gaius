# Gaius — FORMATS.md

Status of every Layer 1 (exact import) format decoder implemented so far. This is the Phase 0 deliverable named in `GAIUS_ROADMAP.md`. For the full reverse-engineering narrative behind each format, see `CAESAR_REVERSE_ENGINEERING_COMPLETE.md` in the RE corpus — this file only documents what's actually implemented and tested in `formats/`.

All formats below are implemented, unit-tested, and (where a real asset was available) validated against actual Caesar (1992) files. **No game assets ship in this repo** — every test that needs one reads from a directory named by the `GAIUS_TEST_ASSETS` environment variable and skips cleanly if it isn't set.

---

## `.P32` — palette (`formats/p32/`)

- 64 bytes, 32 packed 16-bit colors, little-endian.
- Bits 0-3 = R, bits 4-7 = unused, bits 8-11 = B, bits 12-15 = G. Each nibble is a VGA DAC value of `n * 4` (0..60), widened to 8-bit like `.256`: `(dac << 2) | (dac >> 4)`, so 15 -> 243.
- **Status: DEFINITIVE, tested against the running game (2026-09-13).** The `* 4` comes from DOSBox screenshots: the map and forum screens hold `EMAP2.P32`/`FORUM32.P32` in their DAC as exactly `n * 4` (32/32 each). `EMAP2.VPX` + `EMAP2.P32` matches the map screenshot at 6-bit level on every pixel outside the game's province-marker/status overlay (46,351/46,351). The decoder used `n * 17` (15 -> 255) until then; `EMAP2_decoded.png` was generated the same way, so the golden test now compares it at nibble level.

## `.256` — palette (`formats/pal256/`)

- 768 bytes, 256 colors × 3 bytes, each channel a 6-bit VGA DAC value (0-63), expanded via `(v << 2) | (v >> 4)`.
- **Status: DEFINITIVE, validated against real files and the executable (2026-09-13).**
  - **In `CSR.EXE`:** the generic loader (flat `0x10B0E`) reads the whole file into a buffer; `100F:0B50` records the buffer; `2EF9:0CF4` passes it untouched to BIOS INT 10h AX=1012h (set DAC block, BX=0, CX=256). So there is no header and no transform: the file *is* the DAC block. In a non-VGA mode the same routine programs only entries 0-15. The 8-bit expansion is a display convention, not something the executable does.
  - **Real files:** all eight US-build `.256` files (`IMPRLOGO`, `NEWFORUM`, `PANEL1`, `ROME1`, `SHADE`, `TEMPLE`, `TITLE3`, `WAR2`) are 768 bytes with every byte in 0..63. The decoder now rejects a byte above 63 rather than masking it.
  - **Cross-check with `.P32`** (itself proven by the EMAP2 golden image): `PANEL1.256`'s first 32 entries are exactly `PANEL1.P32`'s nibbles times 4, and `SHADE.256` matches `SHADE.P32` on entries 0-19. Rendering `PANEL1.VPX` through each gives the same DAC value at all 64,000 pixels. `test_pal256_corpus` checks all of this.
  - **Visual:** `TITLE3.VPX` and `WAR2.VPX` rendered with their own `.256` files (`dump_vpx`) show the correct title and battle screens.
  - **Against the running game (2026-09-13):** 18 DOSBox screenshots from real play (320×200, 8-bit indexed, each carrying the live DAC palette; kept in `GAIUS_TEST_ASSETS/gaius_test_screens/`, never in the repo). Every city-view screenshot's palette equals `SHADE.256` in **all 256 entries** — so `SHADE.256` is the city palette — and the forum screen's equals `NEWFORUM.256` in all 256. Screenshot tools disagree on 6→8-bit widening (some write `v << 2`, some `(v << 2) | (v >> 4)`), so comparisons are made at 6-bit level.
- **`.P32` in the running game is nibble × 4.** The map screenshot's DAC holds `EMAP2.P32` as `n * 4` in entries 0-31 (32/32; the rest zero), and the forum screenshot holds `FORUM32.P32` the same way. So a `.P32` nibble of 15 is DAC 60, not 63. The `.P32` decoder now expands that way (it had used `n * 17`, up to 12/255 too bright) — see the `.P32` section.

## `.VPX` — graphics container (`formats/vpx/`)

- 4 blocks, each: 8-byte header (packed_size, decoded_size, fill_pair, field3) + RLE-compressed payload.
- Per-command RLE: top 2 bits select explicit-byte / fill-A / fill-B / literal-run, bottom 6 bits + 1 = count.
- Each block decodes to exactly 16000 bytes; the four streams interleave 1-byte-at-a-time into a 320×200 (64000-byte) indexed framebuffer — **not** four separate bit planes.
- **Status: DEFINITIVE, tested against a golden image and the running game.** `EMAP2.VPX` decoded + `EMAP2.P32` applied reproduces `EMAP2_decoded.png` with **zero pixel mismatches out of 64,000** (compared at nibble level — see `.P32`), and its colour indices equal a DOSBox screenshot of the map screen on 63,144 of 64,000 pixels, every difference falling inside the province markers and status text the game draws on top.

## `.PL8` — sprite sheet (`formats/pl8/`)

- Container header: `uint16 unknown_a`, `uint16 frame_count` (4 bytes total).
- Per-frame descriptor (8 bytes): `pixel_offset` (big-endian u16, i.e. reversed byte order vs. everything else in this format), `width` (u8), `height` (u8), `x` (u16 LE), `y` (u16 LE). Pixel data lives at absolute file offset `pixel_offset`, not inline.
- **Status: HIGH CONFIDENCE, tested against a real worked example.** Verified byte-for-byte against `HOUSES.PL8`'s documented first frame (offset 0x0194, 16×16, x=0, y=0) and cross-checked against `MINIFONT.PL8`.
- **Two things resolved here that weren't fully documented upstream:**
  1. **Which header word is the frame count.** The main RE corpus flagged "the first/header word preceding the descriptor array is still not fully understood." Empirically: it's the *second* u16 (byte offset 2), not the first — confirmed exactly against `HOUSES.PL8`'s documented 50-frame count. The first u16's meaning (42 for HOUSES.PL8, 61 for MINIFONT.PL8) is still open.
  2. **Trailing placeholder frames.** `HOUSES.PL8`'s last 6 of 50 descriptors (frames 44-49) all have `pixel_offset` sitting exactly at end-of-file with a nonzero declared width/height that would overrun the file if dereferenced. The decoder treats "pixel_offset == file size" as a valid empty frame (no pixel data) rather than an error; any other case where declared size overruns the file is still a hard error. Why these placeholders exist (spare slots? unused housing variants?) is not yet explained — flag back to the main RE corpus if a consumer of these specific indices ever turns up in the disassembly.
- **Pixels are four interleaved streams (DEFINITIVE, 2026-09-13 — corrects this decoder's original reading).** A frame's `width*height` bytes are four streams stored one after another, interleaved one pixel at a time exactly like `.VPX`: row-major pixel `i` is entry `i/4` of stream `i%4`. Every frame in every shipped `.PL8` has a pixel count divisible by 4, with frames packed back to back; the decoder rejects one that isn't. The original row-major reading was "validated" against `HOUSES.PL8` only at descriptor level — its pixels were never checked, and were wrong.
  - **Proof, against DOSBox screenshots of the real city view** (see the `.256` section): with this layout 13 `HOUSES.PL8`, 13 `HOUSES2.PL8` and 72 `FIXTS.PL8` frames appear in the screenshots verbatim, on the 16-pixel tile grid, drawn with `SHADE.256`; read row-major or column-major, none do. `test_pl8_matches_real_screenshots` pins three of them, including the full 64×64 `HOUSES2.PL8` frame 4.
- **Both halves of the old sprite gap are closed by that:**
  1. **City-content palette: `SHADE.256`.** The city screenshots' DAC palettes equal it in all 256 entries, and the city sheets (`HOUSES`, `HOUSES2`, `FIXTS`, `FIXT3`) only use indices 0-15. It had been dismissed as a UI palette; `dump_pl8 <sheet> <out> SHADE.256` now renders city content correctly.
  2. **Font sheets: the same interleave.** With it, `FONT1.PL8` renders as a legible font (lower case, upper case, digits, punctuation). The note below is kept as the record of how the problem was narrowed; its conclusion (a pixel-path problem, not a palette one) was right. `ROMFONT.PL8` and `FONT2.PL8` haven't been inspected since the fix, and `MINIFONT.PL1` is a different format (its offsets don't step by `width*height`) and is still undecoded. **Old note:** `FONT1.PL8` and `FONT2.PL8` (100 frames of 8×8 each), `ROMFONT.PL8` (27 frames of 16×17) and `MINIFONT.PL1` all parse their *descriptors* correctly — frame count and per-frame width/height/origin all read as sensible, self-consistent values. The *pixels* decode to noise. The palette was ruled out as the cause directly: dumping `FONT1.PL8` with no palette shows only **10 distinct index values, all within 0..11**, with index 0 dominant (background) and index 11 the clear ink colour — exactly the shape a low-colour glyph sheet should have. Remapping those indices to a high-contrast ramp still produces no glyph shapes at any layout tried. So the indices are plausible and the palette is irrelevant; what's wrong is that PL8's pixel path handles `HOUSES.PL8` (the documented worked example it was validated against) but not the font sheets' encoding variant. One suggestive detail for whoever picks this up: the font descriptors give 8-wide frames whose `x` origins step by **16**, not 8, which may mean the stride/packing differs for these sheets.
- **Confirmed working, and worth knowing before assuming "no sprites render":** the UI/panel content decodes *correctly today*. `PANEL1.VPX` + `PANEL1.P32` (or `PANEL1.256` — the two agree on every index the file uses) renders the real in-game bottom control panel: a full-width textured bar occupying **rows 176..199** of the 320×200 screen, i.e. 24 rows tall, bottom-anchored. `P_BLOCKS.PL8` (40 frames of exactly 16×16) holds panel pieces — frames, scroll arrows, small buttons — as of the interleave fix (the earlier "texture/pattern blocks" reading came from the wrong pixel layout), not per-building toolbar icons. `PANEL1A`-`PANEL1D.VPX` do *not* render correctly with `PANEL1.256` (they use high palette indices and come out garish), so they have their own palette that hasn't been matched yet. Those measurements are the source of `ui::kOriginalPanelH`/`kOriginalIconPx`.

## `EMPIRE2.0xx` — strategic map (`formats/empire2/`)

- Exactly 1602 bytes: 2-byte prefix (semantics unresolved — never treat as width/height) + 1600-byte 40×40 cell array.
- **Status: DEFINITIVE, tested against the full corpus.** All 50 supplied `EMPIRE2.000`-`EMPIRE2.049` files round-trip load→save byte-identical.

## `CAESARxx.SAV` — save file (`formats/save/`)

- Exactly 57126 (0xDF26) bytes, split into 20 confirmed contiguous blocks (global words, 70×50 object table, four 10000-byte city simulation layers, embedded EMPIRE2 map, several smaller unidentified tables, final state).
- **Status: READ-ONLY, block table VALIDATED AGAINST REAL SAVES (2026-09-12/13).** Four saves of one city from a real US-build play session (fixtures live in `GAIUS_TEST_ASSETS/gaius_test_saves/`, never in the repo) are each exactly 57126 bytes; `model::load` -> `model::serialize` reproduces all four byte-for-byte; every Phase 5 building footprint appears in their tile grids as a full rectangle of exactly its transcribed size, which also confirms the row-major grid layout; and one reset-plus-dispatch pass with `systems::service` reproduces each save's A2C4 coverage layer and C9D4 service bits cell-for-cell (10000/10000). The save *loader* still hasn't been disassembled, so fields this project doesn't model are carried as opaque bytes rather than interpreted -- but the block boundaries are no longer merely trusted.
- **`global_words_128` is NOT a descending DS range.** The save writer (flat `0x033F2`) makes 128 two-byte writes in an order with 15 discontinuities: it skips `DS:0x6CB8`, writes `DS:0x6C0E` twice (save+`0xC2` and save+`0xDC`), and steps upward in places. The old "descending from `DS:0x6CE2`" rule was wrong for 107 of the 128 words. The exact table is `formats::save::kGlobalWordDsAddress`. It is confirmed three independent ways: the treasury `DS:0x6CA2` (save+`0x3E`) falls 7182 -> 4692 -> 3210 -> 3144 across the session; the twice-saved word holds identical values in both copies; and two tick statistics, `DS:0x6BF2` and `DS:0x6BF0`, equal the tile grid's own building and road counts exactly in every save.
- **Where every block lives at runtime (2026-09-13, DEFINITIVE — read from the save writer's write calls, flat `0x040DD`-`0x04516`).** After the 128 global words, in file order:

  | Block | Runtime address | Size | What it is |
  |---|---|---|---|
  | `objects_70x50` | `DS:0x5D84` | 70×50 | the actor table: word +0 sprite frame, +2/+4 world x/y, byte +6 active, +7 type, word +8 own index (see `model::Actor`; the renderer at flat `0x6946` reads these; every other field is laid out in `systems/actors.hpp` and dispatch findings section 20) |
  | `table_480` | `DS:0x5BA4` | 30×16 | forum records (tiles `0xE0`-`0xE7`, which the corpus called temples): +0 column, +2 row, +4 grade, +6 timer, +8 active — `construction::place_forum` |
  | `table_120` | `DS:0x5B2C` | 10×12 | barracks records (tile `0xEF`): +0 column, +2 row, +6 active (`0x12A94`) |
  | `table_720` | `DS:0x585C` | 30×24 | workshop records (tiles `0xF5`/`0xF6`): +0 column, +2 row, +4 goods, +6 timer, +8 active, +0x10 production level; they also drive the bottom-row sprites (renderer `0x20613`) — `construction::place_workshop` |
  | `city_tiles_100x100` | `43A5:0000` | 10000 | |
  | `cell_flags_c9d4`, `cell_value_a2c4`, `cell_flags_7bb4`, `cell_value_54a4` | `3496:C9D4`, `A2C4`, `7BB4`, `54A4` | 10000 each | |
  | `empire2_1602` | `3496:2752` | 1602 | |
  | `table_50` | `DS:0x581E` | 50 | one flag per province already given (`0x289B0` sets the flag of the current province `DS:0x6CA6`) -- findings section 22 |
  | `table_8` | `DS:0x5816` | 8 | workshops per goods type (bytes) |
  | `table_10` | `DS:0x580C` | 10 | population milestone flags: 200, 1000, 2000, 4000, 8000, 12000, 16000, 20000 people (`0x27BA1`); bytes 8-9 unused |
  | `table_60_a`-`d` | `3496:01F0`, `022C`, `0268`, `02A4` | 60 each | yearly history, 15 (year, value) word pairs each: `DS:0x6BC6`, `DS:0x6BC4`, the treasury `DS:0x6CA2`, population units `DS:0x6C10` |
  | `table_72` | `3496:02E0` | 72 | yearly history of `DS:0x6BB6`, 17 records used of 18 |
  | `final_state` | 34 words: `DS:0x6BB4`, `6BB2`, `6BB0`, `6BAE`, `6BAC`, `6BAA`, 12 bytes at `6B9E`, then `6CB8`, `6C02`, `6BFC`, `6C98`, `6C96`, `6C94`, `6C90`, `6C8E`, `6C8C`, `6C8A`, `6C88`, `6C86`, `6C84`, `6C82`, `6C80`, `6C7E`, `6C5C`, `6BEE`, `6BEC`, `6BEA`, `6BE8`, `6BE6` | 68 | globals, including the monthly scan counters (`6BEA`-`6BEE`) and the event targets `0x2DF7D` rolls (`6C88`) |

  The blocks' meanings beyond these addresses are still mostly open, but each now has a place in memory that code can be searched for.
- **The embedded EMPIRE2 block is scenario plus game state.** In these saves it is `EMPIRE2.047` with two bytes changed: the player city's province cell (col 21, row 23) has bit 7 set (`0x4A` -> `0xCA`), exactly where the province actor stands, and cell (col 0, row 17) goes `0x41` -> `0x78`, unexplained. One scenario, one session: STRONG INFERENCE, not yet a rule.
- **Actor coordinates.** For city actors (types 0-10), `packed_xy` (+0x18) is the reliable position -- `screen_x / 16` agrees with its column. `raw_x/raw_y` (+0x12/+0x13) are not the current cell for city actors, though they are for province actors. See `model/city_state.hpp`.
- **`cell_flags_7bb4`'s low nibble is a part index.** Every cell of a multi-cell building holds its position in the building, `4*dy + dx` (0 is the anchor), written by construction and by housing growth; the housing pass only develops anchors. It holds on every multi-cell building in four real saves.
- **Calendar and population globals** (in `global_words_128`, addressed through `kGlobalWordDsAddress`): month `DS:0x6C1C` (0-11); year `DS:0x6C32` (signed -- the saves run -11 to -1, consistent with BC dates, STRONG INFERENCE); population units `DS:0x6C10` and population `DS:0x6C0E` (4x); land-value growth base `DS:0x6BF6`; housing coverage base `DS:0x6BF8`; last month's building and road counts `DS:0x6BF2` and `DS:0x6BF0`.

---

## `EXEPACK` — DOS executable decompression (`formats/exepack/`)

- Generic Microsoft EXEPACK decompressor (not Caesar-specific, but required for any static analysis of `CSR.EXE`, which is EXEPACK-packed).
- **Status: DEFINITIVE, validated against two independent real builds.** Full algorithm derivation and validation writeup: `docs/CAESAR_EXEPACK_AND_STRINGS_FINDINGS.md`. This supersedes the vague "B0/B2-style command structure" description in the main RE corpus with an exact, disassembly-derived spec.
- Powers the `bindiff_exe` tool below.

---

## Tools built on these decoders

All in `tools/`, all built and smoke-tested against real files:

- `dump_vpx <in.vpx> <out.png> [palette]` — decode + render to PNG.
- `dump_pl8 <in.pl8> <out.png> [palette]` — decode all frames to a contact-sheet PNG.
- `empire_view <in> --ascii|--png|--summary` — render or inspect an EMPIRE2 scenario.
- `save_inspect <in.sav>` — print the block table and decode the global-words section.
- `bindiff_exe <a.exe> <b.exe> [--strings]` — decompress and diff two EXEPACK'd executables (byte-level runs + embedded-string set differences). Already run against both known `CSR.EXE` builds — see `docs/CAESAR_EXEPACK_AND_STRINGS_FINDINGS.md` for what it found.

## What's next (see `GAIUS_ROADMAP.md`)

- `FONT1.PL8` is mapped (the engine's `DS:0F64` table; `ui/game_font.hpp`) and the toolbar draws its label with it. Still to do: `FONT2.PL8`, `ROMFONT.PL8` (its table, `DS:1044`, is known — `docs/CAESAR_CITY_RENDERER_FINDINGS.md` section 7), and `MINIFONT.PL1`, a separate format.
- Tile → sprite mapping: **done** for the city view — `render::render_city`, traced in `docs/CAESAR_CITY_RENDERER_FINDINGS.md` and pixel-exact against the captures. Still open there: animation timing, walkers, the overlay map modes and the province view.
- Match the palette for `PANEL1A`-`PANEL1D.VPX` (the panel variants), which `PANEL1.256` does not cover.
- Phase 1: SDL2 viewer built on top of these decoders, plus the platform/input skeleton per `GAIUS_MASTERPLAN.md` section 5a.
