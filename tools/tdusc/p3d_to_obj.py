#!/usr/bin/env python3
"""Convert a TDUSC `.P3D` model (docs/re/tdusc_pkg_format.md section 7) to a Wavefront OBJ in a folder you name.

    python p3d_to_obj.py --pkg-dir "<TDUSC>/WIN32/PKG" --toc-dir C:/temp/tdusc-scan --out C:/temp/tdusc-export \
        "WIN32\\GRAPH\\CARS\\LAMBORGHINI\\HURACAN\\MESH\\HURACAN_LP640_4_PERFORMANTE_HR.P3D"

Reads the install read-only, decodes the GDeflate vertex / index blob with gdeflate.py, and writes positions,
normals and triangles of every mesh part (`o mesh<i>_part<j>`), in the file's own space (Z up, Y along the car,
metres). The parts are in their NODE-LOCAL space: the scene graph transforms are not applied yet (doors, wheels
and suspension pieces therefore sit at the origin of their own node). UVs / tangents / materials are not written.
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pkg-dir', required=True)
    ap.add_argument('--toc-dir', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('path')
    a = ap.parse_args()
    t = Tdusc(a.pkg_dir, a.toc_dir)
    d = t.read(a.path)
    blob_off = struct.unpack_from('<I', d, 0x34)[0]
    head, blob = d[:blob_off], gdeflate.decompress(d[blob_off:])
    lines = []
    base = 1
    tris = 0
    for mi, m in enumerate(parse_meshes(head)):
        for pi, p in enumerate(m['parts']):
            n, st = p['vcount'], p['streams']
            pos = np.frombuffer(blob, dtype='<f4', count=n * 3, offset=st['vpos']).reshape(-1, 3)
            nor = np.frombuffer(blob, dtype='<f2', count=n * 4, offset=st['vnor']).reshape(-1, 4)[:, :3]
            idx = np.frombuffer(blob, dtype='<u2', count=p['ntri'] * 3, offset=p['iboff']).reshape(-1, 3)
            lines.append('o mesh%d_part%d' % (mi, pi))
            lines += ['v %.5f %.5f %.5f' % tuple(v) for v in pos]
            lines += ['vn %.4f %.4f %.4f' % tuple(v) for v in nor.astype(np.float32)]
            lines += ['f %d//%d %d//%d %d//%d' % (x + base, x + base, y + base, y + base, z + base, z + base)
                      for x, y, z in idx]
            base += n
            tris += len(idx)
    os.makedirs(a.out, exist_ok=True)
    dst = os.path.join(a.out, os.path.splitext(os.path.basename(a.path.replace('\\', '/')))[0] + '.obj')
    open(dst, 'w').write('\n'.join(lines) + '\n')
    print('wrote %s (%d parts, %d triangles)' % (dst, sum(len(m['parts']) for m in parse_meshes(head)), tris))


if __name__ == '__main__':
    main()
