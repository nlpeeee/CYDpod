# Printing and assembling the CYDpod v5 case (prototype 1)

Files: `hardware/case/out/print-ready/` (generated; rebuild with `build_parts.py`). Every STL is already in print orientation: drop it on the plate and don't rotate it. Checksums are in `SHA256SUMS.txt` beside them.

| File | Part | Bambu A1 estimate (0.20 mm Standard, Bambu PETG Basic) |
|---|---|---|
| `CYDpod-case-v5-noaudio-Coupon.stl` | **Print this first:** button flexure + one insert corner | ~20 min, 2 g |
| `CYDpod-case-v5-noaudio-Tub.stl` | Front shell (screen face down) | ~31 min, 14 g |
| `CYDpod-case-v5-noaudio-Chassis.stl` | Inner frame: battery cradle + screw tubes (lid-side face down) | ~20 min, 5 g |
| `CYDpod-case-v5-noaudio-Lid.stl` | Back (inside face down) | ~17 min, 10 g |
| `CYDpod-case-v5-audio-*.stl` | The same set for the WeAct headphone-module build, plus `Spacer` | Tub 31 / Chassis 18 / Lid 17 / Spacer 2 min |

The estimates come from slicing each STL with Bambu Studio's command line using the stock A1 0.4 mm nozzle, 0.20 mm Standard and Bambu PETG Basic profiles. Each sliced with no warnings and no supports.

## Slicer settings

- **Material:** PETG for the tub, because the power button is a printed spring and PLA creeps and fatigues. The chassis and lid can be PETG or ASA.
- **Supports:** off. Nothing needs them; every overhang is ≤ 45° or a bridge of ≤ 20 mm, supported at both ends.
- **Walls:** 3 or more (the shell walls are 1.4–2.0 mm). Infill: 15–20% gyroid.
- **Plate:** textured PEI. The tub's screen face and the lid's inside face sit on it.
- **Lid (optional):** turn on ironing (top surfaces only) for a smooth back.
- **Seam:** aligned or back. Avoid "nearest" on the tub so the seam stays off the button side.

## What to buy

| Item | No-audio | Audio |
|---|---:|---:|
| 604050 LiPo (6 × 40 × 50 mm) with protection board and JST-PH 2.0 lead. **Check polarity against the board's battery socket before plugging in.** | 1 | 1 |
| M3 × 4 short brass heat-set inserts (hole Ø4.0) | 4 | 5 |
| M3 × 18 low/wafer-head screws (head ≤ Ø6.0 × 1.2 mm) | 4 | 3 |
| M3 × 12 button-head screw (module corner) | — | 1 |
| M3 × 6 low/wafer-head screw (fourth lid screw, into the chassis) | — | 1 |
| Kapton tape or fish paper (~60 × 50 mm); 0.5 mm closed-cell foam (above and below the cell) | ✓ | ✓ |
| Small foam pad or Kapton between the module's underside and the ESP32 shield | — | ✓ |

ISO 7380 button heads (1.65 mm tall) also work for the M3 × 18s, but they stand about 0.4 mm proud of the lid.

## Test the coupon first

The coupon is the tub's bottom-right corner: the power button, the insert boss at the (39, −2) PCB hole, and the corner wall.
1. Press an M3 × 4 insert into the boss with a soldering iron (about 230 °C for PETG). It should sit flush with the boss top.
2. Lay the board on the boss, screen down, and screw that corner down with a short M3. The button nub should sit next to SW1, the battery button.
3. Press the oval pad. You should feel SW1 click with about 4 N of force (roughly 400 gf). The nub's position comes from photo estimates (±1 mm), so check that it actually lands on the switch.
4. Check the button feels right and the board edge clears the nub. If the nub misses, the fix is `SW1` in `cydcase/params.py`.

## Assembly

1. Press the inserts into the tub's four bosses (audio: also into the chassis boss by the left edge).
2. Lay the board in the tub, screen down, on the four bosses.
3. **Audio only:**
   - Solder the module's wires to its pads (header pins removed).
   - Stand the spacer on the PCB hole at the ESP32 end, beside the module.
   - Lower the module about 2 mm away from the wall, then slide it toward the wall so the jack nose enters its hole.
   - Fix the module, spacer and PCB to the insert with the M3 × 12.
4. Put Kapton or fish paper and thin foam over the components under the cell. Place the cell and plug it in.
5. Drop the chassis in: its cradle goes round the cell and its tubes stand on the board's corner holes.
6. Put 0.5 mm foam on the cell and fit the lid. Screw the M3 × 18s through the lid, chassis tubes and PCB into the inserts. Audio: also fit the M3 × 6 into the chassis insert.

## Known limits of prototype 1

- Component positions are estimates carried from v4 (±1 mm). The ones to check are the SW1 position, the USB-C height and the TF socket.
- Glass and bezel geometry are copied from the reference case for this board. Fit isn't verified.
- The TF card sits about 3 mm inside the wall. Use a fingernail or a SIM tool; the scoop under the slot helps you grab an ejected card.
- The slanted button slot leaves 45° edges on the outside. They're cosmetic.
- Audio: the jack hole reaches the rim, so a 4.5 mm notch at the top of the jack is closed by the lid.
- P6 has 1 mm of free space on every side for wires. The cell overhangs P6's corner with only 0.4 mm above the housing, so don't route wires over the top.
- Antenna performance with the audio module over the ESP32 hasn't been tested.
