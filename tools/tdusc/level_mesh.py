#!/usr/bin/env python3
"""Decode a TDUSC level `.P3D` (road / spline / imposter meshlet meshes) into world-space triangles.

    python level_mesh.py --pkg-dir "<TDUSC>/WIN32/PKG" --toc-dir C:/temp/tdusc-scan --tile 63 42 [--out C:/temp/hk_roads] [--kind ROADBUILDSTEP]

Format [DATA, docs/re/tdusc_level_meshes.md]:
* `LEVELS/HONGKONG/TILE_Xi/TILE_Xi_Yj/<STEP>/<name>.P3D` is the same `SCENE___` container as the cars (blob = GDeflate at header word 13), but
  each mesh is a *named* entry `u32 len, name ("ROADMESHLET"), 10 floats (bbox min xyz, max xyz, sphere), u32 nparts`, part = as cars.
* vertex streams: `vcol` 4 B, `vnor` 4 B (packed), `vpos` **8 B = 4 x unorm16 across the mesh bbox** (xyz, w unused), `vuvs` 4 B, `vuvy` 8 B.
* indices are **u8 triangle lists in MESHLETS of <= 64 vertices**: every meshlet re-starts its local indices at 0 (first-use order, so a
  meshlet's first triangle is 0,1,2) and its vertices follow the previous meshlet's in the vertex arrays.
* the scene graph holds one `Actor_<id>_<n>` node per mesh with the translation that places it: tile-local -> tile origin
  (x = 256*i - 11008, y = 256*j - 11008) + node translation + local.  Checked: ROADBUILDS_0_1_73730 of X63_Y42 sits within 3 m of the heightfield.
"""
import argparse
import os
import re
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gdeflate  # noqa: E402
import p3d_to_obj as P  # noqa: E402
from tdusc_read import Tdusc  # noqa: E402

BS = chr(92)


def split_meshlets(idx):
    """idx: (n,3) u8 local indices -> list of (first_tri, last_tri_exclusive, vertex_count)."""
    out = []
    start = 0
    seen = -1
    for t in range(len(idx)):
        m = int(idx[t].max())
        if t > start and m <= 2 and seen >= 8:
            out.append((start, t, seen + 1))
            start, seen = t, -1
        seen = max(seen, m)
    out.append((start, len(idx), seen + 1))
    return out


def parse_named_meshes(head):
    """Named MESH entries (`u32 len`, name, 10 floats, nparts, parts...) -> list of dict(name, bbox, parts)."""
    res = []
    for m in re.finditer(rb'([\x05-\x20])\x00\x00\x00([A-Z][A-Z0-9_]{3,31})', head):
        ln = m.group(1)[0]
        name = m.group(2)
        if len(name) < ln:
            continue
        p = m.start() + 4 + ln
        if p + 44 > len(head):
            continue
        bb = struct.unpack_from('<10f', head, p)
        npart = struct.unpack_from('<I', head, p + 40)[0]
        if not (0 < npart < 64) or not all(abs(x) < 1e5 for x in bb):
            continue
        q = p + 44
        parts = []
        ok = True
        for _ in range(npart):
            a, nw = struct.unpack_from('<II', head, q)
            if nw == 0 or nw > 12:
                ok = False
                break
            fmt = head[q + 8:q + 8 + nw * 4].rstrip(b'\0').decode('latin1')
            q += 8 + nw * 4
            _z0, _z1, vc, ns = struct.unpack_from('<IIII', head, q)
            q += 16
            st = {}
            for _s in range(ns):
                st[head[q:q + 4].decode('latin1')] = struct.unpack_from('<I', head, q + 4)[0]
                q += 8
            ntri, ib = struct.unpack_from('<II', head, q)
            q += 12
            parts.append(dict(fmt=fmt, vc=vc, st=st, ntri=ntri, ib=ib))
        if ok:
            res.append(dict(name=name.decode('latin1')[:ln], bbox=bb, parts=parts))
    return res


def decode(d):
    """-> (V (n,3) float64 tile-local incl. node translation, F (m,3) int) for every mesh of the P3D."""
    bo = struct.unpack_from('<I', d, 0x34)[0]
    head, blob = d[:bo], gdeflate.decompress(d[bo:])
    meshes = parse_named_meshes(head)
    try:
        nodes = P.parse_nodes(head)
    except Exception:
        nodes = []
    tr = np.zeros(3)
    for n in nodes:
        if n['name'].startswith('Actor_'):
            tr = np.array(n['world'][3, :3], np.float64)
            break
    Vs, Fs, base = [], [], 0
    for mesh in meshes:
        mn = np.array(mesh['bbox'][:3]); mx = np.array(mesh['bbox'][3:6])
        for p in mesh['parts']:
            vc = p['vc']
            if 'vpos' not in p['st'] or vc == 0:
                continue
            raw = np.frombuffer(blob, '<u2', count=vc * 4, offset=p['st']['vpos']).reshape(-1, 4)
            pos = mn + raw[:, :3] / 65535.0 * (mx - mn) + tr
            idx = np.frombuffer(blob, 'u1', count=p['ntri'] * 3, offset=p['ib']).reshape(-1, 3).astype(np.int64)
            vb = 0
            tris = []
            for a, b, n in split_meshlets(idx):
                tris.append(idx[a:b] + vb)
                vb += n
            F = np.vstack(tris)
            if vb != vc or F.max() >= vc:
                continue                      # meshlet split did not add up: skip rather than emit garbage
            Vs.append(pos)
            Fs.append(F + base)
            base += vc
    if not Vs:
        return np.zeros((0, 3)), np.zeros((0, 3), np.int64)
    return np.vstack(Vs), np.vstack(Fs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pkg-dir', required=True)
    ap.add_argument('--toc-dir', required=True)
    ap.add_argument('--tile', nargs=2, type=int)
    ap.add_argument('--all', action='store_true', help='every tile that has this kind of mesh: writes one npz per tile')
    ap.add_argument('--kind', default='ROADBUILDSTEP')
    ap.add_argument('--out')
    a = ap.parse_args()
    t = Tdusc(a.pkg_dir, a.toc_dir)
    if a.all:
        tiles = sorted({tuple(int(x) for x in re.search(r'TILE_X(\d+)_Y(\d+)', k).groups()) for k in t.entries
                        if (BS + a.kind + BS) in k.upper() and BS + 'LEVELS' + BS + 'HONGKONG' in k.upper()})
        for ti, tj in tiles:
            run_tile(t, ti, tj, a)
        return
    run_tile(t, a.tile[0], a.tile[1], a)


def run_tile(t, i, j, a):
    pre = BS.join(['WIN32', 'LEVELS', 'HONGKONG', 'TILE_X%d' % i, 'TILE_X%d_Y%d' % (i, j), a.kind])
    ox, oy = 256 * i - 11008, 256 * j - 11008
    allv, allf, base = [], [], 0
    for p in t.ls(pre):
        if not p.upper().endswith('.P3D'):
            continue
        V, F = decode(t.read(p))
        print(p.split(BS)[-1], len(V), 'verts', len(F), 'tris')
        if len(V):
            allv.append(V + [ox, oy, 0]); allf.append(F + base); base += len(V)
    if a.out and allv:
        os.makedirs(a.out, exist_ok=True)
        np.savez(os.path.join(a.out, 'TILE_X%d_Y%d_%s.npz' % (i, j, a.kind)), V=np.vstack(allv), F=np.vstack(allf))


if __name__ == '__main__':
    main()
