#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Packs a Linux build into <name>.tar.gz: the game (the language files are built into it), the
# launcher, the desktop entry, the readme, the licences and the Steam Deck notes. No game files: players
# bring their own copy of Caesar.
#
#   packaging/linux/make_tarball.sh <build folder> <name>
set -eu
build="$1"
name="$2"
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
stage="$(mktemp -d)/$name"
mkdir -p "$stage"
cp "$build/gaius_viewer" "$stage/gaius-bin"
cp "$here/gaius.sh" "$stage/gaius"
cp "$here/gaius.desktop" "$stage/"
cp "$root/packaging/icon/gaius.png" "$stage/gaius.png"
cp "$here/STEAM_DECK.md" "$here/README.txt" "$stage/"
cp "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.txt" "$stage/"
chmod +x "$stage/gaius" "$stage/gaius-bin"
tar -C "$(dirname "$stage")" -czf "$name.tar.gz" "$name"
echo "$name.tar.gz"
