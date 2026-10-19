"""Battery save test ROM for the MenuetOS frontend.

  save_counter.py make <out.gb>

MBC1+RAM+BATTERY, 8KB RAM. Each start increments the byte at A000 and
shows its low 2 bits as the screen shade (0 white ... 3 black), so the
shade changes on every start when the save is kept.
"""
import sys


def make(path):
    rom = bytearray(0x8000)
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])      # nop; jp 0150
    rom[0x134:0x13F] = b'SAVECOUNTER'
    rom[0x147] = 0x03                                        # MBC1+RAM+BATTERY
    rom[0x149] = 0x02                                        # 8KB RAM
    code = [0x3E, 0x0A, 0xEA, 0x00, 0x00,                    # ld a,0A; ld (0000),a  RAM on
            0xFA, 0x00, 0xA0, 0x3C, 0xEA, 0x00, 0xA0,        # ld a,(A000); inc a; ld (A000),a
            0xE6, 0x03, 0xE0, 0x47,                          # and 3; ldh (BGP),a
            0xAF, 0xEA, 0x00, 0x00,                          # xor a; ld (0000),a    RAM off
            0x18, 0xFE]                                      # jr $
    rom[0x150:0x150 + len(code)] = bytes(code)
    open(path, 'wb').write(rom)


if __name__ == '__main__':
    make(sys.argv[2])
