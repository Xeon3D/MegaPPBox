"""MegaPPBox logo: the cabinet mark + the wordmark (Fredoka Bold, outlined).

Needs fontTools (pip install fonttools) and Fredoka's variable font next to this
script as Fredoka.ttf (Google Fonts, SIL Open Font License 1.1:
https://github.com/google/fonts/tree/main/ofl/fredoka).  Render PNGs with
rsvg-convert.  Usage: python make_logo.py [output folder]

Writes, into OUT:
  megappbox-mark.svg           the cabinet alone (square)
  megappbox-logo.svg           mark + wordmark, side by side (for light backgrounds)
  megappbox-logo-dark.svg      the same for dark backgrounds
  megappbox-logo-stacked.svg   mark over wordmark
"""
import os
import sys
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'out')
os.makedirs(OUT, exist_ok=True)

# ---- palette (from the app icon) ----
NAVY = '#1B1D2E'
CREAM = '#F5E8CB'
CREAM_SHADE = '#E2CFA6'
CREAM_EDGE = '#C9B486'
SCREEN = '#16182A'
BLUE, GREEN, PURPLE, ORANGE = '#2F7FF0', '#2BBF4F', '#9A4DE6', '#FF9D1E'
RED = '#E8413A'
CORAL = '#FB5D52'
YELLOW = '#FFD23F'
WHITE = '#FFFFFF'


def star(cx, cy, r_out, r_in, n=5, rot=-90):
    import math
    pts = []
    for i in range(2 * n):
        r = r_out if i % 2 == 0 else r_in
        a = math.radians(rot + i * 180 / n)
        pts.append(f'{cx + r * math.cos(a):.1f},{cy + r * math.sin(a):.1f}')
    return 'M' + ' L'.join(pts) + ' Z'


def mark(dark=False):
    """The cabinet, in a 512 x 512 box."""
    edge = '#0B0C14' if dark else NAVY
    s = []
    # side vents ("ears") behind the bezel
    s.append(f'<rect x="34" y="118" width="70" height="200" rx="26" fill="{CREAM_SHADE}"/>')
    s.append(f'<rect x="408" y="118" width="70" height="200" rx="26" fill="{CREAM_SHADE}"/>')
    s.append(f'<rect x="50" y="150" width="16" height="136" rx="8" fill="{edge}" opacity="0.85"/>')
    s.append(f'<rect x="446" y="150" width="16" height="136" rx="8" fill="{edge}" opacity="0.85"/>')
    # stand
    s.append(f'<path d="M132 378 H380 L392 448 H120 Z" fill="#2A2C40"/>')
    s.append(f'<rect x="150" y="398" width="212" height="13" rx="6.5" fill="{RED}"/>')
    s.append(f'<rect x="156" y="420" width="200" height="11" rx="5.5" fill="{ORANGE}"/>')
    s.append(f'<rect x="92" y="446" width="328" height="30" rx="12" fill="{edge}"/>')
    # bezel: shade, body, highlight
    s.append(f'<rect x="72" y="46" width="368" height="346" rx="58" fill="{CREAM_EDGE}"/>')
    s.append(f'<rect x="72" y="40" width="368" height="340" rx="58" fill="{CREAM}"/>')
    s.append(f'<rect x="90" y="54" width="332" height="20" rx="10" fill="#FFF6E2" opacity="0.8"/>')
    # screen
    s.append(f'<rect x="106" y="80" width="300" height="264" rx="30" fill="{SCREEN}"/>')
    # tiles 2 x 2
    x0, y0, w, h, g = 120, 94, 130, 112, 12
    tiles = [(x0, y0, BLUE), (x0 + w + g, y0, GREEN), (x0, y0 + h + g, PURPLE), (x0 + w + g, y0 + h + g, ORANGE)]
    for x, y, c in tiles:
        s.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="18" fill="{c}"/>')
        s.append(f'<rect x="{x + 8}" y="{y + 6}" width="{w - 16}" height="10" rx="5" fill="#FFFFFF" opacity="0.18"/>')
    # blue: dartboard
    cx, cy = x0 + w / 2, y0 + h / 2 + 2
    s.append(f'<circle cx="{cx}" cy="{cy}" r="38" fill="{RED}"/>')
    s.append(f'<circle cx="{cx}" cy="{cy}" r="29" fill="{WHITE}"/>')
    s.append(f'<circle cx="{cx}" cy="{cy}" r="19" fill="{RED}"/>')
    s.append(f'<circle cx="{cx}" cy="{cy}" r="9" fill="{WHITE}"/>')
    s.append(f'<path d="M{cx + 2} {cy - 2} L{cx + 34} {cy - 34}" stroke="{YELLOW}" stroke-width="7" stroke-linecap="round"/>')
    s.append(f'<path d="M{cx + 30} {cy - 44} L{cx + 44} {cy - 30} L{cx + 40} {cy - 40} Z" fill="{BLUE}" stroke="{WHITE}" stroke-width="4" stroke-linejoin="round"/>')
    # green: two cards with a heart and a spade
    gx, gy = x0 + w + g + w / 2, y0 + h / 2 + 2
    s.append(f'<g transform="rotate(-12 {gx - 14} {gy})"><rect x="{gx - 44}" y="{gy - 38}" width="54" height="74" rx="9" fill="{WHITE}" stroke="{NAVY}" stroke-opacity="0.15" stroke-width="2"/>'
             f'<g transform="translate({gx - 17} {gy - 2}) scale(1.15)" fill="{NAVY}">'
             f'<path d="M0 -16 C 6 -7 17 -1 14 8 C 12 14 4 14 0 9 C -4 14 -12 14 -14 8 C -17 -1 -6 -7 0 -16 Z"/>'
             f'<path d="M-2 6 L2 6 L6 18 L-6 18 Z"/></g></g>')
    s.append(f'<g transform="rotate(10 {gx + 14} {gy})"><rect x="{gx - 10}" y="{gy - 36}" width="54" height="74" rx="9" fill="{WHITE}" stroke="{NAVY}" stroke-opacity="0.15" stroke-width="2"/>'
             f'<path d="M{gx + 17} {gy + 8} l -15 -15 c -9 -10 4 -22 15 -10 c 11 -12 24 0 15 10 Z" fill="{RED}"/></g>')
    # purple: puzzle piece
    px, py = x0 + w / 2, y0 + h + g + h / 2 + 4
    s.append(f'<g fill="{YELLOW}"><rect x="{px - 30}" y="{py - 26}" width="60" height="56" rx="8"/>'
             f'<circle cx="{px}" cy="{py - 30}" r="13"/><circle cx="{px + 34}" cy="{py + 2}" r="13"/></g>'
             f'<circle cx="{px - 30}" cy="{py + 2}" r="11" fill="{PURPLE}"/>')
    # orange: bowling pins and ball
    bx, by = x0 + w + g + w / 2, y0 + h + g + h / 2 + 4
    for dx, sc in ((-26, 0.85), (26, 0.85), (0, 1.0)):
        cxp = bx + dx
        top = by - 44 * sc + (0 if dx == 0 else 8)
        s.append(f'<g transform="translate({cxp} {top}) scale({sc})">'
                 f'<circle cx="0" cy="10" r="10" fill="{WHITE}"/>'
                 f'<path d="M-6 18 C -7 30 -18 40 -18 58 C -18 74 -10 82 0 82 C 10 82 18 74 18 58 C 18 40 7 30 6 18 Z" fill="{WHITE}"/>'
                 f'<rect x="-7" y="24" width="14" height="4" rx="2" fill="{RED}"/><rect x="-8" y="31" width="16" height="4" rx="2" fill="{RED}"/></g>')
    s.append(f'<circle cx="{bx - 6}" cy="{by + 22}" r="22" fill="{RED}"/>'
             f'<circle cx="{bx - 12}" cy="{by + 14}" r="3.4" fill="#7A1612"/><circle cx="{bx - 3}" cy="{by + 12}" r="3.4" fill="#7A1612"/><circle cx="{bx - 9}" cy="{by + 23}" r="3.4" fill="#7A1612"/>')
    # the star where the tiles meet
    scx, scy = x0 + w + g / 2, y0 + h + g / 2
    s.append(f'<path d="{star(scx, scy, 30, 14)}" fill="{YELLOW}" stroke="{SCREEN}" stroke-width="6" stroke-linejoin="round"/>')
    return '\n  '.join(s)


# ---- wordmark: Fredoka at weight 700, outlined ----
font = instancer.instantiateVariableFont(TTFont(os.path.join(HERE, 'Fredoka.ttf')), {'wght': 700, 'wdth': 100})
cmap = font.getBestCmap()
glyphs = font.getGlyphSet()
upm = font['head'].unitsPerEm
asc = font['hhea'].ascent


def word(parts, size, x, baseline, tracking=0.0):
    """parts: [(text, fill)]; returns (svg, advance width)."""
    scale = size / upm
    out = []
    pen_x = x
    for text, fill in parts:
        for ch in text:
            gname = cmap[ord(ch)]
            pen = SVGPathPen(glyphs)
            glyphs[gname].draw(TransformPen(pen, (scale, 0, 0, -scale, pen_x, baseline)))
            d = pen.getCommands()
            if d:
                out.append(f'<path d="{d}" fill="{fill}"/>')
            pen_x += glyphs[gname].width * scale + tracking * size
    return '\n  '.join(out), pen_x - x - tracking * size


def svg(w, h, body, title='MegaPPBox'):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:.0f} {h:.0f}" width="{w:.0f}" height="{h:.0f}" '
            f'role="img" aria-label="{title}">\n  <title>{title}</title>\n  {body}\n</svg>\n')


def write(name, text):
    with open(os.path.join(OUT, name), 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print('wrote', name)


write('megappbox-mark.svg', svg(512, 512, mark()))

for dark in (False, True):
    ink = '#FFF4DE' if dark else NAVY
    size = 210
    wm, wm_w = word([('Mega', ink), ('PP', CORAL), ('Box', ink)], size, 0, 0, tracking=-0.01)
    gap = 60
    mark_h = 512
    total_w = mark_h + gap + wm_w + 20
    # centre the cap height on the cabinet's screen
    cap = font['OS/2'].sCapHeight * size / upm
    baseline = 212 + cap / 2
    body = (f'<g>{mark(dark)}</g>\n  <g transform="translate({mark_h + gap:.1f} {baseline:.1f})">{wm}</g>')
    write('megappbox-logo-dark.svg' if dark else 'megappbox-logo.svg', svg(total_w, mark_h, body))

# stacked
size = 150
wm, wm_w = word([('Mega', NAVY), ('PP', CORAL), ('Box', NAVY)], size, 0, 0, tracking=-0.01)
W = max(512, wm_w + 40)
body = (f'<g transform="translate({(W - 512) / 2:.1f} 0)">{mark()}</g>\n'
        f'  <g transform="translate({(W - wm_w) / 2:.1f} {512 + 40 + font["OS/2"].sCapHeight * size / upm:.1f})">{wm}</g>')
write('megappbox-logo-stacked.svg', svg(W, 512 + 40 + size * 0.95, body))
