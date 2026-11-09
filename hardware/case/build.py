"""Build and check the case v5 envelope model.

    .tools/venv-case/Scripts/python -I hardware/case/build.py [--variant audio|no-audio|both]

Writes hardware/case/out/<variant>/envelope.step and report.json, and prints
CASE_V5_ENVELOPE_PASS or CASE_V5_ENVELOPE_FAIL per variant.
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from build123d import Compound, export_step  # noqa: E402

from cydcase import checks, envelope, params  # noqa: E402


def run(variant: str, remove_ext_io=None) -> bool:
    env = envelope.build(variant, remove_ext_io=remove_ext_io)
    ov = checks.overlaps(env.parts)
    oc = checks.outside_cavity(env.parts, env.cavity)
    ok = not ov and not oc
    out = HERE / "out" / (variant + ("-noextio" if env.info["ext_io_removed"] else ""))
    out.mkdir(parents=True, exist_ok=True)
    comp = Compound([s for s in env.parts.values()])
    export_step(comp, str(out / "envelope.step"))
    report = dict(variant=variant, info=env.info, overlaps_mm3=ov, outside_cavity_mm3=oc,
                  result="PASS" if ok else "FAIL")
    (out / "report.json").write_text(json.dumps(report, indent=2))
    print(json.dumps({k: report[k] for k in ("info", "overlaps_mm3", "outside_cavity_mm3")}))
    print(("CASE_V5_ENVELOPE_PASS " if ok else "CASE_V5_ENVELOPE_FAIL ") + variant)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", default="both", choices=list(params.VARIANTS) + ["both"])
    ap.add_argument("--remove-ext-io", action="store_true", default=None)
    a = ap.parse_args()
    variants = params.VARIANTS if a.variant == "both" else (a.variant,)
    results = [run(v, a.remove_ext_io) for v in variants]
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
