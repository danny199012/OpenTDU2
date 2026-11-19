#!/usr/bin/env python3
"""Road mask for the Hong Kong island mod: where a decoded road PAD agrees with the decoded terrain (docs/re/tdusc_pws_format.md 2g).

    python road_mask.py --pkg-dir "<TDUSC>/WIN32/PKG" --toc-dir C:/temp/tdusc-scan --tiles C:/temp/hk_tiles --out C:/temp/hk_roadmask [--tol 0.4] [--jobs 8]

A ROADBUILDSTEP / INTERSECTIONBUILDSTEP piece is an axis-aligned pad whose triangles interpolate the road surface height; the road itself is cut by an alpha texture,
so the mask keeps the pad pixels where |pad z - terrain z| < tol (the terrain was baked flat under the road). Output per tile: uint8 1024x1024, 0.5 m / px, row = y (south first).
[INF] heuristic: flat non-road ground inside a pad also passes.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import level_mesh as L
from tdusc_read import Tdusc
BS = chr(92)
def raster_z(V, F, ox, oy, n=1024, res=0.5):
    Z = np.full((n, n), np.nan, np.float32)
    px = (V[:, 0] - ox) / res; py = (V[:, 1] - oy) / res
    for a, b, c in F:
        x0 = int(max(0, np.floor(min(px[a], px[b], px[c])))); x1 = int(min(n - 1, np.ceil(max(px[a], px[b], px[c]))))
        y0 = int(max(0, np.floor(min(py[a], py[b], py[c])))); y1 = int(min(n - 1, np.ceil(max(py[a], py[b], py[c]))))
        if x1 < x0 or y1 < y0: continue
        d = (py[b] - py[c]) * (px[a] - px[c]) + (px[c] - px[b]) * (py[a] - py[c])
        if abs(d) < 1e-9: continue
        X, Y = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        l1 = ((py[b] - py[c]) * (X - px[c]) + (px[c] - px[b]) * (Y - py[c])) / d
        l2 = ((py[c] - py[a]) * (X - px[c]) + (px[a] - px[c]) * (Y - py[c])) / d
        l3 = 1 - l1 - l2
        m = (l1 >= 0) & (l2 >= 0) & (l3 >= 0)
        z = l1 * V[a, 2] + l2 * V[b, 2] + l3 * V[c, 2]
        sub = Z[y0:y1 + 1, x0:x1 + 1]
        sub[m] = z[m]
    return Z


def do_tile(a):
    (i, j), pkg, toc, tiles, out, tol = a
    dst = os.path.join(out, 'TILE_X%d_Y%d.npy' % (i, j))
    if os.path.exists(dst):
        return i, j, -1
    ox, oy = 256 * i - 11008, 256 * j - 11008
    hp = os.path.join(tiles, 'TILE_X%d_Y%d.npy' % (i, j))
    if not os.path.exists(hp):
        return i, j, -2
    H = np.load(hp)
    t = Tdusc(pkg, toc)
    Zs = []
    for kind in ('ROADBUILDSTEP', 'INTERSECTIONBUILDSTEP'):
        for p in t.ls(BS.join(['WIN32', 'LEVELS', 'HONGKONG', 'TILE_X%d' % i, 'TILE_X%d_Y%d' % (i, j), kind])):
            if p.upper().endswith('.P3D'):
                try:
                    V, F = L.decode(t.read(p), (ox, oy), t.read(p[:-4] + '.PSI'))
                    if len(V):
                        Zs.append(raster_z(V, F, ox, oy))
                except Exception:
                    pass
    if not Zs:
        np.save(dst, np.zeros((1024, 1024), np.uint8)); return i, j, 0
    import warnings
    with warnings.catch_warnings():
        warnings.simplefilter('ignore')
        Z = np.nanmean(np.stack(Zs), 0)
    m = np.isfinite(Z) & (np.abs(Z - H) < tol)
    np.save(dst, m.astype(np.uint8))
    return i, j, int(m.sum())


def main():
    import argparse, re, time
    from multiprocessing import Pool
    ap = argparse.ArgumentParser()
    ap.add_argument('--pkg-dir', required=True); ap.add_argument('--toc-dir', required=True)
    ap.add_argument('--tiles', required=True); ap.add_argument('--out', required=True)
    ap.add_argument('--tol', type=float, default=0.4); ap.add_argument('--jobs', type=int, default=8)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    t = Tdusc(a.pkg_dir, a.toc_dir)
    tiles = sorted({tuple(int(x) for x in re.search(r'TILE_X(\d+)_Y(\d+)', k).groups()) for k in t.entries
                    if (BS + 'ROADBUILDSTEP' + BS) in k.upper() and (BS + 'LEVELS' + BS + 'HONGKONG') in k.upper()})
    print(len(tiles), 'tiles', flush=True)
    t0 = time.time()
    with Pool(a.jobs) as pool:
        for n, r in enumerate(pool.imap_unordered(do_tile, [(tl, a.pkg_dir, a.toc_dir, a.tiles, a.out, a.tol) for tl in tiles])):
            print(n + 1, r, round(time.time() - t0), flush=True)


if __name__ == '__main__':
    main()
