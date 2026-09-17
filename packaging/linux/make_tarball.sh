#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Packs a Linux build into <name>.tar.gz: the game, its language files, the
# launcher, the desktop entry and the Steam Deck notes. No game files: players
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
cp -r "$root/lang" "$stage/lang"
cp "$here/gaius.sh" "$stage/gaius"
cp "$here/gaius.desktop" "$stage/"
cp "$here/STEAM_DECK.md" "$stage/"
cp "$root/LICENSE" "$stage/"
cp -r "$root/third_party/ymfm/LICENSE" "$stage/LICENSE.ymfm"
chmod +x "$stage/gaius" "$stage/gaius-bin"
tar -C "$(dirname "$stage")" -czf "$name.tar.gz" "$name"
echo "$name.tar.gz"
