# Caesar (1992) — GOG package findings addendum

> Source analyzed: `Caesar__2_.zip`, a GOG.com digital distribution of Caesar (Deluxe), containing a GOG/DOSBox launch tree at the archive root plus a preserved original US floppy release under `Caesar/US/`.
>
> This addendum is written in the same confidence-labeled style as `CAESAR_REVERSE_ENGINEERING_COMPLETE.md` and is meant to be folded into that corpus. It does not repeat anything already established there.

---

## 1. Continuity check — DEFINITIVE

`Caesar/US/CSR.EXE` in this package is **byte-identical** (MD5 `114719ea630ba60d5e0640a9a50be5aa`) to the `CSR.EXE` already fully analyzed from the previously supplied `caesar.zip`. Likewise `Caesar/US/EDATA.CSR` is byte-identical to the previously analyzed copy. **Every address, offset, and structural finding already documented applies unchanged to this build.** Nothing in the existing corpus needs revisiting because of this upload.

## 2. A second, distinct CSR.EXE build now exists — DEFINITIVE (new artifact), HIGH CONFIDENCE (interpretation)

The archive root (`Caesar/CSR.EXE`, as opposed to `Caesar/US/CSR.EXE`) is a **different executable**:

| | `Caesar/CSR.EXE` (root) | `Caesar/US/CSR.EXE` (already RE'd) |
|---|---|---|
| Size | 201,709 bytes | 253,509 bytes |
| MD5 | `fd2cb0dee84e5dded9309f886aeab26d` | `114719ea630ba60d5e0640a9a50be5aa` |
| Compiler | Borland C++ (1991), still EXEPACK-packed (`!Packed file is corrupt` stub present in both) | same |
| Embedded UI strings | English **+ German** (`Caesar - Optionsbildschirm`) **+ French** (`Caesar - Ecran option`) | English only |

Interpretation: the root build is an **international/multi-language build**, the `US/` build is the **US-region English-only build** already reverse engineered. Despite carrying three languages' worth of strings, the international build is *smaller* — worth confirming once decompressed (see open item below) whether this is a leaner asset set, different compression, or an actual code-level revision (potential bugfix build).

**New RE opportunity:** bindiff the two decompressed images against each other. This is a from-scratch, unblocked, high-value technique that wasn't available until now — differences between the two builds could confirm/refute provisional findings (anything that differs between two builds of "the same" logic is either localization-only or a genuine version difference, either of which is informative) far faster than continuing to read one disassembly in isolation.

## 3. `COHORT.CSR` located — resolved, but not what was expected — HIGH CONFIDENCE

`COHORT.CSR` (previously flagged as missing) is present in the international build's directory. It is **not** Cohort-2 game data. Its entire content is 14 bytes:

```text
"CaesarXV.sav\0 "
```

Cross-referenced against embedded strings in both executables:

- International build (`Caesar/CSR.EXE`) contains the literal string `CaesarXV.sav`.
- US build (`Caesar/US/CSR.EXE`) contains the literal string `CaesarXX.sav`.

**Interpretation:** `COHORT.CSR` is a small **runtime handoff file** CSR.EXE writes before exiting to invoke the external Cohort 2 program (or its own internal fallback — see section 4), naming the save file the handoff should resume from. The Roman-numeral-styled placeholder filename (`XV`/`XX`) is baked into the executable as a template/default. It is not shipped content and is not something Gaius needs to reverse engineer as a data format — it only needs to reproduce the *behavior* (persist enough state before a battle screen transition to resume correctly after).

This resolves one of the two originally-missing files, but reclassifies it: it was never a content-format gap, it's a process-IPC artifact.

## 4. The actual battle-resolution entry point — NEW, HIGH CONFIDENCE, actionable

`CAESAR.BAT` (both builds) reveals the real launch architecture, and it's a **process loop**, not a single continuously-running program:

```bat
@echo off
csr.exe
if errorlevel 2 goto end
:loop
cohort.exe [caesar]        <- US build passes "caesar" as an argument, root build does not
if errorlevel 2 goto end
csr.exe cohort
if errorlevel 2 goto end
goto loop
:end
```

This means:

1. `CSR.EXE` exits (with a specific errorlevel) when a battle needs to be resolved, having written the `COHORT.CSR` handoff file (section 3).
2. The batch loop attempts to run `cohort.exe` — the external commercial Cohort 2 product. **`COHORT.EXE` is not present in either build and is confirmed still absent from every archive supplied so far** (along with `LOADER.EXE`, `HIRES!.COM`, and both `.MAP` files — see section 6).
3. Regardless of whether `cohort.exe` succeeded, is present, or fails outright under DOS (a missing external program does not necessarily set the errorlevel this script checks for), the batch loop proceeds to run **`csr.exe cohort`** — i.e., **CSR.EXE accepts a `cohort` command-line argument**, which is almost certainly the entry point for the game's own internal battle resolution (the "Battle Screen" the manual documents for players who don't own Cohort 2: Tortoise/Assault/Flank/Charge tactics, resolved without the external program).

**This is a concrete, findable target that de-risks the previously "net-new, no RE corpus at all" battle-resolution blocker** (see `GAIUS_ROADMAP.md` Phase 6). The next RE step is not "reverse engineer the entire battle system from nothing" — it's specifically: locate `CSR.EXE`'s command-line argument parsing, find the `cohort` branch, and trace forward from there. That branch is what actually implements the manual's four-tactic battle resolution.

**Gaius implication:** the finished engine should not shell out to any external process at all — this whole batch-loop/errorlevel dance is a DOS-era limitation Gaius doesn't need to reproduce. But understanding it tells us exactly which code path in the disassembly to chase for the real battle math, and confirms the manual's internal battle screen and the Cohort-2 handoff are two branches of the same decision point, not two unrelated systems.

## 5. Format cross-validation and one resolved false alarm — HIGH CONFIDENCE

- **`EDATA.CSR`** is byte-identical (MD5 match) between both builds — confirms the previously recovered 320-byte structure is stable across localization and is very unlikely to contain any localized/build-specific content.
- **`CONTFRM.GD8`** *differs* between builds (different MD5, same 1000-byte size in both). This is new information: whatever this format is, its content is build/region-dependent while its size is fixed — consistent with a fixed-layout table of localizable strings or UI coordinates. Still an unresolved format, but this narrows the hypothesis space usefully.
- **`MINIFONT.PL1` vs `MINIFONT.PL8`** — initially looked like a second undocumented sprite format. It is not. Byte-level header comparison shows an **identical PL8 frame-descriptor structure** (same `frame_count`/`table_size` header shape, same per-frame `pixel_offset/width/height/x/y` layout) in both files — the international build's minifont is simply smaller (fewer/smaller glyphs, 1012 vs 4036 bytes) and was given a `.PL1` extension instead of `.PL8` for reasons that aren't clear (possibly a packaging typo, possibly a legacy convention from an earlier build stage). **Action for the format importer: sniff PL8 structure rather than trust the file extension, at least for `MINIFONT.*`.**
- **`FONT1.PL8`** differs slightly in size between builds (7188 vs 7300 bytes) with the same header shape — expected, since an international build needs accented characters (German/French) the US font doesn't.

## 6. Still confirmed missing — no change

Despite this being a more complete distribution than the original supplied archive, the following remain **absent from both builds** in this package:

- `COHORT.EXE` (the actual external Cohort 2 program — likely genuinely never bundled, since Cohort 2 was sold separately per the manual)
- `LOADER.EXE`
- `HIRES!.COM`
- Both `.MAP` files referenced by the earlier file-listing research

None of these block Gaius: `COHORT.EXE` is now understood to be optional/replaceable (section 4), and `LOADER.EXE`/`HIRES!.COM`/`.MAP` were already low-priority (likely install-time or alternate-video-mode utilities, not core simulation data) per the original assessment in `CAESAR_CITY_MAP_RESEARCH.md`.

## 7. Audio format bonus: `.MDI` files are standard MIDI — DEFINITIVE, unblocks work with zero RE needed

`Caesar/CZARJIN1.MDI` … `Caesar/CZARTIT.MDI` (international build only) begin with the standard `MThd`/`MTrk` Standard MIDI File (SMF) signature — **these are ordinary General MIDI files**, not a proprietary Impressions format. They correspond one-to-one with the US build's `.XMI`/`.XM2` cues of the same base name (same music, different container):

| Cue | `.MDI` (intl) | `.XMI` (US) | `.XM2` (US) |
|---|---:|---:|---:|
| CZARJIN1 | 1301 B | 1246 B | 206 B |
| CZARJIN2 | 2410 B | 752 B | 174 B |
| CZARTIT | 15045 B | 5504 B | 720 B |

**Interpretation:** this GOG build replaced the original Miles Sound System/AIL-driven `.XMI` playback path with plain Standard MIDI files, played back through DOSBox's built-in `[midi]` MPU-401 emulation (`dosbox_caesar1.conf` confirms `mididevice=default`). This is a well-known GOG re-release pattern for old AIL-based DOS games. **Consequence for Gaius: prefer `.MDI` as the music source when present — it requires no format-specific decoding at all, just any off-the-shelf SMF playback library** — and treat `.XMI`/`.XM2` as the legacy/authentic path only needed if we want bit-for-bit fidelity to the original 1993 release's exact instrument patches. Consistent with this, the international build also drops the original soundcard driver files (`ADLIB.ADV`, `SBFM.ADV`, `SBDIG.ADV`, `PCSPKR.ADV`, `IBMSND.COM`, `IBMBAK.COM`, `SAMPLE.AD`) entirely — they're not needed once music no longer routes through the AIL driver stack.

## 8. Unexplained / flag for verification — do not treat as resolved

The international build's directory is also missing most **digitized sound effects** (`.VOC` files — only 7 of the original ~25 are present: `DEMOL`, `FANFARE`, `FIRE`, `RESPONSE`, `SWORD`, `WAR_CRY`, `WATER`) and both win/lose cutscene animations (`LOSE0001.VAS`, `WINS0001.VAS`). Unlike the MIDI substitution (section 7), there's no equivalent explanation on hand for why these would be intentionally dropped — losing most combat/ambience sound effects and both endgame cutscenes would be a real gameplay regression, not a packaging optimization.

Two plausible explanations, unconfirmed:
1. This is genuinely how GOG ships the "Deluxe" default configuration (possible but would be a notable, citable oddity if true — worth an external check against GOG's own file listing/community reports rather than assuming).
2. This particular zip is an incomplete capture of the installed directory (partial copy/upload), and the full install does include them.

**Do not build any Gaius feature that assumes these files are unavailable.** The `US/` copy has the complete original set regardless, so this is a non-issue for engine development either way — flagged here purely so it isn't mistaken for a confirmed finding later.

## 9. Updated format/asset inventory delta

New palette (`.P32`) and `.256` samples obtained for cross-validation of the already-decoded palette formats: `TEMPLE.P32`, `TITLE3.P32`, `WAR.P32`, `IMP_LOGO.P32`, `FORUM32.P32`, plus the US-side `.256` set (`IMPRLOGO.256`, `NEWFORUM.256`, `PANEL1.256`, `ROME1.256`, `SHADE.256`, `TEMPLE.256`, `TITLE3.256`, `WAR2.256`). No new format work needed — these are simply more fixtures for the existing decoder's test corpus (see `GAIUS_MASTERPLAN.md` testing strategy).

New VPX-only-in-international-build resources (`FORUM32.VPX`, `IMP_LOGO.VPX`, `WAR.VPX`) appear to be renamed/re-encoded replacements for the US build's `NEWFORUM.VPX`, `IMPRLOGO.VPX`/`IMPRSEN.VPX`, and `WAR2.VPX`/`WARMESS.VPX` respectively (consistent file-size ballpark, same role inferred from filename/context). Not yet byte-level confirmed — flagged as a small follow-up, not a blocker.

`MUSIC.MOD` (international build only) is a ProTracker-family module (`ST-01:`-style sample-name convention, title "einsteinium-04"). This is **not original 1993 Caesar content** — it doesn't match any known original audio pipeline (VOC/XMI/AdLib) and is presumably GOG bonus/alternate content. No action needed; not in scope for faithful reimplementation, could optionally be offered as an alternate soundtrack option later, entirely outside the core project.

---

## Confidence summary

| Finding | Confidence |
|---|---|
| `US/CSR.EXE` identical to already-analyzed build | Definitive |
| Root `CSR.EXE` is a distinct, smaller, multi-language build | Definitive |
| `COHORT.CSR` = runtime save-handoff filename artifact, not content | High confidence |
| `csr.exe cohort` argument = internal battle-resolution entry point | High confidence (strong inference from batch-loop structure; not yet confirmed by disassembly) |
| `MINIFONT.PL1` is PL8 format under a different extension | Definitive (byte-level header match) |
| `.MDI` files are standard SMF, need no custom RE | Definitive |
| `CONTFRM.GD8` content is build/region-dependent | Definitive (MD5 differs), meaning still open |
| Missing VOC/VAS files in international build are an intentional design choice | Unresolved — flagged, not assumed |
| `COHORT.EXE`/`LOADER.EXE`/`HIRES!.COM`/`.MAP` files | Still missing, unchanged from prior assessment |
