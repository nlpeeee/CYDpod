"""CYDpod case v5 parameters.

Frame (mm): rear view of the JC2432W328 board. ESP32 module at -X, USB-C at +X,
PCB centre (0, 19). Z runs from the screen face (Z 0) toward the back.

Source tags follow docs/research/case-v5-measurements.md:
S = vendor datasheet/STEP, R = MakerWorld 594615 reference case, E = estimate
carried from the v4 generator (+-1 mm), V4 = queried from the v4 model,
D = design choice.
"""
from pathlib import Path

BOARD = dict(
    l=86.0, w=50.0, cy=19.0,          # S
    t=1.6,                            # E
    corner_r=1.6,                     # S/R
    z_front=5.2, z_back=6.8,          # R
    hole_d=3.2,                       # S
    holes=((-39.0, -2.0), (39.0, -2.0), (-39.0, 40.0), (39.0, 40.0)),  # S/R
)

# Rear-side component envelopes: (x0, x1, y0, y1, height above the PCB rear face). E.
COMPONENTS = {
    "ESP-WROOM-32": (-42.6, -17.3, 10.7, 28.2, 3.1),
    "USB-C": (35.7, 43.0, 19.4, 29.8, 3.3),
    "PWR-4P": (38.1, 43.0, 5.8, 15.4, 4.0),
    "TF": (-0.3, 15.5, 28.9, 44.0, 1.9),
    "P6": (-35.5, -25.1, 38.0, 44.0, 4.5),
    "CN1": (-24.1, -13.9, 38.0, 44.0, 4.5),
    "P3": (-12.9, -2.7, 38.0, 44.0, 4.5),
    "SPK-conn": (12.1, 17.7, -6.0, -2.0, 4.5),
    "BATT-conn": (28.9, 34.5, -6.0, -2.0, 4.5),
    "SW1": (21.5, 26.3, -6.0, -2.8, 2.0),
    "RESET": (-33.9, -30.9, -4.5, -0.7, 1.8),
    "BOOT": (-29.4, -26.4, -4.6, -0.8, 1.8),
    "FPC": (19.0, 25.5, 34.6, 41.6, 1.5),
}

# Extended IO JST housings: unused by CYDpod (wired I2S uses the RGB-LED pads GPIO 4/16/17,
# firmware/cydpod_player/app/board.h). Removing them lowers the cell. Owner decision.
EXT_IO = ("P6", "CN1", "P3")
BOARD_MODS = dict(remove_ext_io=False)
# Free space kept on every side of a connector, PCB to housing top, for a wire run. D (owner 2026-11-08).
WIRE_CLEAR = dict(P6=1.0)

# Vertical battery plug standing in BATT-conn (E, v4 envelope).
BATT_PLUG = (28.9, 34.5, -6.0, -2.0, 11.3, 14.3)

FRONT = dict(
    bezel_t=1.2,                                   # R
    glass=(-35.0, 36.0, -6.5, 44.5, 1.2, 2.4),     # R (x0,x1,y0,y1,z0,z1)
    window=(-33.0, 27.0, -4.5, 42.5),              # R, narrowest
)

MODULE = dict(
    step=Path(r"E:\dev\research\i2s-module\repo\Hardware\WeAct-I2SEarphoneModuleV1 3D.step"),  # S
    # v4 placement: x' = y + x0, y' = -x + y0, z' = z + face_z (rotate -90 about Z).
    x=-24.26, y=18.3, board_t=1.59144,                 # S/V4
    hole=(-39.0, -2.1), hole_d=3.2,                    # S (STEP): coaxial with the PCB hole (-39,-2) within 0.1
    jack_x=-29.6858, jack_above_face=2.653, jack_d=6.4,  # S (jack centre 15.753 at the v4 face Z 13.1)
    strip_headers=True,                                # STEP's straight header solids are dropped; HEADERS models the real ones
)
# Owner 2026-11-08: right-angle header soldered on the module's underside, its plastic resting on the
# ESP32 shield, pins and Dupont housings pointing +Y toward P6. E from the owner's photo (+-1 mm).
HEADERS = dict(plastic=2.5, x0=-43.6, x1=-24.8, y0=15.5, y1=37.0)
MODULE["face_z"] = BOARD["z_back"] + COMPONENTS["ESP-WROOM-32"][4] + HEADERS["plastic"] + MODULE["board_t"]
MODULE["jack_z"] = MODULE["face_z"] + MODULE["jack_above_face"]

CELL = dict(
    name="604050", t=6.0, w=40.0, l=50.0,  # D (owner 2026-11-07, both variants; a 604560 does not fit beside the audio module), nominal
    t_tol=0.2,      # D: thickness tolerance
    wrap=0.3,       # D: tape/sleeve allowance per face (plan only)
    pcm=2.0,        # D: protection-board fold at the lead end (+X)
    swell=0.5,      # D: swelling allowance above the cell
    foam=0.3,       # D: compressed foam between the cell and the lid
)

STACK = dict(
    clear=0.3,        # D: static clearance over the tallest part under the cell
    insulation=0.1,   # D: Kapton / fish paper
    lid=1.8,          # D: lid plate
    jack_clear=0.3,   # D: jack top to lid underside
)

CASE = dict(
    wall=1.6,                                  # D
    cavity_x=43.6,                             # D: PCB + 0.6 per side, lets the corner radius grow to 2.8
    cavity_y0=-7.75, cavity_y1=45.25,          # v4 + 1.0 at the top edge
    cavity_r=2.8,                              # D: tangent-clears the PCB R1.6 corners + 0.25
    wall_edge=1.4,                             # D: wall at the front chamfer and at the rim
    bulge=0.6,                                 # D: barrel bulge at mid-height (sine profile)
    front_chamfer=1.0,                         # D: 45 deg, bed side
    pocket_floor_z=2.4,                        # R
)

FASTENERS = dict(
    screw_d=3.0, screw_clear_d=3.4,            # M3 (owner 2026-11-07)
    head_d=6.0, head_h=1.2,                    # D: low/wafer head M3 (<= 6.0 x 1.2)
    head_gap=0.2,                              # D: internal screw head top below the lid underside (owner 2026-11-08)
    head_hole_d=6.8, collar_d=8.4, collar_h=2.4,  # D: chassis tube top: head pocket inside a collar joined to the plate
    btn_head_d=5.7, btn_head_h=1.65,           # ISO 7380, audio short corner
    spacer_od=6.0,                             # D: audio spacer between the PCB rear and the module
    insert_len=4.0, insert_hole_d=4.0,         # M3 x 4 short insert
    insert_floor_z=0.8,                        # D: hole bottom in the tub boss (bezel 0.8 below)
    boss_d=7.5,                                # D: tub seat boss
    tube_od=6.4,                               # D: chassis screw tube
)

# Snap-on lid (owner 2026-11-08: internal screws, back clips on). Cantilever arms hang from the lid
# underside just inside the tub wall; a 45/45 barb pops into a recess in the wall. D.
# Strain at the root while passing the rim: 1.5 * t * (barb - gap) / L^2 = 2.25 % (PETG).
CLIPS = dict(
    arm_t=1.2, arm_l=6.0, arm_w=8.0, gap=0.15, barb=0.6, gusset=0.8, recess_clear=0.1,
    # (wall, position along it): -Y/+Y walls take x, -X/+X walls take y
    at=(("-Y", -18.0), ("-Y", 6.0), ("+Y", 5.0), ("+Y", 28.0), ("+X", 8.0), ("-X", 26.0)),
)
PRY = dict(wall="+X", s0=13.0, s1=19.0, depth=1.0)   # D: fingernail notch in the tub rim beside the +X clip
LID = dict(edge_chamfer=1.0)                         # D: back edge, 45 deg, printed on the bed like the front

# TF card straight-path proxy (v4) and ports.
SD_TRAVEL = (1.6, 13.6, 43.9, 61.0, 7.1, 8.1)
USB = dict(y=24.6, z=8.45, w=12.4, h=7.0)
# gap: nub tip to the estimated actuator face. 0.15 jammed SW1 on the owner's prototype-1 print
# (2026-11-08), so the face sits further out than estimated; 1.15 covers the +-1 mm estimate. D.
SW1 = dict(x=23.9, z=7.8, face_y=-6.0, gap=1.15)   # E (position)

VARIANTS = ("audio", "no-audio")
