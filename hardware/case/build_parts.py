"""Build, check and export the case v5 printed parts.

    .tools/venv-case/Scripts/python -I hardware/case/build_parts.py [--variant audio|no-audio|both]

Writes hardware/case/out/<variant>/print/{Tub,Chassis,Lid}.stl (print orientation),
assembly.step, assembly/*.stl (assembly pose) and parts-report.json. Prints
CASE_V5_PARTS_PASS or CASE_V5_PARTS_FAIL per variant.
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from build123d import Compound, Vector, export_step, export_stl  # noqa: E402

from cydcase import checks, envelope, params, parts, printcheck  # noqa: E402

PRINTED = ("Tub", "Chassis", "Lid")
ALLOWED = {
    ("Spacer", "ScrewShort-39-2"),                   # bore = keep-out diameter
    ("Tub", "Lid"), ("Chassis", "Lid"),              # face contact at the rim / plate (checked as volume anyway)
}


def run(variant):
    env = envelope.build(variant)
    built = parts.build_parts(env)
    if variant == "no-audio":
        built["Coupon"] = parts.coupon(env, built["Tub"])
    out = HERE / "out" / variant
    (out / "print").mkdir(parents=True, exist_ok=True)
    (out / "assembly").mkdir(parents=True, exist_ok=True)

    solids = {k: len(v.solids()) for k, v in built.items()}
    valid = {k: bool(v.is_valid) for k, v in built.items()}
    keep = {k: v for k, v in env.parts.items() if not k.startswith("Tube")}
    keep.update({k: v for k, v in built.items() if k != "Coupon"})
    ov = checks.overlaps(keep)
    ov = {k: v for k, v in ov.items() if tuple(k.split("|")) not in ALLOWED}

    pr = {}
    for name, shape in built.items():
        export_stl(shape, str(out / "assembly" / f"{name}.stl"), tolerance=0.005, angular_tolerance=0.08)
        placed = parts.print_transform(name, env) * shape
        bb = placed.bounding_box()
        p = out / "print" / f"{name}.stl"
        export_stl(placed, str(p), tolerance=0.005, angular_tolerance=0.08)
        pr[name] = printcheck.check(str(p), inside=lambda pt, s=placed: s.is_inside(Vector(*pt)))
        pr[name]["zmin"] = round(bb.min.Z, 3)
        pr[name]["volume_cm3"] = round(shape.volume / 1000, 2)
    export_step(Compound(list(built.values())), str(out / "assembly.step"))
    ready = HERE / "out" / "print-ready"
    ready.mkdir(exist_ok=True)
    for name in built:
        tag = "noaudio" if variant == "no-audio" else "audio"
        (ready / f"CYDpod-case-v5-{tag}-{name}.stl").write_bytes((out / "print" / f"{name}.stl").read_bytes())
    ctx = [v for k, v in env.parts.items() if k in ("PCB", "Cell", "Module", "BattPlug") or k.startswith("C_")]
    export_stl(Compound(ctx), str(out / "assembly" / "context.stl"), tolerance=0.02, angular_tolerance=0.3)

    ok = (not ov and all(v == 1 for v in solids.values()) and all(valid.values())
          and all(not r["failing_regions"] for r in pr.values())
          and all(abs(r["zmin"]) < 0.01 for r in pr.values()))
    report = dict(variant=variant, info=env.info, z=env.z, solids=solids, valid=valid,
                  overlaps_mm3=ov, printability=pr, result="PASS" if ok else "FAIL")
    (out / "parts-report.json").write_text(json.dumps(report, indent=2))
    print(json.dumps({k: report[k] for k in ("solids", "valid", "overlaps_mm3", "printability")}, indent=1))
    print(("CASE_V5_PARTS_PASS " if ok else "CASE_V5_PARTS_FAIL ") + variant)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", default="both", choices=list(params.VARIANTS) + ["both"])
    a = ap.parse_args()
    variants = params.VARIANTS if a.variant == "both" else (a.variant,)
    sys.exit(0 if all([run(v) for v in variants]) else 1)


if __name__ == "__main__":
    main()
