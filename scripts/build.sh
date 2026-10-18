#!/bin/sh
# Build MenuetBoy and regenerate the VM's CD image.
#
# Usage: scripts/build.sh [rom] [rom_dir]
#   rom:     ROM loaded by default as /MENUETBOY/ROM.GB (default: roms/dmg-acid2.gb)
#   rom_dir: also put its *.gb in /MENUETBOY/ with 8.3 names (GAME.GB)
#
# Requires: brew install blink xorriso
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TOOLS=$ROOT/.tools
BUILD=$ROOT/build
VM=$ROOT/DevContainer/MenuetOS.utm
BASE_ISO=$VM/M64CD16100.ISO
OUT_ISO=$VM/MenuetBoy.ISO
ROM=${1:-$ROOT/roms/dmg-acid2.gb}
ROM_DIR=${2:-}

FASM_VERSION=1.73.35
FASM=$TOOLS/fasm/fasm.x64

# El Torito boot floppy of the official ISO (hidden, not in the file tree)
BOOT_LBA=25
BOOT_BLOCKS=720

for cmd in blink xorriso; do
    command -v $cmd >/dev/null || { echo "missing: $cmd (brew install $cmd)" >&2; exit 1; }
done

if [ ! -x "$FASM" ]; then
    mkdir -p "$TOOLS"
    curl -sSLf "https://flatassembler.net/fasm-$FASM_VERSION.tgz" | tar xz -C "$TOOLS"
fi

mkdir -p "$BUILD" "$TOOLS/blinkroot"

# blink sees host files under /SystemRoot; -m is required for fasm.x64
(cd "$ROOT/src" && BLINK_PREFIX="$TOOLS/blinkroot" blink -m "/SystemRoot$FASM" \
    menuetboy.asm "/SystemRoot$BUILD/MENUETBOY")

dd if="$BASE_ISO" of="$BUILD/boot.img" bs=2048 skip=$BOOT_LBA count=$BOOT_BLOCKS 2>/dev/null

# Enable sound at boot: config.mnt on the boot floppy selects the Intel HDA
# driver, disabled. Patch it in place (same length) to AC97 (the UTM device).
python3 - "$BUILD/boot.img" <<'PATCH'
import sys
path = sys.argv[1]
img = open(path, 'rb').read()
for old, new in [(b"audio_driver            = '\\SYSDIR\\DRIVER/INTELHDA'",
                  b"audio_driver            = '\\SYSDIR\\DRIVER/AC97'    "),
                 (b'audio_card_enable       = 0x00000000',
                  b'audio_card_enable       = 0x00000001')]:
    assert img.count(old) == 1 and len(old) == len(new), old
    img = img.replace(old, new)
open(path, 'wb').write(img)
PATCH

set -- -map "$BUILD/MENUETBOY" /MENUETBOY/MENUETBOY -map "$ROM" /MENUETBOY/ROM.GB
for f in "$ROOT"/roms/*; do
    [ -f "$f" ] && set -- "$@" -map "$f" "/MENUETBOY/$(basename "$f" | tr '[:lower:]' '[:upper:]')"
done
if [ -n "$ROM_DIR" ]; then
    for f in "$ROM_DIR"/*.gb; do
        # "Some Game (Japan).gb" -> SOMEGAME.GB
        name=$(basename "$f" .gb | sed 's/ (.*//' | tr -cd 'A-Za-z0-9' | tr '[:lower:]' '[:upper:]' | cut -c1-8)
        set -- "$@" -map "$f" "/MENUETBOY/$name.GB"
        echo "rom: /MENUETBOY/$name.GB <- $(basename "$f")"
    done
fi

# Write to a new file and rename, so a running VM keeps reading the old image
rm -f "$OUT_ISO.new"
xorriso -indev "$BASE_ISO" -outdev "$OUT_ISO.new" \
    -rockridge off -joliet off \
    -compliance iso_9660_level=2:lowercase:7bit_ascii:no_force_dots:omit_version -volid M64A \
    -boot_image any discard \
    -map "$BUILD/boot.img" /BOOT.IMG "$@" \
    -boot_image any bin_path=/BOOT.IMG -boot_image any emul_type=diskette \
    -boot_image any cat_path=/BOOT.CAT -commit 2>/dev/null
mv -f "$OUT_ISO.new" "$OUT_ISO"

echo "built: $BUILD/MENUETBOY -> $OUT_ISO"
