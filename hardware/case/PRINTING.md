# Printing and assembling the CYDpod v5 case (prototype 1)

Files: `hardware/case/stl/` (committed; `build_parts.py` regenerates them). Every STL is already in print orientation: drop it on the plate and don't rotate it. Checksums are in `SHA256SUMS.txt` beside them.

| File | Part | Bambu A1 estimate (0.20 mm Standard, Bambu PETG Basic) |
|---|---|---|
| `CYDpod-case-v5-noaudio-Coupon.stl` | **Print this first:** button flexure + one insert corner | ~20 min, 2 g |
| `CYDpod-case-v5-noaudio-Tub.stl` | Front shell (screen face down) | ~31 min, 14 g |
| `CYDpod-case-v5-noaudio-Chassis.stl` | Inner frame: battery cradle + screw tubes (lid-side face down) | ~20 min, 5 g |
| `CYDpod-case-v5-noaudio-Lid.stl` | Back: snap-on, back face down, clips pointing up | not re-sliced |
| `CYDpod-case-v5-noaudio-CouponClipTub.stl` + `…CouponClipLid.stl` | **Snap test:** the tub's USB-end corner (one clip recess + pry notch) and the matching lid corner with its clip | small |
| `CYDpod-case-v5-audio-*.stl` | The same set for the WeAct headphone-module build, plus `Spacer` | Tub 31 / Chassis 18 / Lid 17 / Spacer 2 min |

The estimates come from slicing each STL with Bambu Studio's command line using the stock A1 0.4 mm nozzle, 0.20 mm Standard and Bambu PETG Basic profiles. Each sliced with no warnings and no supports.

## Slicer settings

- **Material:** PETG for the tub, because the power button is a printed spring and PLA creeps and fatigues. The chassis and lid can be PETG or ASA.
- **Supports:** off. Nothing needs them; every overhang is ≤ 45° or a bridge of ≤ 20 mm, supported at both ends.
- **Walls:** 3 or more (the shell walls are 1.4–2.0 mm). Infill: 15–20% gyroid.
- **Plate:** textured PEI. The tub's screen face and the lid's back face sit on it, so both outsides get the texture.
- **Lid:** PETG. The six clips are springs that bend across the layers; print them at full wall count (3+ walls makes them solid).
- **Seam:** aligned or back. Avoid "nearest" on the tub so the seam stays off the button side.

## What to buy

| Item | No-audio | Audio |
|---|---:|---:|
| 604050 LiPo (6 × 40 × 50 mm) with protection board and JST-PH 2.0 lead. **Check polarity against the board's battery socket before plugging in.** | 1 | 1 |
| M3 × 4 short brass heat-set inserts (hole Ø4.0) | 4 | 4 |
| M3 × 16 low/wafer-head screws (head ≤ Ø6.0 × 1.2 mm), internal: chassis → PCB → insert | 4 | 3 |
| M3 × 12 button-head screw (module corner) | — | 1 |
| Kapton tape or fish paper (~60 × 50 mm); 0.5 mm closed-cell foam (above and below the cell) | ✓ | ✓ |
| Small foam pad or Kapton between the module's underside and the ESP32 shield | — | ✓ |

The screw heads sit in the chassis tube tops, 0.2 mm under the lid. ISO 7380 button heads (1.65 mm) are too tall there.

## Test the coupon first

The coupon is the tub's bottom-right corner: the power button, the insert boss at the (39, −2) PCB hole, and the corner wall.
1. Press an M3 × 4 insert into the boss with a soldering iron (about 230 °C for PETG). It should sit flush with the boss top.
2. Lay the board on the boss, screen down, and screw that corner down with a short M3. The button nub should sit next to SW1, the battery button.
3. Press the oval pad. You should feel SW1 click with about 4 N of force (roughly 400 gf). The nub's position comes from photo estimates (±1 mm), so check that it actually lands on the switch.
4. Check the button feels right and SW1 isn't held down at rest. Prototype 1's nub held SW1 down, so the nub now stands 1 mm further back; expect a little free travel before the click. If SW1 is still held down, raise `SW1["gap"]` in `cydcase/params.py`. If the pad bottoms out without a click, lower it.

## Test the snap fit

Print `CouponClipTub` and `CouponClipLid` and press them together. The lid corner should click in and stay put, and lift off with a fingernail in the pry notch. If it's too loose or too tight, the barb depth is `CLIPS["barb"]` in `cydcase/params.py` (0.6 mm, 0.45 mm of it into the wall).

## Assembly

1. Press the inserts into the tub's four bosses (audio: also into the chassis boss by the left edge).
2. Lay the board in the tub, screen down, on the four bosses.
3. **Audio only:**
   - Solder a right-angle header to the module's **underside**, pins pointing away from the jack, and clip its solder tails flush on the top side. Put Kapton on the ESP32 shield: the header plastic rests on it.
   - Fit the Dupont leads to the header pins (GND/BCK/DIN/WS to P6; VCC from 3V3).
   - Stand the spacer on the PCB hole at the ESP32 end, beside the module.
   - Lower the module about 2 mm away from the wall, then slide it toward the wall so the jack nose enters its hole.
   - Fix the module, spacer and PCB to the insert with the M3 × 12.
4. Put Kapton or fish paper and thin foam over the components under the cell. Place the cell and plug it in.
5. Drop the chassis in: its cradle goes round the cell and its tubes stand on the board's corner holes.
6. Screw the M3 × 16s down through the chassis tube tops and the PCB into the inserts (audio: three of them; the fourth corner is the module's M3 × 12).
7. Put 0.5 mm foam on the cell and press the lid on until all six clips click. To open it, put a fingernail in the notch in the rim at the USB end and lift.

## Known limits of prototype 1

- Component positions are estimates carried from v4 (±1 mm). The ones to check are the SW1 position, the USB-C height and the TF socket.
- Glass and bezel geometry are copied from the reference case for this board. Fit isn't verified.
- The TF card sits about 3 mm inside the wall. Use a fingernail or a SIM tool; the scoop under the slot helps you grab an ejected card.
- The slanted button slot leaves 45° edges on the outside. They're cosmetic.
- Audio: the jack hole reaches the rim, so a 4.5 mm notch at the top of the jack is closed by the lid. The nearest clip is 12 mm from the jack, on the same wall.
- The back is held only by the six clips. Their feel is untested; print the snap coupon first.
- P6 has 1 mm of free space on every side for wires. The cell overhangs P6's corner with only 0.4 mm above the housing, so don't route wires over the top.
- The audio case is 21.2 mm thick (no-audio 20.5): the module sits on its header plastic. Header and Dupont positions come from a photo (±1 mm).
- Antenna performance with the audio module over the ESP32 hasn't been tested.
