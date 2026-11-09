"""Render section slabs: blender -b -P hardware/case/render_sections.py -- hardware/case/out/<variant>"""
import sys
from pathlib import Path
import bpy
from mathutils import Vector

out = Path(sys.argv[sys.argv.index("--") + 1]).resolve()
sec = out / "sections"
COL = {"Tub": (0.2, 0.25, 0.32, 1), "Chassis": (0.9, 0.5, 0.1, 1), "Lid": (0.45, 0.55, 0.7, 1),
       "Spacer": (0.8, 0.2, 0.6, 1), "context": (0.45, 0.7, 0.3, 1)}
VIEW = {"A": ((200, 0, 0), (0, 0, 1)), "B": ((200, 0, 0), (0, 0, 1)), "D": ((200, 0, 0), (0, 0, 1)),
        "C": ((0, -200, 0), (0, 0, 1)), "E": ((0, -200, 0), (0, 0, 1))}
cuts = sorted({p.name.split("__")[0] for p in sec.glob("*.stl")})
for cut in cuts:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "FLAT"
    sc.display.shading.color_type = "OBJECT"
    sc.display.shading.background_type = "VIEWPORT"
    sc.display.shading.background_color = (1, 1, 1)
    sc.render.resolution_x, sc.render.resolution_y = 1600, 700
    obs = []
    for p in sec.glob(f"{cut}__*.stl"):
        bpy.ops.wm.stl_import(filepath=str(p))
        ob = bpy.context.selected_objects[0]
        ob.color = COL[p.stem.split("__")[1]]
        obs.append(ob)
    pts = [ob.matrix_world @ Vector(c) for ob in obs for c in ob.bound_box]
    ctr = sum(pts, Vector()) / len(pts)
    off, up = VIEW[cut[0]]
    cam = bpy.data.cameras.new("c"); cam.type = "ORTHO"
    horiz = (max(p.y for p in pts) - min(p.y for p in pts)) if cut[0] in "ABD" else (max(p.x for p in pts) - min(p.x for p in pts))
    vert = max(p.z for p in pts) - min(p.z for p in pts)
    cam.ortho_scale = max(horiz, vert * 1600 / 700) * 1.05
    co = bpy.data.objects.new("c", cam); sc.collection.objects.link(co)
    co.location = ctr + Vector(off)
    co.rotation_euler = (ctr - co.location).to_track_quat("-Z", "Y").to_euler()
    sc.camera = co
    sc.render.filepath = str(out / "renders" / f"sec_{cut}.png")
    bpy.ops.render.render(write_still=True)
print("SECTION_RENDERS_DONE")
