#!/bin/sh
# Build MenuetBoy and regenerate the VM's CD image.
#
# Usage: scripts/build.sh [rom]
#   rom: ROM loaded by default as /MENUETBOY/ROM.GB (default: roms/dmg-acid2.gb)
# Every file in roms/ is also put in /MENUETBOY/ under its own name.
#
# Requires: brew install blink xorriso mtools
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

for tool in blink:blink xorriso:xorriso mcopy:mtools; do
    command -v ${tool%:*} >/dev/null || { echo "missing: ${tool%:*} (brew install ${tool#*:})" >&2; exit 1; }
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

# Desktop icon and GAMES menu entry: the icon is drawn into an unused cell
# of the system icon sheet i.png, and a line is added to icon.mnt and to
# both menus (menul.mnt is the large start menu, menu.mnt the small one).
mcopy -n -o -i "$BUILD/boot.img" ::i.png "$BUILD/i.png"
python3 "$ROOT/scripts/make_icon.py" "$BUILD/i.png" "$BUILD/i_mboy.png"
mcopy -n -o -i "$BUILD/boot.img" "$BUILD/i_mboy.png" ::i.png
for f in icon.mnt menu.mnt menul.mnt; do
    mcopy -n -o -i "$BUILD/boot.img" ::$f "$BUILD/$f"
done
python3 - "$BUILD" <<'LAUNCHER'
import sys
build = sys.argv[1]
start = 'start /cd/1/menuetboy/menuetboy'
icon = '/fd/1/i.png:06'

def patch(name, old, new):
    path = f'{build}/{name}'
    text = open(path, 'rb').read()
    assert text.count(old) == 1, (name, old)
    open(path, 'wb').write(text.replace(old, new))

# icon.mnt has fixed columns: position+12, name (up to 8 characters show),
# image:cell, command, each ending in '-'. The desktop places icons only in
# the four 4x4 corners (columns and rows A-D, G-J); BD is free, below SETUP.
# The file keeps its 40 lines by taking the place of TRANSP, which the
# desktop does not show.
def icon_line(pos, name, image, command):
    return (f'{pos}12 {name}'.ljust(18) + '- ' + image.ljust(20) + '- '
            + command.ljust(35) + '-').encode()

patch('icon.mnt', icon_line('GA', 'TRANSP', '/fd/1/i.png:91', '\\sysdir\\transp'),
      icon_line('BD', 'M-BOY', icon, start))
patch('menu.mnt', b'[SUBMENU2]\r\n', b'[SUBMENU2]\r\n'
      + f'MENUETBOY  , APP,  {icon}, \\sysdir\\{start}\r\n'.encode())
patch('menul.mnt', b'#GAMES\r\n', b'#GAMES\r\n'
      + f'MENUETBOY  "MENUETBOY"    , APP,  {icon},/cd/1/sm/game10.bmp,    \\sysdir\\{start}\r\n'.encode())
LAUNCHER
for f in icon.mnt menu.mnt menul.mnt; do
    mcopy -n -o -i "$BUILD/boot.img" "$BUILD/$f" ::$f
done

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
