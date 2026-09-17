#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Starts Gaius from the folder it was unpacked into, so it finds its language
# files. Arguments pass through: a Caesar folder or save, --mute, and so on.
# With no argument Gaius looks for the game in its settings, in
# ~/.local/share/gaius/game, and beside itself, and otherwise explains where
# the files go.
here="$(cd "$(dirname "$0")" && pwd)"
exec "$here/gaius-bin" "$@"
