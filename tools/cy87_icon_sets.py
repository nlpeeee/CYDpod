#!/usr/bin/env python3
"""CY-87 icon design previews, plus CY-90 selected-C export/verification (standard library only).

Generates, with no dependencies and no downloads:
  * three icon/button set options (A classic primitives, B click-wheel 1-bit bitmaps,
    C thin-stroke 4-bit alpha masks) plus a "current v23" reference that ports ui_icon() verbatim;
  * PNG sheets (native and 4x) in the shipped light palette and two proposed palettes;
  * a 320x240 Now Playing mock per option drawn with the CY-85 text faces (Adafruit GFX FreeSans
    bitmaps from the pinned TFT_eSPI, read from the already installed library, never copied);
  * a cost table (flash bytes, SPI window commands and wire bytes per redraw).

Primitive options (current, A) are rasterised by ports of TFT_eSPI 2.5.43 fillTriangle, fillCircle,
fillRoundRect, drawLine and friends, so their pixels follow the firmware's integer arithmetic.
Bitmap options (B, C) are supersampled 4x4 here and then thresholded (1-bit) or quantised (4-bit);
the shipped data are exactly those bytes. Default preview mode leaves firmware/tests untouched;
--export-c writes the selected C flash assets and source-mask fingerprints for CY-90.
--verify-production compares production host PNGs with the source shapes and recompresses losslessly.

Usage:  python -I tools/cy87_icon_sets.py [--fonts DIR_WITH_FreeSans9pt7b.h] [--out DIR]
        python -I tools/cy87_icon_sets.py --export-c
        python -I tools/cy87_icon_sets.py --verify-production tests/out/icon-preview
"""
import argparse
import json
import math
import re
import struct
import subprocess
import sys
import zlib
from pathlib import Path

# ----------------------------------------------------------------------------- colour / PNG
def c565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def rgb888(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def blend565(fg, bg, a, amax=15):
    """Per-channel blend in the 565 domain: what a firmware blitter would do (integer only)."""
    if a <= 0:
        return bg
    if a >= amax:
        return fg
    out = 0
    for shift, bits in ((11, 5), (5, 6), (0, 5)):
        m = (1 << bits) - 1
        f, b = (fg >> shift) & m, (bg >> shift) & m
        out |= (b + ((f - b) * a + amax // 2) // amax) << shift
    return out


def luminance(c):
    def lin(v):
        v /= 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = rgb888(c)
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)


def contrast(a, b):
    la, lb = luminance(a), luminance(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


def png_bytes(w, h, rows):
    raw = b"".join(b"\x00" + bytes(row) for row in rows)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


# ----------------------------------------------------------------------------- GFX fonts (CY-85 faces)
class GfxFont:
    """Adafruit GFX font parsed from the TFT_eSPI header; same data classic_text.cpp draws."""

    def __init__(self, path, stem):
        text = Path(path).read_text()
        bm = re.search(stem + r"Bitmaps\[\][^{]*\{(.*?)\};", text, re.S).group(1)
        self.bitmap = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", bm))
        gl = re.search(stem + r"Glyphs\[\][^{]*\{(.*?)\};", text, re.S).group(1)
        self.glyphs = [tuple(int(v) for v in m) for m in re.findall(
            r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(-?\d+)\s*\}", gl)]
        tail = re.search(r"GFXfont\s+" + stem + r"[^{]*\{.*?0x([0-9A-F]+),\s*0x([0-9A-F]+),\s*(\d+)\s*\}", text, re.S)
        self.first, self.last, self.y_advance = int(tail.group(1), 16), int(tail.group(2), 16), int(tail.group(3))
        if len(self.glyphs) != self.last - self.first + 1:
            raise SystemExit(f"{path}: glyph table size mismatch")

    def glyph(self, ch):
        c = ord(ch)
        if c < self.first or c > self.last:
            c = 32
        return self.glyphs[c - self.first]

    def cap(self):
        return self.glyph("H")[2]

    def width(self, s):
        return sum(self.glyph(ch)[3] for ch in s)

    def draw(self, cv, s, x, cy, align, fg):
        """Exactly cydtext::draw: cap-height centre at cy, 1:1, runs through hline."""
        total = self.width(s)
        left = x - total // 2 if align == 1 else x - total if align == 2 else x
        base = cy - self.cap() // 2 + self.cap()
        for ch in s:
            off, w, h, adv, xo, yo = self.glyph(ch)
            bits, n = self.bitmap, 0
            for row in range(h):
                run = -1
                for col in range(w + 1):
                    on = False
                    if col < w:
                        byte = bits[off + (n >> 3)]
                        on = bool(byte & (0x80 >> (n & 7)))
                        n += 1
                    if on and run < 0:
                        run = col
                    elif not on and run >= 0:
                        cv.hline(left + xo + run, base + yo + row, col - run, fg)
                        run = -1
            left += adv
        return total


def find_font_dir(arg):
    cands = []
    if arg:
        cands.append(Path(arg))
    here = Path(__file__).resolve().parent.parent
    cands.append(here / ".deps/sketchbook/libraries/TFT_eSPI/Fonts/GFXFF")
    try:
        common = subprocess.run(["git", "-C", str(here), "rev-parse", "--git-common-dir"],
                                capture_output=True, text=True, check=True).stdout.strip()
        cands.append((here / common).resolve().parent / ".deps/sketchbook/libraries/TFT_eSPI/Fonts/GFXFF")
    except Exception:
        pass
    for c in cands:
        if (c / "FreeSans9pt7b.h").exists():
            return c
    raise SystemExit("FreeSans9pt7b.h not found; pass --fonts <TFT_eSPI/Fonts/GFXFF>")


# The original starting point for this library was the Adafruit_ILI9341
# library in January 2015.
#
# The licence for that library is MIT.
#
# The first evolution of the library that led to TFT_eSPI is recorded here:
#
# https://www.instructables.com/id/Arduino-TFT-display-and-font-library/
#
# Adafruit_ILI9341 ORIGINAL LIBRARY HEADER:
#
# vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvStartvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
#   This is our library for the Adafruit  ILI9341 Breakout and Shield
#   ----> http://www.adafruit.com/products/1651
#
#   Check out the links above for our tutorials and wiring diagrams
#   These displays use SPI to communicate, 4 or 5 pins are required to
#   interface (RST is optional)
#   Adafruit invests time and resources providing this open source code,
#   please support Adafruit and open-source hardware by purchasing
#   products from Adafruit!
#
#   Written by Limor Fried/Ladyada for Adafruit Industries.
#   MIT license, all text above must be included in any redistribution
#
# ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^End^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
#
#
# Selected functions from the Adafruit_GFX library (as it was in 2015) have
# been imported into the TFT_eSPI.cpp file and modified to improve
# performance, add features and make them compatible with the ESP8266 and
# ESP32.
#
# The fonts from the Adafruit_GFX and Button functions were added later.
# The fonts can be found with the license.txt file in the "Fonts\GFXFF"
# folder.
#
# The Adafruit_GFX functions are covered by the BSD licence.
#
# Adafruit_GFX ORIGINAL LIBRARY LICENSE:
#
# vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvStartvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
#
# Software License Agreement (BSD License)
#
# Copyright (c) 2012 Adafruit Industries.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# - Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^End^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
#
# Due to the evolution of the TFT_eSPI library the original code may no longer
# be recognisable, however in most cases the function names can be used as a
# reference point since the aim is to retain a level of compatibility with
# the popular Adafruit_GFX graphics functions.
#
# Contributions from other authors are recorded on GitHub:
# https://github.com/Bodmer/TFT_eSPI
#
# The major addition to the original library was the addition of fast
# rendering proportional fonts of different sizes as documented here:
#
# https://www.instructables.com/id/Arduino-TFT-display-and-font-library/
#
# The larger fonts are "Run Length Encoded (RLE)", this was done to
# reduce the font memory footprint for AVR processors that have limited
# FLASH, with the added benefit of a significant improvement in rendering
# speed.
#
# In 2016 the library evolved significantly to support the ESP8266 and then
# the ESP32. In 2017 new Touch Screen functions were added and a new Sprite
# class called TFT_eSprite to permit "flicker free" screen updates of complex
# graphics.
#
# In 2018 anti-aliased fonts were added along with a Processing font conversion
# sketch.
#
# In 2019 the library was adapted to be able to use it with any 32-bit Arduino
# compatible processor. It will run on 8-bit and 16-bit processors but will be
# slow due to extensive use of 32-bit variables.
#
# Many of the example sketches are original work that contain code created
# for my own projects. For all the original code the FreeBSD licence applies
# and is compatible with the GNU GPL.
#
# vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvStartvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
# Software License Agreement (FreeBSD License)
#
# Copyright (c) 2023 Bodmer (https://github.com/Bodmer)
#
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# The views and conclusions contained in the software and documentation are those
# of the authors and should not be interpreted as representing official policies,
# either expressed or implied, of the FreeBSD Project.
# ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^End^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

# ----------------------------------------------------------------------------- canvas + TFT_eSPI ports
def cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


class Canvas:
    """RGB565 framebuffer that counts SPI-visible work: ops (each = one address window) and pixels."""

    def __init__(self, w, h, bg=0):
        self.w, self.h = w, h
        self.px = [bg] * (w * h)
        self.ops = 0
        self.pixels = 0
        self.vp = None

    def reset_stats(self):
        self.ops = 0
        self.pixels = 0

    def stats(self):
        return self.ops, self.pixels

    def wire_bytes(self):
        # CASET(1+4) + RASET(1+4) + RAMWR(1) per window, 2 bytes per RGB565 pixel.
        return self.ops * 11 + self.pixels * 2

    def _put(self, x, y, c):
        if self.vp:
            vx, vy, vw, vh = self.vp
            if x < vx or y < vy or x >= vx + vw or y >= vy + vh:
                return 0
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y * self.w + x] = c
            return 1
        return 0

    def pixel(self, x, y, c):
        if self._put(x, y, c):
            self.ops += 1
            self.pixels += 1

    def hline(self, x, y, w, c):
        if w <= 0:
            return
        n = sum(self._put(x + i, y, c) for i in range(w))
        if n:
            self.ops += 1
            self.pixels += n

    def vline(self, x, y, h, c):
        if h <= 0:
            return
        n = sum(self._put(x, y + i, c) for i in range(h))
        if n:
            self.ops += 1
            self.pixels += n

    def fill_rect(self, x, y, w, h, c):
        if w <= 0 or h <= 0:
            return
        n = 0
        for j in range(h):
            for i in range(w):
                n += self._put(x + i, y + j, c)
        if n:
            self.ops += 1
            self.pixels += n

    def draw_rect(self, x, y, w, h, c):
        self.hline(x, y, w, c)
        self.hline(x, y + h - 1, w, c)
        self.vline(x, y, h, c)
        self.vline(x + w - 1, y, h, c)

    def draw_line(self, x0, y0, x1, y1, c):
        steep = abs(y1 - y0) > abs(x1 - x0)
        if steep:
            x0, y0, x1, y1 = y0, x0, y1, x1
        if x0 > x1:
            x0, x1, y0, y1 = x1, x0, y1, y0
        dx, dy = x1 - x0, abs(y1 - y0)
        err, ystep, xs, dlen = dx >> 1, (1 if y0 < y1 else -1), x0, 0
        while x0 <= x1:
            dlen += 1
            err -= dy
            if err < 0:
                if steep:
                    self.pixel(y0, xs, c) if dlen == 1 else self.vline(y0, xs, dlen, c)
                else:
                    self.pixel(xs, y0, c) if dlen == 1 else self.hline(xs, y0, dlen, c)
                dlen = 0
                y0 += ystep
                xs = x0 + 1
                err += dx
            x0 += 1
        if dlen:
            self.vline(y0, xs, dlen, c) if steep else self.hline(xs, y0, dlen, c)

    def fill_triangle(self, x0, y0, x1, y1, x2, y2, c):
        if y0 > y1:
            y0, y1, x0, x1 = y1, y0, x1, x0
        if y1 > y2:
            y2, y1, x2, x1 = y1, y2, x1, x2
        if y0 > y1:
            y0, y1, x0, x1 = y1, y0, x1, x0
        if y0 == y2:
            a = b = x0
            a, b = min(a, x1, x2), max(b, x1, x2)
            self.hline(a, y0, b - a + 1, c)
            return
        dx01, dy01, dx02, dy02, dx12, dy12 = x1 - x0, y1 - y0, x2 - x0, y2 - y0, x2 - x1, y2 - y1
        sa = sb = 0
        last = y1 if y1 == y2 else y1 - 1
        y = y0
        while y <= last:
            a, b = x0 + cdiv(sa, dy01), x0 + cdiv(sb, dy02)
            sa += dx01
            sb += dx02
            if a > b:
                a, b = b, a
            self.hline(a, y, b - a + 1, c)
            y += 1
        sa, sb = dx12 * (y - y1), dx02 * (y - y0)
        while y <= y2:
            a, b = x1 + cdiv(sa, dy12), x0 + cdiv(sb, dy02)
            sa += dx12
            sb += dx02
            if a > b:
                a, b = b, a
            self.hline(a, y, b - a + 1, c)
            y += 1

    def fill_circle(self, x0, y0, r, c):
        x, dx, dy, p = 0, 1, r + r, -(r >> 1)
        self.hline(x0 - r, y0, dy + 1, c)
        while x < r:
            if p >= 0:
                self.hline(x0 - x, y0 + r, dx, c)
                self.hline(x0 - x, y0 - r, dx, c)
                dy -= 2
                p -= dy
                r -= 1
            dx += 2
            p += dx
            x += 1
            self.hline(x0 - r, y0 + x, dy + 1, c)
            self.hline(x0 - r, y0 - x, dy + 1, c)

    def fill_circle_helper(self, x0, y0, r, corner, delta, c):
        f, ddx, ddy, y = 1 - r, 1, -r - r, 0
        delta += 1
        while y < r:
            if f >= 0:
                if corner & 1:
                    self.hline(x0 - y, y0 + r, y + y + delta, c)
                if corner & 2:
                    self.hline(x0 - y, y0 - r, y + y + delta, c)
                r -= 1
                ddy += 2
                f += ddy
            y += 1
            ddx += 2
            f += ddx
            if corner & 1:
                self.hline(x0 - r, y0 + y, r + r + delta, c)
            if corner & 2:
                self.hline(x0 - r, y0 - y, r + r + delta, c)

    def fill_round_rect(self, x, y, w, h, r, c):
        self.fill_rect(x, y + r, w, h - r - r, c)
        self.fill_circle_helper(x + r, y + h - r - 1, r, 1, w - r - r - 1, c)
        self.fill_circle_helper(x + r, y + r, r, 2, w - r - r - 1, c)

    def draw_circle_helper(self, x0, y0, rr, corner, c):
        if rr <= 0:
            return
        f, ddx, ddy, xe, xs = 1 - rr, 1, -2 * rr, 0, 0
        while True:
            while f < 0:
                xe += 1
                ddx += 2
                f += ddx
            ddy += 2
            f += ddy
            if xe - xs == 1:
                if corner & 1:
                    self.pixel(x0 - xe, y0 - rr, c); self.pixel(x0 - rr, y0 - xe, c)
                if corner & 2:
                    self.pixel(x0 + rr, y0 - xe, c); self.pixel(x0 + xs + 1, y0 - rr, c)
                if corner & 4:
                    self.pixel(x0 + xs + 1, y0 + rr, c); self.pixel(x0 + rr, y0 + xs + 1, c)
                if corner & 8:
                    self.pixel(x0 - rr, y0 + xs + 1, c); self.pixel(x0 - xe, y0 + rr, c)
            else:
                ln = xe - xs
                xs += 1
                if corner & 1:
                    self.hline(x0 - xe, y0 - rr, ln, c); self.vline(x0 - rr, y0 - xe, ln, c)
                if corner & 2:
                    self.vline(x0 + rr, y0 - xe, ln, c); self.hline(x0 + xs, y0 - rr, ln, c)
                if corner & 4:
                    self.hline(x0 + xs, y0 + rr, ln, c); self.vline(x0 + rr, y0 + xs, ln, c)
                if corner & 8:
                    self.vline(x0 - rr, y0 + xs, ln, c); self.hline(x0 - xe, y0 + rr, ln, c)
            xs = xe
            keep = xe < rr
            rr -= 1
            if not keep:
                break

    def draw_round_rect(self, x, y, w, h, r, c):
        self.hline(x + r, y, w - r - r, c)
        self.hline(x + r, y + h - 1, w - r - r, c)
        self.vline(x, y + r, h - r - r, c)
        self.vline(x + w - 1, y + r, h - r - r, c)
        self.draw_circle_helper(x + r, y + r, r, 1, c)
        self.draw_circle_helper(x + w - r - 1, y + r, r, 2, c)
        self.draw_circle_helper(x + w - r - 1, y + h - r - 1, r, 4, c)
        self.draw_circle_helper(x + r, y + h - r - 1, r, 8, c)

    # --- bitmap paths: ONE address window per icon (setAddrWindow + a row burst loop, or pushImage)
    def blit_1bit(self, bm, x, y, fg, bg):
        w, h, rows = bm["w"], bm["h"], bm["bits"]
        for j in range(h):
            for i in range(w):
                self._put(x + i, y + j, fg if rows[j][i] else bg)
        self.ops += 1
        self.pixels += w * h

    def blit_4bit(self, bm, x, y, fg, bg):
        w, h, rows = bm["w"], bm["h"], bm["a4"]
        for j in range(h):
            for i in range(w):
                self._put(x + i, y + j, blend565(fg, bg, rows[j][i]))
        self.ops += 1
        self.pixels += w * h

    def write_png(self, path, scale=1):
        rows = []
        for y in range(self.h):
            line = bytearray()
            for x in range(self.w):
                line += bytes(rgb888(self.px[y * self.w + x])) * scale
            for _ in range(scale):
                rows.append(line)
        Path(path).write_bytes(png_bytes(self.w * scale, self.h * scale, rows))



# ----------------------------------------------------------------------------- palettes
class Pal:
    def __init__(self, name, **kw):
        self.name = name
        self.__dict__.update(kw)


# Light = theme.cpp PALETTE verbatim (field order bg band raised | text dim disabled | outline subtle |
# btn btnText btnActive onControlActive pressed | accent onAccent | barBg bar).
LIGHT = Pal("light", label="Classic Touch light (shipped default)", bg=0xFFFF, band=0xD6BA, raised=0xF79E,
            text=0x1082, dim=0x5ACB, disabled=0x9CD3, outline=0x8C71, subtle=0xE71C,
            btn=0xE71C, btnText=0x1082, active=0x1B59, onActive=0xFFFF, pressed=0x3C5C,
            accent=0x1B59, onAccent=0xFFFF, barBg=0xD6DB, bar=0x3C5B)
# Proposals for the planned themes (CY-76 proposal G); not shipped, colours are illustrative.
BLACK = Pal("black", label="iPod Black (proposal)", bg=c565(0, 0, 0), band=c565(34, 34, 36), raised=c565(52, 52, 56),
            text=c565(255, 255, 255), dim=c565(172, 174, 180), disabled=c565(96, 96, 100),
            outline=c565(110, 110, 116), subtle=c565(48, 48, 52),
            btn=c565(52, 52, 56), btnText=c565(255, 255, 255), active=0x1B59, onActive=0xFFFF, pressed=0x3C5C,
            accent=c565(64, 148, 245), onAccent=0xFFFF, barBg=c565(70, 70, 76), bar=c565(64, 148, 245))
HC = Pal("hc", label="High contrast (proposal)", bg=0x0000, band=0x0000, raised=0x0000,
         text=0xFFFF, dim=c565(200, 200, 200), disabled=c565(120, 120, 120),
         outline=0xFFFF, subtle=c565(90, 90, 90),
         btn=0x0000, btnText=0xFFFF, active=c565(255, 224, 0), onActive=0x0000, pressed=0xFFFF,
         accent=c565(255, 224, 0), onAccent=0x0000, barBg=c565(90, 90, 90), bar=c565(255, 224, 0))
PALETTES = [LIGHT, BLACK, HC]


def ink_of(pal, state):
    return pal.onActive if state in ("active", "pressed") else pal.btnText


def face_of(pal, state):
    return pal.pressed if state == "pressed" else pal.active if state == "active" else pal.raised


# ----------------------------------------------------------------------------- "current v23" ui_icon, ported verbatim
GLCD_1 = [0x00, 0x42, 0x7F, 0x40, 0x00]  # font 1 (5x7) digit '1', column-major, bit0 = top


def lroundf(v):
    return int(math.floor(abs(v) + 0.5)) * (1 if v >= 0 else -1)


def cur_speaker(cv, cx, cy, s, fg):
    bx, bh = cx - s // 2, s // 3
    cv.fill_rect(bx, cy - bh // 2, s // 5, bh, fg)
    cv.fill_triangle(bx + s // 5 - 1, cy - bh // 2, bx + s // 2, cy - s // 2, bx + s // 5 - 1, cy + bh // 2, fg)
    cv.fill_triangle(bx + s // 5 - 1, cy + bh // 2, bx + s // 2, cy - s // 2, bx + s // 2, cy + s // 2, fg)


def star_points(cx, cy, h, ratio=0.42):
    ox, oy, ix, iy = [], [], [], []
    for i in range(5):
        a = -math.pi / 2 + i * 2 * math.pi / 5
        b = a + math.pi / 5
        ox.append(cx + lroundf(math.cos(a) * h))
        oy.append(cy + lroundf(math.sin(a) * h) + h // 8)
        ix.append(cx + lroundf(math.cos(b) * h * ratio))
        iy.append(cy + lroundf(math.sin(b) * h * ratio) + h // 8)
    return ox, oy, ix, iy


def cur_star(cv, cx, cy, h, fg):
    ox, oy, ix, iy = star_points(cx, cy, h)
    for i in range(5):
        j = (i + 4) % 5
        cv.fill_triangle(ox[i], oy[i], ix[i], iy[i], ix[j], iy[j], fg)
        cv.fill_triangle(cx, cy + h // 8, ix[i], iy[i], ix[j], iy[j], fg)


def font1_digit_one(cv, cx, cy, fg, bg):
    # setTextDatum(MC_DATUM) drawString("1", cx, cy+1, 1): opaque 6x8 cell, one window.
    x0, y0 = cx - 3, cy + 1 - 4
    for col in range(6):
        for row in range(8):
            on = col < 5 and row < 7 and (GLCD_1[col] >> row) & 1
            cv._put(x0 + col, y0 + row, fg if on else bg)
    cv.ops += 1
    cv.pixels += 48


def cur_icon(cv, name, cx, cy, s, fg, bg):
    """ui_icon() from firmware/cydpod_player/app/ui.cpp at 10416be, line for line."""
    h = s // 2
    if name == "play":
        cv.fill_triangle(cx - h * 2 // 3, cy - h, cx - h * 2 // 3, cy + h, cx + h, cy, fg)
    elif name == "pause":
        cv.fill_rect(cx - h * 3 // 4, cy - h, h // 2 + 1, s, fg)
        cv.fill_rect(cx + h // 4, cy - h, h // 2 + 1, s, fg)
    elif name == "next":
        cv.fill_triangle(cx - h, cy - h, cx - h, cy + h, cx + h // 2, cy, fg)
        cv.fill_rect(cx + h - 2, cy - h, max(3, s // 7), s, fg)
    elif name == "prev":
        cv.fill_triangle(cx + h, cy - h, cx + h, cy + h, cx - h // 2, cy, fg)
        cv.fill_rect(cx - h - max(3, s // 7) + 3, cy - h, max(3, s // 7), s, fg)
    elif name == "gear":
        r = h * 3 // 4
        for i in range(8):
            a = i * math.pi / 4
            tx, ty = int(cx + math.cos(a) * r), int(cy + math.sin(a) * r)
            cv.fill_circle(tx, ty, max(2, s // 9), fg)
        cv.fill_circle(cx, cy, r, fg)
        cv.fill_circle(cx, cy, r // 2, bg)
    elif name == "speaker":
        cur_speaker(cv, cx - s // 6, cy, s, fg)
        wx = cx + s // 4
        cv.draw_line(wx, cy - s // 6, wx, cy + s // 6, fg)
        cv.draw_line(wx + 3, cy - s // 3, wx + 3, cy + s // 3, fg)
    elif name == "headphones":
        rr, ay = h * 3 // 4 + 1, cy + h // 6
        px, py = cx - rr, ay
        for i in range(1, 13):
            a = math.pi + i * math.pi / 12
            x, y = cx + int(math.cos(a) * rr), ay + int(math.sin(a) * rr)
            cv.draw_line(px, py, x, y, fg)
            cv.draw_line(px, py + 1, x, y + 1, fg)
            px, py = x, y
        cw, ch = max(4, s // 5), max(5, h * 2 // 3)
        cv.fill_round_rect(cx - rr - cw // 2, ay - 1, cw, ch, 2, fg)
        cv.fill_round_rect(cx + rr - cw // 2, ay - 1, cw, ch, 2, fg)
    elif name == "bluetooth":
        t, b, x0, x1 = cy - h, cy + h, cx - h // 2, cx + h // 2
        cv.draw_line(cx, t, cx, b, fg)
        cv.draw_line(cx, t, x1, cy - h // 2, fg)
        cv.draw_line(x1, cy - h // 2, x0, cy + h // 2, fg)
        cv.draw_line(cx, b, x1, cy + h // 2, fg)
        cv.draw_line(x1, cy + h // 2, x0, cy - h // 2, fg)
    elif name == "back":
        cv.fill_triangle(cx - h, cy, cx, cy - h, cx, cy + h, fg)
        cv.fill_rect(cx, cy - h // 3, h, h * 2 // 3, fg)
    elif name == "note":
        cv.fill_circle(cx - h // 2, cy + h * 2 // 3, h // 3, fg)
        cv.fill_circle(cx + h * 2 // 3, cy + h // 2, h // 3, fg)
        cv.fill_rect(cx - h // 2 + h // 3 - 2, cy - h, 3, h * 5 // 3, fg)
        cv.fill_rect(cx + h * 2 // 3 + h // 3 - 2, cy - h * 7 // 6, 3, h * 5 // 3, fg)
        cv.fill_triangle(cx - h // 2 + h // 3 - 2, cy - h, cx + h, cy - h * 7 // 6 - 1, cx + h, cy - h * 2 // 3, fg)
        cv.fill_triangle(cx - h // 2 + h // 3 - 2, cy - h, cx - h // 2 + h // 3 - 2, cy - h // 2, cx + h, cy - h * 2 // 3, fg)
    elif name == "shuffle":
        l, r, t, b = cx - h, cx + h - 3, cy - h // 2, cy + h // 2
        cv.draw_line(l, t, r, b, fg); cv.draw_line(l, t + 1, r, b + 1, fg)
        cv.draw_line(l, b, r, t, fg); cv.draw_line(l, b + 1, r, t + 1, fg)
        cv.fill_triangle(r + 3, t, r - 2, t - 4, r - 2, t + 5, fg)
        cv.fill_triangle(r + 3, b + 1, r - 2, b - 4, r - 2, b + 5, fg)
    elif name in ("repeat", "repeat1"):
        w, hh = s, s * 2 // 3
        cv.draw_round_rect(cx - w // 2, cy - hh // 2, w, hh, hh // 3, fg)
        cv.draw_round_rect(cx - w // 2 + 1, cy - hh // 2 + 1, w - 2, hh - 2, hh // 3, fg)
        cv.fill_rect(cx - 2, cy - hh // 2 - 1, 5, 4, bg)
        cv.fill_triangle(cx + 4, cy - hh // 2 + 1, cx - 2, cy - hh // 2 - 4, cx - 2, cy - hh // 2 + 6, fg)
        if name == "repeat1":
            font1_digit_one(cv, cx, cy, fg, bg)
    elif name == "up":
        cv.fill_triangle(cx, cy - h // 2, cx - h, cy + h // 2, cx + h, cy + h // 2, fg)
    elif name == "down":
        cv.fill_triangle(cx, cy + h // 2, cx - h, cy - h // 2, cx + h, cy - h // 2, fg)
    elif name == "chevron":
        cv.fill_triangle(cx - h // 3, cy - h, cx + h // 2, cy, cx - h // 3, cy + h, fg)
        cv.fill_triangle(cx - h // 3 - 3, cy - h, cx + h // 2 - 3, cy, cx - h // 3 - 3, cy + h, bg)
    elif name in ("star", "star_off"):
        # Current set has one star; "star_off" is the same glyph (state is the dim ink colour).
        cur_star(cv, cx, cy, h, fg)


# ----------------------------------------------------------------------------- Option A: classic primitives
def A_icon(cv, name, cx, cy, s, fg, bg):
    """Proposed ui_icon(): the same primitives (triangles, rects, circles, lines), 2 px minimum strokes,
    iPod-style double-triangle skip glyphs and solid triangle back/chevron. Every call is a firmware primitive."""
    h = s // 2
    if name == "play":
        cv.fill_triangle(cx - h * 2 // 3, cy - h, cx - h * 2 // 3, cy + h, cx + h, cy, fg)
    elif name == "pause":
        bw = max(3, (s * 2 + 8) // 9)
        gap = max(3, s // 5)
        ht = h * 9 // 10
        cv.fill_rect(cx - gap // 2 - bw, cy - ht, bw, 2 * ht + 1, fg)
        cv.fill_rect(cx + (gap + 1) // 2, cy - ht, bw, 2 * ht + 1, fg)
    elif name in ("next", "prev"):
        bw = max(2, s // 9)
        ht = h * 85 // 100
        tw = (2 * h - bw - 2) // 2
        if name == "next":
            x0 = cx - h
            for k in range(2):
                a = x0 + k * tw
                cv.fill_triangle(a, cy - ht, a, cy + ht, a + tw, cy, fg)
            cv.fill_rect(cx + h - bw + 1, cy - ht, bw, 2 * ht + 1, fg)
        else:
            x0 = cx + h
            for k in range(2):
                a = x0 - k * tw
                cv.fill_triangle(a, cy - ht, a, cy + ht, a - tw, cy, fg)
            cv.fill_rect(cx - h, cy - ht, bw, 2 * ht + 1, fg)
    elif name == "back":
        cv.fill_triangle(cx + h * 3 // 5, cy - h, cx + h * 3 // 5, cy + h, cx - h * 2 // 3, cy, fg)
    elif name == "chevron":
        ht = max(4, h * 4 // 5)
        cv.fill_triangle(cx - h // 2, cy - ht, cx - h // 2, cy + ht, cx + h // 2 + 1, cy, fg)
    elif name == "up":
        cv.fill_triangle(cx, cy - h // 2, cx - h, cy + h // 2, cx + h, cy + h // 2, fg)
    elif name == "down":
        cv.fill_triangle(cx, cy + h // 2, cx - h, cy - h // 2, cx + h, cy - h // 2, fg)
    elif name == "speaker":
        cur_speaker(cv, cx - s // 4, cy, s * 9 // 10, fg)
        for k, r in enumerate((s // 4, s * 7 // 16)):
            x = cx + s // 8
            cv.draw_circle_helper(x, cy, r, 2 | 4, fg)
            cv.draw_circle_helper(x + 1, cy, r, 2 | 4, fg) if k == 1 and s >= 24 else None
    elif name == "bluetooth":
        t, b, x0, x1 = cy - h, cy + h, cx - h // 2, cx + h // 2
        segs = [(cx, t, cx, b), (cx, t, x1, cy - h // 2), (x1, cy - h // 2, x0, cy + h // 2),
                (cx, b, x1, cy + h // 2), (x1, cy + h // 2, x0, cy - h // 2)]
        for (ax, ay, bx, by) in segs:
            cv.draw_line(ax, ay, bx, by, fg)
            cv.draw_line(ax + 1, ay, bx + 1, by, fg)
    elif name == "headphones":
        rr = h * 3 // 4 + 1
        ay = cy + h // 6
        t = max(2, s // 10)
        cv.fill_circle(cx, ay, rr, fg)
        cv.fill_circle(cx, ay, rr - t, bg)
        cv.fill_rect(cx - rr - 1, ay + 1, 2 * rr + 3, rr + 2, bg)
        cw, ch = max(4, s // 5), max(5, h * 2 // 3)
        cv.fill_round_rect(cx - rr - cw // 2, ay - 1, cw, ch, 2, fg)
        cv.fill_round_rect(cx + rr - cw // 2, ay - 1, cw, ch, 2, fg)
    elif name == "star":
        cur_star(cv, cx, cy, h, fg)
    elif name == "star_off":
        cur_star(cv, cx, cy, h, fg)
        cur_star(cv, cx, cy + max(1, h // 9), max(2, h * 11 // 20), bg)
    elif name == "repeat1":
        cur_icon(cv, "repeat", cx, cy, s, fg, bg)
        one = max(2, s // 12)
        cv.fill_rect(cx - one // 2, cy - s // 8, one, s // 4 + 1, fg)
        cv.fill_triangle(cx - one // 2, cy - s // 8, cx - one // 2, cy - s // 8 + one + 1, cx - one - 2, cy - s // 8 + one + 1, fg)
    else:  # gear, note, shuffle, repeat: kept from v23 (acceptable shapes; see report)
        cur_icon(cv, name, cx, cy, s, fg, bg)


# ----------------------------------------------------------------------------- vector shapes for B / C bitmaps
class Shape:
    sub = False


class Disc(Shape):
    def __init__(s, cx, cy, r, sub=False):
        s.cx, s.cy, s.r, s.sub = cx, cy, r, sub
        s.box = (cx - r, cy - r, cx + r, cy + r)

    def has(s, x, y):
        return (x - s.cx) ** 2 + (y - s.cy) ** 2 <= s.r * s.r


class Ring(Shape):
    def __init__(s, cx, cy, r, w, sub=False):
        s.cx, s.cy, s.r, s.w, s.sub = cx, cy, r, w, sub
        s.box = (cx - r - w, cy - r - w, cx + r + w, cy + r + w)

    def has(s, x, y):
        return abs(math.hypot(x - s.cx, y - s.cy) - s.r) <= s.w / 2


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    t = 0.0 if l2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


class Stroke(Shape):
    def __init__(s, pts, w, closed=False, sub=False):
        s.pts = list(pts) + ([pts[0]] if closed else [])
        s.w, s.sub = w, sub
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        s.box = (min(xs) - w, min(ys) - w, max(xs) + w, max(ys) + w)

    def has(s, x, y):
        r = s.w / 2
        p = s.pts
        for i in range(len(p) - 1):
            if seg_dist(x, y, p[i][0], p[i][1], p[i + 1][0], p[i + 1][1]) <= r:
                return True
        return False


def inside_poly(pts, x, y):
    c = False
    n = len(pts)
    j = n - 1
    for i in range(n):
        xi, yi, xj, yj = pts[i][0], pts[i][1], pts[j][0], pts[j][1]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            c = not c
        j = i
    return c


class Fill(Shape):
    def __init__(s, pts, sub=False):
        s.pts, s.sub = list(pts), sub
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        s.box = (min(xs), min(ys), max(xs), max(ys))

    def has(s, x, y):
        return inside_poly(s.pts, x, y)


def inset_convex(pts, d):
    n = len(pts)
    area = sum(pts[i][0] * pts[(i + 1) % n][1] - pts[(i + 1) % n][0] * pts[i][1] for i in range(n))
    sign = 1 if area > 0 else -1
    lines = []
    for i in range(n):
        ax, ay = pts[i]
        bx, by = pts[(i + 1) % n]
        dx, dy = bx - ax, by - ay
        ln = math.hypot(dx, dy)
        nx, ny = -dy / ln * sign, dx / ln * sign  # inward normal
        lines.append((ax + nx * d, ay + ny * d, dx, dy))
    out = []
    for i in range(n):
        x1, y1, dx1, dy1 = lines[i - 1]
        x2, y2, dx2, dy2 = lines[i]
        den = dx1 * dy2 - dy1 * dx2
        t = ((x2 - x1) * dy2 - (y2 - y1) * dx2) / den
        out.append((x1 + dx1 * t, y1 + dy1 * t))
    return out


class RoundFill(Shape):
    """Convex polygon with corner radius r: inset by r, then everything within r of that."""

    def __init__(s, pts, r, sub=False):
        s.inner = inset_convex(pts, r) if r > 0 else list(pts)
        s.r, s.sub = r, sub
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        s.box = (min(xs), min(ys), max(xs), max(ys))

    def has(s, x, y):
        if inside_poly(s.inner, x, y):
            return True
        p, n = s.inner, len(s.inner)
        return any(seg_dist(x, y, p[i][0], p[i][1], p[(i + 1) % n][0], p[(i + 1) % n][1]) <= s.r for i in range(n))


class RRect(Shape):
    def __init__(s, x, y, w, h, r, sub=False):
        s.x, s.y, s.w, s.h, s.r, s.sub = x, y, w, h, min(r, w / 2, h / 2), sub
        s.box = (x, y, x + w, y + h)

    def has(s, px, py):
        qx = abs(px - (s.x + s.w / 2)) - (s.w / 2 - s.r)
        qy = abs(py - (s.y + s.h / 2)) - (s.h / 2 - s.r)
        return math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) <= s.r


class RRectRing(Shape):
    def __init__(s, x, y, w, h, r, t, sub=False):
        s.x, s.y, s.w, s.h, s.r, s.t, s.sub = x, y, w, h, r, t, sub
        s.box = (x - t, y - t, x + w + t, y + h + t)

    def has(s, px, py):
        qx = abs(px - (s.x + s.w / 2)) - (s.w / 2 - s.r)
        qy = abs(py - (s.y + s.h / 2)) - (s.h / 2 - s.r)
        sd = math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - s.r
        return abs(sd) <= s.t / 2


def box_cut(x0, y0, x1, y1):
    return RRect(x0, y0, x1 - x0, y1 - y0, 0, sub=True)


def arc_pts(cx, cy, r, a0, a1, n=14):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def star_poly(cx, cy, R, ratio):
    pts = []
    for i in range(10):
        a = -math.pi / 2 + i * math.pi / 5
        r = R if i % 2 == 0 else R * ratio
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def vec(name, s, st):
    """Shape list for icon `name` in an s x s field; st is 'B' (round, bold) or 'C' (thin stroke)."""
    B = st == "B"
    sw = max(2.0, 0.095 * s) if B else min(2.1, max(1.5, 0.06 * s))
    rr = 0.085 * s if B else 0.05 * s
    F = lambda *v: tuple(x * s for x in v)
    cx = cy = 0.5 * s
    sh = []

    def tri_filled_or_outline(pts, rad, outline):
        if outline:
            return [Stroke(inset_convex(pts, sw / 2), sw, closed=True)]
        return [RoundFill(pts, rad)]

    if name in ("play",):
        W, H = 0.66 * s, 0.78 * s
        x0 = cx - W * 0.42
        pts = [(x0, cy - H / 2), (x0, cy + H / 2), (x0 + W, cy)]
        sh = tri_filled_or_outline(pts, rr, (not B) and s >= 16)
    elif name == "pause":
        if B:
            sh = [RRect(cx - 0.27 * s, 0.12 * s, 0.20 * s, 0.76 * s, 0.07 * s),
                  RRect(cx + 0.07 * s, 0.12 * s, 0.20 * s, 0.76 * s, 0.07 * s)]
        else:
            w = max(2.4, 0.115 * s)
            sh = [Stroke([F(0.35, 0.18), F(0.35, 0.82)], w), Stroke([F(0.65, 0.18), F(0.65, 0.82)], w)]
    elif name in ("next", "prev"):
        pts = [F(0.12, 0.15), F(0.12, 0.85), F(0.66, 0.5)]
        bw = max(2.4, 0.13 * s) if B else sw * 1.2
        if B:
            sh = [RoundFill(pts, rr), RRect(0.74 * s, 0.15 * s, bw, 0.70 * s, bw * 0.45)]
        else:
            sh = tri_filled_or_outline(pts, rr, s >= 16) + [Stroke([F(0.80, 0.17), F(0.80, 0.83)], bw)]
        if name == "prev":
            out = []
            for o in sh:
                if isinstance(o, RoundFill):
                    out.append(RoundFill([(s - x, y) for x, y in pts], rr))
                elif isinstance(o, RRect):
                    out.append(RRect(s - o.x - o.w, o.y, o.w, o.h, o.r))
                elif isinstance(o, Stroke):
                    mp = [(s - x, y) for x, y in o.pts[:len(o.pts)]]
                    closed = len(o.pts) > 2 and o.pts[0] == o.pts[-1]
                    out.append(Stroke(mp[:-1] if closed else mp, o.w, closed=closed))
            sh = out
    elif name == "back":
        sh = [Stroke([F(0.64, 0.12), F(0.30, 0.5), F(0.64, 0.88)], sw * (1.15 if B else 1.0))]
    elif name == "chevron":
        sh = [Stroke([F(0.34, 0.06), F(0.72, 0.5), F(0.34, 0.94)], sw)]
    elif name in ("up", "down"):
        if B:
            pts = [F(0.10, 0.72), F(0.90, 0.72), F(0.5, 0.26)]
            if name == "down":
                pts = [(x, s - y) for x, y in pts]
            sh = [RoundFill(pts, rr * 0.8)]
        else:
            pts = [F(0.14, 0.68), F(0.5, 0.32), F(0.86, 0.68)]
            if name == "down":
                pts = [(x, s - y) for x, y in pts]
            sh = [Stroke(pts, sw * 1.1)]
    elif name == "speaker":
        if B:
            sh = [RRect(0.06 * s, 0.36 * s, 0.22 * s, 0.28 * s, 0.04 * s),
                  RoundFill([F(0.24, 0.38), F(0.50, 0.14), F(0.50, 0.86), F(0.24, 0.62)], 0.04 * s)]
        else:
            sh = [Stroke([F(0.08, 0.37), F(0.26, 0.37), F(0.48, 0.15), F(0.48, 0.85), F(0.26, 0.63), F(0.08, 0.63)], sw, closed=True)]
        for r in (0.24, 0.40):
            sh.append(Stroke(arc_pts(0.54 * s, cy, r * s, -42, 42), sw))
    elif name == "bluetooth":
        sh = [Stroke([F(0.22, 0.30), F(0.78, 0.70), F(0.50, 0.94), F(0.50, 0.06), F(0.78, 0.30), F(0.22, 0.70)],
                     sw * (1.1 if B else 1.0))]
    elif name == "headphones":
        r = 0.36 * s
        acy = 0.56 * s
        sh = [Stroke(arc_pts(cx, acy, r, 180, 360, 20), sw * (1.2 if B else 1.0)),
              Stroke([(cx - r, acy), (cx - r, 0.80 * s)], 0.24 * s if B else 0.18 * s),
              Stroke([(cx + r, acy), (cx + r, 0.80 * s)], 0.24 * s if B else 0.18 * s)]
        sh[1] = Stroke([(cx - r, 0.62 * s), (cx - r, 0.82 * s)], 0.24 * s if B else 0.18 * s)
        sh[2] = Stroke([(cx + r, 0.62 * s), (cx + r, 0.82 * s)], 0.24 * s if B else 0.18 * s)
        sh.append(Stroke([(cx - r, acy), (cx - r, 0.64 * s)], sw))
        sh.append(Stroke([(cx + r, acy), (cx + r, 0.64 * s)], sw))
    elif name == "note":
        if B:
            sh = [Disc(0.27 * s, 0.76 * s, 0.15 * s), Disc(0.73 * s, 0.68 * s, 0.15 * s),
                  Stroke([F(0.40, 0.74), F(0.40, 0.16)], sw), Stroke([F(0.86, 0.66), F(0.86, 0.08)], sw),
                  Fill([F(0.40, 0.14), F(0.86, 0.06), F(0.86, 0.27), F(0.40, 0.35)])]
        else:
            hr = 0.125 * s
            sh = [Ring(0.27 * s, 0.76 * s, hr, sw), Ring(0.73 * s, 0.68 * s, hr, sw),
                  Stroke([(0.27 * s + hr, 0.74 * s), F(0.40 + 0.0, 0.14)][:1] + [F(0.395, 0.16)], sw),
                  Stroke([(0.73 * s + hr, 0.66 * s), F(0.855, 0.08)][:1] + [F(0.855, 0.08)], sw),
                  Stroke([F(0.395, 0.16), F(0.855, 0.08)], sw * 1.7)]
    elif name in ("star", "star_off"):
        outline = name == "star_off"
        R = 0.5 * s
        cyy = 0.54 * s
        if B:
            rho = 0.05 * s
            pts = star_poly(cx, cyy, R - rho * 1.4, 0.48)
            sh = [Stroke(pts, sw, closed=True)] if outline else [Fill(pts), Stroke(pts, 2 * rho, closed=True)]
            if outline:
                pts = star_poly(cx, cyy, R - sw * 0.8, 0.50)
                sh = [Stroke(pts, sw, closed=True)]
        else:
            if outline:
                pts = star_poly(cx, cyy, R - sw * 0.8, 0.50)
                sh = [Stroke(pts, sw, closed=True)]
            else:
                rho = 0.03 * s
                pts = star_poly(cx, cyy, R - rho * 1.4, 0.48)
                sh = [Fill(pts), Stroke(pts, 2 * rho, closed=True)]
    elif name == "gear":
        for k in range(8):
            a = k * math.pi / 4
            r1, r2 = (0.27 if B else 0.30) * s, 0.46 * s
            sh.append(Stroke([(cx + r1 * math.cos(a), cy + r1 * math.sin(a)), (cx + r2 * math.cos(a), cy + r2 * math.sin(a))],
                             0.16 * s if B else sw * 1.25))
        if B:
            sh.append(Disc(cx, cy, 0.32 * s))
            sh.append(Disc(cx, cy, 0.13 * s, sub=True))
        else:
            sh.append(Ring(cx, cy, 0.27 * s, sw))
            sh.append(Disc(cx, cy, 0.07 * s))
    elif name == "shuffle":
        p1 = [F(0.06, 0.30), F(0.28, 0.30), F(0.62, 0.70), F(0.76, 0.70)]
        p2 = [F(0.06, 0.70), F(0.28, 0.70), F(0.62, 0.30), F(0.76, 0.30)]
        sh = [Stroke(p1, sw), Stroke(p2, sw)]
        for y in (0.70, 0.30):
            if B:
                sh.append(RoundFill([F(0.72, y - 0.17), F(0.72, y + 0.17), F(0.95, y)], rr * 0.6))
            else:
                sh.append(Stroke([F(0.80, y - 0.15), F(0.94, y), F(0.80, y + 0.15)], sw))
    elif name in ("repeat", "repeat1"):
        x0, y0, w, h = 0.07 * s, 0.22 * s, 0.86 * s, 0.56 * s
        sh = [RRectRing(x0, y0, w, h, 0.18 * s, sw), box_cut(0.60 * s, 0.10 * s, 0.78 * s, 0.38 * s),
              box_cut(0.22 * s, 0.62 * s, 0.40 * s, 0.90 * s)]
        if B:
            # Separate the return paths so the arrowheads remain visible at 25 px.
            # A closed ring with overlapping rounded arrowheads reads as an empty box.
            sh = [Stroke([F(0.12, 0.55), F(0.12, 0.36), F(0.24, 0.24), F(0.72, 0.24)], sw),
                  Stroke([F(0.88, 0.45), F(0.88, 0.64), F(0.76, 0.76), F(0.28, 0.76)], sw),
                  Fill([F(0.68, 0.08), F(0.92, 0.24), F(0.68, 0.40)]),
                  Fill([F(0.32, 0.60), F(0.08, 0.76), F(0.32, 0.92)])]
        else:
            sh.append(Stroke([F(0.66, 0.22 - 0.14), F(0.82, 0.22), F(0.66, 0.22 + 0.14)], sw))
            sh.append(Stroke([F(0.34, 0.78 - 0.14), F(0.18, 0.78), F(0.34, 0.78 + 0.14)], sw))
        if name == "repeat1":
            sh.append(Stroke([F(0.51, 0.35), F(0.51, 0.65)], sw))
            sh.append(Stroke([F(0.51, 0.36), F(0.43, 0.43)], sw * 0.9))
    return sh


_RASTER_CACHE = {}
SS = 4
PAD = 2


def rasterise(name, s, st):
    """Supersample the shape list on a (s+2*PAD) square grid; crop to the tight box.
    Returns {w,h,ox,oy,bits,a4}: ox/oy are the crop's top-left relative to the field's top-left."""
    key = (name, s, st)
    if key in _RASTER_CACHE:
        return _RASTER_CACHE[key]
    shapes = vec(name, s, st)
    n = s + 2 * PAD
    cov = [[0] * n for _ in range(n)]
    for j in range(n):
        for i in range(n):
            x0, y0 = i - PAD, j - PAD
            act = [sh for sh in shapes if sh.box[0] <= x0 + 1 and sh.box[2] >= x0 and sh.box[1] <= y0 + 1 and sh.box[3] >= y0]
            if not act:
                continue
            cnt = 0
            for a in range(SS):
                px = x0 + (a + 0.5) / SS
                for b in range(SS):
                    py = y0 + (b + 0.5) / SS
                    inside = False
                    for sh in act:
                        if sh.has(px, py):
                            inside = not sh.sub
                    cnt += inside
            cov[j][i] = cnt
    total = SS * SS
    a4 = [[(c * 15 + total // 2) // total for c in row] for row in cov]
    bits = [[1 if c * 2 >= total else 0 for c in row] for row in cov]
    def tight(grid):
        ys = [j for j in range(n) if any(grid[j])]
        xs = [i for i in range(n) if any(grid[j][i] for j in range(n))]
        if not ys:
            return 0, 0, 0, 0
        return min(xs), min(ys), max(xs), max(ys)
    assert any(any(row) for row in bits), (name, s, st)
    mx0, my0, mx1, my1 = tight(a4)  # same crop for both so the two forms line up
    bm = {"w": mx1 - mx0 + 1, "h": my1 - my0 + 1, "ox": mx0 - PAD, "oy": my0 - PAD,
          "a4": [row[mx0:mx1 + 1] for row in a4[my0:my1 + 1]],
          "bits": [row[mx0:mx1 + 1] for row in bits[my0:my1 + 1]]}
    # The 1-bit form may be empty on its own edge rows; keep the shared crop (honest, simple).
    _RASTER_CACHE[key] = bm
    return bm


def B_icon(cv, name, cx, cy, s, fg, bg):
    bm = rasterise(name, s, "B")
    cv.blit_1bit(bm, cx - s // 2 + bm["ox"], cy - s // 2 + bm["oy"], fg, bg)


def C_icon(cv, name, cx, cy, s, fg, bg):
    bm = rasterise(name, s, "C")
    cv.blit_4bit(bm, cx - s // 2 + bm["ox"], cy - s // 2 + bm["oy"], fg, bg)



# ----------------------------------------------------------------------------- preview composition
NAMES = ['play', 'pause', 'prev', 'next', 'back', 'chevron', 'up', 'down', 'note',
         'gear', 'bluetooth', 'headphones', 'speaker', 'shuffle', 'repeat', 'repeat1', 'star', 'star_off']
LABELS = ['play', 'pause', 'prev', 'next', 'back', 'chevron', 'up', 'down', 'note',
          'gear', 'BT', 'phones', 'volume', 'shuffle', 'repeat', 'repeat1', 'star on', 'star off']
SIZES = [12, 14, 16, 18, 24, 25, 28, 32, 34]
STYLES = {'current': cur_icon, 'A': A_icon, 'B': B_icon, 'C': C_icon}
TITLES = {'current': 'Current v23 reference', 'A': 'A / Classic solid',
          'B': 'B / Rounded wheel', 'C': 'C / Quiet outline'}
FONTS = {}


def text(cv, value, x, y, color, face='label', align=0, maxw=None):
    font = FONTS[face]
    if maxw is not None and font.width(value) > maxw:
        if face == 'title':
            font = FONTS['compact']
        if font.width(value) > maxw:
            while value and font.width(value.rstrip() + '...') > maxw:
                value = value[:-1]
            value = value.rstrip() + '...' if font.width('...') <= maxw else ''
    font.draw(cv, value, x, y, align, color)


def control(cv, style, pal, r, name, state='normal'):
    before_ops, before_pixels = cv.stats()
    x, y, w, h = r
    face, ink = face_of(pal, state), ink_of(pal, state)
    cv.fill_rect(x, y, w, h, pal.bg)
    inset = 0 if style == 'current' else 2
    xx, yy, ww, hh = x + inset, y + inset, w - inset * 2, h - inset * 2
    radius = 6 if style == 'current' else 8 if style == 'A' else min(ww, hh) // 2
    if style == 'C':
        face = pal.pressed if state == 'pressed' else pal.bg
        ink = pal.onActive if state == 'pressed' else pal.accent if state == 'active' else pal.text
        edge = pal.accent if state == 'active' else pal.outline
    else:
        edge = pal.outline
    cv.fill_round_rect(xx, yy, ww, hh, radius, face)
    cv.draw_round_rect(xx, yy, ww, hh, radius, edge)
    if style == 'current' and state == 'normal':
        cv.hline(xx + radius // 2, yy, ww - radius, pal.outline)
    # playerButton expands the logical cell by STYLE_INSET*2 before calling ui_button.
    # Its glyph therefore uses min(w+6,h+6)*3/5, not min(w,h)*3/5.
    size = min(w + 6, h + 6) * 3 // 5
    STYLES[style](cv, name, x + w // 2, y + h // 2, size, ink, face)
    if style in ('B', 'C') and state == 'active':
        cv.hline(x + w // 2 - 3, y + h - 6, 6, ink)
    if style in ('B', 'C'):
        # Proposed row compositor: reproduce the face spans and glyph in a bounded
        # RGB565 row buffer, then stream the ENTIRE cell through one address window.
        # Python's framebuffer is an offline preview, not proposed firmware RAM.
        cv.ops = before_ops + 1
        cv.pixels = before_pixels + w * h


def transport(cv, style, pal, x=0, y=0, width=320, state='normal'):
    wide = width >= 300
    cells = [(0, 2, 52 if wide else 36, 48),
             (56 if wide else 40, 2, 70 if wide else 36, 48),
             (width // 2 - (30 if wide else 40), 0, 80, 52),
             (214 if wide else 164, 2, 70 if wide else 36, 48),
             (width - 36, 2, 36, 48)]
    for i, (r, name) in enumerate(zip(cells, ['shuffle', 'prev', 'play', 'next', 'repeat'])):
        st = state
        if state == 'active':
            st = 'active' if i in (0, 2, 4) else 'normal'
            if i == 2:
                name = 'pause'
        elif state == 'repeat1':
            st = 'active' if i == 4 else 'normal'
            if i == 4:
                name = 'repeat1'
        control(cv, style, pal, (x + r[0], y + r[1], r[2], r[3]), name, st)


def favourite(cv, style, pal, x, y, on=False, pressed=False):
    face = pal.pressed if pressed else pal.active if on else pal.band
    ink = pal.onActive if on or pressed else pal.dim
    cv.fill_rect(x, y, 40, 36, pal.band)
    if on or pressed:
        cv.fill_round_rect(x + 3, y + 3, 34, 30, 6 if style in ('A', 'current') else 15, face)
    STYLES[style](cv, 'star' if on or style == 'current' else 'star_off', x + 20, y + 18, 18, ink, face)


def bar(cv, pal, x, y, w, value):
    cv.fill_round_rect(x, y, w, 8, 4, pal.barBg)
    fill = w * value // 100
    cv.fill_round_rect(x, y, fill, 8, 4, pal.bar)
    cv.fill_circle(x + fill, y + 4, 8, pal.bg)
    cv.fill_circle(x + fill, y + 4, 6, pal.bar)


def mock(style, pal, portrait=False):
    w, h = (240, 320) if portrait else (320, 240)
    cv = Canvas(w, h, pal.bg)
    cv.fill_rect(0, 0, w, 36, pal.band)
    cv.hline(0, 36, w, pal.outline)
    STYLES[style](cv, 'back', 16, 18, 16, pal.text, pal.band)
    # Header parent uses pinned TFT font 2 in firmware. Here it uses CY-85 Label;
    # all track, count, time and chip labels below use the exact CY-85 faces.
    text(cv, 'Songs', 28, 18, pal.text, maxw=72)
    text(cv, '3 / 12' if not portrait else '3/12', 150 if not portrait else 125, 18, pal.dim, align=1)
    favourite(cv, style, pal, w - 90, 0, on=True)
    for k in (-1, 0, 1):
        cv.fill_circle(w - 24 + k * 6, 18, 2, pal.text)
    dy = (h - 240) // 2
    text(cv, '...Baby One More Time', w // 2, 52 + dy, pal.text, 'title', 1, w - 28)
    text(cv, 'Britney Spears', w // 2, 76 + dy, pal.dim, align=1)
    text(cv, '...Baby One More Time', w // 2, 97 + dy, pal.dim, align=1, maxw=w - 28)
    bar(cv, pal, 14, h - 124, w - 28, 37)
    text(cv, '1:24', 14, h - 105, pal.dim)
    text(cv, '-2:01', w - 14, h - 105, pal.dim, align=2)
    transport(cv, style, pal, 0, h - 94, w, 'active')
    cv.hline(0, h - 40, w, pal.outline)
    STYLES[style](cv, 'speaker', 20 if not portrait else 14, h - 20, 16, pal.text, pal.bg)
    vx, vw = (44, 102) if not portrait else (28, 48)
    bar(cv, pal, vx, h - 24, vw, 40)
    text(cv, '40', vx + vw + 10, h - 20, pal.dim)
    chipw = 124 if not portrait else 114
    chipx = w - 6 - chipw
    cv.fill_round_rect(chipx, h - 35, chipw, 30, 6, pal.band)
    cv.draw_round_rect(chipx, h - 35, chipw, 30, 6, pal.outline)
    text(cv, 'Headphones', chipx + 6, h - 20, pal.text, maxw=chipw - 9)
    return cv


def sheet(style, pal):
    cv = Canvas(720, 1120, pal.bg)
    text(cv, TITLES[style], 16, 22, pal.text, 'compact')
    text(cv, pal.label, 16, 48, pal.dim)
    text(cv, '24 px catalogue / volume = speaker enum', 16, 76, pal.dim)
    for i, (name, label) in enumerate(zip(NAMES, LABELS)):
        x, y = 40 + i % 9 * 80, 112 + i // 9 * 68
        STYLES[style](cv, name, x, y, 24, pal.text, pal.bg)
        text(cv, label, x, y + 27, pal.dim, align=1)
    text(cv, 'Same icon order / actual nominal sizes', 16, 239, pal.dim)
    for j, size in enumerate(SIZES):
        y = 278 + j * 42
        text(cv, str(size), 12, y, pal.dim)
        for i, name in enumerate(NAMES):
            STYLES[style](cv, name, 61 + i * 36, y, size, pal.text, pal.bg)
    for j, state in enumerate(['normal', 'pressed', 'active', 'repeat1']):
        y = 686 + j * 74
        text(cv, state, 16, y + 25, pal.dim)
        transport(cv, style, pal, 155, y, 320, state)
    text(cv, 'Portrait cells', 16, 1007, pal.dim)
    transport(cv, style, pal, 155, 980, 240, 'active')
    text(cv, 'Favourite off / on / pressed', 16, 1080, pal.dim)
    for i in range(3):
        favourite(cv, style, pal, 326 + i * 56, 1062, i == 1, i == 2)
    return cv


def paste(dst, src, x, y):
    for j in range(src.h):
        for i in range(src.w):
            dst._put(x + i, y + j, src.px[j * src.w + i])


def measure(style):
    cv = Canvas(160, 160, LIGHT.bg)
    glyph = {'windows': 0, 'pixels': 0, 'wire_bytes': 0}
    masks = rgb565 = max_width = 0
    for size in SIZES:
        for name in NAMES:
            cv.px[:] = [LIGHT.bg] * (cv.w * cv.h)
            cv.reset_stats()
            STYLES[style](cv, name, 80, 80, size, LIGHT.text, LIGHT.bg)
            assert any(c != LIGHT.bg for c in cv.px), (style, name, size)
            glyph['windows'] += cv.ops
            glyph['pixels'] += cv.pixels
            glyph['wire_bytes'] += cv.wire_bytes()
            if style in ('B', 'C'):
                bm = rasterise(name, size, style)
                depth = 1 if style == 'B' else 4
                grid = bm['bits'] if style == 'B' else bm['a4']
                stride = (bm['w'] * depth + 7) // 8
                packed = bytearray(stride * bm['h'])
                for row, values in enumerate(grid):
                    for col, value in enumerate(values):
                        shift = 8 - depth - (col * depth % 8)
                        packed[row * stride + col * depth // 8] |= value << shift
                for row, values in enumerate(grid):
                    for col, value in enumerate(values):
                        shift = 8 - depth - (col * depth % 8)
                        assert (packed[row * stride + col * depth // 8] >> shift) & ((1 << depth) - 1) == value
                # Row-aligned packed formats, with an explicit 12 B metadata entry per glyph.
                masks += len(packed)
                rgb565 += 2 * bm['w'] * bm['h']
                max_width = max(max_width, bm['w'])
    # Transport measurement needs the full 320 px row, unlike glyph isolation.
    cv = Canvas(320, 52, LIGHT.bg)
    costs = {}
    for state in ['normal', 'pressed', 'active', 'repeat1']:
        cv.reset_stats()
        transport(cv, style, LIGHT, state=state)
        costs[state] = {'windows': cv.ops, 'pixels': cv.pixels, 'wire_bytes': cv.wire_bytes(),
                        'wire_floor_us_40mhz': round(cv.wire_bytes() * 0.2, 1)}
    return {'catalogue': glyph, 'entries': len(SIZES) * len(NAMES),
            'transport_model': 'single-window whole-cell row compositor' if style in ('B', 'C') else 'primitive face and glyph calls',
            'mask_bytes': masks, 'metadata_bytes': len(SIZES) * len(NAMES) * 12 if masks else 0,
            'rgb565_one_color_bytes': rgb565, 'max_row_bytes': max_width * 2,
            'transport': costs}


def validate_png(path, expected=None):
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', path
    offset, compressed, dims, ended = 8, bytearray(), None, False
    while offset < len(data):
        length = struct.unpack('>I', data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        body = data[offset + 8:offset + 8 + length]
        crc = struct.unpack('>I', data[offset + 8 + length:offset + 12 + length])[0]
        assert zlib.crc32(kind + body) & 0xffffffff == crc, path
        if kind == b'IHDR':
            w, h, depth, color, compression, filt, interlace = struct.unpack('>IIBBBBB', body)
            assert (depth, color, compression, filt, interlace) == (8, 2, 0, 0, 0), path
            dims = (w, h)
        if kind == b'IDAT':
            compressed.extend(body)
        offset += 12 + length
        if kind == b'IEND':
            ended = True
            break
    assert ended and offset == len(data), path
    raw = zlib.decompress(compressed)
    assert len(raw) == dims[1] * (dims[0] * 3 + 1), path
    assert all(raw[j * (dims[0] * 3 + 1)] == 0 for j in range(dims[1])), path
    if expected:
        assert dims == expected, (path, dims, expected)
    return dims


def export_c(root):
    """CY-90 production export of the selected C shapes. No changes to vec/rasterise.
    High nibble first; each row has its own ceil(width/2) stride. Face spans use
    the existing TFT_eSPI primitive ports above, never a runtime rasteriser.
    """
    names = ['prev', 'play', 'pause', 'next', 'gear', 'bluetooth', 'back', 'note',
             'shuffle', 'repeat', 'repeat1', 'up', 'down', 'chevron', 'speaker', 'star', 'headphones', 'star_off']
    data, records, hashes = [], [], []
    for name in names:
        for size in SIZES:
            b = rasterise(name, size, 'C')
            stride = (b['w'] + 1) // 2
            records.append((len(data), b['w'], b['h'], stride, b['ox'], b['oy']))
            h = 2166136261
            for row in b['a4']:
                for a in row:
                    h = ((h ^ a) * 16777619) & 0xffffffff
                for x in range(0, b['w'], 2):
                    data.append((row[x] << 4) | (row[x+1] if x+1 < b['w'] else 0))
            hashes.append(h)
    def array(values, count=20):
        return '\n'.join('  ' + ','.join(str(v) for v in values[i:i+count]) + ',' for i in range(0, len(values), count))
    faces, face_records = [], []
    for w, h, inset, border in [(36,48,2,1),(52,48,2,1),(70,48,2,1),(80,52,2,1),(40,36,3,0)]:
        cv = Canvas(w,h)
        radius = min(w-2*inset,h-2*inset)//2
        cv.fill_round_rect(inset,inset,w-2*inset,h-2*inset,radius,1)
        if border:
            cv.draw_round_rect(inset,inset,w-2*inset,h-2*inset,radius,2)
        face_records.append((len(faces),w,h))
        for y in range(h):
            row=cv.px[y*w:(y+1)*w]
            outer=[x for x,c in enumerate(row) if c]
            inner=[x for x,c in enumerate(row) if c==1]
            span=[min(outer),max(outer)+1] if outer else [0,0]
            span += [min(inner),max(inner)+1] if inner else [0,0]
            # Verify every pixel can be represented by two nested spans.
            assert all(row[x] == (1 if span[2]<=x<span[3] else 2 if span[0]<=x<span[1] else 0) for x in range(w))
            faces.extend(span)
    app=root/'firmware/cydpod_player/app'
    header = """// Generated from CY-87 option C by tools/cy87_icon_sets.py --export-c.
// Original icon shapes; TFT_eSPI face-span provenance retained in the generator.
#pragma once
#include <Arduino.h>
namespace cydicons {
struct Mask { uint32_t offset; uint8_t w,h,stride; int8_t ox,oy; uint8_t reserved[3]; };
struct Face { uint16_t offset; uint8_t w,h; };
static_assert(sizeof(Mask)==12, "flash mask metadata size");
extern const uint8_t coverage[];
extern const Mask masks[162];
extern const uint8_t spans[];
extern const Face faces[5];
const Mask* find(unsigned icon, int size, bool starOff=false);
const Face* face(int w,int h);
uint8_t alpha(const Mask& mask,int x,int y);
}
"""
    cpp = """// Generated; reproduce with python -I tools/cy87_icon_sets.py --export-c.
#include "ui_icons.h"
#ifndef PROGMEM
#define PROGMEM
#endif
namespace cydicons {
const uint8_t coverage[] PROGMEM = {
""" + array(data) + '\n};\nconst Mask masks[162] PROGMEM = {\n'
    cpp += '\n'.join('  {%d,%d,%d,%d,%d,%d,{0,0,0}},' % r for r in records)
    cpp += '\n};\nconst uint8_t spans[] PROGMEM = {\n'+array(faces)+'\n};\nconst Face faces[5] PROGMEM = {\n'
    cpp += '\n'.join('  {%d,%d,%d},' % r for r in face_records)
    cpp += """
};
const Mask* find(unsigned icon,int size,bool starOff){
  if(icon<1 || icon>17)return nullptr;
  const int sizes[]={12,14,16,18,24,25,28,32,34};
  for(unsigned i=0;i<9;++i)if(size==sizes[i])return &masks[((starOff&&icon==16)?17:icon-1)*9+i];
  return nullptr;
}
const Face* face(int w,int h){for(const Face& f:faces)if(f.w==w&&f.h==h)return &f;return nullptr;}
uint8_t alpha(const Mask& m,int x,int y){
  if(x<0||y<0||x>=m.w||y>=m.h)return 0;
  const uint8_t v=coverage[m.offset+y*m.stride+x/2];
  return (x&1)?(v&15):(v>>4);
}
}
"""
    (app/'ui_icons.h').write_text(header)
    (app/'ui_icons.cpp').write_text(cpp)
    golden=root/'tests/golden/quiet_outline_masks.txt'
    golden.write_text(''.join(f'{n} {z} {h:08x}\n' for (n,z),h in zip(((n,z) for n in names for z in SIZES),hashes)))
    print(f'CY90_EXPORT_OK: {len(records)} masks, {len(data)} coverage bytes, {len(records)*12} metadata bytes, {len(faces)} span bytes, {len(face_records)*4} face metadata bytes')


def verify_production(directory):
    """Independent selected-C pixel oracle: compare production PNG streams with
    CY-87 source shapes/primitive ports, not with the exported packed arrays.
    Also losslessly recompresses host PNGs (no raster changes).
    """
    def read(path):
        validate_png(path)
        raw=path.read_bytes(); at=8; z=b''
        while at<len(raw):
            n=struct.unpack('>I',raw[at:at+4])[0];kind=raw[at+4:at+8];data=raw[at+8:at+8+n];at+=12+n
            if kind==b'IHDR': w,h=struct.unpack('>II',data[:8])
            if kind==b'IDAT': z+=data
        scan=zlib.decompress(z);assert len(scan)==h*(w*3+1)
        pixels=[]
        for y in range(h):
            row=scan[y*(w*3+1):(y+1)*(w*3+1)];assert row[0]==0
            pixels += [c565(*row[i:i+3]) for i in range(1,len(row),3)]
        # Re-encode only the PNG compression; retain every exact RGB byte.
        def chunk(k,d):return struct.pack('>I',len(d))+k+d+struct.pack('>I',zlib.crc32(k+d)&0xffffffff)
        hdr=struct.pack('>IIBBBBB',w,h,8,2,0,0,0)
        path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',hdr)+chunk(b'IDAT',zlib.compress(scan,9))+chunk(b'IEND',b''))
        return w,h,pixels
    names = ['prev','play','pause','next','gear','bluetooth','back','note','shuffle','repeat','repeat1','up','down','chevron','speaker','star','headphones']
    checked=0
    def compare(path,cv,rect=None):
        nonlocal checked
        w,h,pixels=read(directory/path);assert (w,h)==(cv.w,cv.h),path
        x0,y0,ww,hh=rect or (0,0,w,h)
        errors=[(x,y) for y in range(y0,y0+hh) for x in range(x0,x0+ww) if pixels[y*w+x]!=cv.px[y*w+x]]
        assert not errors,(path,len(errors),errors[:4])
        checked+=ww*hh
    for size in SIZES:
        cv=Canvas(320,240,LIGHT.bg)
        for i,name in enumerate(names): C_icon(cv,name,26+i%6*52,28+i//6*56,size,LIGHT.text,LIGHT.bg)
        C_icon(cv,'star_off',286,140,18,LIGHT.dim,LIGHT.bg)
        compare(Path(f'catalogue-{size}.png'),cv)
    for st,state in enumerate(('normal','pressed','active','repeat1')):
        cv=Canvas(320,240,LIGHT.bg);transport(cv,'C',LIGHT,y=146,state=state)
        compare(Path(f'transport-{st}.png'),cv)
    cv=Canvas(320,240,LIGHT.band)
    for st in range(4): favourite(cv,'C',LIGHT,20+st*70,20,bool(st&1),st>=2)
    compare(Path('favourite-states.png'),cv)
    for rotation in (0,1,3):
        w,h=(240,320) if rotation==0 else (320,240)
        cells=[(0,h-92,52 if w==320 else 36,48),(56 if w==320 else 40,h-92,70 if w==320 else 36,48),
               (130 if w==320 else 80,h-94,80,52),(214 if w==320 else 164,h-92,70 if w==320 else 36,48),(w-36,h-92,36,48)]
        for st in range(4):
            cv=Canvas(w,h,LIGHT.bg)
            for i,(r,name) in enumerate(zip(cells,['shuffle','prev','play','next','repeat'])):
                state='normal'
                if st==1 and i==2: name,state='pause','pressed'
                if st==2 and i in (0,2,4): state='active';name='pause' if i==2 else name
                if st==3 and i==4: name,state='repeat1','active'
                control(cv,'C',LIGHT,r,name,state)
                compare(Path(f'player-r{rotation}-s{st}.png'),cv,r)
    # A deliberately retained old expanded wrapper gives a visibly different
    # face and field. The independent oracle must reject that implementation.
    correct=Canvas(320,240,LIGHT.bg);wrong=Canvas(320,240,LIGHT.bg)
    control(correct,'C',LIGHT,(130,146,80,52),'play')
    control(wrong,'C',LIGHT,(127,143,86,58),'play')
    negative=sum(a!=b for a,b in zip(correct.px,wrong.px));assert negative>100
    for path in directory.glob('*.png'):
        w,h,px=read(path)
        if path.stem.endswith('-x4'):
            nw,nh,native=read(path.with_name(path.name.replace('-x4','')))
            assert (w,h)==(nw*4,nh*4)
            assert all(px[y*w+x]==native[(y//4)*nw+x//4] for y in range(h) for x in range(w)),path
    print(f'CY90_PIXELS_OK: {checked} independently expected production pixels; expanded-wrapper negative control differs at {negative} pixels; all native/x4 PNG CRCs and exact replications verified')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-production', type=Path, help='verify actual host-rendered PNGs against selected C source and recompress losslessly')
    parser.add_argument('--export-c', action='store_true', help='export selected C flash assets and independent mask fingerprints only')
    parser.add_argument('--fonts')
    parser.add_argument('--out', type=Path, default=Path(__file__).resolve().parents[1] / 'docs/research/cy87-icon-options')
    args = parser.parse_args()
    if args.verify_production:
        verify_production(args.verify_production)
        return
    if args.export_c:
        export_c(Path(__file__).resolve().parents[1])
        return
    fonts = find_font_dir(args.fonts)
    for face, stem in [('label', 'FreeSans9pt7b'), ('compact', 'FreeSansBold9pt7b'), ('title', 'FreeSansBold12pt7b')]:
        FONTS[face] = GfxFont(fonts / (stem + '.h'), stem)
    args.out.mkdir(parents=True, exist_ok=True)
    written = []

    def save(cv, name):
        for scale, suffix in [(1, ''), (4, '-x4')]:
            path = args.out / (name + suffix + '.png')
            cv.write_png(path, scale)
            validate_png(path, (cv.w * scale, cv.h * scale))
            written.append(path.name)

    for style in STYLES:
        palettes = PALETTES if style != 'current' else [LIGHT]
        for pal in palettes:
            save(sheet(style, pal), style.lower() + '-sheet-' + pal.name)
            save(mock(style, pal), style.lower() + '-player-' + pal.name)
        if style != 'current':
            save(mock(style, LIGHT, True), style.lower() + '-portrait-light')
    overview = Canvas(1016, 866, 0xE71C)
    for col, style in enumerate(['A', 'B', 'C']):
        text(overview, TITLES[style], 12 + col * 336, 18, LIGHT.text, 'compact')
        for row, pal in enumerate(PALETTES):
            y = 46 + row * 274
            text(overview, pal.name + (' / proposal' if row else ' / default'), 12 + col * 336, y - 10, LIGHT.text)
            paste(overview, mock(style, pal), 12 + col * 336, y)
    save(overview, 'comparison')
    metrics = {style: measure(style) for style in STYLES}
    metrics['contrast'] = {p.name: {'normal': round(contrast(p.btnText, p.raised), 2),
                                   'active': round(contrast(p.onActive, p.active), 2),
                                   'pressed': round(contrast(p.onActive, p.pressed), 2)} for p in PALETTES}
    metrics['pngs'] = written
    (args.out / 'metrics.json').write_text(json.dumps(metrics, indent=2) + '\n', encoding='utf-8')
    print('PASS CY87_PREVIEWS: %d PNGs; signature, CRC, RGB format and decompressed dimensions verified' % len(written))
    print('PASS CY87_CATALOGUE: 18 glyph states x 9 sizes x 4 styles; all masks nonempty')
    print('PASS CY87_PACKING: all 1-bit and 4-bit mask samples round-trip without loss')
    for style in STYLES:
        m = metrics[style]
        print('%s: packed=%d metadata=%d row=%d; active transport %s' %
              (style, m['mask_bytes'], m['metadata_bytes'], m['max_row_bytes'], m['transport']['active']))
    print('Contrast:', metrics['contrast'])


if __name__ == '__main__':
    main()
