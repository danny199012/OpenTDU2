#!/usr/bin/env python3
"""Decode a TDUSC level `.P3D` (road / spline / imposter meshlet meshes) into world-space triangles.

    python level_mesh.py --pkg-dir "<TDUSC>/WIN32/PKG" --toc-dir C:/temp/tdusc-scan --tile 63 42 [--out C:/temp/hk_roads] [--kind ROADBUILDSTEP]

Format [DATA, docs/re/tdusc_level_meshes.md]:
* `LEVELS/HONGKONG/TILE_Xi/TILE_Xi_Yj/<STEP>/<name>.P3D` is the same `SCENE___` container as the cars (blob = GDeflate at header word 13), but
  each mesh is a *named* entry `u32 len, name ("ROADMESHLET"), 10 floats (bbox min xyz, max xyz, sphere), u32 nparts`, part = as cars.
* vertex streams: `vcol` 4 B, `vnor` 4 B (packed), `vpos` **one u64**: z = bits 0-15, y = bits 16-29, x = bits 30-43 (each unorm across the mesh bbox; bits 44-63 not decoded), `vuvs` 4 B, `vuvy` 8 B.
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
GROUND = None


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


def actor_translations(head):
    """Every `Actor_<id>_<n>` node record: name, then 16 floats (4x4 row-major), translation = row 3. The node parser cannot read these files
    (many actors, no mesh index in the record), so the translation of each mesh is picked by fitting it to the terrain (see decode)."""
    out = []
    for m in re.finditer(rb'Actor_\d+_\d+', head):
        if m.end() + 64 <= len(head):
            f = struct.unpack_from('<16f', head, m.end())
            out.append(np.array(f[12:15], np.float64))
    return out


def decode(d, origin=(0.0, 0.0), ground=None):
    """-> (V (n,3) float64 in WORLD metres (origin = tile SW corner added), F (m,3) int) for every mesh of the P3D.
    ground = (array, x0, y0, step): a heightfield used to choose, per mesh, which actor translation fits (median |z - ground|)."""
    bo = struct.unpack_from('<I', d, 0x34)[0]
    head, blob = d[:bo], gdeflate.decompress(d[bo:])
    meshes = parse_named_meshes(head)
    trs = actor_translations(head) or [np.zeros(3)]
    Vs, Fs, base = [], [], 0
    for mesh in meshes:
        mn = np.array(mesh['bbox'][:3]); mx = np.array(mesh['bbox'][3:6])
        for p in mesh['parts']:
            vc = p['vc']
            if 'vpos' not in p['st'] or vc == 0:
                continue
            u = np.frombuffer(blob, '<u8', count=vc, offset=p['st']['vpos'])
            loc = mn + np.stack([((u >> np.uint64(30)) & np.uint64(0x3FFF)).astype(np.float64) / 0x3FFF,
                                 ((u >> np.uint64(16)) & np.uint64(0x3FFF)).astype(np.float64) / 0x3FFF,
                                 (u & np.uint64(0xFFFF)).astype(np.float64) / 0xFFFF], 1) * (mx - mn)
            idx = np.frombuffer(blob, 'u1', count=p['ntri'] * 3, offset=p['ib']).reshape(-1, 3).astype(np.int64)
            vb = 0
            tris = []
            for a, b, n in split_meshlets(idx):
                tris.append(idx[a:b] + vb)
                vb += n
            F = np.vstack(tris)
            if vb != vc or F.max() >= vc:
                continue                      # meshlet split did not add up: skip rather than emit garbage
            tr = trs[0]
            if ground is not None and len(trs) > 1:
                G, gx0, gy0, gs = ground
                best = None
                sub = loc[::max(1, len(loc) // 64)]
                for t in trs:
                    w = sub + t
                    c = ((origin[0] + w[:, 0] - gx0) / gs).astype(int)
                    r = ((origin[1] + w[:, 1] - gy0) / gs).astype(int)
                    if c.min() < 0 or r.min() < 0 or c.max() >= G.shape[1] or r.max() >= G.shape[0]:
                        continue
                    e = float(np.nanmedian(np.abs(G[r, c] - w[:, 2])))
                    if best is None or e < best[0]:
                        best = (e, t)
                if best:
                    tr = best[1]
            Vs.append(loc + tr + [origin[0], origin[1], 0.0])
            Fs.append(F + base)
            base += vc
    if not Vs:
        return np.zeros((0, 3)), np.zeros((0, 3), np.int64)
    return np.vstack(Vs), np.vstack(Fs)


def load_ground(path='C:/temp/hk_island_4m.npy'):
    return (np.load(path), -9472, -8448, 4.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pkg-dir', required=True)
    ap.add_argument('--toc-dir', required=True)
    ap.add_argument('--tile', nargs=2, type=int)
    ap.add_argument('--all', action='store_true', help='every tile that has this kind of mesh: writes one npz per tile')
    ap.add_argument('--kind', default='ROADBUILDSTEP')
    ap.add_argument('--out')
    a = ap.parse_args()
    global GROUND
    GROUND = load_ground()
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
        V, F = decode(t.read(p), (ox, oy), GROUND)
        print(p.split(BS)[-1], len(V), 'verts', len(F), 'tris')
        if len(V):
            allv.append(V); allf.append(F + base); base += len(V)
    if a.out and allv:
        os.makedirs(a.out, exist_ok=True)
        np.savez(os.path.join(a.out, 'TILE_X%d_Y%d_%s.npz' % (i, j, a.kind)), V=np.vstack(allv), F=np.vstack(allf))


if __name__ == '__main__':
    main()
