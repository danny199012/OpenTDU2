"""Spoiler finish textures for the tuner mod (session 56).

paint_gloss.png   near-white clear-coat base; tinted at load (paint = body, or tint =)
carbon_twill.png  bright 2x2 twill weave (tinted for the coloured carbons)
carbon_forged.png marbled forged-carbon chunks
"""
import math
import random
from PIL import Image

OUT = r'C:\OpenTDU2\resources\mods\packages\tuner-garage\tuner\parts'
random.seed(56)


def save(im, name):
    im.save(OUT + '\\' + name)
    print('wrote', name, im.size)


# --- clear-coat paint base: flat, with a very faint diagonal sheen so a big
# panel is not a dead-flat colour block.
S = 128
im = Image.new('RGBA', (S, S))
px = im.load()
for y in range(S):
    for x in range(S):
        v = 246 + 6 * math.sin((x + y) / S * math.pi * 2)
        px[x, y] = (int(v), int(v), int(v), 255)
save(im, 'paint_gloss.png')

# --- twill carbon: each 8px cell is a tow running diagonal-ish; 2x2 twill =
# the light/dark tows shift by one cell per row.
S = 128
CELL = 8
im = Image.new('RGBA', (S, S))
px = im.load()
for y in range(S):
    for x in range(S):
        cx, cy = x // CELL, y // CELL
        # tow direction alternates in 2x2 blocks shifted per row (twill)
        warp = ((cx + cy) // 2) % 2 == 0
        u = (x % CELL) / (CELL - 1)
        v = (y % CELL) / (CELL - 1)
        # a tow is a rounded ridge across its short axis
        ridge = math.sin((v if warp else u) * math.pi)
        base = 34 + 62 * ridge
        # light catches warp tows brighter than weft
        base *= 1.12 if warp else 0.82
        # edge darkening between cells (the gap between tows)
        edge = min(u, 1 - u, v, 1 - v)
        if edge < 0.08:
            base *= 0.55
        n = random.uniform(-3, 3)
        c = max(0, min(255, int(base + n)))
        px[x, y] = (c, c, min(255, c + 3), 255)
save(im, 'carbon_twill.png')

# --- forged carbon: random angular chunks, each with its own brightness and
# a short streak direction.
S = 128
im = Image.new('RGBA', (S, S), (22, 22, 24, 255))
px = im.load()
chunks = []
for _ in range(120):
    chunks.append((random.randrange(S), random.randrange(S), random.uniform(0, math.pi),
                   random.uniform(4, 11), random.uniform(1.2, 3.2), random.uniform(0.5, 1.5)))
for y in range(S):
    for x in range(S):
        best = None
        total = 0.0
        for (cx, cy, ang, ln, wd, br) in chunks:
            # wrap-around so the texture tiles
            dx = (x - cx + S / 2) % S - S / 2
            dy = (y - cy + S / 2) % S - S / 2
            u = dx * math.cos(ang) + dy * math.sin(ang)
            v = -dx * math.sin(ang) + dy * math.cos(ang)
            if abs(u) < ln and abs(v) < wd:
                total = max(total, br * (1.0 - abs(v) / wd * 0.5))
        base = 26 + 46 * total
        base += random.uniform(-2, 2)
        c = max(0, min(255, int(base)))
        px[x, y] = (c, c, min(255, c + 2), 255)
save(im, 'carbon_forged.png')
