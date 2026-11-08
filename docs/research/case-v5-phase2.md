# Case v5 Phase 2: printed parts and checks

2026-11-07, branch `cydpod/case-v5`. Phase 2 of `docs/specs/case-v5-plan.md`. Prototype 1 parts for both variants: generator `hardware/case/cydcase/parts.py`, print notes `hardware/case/PRINTING.md`.

## Design as built

| Item | Value |
|---|---|
| Outside | 91.2 × 57.5 mm at the bulge (90.0 × 55.8 at the rim and front), 20.5 mm thick |
| Sides | Wall 1.4 mm at the front chamfer and the rim, sine bulge to 2.0 mm at mid-height; 45° × 1.0 mm front chamfer |
| Plan corners | Cavity R2.8 (tangent-clears the PCB's R1.6 corners + 0.25), outside R4.2–4.8 |
| Lid | 1.8 mm plate, R1.4 top-edge fillet, 0.3 mm bottom chamfer; Ø6.4 × 1.25 counterbores (0.55 mm floor, clamped onto the chassis tubes) |
| Chassis | Plate 1.0 mm at Z 17.7–18.7 around the cell, cradle walls 1.2 mm to Z 13.0, Ø6.4 screw tubes to the PCB. Audio: stays clear of the module footprint and adds a boss with an M3 insert for the fourth lid screw at (−39, 28) |
| Fasteners | M3 × 18 at the PCB corners into M3 × 4 inserts (holes to Z 0.8; tip 0.45 mm above the hole floor). Audio: M3 × 12 through module + spacer + PCB at (−39, −2), M3 × 6 lid screw into the chassis |
| Audio module hole | Ø3.2 at (−39, −2.1) from the vendor STEP, coaxial with the PCB hole within 0.1 mm (v4 had −3.7) |
| Button | Tongue 14 × 6 mm, 1.2 mm thick, hinged at x 34.4, bending in the layer plane. Top slot 0.5 mm (bridge); bottom slot at 45° so the free tongue's underside is self-supporting. Nub at x 23.9 with a 45° underside, 0.15 mm off SW1; Z 7.0–9.6 and 4 mm wide to absorb ±1 mm. Raised oval pad (8 × 6 mm base, 45° sides, 0.3 mm dish). Estimated press force about 4 N (PETG flexure ~2.6 N + SW1 1.6 N); root strain about 0.8% |
| Ports | USB-C 12.4 × 7.0 R2.5 with 0.5 mm 45° lead-in; TF 17.1 × 3.0 with an upward-facing finger scoop under it; audio jack Ø6.4 with lead-in, opened to the rim above its 45° points |
| Screen | v4/reference window (60 × 47 at the land) with 0.6 mm 45° chamfer + 0.6 mm land; glass pocket 71 × 51 × 1.2; right-hand bosses trimmed to the glass-pocket footprint |

## Checks (final run)

Logs are under `.tools/logs/` in the worktree.

| Check | Result |
|---|---|
| `build.py` envelope, both variants | `CASE_V5_ENVELOPE_PASS audio`, `CASE_V5_ENVELOPE_PASS no-audio` |
| `build_parts.py`: one valid solid per part; zero overlap among parts and against PCB, components, battery plug, module STEP, cell, swelling/foam zone, screw shanks and heads, SD path; no unsupported overhang in print orientation | `CASE_V5_PARTS_PASS audio`, `CASE_V5_PARTS_PASS no-audio` |
| Overlap-check control: PCB shifted 1 mm in X | Caught (23.8 mm³ tub overlap) |
| Overhang-check controls | 13 mm cantilever T fails; 8 mm bridge passes; 30 mm bridge fails |
| Exported print STLs, edge manifoldness | 0 non-manifold edges in all 8 files; Bambu Studio `--info` reports `manifold = yes` |
| Bambu Studio 2.x CLI slice, stock A1 0.4 / 0.20 mm Standard / Bambu PETG Basic, supports off | All 8 parts `Success.` with no warning (`case-slice-final.log`) |
| Ray-cast wall thickness (tub, 4000 samples) | p1 0.83 mm. Thin readings are only the intended 0.6 mm window land and the 45° edges where the slanted button slot meets the wall faces. Lid min 0.55 (counterbore floors). Chassis ≥ 1.0 |

The real slicer caught one problem my checker missed. The first spherical thumb dish around the TF slot left a lip that Bambu flagged as a "floating cantilever". I localised it by slicing regions, then replaced the dish with an upward-facing scoop. The checker's thresholds were tightened against controls during the run: bridges must be supported at both ends and ≤ 20 mm.

## Not verified

- Physical fit, button feel, insert retention and the screw clamp. The coupon print is the first physical check.
- Component positions tagged E in `params.py` (±1 mm): SW1, USB-C, TF, battery plug.
- Glass and display stack fit (reference-case geometry, unmeasured).
- RF with the audio module over the antenna area.
