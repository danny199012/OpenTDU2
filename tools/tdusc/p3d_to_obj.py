#!/usr/bin/env python3
"""Convert a TDUSC `.P3D` model (docs/re/tdusc_pkg_format.md section 7) to a Wavefront OBJ in a folder you name.

    python p3d_to_obj.py --pkg-dir "<TDUSC>/WIN32/PKG" --toc-dir C:/temp/tdusc-scan --out C:/temp/tdusc-export \
        "WIN32\\GRAPH\\CARS\\LAMBORGHINI\\HURACAN\\MESH\\HURACAN_LP640_4_PERFORMANTE_HR.P3D"

Reads the install read-only, decodes the GDeflate vertex / index blob with gdeflate.py, and writes positions,
normals and triangles of every mesh part (`o mesh<i>_part<j>`), in the file's own space (Z up, Y along the car,
metres). The scene-graph node transforms are applied (parse_nodes): doors, mirrors, windows, bumpers, wipers sit in place.
`--local` keeps the old node-local output. UVs / tangents / materials are not written.
The OBJ goes where you point `--out`; nothing is written into the repo or the game folder.
"""
import argparse
import os
import re
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gdeflate  # noqa: E402
from tdusc_read import Tdusc  # noqa: E402


def parse_meshes(head):
    """The plain section's MESH entries: bounding box, then per part the vertex format, counts and stream offsets."""
    meshes = []
    for m in re.finditer(b'MESH', head):
        p = m.end()
        if p + 44 > len(head):
            continue
        bbox = struct.unpack_from('<10f', head, p)
        nparts = struct.unpack_from('<I', head, p + 40)[0]
        if not (0 < nparts < 64) or not all(abs(x) < 1e4 for x in bbox):
            continue
        q = p + 44
        parts = []
        ok = True
        for _ in range(nparts):
            a, nw = struct.unpack_from('<II', head, q)
            if nw > 12 or nw == 0:
                ok = False
                break
            fmt = head[q + 8:q + 8 + nw * 4].rstrip(b'\0').decode('latin1')
            q += 8 + nw * 4
            _z0, _z1, vcount, nstreams = struct.unpack_from('<IIII', head, q)
            q += 16
            streams = []
            for _s in range(nstreams):
                tag = head[q:q + 4].decode('latin1')
                streams.append((tag, struct.unpack_from('<I', head, q + 4)[0]))
                q += 8
            ntri, iboff = struct.unpack_from('<II', head, q)
            q += 12
            parts.append(dict(fmt=fmt, vcount=vcount, streams=dict(streams), ntri=ntri, iboff=iboff))
        if ok:
            meshes.append(dict(bbox=bbox, parts=parts))
    return meshes


def parse_nodes(head):
    """The scene graph after the mesh entries (`DEFAULT` + node count). [DATA]

    Two tables, `nDummy` dummies (`D_CAR`, `D_Door_L`, `Fork_FL` ... no mesh) then `nMesh` mesh nodes. Every record is
    `u32 id, u32 nChildren, u32 nDescendants` (ids are the pre-order index of the whole tree, which is how the two tables are
    joined: the parent of id k is the deepest node whose id range (id, id + nDescendants] holds k), `u32 nameLen, name,
    16 x f32` (row-major 4x4, row vectors, translation in row 3, local -> parent). A mesh-node record then carries 21 more
    bytes: `00 00 00 04 7f*8 ff ff 01 00 u32 meshIndex 00`; `meshIndex` counts the `MESH` entries in file order.
    Returns a list of dicts: id, name, matrix (4x4, local), parent (id or None), mesh (index or None), world (4x4)."""
    base = head.find(b'DEFAULT')
    if base < 0:
        return []
    root = head.find(b'\x04\x00\x00\x00Root', base)
    if root < 0:
        return []
    p = root - 12
    ndummy = struct.unpack_from('<I', head, p - 4)[0]
    nodes = []

    def rec(p, with_mesh):
        nid, nch, ndesc, ln = struct.unpack_from('<4I', head, p)
        name = head[p + 16:p + 16 + ln].decode('latin1')
        q = p + 16 + ln
        m = np.array(struct.unpack_from('<16f', head, q), dtype=np.float64).reshape(4, 4)
        q += 64
        mesh = None
        if with_mesh:
            mesh = struct.unpack_from('<I', head, q + 16)[0]
            q += 21
        nodes.append(dict(id=nid, nchild=nch, ndesc=ndesc, name=name, matrix=m, mesh=mesh))
        return q

    for _ in range(ndummy):
        p = rec(p, False)
    nmesh = struct.unpack_from('<I', head, p)[0]
    p += 4
    for _ in range(nmesh):
        p = rec(p, True)
    nodes.sort(key=lambda n: n['id'])
    stack = []
    byid = {n['id']: n for n in nodes}
    for n in nodes:
        while stack and stack[-1]['id'] + stack[-1]['ndesc'] < n['id']:
            stack.pop()
        n['parent'] = stack[-1]['id'] if stack else None
        n['world'] = n['matrix'] @ (byid[n['parent']]['world'] if stack else np.eye(4))
        stack.append(n)
    return nodes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pkg-dir', required=True)
    ap.add_argument('--toc-dir', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--local', action='store_true', help='keep every mesh in its node-local space (old behaviour)')
    ap.add_argument('path')
    a = ap.parse_args()
    t = Tdusc(a.pkg_dir, a.toc_dir)
    d = t.read(a.path)
    blob_off = struct.unpack_from('<I', d, 0x34)[0]
    head, blob = d[:blob_off], gdeflate.decompress(d[blob_off:])
    meshes = parse_meshes(head)
    nodes = [] if a.local else parse_nodes(head)
    # one instance per node that carries a mesh; meshes no node uses stay in their own local space (identity)
    inst = [(n['name'], n['mesh'], n['world']) for n in nodes if n['mesh'] is not None and n['mesh'] < len(meshes)]
    used = {i[1] for i in inst}
    inst += [('mesh%d' % mi, mi, np.eye(4)) for mi in range(len(meshes)) if mi not in used]
    lines = []
    base = 1
    tris = 0
    for name, mi, w in inst:
        r = w[:3, :3]
        flip = np.linalg.det(r) < 0
        rn = np.linalg.inv(r).T
        for pi, p in enumerate(meshes[mi]['parts']):
            n, st = p['vcount'], p['streams']
            pos = np.frombuffer(blob, dtype='<f4', count=n * 3, offset=st['vpos']).reshape(-1, 3).astype(np.float64)
            nor = np.frombuffer(blob, dtype='<f2', count=n * 4, offset=st['vnor']).reshape(-1, 4)[:, :3].astype(np.float64)
            idx = np.frombuffer(blob, dtype='<u2', count=p['ntri'] * 3, offset=p['iboff']).reshape(-1, 3)
            pos = pos @ r + w[3, :3]
            nor = nor @ rn
            nor /= np.maximum(np.linalg.norm(nor, axis=1, keepdims=True), 1e-9)
            if flip:
                idx = idx[:, ::-1]
            lines.append('o %s_part%d' % (name, pi))
            lines += ['v %.5f %.5f %.5f' % tuple(v) for v in pos]
            lines += ['vn %.4f %.4f %.4f' % tuple(v) for v in nor]
            lines += ['f %d//%d %d//%d %d//%d' % (x + base, x + base, y + base, y + base, z + base, z + base)
                      for x, y, z in idx]
            base += n
            tris += len(idx)
    os.makedirs(a.out, exist_ok=True)
    dst = os.path.join(a.out, os.path.splitext(os.path.basename(a.path.replace('\\', '/')))[0] + '.obj')
    open(dst, 'w').write('\n'.join(lines) + '\n')
    print('wrote %s (%d parts, %d triangles)' % (dst, sum(len(meshes[i[1]]['parts']) for i in inst), tris))


if __name__ == '__main__':
    main()
