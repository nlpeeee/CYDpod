# Case v5 plan: printable, pocketable, tactile

2026-11-07. Replaces the CASE-1b v4 enclosure (`releases/0.5.1/hardware/CYDpod-enclosure-v4.zip`, generator `E:\dev\hq\tools\case1b\case_v3.py`). Decisions taken with the user: 604050 cell in both variants (6 × 40 × 50, about 1500 mAh; the 604560 first chosen does not fit beside the audio module, see `docs/research/case-v5-phase1.md`), Extended IO JST housings kept on the board, 4 × M3 screws into heat-set inserts (M3 chosen 2026-11-07 so the screws locate the PCB in its Ø3.2 holes), PLA/PETG/ASA (no TPU, single material), parametric CAD in build123d.

## What is wrong with v4

| Problem | Where in v4 | Why it fails on FDM / in the hand |
|---|---|---|
| Five snap latches, 1.2 mm beams with 0.5 mm hooks | `case_v3.py:105-118` | Beams are vertical, so bending loads go across layer lines: they snap or creep. 0.5 mm engagement is barely one extrusion width. No way to reopen without breaking. |
| Separate 5 mm power button with stem + double flange, captured by a 3.4 mm seam tongue | `case_v3.py:119-123, 165-173` | Flange overhangs the stem in any orientation, so it needs supports. The tongue is fragile. 0.25 mm stroke on a 4.8 mm pad is hard to find and feels dead. |
| AudioSupport part: base r3, riser r1.3, seat r2.6 on a different axis | `case_v3.py:157-164` | Seat overhangs the riser either way up: supports on a 6 mm part. |
| Back carries all retention: 4 tall PCB retainers, 8 battery rails, 4 end stops, 2 strap eyes, module cap, module end stop | `case_v3.py:102-103, 143-153, 163-164` | Printable plate-down, but a forest of thin posts that lose bed adhesion, and the strap eyes (1.8 mm bores) need a strap you thread blind. |
| 24.5 mm thick brick with 4 mm plan radius and 0.6 mm chamfers | `build-report.json`, renders | Mesh booleans in Blender cannot make real fillets, so every edge is a hard line. Thickness comes from the 10 mm 103665 cell. |
| Side holes are plain circles (`teardrop=False`), USB is a 12.4 × 7 mm square slot | `case_v3.py:125-131` | Circle tops droop; a square USB slot looks crude and does not guide the plug. |

## Target

| | No-audio | Audio (WeAct module) |
|---|---|---|
| Thickness at centre | 20.5 mm (Phase 1 envelope, set by the 4.5 mm Extended IO JSTs under the cell) | 20.5 mm (same; jack top would allow 20.3) |
| Thickness felt at the edge | ~13 mm (rolled rear edge) | ~14 mm |
| Footprint | ≤ 91 × 57 mm (≤ 1 mm per side larger than v4, buys the edge roll) | same |
| Printed parts | Tub, Chassis, Lid | Tub, Chassis (with module mount), Lid |
| Fasteners | 4 × M3 button-head screws, 4 × M3 × 4 heat-set inserts | same, plus one short M3 under the module and an M2 for the module itself |
| Supports needed | none | none |

### Thickness stack (to confirm once the cell and jack are measured)

| Item | No-audio, mm | Audio, mm |
|---|---:|---:|
| Front face to PCB rear | 6.8 | 6.8 |
| Highest component under the cell | 3.1 | 4.0 |
| Clearance + insulating sheet | 0.4 | 0.4 |
| Cell (5–6 mm class) | 5.0–6.0 | 5.0–6.0 |
| Swelling + foam pad | 0.8 | 0.8 |
| Lid plate | 1.8 | 1.8 |
| **Total** | **17.9–18.9** | **18.8–19.8**, but the jack top (module face Z 13.1 + jack body ≈ 5) + lid gives ≈ 20 |

The module's 8.2 mm male header pins (top at Z 21.3) must go: remove them and solder wires straight to the pads.

## Architecture: tub + chassis + lid

All three parts print with flat faces on the bed and every overhang at or under 45° or as a short bridge.

1. **Tub (front)** — prints face-down. Holds the screen glass pocket, the PCB seat bosses (Ø7.5, Z 1.2–5.2) with M3 × 4 heat-set inserts, every port, and the power button flexure. Side walls rise from the bed; the upper 4–5 mm of the outside curves inward (an inward lean faces upward, so it needs no support). The top rim has a 1 mm rebate that locates the lid.
2. **Chassis** — prints flat. An open frame that sits on the PCB rear: screw tubes over the four PCB holes, battery cradle walls, a foam/pad shelf, and in the audio variant a module standoff and cable channel. No floor under components. It replaces every rail, retainer, end stop, strap eye and the AudioSupport part.
3. **Lid (back)** — prints inner-face-down, so the outside is the top surface: crowned about 1 mm and rolled at the edges, continuing the tub's curve. Ironed top for a smooth feel. Counterbored screw holes open upward.

**One screw stack:** 4 × M3 × 18 button-head screws (19.0 mm grip in the Phase 1 envelope) pass lid counterbore (Ø6.2) → chassis tube (Ø3.4 bore, Ø6.4 outside) → PCB hole (Ø3.2, which locates the board) → insert in the tub seat boss. Four screws clamp everything, and the battery and module are held by the chassis and a foam pad under the lid. Audio variant: the module board covers the (-39,-2) PCB hole in plan (module Z 11.5–13.1), so that corner gets a short M3 × 6 button head that clamps only chassis + PCB and sits under the module (head top ≤ Z 10.5). It goes in before the module, which then fixes to the chassis with its own M2 screw or pin.

**Seam:** where the tub's inward curve meets the lid's roll, so it reads as part of the curve rather than a line across the side.

## Tactile and ergonomic details

- **Profile:** soap-bar. Front edge (bed side) gets a 1.2 mm 45° chamfer plus a 0.4 mm elephant-foot relief. The rear edge rolls with a radius of about 5 mm, split between tub and lid. Plan corner radius goes from 4 mm to 6–7 mm (limited by the PCB corners: measure).
- **Power button:** printed into the tub's bottom wall as a horizontal cantilever flexure, hinged at one side so it bends along the layers (strong in PETG). About 10 × 6 mm tongue, 1.2 mm thick, 0.3 mm slot, inward nub 0.15 mm off SW1. The pad is a 7 × 5 mm oval, 0.5 mm proud, with a slight dish so you can find it blind in a pocket. No separate part and no assembly tongue.
- **Screen:** glass sits 0.2–0.3 mm below the bezel; 0.6 mm chamfer on the window edge.
- **USB-C:** rounded slot sized to a real plug overmould, with a 0.5 mm lead-in.
- **Headphone jack:** 0.5 mm lead-in. Hole printed as a circle with a flat bridged top hidden behind the chamfer.
- **SD:** keep the top-edge slot; add a 45° fingernail scoop so the card can be pushed and pulled.
- **Lid surface:** smooth (ironed). Optional parameter: shallow grip ridges on the lid roll only.
- **Material:** PETG for the tub (the flexure needs it; PLA fatigues and creeps, and softens around 55 °C in a hot car or pocket). Lid and chassis in PETG or ASA.

## FDM rules the generator must enforce

- No downward-facing surface steeper than 45° from vertical unless it is on the bed or a bridge ≤ 10 mm.
- Walls ≥ 1.2 mm (3 × 0.4 mm lines); features ≥ 0.8 mm; snap-like parts loaded along layers, never across.
- Holes in vertical walls: teardrop or bridged-flat top, hidden behind the outside chamfer.
- Fits: 0.2 mm sliding, 0.15 mm lid-in-rebate, insert bosses to the insert maker's hole size (typically Ø4.0–4.2 for M3 × 4 short inserts; use the maker's figure).
- Bed-side edges: chamfer only; fillets only on upward-facing edges.

## Toolchain

- New generator in **build123d** (Python, OpenCascade): real fillets, variable radius, exports STEP + STL + 3MF. It can import the WeAct module STEP directly instead of the Blender tessellation step.
- Port the component envelopes (`COMPONENTS`, PCB holes, switch/USB/SD/jack positions) from `case_lib.py`/`case_v3.py` into one parameter module.
- Proposed home: `hardware/case/` in this repo (source and checks in Git, generated STL/3MF/STEP ignored, release packages under `releases/<version>/`). The v4 Blender pipeline in `E:\dev\hq\tools\case1b` stays as history.
- Keep Blender only for renders, fed from the exported STLs.

## Checks

1. Geometry: closed manifold solids, zero overlap between parts and against the PCB, component proxies, cell envelope, module STEP, screw shanks and the SD-card travel path (port the v4 overlap list).
2. **Printability (new):** in print orientation, flag any down-facing face beyond 45° that is not on the bed and not a ≤ 10 mm bridge; flag walls under 1.2 mm. Every part must report zero flags.
3. Thickness, footprint and screw lengths reported in `build-report.json`.
4. Renders: front, rear, side profile, exploded, chassis.
5. Physical, in order: a test-coupon print (button flexure, one insert boss, a rim + lid corner, USB/jack/SD mouths, glass pocket corner; about an hour), adjust clearances, then the full print with the real cell. Hardware fit, button feel and screw clamping count as accepted only after this print.

## Measure / buy before CAD

- Cell: 5–6 mm pouch, envelope ≤ 6.0 × 46 × 62 mm including protection board and tab exit (for example the 504560 / 604560 class, roughly 1500–2000 mAh, against about 2500+ for the 103665). Check runtime against `docs/specs/power-plan.md` before buying. Measure the actual cell.
- PCB outline and corner radius, mounting-hole diameter (Ø3.2 from the datasheet; M3 chosen).
- SW1 actuator position, height and travel.
- Jack body height on the WeAct module.
- USB-C plug overmould of the cable you use.
- Glass + display stack thickness at the bezel.
- Which corner the ESP32 antenna sits in: keep brass inserts and screws at least 5 mm clear of it, or use a plastic screw there.

## Phases

| Phase | Output | Done when |
|---|---|---|
| 0 Measure | Filled measurement table, cell bought | All values above recorded in `docs/research/case-v5-measurements.md` |
| 1 Port | build123d parameter module + envelope model | Envelope overlaps match v4's zero-overlap set |
| 2 Design | Tub, chassis, lid, flexure, both variants | Geometry and printability checks pass |
| 3 Coupons | Coupon STLs, print, adjust | Button clicks, insert holds, lid seats |
| 4 Full print | v5 parts, renders, notes | Assembled with real cell; fit and feel accepted by the user |
| 5 Release | `releases/<version>/hardware/v5*`, notes | Packaged with checksums like v4 |

Case work goes on its own branch, not `cydpod/power`.
