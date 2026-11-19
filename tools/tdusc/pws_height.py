#!/usr/bin/env python3
"""Render one TDUSC .PWS tile's heightfield to a PNG / .npy (uses pws_island.tile_height).
    python pws_height.py <HONGKONG.PWS> --png out.png [--npy out.npy]"""
import argparse, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pws_island

ap = argparse.ArgumentParser()
ap.add_argument('pws'); ap.add_argument('--png'); ap.add_argument('--npy')
a = ap.parse_args()
H, x0, y0, n = pws_island.tile_height(a.pws)
if H is None:
    sys.exit('stream count %d != 1364' % -n)
print('origin', x0, y0, 'nodes with data', n, 'coverage %.3f' % np.isfinite(H).mean(), 'z', np.nanmin(H), np.nanmax(H))
if a.npy:
    np.save(a.npy, H)
if a.png:
    from PIL import Image
    lo, hi = np.nanpercentile(H, 1), np.nanpercentile(H, 99)
    v = np.nan_to_num(np.clip((H - lo) / (hi - lo), 0, 1), nan=0)
    img = np.stack([v] * 3, -1); img[np.isnan(H)] = [0, .1, .3]
    Image.fromarray((img[::-1] * 255).astype(np.uint8)).save(a.png)
