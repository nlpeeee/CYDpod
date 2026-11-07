# Case v5 Phase 0: dimensions and cell choice

2026-11-07. Phase 0 of [the case v5 plan](../specs/case-v5-plan.md). The owner chose not to take caliper measurements now. Dimensions are cribbed from a published case for this exact board and from the existing models. The Phase 3 coupon print is the physical check.

Source tags:
- **S**: vendor datasheet (`releases/0.5.0/hardware/JC2432W328-Specifications-EN.pdf`) or vendor STEP.
- **R**: reference case MakerWorld 594615 for the JC2432W328, measured from its STLs to ±0.25 mm (`E:\dev\research\jc2432w328\case\renders\ref\reference-measurements.md`). It is a published, printed design, so its board fit is the best evidence available without calipers.
- **E**: estimate from datasheet photos, carried in the v4 parameters (`E:\dev\research\jc2432w328\case\out\case-parameters.md`).
- **V4**: queried from the v4 assembled model `case-v4.blend` (vendor STEP tessellation).

Frame: rear view, mm. ESP32 module on the left (−X), USB-C on the right (+X). PCB centre (0,19). Z runs from the screen face (Z 0) toward the back.

## 1. Cell

**Superseded 2026-11-07: 604050 in both variants** (6 × 40 × 50, about 1500 mAh estimated; about 9.4 h at 0.5 W, 5.9 h at 0.8 W). The Phase 1 envelope showed a 604560 cannot fit beside the audio module (`docs/research/case-v5-phase1.md`), and the owner chose one cell for both. The original 604560 choice follows for the record: **604560, chosen by the owner 2026-11-07.** 6 × 45 × 60 nominal, about 2000 mAh (estimated by volume from the 103665's listed 3000 mAh). Estimated runtime: about 12.6 h at 0.5 W, 7.9 h at 0.8 W (assumes 85% conversion; the 0.8 W playback figure is not measured).

Design envelope until the real cell arrives:
- 6.0 + 0.2 thickness tolerance × 46 × 62 mm, including the protection board and lead exit.
- 0.5 mm swelling allowance and a 0.3 mm compressed foam pad.
- Lead exit on a short edge.

When buying, get a protection board fitted, a JST-PH 2.0 lead and a datasheet. Check polarity against the board socket before plugging in. Record the purchased model and its actual size here.

| Cell (for the record) | T × W × L | mAh | h @ 0.5 / 0.8 W |
|---|---|---:|---|
| 103665 (v4) | 10 × 36 × 65 | 3000 (listed) | 18.9 / 11.8 |
| **604560** | 6 × 45 × 60 | ~2000 | 12.6 / 7.9 |
| 504560 | 5 × 45 × 60 | ~1700 | 10.7 / 6.7 |

## 2. Board

| # | Item | Value used | Src |
|---|---|---|---|
| B1 | PCB outline | 86.0 × 50.0 | S |
| B2 | PCB corner radius | **1.6** (pocket corner 1.85 with clearance) | S/R. The reference pocket and v4 both assume it |
| B3 | Mounting-hole centres | (±39, −2) and (±39, 40): 78 × 42 pitch, 4.0 from the edges | S, R matches exactly |
| B4 | Mounting-hole Ø | **3.2** (datasheet "R 1.60"). The reference uses Ø3.0 locating holes, Ø4.0 lid holes and Ø6.0 counterbores: an M3-class fastener | S/R |
| B5 | PCB thickness | 1.6 | E (standard) |
| B6 | PCB front face / rear face | Z 5.2 / Z 6.8 | R (post-top seat 5.2) |
| B7 | Glass pocket | 71.0 × 51.0 (x −35…36, y −6.5…44.5), Z 1.2…2.4 | R |
| B8 | Screen window | narrowest 60.0 × 47.0 (x −33…27, y −4.5…42.5); 62.5 × 49.5 at the face (taper); 9 mm lip on the FPC (+X) side | R. Active area 57.6 × 43.2 (S) fits with 1.2 / 1.9 mm margin |
| B9 | Rear component heights above Z 6.8 | ICs ≤1.75, TF 1.9, ESP shield 3.1, USB-C 3.3, 1.25 mm JST connectors (Extended IO, 4P power, battery, speaker) **4.5** | E |
| B10 | ESP32 antenna | Left (−X) end, about x −43…−37, y 10…28. Nearest screws at (−39,−2) and (−39,40) are about 12 mm clear. In the audio variant the module (x −42.5…−24.3, y −7.2…18.3) overlaps this zone; RF untested | E (datasheet photo) |
| B11 | SW1 battery button | centre x 23.9, y −4.3, Z 7.8 (1.0 above the PCB rear); actuator face y −6.0 (flush with the PCB edge); needs ~0.3 mm travel at 1.6 N (160 gf) | E ±1.0 |
| B12 | USB-C | centre y 24.6, Z 8.45; body y 19.4…29.8. The 4P power connector beside it is y 5.8…15.4 | E/R |
| B13 | TF socket | x −0.3…15.5, y 28.9…44, height 1.9. Reference window x −0.25…15.0, Z 7.0…10.0 | R (v3 notes), R |
| B14 | Battery socket | 1.25 mm JST on the bottom (−Y) edge near x 30, 4.5 tall; v4 vertical-plug envelope x 28.9…34.5, y −6…−2, Z to 14.3 | E |
| B15 | RESET / BOOT | (−32.4, −2.6) and (−27.9, −2.7), 1.8 tall. Stay enclosed | E ±1.0 |

## 3. Audio module

| # | Item | Value used | Src |
|---|---|---|---|
| A1 | Module PCB | 25.50 × 18.24 × 1.59, at x −42.5…−24.26, y −7.2…18.3, component face Z 13.1 | S |
| A2 | Jack centre | x −29.69, Z 15.75 | S |
| A3 | Jack body top | **Z 18.21**, i.e. 5.1 above the module face; body y −9.75…4.25 (nose 2 mm past the inner cavity wall) | V4 |
| A4 | Header pins | Remove; solder wires to the pads (pins currently reach Z 21.3) | decision |

## 4. Thickness this gives

| Item | No-audio | Audio |
|---|---:|---:|
| PCB rear face | 6.8 | 6.8 |
| Highest part under the cell | 3.1 (ESP shield; cell centred) | 4.5 (JST) |
| Clearance + insulating sheet | 0.4 | 0.4 |
| Cell + tolerance | 6.2 | 6.2 |
| Swelling + foam | 0.8 | 0.8 |
| Lid | 1.8 | 1.8 |
| **Centre thickness** | **≈ 19.1** | **≈ 20.5** (jack top 18.21 + 0.3 + lid gives 20.3, so the cell stack decides) |

The no-audio figure assumes the cell clears the 4.5 mm JST connectors in plan. Phase 1 checks that against the component proxies.

## 5. Defaults where nothing is known (verify on the coupon print)

| # | Item | Default |
|---|---|---|
| C1 | USB-C plug overmould | 12.4 × 7.0 slot (v4/reference size), stadium corners, 0.5 mm lead-in. Fits common overmoulds up to about 12 × 6.5 |
| C2 | 3.5 mm plug shoulder | Ø6.4 bore (v4), 0.5 mm lead-in |
| C3 | Heat-set inserts | M3 × 4 short, typical OD 4.6–5.0; hole Ø4.0–4.2 to the maker's figure; boss Ø7.5 |
| C4 | Fits | 0.2 mm sliding, 0.15 mm lid in rebate, 0.25 mm PCB to wall |
| P1–P3 | Printer | 0.4 mm nozzle, 0.2 mm layers, PETG tub; ironing optional |

## Open

- **Fastener size: decided, M3** (owner, 2026-11-07). The PCB's Ø3.2 holes locate it on the M3 screws. Buy 4 × M3 × 18 button heads (ISO 7380), 1 × M3 × 6 for the audio corner, M3 × 4 short heat-set inserts (plus spares), and one M2 × 6 for the audio module.
- **Real cell.** Record its dimensions on arrival; the envelope above has margin for a typical 604560.
- **Estimates (E).** Everything tagged E is ±1 mm. The SW1 position matters most, so the button flexure coupon checks it before the full print.

Sources for cell capacities: [OpenELAB 103665 3000 mAh](https://openelab.io/products/openelab-103665-3000mah-polymer-lithium-battery), [Akyga LP604560 listing](https://www.elektronik.ropla.eu/en/magazyn/magazyn/?ic=AKY0962).
