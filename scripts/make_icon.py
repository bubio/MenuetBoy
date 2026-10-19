"""Draw the MenuetBoy icon into the system icon sheet.

  make_icon.py <i.png> <out.png> [preview.png]

MenuetOS desktop and menu icons are 32x32 cells of /fd/1/i.png, a 320x320
RGB PNG (icon.mnt / menu.mnt refer to them as i.png:XY, X column, Y row).
The desktop ignores the file name and always takes the cell from i.png,
so the icon goes into CELL_X, CELL_Y, which is unused (white) and not
referred to by the system. Black is the background, as in the other cells.
"""
import struct
import sys
import zlib

SIZE = 320
CELL = 32
CELL_X, CELL_Y = 0, 6

BODY = (200, 200, 192)
EDGE = (120, 120, 128)
BEZEL = (88, 88, 104)
LCD = (155, 188, 15)
LCD_DARK = (48, 98, 48)
KEY = (24, 24, 24)
BUTTON = (160, 48, 106)


def draw():
    px = [[(0, 0, 0)] * CELL for _ in range(CELL)]

    def rect(x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                px[y][x] = c

    rect(6, 1, 25, 30, EDGE)                 # Body with outline
    rect(7, 2, 24, 29, BODY)
    for x, y in [(6, 1), (25, 1), (6, 30)]:  # Rounded corners
        px[y][x] = (0, 0, 0)
    for d in range(4):                       # Large bottom right curve
        rect(25 - d, 30 - 3 + d, 25, 30 - 3 + d, (0, 0, 0))
        px[30 - 3 + d][24 - d] = EDGE
    rect(9, 4, 22, 15, BEZEL)                # Screen
    rect(11, 6, 20, 13, LCD)
    for x, y in [(13, 11), (14, 11), (14, 10), (15, 10), (16, 9), (17, 9),
                 (17, 10), (18, 10), (18, 11), (13, 12), (18, 12)]:
        px[y][x] = LCD_DARK                  # A hill on the screen
    rect(10, 21, 14, 21, KEY)                # D-pad
    rect(12, 19, 12, 23, KEY)
    rect(11, 20, 13, 22, KEY)
    for cx, cy in [(21, 22), (18, 23)]:      # B and A buttons
        rect(cx, cy - 1, cx + 1, cy, BUTTON)
    rect(13, 26, 14, 26, EDGE)               # Select, Start
    rect(16, 26, 17, 26, EDGE)
    return px


def read_png(path):
    """8-bit RGB, non-interlaced only (as i.png is). Returns rows of pixels."""
    data = open(path, 'rb').read()
    pos, idat = 8, b''
    while pos < len(data):
        n, = struct.unpack('>I', data[pos:pos + 4])
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b'IHDR':
            w, h = struct.unpack('>II', body[:8])
            assert body[8:] == b'\x08\x02\x00\x00\x00', 'not 8-bit RGB'
        elif kind == b'IDAT':
            idat += body
    raw, stride, rows = zlib.decompress(idat), w * 3, []
    prev = bytearray(stride)
    for y in range(h):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - 3] if x >= 3 else 0
            b = prev[x]
            c = prev[x - 3] if x >= 3 else 0
            if kind == 1:
                line[x] = (line[x] + a) & 255
            elif kind == 2:
                line[x] = (line[x] + b) & 255
            elif kind == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif kind == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc
                                      else b if pb <= pc else c)) & 255
        rows.append([tuple(line[i:i + 3]) for i in range(0, stride, 3)])
        prev = line
    return rows


def png(path, w, h, pixel):
    raw = b''.join(b'\0' + b''.join(bytes(pixel(x, y)) for x in range(w))
                   for y in range(h))

    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n'
                           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 9))
                           + chunk(b'IEND', b''))


def main():
    px = draw()
    sheet = read_png(sys.argv[1])
    x0, y0 = CELL_X * CELL, CELL_Y * CELL
    assert all(c == (255, 255, 255) for row in sheet[y0:y0 + CELL]
               for c in row[x0:x0 + CELL]), 'icon cell is not empty'
    for y in range(CELL):
        sheet[y0 + y][x0:x0 + CELL] = px[y]
    png(sys.argv[2], SIZE, SIZE, lambda x, y: sheet[y][x])
    if len(sys.argv) > 3:                    # 8x preview of the cell
        png(sys.argv[3], CELL * 8, CELL * 8, lambda x, y: px[y // 8][x // 8])


if __name__ == '__main__':
    main()
