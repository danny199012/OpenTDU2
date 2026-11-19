#!/usr/bin/env python3
"""Decode every Hong Kong .PWS tile into per-tile float32 heightmaps (parallel).

    python pws_island.py --levels "<TDUSC>/WIN32/LEVELS/HONGKONG" --out C:/temp/hk_tiles [--jobs 14] [--limit N]

Placement [DATA, fitted on 5 tiles, docs/re/tdusc_pws_format.md]: tile TILE_X<i>_Y<j> has its south-west corner at
world x = 256*i - 11008, y = 256*j - 11008 (metres), is 512 m square, and tiles form a staggered brick pattern
(X pitch 256 m, Y pitch 256 m, only every 4th Y per column). Each output `<name>.npy` is 1024x1024 float32 (0.5 m samples,
row = y, col = x, NaN = no leaf cell), plus `<name>.org` with `x0 y0`.
Only leaf streams (single GDeflate tile, < 20 KB compressed) are decoded: the parents are coarser copies.
"""
import argparse, os, re, struct, sys, time
from multiprocessing import Pool
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gdeflate, pws_scan

def do_tile(args):
    path, out = args
    name = os.path.basename(os.path.dirname(path))
    dst = os.path.join(out, name + '.npy')
    if os.path.exists(dst):
        return name, -1
    m = re.match(r'TILE_X(\d+)_Y(\d+)', name)
    x0 = 256 * int(m.group(1)) - 11008; y0 = 256 * int(m.group(2)) - 11008
    d = open(path, 'rb').read()
    H = np.full((1024, 1024), np.nan, np.float32)
    n = 0
    for o, t, last, offs in pws_scan.candidates(d):
        if t != 1 or offs[0] > 20000:
            continue
        try:
            r = gdeflate.decompress(d[o:o + 8 + 4 + offs[0]])
        except Exception:
            continue
        if not 4700 < len(r) < 20000:
            continue
        f = struct.unpack('<15f', r[:60])
        if f[9] == 0 and f[10] == 0:
            continue
        ix = int((f[9] - x0) // 16); iy = int((f[10] - y0) // 16)
        if not (0 <= ix < 32 and 0 <= iy < 32):
            continue
        h = np.frombuffer(r[257:257 + 4 * 1089], '<f4').reshape(33, 33).T
        H[iy * 32:iy * 32 + 32, ix * 32:ix * 32 + 32] = h[:32, :32]
        n += 1
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
    jobs.sort(key=lambda j: os.path.getsize(j[0]))
    if a.limit:
        jobs = jobs[:a.limit]
    t0 = time.time()
    with Pool(a.jobs) as p:
        for i, (name, n) in enumerate(p.imap_unordered(do_tile, jobs)):
            print('%d/%d %s cells=%d  %.0fs' % (i + 1, len(jobs), name, n, time.time() - t0), flush=True)

if __name__ == '__main__':
    main()
