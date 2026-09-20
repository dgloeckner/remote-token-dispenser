#!/usr/bin/env python3
"""Generates front-view.svg: hopper in wall cabinet, door closed, exit slot + coin cup.

All values in mm. Edit the parameters below and re-run:
    python3 hardware/enclosure/draw_front_view.py
"""
from pathlib import Path

# ---- Parameters -----------------------------------------------------------
CAB_W, CAB_H, CAB_D = 400, 400, 200   # ekabel24 SPT-404020 (HxBxT 400x400x200)
WALL = 2                              # sheet thickness incl. paint (drawn)
PLATE_D = 15                          # mounting plate distance from back wall

HOP_X0 = 45                           # connector face of hopper from cabinet outer left
HOP_L, HOP_W, HOP_H = 228, 131, 155   # Azkoyen U-II "Grande" (manual 10239 EN, fig. 4)
BOWL_W, HOP_H_TOTAL = 150, 220        # metal bowl extension (measured)
# Coin exit zone on hopper side, relative to connector face / base underside (fig. 4)
EXIT_TOP = (26.3, 128.7)
EXIT_BOT = (43.3, 99.2)
EXIT_SLOT_W = 5.75

SLOT_CX, SLOT_W = 80, 40              # door slot: centre from cabinet left, width
SLOT_Y0, SLOT_Y1 = 56, 88             # door slot: bottom / top edge from cabinet bottom
CUP_W, CUP_D = 120, 55                # coin cup width / depth
CUP_Y0, CUP_LIP, CUP_BACK = 15, 50, 108 # cup floor / front lip / back plate top
HASP_Y, HASP_W, HASP_DOOR_L = 248, 45, 50   # articulated hasp (ABUS 110/155 or similar): centre height, width, length on door
DUCT_X0, DUCT_L, DUCT_H, DUCT_TOP = 18, 360, 60, 388   # slotted wiring duct 60x40 on the back wall (60 = visible height)
HASP_BOLT_EDGE = 25                         # first bolt at least this far from door edge / cabinet front edge

from _svg import View, add, begin, out, finish

PAGE_W, PAGE_H = 1010, 1000
begin(PAGE_W, PAGE_H)
add(f'<rect x="8" y="8" width="{PAGE_W - 16}" height="{PAGE_H - 16}" class="thin"/>')

B = WALL  # hopper base sits on cabinet floor
hx = HOP_X0
ex_top = (hx + EXIT_TOP[0], B + EXIT_TOP[1])
ex_bot = (hx + EXIT_BOT[0], B + EXIT_BOT[1])
slot_x0, slot_x1 = SLOT_CX - SLOT_W / 2, SLOT_CX + SLOT_W / 2
cup_x0, cup_x1 = SLOT_CX - CUP_W / 2, SLOT_CX + CUP_W / 2

# ===========================================================================
# FRONT VIEW (door closed)
# ===========================================================================
F = View(125, 510)
F.text(0, CAB_H + 78, "VORDERANSICHT – Tür geschlossen", "h")
F.text(0, CAB_H + 64, "verdeckte Teile gestrichelt", "t")

F.rect(0, 0, CAB_W, CAB_H, "vis")
F.rect(4, 4, CAB_W - 4, CAB_H - 4, "door")

# --- hidden: hopper (simplified side contour, exit side faces the door)
hop = [(hx, B), (hx + 110, B), (hx + 110, B + 10), (hx + HOP_L, B + 78),
       (hx + HOP_L, B + HOP_H_TOTAL), (hx + 92, B + HOP_H_TOTAL), (hx + 42, B + 168),
       (hx, B + 168)]
F.poly(hop, "hid")
F.line(hx, B + HOP_H, hx + HOP_L, B + HOP_H, "hid")           # bowl extension joint
# inclined disc support (red part on the real hopper), 60 deg; the exit slit runs
# parallel to its upper edge (manual fig. 4)
T60 = 3 ** 0.5
edge_top_x = EXIT_TOP[0] - (HOP_H - EXIT_TOP[1]) / T60 - 3.3   # upper band edge at y = HOP_H
edge_bot_x = EXIT_TOP[0] + EXIT_TOP[1] / T60 - 3.3             # upper band edge at y = 0
band_w = 25 / (T60 / 2)                                        # 25 mm band, measured horizontally
F.poly([(hx + edge_top_x, B + HOP_H), (hx + edge_bot_x, B), (hx + edge_bot_x - band_w, B),
        (hx, B + (edge_bot_x - band_w) * T60), (hx, B + HOP_H)], "band")
F.rect(hx - 22, B + 60, hx, B + 85, "hid")                    # IDC plug + cable bend
F.line(hx + 150, B + HOP_H_TOTAL, hx + 225, B + HOP_H_TOTAL, "zone", 'stroke-width="3"')

# --- hidden: electronics + fill space
# wiring duct with the electronics, left to right; fill space stays free below / in front of it
duct_y0 = DUCT_TOP - DUCT_H
F.rect(DUCT_X0, duct_y0, DUCT_X0 + DUCT_L, DUCT_TOP, "hidf", 'stroke-dasharray="7 4"')
duct_parts = [("Spleiß + Elko", 70), ("Opto 1", 50), ("Opto 2", 50), ("Opto 3", 50), ("Opto 4", 50),
              ("5-V-Wandler", 42), ("Wemos D1", 48)]
duct_sub = ["2200 µF", "Control", "Coin", "Error", "Empty", "Mini-360", "Antenne frei"]
dx = DUCT_X0
for (name, w), sub in zip(duct_parts, duct_sub):
    if dx > DUCT_X0:
        F.line(dx, duct_y0 + 4, dx, DUCT_TOP - 4, "hid")
    F.text(dx + w / 2, duct_y0 + 34, name, "s", "middle")
    F.text(dx + w / 2, duct_y0 + 22, sub, "s", "middle")
    dx += w
F.text(DUCT_X0 + DUCT_L / 2, DUCT_TOP - 12, "Verdrahtungskanal 60 × 40, geschlitzt, mit Deckel – jede Platine mit Kabelbinder fixiert",
       "t", "middle")
# ribbon cable to the hopper plug (service loop) and 12 V feed from the gland via inline fuse
F.poly([(DUCT_X0 + 14, duct_y0), (DUCT_X0 + 14, 250), (12, 215), (12, 120), (30, 100), (hx - 11, B + 85)],
       "cable", closed=False)
F.poly([(342, 2), (342, 22), (388, 40), (388, duct_y0 + 30), (DUCT_X0 + DUCT_L, duct_y0 + 30)], "cable", closed=False)
F.rect(383, 78, 393, 108, "hidf")
F.rect(190, B + HOP_H_TOTAL + 4, CAB_W - 10, duct_y0 - 6, "zone")

# --- hidden: coin exit zone + funnel/ramp (red)
a, b = F.p(*ex_top)
c, d = F.p(*ex_bot)
add(f'<line x1="{a:.1f}" y1="{b:.1f}" x2="{c:.1f}" y2="{d:.1f}" class="exit" stroke-width="{EXIT_SLOT_W}"/>')
F.poly([(slot_x0 - 8, ex_top[1] + 8), (slot_x1 + 8, ex_top[1] + 8),
        (slot_x1, SLOT_Y1), (slot_x0, SLOT_Y1)], "coinh")

# --- visible: coin cup on door
F.rect(cup_x0, CUP_Y0, cup_x1, CUP_BACK, "door")               # back plate
F.rect(slot_x0, SLOT_Y0, slot_x1, SLOT_Y1, "cut")              # door slot
F.rect(cup_x0, CUP_Y0, cup_x1, CUP_LIP, "part")                # cup front wall
for sx_, sy_ in ((cup_x0 + 8, CUP_BACK - 8), (cup_x1 - 8, CUP_BACK - 8),
                 (cup_x0 + 8, CUP_LIP + 9), (cup_x1 - 8, CUP_LIP + 9)):
    F.circle(sx_, sy_, 2.2, "thin")
F.line(SLOT_CX, CUP_Y0 - 8, SLOT_CX, CUP_BACK + 10, "ctr")
F.line(slot_x0 - 10, (SLOT_Y0 + SLOT_Y1) / 2, slot_x1 + 10, (SLOT_Y0 + SLOT_Y1) / 2, "ctr")

# --- visible: hinges (left), lock (right), antenna, cable gland
for hy in (150, CAB_H - 90):
    F.rect(-5, hy, 4, hy + 30, "part")
F.circle(CAB_W - 28, CAB_H / 2, 10, "part")
F.rect(CAB_W - 31, CAB_H / 2 - 5, CAB_W - 25, CAB_H / 2 + 5, "cut")
F.rect(336, CAB_H, 348, CAB_H + 8, "part")
F.rect(339.5, CAB_H + 8, 344.5, CAB_H + 58, "part")
F.rect(333, -16, 351, 0, "part")
F.line(342, -16, 342, -40, "vis")

# --- hasp with padlock on the lock side (schematic, see detail B)
hy0, hy1 = HASP_Y - HASP_W / 2, HASP_Y + HASP_W / 2
F.rect(CAB_W - HASP_DOOR_L - 14, HASP_Y - 40, CAB_W - 8, HASP_Y + 40, "hid")          # counter plate behind door
F.rect(CAB_W - HASP_DOOR_L, hy0, CAB_W, hy1, "part")                                   # hasp plate on door
F.rect(CAB_W, hy0, CAB_W + 7, hy1, "part")                                             # arm folded round the corner
F.rect(CAB_W + 7, HASP_Y - 38, CAB_W + 27, HASP_Y - 2, "part")                         # padlock (narrow side)
F.poly([(CAB_W + 12, HASP_Y - 2), (CAB_W + 12, HASP_Y + 9), (CAB_W + 22, HASP_Y + 9), (CAB_W + 22, HASP_Y - 2)],
       "vis", closed=False)
for bx in (CAB_W - HASP_DOOR_L + 10, CAB_W - HASP_BOLT_EDGE):
    F.circle(bx, HASP_Y, 3.2, "thin")
F.circle(CAB_W - 14, HASP_Y - 6, 56, "ctr")
F.text(CAB_W + 30, HASP_Y + 50, "B", "h")
F.leader(CAB_W - HASP_DOOR_L, HASP_Y + 8, CAB_W - 66, 277, "Gelenküberfalle + Vorhängeschloss", "end")

# --- labels
F.leader(CAB_W - 35, CAB_H / 2 - 7, CAB_W - 62, 178, "Doppelbart-Vorreiber (bleibt)", "end")
F.leader(-5, CAB_H - 75, -30, CAB_H - 50, "Scharnier links", "end")
F.leader(342, CAB_H + 40, 300, CAB_H + 50, "WLAN-Antenne (SMA-Durchführung)", "end")
F.leader(351, -8, 362, -22, "Kabelverschraubung M12 (12 V DC)")
F.leader(12, 180, 60, 200, "Flachbandkabel mit Serviceschlaufe")
F.leader(388, 93, 345, 80, "Sicherung 4 A", "end")
for i, s_ in enumerate(("Einfüllraum – frei halten", "(Befüllen bei offener Tür von vorn)")):
    F.text(268, 311 - 12 * i, s_, "tb", "middle")
F.leader(hx + 188, B + HOP_H_TOTAL, 215, 250, "Einfüllöffnung Bunker")
F.text(hx + 150, 120, "Azkoyen Hopper U-II „Grande“", "t", "middle")
F.text(hx + 150, 108, "Auswurfseite zur Tür, Stecker links", "t", "middle")
F.leader((ex_top[0] + ex_bot[0]) / 2 + 3, (ex_top[1] + ex_bot[1]) / 2, 150, 165,
         "Münzaustritt 34 × 5,75 – parallel zur 60°-Kante")
out[-1] = out[-1].replace('class="t"', 'class="tr"')
F.leader(slot_x1 + 4, 92, 150, 148, "Fangtrichter + Rampe (innen)")
out[-1] = out[-1].replace('class="t"', 'class="tr"')
F.leader(slot_x1, SLOT_Y0 + 6, 150, 84, f"Türschlitz {SLOT_W} × {SLOT_Y1 - SLOT_Y0}")
F.leader(cup_x1, 32, 150, 40, f"Münzschale {CUP_W} × {CUP_LIP - CUP_Y0} × {CUP_D} tief")
F.leader(cup_x1, CUP_BACK - 3, 150, 62, f"Rückwand {CUP_W} × {CUP_BACK - CUP_Y0}, 4× M4")

# --- dimensions
F.hdim(0, hx, -22, f"{hx}", ref_y=0)
F.hdim(hx, hx + HOP_L, -22, f"{HOP_L} (Hopper)", ref_y=0)
F.hdim(0, SLOT_CX, -44, f"{SLOT_CX}", ref_y=0)
F.hdim(0, CAB_W, -66, f"{CAB_W}", ref_y=0)
F.vdim(0, SLOT_Y0, -22, f"{SLOT_Y0}", ref_x=0)
F.vdim(SLOT_Y0, SLOT_Y1, -22, "", ref_x=0)
F.text(-27, SLOT_Y1 + 4, f"{SLOT_Y1 - SLOT_Y0}", "d", "end")
F.vdim(0, ex_bot[1], -48, f"≈{ex_bot[1]:.0f}", ref_x=0)
F.vdim(0, ex_top[1], -72, f"≈{ex_top[1]:.0f}", ref_x=0)
F.vdim(0, CAB_H, CAB_W + 72, f"{CAB_H}", ref_x=CAB_W)
F.vdim(B, B + HOP_H_TOTAL, CAB_W - 55, f"{HOP_H_TOTAL}", ref_x=hx + HOP_L)
F.vdim(B + HOP_H_TOTAL, DUCT_TOP - DUCT_H, CAB_W - 55, f"≈{DUCT_TOP - DUCT_H - B - HOP_H_TOTAL}")

# ===========================================================================
# SECTION A-A through the door slot (seen from the left)
# ===========================================================================
S = View(680, 510)
S.text(0, CAB_H + 78, "SCHNITT A–A durch den Türschlitz", "h")
S.text(0, CAB_H + 64, "von links gesehen, Wand links / Tür rechts", "t")

# wall hatch
S.line(-WALL, -30, -WALL, CAB_H + 30, "vis")
for yy in range(-30, CAB_H + 30, 14):
    S.line(-WALL, yy + 10, -WALL - 10, yy, "wall")
S.text(-16, CAB_H + 38, "Wand", "t", "end")

S.rect(0, 0, CAB_D - 4, CAB_H, "vis")                         # body
S.line(PLATE_D, 25, PLATE_D, CAB_H - 25, "thin")              # mounting plate
door_x0, door_x1 = CAB_D - 4, CAB_D
S.rect(door_x0, 4, door_x1, SLOT_Y0, "cut")                   # door below slot
S.rect(door_x0, SLOT_Y1, door_x1, CAB_H - 4, "cut")           # door above slot

# hopper end view
bowl0 = PLATE_D + 3
body0 = bowl0 + (BOWL_W - HOP_W) / 2
body1 = body0 + HOP_W
S.rect(body0, B, body1, B + HOP_H, "hidf")
S.rect(bowl0, B + HOP_H, bowl0 + BOWL_W, B + HOP_H_TOTAL, "hidf")
S.text((body0 + body1) / 2, 60, "Hopper", "t", "middle")
S.text((body0 + body1) / 2, 48, f"{HOP_W} breit", "t", "middle")
S.text(bowl0 + BOWL_W / 2, B + HOP_H + 28, f"Bunker {BOWL_W} breit", "t", "middle")

# exit, baffle, ramp, coin path
S.line(body1, ex_bot[1], body1, ex_top[1], "exit", 'stroke-width="5"')
S.poly([(body1, ex_top[1] + 8), (door_x0 - 3, ex_top[1] + 8), (door_x0, SLOT_Y1 + 1)], "coin", closed=False)
S.poly([(body1, ex_bot[1] - 4), (door_x0, SLOT_Y0 + 1), (door_x1 + 6, SLOT_Y0 - 3)], "coin", closed=False)
S.poly([(body1 + 2, (ex_top[1] + ex_bot[1]) / 2), (body1 + 22, ex_bot[1] - 8),
        (door_x1 + 8, SLOT_Y0 + 3), (door_x1 + 30, CUP_Y0 + 8)], "coinp", closed=False)

# cup
S.rect(door_x1, CUP_Y0, door_x1 + 2, SLOT_Y0, "part")
S.rect(door_x1, SLOT_Y1, door_x1 + 2, CUP_BACK, "part")
S.poly([(door_x1 + 2, CUP_Y0 + 2), (door_x1 + CUP_D - 2, CUP_Y0 + 2), (door_x1 + CUP_D - 2, CUP_LIP),
        (door_x1 + CUP_D, CUP_LIP), (door_x1 + CUP_D, CUP_Y0), (door_x1 + 2, CUP_Y0)], "part")

# labels + dims
S.leader(PLATE_D, 300, 40, 320, "Montageplatte")
S.leader(door_x0 + 2, 250, door_x1 + 20, 270, "Tür")
S.leader(body1 + 14, ex_top[1] + 8, door_x1 + 20, 170, "Prallblech")
out[-1] = out[-1].replace('class="t"', 'class="tr"')
S.leader(body1 + 18, 89, door_x1 + 20, 140, "Rampe ≈45°")
out[-1] = out[-1].replace('class="t"', 'class="tr"')
S.leader(door_x1 + CUP_D, 35, door_x1 + CUP_D + 8, 60, "Schale")
S.hdim(body1, door_x0, 200, f"≈{door_x0 - body1:.0f}", ref_y=ex_top[1] + 12)
S.hdim(0, CAB_D, -44, f"{CAB_D}", ref_y=0)
S.hdim(door_x1, door_x1 + CUP_D, -22, f"{CUP_D}", ref_y=CUP_Y0)
S.vdim(CUP_Y0, CUP_LIP, door_x1 + CUP_D + 22, f"{CUP_LIP - CUP_Y0}", ref_x=door_x1 + CUP_D)

# section markers in front view
for yy, up in ((CAB_H + 22, 1), (-76, -1)):
    F.line(SLOT_CX, yy, SLOT_CX, yy + 14 * up, "vis")
    F.text(SLOT_CX + 5, yy + (4 if up > 0 else -2), "A", "h")

# ===========================================================================
# DETAIL B: horizontal section through the hasp, scale 2:1 (seen from above)
# x: 0 = outer face of side wall, negative = into the cabinet; y: 0 = outer door face, positive = towards the wall
# ===========================================================================
D = View(390, 925, s=2.0)
D.text(-78, 86, "DETAIL B – Überfalle, Schnitt von oben (M 2:1)", "h")
D.text(-78, 79.5, "schematisch; Lochbild und Maße an der gekauften Überfalle abnehmen", "t")
SHEET, CP = 1.5, 3.0                                   # cabinet sheet / counter plate thickness
b_door = (-(HASP_BOLT_EDGE + 24), -HASP_BOLT_EDGE - 2)  # bolt x positions on door
b_side = (HASP_BOLT_EDGE + 10, HASP_BOLT_EDGE + 38)     # bolt y positions on side wall
# cabinet body: side wall + front return flange, door with edge fold, seal
D.rect(-SHEET, 20, 0, 78, "cut")
D.rect(-20, 20, 0, 20 + SHEET, "cut")
D.rect(-78, 0, 0, SHEET, "cut")
D.rect(-SHEET - 1, 0, -1, 17, "cut")
D.rect(-19, 12, -7, 20, "zone")
# counter plates
D.rect(b_door[0] - 12, SHEET, b_door[1] + 8, SHEET + CP, "part")
D.rect(-SHEET - CP, b_side[0] - 9, -SHEET, b_side[1] + 9, "part")
# hasp: eye plate on side wall, hinge plate on door, articulated arm round the corner, eye + padlock
D.rect(0, b_side[0] - 9, 3, b_side[1] + 9, "part")
D.rect(-(HASP_DOOR_L + 8), -4, -8, 0, "part")
D.poly([(-8, -2), (5, -2), (5, b_side[1] + 2)], "vis", closed=False,
       extra='style="stroke-width:7;stroke-linejoin:round;stroke:#555"')
for jx, jy in ((-8, -2), (5, -2)):
    D.circle(jx, jy, 5.5, "part")
eye_y = (b_side[0] + b_side[1]) / 2
D.poly([(3, eye_y - 5), (17, eye_y - 5), (17, eye_y + 5), (3, eye_y + 5)], "vis", closed=False)
D.rect(10, eye_y - 22, 32, eye_y + 22, "door", 'fill-opacity="0.85"')
D.text(21, eye_y - 1, "Schloss", "t", "middle")
# carriage bolts M5 with washers and cap nuts
for bx in b_door:
    D.rect(bx - 2.5, -4, bx + 2.5, SHEET + CP + 1, "cut")
    D.poly([(bx - 6, -4), (bx - 4, -6.5), (bx + 4, -6.5), (bx + 6, -4)], "part")
    D.rect(bx - 6, SHEET + CP, bx + 6, SHEET + CP + 1, "part")
    D.poly([(bx - 4.5, SHEET + CP + 1), (bx - 4.5, SHEET + CP + 6), (bx - 2.5, SHEET + CP + 9.5),
            (bx + 2.5, SHEET + CP + 9.5), (bx + 4.5, SHEET + CP + 6), (bx + 4.5, SHEET + CP + 1)], "part")
for by in b_side:
    D.rect(-SHEET - CP - 1, by - 2.5, 3, by + 2.5, "cut")
    D.rect(-SHEET - CP - 1, by - 6, -SHEET - CP, by + 6, "part")
    x0 = -SHEET - CP - 1
    D.poly([(x0, by - 4.5), (x0 - 5, by - 4.5), (x0 - 8.5, by - 2.5), (x0 - 8.5, by + 2.5),
            (x0 - 5, by + 4.5), (x0, by + 4.5)], "part")
# labels
D.leader(-SHEET / 2, 76, -84, 74, "Seitenwand", "end")
D.leader(-SHEET - CP - 6, b_side[1], -84, 64, "Hutmutter M5 + U-Scheibe", "end")
D.leader(-SHEET - CP / 2, eye_y, -84, 54, "Gegenplatte 3 mm, ca. 60 × 80", "end")
D.leader(-13, 16, -84, 40, "Dichtung / Falz", "end")
D.leader(-72, SHEET / 2, -84, 28, "Tür", "end")
D.leader(-(HASP_DOOR_L + 2), -2, -84, -14, "Überfalle, Türplatte", "end")
D.leader(b_door[0], -6, b_door[0] + 4, -22, "Schlossschraube M5 × 16")
D.leader(1.5, b_side[1] + 7, 44, 66, "Ösenplatte")
D.leader(5, 10, 46, 12, "Gelenkarm, verdeckt Schrauben")
D.hdim(b_door[1], 0, -12, f"≥ {HASP_BOLT_EDGE}", ref_y=-7)
D.vdim(0, b_side[0], 40, f"≥ {HASP_BOLT_EDGE + 10}", ref_x=8)

# ===========================================================================
# Notes + title block
# ===========================================================================
notes = [
    ("nb", "Hinweise"),
    ("n", f"1  Schrank: Stahlblech IP65, {CAB_H} × {CAB_W} × {CAB_D} (H×B×T), z. B. ekabel24 SPT-404020. Türanschlag links, Schale nahe am Scharnier."),
    ("n", f"2  Hopper: Azkoyen U-II „Grande“ {HOP_L} × {HOP_W} × {HOP_H}, mit Bunkeraufsatz {HOP_H_TOTAL} hoch (gemessen). Steht direkt auf dem Schrankboden."),
    ("n", "3  Münzaustritt nach Azkoyen „Technical Information Hopper U-II“ 10239 EN, Fig. 4: 99,2–128,7 über Basis, 26,3–43,3 ab Steckerfläche."),
    ("n", "    Der Schlitz liegt parallel zur 60°-Kante der Förderscheibe (rot); die Münze tritt quer zur Längsachse aus, also zur Tür hin."),
    ("n", "4  Hopper-Kontur vereinfacht. Türschlitz, Rampe und Schale sind Planungswerte – vor dem Bohren mit Pappmodell und ≥ 20 Testauswürfen prüfen."),
    ("n", "5  Der Türschlitz hebt IP65 auf. Rampe und Prallblech am Schrank befestigen, nicht an der Tür. Spalt Rampe–Tür ≤ 5."),
    ("n", "    Türschlitz so hoch, dass auch eine hochkant rollende Münze (Ø 25,75) durchpasst. Trichter-Details: funnel.pdf."),
    ("n", "8  Elektronik im PVC-Verdrahtungskanal an der Rückwand (2–3 Nieten, nicht nur kleben). Der Kanal ist 40 tief – davor und darunter bleibt der Einfüllraum frei."),
    ("n", "6  Montagehöhe: Schrankunterkante ≈ 1000 über Fußboden → Schale auf ≈ 1015–1050."),
    ("n", "7  Gelenküberfalle (z. B. ABUS 110/155) als zweiter Schließpunkt: Schlossschrauben M5 + Hutmuttern, innen Gegenplatten 3 mm, siehe Detail B."),
]
ny = 606
for cls, s in notes:
    add(f'<text x="24" y="{ny}" class="{cls}">{s}</text>')
    ny += 13

tbx, tby, tbw, tbh = PAGE_W - 8 - 350, PAGE_H - 8 - 62, 350, 62
add(f'<rect x="{tbx}" y="{tby}" width="{tbw}" height="{tbh}" class="thin" fill="#fff"/>')
add(f'<line x1="{tbx}" y1="{tby + 24}" x2="{tbx + tbw}" y2="{tby + 24}" class="thin"/>')
add(f'<line x1="{tbx + 250}" y1="{tby + 24}" x2="{tbx + 250}" y2="{tby + tbh}" class="thin"/>')
add(f'<text x="{tbx + 8}" y="{tby + 17}" class="title">Token-Dispenser – Gehäuse Vorderansicht</text>')
add(f'<text x="{tbx + 8}" y="{tby + 39}" class="n">Maße in mm · Planungsstand, nicht fertigungsreif</text>')
add(f'<text x="{tbx + 8}" y="{tby + 53}" class="n">Quelle: hardware/enclosure/draw_front_view.py</text>')
add(f'<text x="{tbx + 258}" y="{tby + 39}" class="n">Datum: 2026-09-20</text>')
add(f'<text x="{tbx + 258}" y="{tby + 53}" class="n">Rev. A</text>')

finish(Path(__file__).with_name("front-view.svg"))
