"""Golden generator for tests/data/aosp_fling.csv (CY-96).

Independent reproduction of AOSP OverScroller.SplineOverScroller (table of 101 samples built by bisection,
linear interpolation, duration/distance formulas) at 143 ppi. Usage: python -I tools/gen_aosp_fling.py <out.csv>
Columns: v_pxs,t_ms,pos_px,vel_pxs,duration_ms,total_px   (one row per 8 ms plus the final instant).
"""
import math
import sys

PPI = 143.0
FRICTION = 0.015
DECEL = math.log(0.78) / math.log(0.9)
INFLEXION = 0.35
P1 = 0.5 * INFLEXION
P2 = 1.0 - 1.0 * (1.0 - INFLEXION)
NB = 100


def build_table():
    pos = [0.0] * (NB + 1)
    for i in range(NB):
        a = i / NB
        x_min, x_max = 0.0, 1.0
        while True:
            x = x_min + (x_max - x_min) / 2.0
            coef = 3.0 * x * (1.0 - x)
            tx = coef * ((1.0 - x) * P1 + x * P2) + x * x * x
            if abs(tx - a) < 1e-5:
                break
            if tx > a:
                x_max = x
            else:
                x_min = x
        pos[i] = coef * ((1.0 - x) * 0.5 + x) + x * x * x
    pos[NB] = 1.0
    return pos


TABLE = build_table()


def fling(v):
    coeff = 9.80665 * 39.37 * PPI * 0.84
    l = math.log(INFLEXION * abs(v) / (FRICTION * coeff))
    dur = 1000.0 * math.exp(l / (DECEL - 1.0))
    dist = FRICTION * coeff * math.exp(DECEL / (DECEL - 1.0) * l)
    return dur, dist


def sample(v, t_ms, dur, dist):
    t = min(t_ms / dur, 1.0)
    i = min(int(NB * t), NB - 1)
    d_a = i / NB
    d_b = (i + 1) / NB
    p_a, p_b = TABLE[i], TABLE[i + 1]
    slope = (p_b - p_a) / (d_b - d_a)
    coef = p_a + (t - d_a) * slope
    if t_ms >= dur:
        return dist, 0.0
    return coef * dist, slope * dist / dur * 1000.0


def main(out):
    with open(out, "w", newline="\n") as f:
        f.write("v_pxs,t_ms,pos_px,vel_pxs,duration_ms,total_px\n")
        for v in (100, 200, 500, 1000, 2000, 4000, 7000):
            dur, dist = fling(v)
            ts = list(range(0, int(dur), 8)) + [int(math.ceil(dur))]
            for t in ts:
                p, vel = sample(v, t, dur, dist)
                f.write("%d,%d,%.3f,%.3f,%.3f,%.3f\n" % (v, t, p, vel, dur, dist))


if __name__ == "__main__":
    main(sys.argv[1])
