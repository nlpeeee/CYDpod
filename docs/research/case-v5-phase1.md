# Case v5 Phase 1: envelope model and checks

2026-11-07, branch `cydpod/case-v5`. Phase 1 of `docs/specs/case-v5-plan.md`: port the v4 board data to build123d and check the v5 stack (604560 cell, M3 screw stack, chassis tubes, stripped audio-module headers) before any printed part exists.

## What was built

`hardware/case/` (see its README). The envelope holds:
- the PCB with its Ø3.2 holes;
- the 13 v4 component envelopes and the vertical battery plug;
- the WeAct module from the vendor STEP, rotated and placed as in v4, with its one header solid stripped (233 of 234 solids kept). Placed bbox: x −42.5…−24.26, y −9.75…18.3, Z 10.95…18.21, matching v4;
- the cell (plan size + 0.3 mm wrap per side + 2 mm protection-board fold, thickness + 0.2 mm tolerance) with a 0.5 mm swelling + 0.3 mm foam zone above it;
- M3 shank keep-outs (Ø3.4) from the insert floor to the lid top, and Ø6.4 chassis tubes from the PCB rear to the lid. In the audio variant the corner under the module gets a short screw and a head keep-out instead;
- the v4 SD-card travel path and the cavity (v4 plan + 1 mm at the top edge).

The cell base is computed: tallest part under the cell footprint + 0.3 mm clearance + 0.1 mm insulation.

## Results

Log: `.tools/logs/case-envelope*.log`.

| Variant | Cell | Extended IO JSTs | Cell base set by | Thickness | Result |
|---|---|---|---|---:|---|
| no-audio | 604560 | kept | P6 (4.5 mm) | 20.5 | `CASE_V5_ENVELOPE_PASS no-audio` |
| no-audio | 604560 | removed | ESP shield (3.1 mm) | **19.1** | `CASE_V5_ENVELOPE_PASS no-audio` |
| audio | 604560 | kept or removed | — | — | `CASE_V5_ENVELOPE_FAIL audio` |
| audio | 604050 | kept | CN1 (4.5 mm) | 20.5 | PASS |
| audio | 604050 | removed | jack top 18.21 | **20.3** | PASS |
| audio | 603450 | either | jack top 18.21 | 20.3 | PASS |

Footprint 89.7 × 56.2 mm (plan corner radius still to be raised in Phase 2).

### Why the audio variant fails with a 604560

The cell needs 62.6 mm (60 + 2 mm protection-board fold + 2 × 0.3 wrap). Between the module (x −24.26, +0.3 clearance) and the right-hand screw tubes (x 35.8, −0.3) there is 59.5 mm; with bare screws and no tubes, 61.2 mm. It overlaps the shanks and tubes at (39,−2) and (39,40) by up to 85 mm³. v4 fit its 103665 only because that cell was 36 mm wide and passed between the screws at y −2 and y 40. A footprint change does not help: the screws go through the PCB's fixed corner holes.

Cells that fit beside the module are ≤ 52.6 mm long including the fold (a 50 mm body). The 604050 (6 × 40 × 50, about 1500 mAh estimated by volume) is the best common fit. A narrow cell ≤ 34 mm wide could run past the screws, but common sizes in that width are 50 mm long anyway.

### Extended IO connectors

P6, CN1 and P3 are 4.5 mm JST housings along the top edge. A 45 mm-wide cell cannot avoid them. CYDpod does not use them: wired I2S comes from the RGB-LED pads, GPIO 4/16/17 (`firmware/cydpod_player/app/board.h:98-100`). Removing the three housings (hot air, or cut and clean the pins) lowers the no-audio case by 1.4 mm. The audio case is jack-limited at 20.3 mm either way.

## Limits

- Component envelopes are v4 estimates (±1 mm). The checks prove the nominal model only; the Phase 3 coupons are the physical check.
- The jack nose and the SD path leave the cavity on purpose; the screws go into the bosses on purpose. Their wall openings come in Phase 2.
- Owner decisions pending: none.

## Decisions (owner, 2026-11-07)

- **604050 in both variants.** One cell to buy. The no-audio case gains no thickness over the 604560 and gives up about 25% of the runtime.
- **Extended IO housings kept.** No board rework.

A 604050 cannot avoid the 4.5 mm JSTs in either variant: free space between the bottom connector row (y ≤ −2) and the top row (y ≥ 38) is 40.0 mm against a 40.6 mm wrapped cell, and the rows leave no x-window for a 52.6 mm cell.

| Variant | Cell footprint (x0, x1, y0, y1) | Cell base | Thickness | Screw grip (head seat to insert floor) | Result |
|---|---|---|---:|---|---|
| audio | −23.96, 28.64, −1.6, 39.0 | Z 11.7 (CN1) | 20.5 | 19.0 × 3, short corner 7.1 | `CASE_V5_ENVELOPE_PASS audio` |
| no-audio | −26.3, 26.3, −1.6, 39.0 | Z 11.7 (P6) | 20.5 | 19.0 × 4 | `CASE_V5_ENVELOPE_PASS no-audio` |

Log: `.tools/logs/case-envelope-604050.log`. Footprint 89.7 × 56.2 × 20.5 mm. Screws: M3 × 18 (tip 1 mm above the insert floor), M3 × 6 at the audio short corner.
