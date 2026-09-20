"""Tiny SVG drawing helpers shared by the enclosure drawing scripts (units: mm)."""

out = []


def add(s):
    out.append(s)


class View:
    """Maps mm coordinates (x right, y up) to page coordinates."""

    def __init__(self, ox, oy, k=1.0, s=1.0):
        self.ox, self.oy = ox, oy  # page position of (0, 0)
        self.s = s                 # drawing scale (page units per mm)
        self.k = k                 # size factor for arrows, leader dots and text offsets

    def p(self, x, y):
        return self.ox + x * self.s, self.oy - y * self.s

    def rect(self, x0, y0, x1, y1, cls, extra=""):
        px, py = self.p(x0, y1)
        add(f'<rect x="{px:.1f}" y="{py:.1f}" width="{(x1 - x0) * self.s:.1f}" height="{(y1 - y0) * self.s:.1f}" class="{cls}" {extra}/>')

    def line(self, x0, y0, x1, y1, cls, extra=""):
        a, b = self.p(x0, y0)
        c, d = self.p(x1, y1)
        add(f'<line x1="{a:.1f}" y1="{b:.1f}" x2="{c:.1f}" y2="{d:.1f}" class="{cls}" {extra}/>')

    def poly(self, pts, cls, closed=True, extra=""):
        s = " ".join(f"{self.p(x, y)[0]:.1f},{self.p(x, y)[1]:.1f}" for x, y in pts)
        tag = "polygon" if closed else "polyline"
        add(f'<{tag} points="{s}" class="{cls}" {extra}/>')

    def circle(self, x, y, r, cls):
        px, py = self.p(x, y)
        add(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="{r}" class="{cls}"/>')

    def text(self, x, y, s, cls="t", anchor="start", rot=0):
        px, py = self.p(x, y)
        tr = f' transform="rotate({rot} {px:.1f} {py:.1f})"' if rot else ""
        add(f'<text x="{px:.1f}" y="{py:.1f}" class="{cls}" text-anchor="{anchor}"{tr}>{s}</text>')

    def _arrow(self, x, y, dx, dy):
        # arrowhead with tip at (x, y) pointing in direction (dx, dy)
        L, Wd = 7 * self.k, 1.8 * self.k
        nx, ny = -dy, dx
        self.poly([(x, y), (x - dx * L + nx * Wd, y - dy * L + ny * Wd),
                   (x - dx * L - nx * Wd, y - dy * L - ny * Wd)], "arrow")

    def hdim(self, x0, x1, y, label, ref_y=None):
        """Horizontal dimension at height y; extension lines come from ref_y."""
        if ref_y is not None:
            for x in (x0, x1):
                self.line(x, ref_y, x, y - 4 * self.k if y < ref_y else y + 4 * self.k, "dim")
        self.line(x0, y, x1, y, "dim")
        self._arrow(x0, y, -1, 0)
        self._arrow(x1, y, 1, 0)
        self.text((x0 + x1) / 2, y + 3 * self.k, label, "d", "middle")

    def vdim(self, y0, y1, x, label, ref_x=None):
        """Vertical dimension at position x; extension lines come from ref_x."""
        if ref_x is not None:
            for y in (y0, y1):
                self.line(ref_x, y, x - 4 * self.k if x < ref_x else x + 4 * self.k, y, "dim")
        self.line(x, y0, x, y1, "dim")
        self._arrow(x, y0, 0, -1)
        self._arrow(x, y1, 0, 1)
        self.text(x - 3 * self.k, (y0 + y1) / 2, label, "d", "middle", rot=-90)

    def leader(self, x, y, tx, ty, label, anchor="start"):
        self.line(x, y, tx, ty, "dim")
        self.circle(x, y, 1.3 * self.k, "arrow")
        self.text(tx + (3 if anchor == "start" else -3) * self.k, ty - 3 * self.k, label, "t", anchor)


STYLE = """<style>
.vis   { fill: none; stroke: #111; stroke-width: 1.6; }
.door  { fill: #f4f4f2; stroke: #111; stroke-width: 1.6; }
.thin  { fill: none; stroke: #111; stroke-width: 0.8; }
.part  { fill: #d9d9d6; stroke: #111; stroke-width: 1.2; }
.cut   { fill: #111; stroke: #111; stroke-width: 0.8; }
.hid   { fill: none; stroke: #666; stroke-width: 0.9; stroke-dasharray: 7 4; }
.hidf  { fill: #ececea; stroke: #666; stroke-width: 0.9; }
.zone  { fill: #2a6fb5; fill-opacity: 0.06; stroke: #2a6fb5; stroke-width: 0.9; stroke-dasharray: 3 3; }
.band  { fill: #c0281c; fill-opacity: 0.10; stroke: #c0281c; stroke-width: 0.9; stroke-dasharray: 7 4; }
.coin  { fill: none; stroke: #c0281c; stroke-width: 1.4; }
.coinh { fill: none; stroke: #c0281c; stroke-width: 1.2; stroke-dasharray: 6 3; }
.coinp { fill: none; stroke: #c0281c; stroke-width: 1; stroke-dasharray: 1.5 3; }
.exit  { stroke: #c0281c; stroke-opacity: 0.55; stroke-linecap: butt; }
.ctr   { fill: none; stroke: #111; stroke-width: 0.5; stroke-dasharray: 12 3 2 3; }
.dim   { fill: none; stroke: #111; stroke-width: 0.5; }
.arrow { fill: #111; stroke: none; }
.cable { fill: none; stroke: #7a5c00; stroke-width: 1.4; stroke-dasharray: 9 3; }
.s     { font-size: 7.5px; }
.wall  { fill: none; stroke: #111; stroke-width: 0.6; }
text   { fill: #111; paint-order: stroke; stroke: #fff; stroke-width: 3px; stroke-linejoin: round; }
.d     { font-size: 10px; }
.t     { font-size: 9.5px; }
.tr    { font-size: 9.5px; fill: #c0281c; }
.tb    { font-size: 9.5px; fill: #2a6fb5; }
.h     { font-size: 14px; font-weight: bold; }
.n     { font-size: 9.5px; stroke: none; }
.nb    { font-size: 9.5px; font-weight: bold; stroke: none; }
.title { font-size: 13px; font-weight: bold; stroke: none; }
</style>"""


def begin(page_w, page_h, print_mm=False):
    """Start a page. With print_mm the SVG is sized in mm, so printing at 100 % gives 1:1."""
    size = f'width="{page_w}mm" height="{page_h}mm"' if print_mm else f'width="{page_w}" height="{page_h}"'
    add(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {page_w} {page_h}" {size} '
        f'font-family="Helvetica, Arial, sans-serif">')
    add(STYLE)
    add(f'<rect x="0" y="0" width="{page_w}" height="{page_h}" fill="#fff"/>')


def finish(path):
    add("</svg>")
    path.write_text("\n".join(out), encoding="utf-8")
    del out[:]
    print(f"wrote {path}")
