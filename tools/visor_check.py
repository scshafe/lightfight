#!/usr/bin/env python3
"""Checks the sun visor's shading geometry (hardware/visor.scad).

It reads the numbers at the top of the .scad file, rebuilds the brim and traces
rays from a grid of points on the receiver's face:

  * from any sun more than `cut_deg` above the horizon and up to `side_deg`
    to either side, every point of the face is in the brim's shadow;
  * for rays at elevation 0 the whole face is open (nothing in front of it)
    and it stays open sideways to +-`side_deg`;
  * at 15 degrees at least half of the face is still lit (the brim is a
    cut-off, not a tube).

OpenSCAD is not needed. Exit 0 when all hold, 1 otherwise.
"""
import math
import re
import sys
from pathlib import Path

SCAD = Path(__file__).resolve().parent.parent / "hardware" / "visor.scad"


def params(text):
    """Evaluate the simple `name = expr;` lines at the top of the scad file."""
    env = {"tan": lambda d: math.tan(math.radians(d))}
    for m in re.finditer(r"^(\w+)\s*=\s*(.+?);\s*(?://.*)?$", text, re.M):
        name, expr = m.group(1), m.group(2).strip()
        try:
            env[name] = eval(expr, {"__builtins__": {}}, env)
        except Exception:
            pass
    return env


def hull(points):
    pts = sorted(set(points))
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def roof_polygon(p):
    pts = []
    n = 12
    for i in range(n + 1):
        a = math.radians(p["side_deg"] * i / n)
        r = p["roof_len"]
        pts.append((r * math.cos(a), p["half_w"] + r * math.sin(a)))
        pts.append((r * math.cos(a), -(p["half_w"] + r * math.sin(a))))
    pts += [(0, p["half_w"]), (0, -p["half_w"])]
    return hull(pts)


def inside(poly, pt, eps=1e-9):
    x, y = pt
    for i in range(len(poly)):
        ax, ay = poly[i]
        bx, by = poly[(i + 1) % len(poly)]
        if (bx - ax) * (y - ay) - (by - ay) * (x - ax) < -eps:
            return False
    return True


def lit_fraction(p, poly, elev, az, grid=15):
    """Fraction of face points whose ray to the sun misses the brim."""
    e, a = math.radians(elev), math.radians(az)
    lit = total = 0
    for iy in range(grid):
        y = -p["face_w"] / 2 + p["face_w"] * (iy + 0.5) / grid
        for iz in range(grid):
            z = p["face_h"] * (iz + 0.5) / grid
            total += 1
            if elev <= 0:
                lit += 1
                continue
            t = (p["roof_z"] - z) / math.sin(e)  # the ray reaches the roof underside
            x_hit = t * math.cos(e) * math.cos(a)
            y_hit = y + t * math.cos(e) * math.sin(a)
            if not (x_hit >= 0 and inside(poly, (x_hit, y_hit))):
                lit += 1
    return lit / total


def check(text=None):
    p = params(SCAD.read_text() if text is None else text)
    for k in ("face_w", "face_h", "roof_len", "half_w", "roof_z", "cut_deg", "side_deg"):
        if k not in p:
            return [f"parameter {k} not found in {SCAD.name}"], False
    poly = roof_polygon(p)
    lines, ok = [], True

    worst = 0.0
    cut, side = int(p["cut_deg"]), int(p["side_deg"])
    for elev in [cut, cut + 1, cut + 3, cut + 10, cut + 20, 70, 90]:
        for az in sorted(set(range(-side, side + 1, 5)) | {-side, side}):
            worst = max(worst, lit_fraction(p, poly, elev, az, grid=9))
    good = worst == 0.0
    lines.append(f"sun above {p['cut_deg']:.0f} deg, up to +-{p['side_deg']:.0f} deg to the side: "
                 f"lit fraction of the face {worst:.3f} ({'ok' if good else 'FAIL, light reaches the face'})")
    ok &= good

    f0 = min(lit_fraction(p, poly, 0, az) for az in range(-int(p["side_deg"]), int(p["side_deg"]) + 1))
    good = f0 == 1.0
    lines.append(f"horizon, +-{p['side_deg']:.0f} deg: face fully open {f0:.2f} ({'ok' if good else 'FAIL'})")
    ok &= good

    f15 = lit_fraction(p, poly, 15, 0)
    good = f15 >= 0.5
    lines.append(f"15 deg elevation, on axis: {f15:.2f} of the face lit ({'ok' if good else 'FAIL, brim too long'})")
    ok &= good

    # the brim is not absurdly large
    wide = max(abs(y) for _, y in poly) * 2
    good = p["roof_len"] <= 14 and wide <= 30
    lines.append(f"brim {p['roof_len']:.1f} mm long, {wide:.1f} mm wide ({'ok' if good else 'FAIL, over 14 x 30 mm'})")
    ok &= good
    return lines, ok


if __name__ == "__main__":
    lines, ok = check()
    print("\n".join(lines))
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)
