"""MBC3 RTC test ROM.

  rtc_test.py make <out.gb>

MBC3+TIMER without RAM (type 0F). Prints Passed/Failed on the serial port
after checking, in emulated time:
  1. 511 days 23:59:59 rolls over to day 0 00:00:00 with the day carry set
  2. Seconds written as 63 wrap to 0 without a minute carry
  3. The clock does not count while halted (DH bit 6)
"""
import sys


class Asm:
    def __init__(self, org):
        self.org, self.code, self.labels, self.fixups = org, bytearray(), {}, []

    def label(self, name):
        self.labels[name] = self.org + len(self.code)

    def db(self, *data):
        self.code += bytes(data)

    def jp(self, op, name):                  # op: C3 jp, CA jp z, C2 jp nz, CD call
        self.db(op, 0, 0)
        self.fixups.append((len(self.code) - 2, name))

    def ld_hl(self, name):
        self.db(0x21, 0, 0)
        self.fixups.append((len(self.code) - 2, name))

    def link(self):
        for pos, name in self.fixups:
            addr = self.labels[name]
            self.code[pos:pos + 2] = bytes([addr & 0xFF, addr >> 8])
        return self.code

    # Cartridge helpers
    def sel(self, reg):                      # ld a,reg; ld (4000),a
        self.db(0x3E, reg, 0xEA, 0x00, 0x40)

    def put(self, reg, value):               # select reg; ld a,value; ld (A000),a
        self.sel(reg)
        self.db(0x3E, value, 0xEA, 0x00, 0xA0)

    def get(self, reg):                      # latch; select reg; ld a,(A000)
        self.jp(0xCD, 'latch')
        self.sel(reg)
        self.db(0xFA, 0x00, 0xA0)

    def expect(self, reg, value):            # fail unless reg == value
        self.get(reg)
        self.db(0xFE, value)                 # cp value
        self.jp(0xC2, 'fail')


def make(path):
    a = Asm(0x150)
    a.db(0xF3, 0x31, 0xFE, 0xFF)             # di; ld sp,FFFE
    a.db(0x3E, 0x0A, 0xEA, 0x00, 0x00)       # RAM/RTC enable

    # 1. Rollover of day 511 23:59:59 (seconds last: it restarts the second)
    a.put(0x0C, 0x01)
    a.put(0x0B, 0xFF)
    a.put(0x0A, 23)
    a.put(0x09, 59)
    a.put(0x08, 59)
    a.label('wait1')
    a.get(0x08)
    a.db(0xFE, 59)
    a.jp(0xCA, 'wait1')
    for reg, value in [(0x08, 0), (0x09, 0), (0x0A, 0), (0x0B, 0), (0x0C, 0x80)]:
        a.expect(reg, value)

    # 2. Seconds 63 -> 0 without a minute carry
    a.put(0x09, 0)
    a.put(0x08, 63)
    a.label('wait2')
    a.get(0x08)
    a.db(0xFE, 63)
    a.jp(0xCA, 'wait2')
    a.expect(0x08, 0)
    a.expect(0x09, 0)

    # 3. Halted: about 1.7 s (4 x 65536 x 7 M-cycles) without a tick
    a.put(0x0C, 0x40)
    a.put(0x08, 5)
    a.db(0x16, 4)                            # ld d,4
    a.label('outer')
    a.db(0x01, 0, 0)                         # ld bc,0
    a.label('inner')
    a.db(0x0B, 0x78, 0xB1)                   # dec bc; ld a,b; or c
    a.jp(0xC2, 'inner')
    a.db(0x15)                               # dec d
    a.jp(0xC2, 'outer')
    a.expect(0x08, 5)

    a.ld_hl('passed')
    a.jp(0xCD, 'print')
    a.label('stop')
    a.jp(0xC3, 'stop')
    a.label('fail')
    a.ld_hl('failed')
    a.jp(0xCD, 'print')
    a.jp(0xC3, 'stop')

    a.label('latch')                         # 00 then 01 to 6000
    a.db(0xAF, 0xEA, 0x00, 0x60, 0x3E, 0x01, 0xEA, 0x00, 0x60, 0xC9)
    a.label('print')                         # Send the string at HL
    a.db(0x2A, 0xB7, 0xC8)                   # ld a,(hl+); or a; ret z
    a.db(0xE0, 0x01, 0x3E, 0x81, 0xE0, 0x02) # ldh (SB),a; ld a,81; ldh (SC),a
    a.jp(0xC3, 'print')
    a.label('passed')
    a.db(*b'MBC3 RTC Passed', 0)
    a.label('failed')
    a.db(*b'MBC3 RTC Failed', 0)

    rom = bytearray(0x8000)
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])      # nop; jp 0150
    rom[0x147] = 0x0F                                        # MBC3+TIMER+BATTERY
    code = a.link()
    rom[0x150:0x150 + len(code)] = code
    open(path, 'wb').write(rom)


if __name__ == '__main__':
    make(sys.argv[2])
