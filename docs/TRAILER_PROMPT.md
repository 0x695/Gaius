# Gaius teaser trailer: prompt pack

For a text-to-video model (Veo, Sora, Runway, Kling). Tune the shot count to the model's clip limit, which is usually 8-10 s per generation. Stitch the clips together, then add the title and end card in an editor, because video models garble text.

## Master prompt (single clip, ~15 s)

```
Cinematic teaser trailer, 16:9, 24fps, ancient Roman world with no anachronisms, painterly and warm.
Open on black. A gold wreath-framed "G" glints, then dissolves into a dawn sky over a half-built
Roman provincial city seen from a high isometric angle: terracotta roofs, white marble forum,
stone aqueduct, a crimson banner snapping in the wind. Slow crane move down into the streets:
citizens carrying amphorae, a fountain sputtering to life as water finally runs through the pipes,
children running toward it. Time-lapse: sun races across the sky, dirt roads become paved,
tiny houses merge into grand villas, a coliseum rises stone by stone. Hard cut to dusk: war
horns, a dark barbarian horde cresting a ridge, torches in fog. Roman cohorts form a shield wall,
red cloaks, bronze helmets, dust and sparks. Clash in slow motion, one frame of silence,
then the city's bells ring and the banner still stands. Final shot: pull back to the whole
province glowing at golden hour, laurel crown settling onto the horizon.
Style: oil-painting meets miniature diorama, tilt-shift depth of field, deep Roman crimson
(#8B1A1A), burnished gold (#D4A537), travertine cream, long warm shadows, subtle film grain.
Score: low war drums building under a solemn brass fanfare, one swelling choir note at the end.
```

## Shot list (if you generate per shot)

| # | Time | Shot | Notes |
|---|------|------|-------|
| 1 | 0-2 s | Black, gold laurel "G" glint, dissolve to dawn | Add the logo in post |
| 2 | 2-5 s | High isometric crane-down on the unfinished city | Establishes the genre: top-down city builder |
| 3 | 5-8 s | Water reaches the fountain; people gather | The payoff of the build-and-serve loop |
| 4 | 8-10 s | Time-lapse: dirt road to paved road, huts to villas, coliseum rising | Fast, rhythmic cuts on the drums |
| 5 | 10-12 s | Horns. Barbarians crest the ridge at dusk | Darker palette, torches, fog |
| 6 | 12-14 s | Cohort shield wall, slow-motion clash, then silence | One beat of quiet before the end |
| 7 | 14-16 s | Pull back over the whole province at golden hour | Hero shot |
| 8 | 16-18 s | End card (add in post) | See below |

## End card (add in an editor)

```
GAIUS
An open-source reimagining of the 1992 classic Caesar
Windows · Linux · Steam Deck · Android
github.com/0x695/Gaius
```

Optional on-screen lines between shots, each over black for 1 s: **"Build."**, **"Provide."**, **"Defend."**, **"Rise to Caesar."**

## Negative prompt

```
text, subtitles, watermark, logos, modern buildings, modern clothing, cartoon style, anime,
pixel art, low-poly, UI overlays, gladiator-movie cliches, blood or gore, extra fingers,
warped faces, flickering textures
```

## Tips

- **Match the game.** The game's own title screen (gold FONT1 letters fading in over black) and the gold-on-crimson icon already set the palette, so use that for shot 1 and the end card.
- **Real footage.** If you would rather show the actual game, cut in a screen capture of the city view and the build preview (`gaius_viewer --no-intro`, Windows capture at 1x with the game's files). A good mix is 3 s of generated opening, 8 s of real gameplay and 4 s of generated battle.
- **IP.** Do not prompt for "Caesar (1992) box art" or paste the original's tagline. Gaius is clean-room, so keep the trailer's visuals and copy its own.
