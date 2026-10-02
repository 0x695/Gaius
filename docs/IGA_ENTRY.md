# Gaius in the IGA

Phase 10's last item: listing Gaius on the Impressions Games Archive (IGA, the
sibling project in `E:\dev\IGA`) as the engine reimplementation of Caesar (1992).
This is the record, and what in the IGA it made untrue. **Applied on 2026-10-02**
in the IGA (`src/data/engine-projects.json`, `games.json`, `file-formats.json`,
a devlog entry, and its `CLAUDE.md`, which git ignores there), following how
Caesar II left the wanted board. What is still open is the format pages, below.

## The engine project

An entry for `src/data/engine-projects.json`, in the shape of `src/content.config.ts`'s
`engineProjects` (the `lastCommit` and `activityCheckedAt` fields are the ones
`scripts/update-engine-activity.mjs` fills in, which is the thing to run to
refresh them):

```json
{
  "id": "gaius",
  "name": "Gaius",
  "games": ["caesar1"],
  "author": "0x695",
  "authorUrl": "https://github.com/0x695",
  "lineage": "Independent — clean-room reimplementation",
  "language": "C++17 (SDL2)",
  "status": "active",
  "saveCompat": "Reads and writes the original's .SAV files — all 17 real saves round-trip byte-identical",
  "platforms": "Windows, Linux, Steam Deck, Android",
  "activity": "Begun Sep 2026 · the whole game plays, simulation checked against real saves · actively developed",
  "repoUrl": "https://github.com/0x695/Gaius",
  "docsUrl": "https://github.com/0x695/Gaius/tree/master/docs",
  "websiteUrl": null,
  "playUrl": null,
  "lastCommit": { "sha": "33cd7ce", "date": "2026-10-02T09:59:50Z", "branch": "master" },
  "activityCheckedAt": "2026-10-02"
}
```

Every claim is something the repository shows: the repository is public (GPL-3.0);
the round trip is checked by the save corpus tests (`GAIUS_TEST_ASSETS/gaius_test_saves`);
the platforms are the 1.0 scope (`GAIUS_ROADMAP.md`). It needs the player's own
copy of the game: Gaius ships none of the original's files.

## What it made untrue

`src/data/games.json`, the `caesar1` entry, said in `wanted`:

> No open-source engine or format documentation exists for Caesar — completely
> virgin reverse-engineering territory.

That is no longer so, and `wanted` is now null (the schema keeps it for games
with no engine). The IGA's own rule ("mark gaps honestly") applies both ways, so
the open items went into the devlog entry instead, from `GAIUS_ROADMAP.md`:

- the province view's picture, not yet checked against a capture of the real screen;
- the four `PANEL1A`-`D` panel pictures' palette (`scripts/export_assets.py` lists
  it as unresolved);
- a walker's path and the random draws' timing, which no save can pin;
- nothing run yet on a physical Android phone or a Steam Deck.

`engineNote` and `timelineNote` ("its own reverse-engineering problem") stay true.
The IGA's `CLAUDE.md` named Caesar I among the games with no engine, so that
sentence changed too, and the "assets" format row moved from undocumented to
partial.

## Formats

The IGA gives a format a page "if and only if it has a written spec"
(`CLAUDE.md`). `docs/FORMATS.md` is a written spec of the Caesar I files, with a
status for each, and the findings documents give the rest:

| File | Where it is specified |
|---|---|
| `.VPX` pictures, `.PL8`/`.PL1` sprite sheets, `.256`/`.P32` palettes | `docs/FORMATS.md` |
| `.SAV` saves (the save writer's address map) and `EMPIRE2.0NN` scenarios | `docs/FORMATS.md`, findings sections 22 and 33 |
| `.VAS` animations, `CONTFRM.GD8` click maps, `EDATA.CSR` | findings section 34 |
| `.VOC` effects, `.XMI`/`.XM2` music | `formats/voc/voc.hpp`, findings sections 34 and 45 |
| `CSR.EXE`'s EXEPACK compression | `docs/CAESAR_EXEPACK_AND_STRINGS_FINDINGS.md` |

**Still open:** which of those become format pages, and under which ids. The
Caesar assets row links to `docs/FORMATS.md` and the findings, with `spec: null`
(no page), as Caesar II's row did before its specs were written. Each page has to
carry its "what this can't tell you" callout, and the status labels (DEFINITIVE,
HIGH CONFIDENCE, STRONG INFERENCE, UNRESOLVED) already in these documents are the
material for it.
