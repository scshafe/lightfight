// Sun visor for one 38 kHz IR receiver module (TSOP38238 class, minicast
// package about 5.0 wide x 6.95 high). Source for the printable part; the
// dimensions and the reasoning are in docs/hardware.md, "Sun visors", and
// tools/visor_check.py reads the numbers below to check the shading.
//
// Axes: x forward (the receiver's axis), y sideways, z up. The receiver's
// front face is the plane x = 0, its bottom is z = 0, the leads go down.
//
// The brim is a flat roof over the face, longer than the face is high
// divided by tan(30 degrees), so every point of the face is in shadow from a
// sun more than 30 degrees above the horizon; it is widened by 45-degree
// fans on both sides so the shadow also holds for sun up to 45 degrees to
// the side. There are no side walls in front of the face and no tube, so
// the +-45 degree horizontal acceptance of the receiver is untouched.
//
// Print flat, roof up or on its back plate, 0.2 mm layers, 4 perimeters,
// opaque black PETG or ABS (check that your filament blocks 940 nm: bench
// check in docs/bench.md), then paint the inside matte black if it is shiny.

face_w   = 5.0;   // receiver width (datasheet)
face_h   = 6.95;  // receiver height (datasheet)
clear    = 0.2;   // clearance around the receiver
wall     = 1.2;   // wall and roof thickness
depth    = 5.0;   // pocket depth behind the face (module body)
cut_deg  = 30;    // elevations above this are shaded
side_deg = 45;    // horizontal acceptance kept on each side

win_w    = face_w + 2 * clear;          // pocket width
win_h    = face_h + clear;              // pocket height; roof sits on it
roof_len = win_h / tan(cut_deg) + 0.3;  // brim length forward of the face
half_w   = win_w / 2;                   // the fan starts at the face edges
roof_z   = win_h;                       // underside of the roof
tab_len  = 6;     // rear mounting tab
tab_w    = 18;
tab_t    = 1.6;
tie_w    = 3.0;   // zip-tie / thread slots in the tab
tie_l    = 1.8;

module fan(r, a) {
    // quarter-ish fan from 0 to a degrees, centred on the origin
    n = 12;
    polygon(concat([[0, 0]],
        [for (i = [0 : n]) [r * cos(a * i / n), r * sin(a * i / n)]]));
}

module roof2d() {
    hull() {
        translate([0,  half_w]) fan(roof_len, side_deg);
        translate([0, -half_w]) mirror([0, 1]) fan(roof_len, side_deg);
    }
}

module visor() {
    difference() {
        union() {
            // roof: the brim over the face and over the pocket behind it
            translate([0, 0, roof_z]) linear_extrude(wall) roof2d();
            translate([-depth - wall, -win_w / 2 - wall, roof_z])
                cube([depth + wall, win_w + 2 * wall, wall]);
            // back wall and side walls of the pocket (the face stays open)
            translate([-depth - wall, -win_w / 2 - wall, 0])
                cube([wall, win_w + 2 * wall, roof_z]);
            for (s = [-1, 1])
                translate([-depth - wall, s * (win_w / 2 + wall / 2) - wall / 2, 0])
                    cube([depth + wall, wall, roof_z]);
            // rear mounting tab
            translate([-depth - wall - tab_len, -tab_w / 2, 0])
                cube([tab_len + wall, tab_w, tab_t]);
        }
        // slots for a zip tie or thread through the tab
        for (s = [-1, 1])
            translate([-depth - wall - tab_len / 2 - tie_l / 2, s * (tab_w / 2 - 3) - tie_w / 2, -1])
                cube([tie_l, tie_w, tab_t + 2]);
        // lead notch in the back wall, low
        translate([-depth - wall - 1, -1.6, -1]) cube([wall + 2, 3.2, 3]);
    }
}

visor();
