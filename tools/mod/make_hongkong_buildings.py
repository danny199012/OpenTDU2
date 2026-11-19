#!/usr/bin/env python3
"""Extrudes the TDUSC Hong Kong building footprints into box buildings and writes them as mod props
(source/game/spots/mod_props.h: <mods>/props.txt + <mods>/props/*.obj).

    python tools/tdusc/plm_footprints.py --pkg-dir "<TDUSC>/WIN32/PKG" --toc-dir C:/temp/tdusc-scan --out C:/temp/hk_footprints.json
    python tools/mod/make_hongkong_buildings.py --userdata C:/temp/otdu2-win-userdata --tiles C:/temp/hk_tiles

Footprints: `ManualBuildingPoly` and the building-like `exclusion` polygons (docs/re/tdusc_pws_format.md 2m). Heights are [INF]
(by name / footprint area, deterministic jitter): the data has no height. Ground = lowest corner on the same height
raster as the island mod; footprints on no-data / underwater ground are skipped. One OBJ per 512 m cell, Blender
convention (the loader mirrors X), so obj x = -(Eden x - cell x), obj z = Eden z - cell z.
"""
import argparse
import json
import math
import os
import re
import sys
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_hongkong_island as mh  # noqa: E402

CELL = 512.0
LIKE = re.compile(r'(BUILDING|TOWER|RESIDENCE|SCHOOL|OFFICE|HOTEL|MALL|HOSPITAL|COMMERCIAL|CARDEALER|GASSTATION)', re.I)


def area2(p):
    return sum(p[i][0] * p[(i + 1) % len(p)][1] - p[(i + 1) % len(p)][0] * p[i][1] for i in range(len(p)))


def clean(pts):
    out = []
    for x, y, _ in pts:
        if not out or math.hypot(x - out[-1][0], y - out[-1][1]) > 0.3:
            out.append((x, y))
    if len(out) > 1 and math.hypot(out[0][0] - out[-1][0], out[0][1] - out[-1][1]) < 0.3:
        out.pop()
    return out


def triangulate(p):
    """Ear clipping of a CCW polygon in 2D; falls back to a fan when no ear is found. Returns index triples."""
    idx = list(range(len(p)))
    tris = []

    def cross(a, b, c):
        return (p[b][0] - p[a][0]) * (p[c][1] - p[a][1]) - (p[b][1] - p[a][1]) * (p[c][0] - p[a][0])

    def inside(a, b, c, q):
        return cross(a, b, q) >= 0 and cross(b, c, q) >= 0 and cross(c, a, q) >= 0

    guard = 0
    while len(idx) > 3 and guard < 10000:
        guard += 1
        for k in range(len(idx)):
            a, b, c = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            if cross(a, b, c) <= 1e-9:
                continue
            if any(inside(a, b, c, j) for j in idx if j not in (a, b, c)):
                continue
            tris.append((a, b, c))
            idx.pop(k)
            break
        else:
            break
    if len(idx) >= 3:
        for k in range(1, len(idx) - 1):
            tris.append((idx[0], idx[k], idx[k + 1]))
    return tris


def building_height(name, area, seed):
    j = 0.75 + 0.5 * (seed % 1000) / 1000.0
    u = name.upper()
    if 'TOWER' in u:
        h = 110.0
    elif 'CARDEALER' in u or 'GASSTATION' in u:
        h = 12.0
    elif 'TOP' in u:
        h = 30.0
    elif 'SCHOOL' in u:
        h = 18.0
    elif area < 150:
        h = 9.0
    elif area < 600:
        h = 22.0
    else:
        h = 45.0
    return h * j


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--userdata', required=True)
    ap.add_argument('--tiles', default='C:/temp/hk_tiles')
    ap.add_argument('--footprints', default='C:/temp/hk_footprints.json')
    ap.add_argument('--island', default='hongkong')
    ap.add_argument('--max-area', type=float, default=8000.0)
    a = ap.parse_args()
    recs = json.load(open(a.footprints))
    world = mh.World(mh.HeightRaster(a.tiles))
    cells = {}
    skipped = {'small': 0, 'big': 0, 'water': 0, 'kind': 0}
    for r in recs:
        if not (r['kind'] == 'manual' or (r['kind'] == 'exclusion' and LIKE.search(r['name']))):
            skipped['kind'] += 1
            continue
        p = clean(r['pts'])
        if len(p) < 3:
            continue
        ar = area2(p)
        if ar < 0:
            p.reverse()
            ar = -ar
        ar *= 0.5
        if ar < 25:
            skipped['small'] += 1
            continue
        if ar > a.max_area:
            skipped['big'] += 1
            continue
        xs = np.array([q[0] for q in p])
        ys = np.array([q[1] for q in p])
        raw = world.r.sample(xs, ys)
        ground = float(raw.min())
        if ground < 0.5:                      # sea, no data (coarse fill is <= -8) or beach under water
            skipped['water'] += 1
            continue
        cx = float(xs.mean())
        cy = float(ys.mean())
        key = (int(math.floor((cx - mh.HK_X0) / CELL)), int(math.floor((mh.HK_Y_NORTH - cy) / CELL)))
        seed = zlib.crc32(r['name'].encode()) ^ int(cx * 7) ^ int(cy * 13)
        g = ground + mh.HEIGHT_SHIFT
        cells.setdefault(key, []).append((r['name'], p, g - 3.0, g + building_height(r['name'], ar, seed), seed))
    mods = os.path.join(a.userdata, 'mods')
    pdir = os.path.join(mods, 'props')
    os.makedirs(pdir, exist_ok=True)
    mtl = 'hk_buildings.mtl'
    from PIL import Image, ImageDraw
    tints = (('hk_wall', (150, 154, 160), (60, 80, 100)), ('hk_wall2', (120, 140, 160), (40, 60, 85)),
             ('hk_wall3', (170, 160, 145), (70, 70, 80)), ('hk_roof', (90, 92, 96), (90, 92, 96)),
             ('hk_dealer', (200, 40, 40), (230, 230, 240)))
    with open(os.path.join(pdir, mtl), 'w') as f:
        for n, wall, win in tints:
            im = Image.new('RGB', (64, 64), wall)
            if n != 'hk_roof':
                dr = ImageDraw.Draw(im)
                for gx in range(2):
                    for gy in range(2):
                        dr.rectangle((4 + gx * 32, 6 + gy * 32, 26 + gx * 32, 26 + gy * 32), fill=win)
            im.save(os.path.join(pdir, n + '.png'))
            f.write('newmtl %s\nKd 1 1 1\nKa 0 0 0\nKs 0 0 0\nmap_Kd %s.png\n\n' % (n, n))
    lines = []
    total = 0
    ntri = 0
    for (ci, cj), bl in sorted(cells.items()):
        ex0 = ci * CELL + CELL / 2          # Eden coordinates of the cell centre
        ez0 = cj * CELL + CELL / 2
        name = 'hk_b_%d_%d' % (ci, cj)
        v = []
        vts = []
        faces = {'hk_wall': [], 'hk_wall2': [], 'hk_wall3': [], 'hk_roof': [], 'hk_dealer': []}

        def vert(px, py, y, u=0.0, vv=0.0):
            ex = px - mh.HK_X0
            ez = mh.HK_Y_NORTH - py
            v.append((-(ex - ex0), y, ez - ez0))
            vts.append((u, vv))
            return len(v)

        for nm, p, y0, y1, seed in bl:
            mat = 'hk_dealer' if 'CARDEALER' in nm.upper() else ('hk_wall', 'hk_wall2', 'hk_wall3')[seed % 3]
            top = [vert(q[0], q[1], y1) for q in p]
            n = len(p)
            rows = max(1.0, (y1 - y0 - 3.0) / 3.5)
            for i in range(n):
                j = (i + 1) % n
                L = math.hypot(p[j][0] - p[i][0], p[j][1] - p[i][1]) / 6.0
                bi = vert(p[i][0], p[i][1], y0, 0.0, 0.0)
                bj = vert(p[j][0], p[j][1], y0, L, 0.0)
                ti = vert(p[i][0], p[i][1], y1, 0.0, rows / 2)
                tj = vert(p[j][0], p[j][1], y1, L, rows / 2)
                faces[mat].append((bi, tj, bj))
                faces[mat].append((bi, ti, tj))
            for t in triangulate(p):
                faces['hk_roof'].append((top[t[0]], top[t[2]], top[t[1]]))
            total += 1
        with open(os.path.join(pdir, name + '.obj'), 'w') as f:
            f.write('mtllib %s\n' % mtl)
            for x, y, z in v:
                f.write('v %.2f %.2f %.2f\n' % (x, y, z))
            for u, vv in vts:
                f.write('vt %.3f %.3f\n' % (u, vv))
            for m, fl in faces.items():
                if fl:
                    f.write('usemtl %s\n' % m)
                    for t in fl:
                        f.write('f %d/%d %d/%d %d/%d\n' % (t[0], t[0], t[1], t[1], t[2], t[2]))
                        ntri += 1
        lines.append('%s %s %.1f 0 %.1f 0 solid' % (a.island, name, ex0, ez0))
    pp = os.path.join(mods, 'props.txt')
    keep = []
    if os.path.exists(pp):
        for l in open(pp).read().splitlines():
            t = l.split()
            if l.strip() and not (len(t) > 1 and t[1].startswith('hk_b_')):
                keep.append(l)
    with open(pp, 'w') as f:
        f.write('\n'.join(keep + ['# Hong Kong box buildings (make_hongkong_buildings.py)'] + lines) + '\n')
    print('%d buildings in %d cells, %d triangles; skipped %s' % (total, len(cells), ntri, skipped))


if __name__ == '__main__':
    main()
