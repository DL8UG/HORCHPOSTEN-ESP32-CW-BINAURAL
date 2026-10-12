// Horchposten - enclosure for the ESP32-Audio-Kit V2.2 (ESP32-A1S)
//
// Parametric: all board positions are in the block below. The start
// values are estimates; measure your board (see README.md, "Measuring")
// and correct them before the first print.
//
// Parts (select with -D part="..." on the command line):
//   bottom   shell with standoffs and wall openings
//   lid      top plate with key guides, labels and screw tubes
//   buttons  six key plungers
//   assembly preview of everything with a board dummy (default)
//
// Coordinates: x/y in mm on the board, origin = front left PCB corner
// seen from the top, front edge = y 0 (keys side).

part = "assembly";

/* ---------- board (MEASURE) ---------- */
// Start values from a product photo with dimensions (83 x 68 mm); the
// layout (keys in front, jacks right, USB left, antenna out at the back)
// is checked on a V2.2 A618 board, the millimetres are not.
pcb = [83, 68];            // PCB size x, y
pcb_t = 1.6;               // PCB thickness
holes = [[3.3, 3.3], [79.7, 3.3], [3.3, 64.7], [79.7, 64.7]];  // mounting holes
hole_d = 3.2;              // mounting hole diameter
under_h = 4.0;             // room under the PCB (solder pins, battery connector)
over_h = 11.0;             // room above the PCB (tallest part + margin)
// the antenna end of the ESP32-A1S module sticks out over the back edge:
// [x from, x to, how far out]
antenna = [33.5, 48.5, 6.0];

// one row of eight keys along the front edge: RST, BOOT, KEY1..KEY6
key_pitch = 8.5;
key_row = [10.9, 4.0];     // centre of RST; the others follow at key_pitch
key_top = 5.0;             // height of the switch actuator top above the PCB
keys = [for (i = [2:7]) [key_row[0] + i * key_pitch, key_row[1]]];   // KEY1..KEY6
// short / long press function, printed on the lid
key_labels = [["MODE", "L/R"], ["FILTER", "AGC"], ["PITCH-", "WIDTH"],
              ["PITCH+", "AUTO"], ["VOL-", "GAIN"], ["VOL+", "MUTE"]];

// RST and BOOT: holes for a paper clip, no plungers (BOOT stops the codec
// clock while it is held)
pin_holes = [key_row, [key_row[0] + key_pitch, key_row[1]]];
// status LED (GPIO22): hole for a piece of clear filament as light pipe
led = [70, 58];

// wall openings: [label, side, position along the side, centre height above
// the PCB top, width, height, round?]; side: "left" (x 0), "right", "front"
// (y 0), "back"; label "mic", "phones" and "power" draw a symbol, anything
// else is printed as text
ports = [
    ["mic",    "right", 44.2, 2.0, 7.0, 9.0, false],    // LINE IN; 2 mm extra
    ["phones", "right", 55.2, 2.0, 7.0, 9.0, false],    // room below the plug
    ["power",  "left",  21.7, 1.8, 11.0, 6.5, false],
    ["UART",   "left",  35.9, 1.8, 11.0, 6.5, false],
];

/* ---------- enclosure ---------- */
clear = 0.6;               // gap PCB to wall
wall = 2.2;
floor_t = 2.0;
lid_t = 2.4;
corner_r = 4;
screw_d = 3.4;             // M3 clearance in the lid
screw_head_d = 6.2;        // counterbore for M3 button head screws (ISO 7380:
screw_head_h = 1.8;        // head 5.7 x 1.65 mm), sunk a little below the top
insert_d = 4.0;            // M3 heat-set insert (or 2.6 for self-tapping screws)
standoff_d = 7;
foot_d = 10.5;             // recesses for self-adhesive rubber feet (10 mm)
foot_h = 0.8;
foot_inset = 14;           // foot centre from the outer edges; clear of the standoffs
key_hole_d = 6.6;          // lid guide for the plungers
plunger_d = 6.0;           // 0.3 mm play on each side in the guide
plunger_flange_d = 7.6;    // < key_pitch: neighbouring flanges must not touch
plunger_flange_h = 1.2;
plunger_above = 9.6;       // how far the plungers stand above the lid
tube_press = 0.2;          // lid tubes this much longer than the room above the PCB
engrave = 0.6;             // depth of the lid and wall lettering
port_label_h = 5.0;        // wall labels and symbols, smaller where the wall is too low
sym_stroke = 0.8;          // line width of the wall symbols
font = "Liberation Sans:style=Bold";

// later stage: window for a display in the lid
display_window = false;
display_pos = [20, 30];    // front left corner of the window on the board
display_size = [42, 32];

/* ---------- derived ---------- */
inner = [pcb[0] + 2 * clear, pcb[1] + 2 * clear + antenna[2]];   // room for the antenna
outer = [inner[0] + 2 * wall, inner[1] + 2 * wall];
pcb_z = floor_t + under_h;                 // PCB bottom
pcb_top = pcb_z + pcb_t;
shell_h = pcb_top + over_h;                // bottom shell height = lid underside
off = [wall + clear, wall + clear];        // PCB origin in shell coordinates
// the back feet move forward with the antenna room, in line with the board
feet = [for (x = [foot_inset, outer[0] - foot_inset],
             y = [foot_inset, outer[1] - foot_inset - antenna[2]]) [x, y]];

// a foot recess under a standoff would leave almost no floor under the insert
assert(plunger_flange_d < key_pitch - 0.5, "plunger flanges touch: lower plunger_flange_d");
for (f = feet, h = holes)
    assert(norm(f - (off + h)) >= (foot_d + standoff_d) / 2 + 0.5,
           str("foot at ", f, " runs into the standoff at ", off + h, ": raise foot_inset"));

$fn = 48;

module rounded_box(size, r, h) {
    linear_extrude(h)
        offset(r) offset(-r) square(size);
}

module at_board(p) { translate([off[0] + p[0], off[1] + p[1], 0]) children(); }

module port_cut(p) {
    side = p[1]; pos = p[2]; zc = pcb_top + p[3]; w = p[4]; h = p[5];
    // cut through the wall, a bit into the inside
    len = wall + clear + 2;
    module shape() {
        if (p[6]) cylinder(d = w, h = len);
        // slot, rounded on its short sides; x along the wall, y upwards
        else hull() for (s = [-1, 1])
            translate(w >= h ? [s * (w - h) / 2, 0, 0] : [0, s * (h - w) / 2, 0]) cylinder(d = min(w, h), h = len);
    }
    if (side == "left")
        translate([-1, off[1] + pos, zc]) rotate([0, 90, 0]) rotate([0, 0, 90]) shape();
    else if (side == "right")
        translate([outer[0] + 1, off[1] + pos, zc]) rotate([0, -90, 0]) rotate([0, 0, 90]) shape();
    else if (side == "front")
        translate([off[0] + pos, -1, zc]) rotate([-90, 0, 0]) shape();
    else
        translate([off[0] + pos, outer[1] + 1, zc]) rotate([90, 0, 0]) shape();
}

// wall symbols, 2D, centred on x = 0, standing on y = 0, h high
module sym_power(h, s = sym_stroke) {
    r = (h - s) / 2;                          // centre line of the ring
    translate([0, h / 2]) {
        difference() {
            difference() { circle(r = r + s / 2); circle(r = r - s / 2); }
            // gap at the top for the bar
            polygon([[0, 0], [-h * sin(38), h * cos(38)], [h * sin(38), h * cos(38)]]);
        }
        translate([-s / 2, -r * 0.15]) square([s, h / 2 + r * 0.15]);
    }
}

module sym_phones(h, s = sym_stroke) {
    ro = h / 2;
    // headband
    translate([0, h - ro]) intersection() {
        difference() { circle(r = ro); circle(r = ro - s); }
        translate([-ro, 0]) square([2 * ro, ro]);
    }
    // ear cups, the legs of the band run into them
    for (m = [0, 1]) mirror([m, 0]) {
        translate([ro - s, 0.4 * h]) square([s, 0.6 * h - ro + 0.01]);
        translate([ro - 0.3 * h, 0]) offset(r = 0.3) offset(delta = -0.3) square([0.3 * h, 0.45 * h]);
    }
}

module sym_mic(h, s = sym_stroke) {
    ri = 2 * s;                               // inner radius of the holder
    yu = s + 0.3 + ri + s;                    // holder centre above foot and stem
    // capsule, its lower end concentric with the holder
    hull() { translate([0, yu]) circle(r = s); translate([0, h - s]) circle(r = s); }
    // U-shaped holder, arms a little above its centre
    difference() {
        union() {
            translate([0, yu]) circle(r = ri + s);
            translate([-ri - s, yu]) square([2 * (ri + s), 0.4]);
        }
        translate([0, yu]) circle(r = ri);
        translate([-ri, yu]) square([2 * ri, h]);
        translate([-ri - s - 1, yu + 0.4]) square([2 * (ri + s) + 2, h]);
    }
    // stem and foot
    translate([-s / 2, s - 0.01]) square([s, yu - ri - s - s + 0.02]);
    translate([-1.5 * s, 0]) square([3 * s, s]);
}

module port_label_2d(name, h) {
    if (name == "power") sym_power(h);
    else if (name == "phones") sym_phones(h);
    else if (name == "mic") sym_mic(h, min(sym_stroke, (h - 1) / 5));   // thinner when small
    else text(name, size = 0.95 * h, font = font, halign = "center");
}

// label below a port on the outside of the wall, smaller if the room down
// to the floor is too low
module port_label(p) {
    side = p[1]; pos = p[2]; zc = pcb_top + p[3];
    room_down = zc - p[5] / 2 - 0.6 - 0.6;
    lh = min(port_label_h, room_down);
    z = zc - p[5] / 2 - 0.6 - lh;
    if (lh >= 2) {
        if (side == "left")
            translate([engrave - 0.01, off[1] + pos, z]) rotate([90, 0, -90])
                linear_extrude(engrave + 1) port_label_2d(p[0], lh);
        else if (side == "right")
            translate([outer[0] - engrave + 0.01, off[1] + pos, z]) rotate([90, 0, 90])
                linear_extrude(engrave + 1) port_label_2d(p[0], lh);
        else if (side == "front")
            translate([off[0] + pos, engrave - 0.01, z]) rotate([90, 0, 0])
                linear_extrude(engrave + 1) port_label_2d(p[0], lh);
        else
            translate([off[0] + pos, outer[1] - engrave + 0.01, z]) rotate([90, 0, 180])
                linear_extrude(engrave + 1) port_label_2d(p[0], lh);
    }
}

module bottom() {
    difference() {
        union() {
            difference() {
                rounded_box(outer, corner_r, shell_h);
                translate([wall, wall, floor_t]) rounded_box(inner, max(corner_r - wall, 0.5), shell_h);
            }
            // standoffs under the mounting holes
            for (h = holes) at_board(h) cylinder(d = standoff_d, h = pcb_z);
        }
        // screw holes for heat-set inserts, all the way down for M3x16
        for (h = holes) at_board(h) translate([0, 0, 1]) cylinder(d = insert_d, h = pcb_z);
        for (p = ports) port_cut(p);
        for (p = ports) port_label(p);
        // vent slots in the floor, away from the standoffs
        for (i = [0:5]) translate([outer[0] / 2 - 25 + i * 10, outer[1] / 2 - 15, -1])
            hull() for (y = [0, 30]) translate([0, y, 0]) cylinder(d = 2.4, h = floor_t + 2);
        // recesses for self-adhesive rubber feet
        for (f = feet) translate([f[0], f[1], -0.01]) cylinder(d = foot_d, h = foot_h);
    }
}

module lid() {
    tube_h = shell_h - pcb_top + tube_press;   // presses the PCB down a little
    difference() {
        union() {
            translate([0, 0, shell_h]) rounded_box(outer, corner_r, lid_t);
            // lip that centres the lid in the shell
            translate([wall + 0.25, wall + 0.25, shell_h - 2])
                difference() {
                    rounded_box([inner[0] - 0.5, inner[1] - 0.5], max(corner_r - wall, 0.5), 2);
                    translate([1.2, 1.2, -1])
                        rounded_box([inner[0] - 2.9, inner[1] - 2.9], max(corner_r - wall - 1.2, 0.5), 4);
                }
            // tubes on the mounting holes: clamp the PCB onto the standoffs
            for (h = holes) at_board(h) translate([0, 0, shell_h - tube_h])
                cylinder(d = standoff_d, h = tube_h);
            // guides for the plungers
            for (k = keys) at_board(k) translate([0, 0, shell_h - 3])
                cylinder(d = key_hole_d + 2.4, h = 3);
        }
        for (h = holes) at_board(h) {
            translate([0, 0, -1]) cylinder(d = screw_d, h = shell_h + lid_t + 2);
            translate([0, 0, shell_h + lid_t - screw_head_h]) cylinder(d = screw_head_d, h = 5);
        }
        for (k = keys) at_board(k) translate([0, 0, shell_h - 4]) cylinder(d = key_hole_d, h = lid_t + 6);
        for (p = pin_holes) at_board(p) translate([0, 0, shell_h - 1]) cylinder(d = 2.0, h = lid_t + 2);
        at_board(led) translate([0, 0, shell_h - 1]) cylinder(d = 3.1, h = lid_t + 2);
        if (display_window)
            at_board(display_pos) translate([0, 0, shell_h - 1]) cube([display_size[0], display_size[1], lid_t + 2]);
        lid_lettering();
    }
}

module lid_lettering() {
    top = shell_h + lid_t - engrave;
    translate([0, 0, top]) linear_extrude(engrave + 1) {
        // name in the free area in the middle
        translate([outer[0] / 2, off[1] + 45]) text("HORCHPOSTEN", size = 7.5, font = font, halign = "center");
        translate([outer[0] / 2, off[1] + 37]) text("ESP32 CW BINAURAL", size = 4.2, font = font, halign = "center");
        translate([outer[0] / 2, off[1] + 30.5]) text("by DL8UG", size = 4.2, font = "Liberation Sans", halign = "center");
        // functions behind the row of eight positions (RST, BOOT, KEY1..KEY6):
        // short press, long press behind it; every second position one row
        // further back, so neighbouring labels get twice the key pitch
        all = concat(pin_holes, keys);
        labels = concat([["RST", ""], ["BOOT", ""]], key_labels);
        for (i = [0:len(all) - 1]) {
            k = all[i];
            dy = i % 2 == 0 ? 0 : 8.5;
            translate([off[0] + k[0], off[1] + k[1] + 6.0 + dy]) text(labels[i][0], size = 3.0, font = font, halign = "center");
            translate([off[0] + k[0], off[1] + k[1] + 10.0 + dy]) text(labels[i][1], size = 2.7, font = "Liberation Sans", halign = "center");
        }
    }
}

// plungers, printed standing on the flange
module plunger() {
    total = (shell_h + lid_t + plunger_above) - (pcb_top + key_top + 0.3);
    cylinder(d = plunger_flange_d, h = plunger_flange_h);
    cylinder(d = plunger_d, h = total);
}

module buttons() {
    for (i = [0:5]) translate([(i % 3) * 13, floor(i / 3) * 13, 0]) plunger();
}

module board_dummy() {
    translate([off[0], off[1], pcb_z]) {
        color("green") difference() {
            cube([pcb[0], pcb[1], pcb_t]);
            for (h = holes) translate([h[0], h[1], -1]) cylinder(d = hole_d, h = 4);
        }
        // ESP32-A1S with the antenna end over the back edge
        color("silver") translate([antenna[0], pcb[1] - 26, pcb_t]) cube([antenna[1] - antenna[0], 26 + antenna[2], 3.3]);
        color("black") for (k = concat(pin_holes, keys)) translate([k[0] - 3, k[1] - 3, pcb_t]) cube([6, 6, key_top]);
        color("dimgray") for (p = ports) {
            if (p[1] == "right") translate([pcb[0] - 14, p[2] - 3, pcb_t]) cube([14, 6, 6]);   // 3.5 mm jack, centre 3 mm above the PCB
            if (p[1] == "left") translate([0, p[2] - 4, pcb_t]) cube([5, 8, 3]);
        }
    }
}

module assembly() {
    color("lightsteelblue") bottom();
    board_dummy();
    color("lightgray", 0.85) translate([0, 0, 25]) lid();
    for (k = keys) at_board(k) translate([0, 0, pcb_top + key_top + 0.3 + 25]) color("orange") plunger();
}

if (part == "bottom") bottom();
else if (part == "lid") translate([0, outer[1], shell_h + lid_t]) rotate([180, 0, 0]) lid(); // print upside down
else if (part == "buttons") buttons();
else if (part == "lidview") lid();
else assembly();
