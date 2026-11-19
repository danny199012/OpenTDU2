#!/usr/bin/env python3
"""Writes the decoded TDUSC Hong Kong road meshes as mod props (source/game/spots/mod_props.h).

    python tools/tdusc/... (road decode -> C:/temp/hk_roads/*.npz, arrays V (HK world xyz) and F)
    python tools/mod/make_hongkong_roads.py --userdata C:/temp/otdu2-win-userdata

The decode has ~23M triangles (LOD0 meshlets), far too many to draw, so each 512 m cell is vertex-clustered on a
grid (--grid metres, xy; z averaged) and degenerate triangles dropped. Road meshes are within ~0.3 m of the terrain
raster ([DATA] docs/re/tdusc_pws_format.md 2q), so they are lifted by --lift and carry no collision (the terrain
already has it). One OBJ per cell, Blender convention (loader mirrors X): obj x = -(Eden x - cell x), z = Eden z - cell z.
"""
import argparse
import glob
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_hongkong_island as mh  # noqa: E402

CELL = 512.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--userdata', required=True)
    ap.add_argument('--roads', default='C:/temp/hk_roads')
    ap.add_argument('--pattern', default='*.npz')
    ap.add_argument('--prefix', default='hk_r_', help='prop name prefix (also what a rerun replaces in props.txt)')
    ap.add_argument('--colour', default='70,72,76')
    ap.add_argument('--max-edge', type=float, default=20.0, help='drop triangles with an edge longer than this (decode noise)')
    ap.add_argument('--island', default='hongkong')
    ap.add_argument('--grid', type=float, default=1.0)
    ap.add_argument('--lift', type=float, default=0.08)
    a = ap.parse_args()
    cells = {}
    for fn in sorted(glob.glob(os.path.join(a.roads, a.pattern))):
        d = np.load(fn)
        V, F = d['V'], d['F']
        if len(F) == 0:
            continue
        T0 = V[F]
        F = F[np.max(np.linalg.norm(T0 - np.roll(T0, 1, 1), axis=2), 1) <= a.max_edge]
        if len(F) == 0:
            continue
        ex = V[:, 0] - mh.HK_X0
        ez = mh.HK_Y_NORTH - V[:, 1]
        ey = V[:, 2] + mh.HEIGHT_SHIFT
        P = np.stack([ex, ey, ez], 1)
        c = P[F].mean(1)
        ci = np.floor(c[:, 0] / CELL).astype(np.int64)
        cj = np.floor(c[:, 2] / CELL).astype(np.int64)
        for key in set(zip(ci.tolist(), cj.tolist())):
            if key[0] < 0 or key[1] < 0:      # decode noise outside the island
                continue
            m = (ci == key[0]) & (cj == key[1])
            cells.setdefault(key, []).append(P[F[m]])      # (n,3,3) triangles
    mods = os.path.join(a.userdata, 'mods')
    pdir = os.path.join(mods, 'props')
    os.makedirs(pdir, exist_ok=True)
    from PIL import Image
    Image.new('RGB', (16, 16), tuple(int(x) for x in a.colour.split(','))).save(os.path.join(pdir, a.prefix + 'tex.png'))
    with open(os.path.join(pdir, a.prefix + 'mat.mtl'), 'w') as f:
        f.write('newmtl hk_road\nKd 1 1 1\nKa 0 0 0\nKs 0 0 0\nmap_Kd hk_road.png\n\n')
    lines = []
    ntri = 0
    for (ci, cj), tl in sorted(cells.items()):
        T = np.concatenate(tl)
        ex0, ez0 = ci * CELL + CELL / 2, cj * CELL + CELL / 2
        q = np.floor(T.reshape(-1, 3) / np.array([a.grid, 1e9, a.grid])).astype(np.int64)
        q[:, 1] = 0
        uq, inv = np.unique(q, axis=0, return_inverse=True)
        inv = inv.reshape(-1)
        cnt = np.bincount(inv).astype(float)
        P = T.reshape(-1, 3)
        C = np.stack([np.bincount(inv, P[:, k]) / cnt for k in range(3)], 1)
        tri = inv.reshape(-1, 3)
        ok = (tri[:, 0] != tri[:, 1]) & (tri[:, 1] != tri[:, 2]) & (tri[:, 0] != tri[:, 2])
        tri = tri[ok]
        # drop duplicate triangles (the decode lists overlapping pieces)
        tri = np.unique(np.sort(tri, 1), axis=0)
        if len(tri) == 0:
            continue
        used = np.unique(tri)
        remap = np.zeros(len(C), np.int64)
        remap[used] = np.arange(1, len(used) + 1)
        C = C[used]
        # orient every triangle upward (roads are drawn from above; the decode winding is not trusted)
        t = remap[tri]
        A, B, D = C[t[:, 0] - 1], C[t[:, 1] - 1], C[t[:, 2] - 1]
        # the loader mirrors X, so work in obj space for the normal sign
        ox = -(C[:, 0] - ex0)
        oy = C[:, 1] + a.lift
        oz = C[:, 2] - ez0
        O = np.stack([ox, oy, oz], 1)
        n = np.cross(O[t[:, 1] - 1] - O[t[:, 0] - 1], O[t[:, 2] - 1] - O[t[:, 0] - 1])
        flip = n[:, 1] < 0
        t[flip] = t[flip][:, [0, 2, 1]]
        name = '%s%d_%d' % (a.prefix, ci, cj)
        with open(os.path.join(pdir, name + '.obj'), 'w') as f:
            f.write('mtllib %smat.mtl\nvt 0 0\nusemtl %smat\n' % (a.prefix, a.prefix))
            f.write(''.join('v %.2f %.2f %.2f\n' % tuple(r) for r in O))
            f.write(''.join('f %d/1 %d/1 %d/1\n' % tuple(r) for r in t))
        ntri += len(t)
        lines.append('%s %s %.1f 0 %.1f 0' % (a.island, name, ex0, ez0))
    pp = os.path.join(mods, 'props.txt')
    keep = []
    if os.path.exists(pp):
        for l in open(pp).read().splitlines():
            t = l.split()
            if l.strip() and not (len(t) > 1 and t[1].startswith(a.prefix)) and l.strip() != '# Hong Kong roads (make_hongkong_roads.py)':
                keep.append(l)
    with open(pp, 'w') as f:
        f.write('\n'.join(keep + ['# Hong Kong roads (make_hongkong_roads.py)'] + lines) + '\n')
    print('%d cells, %d triangles' % (len(lines), ntri))


if __name__ == '__main__':
    main()
