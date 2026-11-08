"""FDM printability check on a binary STL in print orientation (Z up, bed at min Z).

Flags down-facing regions steeper than max_deg from vertical that are not on the
bed and are wider than a bridge in both horizontal directions.
"""
import struct
from math import cos, radians

import numpy as np


def read_stl(path):
    data = open(path, "rb").read()
    n = struct.unpack("<I", data[80:84])[0]
    rec = np.frombuffer(data[84:84 + n * 50], dtype=np.dtype([("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")]))
    return rec["v"].astype(np.float64)


def _components(tri_idx, tris):
    """Union-find over triangles sharing a vertex."""
    parent = {i: i for i in tri_idx}

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    owner = {}
    for i in tri_idx:
        for v in tris[i]:
            key = tuple(np.round(v, 3))
            if key in owner:
                ra, rb = find(i), find(owner[key])
                if ra != rb:
                    parent[ra] = rb
            else:
                owner[key] = i
    groups = {}
    for i in tri_idx:
        groups.setdefault(find(i), []).append(i)
    return list(groups.values())


def _supported_axes(lo, hi, inside, drop=0.6, gaps=(0.4, 1.0, 2.0, 3.0)):
    """Axes (0=x, 1=y) along which material sits just below both ends of the region."""
    if inside is None or lo[2] - drop < 0.2:          # first layers: the slicer bridges from the bed
        return (0, 1)
    z = lo[2] - drop
    axes = []
    for ax in (0, 1):
        other = 1 - ax
        ok = True
        for frac in (0.25, 0.5, 0.75):
            o = lo[other] + (hi[other] - lo[other]) * frac
            for sign, end in ((-1, lo[ax]), (1, hi[ax])):
                hit = False
                for g in gaps:
                    pt = [0.0, 0.0, z]
                    pt[ax], pt[other] = end + sign * g, o
                    if inside(pt):
                        hit = True
                        break
                if not hit:
                    ok = False
                    break
            if not ok:
                break
        if ok:
            axes.append(ax)
    return tuple(axes)


def check(path, max_deg=45.0, bridge=20.0, tol_deg=1.0, inside=None):
    tris = read_stl(path)
    e1, e2 = tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]
    nrm = np.cross(e1, e2)
    area = np.linalg.norm(nrm, axis=1) / 2
    nz = nrm[:, 2] / np.maximum(np.linalg.norm(nrm, axis=1), 1e-12)
    zmin = tris[:, :, 2].min()
    on_bed = np.all(np.abs(tris[:, :, 2] - zmin) < 0.05, axis=1)
    limit = -cos(radians(90 - max_deg - tol_deg))        # nz below this = steeper than max_deg+tol from vertical
    cand = np.where((nz < limit) & ~on_bed & (area > 1e-6))[0]
    fails, bridges = [], 0
    for g in _components(cand, tris):
        pts = tris[g].reshape(-1, 3)
        lo, hi = pts.min(axis=0), pts.max(axis=0)
        span = min(hi[0] - lo[0], hi[1] - lo[1])
        a = float(area[g].sum())
        axes = () if a < 0.5 else _supported_axes(lo, hi, inside)
        if a < 0.5 or any(hi[ax] - lo[ax] <= bridge for ax in axes):
            bridges += 1
            continue
        fails.append(dict(area_mm2=round(a, 3), span=round(float(span), 2),
                          bbox=[round(float(v), 2) for v in (*lo, *hi)]))
    size = tris.reshape(-1, 3).max(axis=0) - tris.reshape(-1, 3).min(axis=0)
    return dict(triangles=int(len(tris)), size_mm=[round(float(s), 2) for s in size],
                short_overhang_regions=bridges, failing_regions=fails)


def thickness(path, samples=6000, thin=1.0, seed=1):
    """Ray-cast wall thickness: from area-weighted surface samples, inward along -normal."""
    tris = read_stl(path)
    v0, e1, e2 = tris[:, 0], tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]
    nrm = np.cross(e1, e2)
    ar = np.linalg.norm(nrm, axis=1)
    keep = ar > 1e-9
    v0, e1, e2, nrm, ar = v0[keep], e1[keep], e2[keep], nrm[keep], ar[keep]
    unit = nrm / ar[:, None]
    rng = np.random.default_rng(seed)
    idx = rng.choice(len(ar), size=min(samples, len(ar) * 4), p=ar / ar.sum())
    r1, r2 = rng.random(len(idx)), rng.random(len(idx))
    s = np.sqrt(r1)
    pts = v0[idx] + (1 - s)[:, None] * 0 + (s * (1 - r2))[:, None] * e1[idx] + (s * r2)[:, None] * e2[idx]
    dirs = -unit[idx]
    out = np.full(len(idx), np.inf)
    for k in range(len(idx)):
        o, d = pts[k] + dirs[k] * 1e-4, dirs[k]
        p = np.cross(d, e2)
        det = np.einsum("ij,ij->i", e1, p)
        ok = np.abs(det) > 1e-12
        inv = np.where(ok, 1.0 / np.where(ok, det, 1), 0)
        tv = o - v0
        u = np.einsum("ij,ij->i", tv, p) * inv
        q = np.cross(tv, e1)
        w = (q @ d) * inv
        t = np.einsum("ij,ij->i", e2, q) * inv
        hit = ok & (u >= 0) & (w >= 0) & (u + w <= 1) & (t > 1e-3)
        if hit.any():
            out[k] = t[hit].min()
    finite = out[np.isfinite(out)]
    bad = np.where(out < thin)[0]
    clusters = {}
    for k in bad:
        key = tuple(np.round(pts[idx.size and k] / 3.0).astype(int))
        c = clusters.setdefault(key, [np.inf, pts[k]])
        c[0] = min(c[0], out[k])
    return dict(samples=int(len(idx)), min_mm=round(float(finite.min()), 3), p1_mm=round(float(np.percentile(finite, 1)), 3),
                below_thin=int(len(bad)),
                thin_spots=sorted(([round(float(v[0]), 2)] + [round(float(x), 1) for x in v[1]] for v in clusters.values()))[:25])
