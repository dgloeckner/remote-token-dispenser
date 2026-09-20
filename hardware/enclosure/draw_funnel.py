#!/usr/bin/env python3
"""Generates the coin funnel drawing: 1:1 flat patterns (A4) + assembly section.

Two riveted aluminium parts between hopper exit and door slot:
  part A  U-shaped housing (cheek | baffle | cheek) with lid and feet
  part B  ramp with side flanges and a small lip towards the hopper

All values in mm. Keep GAP / slot values in sync with draw_front_view.py. Run:
    python3 hardware/enclosure/draw_funnel.py
Print funnel.pdf at 100 % ("Tatsächliche Größe") and check the 50 mm control bar.
"""
import math
from pathlib import Path

from _svg import View, add, begin, finish

# ---- Parameters -----------------------------------------------------------
T = 1.0                    # sheet thickness (aluminium)
GAP = 38                   # hopper side plate -> inner door face (measure on the real cabinet!)
DEPTH = GAP - 3            # cheek depth: ~1.5 air to hopper, ~1.5 to door
INNER_W = 46               # clear width between cheeks
FLOOR = 2                  # cabinet floor thickness (heights below are from cabinet underside)
TOP_Y = 140                # lid height
EXIT_Y0, EXIT_Y1 = 101, 131  # hopper exit slit (manual fig. 4 + floor)
RAMP_Y_HOPPER = 96         # ramp height at hopper side
RAMP_Y_DOOR = 59           # ramp height at door side
BAFFLE_Y0 = 90             # lower edge of baffle = upper edge of outlet
SLOT_Y0, SLOT_Y1 = 56, 88  # door slot
LID = DEPTH - 2            # lid length
FOOT = 15                  # foot flange
FLANGE = 10                # ramp side flange
LIP = 5                    # ramp lip towards hopper
RIVET_S = (16, 40)         # rivet positions along the ramp, from hopper end
RIVET_D, FOOT_D = 3.3, 4.3  # hole diameters (3.2 blind rivet / M4)

CHEEK_H = TOP_Y - FLOOR
ramp_dy = RAMP_Y_HOPPER - RAMP_Y_DOOR
RAMP_L = math.hypot(DEPTH, ramp_dy)
RAMP_DEG = math.degrees(math.atan2(ramp_dy, DEPTH))
ux, uy = DEPTH / RAMP_L, -ramp_dy / RAMP_L          # along ramp, downhill
nx, ny = -ramp_dy / RAMP_L, -DEPTH / RAMP_L         # perpendicular, below ramp


S_TRIM = FLANGE * -nx / ux + 1   # flange starts here at its outer edge, so it clears the hopper plate


def rivet_pos(s):
    """(d, y) of a rivet: s along the ramp from the hopper end, FLANGE/2 below the surface."""
    return s * ux + FLANGE / 2 * nx, RAMP_Y_HOPPER + s * uy + FLANGE / 2 * ny


PRINT_STYLE = """<style>
.vis, .door, .part { stroke-width: 0.5; }
.thin { stroke-width: 0.25; }
.cut  { stroke-width: 0.25; }
.hid  { stroke-width: 0.3; stroke-dasharray: 2 1.2; }
.hidf { stroke-width: 0.3; }
.bend { fill: none; stroke: #2a6fb5; stroke-width: 0.4; stroke-dasharray: 4 1.2 0.8 1.2; }
.sheet { fill: #eef1f4; stroke: #111; stroke-width: 0.5; }
.coin { stroke-width: 0.5; }
.coinp { stroke-width: 0.4; stroke-dasharray: 0.6 1.2; }
.exit { stroke-opacity: 0.55; }
.ctr  { stroke-width: 0.18; stroke-dasharray: 4 1 0.7 1; }
.dim  { stroke-width: 0.18; }
.wall { stroke-width: 0.2; }
text  { stroke-width: 0.9px; }
.d    { font-size: 3.1px; }
.t    { font-size: 3px; }
.tr   { font-size: 3px; }
.tb   { font-size: 3px; }
.h    { font-size: 4.6px; }
.n    { font-size: 3px; }
.nb   { font-size: 3px; }
.title { font-size: 4.2px; }
</style>"""
K = 0.34
A4_W, A4_H = 210, 297


def page_frame(title, sub, sheet):
    add(PRINT_STYLE)
    add(f'<rect x="7" y="7" width="{A4_W - 14}" height="{A4_H - 14}" class="thin"/>')
    add(f'<text x="14" y="18" class="h">{title}</text>')
    add(f'<text x="14" y="24" class="t">{sub}</text>')
    # 50 mm control bar
    x0, y0 = A4_W - 14 - 50, 17
    add(f'<line x1="{x0}" y1="{y0}" x2="{x0 + 50}" y2="{y0}" class="vis"/>')
    for i in range(6):
        add(f'<line x1="{x0 + 10 * i}" y1="{y0 - 2}" x2="{x0 + 10 * i}" y2="{y0 + 2}" class="thin"/>')
    add(f'<text x="{x0 + 25}" y="{y0 + 6}" class="t" text-anchor="middle">Kontrollmaß 50 mm – Druck 100 %</text>')
    # title block
    ty = A4_H - 7 - 14
    add(f'<rect x="{A4_W - 7 - 110}" y="{ty}" width="110" height="14" class="thin" fill="#fff"/>')
    add(f'<text x="{A4_W - 7 - 107}" y="{ty + 5.5}" class="title">Münztrichter Alu {T:g} mm, vernietet</text>')
    add(f'<text x="{A4_W - 7 - 107}" y="{ty + 11}" class="n">M 1:1 · Maße in mm · 2026-09-20 · Rev. A · Blatt {sheet}/3</text>')


def notes(x, y, lines):
    for cls, s in lines:
        add(f'<text x="{x}" y="{y}" class="{cls}">{s}</text>')
        y += 4.3


# ===========================================================================
# Sheet 1: part A flat pattern
# ===========================================================================
begin(A4_W, A4_H, print_mm=True)
page_frame("Teil A – Gehäuse (Abwicklung 1:1)",
           "Ansicht von der Türseite. Blau strichpunktiert = Biegelinie. 1× fertigen.", 1)

u1, u2, u3 = DEPTH, DEPTH + INNER_W, 2 * DEPTH + INNER_W     # bend lines / overall width
bv = BAFFLE_Y0 - FLOOR                                       # baffle lower edge in pattern
A = View(50, 236, K)
outline = [(0, -FOOT), (u1 - 3, -FOOT), (u1 - 3, 0), (u1, 0), (u1, bv), (u2, bv), (u2, 0),
           (u2 + 3, 0), (u2 + 3, -FOOT), (u3, -FOOT), (u3, CHEEK_H), (u2 - 1, CHEEK_H),
           (u2 - 1, CHEEK_H + LID), (u1 + 1, CHEEK_H + LID), (u1 + 1, CHEEK_H), (0, CHEEK_H)]
A.poly(outline, "sheet")
for x in (u1, u2):
    A.line(x, bv, x, CHEEK_H, "bend")
A.line(u1 + 1, CHEEK_H, u2 - 1, CHEEK_H, "bend")
A.line(0, 0, u1 - 3, 0, "bend")
A.line(u2 + 3, 0, u3, 0, "bend")

# ramp scribe line + rivet holes on both cheeks (left: d = u, right: d = u3 - u)
for d2u in (lambda d: d, lambda d: u3 - d):
    A.line(d2u(0), RAMP_Y_HOPPER - FLOOR, d2u(DEPTH), RAMP_Y_DOOR - FLOOR, "ctr")
    for s in RIVET_S:
        d, y = rivet_pos(s)
        A.circle(d2u(d), y - FLOOR, RIVET_D / 2, "thin")
for x in ((u1 - 3) / 2, u3 - (u1 - 3) / 2):
    A.circle(x, -FOOT / 2, FOOT_D / 2, "thin")

A.text(u1 / 2, 118, "Wange L", "t", "middle")
A.text(u3 - u1 / 2, 118, "Wange R", "t", "middle")
A.text((u1 + u2) / 2, 112, "Prallblech", "t", "middle")
A.text((u1 + u2) / 2, CHEEK_H + LID / 2 - 1, "Deckel", "t", "middle")
A.text((u1 - 3) / 2, -FOOT + 1.5, "Fuß", "t", "middle")
A.text(u3 - (u1 - 3) / 2, -FOOT + 1.5, "Fuß", "t", "middle")
A.text((u1 + u2) / 2, 40, "Ausschnitt", "t", "middle")
A.text((u1 + u2) / 2, 35, "(Auslass zur Tür)", "t", "middle")
A.text(-2, 108, "freie Kante → zum Hopper", "t", "middle", rot=-90)
A.text(u3 + 5, 108, "freie Kante → zum Hopper", "t", "middle", rot=-90)
A.leader(u1, 128, u1 - 12, 150, "① Wangen 90° nach hinten", "end")
A.leader((u1 + u2) / 2 + 8, CHEEK_H, u2 + 14, CHEEK_H + 22, "② Deckel 90° nach hinten")
A.leader(8, 0, -2, -30, "③ Füße 90° nach vorn", "end")
d_, y_ = rivet_pos(RIVET_S[0])
A.leader(u3 - d_, y_ - FLOOR, u3 + 10, 70, f"4× Ø{RIVET_D:g}".replace(".", ","))
for i, s_ in enumerate(("erst nach Anprobe", "durch die Laschen", "von Teil B bohren")):
    A.text(u3 + 11, 62.5 - 4 * i, s_, "t")
A.leader(u3 - DEPTH * 0.55, RAMP_Y_HOPPER - FLOOR - ramp_dy * 0.55, u3 + 10, 36, "Anriss Rampe")
A.leader(u3 - (u1 - 3) / 2, -FOOT / 2, u3 + 10, -12, f"2× Ø{FOOT_D:g} (M4)".replace(".", ","))

A.hdim(0, u1, -24, f"{u1:g}", ref_y=-FOOT)
A.hdim(u1, u2, -24, f"{INNER_W:g}", ref_y=0)
A.hdim(u2, u3, -24, f"{DEPTH:g}", ref_y=-FOOT)
A.hdim(0, u3, -33, f"{u3:g}", ref_y=-FOOT)
A.vdim(-FOOT, 0, -14, f"{FOOT:g}", ref_x=0)
A.vdim(0, CHEEK_H, -14, f"{CHEEK_H:g}", ref_x=0)
A.vdim(0, bv, (u1 + u2) / 2 - 12, f"{bv:g}")
A.vdim(bv, CHEEK_H, (u1 + u2) / 2 - 12, f"{CHEEK_H - bv:g}")
A.vdim(CHEEK_H, CHEEK_H + LID, -14, f"{LID:g}", ref_x=u1 + 1)
A.hdim(u1 + 1, u2 - 1, CHEEK_H + LID + 6, f"{INNER_W - 2:g}", ref_y=CHEEK_H + LID)
A.vdim(0, RAMP_Y_HOPPER - FLOOR, -24, f"{RAMP_Y_HOPPER - FLOOR:g}", ref_x=0)
A.vdim(0, RAMP_Y_DOOR - FLOOR, u2 + 8, f"{RAMP_Y_DOOR - FLOOR:g}")

finish(Path(__file__).with_name("funnel-1-teil-a.svg"))

# ===========================================================================
# Sheet 2: part B flat pattern + assembly section + notes
# ===========================================================================
begin(A4_W, A4_H, print_mm=True)
page_frame("Teil B – Rampe (Abwicklung 1:1)",
           "Ansicht auf die Rutschfläche. Blau strichpunktiert = Biegelinie. 1× fertigen.", 2)

bw = INNER_W - 1                                  # ramp width with 0.5 clearance per side
Bv = View(32, 118, K)
Bv.poly([(FLANGE, 0), (FLANGE + bw, 0), (FLANGE + bw, 3), (2 * FLANGE + bw, 3),
         (2 * FLANGE + bw, RAMP_L - S_TRIM), (FLANGE + bw, RAMP_L - 3), (FLANGE + bw, RAMP_L + LIP),
         (FLANGE, RAMP_L + LIP), (FLANGE, RAMP_L - 3), (0, RAMP_L - S_TRIM), (0, 3), (FLANGE, 3)], "sheet")
Bv.line(FLANGE, 3, FLANGE, RAMP_L - 3, "bend")
Bv.line(FLANGE + bw, 3, FLANGE + bw, RAMP_L - 3, "bend")
Bv.line(FLANGE, RAMP_L, FLANGE + bw, RAMP_L, "bend")
for s in RIVET_S:
    for x in (FLANGE / 2, 1.5 * FLANGE + bw):
        Bv.circle(x, RAMP_L - s, RIVET_D / 2, "thin")
Bv.text(FLANGE + bw / 2, RAMP_L / 2 + 4, "Rutschfläche", "t", "middle")
Bv.text(FLANGE + bw / 2, RAMP_L / 2 - 1, "(blank, entgratet, keine Niete)", "t", "middle")
Bv.text(FLANGE + bw / 2, RAMP_L + 1.2, "Lippe", "t", "middle")
Bv.text(FLANGE + bw / 2, -5, "↓ Türseite", "t", "middle")
Bv.text(FLANGE + bw / 2, RAMP_L + LIP + 3, "↑ Hopperseite", "t", "middle")
Bv.leader(FLANGE, 12, FLANGE + 6, -26, "① Laschen 90° nach unten (weg von der Rutschfläche)")
Bv.leader(FLANGE + bw - 6, RAMP_L, FLANGE + bw - 2, RAMP_L + LIP + 12, "② Lippe 90° nach oben")
Bv.leader(1.5 * FLANGE + bw, RAMP_L - RIVET_S[1], 2 * FLANGE + bw + 6, 4, f"4× Ø{RIVET_D:g}".replace(".", ","))
Bv.hdim(0, FLANGE, -14, f"{FLANGE:g}", ref_y=3)
Bv.hdim(FLANGE, FLANGE + bw, -14, f"{bw:g}", ref_y=0)
Bv.hdim(FLANGE + bw, 2 * FLANGE + bw, -14, f"{FLANGE:g}", ref_y=3)
Bv.vdim(0, RAMP_L, -10, f"{RAMP_L:.1f}".replace(".", ","), ref_x=FLANGE)
Bv.vdim(RAMP_L, RAMP_L + LIP, -10, f"{LIP:g}", ref_x=FLANGE)
Bv.leader(3, RAMP_L - S_TRIM + 2.5, -3, RAMP_L + 9, f"Schräge {S_TRIM:.0f}", "end")
Bv.vdim(RAMP_L - RIVET_S[0], RAMP_L, 2 * FLANGE + bw + 8, f"{RIVET_S[0]:g}", ref_x=2 * FLANGE + bw)
Bv.vdim(RAMP_L - RIVET_S[1], RAMP_L - RIVET_S[0], 2 * FLANGE + bw + 8, f"{RIVET_S[1] - RIVET_S[0]:g}",
        ref_x=2 * FLANGE + bw)

notes(132, 48, [
    ("nb", "Stückliste"),
    ("n", f"Alu-Blech {T:g} mm (AlMg3 o. ä.):"),
    ("n", f"  Teil A  Zuschnitt {u3:g} × {FOOT + CHEEK_H + LID:g}"),
    ("n", f"  Teil B  Zuschnitt {2 * FLANGE + bw:g} × {RAMP_L + LIP:.0f}"),
    ("n", "4× Blindniet Alu Ø3,2 × 6"),
    ("n", "2× M4 × 10 + Mutter (oder Blindniet Ø4)"),
    ("n", ""),
    ("nb", "Reihenfolge"),
    ("n", "1  Vorlage 100 % drucken, Kontrollmaß prüfen,"),
    ("n", "    aufkleben, schneiden, alle Kanten entgraten."),
    ("n", "2  Fuß- und Laschenlöcher bohren (Teil B),"),
    ("n", "    Wangenlöcher in Teil A noch NICHT."),
    ("n", "3  Biegen in der Reihenfolge ①②③."),
    ("n", "4  Rampe nach Anriss einsetzen, Wangen durch"),
    ("n", "    die Laschen abbohren, nieten."),
    ("n", "    Nietköpfe liegen unter der Rutschfläche."),
    ("n", "5  Erst aus Pappe bauen und ≥ 20 Münzen"),
    ("n", "    testen, dann Blech. Türschlitz zuletzt."),
    ("n", ""),
    ("nb", "Hinweise"),
    ("n", "• Maße sind Innenmaße; Biegeverkürzung bei"),
    ("n", f"   {T:g} mm Alu (≈ 1 mm je Biegung) unkritisch."),
    ("n", "• Spalt Hopper–Tür am Schrank messen und"),
    ("n", "   GAP im Skript anpassen (Wange = Spalt − 3)."),
    ("n", "• Trichter nur am Schrankboden befestigen,"),
    ("n", "   nicht an Hopper oder Tür."),
    ("n", "• Schräge an den Laschen von Teil B hält sie"),
    ("n", "   frei vom Hopper-Seitenblech."),
    ("n", "• Auslass und Türschlitz ≥ 30 hoch, damit"),
    ("n", "   auch hochkant rollende Münzen durchgehen."),
])

finish(Path(__file__).with_name("funnel-2-teil-b.svg"))

# ===========================================================================
# Sheet 3: assembly section
# ===========================================================================
begin(A4_W, A4_H, print_mm=True)
page_frame("Zusammenbau – Schnitt durch die Trichtermitte (1:1)",
           "Von links gesehen: Hopper links, Tür rechts. Rot gepunktet = Münzweg.", 3)

# --- assembly section, 1:1 (d = 0 at the cheeks' free edge)
S = View(92, 236, K)
air = (GAP - DEPTH) / 2
S.rect(-30, 0, GAP - air + 14, FLOOR, "cut")                           # cabinet floor
S.rect(-air - 1.5, FLOOR, -air, TOP_Y + 12, "hidf")                    # hopper side plate
S.line(-air - 0.75, EXIT_Y0, -air - 0.75, EXIT_Y1, "exit", 'stroke="#c0281c" stroke-width="2.4"')
S.rect(DEPTH + air, FLOOR + 2, DEPTH + air + 1.5, SLOT_Y0, "cut")      # door below slot
S.rect(DEPTH + air, SLOT_Y1, DEPTH + air + 1.5, TOP_Y + 12, "cut")     # door above slot
S.rect(0, FLOOR, DEPTH, TOP_Y, "sheet", 'fill-opacity="0.6"')          # cheek (behind section)
S.line(DEPTH, BAFFLE_Y0, DEPTH, TOP_Y, "vis", 'stroke-width="1"')      # baffle
S.line(DEPTH - LID, TOP_Y, DEPTH, TOP_Y, "vis", 'stroke-width="1"')    # lid
S.line(0, FLOOR + 0.5, DEPTH, FLOOR + 0.5, "vis", 'stroke-width="1"')  # feet (sideways)
S.poly([(0, RAMP_Y_HOPPER + LIP), (0, RAMP_Y_HOPPER), (DEPTH, RAMP_Y_DOOR)], "vis", closed=False,
       extra='stroke-width="1"')
S.poly([(0, RAMP_Y_HOPPER), (DEPTH, RAMP_Y_DOOR), (DEPTH + FLANGE * nx, RAMP_Y_DOOR + FLANGE * ny),
        (FLANGE * nx + S_TRIM * ux, RAMP_Y_HOPPER + FLANGE * ny + S_TRIM * uy),
        (3 * ux, RAMP_Y_HOPPER + 3 * uy)], "hid")                                  # ramp flange
for s in RIVET_S:
    S.circle(*rivet_pos(s), 1.6, "thin")
ym = (EXIT_Y0 + EXIT_Y1) / 2
S.poly([(-air, ym), (DEPTH - 3, ym - 1), (DEPTH - 6, ym - 14), (DEPTH * 0.55, RAMP_Y_HOPPER - ramp_dy * 0.5 + 3),
        (DEPTH + air + 8, RAMP_Y_DOOR - 8)], "coinp", closed=False, extra='stroke="#c0281c" fill="none"')

S.leader(-air - 0.75, EXIT_Y1 - 4, -34, EXIT_Y1 + 10, "Hopper-Seitenblech mit Austritt", "end")
S.leader(DEPTH, 122, DEPTH + 14, 130, "Prallblech")
S.leader(DEPTH - 12, TOP_Y, DEPTH + 14, TOP_Y + 8, "Deckel")
S.leader(DEPTH * 0.5, RAMP_Y_HOPPER - ramp_dy * 0.5, DEPTH + 14, 100, f"Rampe {RAMP_DEG:.0f}°")
S.leader(0, RAMP_Y_HOPPER + LIP - 1, -34, RAMP_Y_HOPPER - 12, "Lippe, Spalt zum Hopper ≤ 1,5", "end")
S.leader(DEPTH + air + 0.75, 40, DEPTH + 14, 30, "Tür")
S.leader(DEPTH + air + 0.75, (SLOT_Y0 + SLOT_Y1) / 2, DEPTH + 14, 72,
         f"Türschlitz {SLOT_Y1 - SLOT_Y0:g} hoch")
S.leader(rivet_pos(RIVET_S[1])[0], rivet_pos(RIVET_S[1])[1], -34, 44, "Blindniet Ø3,2 (2 je Seite)", "end")
S.leader(8, FLOOR + 0.5, -34, 16, "Füße (zeigen seitlich), M4 / Niet", "end")
S.hdim(0, DEPTH, -8, f"{DEPTH:g}", ref_y=0)
S.hdim(-air, DEPTH + air, -17, f"Spalt {GAP:g} (messen!)", ref_y=0)
S.vdim(0, RAMP_Y_DOOR, DEPTH + 52, f"{RAMP_Y_DOOR:g}", ref_x=DEPTH)
S.vdim(0, BAFFLE_Y0, DEPTH + 60, f"{BAFFLE_Y0:g}", ref_x=DEPTH)
S.vdim(0, TOP_Y, DEPTH + 68, f"{TOP_Y:g}", ref_x=DEPTH)
S.vdim(0, RAMP_Y_HOPPER, -10, f"{RAMP_Y_HOPPER:g}", ref_x=0)
S.vdim(0, EXIT_Y0, -18, f"{EXIT_Y0:g}", ref_x=-air)
S.vdim(0, EXIT_Y1, -26, f"{EXIT_Y1:g}", ref_x=-air)

finish(Path(__file__).with_name("funnel-3-montage.svg"))


# A4 print wrapper: make_pdf.sh turns this into funnel.pdf via headless Chrome
Path(__file__).with_name("funnel.html").write_text("""<!doctype html><meta charset="utf-8">
<style>@page { size: A4; margin: 0 } body { margin: 0 } img { display: block; width: 210mm; height: 297mm; page-break-after: always }</style>
<img src="funnel-1-teil-a.svg"><img src="funnel-2-teil-b.svg"><img src="funnel-3-montage.svg">
""", encoding="utf-8")
