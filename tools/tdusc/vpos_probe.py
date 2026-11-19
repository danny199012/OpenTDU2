#!/usr/bin/env python3
"""Per-bit edge-smoothness probe for the level-mesh `vpos` stream (docs/re/tdusc_pws_format.md 2p).

    python vpos_probe.py --pkg-dir "<TDUSC>/WIN32/PKG" --toc-dir C:/temp/tdusc-scan "WIN32\GRAPH\INTERIOR\CAR_DEALERS\MESH\CARDEALER_COUCH_01.P3D"

For the largest part of the first mesh it prints, per bit of the 64-bit vpos word, how often that bit differs between the two ends of an
index edge (pe) versus a random vertex pair (pr). Bits that belong to the most significant part of a coordinate are smooth (pe/pr << 1); noise bits are ~1.
"""
import argparse, os, struct, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gdeflate, level_mesh as lm
from tdusc_read import Tdusc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pkg-dir', required=True); ap.add_argument('--toc-dir', required=True); ap.add_argument('path')
    a = ap.parse_args()
    d = Tdusc(a.pkg_dir, a.toc_dir).read(a.path)
    bo = struct.unpack_from('<I', d, 0x34)[0]
    head, blob = d[:bo], gdeflate.decompress(d[bo:])
    m = lm.parse_named_meshes(head)[0]
    p = max(m['parts'], key=lambda q: q['vc'])
    U = np.frombuffer(blob, '<u8', count=p['vc'], offset=p['st']['vpos'])
    idx = np.frombuffer(blob, 'u1', count=p['ntri'] * 3, offset=p['ib']).reshape(-1, 3).astype(int)
    F, vb = [], 0
    for s, e, n in lm.split_meshlets(idx):
        F.append(idx[s:e] + vb); vb += n
    F = np.vstack(F)
    E = np.concatenate([F[:, [0, 1]], F[:, [1, 2]], F[:, [2, 0]]])
    R = np.random.default_rng(1).integers(0, len(U), (len(E), 2))
    print(m['name'], 'bbox', [round(x, 3) for x in m['bbox']], 'vc', p['vc'])
    for b in range(64):
        bit = ((U >> np.uint64(b)) & np.uint64(1)).astype(int)
        pe = (bit[E[:, 0]] != bit[E[:, 1]]).mean(); pr = (bit[R[:, 0]] != bit[R[:, 1]]).mean()
        print('bit %2d pe %.3f pr %.3f ratio %.2f' % (b, pe, pr, pe / pr if pr else -1))


if __name__ == '__main__':
    main()
