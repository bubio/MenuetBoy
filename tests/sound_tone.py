"""Square-wave tone test for the APU.

  sound_tone.py make <out.gb>      write a ROM playing ch2 at 440Hz
  sound_tone.py check <audio.raw>  check s16le stereo 48kHz output
"""
import struct
import sys

RATE = 48000
FREQ_REG = 1750                       # 131072 / (2048 - 1750) = 439.8 Hz
EXPECTED_HZ = 131072 / (2048 - FREQ_REG)


def make(path):
    rom = bytearray(0x8000)
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])      # nop; jp 0150
    code = []
    for reg, val in [(0x26, 0x80), (0x24, 0x77), (0x25, 0x22), (0x16, 0x80),
                     (0x17, 0xF0), (0x18, FREQ_REG & 0xFF),
                     (0x19, 0x80 | FREQ_REG >> 8)]:
        code += [0x3E, val, 0xE0, reg]                       # ld a,n; ldh (reg),a
    code += [0x18, 0xFE]                                     # jr $
    rom[0x150:0x150 + len(code)] = bytes(code)
    open(path, 'wb').write(rom)


def check(path):
    data = open(path, 'rb').read()
    frames = list(struct.iter_unpack('<hh', data))[RATE // 10:]  # Skip settling
    left = [l for l, _ in frames]
    right = [r for _, r in frames]
    crossings = sum(1 for a, b in zip(left, left[1:]) if a < 0 <= b)
    hz = crossings * RATE / len(left)
    peak_l, peak_r = max(map(abs, left)), max(map(abs, right))
    print(f'{hz:.1f} Hz (expected {EXPECTED_HZ:.1f}), peak L {peak_l} R {peak_r}')
    ok = abs(hz - EXPECTED_HZ) < 2 and peak_l > 3000 and peak_r > 3000
    print('Passed' if ok else 'Failed')
    return ok


if __name__ == '__main__':
    if sys.argv[1] == 'make':
        make(sys.argv[2])
    else:
        sys.exit(0 if check(sys.argv[2]) else 1)
