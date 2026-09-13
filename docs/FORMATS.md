# Gaius — FORMATS.md

Status of every Layer 1 (exact import) format decoder implemented so far. This is the Phase 0 deliverable named in `GAIUS_ROADMAP.md`. For the full reverse-engineering narrative behind each format, see `CAESAR_REVERSE_ENGINEERING_COMPLETE.md` in the RE corpus — this file only documents what's actually implemented and tested in `formats/`.

All formats below are implemented, unit-tested, and (where a real asset was available) validated against actual Caesar (1992) files. **No game assets ship in this repo** — every test that needs one reads from a directory named by the `GAIUS_TEST_ASSETS` environment variable and skips cleanly if it isn't set.

---

## `.P32` — palette (`formats/p32/`)

- 64 bytes, 32 packed 16-bit colors, little-endian.
- Bits 0-3 = R, bits 4-7 = unused, bits 8-11 = B, bits 12-15 = G. Each nibble expands to 8-bit via `n * 17`.
- **Status: DEFINITIVE, tested.** Unit-tested against synthetic fixtures with known expected RGB output, and validated end-to-end via the VPX golden-image test below (EMAP2.P32 produces the exact reference image).

## `.256` — palette (`formats/pal256/`)

- 768 bytes, 256 colors × 3 bytes, each channel a 6-bit VGA DAC value (0-63), expanded via `(v << 2) | (v >> 4)`.
- **Status: DEFINITIVE, tested.** Unit-tested against synthetic fixtures. Not yet exercised against a real `.256` file end-to-end (no VPX+.256 golden-image pairing has been assembled yet — only VPX+.P32 for EMAP2). Follow-up: do the same golden-image validation with one of the `.256`-paired VPX files (e.g. `TITLE3.VPX`/`TITLE3.256`).

## `.VPX` — graphics container (`formats/vpx/`)

- 4 blocks, each: 8-byte header (packed_size, decoded_size, fill_pair, field3) + RLE-compressed payload.
- Per-command RLE: top 2 bits select explicit-byte / fill-A / fill-B / literal-run, bottom 6 bits + 1 = count.
- Each block decodes to exactly 16000 bytes; the four streams interleave 1-byte-at-a-time into a 320×200 (64000-byte) indexed framebuffer — **not** four separate bit planes.
- **Status: DEFINITIVE, tested against a real golden image.** `EMAP2.VPX` decoded + `EMAP2.P32` applied reproduces `EMAP2_decoded.png` with **zero pixel mismatches out of 64,000**.

## `.PL8` — sprite sheet (`formats/pl8/`)

- Container header: `uint16 unknown_a`, `uint16 frame_count` (4 bytes total).
- Per-frame descriptor (8 bytes): `pixel_offset` (big-endian u16, i.e. reversed byte order vs. everything else in this format), `width` (u8), `height` (u8), `x` (u16 LE), `y` (u16 LE). Pixel data lives at absolute file offset `pixel_offset`, not inline.
- **Status: HIGH CONFIDENCE, tested against a real worked example.** Verified byte-for-byte against `HOUSES.PL8`'s documented first frame (offset 0x0194, 16×16, x=0, y=0) and cross-checked against `MINIFONT.PL8`.
- **Two things resolved here that weren't fully documented upstream:**
  1. **Which header word is the frame count.** The main RE corpus flagged "the first/header word preceding the descriptor array is still not fully understood." Empirically: it's the *second* u16 (byte offset 2), not the first — confirmed exactly against `HOUSES.PL8`'s documented 50-frame count. The first u16's meaning (42 for HOUSES.PL8, 61 for MINIFONT.PL8) is still open.
  2. **Trailing placeholder frames.** `HOUSES.PL8`'s last 6 of 50 descriptors (frames 44-49) all have `pixel_offset` sitting exactly at end-of-file with a nonzero declared width/height that would overrun the file if dereferenced. The decoder treats "pixel_offset == file size" as a valid empty frame (no pixel data) rather than an error; any other case where declared size overruns the file is still a hard error. Why these placeholders exist (spare slots? unused housing variants?) is not yet explained — flag back to the main RE corpus if a consumer of these specific indices ever turns up in the disassembly.
- **Known gap, now split into two distinct problems** (narrowed during Phase 5's toolbar work — the original single-line version of this note conflated them, which made the whole sprite pipeline look more blocked than it is):
  1. **City-content palette — still open.** No correct in-game sprite palette has been identified for city-content sheets like `HOUSES.PL8`/`SPRITE2.PL8`. The `.P32`/`.256` files shipped are all UI/title-screen palettes. `dump_pl8` without a palette argument produces a mostly-black grayscale visualization because raw index values are low numbers — expected, not a bug. Tracked as a Phase 1/8 follow-up.
  2. **Font sheets — a *pixel decode* problem, not a palette problem. NEW, and specifically narrowed so it isn't re-diagnosed from scratch.** `FONT1.PL8` and `FONT2.PL8` (100 frames of 8×8 each), `ROMFONT.PL8` (27 frames of 16×17) and `MINIFONT.PL1` all parse their *descriptors* correctly — frame count and per-frame width/height/origin all read as sensible, self-consistent values. The *pixels* decode to noise. The palette was ruled out as the cause directly: dumping `FONT1.PL8` with no palette shows only **10 distinct index values, all within 0..11**, with index 0 dominant (background) and index 11 the clear ink colour — exactly the shape a low-colour glyph sheet should have. Remapping those indices to a high-contrast ramp still produces no glyph shapes at any layout tried. So the indices are plausible and the palette is irrelevant; what's wrong is that PL8's pixel path handles `HOUSES.PL8` (the documented worked example it was validated against) but not the font sheets' encoding variant. One suggestive detail for whoever picks this up: the font descriptors give 8-wide frames whose `x` origins step by **16**, not 8, which may mean the stride/packing differs for these sheets.
- **Confirmed working, and worth knowing before assuming "no sprites render":** the UI/panel content decodes *correctly today*. `PANEL1.VPX` + `PANEL1.P32` (or `PANEL1.256` — the two agree on every index the file uses) renders the real in-game bottom control panel: a full-width textured bar occupying **rows 176..199** of the 320×200 screen, i.e. 24 rows tall, bottom-anchored. `P_BLOCKS.PL8` (40 frames of exactly 16×16) also decodes coherently with that palette, though its frames turn out to be panel *texture/pattern* blocks rather than per-building toolbar icons. `PANEL1A`-`PANEL1D.VPX` do *not* render correctly with `PANEL1.256` (they use high palette indices and come out garish), so they have their own palette that hasn't been matched yet. Those measurements are the source of `ui::kOriginalPanelH`/`kOriginalIconPx`.

## `EMPIRE2.0xx` — strategic map (`formats/empire2/`)

- Exactly 1602 bytes: 2-byte prefix (semantics unresolved — never treat as width/height) + 1600-byte 40×40 cell array.
- **Status: DEFINITIVE, tested against the full corpus.** All 50 supplied `EMPIRE2.000`-`EMPIRE2.049` files round-trip load→save byte-identical.

## `CAESARxx.SAV` — save file (`formats/save/`)

- Exactly 57126 (0xDF26) bytes, split into 20 confirmed contiguous blocks (global words, 70×50 object table, four 10000-byte city simulation layers, embedded EMPIRE2 map, several smaller unidentified tables, final state).
- **Status: READ-ONLY, block table VALIDATED AGAINST REAL SAVES (2026-09-12/13).** Four saves of one city from a real US-build play session (fixtures live in `GAIUS_TEST_ASSETS/gaius_test_saves/`, never in the repo) are each exactly 57126 bytes; `model::load` -> `model::serialize` reproduces all four byte-for-byte; every Phase 5 building footprint appears in their tile grids as a full rectangle of exactly its transcribed size, which also confirms the row-major grid layout; and one reset-plus-dispatch pass with `systems::service` reproduces each save's A2C4 coverage layer and C9D4 service bits cell-for-cell (10000/10000). The save *loader* still hasn't been disassembled, so fields this project doesn't model are carried as opaque bytes rather than interpreted -- but the block boundaries are no longer merely trusted.
- **`global_words_128` is NOT a descending DS range.** The save writer (flat `0x033F2`) makes 128 two-byte writes in an order with 15 discontinuities: it skips `DS:0x6CB8`, writes `DS:0x6C0E` twice (save+`0xC2` and save+`0xDC`), and steps upward in places. The old "descending from `DS:0x6CE2`" rule was wrong for 107 of the 128 words. The exact table is `formats::save::kGlobalWordDsAddress`. It is confirmed three independent ways: the treasury `DS:0x6CA2` (save+`0x3E`) falls 7182 -> 4692 -> 3210 -> 3144 across the session; the twice-saved word holds identical values in both copies; and two tick statistics, `DS:0x6BF2` and `DS:0x6BF0`, equal the tile grid's own building and road counts exactly in every save.
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

- Golden-image validation for `.256`-paired VPX files (currently only `.P32`-paired `EMAP2.VPX` has been validated this way).
- Locate/derive the correct in-game sprite palette for `HOUSES.PL8`/`SPRITE2.PL8`/etc.
- Decode the font sheets' PL8 pixel variant (`FONT1`/`FONT2`/`ROMFONT`/`MINIFONT`) — see the split gap note above. This currently blocks using the original's real typeface anywhere in the UI; `ui/font.hpp` ships a clearly-labelled placeholder in the meantime.
- Match the palette for `PANEL1A`-`PANEL1D.VPX` (the panel variants), which `PANEL1.256` does not cover.
- Phase 1: SDL2 viewer built on top of these decoders, plus the platform/input skeleton per `GAIUS_MASTERPLAN.md` section 5a.
