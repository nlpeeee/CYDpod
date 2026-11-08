"""Phase 2 printed parts: tub (front), chassis, lid. Assembly frame of params.py.

Print orientations (see print_transform):
  Tub      as modelled: screen face on the bed.
  Chassis  flipped about X: the lid-side plate on the bed.
  Lid      as modelled, shifted down: inner face on the bed.
"""
from math import cos, pi, sin

from build123d import (Axis, Box, Circle, Ellipse, Location, Plane, Polygon, Pos, Rot,
                       RectangleRounded, Sphere, Align, extrude, fillet, chamfer, loft, Compound)

from . import params as P
from .envelope import box, cyl_z, rrect_prism

K, F, B = P.CASE, P.FASTENERS, P.BOARD
CX = 0.0
CY = (K["cavity_y0"] + K["cavity_y1"]) / 2
CW = 2 * K["cavity_x"]
CH = K["cavity_y1"] - K["cavity_y0"]


def outline(z, off):
    """Cavity outline offset outward by off, as a sketch at height z."""
    return Pos(CX, CY, z) * RectangleRounded(CW + 2 * off, CH + 2 * off, max(K["cavity_r"] + off, 0.4))


def outline_prism(off, z0, z1):
    return rrect_prism(CX - CW / 2 - off, CX + CW / 2 + off, CY - CH / 2 - off, CY + CH / 2 + off,
                       max(K["cavity_r"] + off, 0.4), z0, z1)


def wall_off(z, zt):
    """Outer wall offset from the cavity at height z (barrel profile)."""
    ch = K["front_chamfer"]
    if z <= ch:
        return K["wall_edge"] - (ch - z)
    t = min(max((z - ch) / (zt - ch), 0.0), 1.0)
    return K["wall_edge"] + K["bulge"] * sin(pi * t)


def wall_plane(origin, normal):
    """Plane on a side wall: x_dir horizontal, local y = +Z or -Z (symmetric cutters only)."""
    x_dir = (0, 1, 0) if abs(normal[0]) > 0.5 else (1, 0, 0)
    return Plane(origin=origin, x_dir=x_dir, z_dir=normal)


def rounded_cut(origin, normal, w, h, r, depth, leadin=0.5):
    """Through-cut with a 45 deg lead-in at the outer surface. origin = point on the outer surface."""
    nx, ny, nz = normal
    inner = (origin[0] - nx * depth, origin[1] - ny * depth, origin[2])
    pl = wall_plane(inner, normal)
    cut = extrude(pl * RectangleRounded(w, h, r), amount=depth + 2)
    pin = wall_plane((origin[0] - nx * leadin, origin[1] - ny * leadin, origin[2]), normal)
    cut += extrude(pin * RectangleRounded(w, h, r), amount=leadin + 1.5, taper=-45)
    return cut


def round_cut(origin, normal, d, depth, leadin=0.5):
    nx, ny, nz = normal
    inner = (origin[0] - nx * depth, origin[1] - ny * depth, origin[2])
    cut = extrude(wall_plane(inner, normal) * Circle(d / 2), amount=depth + 2)
    pin = wall_plane((origin[0] - nx * leadin, origin[1] - ny * leadin, origin[2]), normal)
    cut += extrude(pin * Circle(d / 2), amount=leadin + 1.5, taper=-45)
    return cut


# ---------------------------------------------------------------- tub
def tub(env):
    zt = env.z["lid_under"]
    ch, we = K["front_chamfer"], K["wall_edge"]
    band = loft([outline(0, we - ch), outline(ch, we)], ruled=True)
    zs = [ch + (zt - ch) * i / 6 for i in range(7)]
    barrel = loft([outline(z, wall_off(z, zt)) for z in zs])
    body = band + barrel

    body -= outline_prism(0, K["pocket_floor_z"], zt + 1)
    # Seat bosses with insert holes (insert flush with the boss top = PCB front face).
    for x, y in B["holes"]:
        body += cyl_z(x, y, F["boss_d"], 1.0, B["z_front"])
    gx0, gx1, gy0, gy1, gz0, gz1 = P.FRONT["glass"]
    body -= box(gx0, gx1, gy0, gy1, gz0, gz1 + 0.01)
    # Bosses must not overhang the glass pocket: trim them to its footprint all the way up.
    for x, y in B["holes"]:
        body -= box(gx0, gx1, gy0, gy1, gz0, B["z_front"] + 0.01) & cyl_z(x, y, F["boss_d"] + 0.01, gz0, B["z_front"] + 0.02)
    # Screen window, 45 deg taper toward the face.
    wx0, wx1, wy0, wy1 = P.FRONT["window"]
    bt = P.FRONT["bezel_t"]
    wcx, wcy, ww, wh = (wx0 + wx1) / 2, (wy0 + wy1) / 2, wx1 - wx0, wy1 - wy0
    wc = 0.6                                   # 45 deg chamfer at the face, then a 0.6 mm vertical land
    win = loft([Pos(wcx, wcy, -0.01) * RectangleRounded(ww + 2 * wc + 0.02, wh + 2 * wc + 0.02, 1.0 + wc),
                Pos(wcx, wcy, wc) * RectangleRounded(ww, wh, 1.0)], ruled=True)
    win += extrude(Pos(wcx, wcy, wc - 0.01) * RectangleRounded(ww, wh, 1.0), amount=2.0)
    body -= win
    for x, y in B["holes"]:
        body -= cyl_z(x, y, F["insert_hole_d"], F["insert_floor_z"], B["z_front"] + 0.01)

    # USB-C (+X wall).
    u = P.USB
    xo = K["cavity_x"] + wall_off(u["z"], zt)
    body -= rounded_cut((xo, u["y"], u["z"]), (1, 0, 0), u["w"], u["h"], 2.5, depth=xo - K["cavity_x"] + 0.5)
    # TF card (+Y wall) with a thumb dish.
    sx0, sx1, _, _, _, _ = P.SD_TRAVEL
    sdx0, sdx1, sdz0, sdz1 = -0.6, 16.5, 6.3, 9.3
    sz = (sdz0 + sdz1) / 2
    yo = K["cavity_y1"] + wall_off(sz, zt)
    body -= rounded_cut(((sdx0 + sdx1) / 2, yo, sz), (0, 1, 0), sdx1 - sdx0, sdz1 - sdz0, 1.0,
                        depth=yo - K["cavity_y1"] + 0.5)
    # Finger scoop under the slot only: a concave surface facing up, so it needs no support.
    scoop = Pos((sdx0 + sdx1) / 2, yo + 1.2, sdz0) * Rot(0, 90, 0) *         extrude(Circle(2.2), amount=(sdx1 - sdx0) / 2, both=True)
    body -= scoop & box(sdx0 + 1.0, sdx1 - 1.0, K["cavity_y1"] + 0.6, yo + 5, 0, sdz0 + 0.5)
    if env.variant == "audio":
        m = P.MODULE
        yj = K["cavity_y0"] - wall_off(m["jack_z"], zt)
        body -= round_cut((m["jack_x"], yj, m["jack_z"]), (0, -1, 0), m["jack_d"], depth=abs(yj - K["cavity_y0"]) + 0.5)
        # The bore breaks through the rim: open its top as a straight slot from the 45 deg points up.
        r = m["jack_d"] / 2
        body -= box(m["jack_x"] - r * 0.7071, m["jack_x"] + r * 0.7071, yj - 2, K["cavity_y0"] + 0.5,
                    m["jack_z"] + r * 0.7071, zt + 1)
    body = flexure(body, zt)
    return body


def flexure(body, zt):
    """Side-hinged power-button tongue in the -Y wall, bending in the layer plane."""
    s = P.SW1
    x_free, x_hinge, z0, z1, slot = 20.4, 34.4, 4.8, 10.8, 0.5
    y_in = K["cavity_y0"]
    y_out_min = y_in - (K["wall_edge"] + K["bulge"] + 0.5)
    tongue_t = 1.2
    y_pocket = y_in - wall_off(z0, zt) + tongue_t
    # U-slot through the wall. The top slot is a bridge (both ends held); the bottom slot runs at
    # 45 deg so the free tongue's underside is self-supporting instead of a 14 mm cantilever.
    body -= box(x_free - slot, x_hinge, y_out_min - 1, y_in + 0.5, z1, z1 + slot)
    yo, yi, gap = y_out_min - 1, y_in + 0.5, slot / 0.7071
    zt_o, zt_i = z0 - (y_in - yo), z0 - (y_in - yi)
    prof = [(yo, zt_o - gap), (yi, zt_i - gap), (yi, zt_i), (yo, zt_o)]
    body -= extrude(Plane.YZ.offset(x_free - slot) * Polygon(*prof, align=None), amount=x_hinge - x_free + slot)
    end = [(yo, zt_o - gap), (yi, zt_i - gap), (yi, z1 + slot), (yo, z1 + slot)]
    body -= extrude(Plane.YZ.offset(x_free - slot) * Polygon(*end, align=None), amount=slot)
    # Thin the tongue from the inside; the pocket floor follows the slanted slot so no wedge is left.
    zt_at = lambda y: z0 - (y_in - y)
    pk = [(y_pocket, zt_at(y_pocket)), (y_in + 0.01, zt_at(y_in + 0.01)), (y_in + 0.01, z1), (y_pocket, z1)]
    body -= extrude(Plane.YZ.offset(x_free) * Polygon(*pk, align=None), amount=x_hinge - x_free)
    # Nub with a 45 deg underside, 0.15 mm off the actuator face.
    y_tip = s["face_y"] - P.FASTENERS.get("sw_gap", 0.15)
    nz0, nz1, nw = 7.0, 9.6, 4.0
    run = y_tip - y_pocket
    prof = [(y_pocket - 0.01, nz0 - run), (y_tip, nz0), (y_tip, nz1), (y_pocket - 0.01, nz1)]
    nub = extrude(Plane.YZ.offset(s["x"] - nw / 2) * Polygon(*prof, align=None), amount=nw)
    body += nub
    # Raised oval pad with 45 deg sides and a shallow dish.
    pcx, pcz = 24.4, (z0 + z1) / 2
    yo = y_in - wall_off(pcz, zt)
    pad = extrude(wall_plane((pcx, yo + 0.3, pcz), (0, -1, 0)) * Ellipse(4.0, 3.0), amount=0.8, taper=45)
    body += pad
    body -= Pos(pcx, yo - 0.5 - 12.0 + 0.3, pcz) * Sphere(12.0)
    return body


# ---------------------------------------------------------------- chassis
def _module_keepouts(env, dilate=0.3, plate_z=None):
    """Bounding-box keep-outs of the module's parts; the board itself is kept exact."""
    outs, board = [], None
    mod = env.parts.get("Module")
    if mod is None:
        return outs, board
    for s in mod.solids():
        bb = s.bounding_box()
        dx, dy, dz = bb.max.X - bb.min.X, bb.max.Y - bb.min.Y, bb.max.Z - bb.min.Z
        if dx > 15 and dy > 20 and dz < 1.8:          # the module PCB
            board = s
            continue
        top = bb.max.Z + dilate
        if plate_z is not None and top > plate_z - 0.4:
            top = 40
        outs.append(box(bb.min.X - dilate, bb.max.X + dilate, bb.min.Y - dilate, bb.max.Y + dilate,
                        bb.min.Z - dilate, top))
    return outs, board


def chassis(env):
    zt = env.z["lid_under"]
    plate_t, cradle_t, cradle_z0 = 1.0, 1.2, 13.0
    c = env.info["cell"]["footprint"]
    inner = (c[0] - 0.2, c[1] + 0.2, c[2] - 0.2, c[3] + 0.2)
    clip = outline_prism(-0.3, B["z_back"], zt)

    part = outline_prism(-0.3, zt - plate_t, zt) - box(*inner, zt - plate_t - 1, zt + 1)
    part += box(inner[0] - cradle_t, inner[1] + cradle_t, inner[2] - cradle_t, inner[3] + cradle_t, cradle_z0, zt) \
        - box(*inner, cradle_z0 - 1, zt + 1)
    audio = env.variant == "audio"
    short = tuple(B["holes"][0]) if audio else None
    for x, y in B["holes"]:
        if (x, y) == short:
            continue
        part += cyl_z(x, y, F["tube_od"], B["z_back"], zt)
    if audio:
        lx, ly = F["lid_screw_audio"]
        part += cyl_z(lx, ly, F["boss_d"], zt - 6.2, zt)
    part = part & clip

    # Keep-outs (components and plug grow upward through anything above them only to their top + 0.3).
    for name, (x0, x1, y0, y1, h) in P.COMPONENTS.items():
        part -= box(x0 - 0.3, x1 + 0.3, y0 - 0.3, y1 + 0.3, B["z_back"] - 1, B["z_back"] + h + 0.3)
    for name, g in P.WIRE_CLEAR.items():          # wire run: g on every side (flattens the (-39,40) tube)
        x0, x1, y0, y1, h = P.COMPONENTS[name]
        part -= box(x0 - g, x1 + g, y0 - g, y1 + g, B["z_back"] - 1, B["z_back"] + h + 0.3)
    bx0, bx1, by0, by1, bz0, bz1 = P.BATT_PLUG
    part -= box(bx0 - 0.3, bx1 + 0.3, by0 - 0.3, by1 + 0.3, bz0 - 1, bz1 + 0.3)
    if audio:
        outs, board = _module_keepouts(env, plate_z=zt - plate_t)
        for o in outs:
            part -= o
        if board is not None:
            part -= board
        sx, sy = B["holes"][0]
        part -= cyl_z(sx, sy, F["btn_head_d"] + 1.0, P.MODULE["face_z"] - 0.1, P.MODULE["face_z"] + F["btn_head_h"] + 0.6)
        part -= cyl_z(lx, ly, F["insert_hole_d"], zt - F["insert_len"], zt + 0.1)
        part -= cyl_z(lx, ly, F["screw_clear_d"], zt - 7.0, zt)
        # Stay clear of the whole module footprint: no slivers between its parts.
        mb = env.info["module_bbox"]
        part -= box(-50, mb[1] + 0.4, mb[2] - 3, mb[3] + 0.5, B["z_back"] - 1, zt + 1)
    for x, y in B["holes"]:
        part -= cyl_z(x, y, F["screw_clear_d"], B["z_back"] - 1, zt + 1)
    return part


# ---------------------------------------------------------------- lid
def lid(env):
    zt, zl = env.z["lid_under"], env.z["lid_top"]
    we = K["wall_edge"]
    plate = outline_prism(we, zt, zl)
    top_edges = plate.edges().group_by(Axis.Z)[-1]
    plate = fillet(top_edges, radius=1.4)
    bot_edges = plate.edges().group_by(Axis.Z)[0]
    plate = chamfer(bot_edges, length=0.3)
    holes = [h for h in B["holes"] if not (env.variant == "audio" and tuple(h) == tuple(B["holes"][0]))]
    if env.variant == "audio":
        holes.append(F["lid_screw_audio"])
    for x, y in holes:
        plate -= cyl_z(x, y, F["screw_clear_d"], zt - 1, zl + 1)
        plate -= cyl_z(x, y, F["cbore_d"], zl - F["cbore_depth"], zl + 1)
    if env.variant == "audio":
        m = P.MODULE
        y0 = K["cavity_y0"] - 5
        plate -= Pos(m["jack_x"], y0 + 4, m["jack_z"]) * Rot(90, 0, 0) * \
            extrude(Circle(m["jack_d"] / 2), amount=8, both=True)
    return plate


def coupon(env, tub_shape):
    """Quick test print: the power-button flexure plus the (39,-2) insert boss and corner."""
    return tub_shape & box(15.0, 50.0, -15.0, 3.5, -1, env.z["lid_under"] + 1)


def spacer(env):
    """Audio only: tube between the PCB rear face and the module underside at (-39,-2)."""
    x, y = B["holes"][0]
    z1 = P.MODULE["face_z"] - P.MODULE["board_t"]
    return cyl_z(x, y, F["spacer_od"], B["z_back"], z1) - cyl_z(x, y, F["screw_clear_d"], B["z_back"] - 1, z1 + 1)


def build_parts(env):
    d = dict(Tub=tub(env), Chassis=chassis(env), Lid=lid(env))
    if env.variant == "audio":
        d["Spacer"] = spacer(env)
    return d


def print_transform(name, env):
    zt, zl = env.z["lid_under"], env.z["lid_top"]
    if name == "Chassis":
        return Pos(0, 0, zt) * Rot(180, 0, 0)          # Z' = zt - Z
    if name == "Lid":
        return Pos(0, 0, -zt)
    if name == "Coupon":
        return Location()
    if name == "Spacer":
        return Pos(0, 0, -B["z_back"])
    return Location()
