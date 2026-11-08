"""Phase 1 envelope model: board, components, cell, module, fasteners, cavity.

Every solid here is a keep-out or occupied volume, not a printed part. The
printed tub, chassis and lid come in Phase 2 and must clear these.
"""
from dataclasses import dataclass, field

from build123d import (Box, Compound, Cylinder, Location, Pos, Rot, RectangleRounded,
                       Solid, extrude, import_step, Align)

from . import params as P

MIN = (Align.MIN, Align.MIN, Align.MIN)


def box(x0, x1, y0, y1, z0, z1) -> Solid:
    return Pos(x0, y0, z0) * Box(x1 - x0, y1 - y0, z1 - z0, align=MIN)


def rrect_prism(x0, x1, y0, y1, r, z0, z1) -> Solid:
    face = Pos((x0 + x1) / 2, (y0 + y1) / 2, z0) * RectangleRounded(x1 - x0, y1 - y0, r)
    return extrude(face, z1 - z0)


def cyl_z(x, y, d, z0, z1) -> Solid:
    return Pos(x, y, z0) * Cylinder(d / 2, z1 - z0, align=(Align.CENTER, Align.CENTER, Align.MIN))


@dataclass
class Envelope:
    variant: str
    parts: dict = field(default_factory=dict)       # name -> Solid/Compound (occupied)
    info: dict = field(default_factory=dict)


def pcb() -> Solid:
    b = P.BOARD
    y0 = b["cy"] - b["w"] / 2
    s = rrect_prism(-b["l"] / 2, b["l"] / 2, y0, y0 + b["w"], b["corner_r"], b["z_front"], b["z_back"])
    for x, y in b["holes"]:
        s = s - cyl_z(x, y, b["hole_d"], b["z_front"] - 1, b["z_back"] + 1)
    return s


def module(info: dict):
    m = P.MODULE
    raw = import_step(m["step"])
    kept, stripped = [], 0
    for s in raw.solids():
        bb = s.bounding_box()
        # Header housings and pins: above the jack, or below the board's underside parts.
        if m["strip_headers"] and (bb.max.Z > 5.5 or bb.min.Z < -2.2):
            stripped += 1
            continue
        kept.append(s)
    placed = Location((m["x"], m["y"], m["face_z"])) * Rot(0, 0, -90) * Compound(kept)
    bb = placed.bounding_box()
    info["module_solids"] = len(kept)
    info["module_stripped_solids"] = stripped
    info["module_bbox"] = [round(v, 2) for v in (bb.min.X, bb.max.X, bb.min.Y, bb.max.Y, bb.min.Z, bb.max.Z)]
    return placed, bb


def cell_base(footprint, extra=(), comps=None):
    """Lowest cell base Z over the parts under its footprint."""
    x0, x1, y0, y1 = footprint
    tallest, who = 0.0, "none"
    comps = P.COMPONENTS if comps is None else comps
    items = [(n, (a, b, c, d, P.BOARD["z_back"] + h)) for n, (a, b, c, d, h) in comps.items()]
    items += list(extra)
    for name, (a, b, c, d, ztop) in items:
        if a < x1 and b > x0 and c < y1 and d > y0:
            h = ztop - P.BOARD["z_back"]
            if h > tallest:
                tallest, who = h, name
    base = P.BOARD["z_back"] + tallest + P.STACK["clear"] + P.STACK["insulation"]
    return base, who, tallest


def build(variant: str, cell_x0=None, cell_y0=-1.6, remove_ext_io=None) -> Envelope:
    assert variant in P.VARIANTS
    env = Envelope(variant)
    parts, info = env.parts, env.info
    b, c, f, k = P.BOARD, P.CELL, P.FASTENERS, P.CASE
    audio = variant == "audio"
    if remove_ext_io is None:
        remove_ext_io = P.BOARD_MODS["remove_ext_io"]
    comps = {n: v for n, v in P.COMPONENTS.items() if not (remove_ext_io and n in P.EXT_IO)}

    parts["PCB"] = pcb()
    for name, (x0, x1, y0, y1, h) in comps.items():
        parts["C_" + name] = box(x0, x1, y0, y1, b["z_back"], b["z_back"] + h)
    parts["BattPlug"] = box(*P.BATT_PLUG)
    wire_cuts = []
    for name, g in P.WIRE_CLEAR.items():
        if name in comps:
            x0, x1, y0, y1, h = comps[name]
            zz = (b["z_back"], b["z_back"] + h)
            wire_cuts.append(box(x0 - g, x1 + g, y0 - g, y1 + g, *zz))
            parts["Wire" + name] = wire_cuts[-1] - box(x0, x1, y0, y1, zz[0] - 1, zz[1] + 1)

    jack_top = 0.0
    if audio:
        parts["Module"], mbb = module(info)
        jack_top = mbb.max.Z

    # Cell: plan size with wrap, PCM fold at +X.
    cl = c["l"] + c["pcm"] + 2 * c["wrap"]
    cw = c["w"] + 2 * c["wrap"]
    ct = c["t"] + c["t_tol"]   # wrap is a plan allowance; the insulation sheet is in the base
    if cell_x0 is None:
        cell_x0 = (-24.26 + 0.3) if audio else -cl / 2
    fp = (cell_x0, cell_x0 + cl, cell_y0, cell_y0 + cw)
    plug = ("BattPlug", (P.BATT_PLUG[0], P.BATT_PLUG[1], P.BATT_PLUG[2], P.BATT_PLUG[3], P.BATT_PLUG[5]))
    z0, who, tallest = cell_base(fp, [plug], comps)
    z_cell_top = z0 + ct
    parts["Cell"] = box(fp[0], fp[1], fp[2], fp[3], z0, z_cell_top)
    parts["CellSwellFoam"] = box(fp[0], fp[1], fp[2], fp[3], z_cell_top, z_cell_top + c["swell"] + c["foam"])

    lid_under = max(z_cell_top + c["swell"] + c["foam"], jack_top + P.STACK["jack_clear"] if audio else 0)
    lid_top = lid_under + P.STACK["lid"]

    # Fasteners: shank keep-out from the insert floor to the lid top; chassis tubes over the PCB.
    insert_floor = f["insert_floor_z"]
    head_seat = lid_top - f["cbore_depth"]
    short_corner = tuple(b["holes"][0]) if audio else None    # (-39,-2) sits under the module
    screws = {}
    for x, y in b["holes"]:
        tag = f"{x:+.0f}{y:+.0f}"
        if (x, y) == short_corner:
            # One M3 clamps the module, a printed spacer and the PCB into the tub insert.
            head_z0 = P.MODULE["face_z"]
            parts["ScrewShort" + tag] = cyl_z(x, y, f["screw_clear_d"], insert_floor, head_z0)
            parts["ScrewHeadShort" + tag] = cyl_z(x, y, f["btn_head_d"] + 0.4, head_z0, head_z0 + f["btn_head_h"] + 0.2)
            screws[tag] = round(head_z0 - insert_floor, 2)
        else:
            parts["Screw" + tag] = cyl_z(x, y, f["screw_clear_d"], insert_floor, lid_top)
            parts["ScrewHead" + tag] = cyl_z(x, y, f["head_d"] + 0.2, head_seat, lid_top)
            tube = cyl_z(x, y, f["tube_od"], b["z_back"], lid_under)
            for w in wire_cuts:
                tube = tube - w
            parts["Tube" + tag] = tube
            screws[tag] = round(head_seat - insert_floor, 2)
    if audio:
        x, y = f["lid_screw_audio"]
        parts["ScrewLid"] = cyl_z(x, y, f["screw_clear_d"], lid_under - 5.9, lid_top)
        parts["ScrewHeadLid"] = cyl_z(x, y, f["head_d"] + 0.2, head_seat, lid_top)
        screws["lid4"] = round(head_seat - (lid_under - 5.9), 2)
    env.z = dict(lid_under=lid_under, lid_top=lid_top, head_seat=head_seat, insert_floor=insert_floor)

    parts["SDTravel"] = box(*P.SD_TRAVEL)

    cavity = rrect_prism(-k["cavity_x"], k["cavity_x"], k["cavity_y0"], k["cavity_y1"], k["cavity_r"],
                         k["pocket_floor_z"], lid_under)
    env.cavity = cavity
    w = k["wall"]
    outer = (2 * (k["cavity_x"] + w), (k["cavity_y1"] - k["cavity_y0"]) + 2 * w)

    info["ext_io_removed"] = bool(remove_ext_io)
    info.update(
        cell=dict(name=c["name"], footprint=[round(v, 2) for v in fp], base_z=round(z0, 2),
                  top_z=round(z_cell_top, 2), set_by=who, tallest_under=tallest),
        jack_top_z=round(jack_top, 2) if audio else None,
        lid_under_z=round(lid_under, 2), lid_top_z=round(lid_top, 2),
        thickness_driver="jack" if audio and jack_top + P.STACK["jack_clear"] > z_cell_top + c["swell"] + c["foam"] else "cell",
        outer_mm=[round(outer[0], 2), round(outer[1], 2), round(lid_top, 2)],
        screw_grip_mm=screws,
    )
    return env
