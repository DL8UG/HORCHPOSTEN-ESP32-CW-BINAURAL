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
pcb = [82, 73];            // PCB size x, y (maker: 82 x 73 +-0.2)
pcb_t = 1.6;               // PCB thickness
holes = [[3.5, 3.5], [78.5, 3.5], [3.5, 69.5], [78.5, 69.5]];  // mounting holes
hole_d = 3.2;              // mounting hole diameter
under_h = 4.0;             // room under the PCB (solder pins, battery connector)
over_h = 11.0;             // room above the PCB (tallest part + margin)

// keys KEY1..KEY6: centre x, y; height of the switch actuator top above the PCB
keys = [[12, 8], [23.5, 8], [35, 8], [46.5, 8], [58, 8], [69.5, 8]];
key_top = 5.0;
// short / long press function, printed on the lid
key_labels = [["MODE", "L/R"], ["FILTER", "AGC"], ["PITCH-", "WIDTH"],
              ["PITCH+", "AUTO"], ["VOL-", "GAIN"], ["VOL+", "MUTE"]];

// RESET and BOOT: reachable with a paper clip
pin_holes = [[8, 32], [8, 42]];
// status LED (GPIO22): hole for a piece of clear filament as light pipe
led = [74, 30];

// wall openings: [label, side, position along the side, centre height above
// the PCB top, width, height, round?]; side: "left" (x 0), "right", "front"
// (y 0), "back"
ports = [
    ["PHONES",  "left", 24, 3.0, 7.0, 7.0, true],
    ["LINE IN", "left", 40, 3.0, 7.0, 7.0, true],
    ["UART",    "back", 22, 1.8, 9.0, 4.5, false],
    ["POWER",   "back", 60, 1.8, 9.0, 4.5, false],
];

/* ---------- enclosure ---------- */
clear = 0.6;               // gap PCB to wall
wall = 2.2;
floor_t = 2.0;
lid_t = 2.4;
corner_r = 4;
screw_d = 3.4;             // M3 clearance in the lid
screw_head_d = 6.2;        // counterbore for M3 cap screws
screw_head_h = 1.6;
insert_d = 4.0;            // M3 heat-set insert (or 2.6 for self-tapping screws)
standoff_d = 7;
key_hole_d = 6.6;          // lid guide for the plungers
plunger_d = 6.0;           // 0.3 mm play on each side in the guide
plunger_flange_d = 8.6;
plunger_flange_h = 1.2;
plunger_above = 1.6;       // how far the plungers stand above the lid
tube_press = 0.2;          // lid tubes this much longer than the room above the PCB
engrave = 0.6;             // depth of the lid lettering
font = "Liberation Sans:style=Bold";

// later stage: window for a display in the lid
display_window = false;
display_pos = [20, 30];    // front left corner of the window on the board
display_size = [42, 32];

/* ---------- derived ---------- */
inner = [pcb[0] + 2 * clear, pcb[1] + 2 * clear];
outer = [inner[0] + 2 * wall, inner[1] + 2 * wall];
pcb_z = floor_t + under_h;                 // PCB bottom
pcb_top = pcb_z + pcb_t;
shell_h = pcb_top + over_h;                // bottom shell height = lid underside
off = [wall + clear, wall + clear];        // PCB origin in shell coordinates

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
        else hull() for (s = [-1, 1]) translate([s * (w - h) / 2, 0, 0]) cylinder(d = h, h = len);
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

// label next to a port on the outside of the wall
module port_label(p) {
    side = p[1]; pos = p[2]; zc = pcb_top + p[3];
    // above the opening if there is room below the lid, else below it
    z_up = zc + p[5] / 2 + 1.0;
    z = z_up + 3 < shell_h - 0.5 ? z_up : zc - p[5] / 2 - 3.6;
    if (z > 0.8) {
        if (side == "left")
            translate([engrave - 0.01, off[1] + pos, z]) rotate([90, 0, -90])
                linear_extrude(engrave + 1) text(p[0], size = 2.6, font = font, halign = "center");
        else if (side == "right")
            translate([outer[0] - engrave + 0.01, off[1] + pos, z]) rotate([90, 0, 90])
                linear_extrude(engrave + 1) text(p[0], size = 2.6, font = font, halign = "center");
        else if (side == "front")
            translate([off[0] + pos, engrave - 0.01, z]) rotate([90, 0, 0])
                linear_extrude(engrave + 1) text(p[0], size = 2.6, font = font, halign = "center");
        else
            translate([off[0] + pos, outer[1] - engrave + 0.01, z]) rotate([90, 0, 180])
                linear_extrude(engrave + 1) text(p[0], size = 2.6, font = font, halign = "center");
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
        for (x = [10, outer[0] - 10], y = [10, outer[1] - 10])
            translate([x, y, -0.01]) cylinder(d = 10.5, h = 0.8);
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
        // name in the free area behind the keys
        translate([outer[0] / 2, outer[1] - 16]) text("HORCHPOSTEN", size = 6, font = font, halign = "center");
        translate([outer[0] / 2, outer[1] - 23]) text("ESP32 CW BINAURAL", size = 3.2, font = font, halign = "center");
        translate([outer[0] / 2, outer[1] - 28]) text("by DL8UG", size = 2.4, font = "Liberation Sans", halign = "center");
        // key functions: short press above, long press below it
        for (i = [0:len(keys) - 1]) {
            k = keys[i];
            translate([off[0] + k[0], off[1] + k[1] + 6.2]) text(key_labels[i][0], size = 2.1, font = font, halign = "center");
            translate([off[0] + k[0], off[1] + k[1] + 9.4]) text(key_labels[i][1], size = 1.9, font = "Liberation Sans", halign = "center");
        }
        // pin holes and LED
        translate([off[0] + pin_holes[0][0] + 3, off[1] + pin_holes[0][1] - 1]) text("RST", size = 2.2, font = font);
        translate([off[0] + pin_holes[1][0] + 3, off[1] + pin_holes[1][1] - 1]) text("BOOT", size = 2.2, font = font);
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
        color("silver") translate([pcb[0] / 2 - 16, pcb[1] - 32, pcb_t]) cube([32, 30, 3.3]); // ESP32-A1S
        color("black") for (k = keys) translate([k[0] - 3, k[1] - 3, pcb_t]) cube([6, 6, key_top]);
        color("dimgray") for (p = ports) {
            if (p[1] == "left") translate([0, p[2] - 3, pcb_t]) cube([12, 6, p[3] * 2]);
            if (p[1] == "back") translate([p[2] - 4, pcb[1] - 5, pcb_t]) cube([8, 5, 3]);
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
