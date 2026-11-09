"""Overlap and containment checks for the envelope model."""
from itertools import combinations

# Pairs that touch or nest by design (zero volume is still required where it matters).
ALLOWED = {
    ("PCB", "Screw"), ("PCB", "ScrewShort"), ("Module", "ScrewShort"),            # shanks pass the Ø3.2 holes; Ø3.4 keep-out bites 0.1 mm
    ("Screw", "Tube"), ("ScrewShort", "ScrewHeadShort"), ("Screw", "ScrewHead"),
    ("ScrewHead", "Tube"),
    ("Cell", "CellSwellFoam"),
    ("C_TF", "SDTravel"),                                # the card enters the socket
}
# Solids that leave the cavity on purpose.
LEAVES_CAVITY = ("Screw", "SDTravel", "Module")   # module: jack nose enters the wall bore


def _kind(name):
    for prefix in ("ScrewHeadShort", "ScrewHead", "ScrewShort", "Screw", "Tube"):
        if name.startswith(prefix):
            return prefix
    return name


def _bb_overlap(a, b):
    A, B = a.bounding_box(), b.bounding_box()
    return (A.min.X < B.max.X and B.min.X < A.max.X and A.min.Y < B.max.Y and B.min.Y < A.max.Y
            and A.min.Z < B.max.Z and B.min.Z < A.max.Z)


def _vol(shape):
    try:
        return float(shape.volume)
    except Exception:
        return 0.0


def overlaps(parts: dict, tol=1e-3):
    out = {}
    for (na, a), (nb, b) in combinations(parts.items(), 2):
        ka, kb = _kind(na), _kind(nb)
        if (ka, kb) in ALLOWED or (kb, ka) in ALLOWED:
            continue
        if not _bb_overlap(a, b):
            continue
        if na.startswith("C_") and nb == "PCB" or nb.startswith("C_") and na == "PCB":
            continue
        v = _vol(a & b)
        if v > tol:
            out[f"{na}|{nb}"] = round(v, 3)
    return out


def outside_cavity(parts: dict, cavity, tol=1e-3):
    out = {}
    for name, s in parts.items():
        if name.startswith(LEAVES_CAVITY):
            continue
        v = _vol(s - cavity)
        if v > tol:
            out[name] = round(v, 3)
    return out
