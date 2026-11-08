"""Export thin slabs of the assembly for section renders (hardware/case/out/<variant>/sections)."""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from build123d import Compound, export_stl  # noqa: E402

from cydcase import envelope, parts  # noqa: E402
from cydcase.envelope import box  # noqa: E402

CUTS = {  # name: slab box (x0,x1,y0,y1,z0,z1)
    "A_button_x23.9": (23.8, 24.0, -20, 60, -5, 30),
    "B_jack_x-29.7": (-29.8, -29.6, -20, 60, -5, 30),
    "C_stack_y19": (-60, 60, 18.9, 19.1, -5, 30),
    "D_screw_x39": (38.9, 39.1, -20, 60, -5, 30),
    "E_usb_y24.6": (-60, 60, 24.5, 24.7, -5, 30),
}
for variant in ("audio", "no-audio"):
    env = envelope.build(variant)
    built = parts.build_parts(env)
    out = HERE / "out" / variant / "sections"
    out.mkdir(parents=True, exist_ok=True)
    ctx_names = ["PCB", "Cell", "Module", "BattPlug"] + [k for k in env.parts if k.startswith("C_") or k.startswith("Screw")]
    for cut, b in CUTS.items():
        slab = box(*b)
        for name, shp in list(built.items()) + [("context", Compound([env.parts[n] for n in ctx_names if n in env.parts]))]:
            try:
                s = shp & slab
                if s.volume > 1e-4:
                    export_stl(s, str(out / f"{cut}__{name}.stl"), tolerance=0.01, angular_tolerance=0.1)
            except Exception as e:
                print("skip", variant, cut, name, e)
    print("SECTIONS_DONE", variant)
