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
- **Known gap:** no correct in-game sprite palette has been identified yet for city-content sheets like `HOUSES.PL8` (the `.P32`/`.256` files found so far are all UI/title-screen palettes). `dump_pl8` without a palette argument produces a mostly-black grayscale visualization because raw index values are low numbers — this is expected, not a bug, and is tracked as a Phase 1/8 follow-up (find or derive the real sprite palette).

## `EMPIRE2.0xx` — strategic map (`formats/empire2/`)

- Exactly 1602 bytes: 2-byte prefix (semantics unresolved — never treat as width/height) + 1600-byte 40×40 cell array.
- **Status: DEFINITIVE, tested against the full corpus.** All 50 supplied `EMPIRE2.000`-`EMPIRE2.049` files round-trip load→save byte-identical.

## `CAESARxx.SAV` — save file (`formats/save/`)

- Exactly 57126 (0xDF26) bytes, split into 20 confirmed contiguous blocks (global words, 70×50 object table, four 10000-byte city simulation layers, embedded EMPIRE2 map, several smaller unidentified tables, final state).
- **Status: READ-ONLY, block table verified contiguous and size-correct against the original Python reference (`caesar_save_layout.py`).** Not yet validated against a real `.SAV` file — none has been captured/supplied yet (the corpus test for this skips cleanly rather than failing). This remains a real gap: per `GAIUS_ROADMAP.md` Phase 2/8, the save *loader* (as opposed to the already-reconstructed serializer) hasn't been reverse engineered, so this block table is currently trusted but not proven against a live save.

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
- Phase 1: SDL2 viewer built on top of these decoders, plus the platform/input skeleton per `GAIUS_MASTERPLAN.md` section 5a.
