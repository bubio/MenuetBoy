# MenuetBoy

A GAME BOY (DMG) emulator for [MenuetOS](https://www.menuetos.net/) 64-bit,
written in x86-64 assembly.

- Runs original GAME BOY games (not Color or Super GAME BOY)
- No boot ROM needed
- Sound, battery-backed saves, MBC1/2/3/5 cartridges (MBC3 clock included)
- Passes Blargg's cpu_instrs, instr_timing, mem_timing and dmg_sound (01-08,
  11) tests, the Mooneye tests in `tests/roms/`, and dmg-acid2

MenuetOS itself is not included; get it from the
[MenuetOS site](https://www.menuetos.net/download.htm). Tested with
MenuetOS 64 1.61.00 on QEMU.

## Running

Copy `MENUETBOY` and your ROM files to a drive MenuetOS can read, then start
it from the file browser or the command line:

```
start /hd/1/games/menuetboy game.gb
```

A ROM path that does not start with `/` is relative to the folder of
`MENUETBOY`. Without a ROM, MenuetBoy looks for `/CD/1/MENUETBOY/ROM.GB`.

While running:

- **FILE > Open ROM..** picks a ROM with the system file browser
- Dropping a ROM file from the file browser onto the window opens it
- **VIEW > x1 - x4** sets the screen scale; **Debug Info** shows timing and
  audio details

| GAME BOY | Key |
|---|---|
| D-pad | Arrow keys |
| A | X |
| B | Z |
| Start | Enter |
| Select | Space or Backspace |

## Saves

Cartridge RAM of games with a battery is saved to `/FD/1/<ROM name>.SAV`
(the first 8 characters of the file name) when the game stops writing to it
and when MenuetBoy closes. `/FD/1` is MenuetOS's in-memory copy of the boot
floppy, so saves last until MenuetOS restarts unless the floppy image is
written back.

The MBC3 clock runs only while the game runs.

## Desktop icon and menu entry

Add a line to `icon.mnt` on the boot floppy. Lines are fixed width (80
bytes with CRLF): keep the `-` separators in the same columns as the other
lines. The position (`BD` below) must be free and within a corner block of
the desktop (columns and rows A-D or G-J); the name shows up to 8
characters. `i.png:25` is the system's joystick icon.

```
BD12 M-BOY        - /fd/1/i.png:25      - start /hd/1/games/menuetboy        -
```

For the start menu, add a line after `#GAMES` in `menul.mnt`:

```
MENUETBOY  "MENUETBOY"    , APP,  /fd/1/i.png:25,/cd/1/sm/game10.bmp,    \sysdir\start /hd/1/games/menuetboy
```

Replace `/hd/1/games/menuetboy` with where you put `MENUETBOY`.

## Building

The build runs on macOS with fasm (run through blink) and makes a MenuetOS
CD image for a QEMU (UTM) VM. See `docs/dev/` (Japanese).

```sh
brew install blink xorriso mtools
scripts/build.sh            # build/MENUETBOY and the VM's CD image
scripts/test.sh             # core tests
scripts/package.sh          # build/MenuetBoy-<version>.zip (rebuilds as build.sh does)
```

## License

MIT, see [LICENSE](LICENSE).

Test ROMs in this repository: the Mooneye test suite (MIT,
`tests/roms/LICENSE-mooneye-test-suite`), dmg-acid2 by Matt Currie (MIT),
and Blargg's GAME BOY test ROMs.
