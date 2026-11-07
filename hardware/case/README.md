# CYDpod case v5 generator

Parametric enclosure for the JC2432W328 CYDpod, in [build123d](https://github.com/gumyr/build123d). Plan: `docs/specs/case-v5-plan.md`. Input dimensions and their sources: `docs/research/case-v5-measurements.md`.

## Setup

Python 3.12, from the repository root:

```powershell
py -3.12 -m venv .tools/venv-case
.tools/venv-case/Scripts/python -m pip install -r hardware/case/requirements.lock.txt
```

`requirements.txt` names the direct dependency; `requirements.lock.txt` is the frozen set the checks were run with (build123d 0.13.0, cadquery-ocp-novtk 8.0.1.1.0).

The audio variant imports the vendor STEP at `E:\dev\research\i2s-module\repo\Hardware\WeAct-I2SEarphoneModuleV1 3D.step` (path in `cydcase/params.py`).

## Run

```powershell
.tools/venv-case/Scripts/python -I hardware/case/build.py                 # both variants
.tools/venv-case/Scripts/python -I hardware/case/build.py --variant audio
.tools/venv-case/Scripts/python -I hardware/case/build.py --remove-ext-io # model with P6/CN1/P3 housings removed
```

Each variant writes `hardware/case/out/<variant>/envelope.step` and `report.json` (ignored by Git) and prints `CASE_V5_ENVELOPE_PASS` or `CASE_V5_ENVELOPE_FAIL`.

## Layout

| File | Contents |
|---|---|
| `cydcase/params.py` | Every dimension, tagged with its source (S/R/E/V4/D) |
| `cydcase/envelope.py` | Phase 1 keep-out model: PCB, rear components, battery plug, WeAct module (header pins stripped), cell + swelling/foam, M3 screw shanks and chassis tubes, SD-card path, cavity |
| `cydcase/checks.py` | Pairwise overlap volumes and cavity containment |
| `build.py` | CLI, STEP export, report |

Printed parts (tub, chassis, lid) come in Phase 2 and must clear the envelope solids.
