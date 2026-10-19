#!/bin/sh
# Build MenuetBoy and regenerate the VM's CD image.
#
# Usage: scripts/build.sh [rom]
#   rom: ROM loaded by default as /MENUETBOY/ROM.GB (default: roms/dmg-acid2.gb)
# Every file in roms/ is also put in /MENUETBOY/ under its own name.
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

# config.mnt on the boot floppy, patched in place (same length):
# - sound with the AC97 driver (the UTM device; the default is Intel HDA, off)
# - the CD on the primary IDE master, so boot option 3 (no
#   auto-configuration) also sees it
# - keyboard layout 2 = eng (0, the default, types - for /)
python3 - "$BUILD/boot.img" <<'PATCH'
import sys
path = sys.argv[1]
img = open(path, 'rb').read()
for old, new in [(b"audio_driver            = '\\SYSDIR\\DRIVER/INTELHDA'",
                  b"audio_driver            = '\\SYSDIR\\DRIVER/AC97'    "),
                 (b'audio_card_enable       = 0x00000000',
                  b'audio_card_enable       = 0x00000001'),
                 (b'cd_1_enable             = 0x000000',
                  b'cd_1_enable             = 0x000001'),
                 (b'cd_1_base               = 0x000000',
                  b'cd_1_base               = 0x0001f0'),
                 (b'keyboard_layout         = 00000000',
                  b'keyboard_layout         = 00000002')]:
    assert img.count(old) == 1 and len(old) == len(new), old
    img = img.replace(old, new)
open(path, 'wb').write(img)
PATCH

set -- -map "$BUILD/MENUETBOY" /MENUETBOY/MENUETBOY -map "$ROM" /MENUETBOY/ROM.GB
for f in "$ROOT"/roms/*; do
    [ -f "$f" ] && set -- "$@" -map "$f" "/MENUETBOY/$(basename "$f")"
done

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
