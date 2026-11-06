"""Outline the CYDpod wordmark: Source Sans 3 Regular glyphs + click-wheel 'o'.

Mirrors the brand sheet CSS: letter-spacing -0.025em, o = 0.5em box,
margins 0.03em / 0.02em, sunk 0.01em below baseline; ring stroke 16%, dot 25%.
"""
import sys, os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.transformPen import TransformPen

font_path, out_dir = sys.argv[1], sys.argv[2]
f = TTFont(font_path)
upm = f["head"].unitsPerEm
cmap = f.getBestCmap()
gs = f.getGlyphSet()
hmtx = f["hmtx"]
em = upm / 1000.0


def kern(left, right):
    """Pair kerning from GPOS 'kern' (PairPos formats 1 and 2)."""
    if "GPOS" not in f:
        return 0
    gpos = f["GPOS"].table
    lookups = set()
    for fr in gpos.FeatureList.FeatureRecord:
        if fr.FeatureTag == "kern":
            lookups.update(fr.Feature.LookupListIndex)
    total = 0
    for li in sorted(lookups):
        lk = gpos.LookupList.Lookup[li]
        for st in lk.SubTable:
            if lk.LookupType == 9:
                st = st.ExtSubTable
            if getattr(st, "LookupType", 2) != 2 or left not in st.Coverage.glyphs:
                continue
            if st.Format == 1:
                ps = st.PairSet[st.Coverage.glyphs.index(left)]
                for pvr in ps.PairValueRecord:
                    if pvr.SecondGlyph == right and pvr.Value1 is not None:
                        return getattr(pvr.Value1, "XAdvance", 0) or 0
            elif st.Format == 2:
                c1 = st.ClassDef1.classDefs.get(left, 0)
                c2 = st.ClassDef2.classDefs.get(right, 0)
                v = st.Class1Record[c1].Class2Record[c2].Value1
                if v is not None:
                    return getattr(v, "XAdvance", 0) or 0
    return total


tracking = -25 * em
parts = []  # (svg path d, colour role)
x = 0.0
prev = None


def add_glyph(ch):
    global x, prev
    g = cmap[ord(ch)]
    if prev:
        x += kern(prev, g)
    pen = SVGPathPen(gs)
    gs[g].draw(TransformPen(pen, (1, 0, 0, 1, x, 0)))
    parts.append((pen.getCommands(), "ink"))
    x += hmtx[g][0] + tracking
    prev = g


for ch in "CYDp":
    add_glyph(ch)

# wheel-o
x += 30 * em
size = 500 * em
cx, cy = x + size / 2, size / 2 - 10 * em
r_mid, stroke = 0.42 * size, 0.16 * size
ro, ri, rd = r_mid + stroke / 2, r_mid - stroke / 2, 0.125 * size


def circle(cx, cy, r, ccw=False):
    s = 0 if ccw else 1
    return (f"M{cx - r:.2f} {cy:.2f}A{r:.2f} {r:.2f} 0 1 {s} {cx + r:.2f} {cy:.2f}"
            f"A{r:.2f} {r:.2f} 0 1 {s} {cx - r:.2f} {cy:.2f}Z")


parts.append((circle(cx, cy, ro) + circle(cx, cy, ri, ccw=True), "ink"))
parts.append((circle(cx, cy, rd), "dot"))
x += size + 20 * em
prev = None
add_glyph("d")

# bounds of ink (glyphs via BoundsPen, wheel analytically)
bp = BoundsPen(gs)
xx = 0.0
p = None
xmin, ymin, xmax, ymax = cx - ro, cy - ro, cx + ro, cy + ro
for ch in "CYDp":
    g = cmap[ord(ch)]
    if p:
        xx += kern(p, g)
    b = BoundsPen(gs)
    gs[g].draw(b)
    if b.bounds:
        bx0, by0, bx1, by1 = b.bounds
        xmin, ymin = min(xmin, bx0 + xx), min(ymin, by0)
        xmax, ymax = max(xmax, bx1 + xx), max(ymax, by1)
    xx += hmtx[g][0] + tracking
    p = g
gd = cmap[ord("d")]
b = BoundsPen(gs)
gs[gd].draw(b)
dx = x - hmtx[gd][0] - tracking
xmin, ymin = min(xmin, b.bounds[0] + dx), min(ymin, b.bounds[1])
xmax, ymax = max(xmax, b.bounds[2] + dx), max(ymax, b.bounds[3])

pad = 0
w, h = xmax - xmin + 2 * pad, ymax - ymin + 2 * pad
tf = f"matrix(1 0 0 -1 {-xmin + pad:.2f} {ymax + pad:.2f})"

variants = {
    "cydpod-wordmark": ("#1D1D1F", "#FFD21F"),
    "cydpod-wordmark-reversed": ("#FBFBFB", "#FFD21F"),
    "cydpod-wordmark-on-yellow": ("#1D1D1F", "#1D1D1F"),
    "cydpod-wordmark-mono": ("#000000", "#000000"),
}
os.makedirs(out_dir, exist_ok=True)
for name, (ink, dot) in variants.items():
    ink_d = "".join(d for d, role in parts if role == "ink")
    dot_d = "".join(d for d, role in parts if role == "dot")
    svg = (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:.0f} {h:.0f}" '
        f'width="{w / 2:.0f}" height="{h / 2:.0f}">\n'
        f"  <title>CYDpod</title>\n"
        f'  <g transform="{tf}">\n'
        f'    <path fill="{ink}" fill-rule="nonzero" d="{ink_d}"/>\n'
        f'    <path fill="{dot}" d="{dot_d}"/>\n'
        f"  </g>\n</svg>\n"
    )
    with open(os.path.join(out_dir, name + ".svg"), "w", encoding="utf-8") as fh:
        fh.write(svg)
    print("wrote", name, f"{w:.0f}x{h:.0f}")

print("kern C-Y", kern(cmap[ord('C')], cmap[ord('Y')]), "Y-D", kern(cmap[ord('Y')], cmap[ord('D')]),
      "D-p", kern(cmap[ord('D')], cmap[ord('p')]))
