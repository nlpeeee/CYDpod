#!/usr/bin/env python3
"""Generate firmware/cydpod_player/app/splash_logo.h from assets/logo/cydpod-wordmark-on-yellow.svg.

Pure Python (stdlib only): flattens the SVG outline paths (M/L/H/V/Q/A/Z), fills them with a
nonzero-winding scanline filler at 8x8 supersampling, and writes 4-bit alpha masks (two pixels
per byte, high nibble first, row-major, stride ceil(w/2)) for portrait and landscape.

  python -I tools/gen_splash.py                         regenerate splash_logo.h
  python -I tools/gen_splash.py --png IN.ppm OUT.png    helper used by tests/build_splash.sh
"""
import math, os, re, struct, sys, zlib

SS = 8                        # supersampling per axis
SIZES = {'P': 240, 'L': 320}  # screen width per orientation; the mask is 80% of it
FRACTION = 0.8
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVG = os.path.join(ROOT, 'assets', 'logo', 'cydpod-wordmark-on-yellow.svg')
OUT = os.path.join(ROOT, 'firmware', 'cydpod_player', 'app', 'splash_logo.h')


def arc_points(x0, y0, rx, ry, large, sweep, x1, y1, steps=96):
    """SVG endpoint arc -> polyline (x-axis rotation ignored: every arc in the file is a circle)."""
    dx, dy = (x0 - x1) / 2.0, (y0 - y1) / 2.0
    rx, ry = abs(rx), abs(ry)
    lam = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry)
    if lam > 1:
        rx *= math.sqrt(lam)
        ry *= math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * dy * dy - ry * ry * dx * dx
    den = rx * rx * dy * dy + ry * ry * dx * dx
    co = math.sqrt(max(0.0, num / den)) * (-1 if large == sweep else 1)
    cxp, cyp = co * rx * dy / ry, -co * ry * dx / rx
    cx, cy = cxp + (x0 + x1) / 2.0, cyp + (y0 + y1) / 2.0
    a0 = math.atan2((dy - cyp) / ry, (dx - cxp) / rx)
    a1 = math.atan2((-dy - cyp) / ry, (-dx - cxp) / rx)
    d = a1 - a0
    if sweep and d < 0:
        d += 2 * math.pi
    if not sweep and d > 0:
        d -= 2 * math.pi
    return [(cx + rx * math.cos(a0 + d * i / steps), cy + ry * math.sin(a0 + d * i / steps))
            for i in range(1, steps + 1)]


def parse_path(d):
    """Return a list of closed, flattened polygons."""
    toks = re.findall(r'[MLHVQAZ]|-?\d+(?:\.\d+)?', d)
    state = {'i': 0}
    polys, cur, pos, cmd = [], None, (0.0, 0.0), None

    def num():
        v = float(toks[state['i']])
        state['i'] += 1
        return v
    while state['i'] < len(toks):
        if toks[state['i']] in 'MLHVQAZ':
            cmd = toks[state['i']]
            state['i'] += 1
        if cmd == 'M':
            if cur:
                polys.append(cur)
            pos = (num(), num())
            cur = [pos]
            cmd = 'L'
        elif cmd == 'L':
            pos = (num(), num())
            cur.append(pos)
        elif cmd == 'H':
            pos = (num(), pos[1])
            cur.append(pos)
        elif cmd == 'V':
            pos = (pos[0], num())
            cur.append(pos)
        elif cmd == 'Q':
            c, e = (num(), num()), (num(), num())
            for k in range(1, 13):
                t = k / 12.0
                cur.append(((1 - t) ** 2 * pos[0] + 2 * (1 - t) * t * c[0] + t * t * e[0],
                            (1 - t) ** 2 * pos[1] + 2 * (1 - t) * t * c[1] + t * t * e[1]))
            pos = e
        elif cmd == 'A':
            rx, ry, _rot, la, sw = num(), num(), num(), int(num()), int(num())
            e = (num(), num())
            cur.extend(arc_points(pos[0], pos[1], rx, ry, la, sw, e[0], e[1]))
            pos = e
        elif cmd == 'Z':
            if cur:
                polys.append(cur)
                cur = None
            cmd = None
    if cur:
        polys.append(cur)
    return polys


def load():
    svg = open(SVG, encoding='utf-8').read()
    vb = [float(v) for v in re.search(r'viewBox="([^"]+)"', svg).group(1).split()]
    m = re.search(r'matrix\(([^)]+)\)', svg)
    a, b, c, dd, e, f = [float(v) for v in m.group(1).split()]
    paths = []
    for d in re.findall(r'<path[^>]* d="([^"]+)"', svg):
        paths.append([[(a * x + c * y + e, b * x + dd * y + f) for x, y in p] for p in parse_path(d)])
    return vb, paths


def rasterise(vb, paths, w, h):
    """4-bit alpha mask (list of rows); each path is filled with nonzero winding."""
    sx, sy = w * SS / vb[2], h * SS / vb[3]
    cov = [[0] * w for _ in range(h)]
    for polys in paths:
        edges = []
        for p in polys:
            q = [((x - vb[0]) * sx, (y - vb[1]) * sy) for x, y in p]
            for k in range(len(q)):
                (x0, y0), (x1, y1) = q[k], q[(k + 1) % len(q)]
                if y0 != y1:
                    edges.append((x0, y0, x1, y1))
        for sy_i in range(h * SS):
            yc = sy_i + 0.5
            xs = []
            for x0, y0, x1, y1 in edges:
                if (y0 <= yc < y1) or (y1 <= yc < y0):
                    xs.append((x0 + (yc - y0) * (x1 - x0) / (y1 - y0), 1 if y1 > y0 else -1))
            xs.sort()
            wind, row = 0, cov[sy_i // SS]
            for k in range(len(xs) - 1):
                wind += xs[k][1]
                if wind != 0:
                    lo = max(0, int(math.ceil(xs[k][0] - 0.5)))
                    hi = min(w * SS, int(math.ceil(xs[k + 1][0] - 0.5)))
                    for sx_i in range(lo, hi):
                        row[sx_i // SS] += 1
    full = SS * SS
    return [[(min(v, full) * 15 + full // 2) // full for v in row] for row in cov]


def pack(mask):
    data = []
    for row in mask:
        r = row + [0] * (len(row) & 1)
        data += [(r[i] << 4) | r[i + 1] for i in range(0, len(r), 2)]
    return data


def main():
    vb, paths = load()
    lines = ['// Generated by tools/gen_splash.py from assets/logo/cydpod-wordmark-on-yellow.svg -- do not edit.',
             '// 4-bit alpha masks (0 = background, 15 = ink), two pixels per byte (high nibble first),',
             '// row-major, stride (w+1)/2 bytes. Portrait mask is 80% of 240 px, landscape 80% of 320 px.',
             '#pragma once', '#include <stdint.h>', '#ifndef PROGMEM', '#define PROGMEM', '#endif', '']
    for key, sw in SIZES.items():
        w = int(sw * FRACTION)
        h = int(round(w * vb[3] / vb[2]))
        data = pack(rasterise(vb, paths, w, h))
        name = 'portrait' if key == 'P' else 'landscape'
        lines.append('static const int SPLASH_%s_W = %d;' % (name.upper(), w))
        lines.append('static const int SPLASH_%s_H = %d;' % (name.upper(), h))
        lines.append('static const uint8_t SPLASH_%s[%d] PROGMEM = {' % (name.upper(), len(data)))
        for i in range(0, len(data), 24):
            lines.append('  ' + ','.join('0x%02X' % b for b in data[i:i + 24]) + ',')
        lines.append('};')
        lines.append('')
        print('%s %dx%d %d bytes' % (name, w, h, len(data)))
    with open(OUT, 'w', newline='\n') as f:
        f.write('\n'.join(lines))


def ppm_to_png(src, dst):
    raw = open(src, 'rb').read()
    m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', raw)
    w, h = int(m.group(1)), int(m.group(2))
    px = raw[m.end():]
    body = b''.join(b'\x00' + px[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    open(dst, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                          + chunk(b'IDAT', zlib.compress(body, 9)) + chunk(b'IEND', b''))


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == '--png':
        ppm_to_png(sys.argv[2], sys.argv[3])
    else:
        main()
