#!/bin/sh
# Make the release zip: the app, README and LICENSE.
#
# Usage: scripts/package.sh   (builds first)
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$ROOT/build

"$ROOT/scripts/build.sh" >/dev/null
VERSION=$(sed -n "s/^VERSION *equ *'\(.*\)'/\1/p" "$ROOT/src/menuetboy.asm")
NAME=MenuetBoy-$VERSION
OUT=$BUILD/$NAME.zip

rm -rf "$BUILD/$NAME" "$OUT"
mkdir "$BUILD/$NAME"
cp "$BUILD/MENUETBOY" "$ROOT/README.md" "$ROOT/LICENSE" "$BUILD/$NAME/"
(cd "$BUILD" && zip -q -r "$OUT" "$NAME")
rm -rf "$BUILD/$NAME"
echo "packaged: $OUT"
