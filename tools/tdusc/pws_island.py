#!/usr/bin/env python3
"""Decode Hong Kong .PWS tiles into per-tile float32 heightmaps (parallel).

    python pws_island.py --levels "<TDUSC>/WIN32/LEVELS/HONGKONG" --out C:/temp/hk_tiles [--jobs 16] [--limit N]

Format [DATA, docs/re/tdusc_pws_format.md]:
* A tile TILE_X<i>_Y<j> is 512 m square with its SW corner at world x = 256*i - 11008, y = 256*j - 11008 (m).
* It holds 1364 GDeflate streams = a quadtree in DEPTH-FIRST PRE-ORDER: 4 cells of 256 m (order (0,0) (1,0) (0,1) (1,1), x fast),
  each followed by its 4 children, down to 16 m (4 + 16 + 64 + 256 + 1024 = 1364). The k-th stream in file order is the k-th node.
* Stream = 256-byte header (box 1: 3x3 rotation, centre xyz, half extents; box 2; 34 floats), 1 pad byte, then float32 data:
  a 33x33 heightfield at +257, rows stored TOP-DOWN in y (flip rows to get y up), 32 intervals per cell side. Header centre (0,0) = no data.
  Verified: every parent equals its children's samples decimated 2x (mean abs diff 0.0 at all 4 levels, X63_Y42), and 1280/1280 box
  centres lie within 0.6*S of their node's cell centre.
Output `<name>.npy`: 1024x1024 float32, 0.5 m/px, row = y (south first), NaN = no data; `<name>.org` = "x0 y0".
"""
import argparse, os, re, struct, sys, time
from multiprocessing import Pool
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gdeflate, pws_scan

def nodes():
    out = []
    def walk(S, ix, iy):
        out.append((S, ix, iy))
        if S > 16:
            for dy in (0, 1):
                for dx in (0, 1):
                    walk(S // 2, ix * 2 + dx, iy * 2 + dy)
    for qy in (0, 1):
        for qx in (0, 1):
            walk(256, qx, qy)
    return out

def _bilerp(h, S):
    """33x33 (y up) -> (2S)x(2S) pixels at 0.5 m, sampled at pixel centres."""
    n = int(S * 2)
    u = np.clip((np.arange(n) + 0.5) * (32.0 / n), 0, 32)
    i0 = np.minimum(u.astype(int), 31); t = (u - i0)
    a = h[i0][:, i0]; b = h[i0][:, i0 + 1]; c = h[i0 + 1][:, i0]; d = h[i0 + 1][:, i0 + 1]
    tx = t[None, :]; ty = t[:, None]
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty

def tile_height(path):
    """-> (H 1024x1024 float32 or None, x0, y0, nodes_with_data)"""
    m = re.search(r'TILE_X(\d+)_Y(\d+)', path)
    x0 = 256 * int(m.group(1)) - 11008; y0 = 256 * int(m.group(2)) - 11008
    d = open(path, 'rb').read()
    cand = list(pws_scan.candidates(d))
    tree = nodes()
    items = []
    if len(cand) != len(tree):
        return None, x0, y0, -len(cand)
    for (S, ix, iy), (o, t, last, offs) in zip(tree, cand):
        end = o + 8 + 4 * t + (offs[-1] if t > 1 else 0) + offs[0]
        try:
            r = gdeflate.decompress(d[o:end])
        except Exception:
            continue
        if len(r) < 257 + 4 * 1089:
            continue
        f = struct.unpack('<15f', r[:60])
        if f[9] == 0 and f[10] == 0:
            continue
        h = np.frombuffer(r[257:257 + 4 * 1089], '<f4').reshape(33, 33)[::-1]
        if np.isfinite(h).all():
            items.append((S, ix, iy, h))
    H = np.full((1024, 1024), np.nan, np.float32)
    for S, ix, iy, h in sorted(items, key=lambda i: -i[0]):          # coarse first, fine overwrites
        n = int(S * 2)
        H[iy * n:iy * n + n, ix * n:ix * n + n] = _bilerp(h, S)
    return H, x0, y0, len(items)

def do_tile(args):
    path, out = args
    name = os.path.basename(os.path.dirname(path))
    dst = os.path.join(out, name + '.npy')
    if os.path.exists(dst):
        return name, -1
    H, x0, y0, n = tile_height(path)
    if H is None:
        return name, n
    np.save(dst, H)
    open(os.path.join(out, name + '.org'), 'w').write('%d %d\n' % (x0, y0))
    return name, n

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--levels', required=True); ap.add_argument('--out', required=True)
    ap.add_argument('--jobs', type=int, default=8); ap.add_argument('--limit', type=int, default=0)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    jobs = []
    for dp, _, fs in os.walk(a.levels):
        if 'HONGKONG.PWS' in fs:
            jobs.append((os.path.join(dp, 'HONGKONG.PWS'), a.out))
    jobs.sort(key=lambda j: -os.path.getsize(j[0]))   # big first: better load balance
    if a.limit:
        jobs = jobs[:a.limit]
    t0 = time.time()
    with Pool(a.jobs) as p:
        for i, (name, n) in enumerate(p.imap_unordered(do_tile, jobs)):
            print('%d/%d %s nodes=%d  %.0fs' % (i + 1, len(jobs), name, n, time.time() - t0), flush=True)

if __name__ == '__main__':
    main()
