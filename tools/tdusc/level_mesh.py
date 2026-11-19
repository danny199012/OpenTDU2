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
            parts.append(dict(fmt=fmt, vc=vc, st=st, ntri=ntri, ib=ib, qf=struct.unpack_from('<f', head, q - 4)[0]))
        if ok:
            res.append(dict(name=name.decode('latin1')[:ln], bbox=bb, parts=parts))
    return res


def actor_translations(head):
    """Length-prefixed `Actor_<id>_<n>` node records -> {name: translation}: name, then 16 floats (4x4 row-major), translation = row 3."""
    out = {}
    for m in re.finditer(rb'Actor_\d+_\d+', head):
        n = m.group()
        if m.start() >= 4 and struct.unpack_from('<I', head, m.start() - 4)[0] == len(n) and m.end() + 64 <= len(head):
            f = struct.unpack_from('<16f', head, m.end())
            out.setdefault(n.decode(), np.array(f[12:15], np.float64))
    return out


def psi_actors(psi):
    """The `.PSI` lists one `Actor_*` name per mesh PART, in the P3D's part order (X56_Y36: 97 names for 97 parts); each actor appears 3 times = 3 LODs."""
    return [n.decode() for n in re.findall(rb'Actor_\d+_\d+', psi)]



def vpos_fields(blob, part):
    """vpos u64 -> (n,3) float64 signed fixed-point fields (x: bits 0-15, y: 16-29, z: 30-43, two's complement), see tdusc_pws_format.md 2p."""
    u = np.frombuffer(blob, '<u8', count=part['vc'], offset=part['st']['vpos']).astype(np.int64)

    def sg(v, b):
        return np.where((v >> (b - 1)) & 1, v - (1 << b), v)
    return np.stack([sg(u & 0xFFFF, 16), sg((u >> 16) & 0x3FFF, 14), sg((u >> 30) & 0x3FFF, 14)], 1).astype(np.float64)


VPOS_UNIT = np.array([2.0 ** -14, 2.0 ** -12, 2.0 ** -12])


def mesh_exponent(blob, mesh):
    """Per-mesh power-of-two scale exponent k: position = bbox centre + field * VPOS_UNIT * 2**k.  [DATA] 115 of 117 tight meshes (parts whose data fills the
    bbox) show an integer k with k = ceil(log2(H+1)) - 1, H = largest bbox half extent of the file.  The exponent is not stored anywhere found yet, so it is
    estimated as the largest k that keeps the union of the mesh's parts inside its bbox (the encoder fills the bbox to within one octave)."""
    mn = np.array(mesh['bbox'][:3]); mx = np.array(mesh['bbox'][3:6]); h = (mx - mn) / 2
    big = np.zeros(3)
    for p in mesh['parts']:
        if 'vpos' in p['st'] and p['vc']:
            big = np.maximum(big, np.abs(vpos_fields(blob, p)).max(0) * VPOS_UNIT)
    ok = big > 0
    if not ok.any():
        return 0
    r = np.log2(h[ok] / big[ok] + 1e-12)
    return int(max(0, np.floor(r.min() + 0.06)))


def decode(d, origin=(0.0, 0.0), psi=None, lod0=True, attrs=None):
    """-> (V (n,3) float64 in WORLD metres (origin = tile SW corner added), F (m,3) int) for every mesh of the P3D.
    psi = the matching `.PSI` bytes (gives each part's actor, hence its translation); lod0 keeps only the largest part of each actor."""
    bo = struct.unpack_from('<I', d, 0x34)[0]
    head, blob = d[:bo], gdeflate.decompress(d[bo:])
    meshes = parse_named_meshes(head)
    trs = actor_translations(head)
    names = psi_actors(psi) if psi else []
    flat = [(mi, pi) for mi, m in enumerate(meshes) for pi in range(len(m['parts']))]
    if names and len(names) != len(flat):
        names = []
    keep = None
    if lod0 and names:
        best = {}
        for k, (mi, pi) in enumerate(flat):
            v = meshes[mi]['parts'][pi]['vc']
            if names[k] not in best or v > best[names[k]][0]:
                best[names[k]] = (v, k)
        keep = {v[1] for v in best.values()}
    Vs, Fs, base = [], [], 0
    if attrs is not None:
        attrs.clear(); attrs['col'] = []; attrs['uv'] = []
    for k, (mi, pi) in enumerate(flat):
        if keep is not None and k not in keep:
            continue
        mesh = meshes[mi]; p = mesh['parts'][pi]
        mn = np.array(mesh['bbox'][:3]); mx = np.array(mesh['bbox'][3:6])
        vc = p['vc']
        if 'vpos' not in p['st'] or vc == 0:
            continue
        loc = (mn + mx) / 2 + vpos_fields(blob, p) * VPOS_UNIT * 2.0 ** mesh_exponent(blob, mesh)
        idx = np.frombuffer(blob, 'u1', count=p['ntri'] * 3, offset=p['ib']).reshape(-1, 3).astype(np.int64)
        vb = 0
        tris = []
        for a, b, n in split_meshlets(idx):
            tris.append(idx[a:b] + vb)
            vb += n
        F = np.vstack(tris)
        if vb != vc or F.max() >= vc:
            continue
        if attrs is not None:
            attrs['col'].append(np.frombuffer(blob, 'u1', count=vc * 4, offset=p['st']['vcol']).reshape(-1, 4) if 'vcol' in p['st'] else np.full((vc, 4), 255, np.uint8))
            attrs['uv'].append(np.frombuffer(blob, '<u2', count=vc * 2, offset=p['st']['vuvs']).reshape(-1, 2) / 65535.0 if 'vuvs' in p['st'] else np.zeros((vc, 2)))
        tr = trs.get(names[k], np.zeros(3)) if names else (next(iter(trs.values())) if trs else np.zeros(3))
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
                        if (BS + a.kind + BS) in k.upper() and BS + 'LEVELS' + BS + 'HONGKONG' in k.upper() and re.search(r'TILE_X(\d+)_Y(\d+)', k)})
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
        V, F = decode(t.read(p), (ox, oy), t.read(p[:-4] + '.PSI'))
        print(p.split(BS)[-1], len(V), 'verts', len(F), 'tris')
        if len(V):
            allv.append(V); allf.append(F + base); base += len(V)
    if a.out and allv:
        os.makedirs(a.out, exist_ok=True)
        np.savez(os.path.join(a.out, 'TILE_X%d_Y%d_%s.npz' % (i, j, a.kind)), V=np.vstack(allv), F=np.vstack(allf))


if __name__ == '__main__':
    main()
