# Gaius in the IGA

Phase 10's last item: listing Gaius on the Impressions Games Archive (IGA, the
sibling project in `E:\dev\IGA`) as the engine reimplementation of Caesar (1992).
This is the record to add, and what in the IGA it makes untrue. Nothing here has
been applied to the IGA's repository yet: it's that project's content, and its
`CLAUDE.md` has its own rules for it.

## The engine project

An entry for `src/data/engine-projects.json`, in the shape of `src/content.config.ts`'s
`engineProjects` (the `lastCommit` and `activityCheckedAt` fields are the ones
`scripts/update-engine-activity.mjs` fills in, so run it rather than trusting
these values):

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
  "lastCommit": { "sha": "d3afac1", "date": "2026-09-17T04:12:54Z", "branch": "master" },
  "activityCheckedAt": "2026-10-02"
}
```

Every claim is something the repository shows: the repository is public (GPL-3.0);
the round trip is checked by the save corpus tests (`GAIUS_TEST_ASSETS/gaius_test_saves`);
the platforms are the 1.0 scope (`GAIUS_ROADMAP.md`). It needs the player's own
copy of the game: Gaius ships none of the original's files.

## What it makes untrue

`src/data/games.json`, the `caesar1` entry, says in `wanted`:

> No open-source engine or format documentation exists for Caesar — completely
> virgin reverse-engineering territory.

That is no longer so. The IGA's own rule ("mark gaps honestly") applies both
ways: `wanted` should now say what is still open rather than claim nothing exists.
Candidates, from `GAIUS_ROADMAP.md`:

- the province view's picture, not yet checked against a capture of the real screen;
- the four `PANEL1A`-`D` panel pictures' palette (`scripts/export_assets.py` lists
  it as unresolved);
- a walker's path and the random draws' timing, which no save can pin;
- nothing run yet on a physical Android phone or a Steam Deck.

`engineNote` and `timelineNote` ("its own reverse-engineering problem") stay true.
`CLAUDE.md` of the IGA names Caesar I among the games with no engine
("No open-source engine exists for Emperor or Caesar I"), so that sentence changes too.

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

Which of those become format pages, and under which ids, is for the IGA's
`fileFormat` collection to decide; each page has to carry its "what this can't
tell you" callout, and the status labels (DEFINITIVE, HIGH CONFIDENCE, STRONG
INFERENCE, UNRESOLVED) already in these documents are the material for it.
