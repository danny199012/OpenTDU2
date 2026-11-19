#!/usr/bin/env python3
"""Render / export the terrain heightfield of a TDUSC .PWS tile (docs/re/tdusc_pws_format.md s.3).

    python pws_height.py <HONGKONG.PWS> --png out.png [--npy out.npy]

Every stream = 256-byte header (2 x [3x3 rotation, centre xyz, half extents xyz], then 34 floats) + 1 pad byte +
float32 heights 33x33 (rows = x after a transpose) + other data. A leaf is a 16 m cell (0.5 m samples) on a
16 m grid; the tile is 32x32 cells (512 m). World frame: x, y horizontal, z UP (metres).
"""
import argparse, os, struct, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gdeflate, pws_scan

def leaf_cells(path):
    d = open(path, 'rb').read()
    for o, t, last, offs in pws_scan.candidates(d):
        end = o + 8 + 4 * t + (offs[-1] if t > 1 else 0) + offs[0]
        r = gdeflate.decompress(d[o:end])
        if not 4700 < len(r) < 20000:
            continue
        f = struct.unpack('<15f', r[:60])
        if f[9] == 0 and f[10] == 0:
            continue
        h = np.frombuffer(r[257:257 + 4 * 1089], '<f4').reshape(33, 33).T
        yield f[9], f[10], f[11], h

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pws'); ap.add_argument('--png'); ap.add_argument('--npy')
    a = ap.parse_args()
    cells = list(leaf_cells(a.pws))
    if not cells:
        sys.exit('no leaf cells')
    x0 = min(c[0] for c in cells) - 8; y0 = min(c[1] for c in cells) - 8
    nx = int(round((max(c[0] for c in cells) - 8 - x0) / 16)) + 1
    ny = int(round((max(c[1] for c in cells) - 8 - y0) / 16)) + 1
    H = np.full((ny * 32, nx * 32), np.nan, np.float32)
    for cx, cy, cz, h in cells:
        ix = int(round((cx - 8 - x0) / 16)); iy = int(round((cy - 8 - y0) / 16))
        H[iy * 32:iy * 32 + 32, ix * 32:ix * 32 + 32] = h[:32, :32]
    print('origin', x0, y0, 'cells', len(cells), 'size', H.shape, 'z', np.nanmin(H), np.nanmax(H))
    if a.npy:
        np.save(a.npy, H)
    if a.png:
        from PIL import Image
        lo, hi = np.nanpercentile(H, 1), np.nanpercentile(H, 99)
        v = np.nan_to_num(np.clip((H - lo) / (hi - lo), 0, 1), nan=0)
        img = np.stack([v] * 3, -1); img[np.isnan(H)] = [0, .2, .5]
        Image.fromarray((img * 255).astype(np.uint8)).save(a.png)

if __name__ == '__main__':
    main()
