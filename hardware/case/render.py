"""Render case v5 previews from the exported STLs (Blender, headless).

    blender -b --python-exit-code 1 -P hardware/case/render.py -- hardware/case/out/<variant>
"""
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

out = Path(sys.argv[sys.argv.index("--") + 1]).resolve()
asm = out / "assembly"
rdir = out / "renders"
rdir.mkdir(exist_ok=True)

COLORS = {"Tub": (0.16, 0.18, 0.21, 1), "Chassis": (0.85, 0.45, 0.12, 1),
          "Lid": (0.30, 0.34, 0.40, 1), "context": (0.55, 0.62, 0.35, 1)}


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sh = sc.display.shading
    sh.light = "STUDIO"
    sh.color_type = "OBJECT"
    sh.show_cavity = True
    sh.cavity_type = "BOTH"
    sh.show_shadows = False
    sc.render.resolution_x, sc.render.resolution_y = 1400, 1000
    sc.world = bpy.data.worlds.new("w")
    sc.display.shading.background_type = "VIEWPORT"
    sc.display.shading.background_color = (0.96, 0.96, 0.97)
    return sc


def load(name, offset=(0, 0, 0)):
    bpy.ops.wm.stl_import(filepath=str(asm / f"{name}.stl"))
    ob = bpy.context.selected_objects[0]
    ob.name = name
    ob.color = COLORS[name]
    ob.location = Vector(offset)
    return ob


def shoot(sc, fname, eye, target=(0, 19, 10), scale=120):
    cam = bpy.data.cameras.new("c")
    cam.type = "ORTHO"
    cam.ortho_scale = scale
    co = bpy.data.objects.new("c", cam)
    sc.collection.objects.link(co)
    co.location = Vector(eye)
    d = Vector(target) - Vector(eye)
    co.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    sc.camera = co
    sc.render.filepath = str(rdir / fname)
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(co)


views = [
    ("01_front.png", ["Tub", "Chassis", "Lid"], {}, (120, -110, -150), (0, 19, 10), 120),
    ("02_rear.png", ["Tub", "Chassis", "Lid"], {}, (-120, -110, 170), (0, 19, 10), 120),
    ("03_bottom_edge.png", ["Tub", "Lid", "context"], {}, (40, -160, 40), (0, 0, 10), 100),
    ("04_exploded.png", ["Tub", "context", "Chassis", "Lid"], {"Chassis": (0, 0, 30), "Lid": (0, 0, 55)},
     (-140, -160, 170), (0, 19, 30), 170),
    ("05_tub_inside.png", ["Tub"], {}, (-60, -90, 170), (0, 19, 5), 110),
    ("06_chassis.png", ["Chassis", "context"], {}, (-90, -120, 160), (0, 19, 12), 110),
    ("07_top_edge_sd_usb.png", ["Tub", "Lid"], {}, (130, 160, 40), (10, 30, 9), 90),
    ("08_button_closeup.png", ["Tub", "Lid", "context"], {}, (60, -150, 20), (26, -9, 8), 28),
    ("09_button_inside.png", ["Tub"], {}, (10, 60, 60), (27, -8, 7), 30),
]
for fname, names, offs, eye, target, scale in views:
    sc = reset()
    for n in names:
        if (asm / f"{n}.stl").exists():
            load(n, offs.get(n, (0, 0, 0)))
    shoot(sc, fname, eye, target, scale)
print("CASE_V5_RENDER_DONE", rdir)
