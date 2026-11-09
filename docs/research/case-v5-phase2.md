# Case v5 Phase 2: printed parts and checks

2026-11-07, branch `cydpod/case-v5`. Phase 2 of `docs/specs/case-v5-plan.md`. Prototype 1 parts for both variants: generator `hardware/case/cydcase/parts.py`, print notes `hardware/case/PRINTING.md`.

## Design as built

| Item | Value |
|---|---|
| Outside | 91.2 × 57.5 mm at the bulge (90.0 × 55.8 at the rim and front), 20.5 mm thick (audio 21.2 mm since 2026-11-08, set by the raised jack) |
| Sides | Wall 1.4 mm at the front chamfer and the rim, sine bulge to 2.0 mm at mid-height; 45° × 1.0 mm front chamfer |
| Plan corners | Cavity R2.8 (tangent-clears the PCB's R1.6 corners + 0.25), outside R4.2–4.8 |
| Lid | 1.8 mm plate, R1.4 top-edge fillet, 0.3 mm bottom chamfer; Ø6.4 × 1.25 counterbores (0.55 mm floor, clamped onto the chassis tubes) |
| Chassis | Plate 1.0 mm at Z 17.7–18.7 around the cell, cradle walls 1.2 mm to Z 13.0, Ø6.4 screw tubes to the PCB. Audio: stays clear of the module footprint and adds a boss with an M3 insert for the fourth lid screw at (−39, 28) |
| Fasteners | M3 × 18 at the PCB corners into M3 × 4 inserts (holes to Z 0.8; tip 0.45 mm above the hole floor). Audio: M3 × 12 through module + spacer + PCB at (−39, −2), M3 × 6 lid screw into the chassis |
| Audio module hole | Ø3.2 at (−39, −2.1) from the vendor STEP, coaxial with the PCB hole within 0.1 mm (v4 had −3.7) |
| Button | Tongue 14 × 6 mm, 1.2 mm thick, hinged at x 34.4, bending in the layer plane. Top slot 0.5 mm (bridge); bottom slot at 45° so the free tongue's underside is self-supporting. Nub at x 23.9 with a 45° underside, 1.15 mm off the estimated SW1 face (was 0.15 mm, which held SW1 pressed on the owner's prototype-1 print, 2026-11-08; root strain at full press now about 2.6 %); Z 7.0–9.6 and 4 mm wide to absorb ±1 mm. Raised oval pad (8 × 6 mm base, 45° sides, 0.3 mm dish). Estimated press force about 4 N (PETG flexure ~2.6 N + SW1 1.6 N); root strain about 0.8% |
| Ports | USB-C 12.4 × 7.0 R2.5 with 0.5 mm 45° lead-in; TF 17.1 × 3.0 with an upward-facing finger scoop under it; audio jack Ø6.4 with lead-in, opened to the rim above its 45° points |
| P6 wire run | ≥ 1.0 mm free on every side of the P6 housing, from the PCB to the housing top (owner request 2026-11-08). Toward the top wall it's 1.25 mm, toward CN1 1.0 mm. The chassis tube at (−39, 40) is flattened on its P6 side, leaving 0.8 mm of tube wall there. Checked as the `WireP6` keep-out in both overlap checks |
| Audio header bay (2026-11-08) | Owner's print showed the module's right-angle header and Dupont housings running about 19 mm toward P6. The header now goes on the module's underside, its plastic resting on the ESP32 shield, so the module sits at face Z 13.99 (+0.89) and the jack and its tub hole move up with it. The `Headers` keep-out (x −43.6…−24.8, y 15.5…37.0, shield top +2.5, +3.0 beyond the module edge; photo estimate ±1 mm) clears the chassis to 0.3 mm. The (−39, 40) tube is flattened on its −Y side for the wire turn (1.0 mm tube wall left), and the lid-screw boss at (−39, 28) now starts above the Duponts. Grip at (−39, −2) is 13.2 mm, so the M3 × 12 still engages 3.2 mm of the insert |
| Back attachment (2026-11-08) | Owner: screws internal, back clips on. Corner screws are M3 × 16 low heads from inside: chassis tube → PCB → insert (grip 16.5 mm no-audio, 17.2 mm audio; heads seat in a Ø6.8 pocket in an Ø8.4 collar, 0.2 mm under the lid). The (−39, 28) lid screw, its chassis boss and the fifth insert are gone. The lid has no holes. It prints back face down with a 1.0 mm 45° edge chamfer and carries six PETG cantilever clips (1.2 × 8 × 6 mm, 0.8 mm root gusset, 0.6 mm 45°/45° barb with 0.45 mm in the tub wall; root strain while passing the rim 1.5·1.2·0.45/6² = 2.25 %). Each clips into a 45°-faced recess in the tub wall with 0.1 mm clearance. Positions: −Y x −18 and +6, +Y x +5 and +28, +X y 8, −X y 26. 6 × 1 mm pry notch in the +X rim. `Clip*` keep-outs (arm, flex, 0.3 margin) are in both overlap checks |
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
| Ray-cast wall thickness (tub, 4000 samples) | p1 0.83 mm. Thin readings are only the intended 0.6 mm window land and the 45° edges where the slanted button slot meets the wall faces. Lid min 0.55 (counterbore floors). Chassis ≥ 1.0, except 0.8 at the flattened (−39, 40) tube beside P6 |

The real slicer caught one problem my checker missed. The first spherical thumb dish around the TF slot left a lip that Bambu flagged as a "floating cantilever". I localised it by slicing regions, then replaced the dish with an upward-facing scoop. The checker's thresholds were tightened against controls during the run: bridges must be supported at both ends and ≤ 20 mm.

## Not verified

- Physical fit, button feel, insert retention and the screw clamp. The coupon print is the first physical check.
- Component positions tagged E in `params.py` (±1 mm): SW1, USB-C, TF, battery plug.
- Glass and display stack fit (reference-case geometry, unmeasured).
- RF with the audio module over the antenna area.
