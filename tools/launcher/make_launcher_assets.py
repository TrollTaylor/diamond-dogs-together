"""Build the launcher's icon and in-app logo from the project logo.

Reads docs/readme/logo.png (the Diamond Dogs Together emblem, RGBA) and writes
  native/launcher/resources/ddt_launcher.ico  (16..256 px, PNG-compressed entries)
  native/launcher/resources/ddt_logo.png      (the emblem, 256 px wide, for the window)

Pure standard library (zlib + struct): no downloads, no image packages. The
small icon sizes (<= 32 px) use a simplified emblem drawn here (the shield with
an orange inner diamond), because the full artwork turns to noise at that size.

Usage: python tools/launcher/make_launcher_assets.py [--preview DIR]
"""
import argparse
import os
import struct
import zlib

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SOURCE = os.path.join(ROOT, 'docs', 'readme', 'logo.png')
OUT_DIR = os.path.join(ROOT, 'native', 'launcher', 'resources')
ACCENT = (232, 134, 46)


def read_png_rgba(path):
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    pos = 8
    width = height = None
    idat = b''
    while pos < len(data):
        length, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b'IHDR':
            width, height, depth, color, _, _, interlace = struct.unpack('>IIBBBBB', body)
            if depth != 8 or color != 6 or interlace != 0:
                raise ValueError('expected 8-bit RGBA, not interlaced')
        elif kind == b'IDAT':
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray(stride * height)
    prev = bytearray(stride)
    src = 0
    for y in range(height):
        ftype = raw[src]
        line = bytearray(raw[src + 1:src + 1 + stride])
        src += 1 + stride
        if ftype == 1:
            for i in range(4, stride):
                line[i] = (line[i] + line[i - 4]) & 255
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif ftype == 3:
            for i in range(stride):
                left = line[i - 4] if i >= 4 else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif ftype == 4:
            for i in range(stride):
                a = line[i - 4] if i >= 4 else 0
                b = prev[i]
                c = prev[i - 4] if i >= 4 else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, out


def encode_png(width, height, rgba):
    rows = b''.join(b'\x00' + bytes(rgba[y * width * 4:(y + 1) * width * 4]) for y in range(height))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)

    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def pad_square(width, height, rgba):
    side = max(width, height)
    out = bytearray(side * side * 4)
    ox, oy = (side - width) // 2, (side - height) // 2
    for y in range(height):
        start = ((y + oy) * side + ox) * 4
        out[start:start + width * 4] = rgba[y * width * 4:(y + 1) * width * 4]
    return side, side, out


def resize(width, height, rgba, new_w, new_h):
    """Area-average downscale in premultiplied alpha (no dark fringes)."""
    out = bytearray(new_w * new_h * 4)
    sx, sy = width / new_w, height / new_h
    for y in range(new_h):
        y0, y1 = y * sy, (y + 1) * sy
        for x in range(new_w):
            x0, x1 = x * sx, (x + 1) * sx
            r = g = b = a = wsum = 0.0
            iy = int(y0)
            while iy < y1 and iy < height:
                wy = min(y1, iy + 1) - max(y0, iy)
                ix = int(x0)
                while ix < x1 and ix < width:
                    w = wy * (min(x1, ix + 1) - max(x0, ix))
                    p = (iy * width + ix) * 4
                    al = rgba[p + 3] / 255.0
                    r += rgba[p] * al * w
                    g += rgba[p + 1] * al * w
                    b += rgba[p + 2] * al * w
                    a += al * w
                    wsum += w
                    ix += 1
                iy += 1
            q = (y * new_w + x) * 4
            if a > 0:
                out[q] = min(255, int(round(r / a)))
                out[q + 1] = min(255, int(round(g / a)))
                out[q + 2] = min(255, int(round(b / a)))
            out[q + 3] = min(255, int(round(255 * a / wsum)))
    return out


def draw_small_emblem(size):
    """The emblem's silhouette for tiny sizes: a shield (pentagon) with a black
    rim, a light face and an orange diamond, rendered with 4x4 supersampling."""
    ss = 4
    big = size * ss

    def inside(poly, px, py):
        c = False
        n = len(poly)
        for i in range(n):
            x1, y1 = poly[i]
            x2, y2 = poly[(i + 1) % n]
            if (y1 > py) != (y2 > py) and px < (x2 - x1) * (py - y1) / (y2 - y1) + x1:
                c = not c
        return c

    def scaled(poly, inset):
        cx, cy = 0.5, 0.52
        return [((cx + (x - cx) * (1 - inset)) * big, (cy + (y - cy) * (1 - inset)) * big) for x, y in poly]

    shield = [(0.20, 0.04), (0.80, 0.04), (0.99, 0.36), (0.50, 0.98), (0.01, 0.36)]
    outer = scaled(shield, 0.0)
    face = scaled(shield, 0.16)
    gem = [(0.5 * big, 0.24 * big), (0.76 * big, 0.47 * big), (0.5 * big, 0.80 * big), (0.24 * big, 0.47 * big)]
    acc = [[0.0, 0.0, 0.0, 0.0] for _ in range(size * size)]
    for y in range(big):
        for x in range(big):
            px, py = x + 0.5, y + 0.5
            if not inside(outer, px, py):
                continue
            if inside(gem, px, py):
                col = ACCENT
            elif inside(face, px, py):
                col = (214, 214, 210)
            else:
                col = (12, 12, 12)
            cell = acc[(y // ss) * size + (x // ss)]
            cell[0] += col[0]
            cell[1] += col[1]
            cell[2] += col[2]
            cell[3] += 1
    out = bytearray(size * size * 4)
    for i, (r, g, b, n) in enumerate(acc):
        if n:
            out[i * 4:i * 4 + 4] = bytes((int(r / n), int(g / n), int(b / n), int(255 * n / (ss * ss))))
    return out


def write_ico(path, images):
    header = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b''
    blobs = b''
    for size, png in images:
        dim = 0 if size >= 256 else size
        entries += struct.pack('<BBBBHHII', dim, dim, 0, 0, 1, 32, len(png), offset)
        offset += len(png)
        blobs += png
    with open(path, 'wb') as f:
        f.write(header + entries + blobs)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--preview', help='also write each icon size as a PNG here')
    args = parser.parse_args()
    w, h, rgba = read_png_rgba(SOURCE)
    sw, sh, square = pad_square(w, h, rgba)
    images = []
    for size in (256, 64, 48, 40, 32, 24, 20, 16):
        if size >= 40:
            px = resize(sw, sh, square, size, size)
        else:
            px = draw_small_emblem(size)
        png = encode_png(size, size, px)
        images.append((size, png))
        if args.preview:
            os.makedirs(args.preview, exist_ok=True)
            open(os.path.join(args.preview, 'icon-%d.png' % size), 'wb').write(png)
    write_ico(os.path.join(OUT_DIR, 'ddt_launcher.ico'), images)
    lw = 256
    lh = int(round(h * lw / w))
    logo = resize(w, h, rgba, lw, lh)
    open(os.path.join(OUT_DIR, 'ddt_logo.png'), 'wb').write(encode_png(lw, lh, logo))
    print('wrote ddt_launcher.ico (%d sizes) and ddt_logo.png (%dx%d)' % (len(images), lw, lh))


if __name__ == '__main__':
    main()
