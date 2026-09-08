# Caesar (1992) — EXEPACK algorithm & cross-build string findings

> This addendum documents two things produced by building and running `bindiff_exe` (Gaius Phase 0): the fully reverse-engineered EXEPACK decompression algorithm (previously only vaguely described in the main RE corpus), and a rich set of new UI-text/game-data findings from diffing the two known `CSR.EXE` builds. Written in the same confidence-labeled style as the rest of the corpus; meant to be folded in.

---

## 1. EXEPACK decompression algorithm — DEFINITIVE (derived from disassembly, not inferred)

`CAESAR_REVERSE_ENGINEERING_COMPLETE.md` section 3.2 established that a decompressed image had been reconstructed, and vaguely described "B0/B2-style command structure... decoder works backwards through the packed stream" without giving exact semantics. That phrasing was ambiguous — 0xB0/0xB2 also happen to be common x86 opcodes (`MOV AL,imm8` / `MOV DL,imm8`), which is a red herring. The real algorithm was derived by extracting the actual embedded decompression stub from a live `CSR.EXE` and disassembling it (`ndisasm`), then validating the result byte-exact against the file.

### File layout

```text
[MZ header, e_cparhdr * 16 bytes]
[load-module image: raw prefix, then backward-RLE-compressed suffix]
[18-byte EXEPACK header]
[decompression stub code + embedded header copy + relocation table]
```

The 18-byte header is located by finding the unique `"RB"` signature (bytes `0x52 0x42`) and reading the 16 bytes immediately before it:

```c
struct ExepackHeader {
    uint16_t real_IP, real_CS, mem_start, exepack_size;
    uint16_t real_SP, real_SS, dest_len, skip_len;
    uint16_t signature;  // 0x4252
};
```

Confirmed invariant: `file_size - header_offset == exepack_size` exactly, in both known builds — the trailing block (header + stub code + stub's own relocations) is exactly `exepack_size` bytes and runs to EOF.

### Compression scheme

The load-module image is a raw, uncompressed **prefix** (copied verbatim) followed by a **backward-RLE-compressed suffix**. Per-record layout, in normal ascending memory order:

```text
[payload...][length_lo][length_hi][command]
```

The command byte is the *last* byte of a record (not the first), because records are processed back-to-front: decompression starts near the end of the compressed suffix and moves toward its start, since the decompressed output is always larger than its compressed source (this is what makes classic in-place backward decompression safe on real 1990s hardware — irrelevant to us since this implementation just builds an output vector, but it explains the byte order).

```c
uint8_t masked = command & 0xFE;
bool is_last_record = command & 1;

if (masked == 0xB0) {
    // FILL: one payload byte, repeated `length` times
} else if (masked == 0xB2) {
    // COPY: `length` raw payload bytes, copied verbatim
}
// if is_last_record: stop after this record (this is the ONLY termination
// condition — there is no separate end-of-stream sentinel)
```

Before the first record, the decompressor scans backward through the final `skip_len` paragraphs (16 bytes each; `skip_len` has only ever been observed as 1) skipping trailing `0xFF` filler bytes, landing on the true first command byte.

### Validation

Implemented in `gaius/formats/exepack/`. Validated two independent ways:

1. **Against the already-analyzed US build**: parses cleanly through 536 records to a proper terminal flag with zero errors; the three known embedded strings (`"Borland C++ - Copyright 1991 Borland Intl."`, `"CaesarXX.sav"`, `"Caesar - Online help"`) are found fully intact at sensible offsets; and — the strongest check — **the reconstructed image size (raw prefix + decompressed suffix) matches the header's declared `dest_len * 16` exactly: 497,568 bytes (0x797A0)**, the same figure already on record in the main RE corpus.
2. **Against the previously-unanalyzed international build** (see `CAESAR_GOG_BUILD_FINDINGS.md`): decompresses cleanly with a *different* record count (637), different raw-prefix size, and a different `dest_len` (500,400 bytes) — and again the reconstructed size matches that build's own header exactly. Two structurally different real-world inputs both round-tripping perfectly against their own declared sizes is strong evidence the algorithm is correct in general, not overfit to one file.

**Confidence: DEFINITIVE.** This should be considered the authoritative EXEPACK spec for this project going forward, superseding the vague description in the main corpus's section 3.2.

---

## 2. Cross-build string findings (via `bindiff_exe --strings`)

Diffing the two builds' decompressed images surfaced a large amount of genuine game-content text that wasn't previously extracted into the corpus. Byte-level diffing is much less useful here (45.9% of the common-length region differs, almost certainly dominated by code/data shifting from the differing raw-prefix sizes rather than meaningful logic changes — a naive linear offset diff isn't alignment-aware) but the **string-set diff is immediately actionable**.

### Full political rank ladder (US build)

```text
Plebian, Citizen, Equitus, Taberllarius, Decurian, Iuridicus, Procurator,
Magistrate, Logistas, Praefectus, Magister, Cubicularius, Legate, Quaestor,
Senator, Praetor, Consul, Proconsul, Princeps, Imperator, Caesar
```

21 ranks total. The manual only mentions the starting rank (Decurian) and the final rank (Caesar) — this is the complete promotion ladder, directly relevant to `GAIUS_ROADMAP.md` Phase 7 (promotion/politics system).

### Full province name list (US build)

A list of ~60 real Roman provinces embedded as a single string (Sicilia, Campania, Latium, Cisalpine Gaul, Corsica, Sardinia, Alpes Maritimae, Narbonensis, Hispania Inf./Sup., Baetica, Lusitania Inf./Sup., Tarraconensis, Aquitania Inf./Sup., Hispania Sup., Lugdunensis, Belgica, Gallia Sup./Inf., W./E. Britannia, Britannia Sup., Caledonia, Germania Inf./Sup., Pannonia, Dacia, Illyricum, Dalmatia, Macedonia, Achaea, Creta, Thracia, Asia, Pamphylia, Cappadocia, Assyria, Syria, Mesopotamia, Judea, Arabia, Aegyptus, Cyrenaica, Africa, Numidia, Mauretania, Caeariensis, Tingitania, Moesia — see raw tool output for the exact full string). This is the complete set of governable provinces referenced in "Map of the Empire."

### Other confirmed UI/game text (US build)

- Full credits block matching the manual exactly: Chris Denman, Erik Casey, Jon Baker, David Lester, Simon Bradbury, Chris Bamford.
- Cohort status words: `nothing, waiting, patrolling, attacking, retiring, demobilized` — the actual state-machine labels for a Cohort, directly useful for `systems::military`.
- Warning/advisory message text (tribute warnings, tax/spending warnings, forum advisor prompts) in full, previously only summarized.
- `Eagle, Rabbit, Snake, Fish, Horse, Pig, Wolf, Hero, Explorer, Protector` — ten totem/symbol names, likely battle-standard or barbarian-race identifiers (the manual mentions sixteen barbarian races; only ten names surfaced here — the remaining six may be encoded elsewhere or this list may serve a different purpose. Flagged as **STRONG INFERENCE, not definitive** — needs a consumer trace before assuming it's the barbarian race list).

### International build (German/French) confirms manual-documented systems independently

- Workshop goods: French `Verre, Etain, Poterie, Cuivre, Vin, Ivoire, Bl[é], Epices` = Glass, Tin, Pottery, Copper, Wine, Ivory, Wheat, Spices — exactly the eight goods the manual lists, now confirmed via an independent-language source rather than only the manual text.
- Event/UI strings: `Barbaren gesichtet` (Barbarians sighted), `Es gibt Unruhen in bestimmten Stadtteilen` (There is unrest in certain districts), `Rom hat Ihre... Festnahme angeordnet` (Rome has ordered your arrest — the execution-on-three-missed-tributes flow), `Click here for Cohort option` / `Cliquer en haut continue` (French).
- Military-unit abbreviation hints: `(   )   Auxiliaires` / `Hilfstruppen` (German), `(   )   Regul[iers]`/`Regul[äre]` — matches Regulars/Irregulars/Auxiliaries from the manual, in three languages now.

### Practical use

This is a genuinely useful primary source for Phase 5 (construction UI text) and Phase 7 (Forum/advisors/promotion), independent of any RE work still needed on the underlying simulation logic — it's ready-to-use game content, extracted directly from the executable rather than re-derived from the manual by hand.

---

## Confidence summary

| Finding | Confidence |
|---|---|
| EXEPACK file layout (header location, `exepack_size` invariant) | Definitive |
| EXEPACK record format and FILL/COPY/last-record semantics | Definitive (validated against 2 independent real builds) |
| Political rank ladder (21 ranks) | Definitive (extracted string, not inferred) |
| Province name list | Definitive (extracted string, not inferred) |
| Cohort status words | High confidence (plausible state-machine labels; consumer not yet traced) |
| Ten totem names = barbarian races | Strong inference, not definitive — do not treat as final |
| Workshop goods (cross-confirmed via French/German) | Definitive |
