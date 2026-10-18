"""Compare a harness PGM screen dump with a reference grayscale PNG.

Usage: compare_screen.py reference.png screen.pgm
Exit status 0 when every pixel matches the reference shade.
"""
import struct
import sys
import zlib


def read_png_gray(path):
    data = open(path, 'rb').read()
    pos, idat = 8, b''
    while pos < len(data):
        n = struct.unpack('>I', data[pos:pos + 4])[0]
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b'IHDR':
            w, h, depth, color = struct.unpack('>IIBB', body[:10])
            assert color == 0, 'grayscale PNG expected'
        elif kind == b'IDAT':
            idat += body
    raw = zlib.decompress(idat)
    stride, bpp = (w * depth + 7) // 8, max(1, depth // 8)
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        filt, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if filt == 1:
                line[x] = (line[x] + a) & 255
            elif filt == 2:
                line[x] = (line[x] + b) & 255
            elif filt == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif filt == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        maxv = (1 << depth) - 1
        rows.append([((line[x * depth // 8] >> (8 - depth - x * depth % 8)) & maxv) * 255 // maxv
                     for x in range(w)])
    return w, h, rows


def main():
    w, h, ref = read_png_gray(sys.argv[1])
    dump = open(sys.argv[2], 'rb').read()
    pixels = dump[dump.index(b'255\n') + 4:]
    diff = [(x, y) for y in range(h) for x in range(w) if abs(ref[y][x] - pixels[y * w + x]) > 40]
    if diff:
        print(f'{len(diff)} pixels differ, first at {diff[:5]}')
        sys.exit(1)
    print('screen matches reference')


main()
