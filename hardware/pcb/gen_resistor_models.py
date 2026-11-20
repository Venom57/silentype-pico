#!/usr/bin/env python3
"""gen_resistor_models.py: VRML 3D models of axial resistors with the correct colour code.

KiCad's R_Axial model is one plain body for every value, so the renders could not show
which resistor is which. This writes models/R_<value>.wrl for each value on the board:
a DIN0207 body (6.3 mm x 2.5 mm), 4-band 5 % colour code, leads bent down to pads 7.62 mm
apart, matching footprint R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal (pad 1 at the
origin, pad 2 at +X). Band 1 sits nearest pad 1.

VRML units in KiCad are 0.1 inch (2.54 mm); everything below is written in mm and scaled.
Render only: 3D models never reach the Gerbers.
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "models")
U = 1 / 2.54                      # mm -> VRML units (0.1 in)

COLOURS = {  # IEC 60062 digit colours, as diffuse RGB
    0: (0.06, 0.06, 0.06), 1: (0.45, 0.24, 0.10), 2: (0.80, 0.08, 0.08), 3: (0.95, 0.47, 0.05),
    4: (0.95, 0.85, 0.10), 5: (0.10, 0.60, 0.15), 6: (0.10, 0.25, 0.80), 7: (0.55, 0.20, 0.75),
    8: (0.50, 0.50, 0.50), 9: (0.95, 0.95, 0.95),
}
GOLD = (0.83, 0.68, 0.21)
BODY = (0.86, 0.76, 0.58)
LEAD = (0.75, 0.75, 0.78)

PITCH, BODY_L, BODY_D, LEAD_D = 7.62, 6.3, 2.5, 0.6
AXIS_Z = BODY_D / 2 + 0.3         # body rests just above the board
LEAD_DOWN = 3.2                   # lead length below the axis (through a 1.6 mm board)


def bands(ohms):
    """4-band code: two significant digits, multiplier, gold (5 %)."""
    exp = 0
    v = ohms
    while v >= 100:
        v /= 10
        exp += 1
    v = round(v)
    if v < 10:                    # values below 10 ohms are not used on this board
        raise ValueError(ohms)
    return [COLOURS[v // 10], COLOURS[v % 10], COLOURS[exp], GOLD]


SEG = 28                          # facets around each cylinder


def _shape(points, faces, rgb, shiny=0.3):
    """One VRML Shape as an IndexedFaceSet (KiCad's VRML reader skips primitive geometry)."""
    pts = ", ".join(f"{x * U:.4f} {y * U:.4f} {z * U:.4f}" for x, y, z in points)
    idx = " ".join(" ".join(str(v) for v in f) + " -1" for f in faces)
    return (f"Shape {{ appearance Appearance {{ material Material {{ diffuseColor {rgb[0]} {rgb[1]} {rgb[2]} "
            f"specularColor {shiny} {shiny} {shiny} shininess {shiny} }} }}\n"
            f"  geometry IndexedFaceSet {{ solid FALSE creaseAngle 0.8 coord Coordinate {{ point [ {pts} ] }}\n"
            f"    coordIndex [ {idx} ] }} }}\n")


def _cylinder(p0, p1, d, rgb, shiny=0.3):
    """Capped cylinder between points p0 and p1 (mm), axis along X or Z."""
    import math
    r = d / 2
    along_x = p0[0] != p1[0]
    ring = []
    for end in (p0, p1):
        for k in range(SEG):
            a = 2 * math.pi * k / SEG
            c, s_ = r * math.cos(a), r * math.sin(a)
            ring.append((end[0], end[1] + c, end[2] + s_) if along_x else (end[0] + c, end[1] + s_, end[2]))
    faces = [(k, (k + 1) % SEG, SEG + (k + 1) % SEG, SEG + k) for k in range(SEG)]
    faces.append(tuple(range(SEG - 1, -1, -1)))
    faces.append(tuple(range(SEG, 2 * SEG)))
    return _shape(ring, faces, rgb, shiny)


def cyl_x(x0, x1, d, rgb, z=AXIS_Z):
    """Cylinder along X from x0 to x1 (mm), diameter d, axis at height z."""
    return _cylinder((x0, 0, z), (x1, 0, z), d, rgb)


def cyl_z(x, z0, z1, d, rgb):
    """Vertical lead at x from z0 to z1."""
    return _cylinder((x, 0, z0), (x, 0, z1), d, rgb, shiny=0.6)


def model(ohms):
    x0 = (PITCH - BODY_L) / 2
    parts = [cyl_x(x0, x0 + BODY_L, BODY_D, BODY)]
    # band positions from the pad-1 end of the body (mm), 0.55 mm wide, slightly proud
    for (start, rgb) in zip((0.75, 1.55, 2.35, 4.95), bands(ohms)):
        parts.append(cyl_x(x0 + start, x0 + start + 0.55, BODY_D + 0.06, rgb))
    parts.append(cyl_x(0, x0 + 0.2, LEAD_D, LEAD))                     # lead stubs into the body
    parts.append(cyl_x(x0 + BODY_L - 0.2, PITCH, LEAD_D, LEAD))
    parts.append(cyl_z(0, AXIS_Z - LEAD_DOWN, AXIS_Z, LEAD_D, LEAD))   # bent down into the pads
    parts.append(cyl_z(PITCH, AXIS_Z - LEAD_DOWN, AXIS_Z, LEAD_D, LEAD))
    return "#VRML V2.0 utf8\n" + "".join(parts)


VALUES = {"47R": 47, "330R": 330, "1k": 1000, "4k7": 4700, "10k": 10000, "47k": 47000}


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, ohms in VALUES.items():
        with open(os.path.join(OUT, f"R_{name}.wrl"), "w") as f:
            f.write(model(ohms))
    print(f"wrote {len(VALUES)} resistor models -> {OUT}")


if __name__ == "__main__":
    main()
