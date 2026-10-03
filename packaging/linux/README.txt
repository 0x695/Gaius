Gaius -- an open-source engine for Caesar (1992)
================================================

Gaius plays Caesar (Impressions Games, 1992/93) with your own copy of the game.
This package contains no game files.

To play
  1. Have the files of your own copy of Caesar: the folder that holds CSR.EXE,
     HOUSES.PL8 and EMPIRE2.001 (the US release; GOG keeps it in a folder called US).
  2. Run ./gaius. It looks in the usual places (GOG Games, Steam, ~/Games, ...) and
     asks where the game is if it finds none. It remembers the answer. You can also
     drop the folder on the window.
  3. Escape opens Settings: game speed and pace, sound, window, keys, language.

It needs SDL 2 (libsdl2-2.0-0 on Debian and Ubuntu; the Steam Deck has it).
The Steam Deck: see STEAM_DECK.md.

Where things are kept
  Settings:  ~/.config/gaius          Saves and game:  ~/.local/share/gaius
  Gaius never writes into your game folder, so the original's saves are safe.
  Its saves are the original's .SAV files, so the DOS game can read them.

Problems, ideas, saves to share
  https://github.com/0x695/Gaius

Gaius is free software (GPL-3.0-or-later, see LICENSE; the licences of the
components it contains are in THIRD_PARTY_NOTICES.txt). It is not affiliated with
or endorsed by the makers or owners of Caesar.
