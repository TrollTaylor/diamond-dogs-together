"""Build the launcher's icon and in-window logo from the project logo.

Reads docs/readme/logo.png (the Diamond Dogs Together logo: two dog heads in the
diamond with the "DIAMOND DOGS TOGETHER" banner, RGBA) and writes
  native/launcher/resources/ddt_launcher.ico  the app icon, 16 20 24 32 40 48 64 96 128 256 px
  native/launcher/resources/ddt_logo.png      the logo 256 px wide, shown in the window

Every icon size is the logo itself, scaled down. Nothing is drawn by hand:
  - an exact area (box) filter in premultiplied alpha, the logo centred and as wide
    as the icon;
  - the logo is moved up or down by less than half a pixel so the diamond's top edge
    falls on a pixel boundary, which keeps the rim crisp at small sizes;
  - a light unsharp mask, a little stronger at small sizes (SHARPEN);
  - at 48 px and below the banner is drawn without its lettering: "DIAMOND DOGS
    TOGETHER" would be 1 to 3 px tall there and only adds grey noise. The banner's
    face is repainted with its own shading (a least-squares plane fit of its clean
    pixels); the diamond, the dogs and the banner's shape are untouched.
The 256 px image is PNG-compressed, the others are 32-bit bitmaps with a mask (the
usual icon layout, which every part of Windows reads).

Standard library only (zlib + struct), no downloads, no image packages, and
deterministic: the same logo gives the same pixels on every machine (the PNG bytes
can differ between zlib builds, so --check compares pixels).
tests/launcher_icon_tests.cpp checks the committed files independently: every size
must match the logo scaled by Windows' own scaler, and the built exe must embed them.

Usage: python tools/launcher/make_launcher_assets.py [--check] [--preview DIR]
  (no option)    regenerate both files
  --check        regenerate in memory; exit 1 if the committed files differ
  --preview DIR  also write each size as icon-<size>.png and contact sheets of all
                 sizes at 1x and magnified, on a light and a dark background
"""
import argparse
import itertools
import os
import struct
import sys
import zlib

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SOURCE = os.path.join(ROOT, 'docs', 'readme', 'logo.png')
OUT_DIR = os.path.join(ROOT, 'native', 'launcher', 'resources')
ICO_PATH = os.path.join(OUT_DIR, 'ddt_launcher.ico')
EMBLEM_PATH = os.path.join(OUT_DIR, 'ddt_logo.png')

ICON_SIZES = (256, 128, 96, 64, 48, 40, 32, 24, 20, 16)
PLAIN_BANNER_UP_TO = 48          # sizes at or below this drop the banner lettering
SHARPEN = {256: 0.3, 128: 0.5, 96: 0.5, 64: 0.7, 48: 1.0, 40: 1.0, 32: 1.0, 24: 1.0, 20: 1.0, 16: 1.0}
SHARPEN_SIGMA = 0.7
EMBLEM_WIDTH = 256
# A point on the banner's face, between its two lines of lettering (logo pixels).
BANNER_SEED = (300, 375)


# ---- PNG -----------------------------------------------------------------------
def read_png_rgba(path_or_bytes):
    data = path_or_bytes if isinstance(path_or_bytes, bytes) else open(path_or_bytes, 'rb').read()
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


# ---- The banner without its lettering ------------------------------------------
def _flood(start, allowed, width, height):
    """4-connected flood fill from the indices in `start` through indices where allowed[i]."""
    seen = bytearray(width * height)
    stack = [i for i in start if allowed[i]]
    for i in stack:
        seen[i] = 1
    while stack:
        i = stack.pop()
        x, y = i % width, i // width
        for j, ok in ((i - 1, x > 0), (i + 1, x < width - 1), (i - width, y > 0), (i + width, y < height - 1)):
            if ok and allowed[j] and not seen[j]:
                seen[j] = 1
                stack.append(j)
    return seen


def _grow(mask, steps, width, height):
    """Dilate a 0/1 mask by `steps` 4-neighbour steps (pixels within that city-block distance)."""
    out = bytearray(mask)
    edge = [i for i in range(width * height) if mask[i]]
    for _ in range(steps):
        nxt = []
        for i in edge:
            x, y = i % width, i // width
            for j, ok in ((i - 1, x > 0), (i + 1, x < width - 1), (i - width, y > 0), (i + width, y < height - 1)):
                if ok and not out[j]:
                    out[j] = 1
                    nxt.append(j)
        edge = nxt
    return out


def plain_banner(width, height, rgba):
    """The logo with the banner's lettering removed: the banner's light face (the region
    around BANNER_SEED) with its holes (the letters) filled, repainted with a plane fit
    of the face's own clean pixels. Everything outside the face is the logo unchanged."""
    n = width * height
    light = bytearray(1 if rgba[i * 4 + 3] > 127 and rgba[i * 4] + rgba[i * 4 + 1] + rgba[i * 4 + 2] > 450 else 0
                      for i in range(n))
    sx, sy = BANNER_SEED
    face = _flood([sy * width + sx], light, width, height)
    xs = [i % width for i in range(n) if face[i]]
    ys = [i // width for i in range(n) if face[i]]
    if not (20000 <= len(xs) <= 50000 and min(ys) > 250 and max(ys) < 470 and max(xs) - min(xs) > 400):
        raise SystemExit('logo.png changed: the banner face around BANNER_SEED is not where expected; '
                         'review plain_banner() before regenerating')
    # Holes = pixels the outside cannot reach without crossing the face.
    not_face = bytearray(1 - v for v in face)
    border = [i for i in range(width)] + [n - width + i for i in range(width)] + \
             [y * width for y in range(height)] + [y * width + width - 1 for y in range(height)]
    outside = _flood(border, not_face, width, height)
    filled = bytearray(1 - v for v in outside)
    holes = bytearray(f & (1 - a) for f, a in zip(filled, face))
    near_letters = _grow(holes, 3, width, height)
    near_edge = _grow(outside, 3, width, height)
    # Least-squares plane c = a + b*x + d*y per channel over the face's clean, bright pixels.
    pts = [i for i in range(n) if face[i] and not near_letters[i] and not near_edge[i] and
           rgba[i * 4] + rgba[i * 4 + 1] + rgba[i * 4 + 2] > 555]
    cx = sum(i % width for i in pts) / len(pts)
    cy = sum(i // width for i in pts) / len(pts)
    sxx = sum((i % width - cx) ** 2 for i in pts)
    syy = sum((i // width - cy) ** 2 for i in pts)
    sxy = sum((i % width - cx) * (i // width - cy) for i in pts)
    det = sxx * syy - sxy * sxy
    planes = []
    for c in range(3):
        mean = sum(rgba[i * 4 + c] for i in pts) / len(pts)
        sxv = sum((i % width - cx) * rgba[i * 4 + c] for i in pts)
        syv = sum((i // width - cy) * rgba[i * 4 + c] for i in pts)
        planes.append((mean, (sxv * syy - syv * sxy) / det, (syv * sxx - sxv * sxy) / det))
    # Repaint the filled face except its outermost ring (the soft edge against the banner's border).
    keep_edge = _grow(outside, 1, width, height)
    out = bytearray(rgba)
    for i in range(n):
        if filled[i] and not keep_edge[i]:
            dx, dy = i % width - cx, i // width - cy
            for c in range(3):
                mean, bx, by = planes[c]
                out[i * 4 + c] = max(0, min(255, int(mean + bx * dx + by * dy + 0.5)))
            out[i * 4 + 3] = 255
    return out


# ---- Scaling -------------------------------------------------------------------
def premultiplied_prefix(width, height, rgba):
    """Per row and channel, prefix sums of premultiplied R, G, B and of A (0..255 scale)."""
    rows = []
    for y in range(height):
        base = y * width * 4
        a = [rgba[base + x * 4 + 3] for x in range(width)]
        chans = [[rgba[base + x * 4 + c] * a[x] / 255.0 for x in range(width)] for c in range(3)] + [[float(v) for v in a]]
        rows.append([(ch, list(itertools.accumulate(ch, initial=0.0))) for ch in chans])
    return rows


def _box_taps(count_src, count_dst, scale, offset):
    """For each output pixel j, the source span [(j - offset)/scale, (j + 1 - offset)/scale)
    clamped to the image, as (whole index, fraction) pairs for both ends."""
    taps = []
    for j in range(count_dst):
        ends = []
        for edge in (j - offset, j + 1 - offset):
            s = min(max(edge / scale, 0.0), float(count_src))
            k = min(int(s), count_src - 1) if s < count_src else count_src
            ends.append((k, s - k))
        taps.append(ends)
    return taps


def _box(values, prefix, ends, scale):
    (k0, f0), (k1, f1) = ends
    lo = prefix[k0] + (f0 * values[k0] if f0 else 0.0)
    hi = prefix[k1] + (f1 * values[k1] if f1 else 0.0)
    return (hi - lo) * scale


def area_scale(src_w, src_h, prefix_rows, out_w, out_h, kx, ky, ox, oy):
    """Exact box-filter resampling: output pixel (j, i) is the average of the source over
    x in [(j - ox)/kx, (j + 1 - ox)/kx), y in [(i - oy)/ky, (i + 1 - oy)/ky); transparent
    outside the source. Returns 4 premultiplied planes (R, G, B, A), each out_h x out_w."""
    xt = _box_taps(src_w, out_w, kx, ox)
    yt = _box_taps(src_h, out_h, ky, oy)
    # Horizontal pass: per source row, per channel, out_w values.
    mid = [[[_box(ch, pre, ends, kx) for ends in xt] for ch, pre in row] for row in prefix_rows]
    planes = []
    for c in range(4):
        plane = [[0.0] * out_w for _ in range(out_h)]
        for j in range(out_w):
            col = [mid[y][c][j] for y in range(src_h)]
            pre = list(itertools.accumulate(col, initial=0.0))
            for i, ends in enumerate(yt):
                plane[i][j] = _box(col, pre, ends, ky)
        planes.append(plane)
    return planes


def straight(planes, w, h):
    """Premultiplied planes -> straight colour planes (0..255) plus alpha."""
    alpha = planes[3]
    colour = [[[planes[c][y][x] * 255.0 / alpha[y][x] if alpha[y][x] > 0 else 0.0 for x in range(w)] for y in range(h)]
              for c in range(3)]
    return colour + [alpha]


def sharpen(planes, w, h, amount, sigma=SHARPEN_SIGMA):
    """Unsharp mask on straight colour that cannot create halos or fringes:
    the blur is weighted by alpha (transparent neighbours do not count), and each
    sharpened value stays within the range of its opaque 3x3 neighbourhood. Alpha is
    unchanged."""
    if amount <= 0:
        return planes
    import math
    radius = 3
    kernel = [math.exp(-(t * t) / (2 * sigma * sigma)) for t in range(-radius, radius + 1)]

    def blur(p):
        tmp = [[sum(kernel[t + radius] * p[y][x + t] for t in range(-radius, radius + 1) if 0 <= x + t < w)
                for x in range(w)] for y in range(h)]
        return [[sum(kernel[t + radius] * tmp[y + t][x] for t in range(-radius, radius + 1) if 0 <= y + t < h)
                 for x in range(w)] for y in range(h)]

    alpha = planes[3]
    weight = blur(alpha)
    out = []
    for c in range(3):
        p = planes[c]
        b = blur([[p[y][x] * alpha[y][x] for x in range(w)] for y in range(h)])
        plane = [[0.0] * w for _ in range(h)]
        for y in range(h):
            for x in range(w):
                if alpha[y][x] <= 0:
                    continue
                near = [p[j][i] for j in range(max(0, y - 1), min(h, y + 2)) for i in range(max(0, x - 1), min(w, x + 2))
                        if alpha[j][i] > 0]
                v = p[y][x] + amount * (p[y][x] - b[y][x] / weight[y][x])
                plane[y][x] = min(max(v, min(near)), max(near))
        out.append(plane)
    return out + [alpha]


def to_rgba(planes, w, h):
    """Straight colour planes plus alpha -> RGBA bytes (fully transparent pixels are 0)."""
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            ia = int(planes[3][y][x] + 0.5)
            if ia <= 0:
                continue
            q = (y * w + x) * 4
            for c in range(3):
                out[q + c] = max(0, min(255, int(planes[c][y][x] + 0.5)))
            out[q + 3] = min(255, ia)
    return out


def diamond_top(width, height, rgba):
    """Sub-pixel y of the diamond's top edge in the logo's centre column."""
    x = width // 2
    top = 0.0
    for y in range(height):
        a = rgba[(y * width + x) * 4 + 3]
        top += 1.0 - a / 255.0
        if a == 255:
            return top
    raise ValueError('no opaque pixel in the centre column')


def render_icons(width, height, rgba):
    """{size: RGBA bytes} for every icon size, all rendered from the logo."""
    plain = plain_banner(width, height, rgba)
    prefix = {'full': premultiplied_prefix(width, height, rgba), 'plain': premultiplied_prefix(width, height, plain)}
    top = diamond_top(width, height, rgba)
    side = max(width, height)
    icons = {}
    for size in ICON_SIZES:
        k = size / side
        ox = (size - width * k) / 2
        oy = (size - height * k) / 2
        oy = int(oy + top * k + 0.5) - top * k  # the diamond's top edge on a pixel boundary
        source = prefix['plain' if size <= PLAIN_BANNER_UP_TO else 'full']
        planes = straight(area_scale(width, height, source, size, size, k, k, ox, oy), size, size)
        icons[size] = to_rgba(sharpen(planes, size, size, SHARPEN[size]), size, size)
    return icons


def render_emblem(width, height, rgba):
    lw = EMBLEM_WIDTH
    lh = int(height * lw / width + 0.5)
    planes = area_scale(width, height, premultiplied_prefix(width, height, rgba), lw, lh, lw / width, lh / height, 0.0, 0.0)
    return lw, lh, to_rgba(straight(planes, lw, lh), lw, lh)


# ---- .ico ----------------------------------------------------------------------
def encode_dib(size, rgba):
    """A 32-bit icon bitmap: BITMAPINFOHEADER (height doubled), BGRA rows bottom-up, then
    the 1-bit AND mask (1 = transparent where alpha < 128), rows padded to 4 bytes."""
    mask_stride = (size + 31) // 32 * 4
    xor = bytearray()
    mask = bytearray()
    for y in range(size - 1, -1, -1):
        bits = bytearray(mask_stride)
        for x in range(size):
            r, g, b, a = rgba[(y * size + x) * 4:(y * size + x) * 4 + 4]
            xor += bytes((b, g, r, a))
            if a < 128:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits
    header = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, len(xor) + len(mask), 0, 0, 0, 0)
    return header + bytes(xor) + bytes(mask)


def decode_dib(size, data):
    bi_size, w, h2, planes, bits, compression = struct.unpack('<IiiHHI', data[:20])
    if (bi_size, w, h2, planes, bits, compression) != (40, size, size * 2, 1, 32, 0):
        raise ValueError('unexpected bitmap header for %d px' % size)
    out = bytearray(size * size * 4)
    for y in range(size):
        row = 40 + (size - 1 - y) * size * 4
        for x in range(size):
            b, g, r, a = data[row + x * 4:row + x * 4 + 4]
            out[(y * size + x) * 4:(y * size + x) * 4 + 4] = bytes((r, g, b, a))
    return out


def encode_ico(icons):
    blobs = [(size, encode_png(size, size, icons[size]) if size >= 256 else encode_dib(size, icons[size]))
             for size in ICON_SIZES]
    header = struct.pack('<HHH', 0, 1, len(blobs))
    offset = 6 + 16 * len(blobs)
    entries = b''
    for size, blob in blobs:
        dim = 0 if size >= 256 else size
        entries += struct.pack('<BBBBHHII', dim, dim, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    return header + entries + b''.join(blob for _, blob in blobs)


def decode_ico(data):
    _, kind, count = struct.unpack('<HHH', data[:6])
    if kind != 1:
        raise ValueError('not an icon file')
    icons = {}
    for i in range(count):
        w, h, _, _, _, _, length, offset = struct.unpack('<BBBBHHII', data[6 + 16 * i:22 + 16 * i])
        size = w or 256
        blob = data[offset:offset + length]
        icons[size] = read_png_rgba(blob)[2] if blob[:8] == b'\x89PNG\r\n\x1a\n' else decode_dib(size, blob)
    return icons


# ---- Previews ------------------------------------------------------------------
def _sheet(images, background, magnify):
    """images: [(w, h, rgba)] side by side over a solid background, each scaled by
    magnify(w) with nearest neighbour."""
    pad = 12
    scaled = [(w * magnify(w), h * magnify(w), w, h, rgba, magnify(w)) for w, h, rgba in images]
    sw = pad + sum(s[0] + pad for s in scaled)
    sh = pad * 2 + max(s[1] for s in scaled)
    out = bytearray(bytes(background) + b'\xff') * (sw * sh)
    x0 = pad
    for dw, dh, w, h, rgba, m in scaled:
        y0 = sh - pad - dh
        for y in range(dh):
            for x in range(dw):
                p = ((y // m) * w + x // m) * 4
                a = rgba[p + 3]
                if not a:
                    continue
                q = ((y0 + y) * sw + x0 + x) * 4
                for c in range(3):
                    out[q + c] = (rgba[p + c] * a + background[c] * (255 - a) + 127) // 255
        x0 += dw + pad
    return sw, sh, out


def write_previews(directory, icons, emblem):
    os.makedirs(directory, exist_ok=True)
    for size in ICON_SIZES:
        open(os.path.join(directory, 'icon-%d.png' % size), 'wb').write(encode_png(size, size, icons[size]))
    ordered = [(s, s, icons[s]) for s in sorted(ICON_SIZES)]
    for name, background in (('light', (243, 243, 243)), ('dark', (32, 32, 32))):
        for label, magnify in (('1x', lambda w: 1), ('zoom', lambda w: max(1, 256 // w))):
            w, h, px = _sheet(ordered, background, magnify)
            open(os.path.join(directory, 'sheet-%s-%s.png' % (label, name)), 'wb').write(encode_png(w, h, px))
        w, h, px = _sheet([emblem], background, lambda w: 1)
        open(os.path.join(directory, 'emblem-%s.png' % name), 'wb').write(encode_png(w, h, px))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--check', action='store_true', help='exit 1 if the committed files differ from a fresh render')
    parser.add_argument('--preview', help='also write each icon size and contact sheets here')
    args = parser.parse_args()
    w, h, rgba = read_png_rgba(SOURCE)
    icons = render_icons(w, h, rgba)
    emblem = render_emblem(w, h, rgba)
    if args.preview:
        write_previews(args.preview, icons, emblem)
    if args.check:
        problems = []
        committed = decode_ico(open(ICO_PATH, 'rb').read())
        if sorted(committed) != sorted(ICON_SIZES):
            problems.append('ddt_launcher.ico sizes %s, expected %s' % (sorted(committed), sorted(ICON_SIZES)))
        for size in ICON_SIZES:
            if size in committed and committed[size] != icons[size]:
                problems.append('ddt_launcher.ico %d px differs from the logo render' % size)
        ew, eh, epx = read_png_rgba(EMBLEM_PATH)
        if (ew, eh, epx) != emblem:
            problems.append('ddt_logo.png differs from the logo render')
        for line in problems:
            print('FAIL: ' + line)
        print('launcher assets: %s' % ('out of date, run tools/launcher/make_launcher_assets.py' if problems else 'match the logo'))
        return 1 if problems else 0
    open(ICO_PATH, 'wb').write(encode_ico(icons))
    lw, lh, logo = emblem
    open(EMBLEM_PATH, 'wb').write(encode_png(lw, lh, logo))
    print('wrote ddt_launcher.ico (%s px) and ddt_logo.png (%dx%d)' % (' '.join(str(s) for s in sorted(ICON_SIZES)), lw, lh))
    return 0


if __name__ == '__main__':
    sys.exit(main())
