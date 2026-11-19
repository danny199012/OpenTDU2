#!/usr/bin/env python3
"""Assemble a TDUSC car (body OBJ + four wheels) into one OBJ, in a folder you name (docs/re/tdusc_pkg_format.md section 7.6).

    python car_assemble.py --pkg-dir "<TDUSC>/WIN32/PKG" --toc-dir C:/temp/tdusc-scan --out C:/temp/tdusc-car \
        --car "WIN32\\GRAPH\\CARS\\LAMBORGHINI\\HURACAN" --stem HURACAN_LP640_4_PERFORMANTE --wheel 01

Runs p3d_to_obj.py for `MESH\\<stem>_HR.P3D` and for the front / rear wheel meshes (`<brand model>_W_F_nn`, `_W_R_nn`), reads the wheel anchors
(`Fork_FL`, `Fork_RL`: x = half track, y = axle position) from the LR file, and writes `<stem>_assembled.obj` / `.mtl`. A wheel mesh is the
RIGHT wheel (disc on the +X side, axle = X, centred): the left wheels are mirrored in X (winding flipped). The wheel meshes carry no tyre, so a
simple lathe tyre (black) is added: radius / width are `[INF]` (245/30R20 front, 305/30R20 rear, hub height = tyre radius).
Also writes `<stem>_wheels.json` (anchors, tyre size) for a car-swap pack.
"""
import argparse
import json
import math
import os
import re
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import p3d_to_obj  # noqa: E402
from tdusc_read import Tdusc  # noqa: E402

BS = chr(92)
TYRES = {'F': (0.245, 0.30), 'R': (0.305, 0.30)}      # width m, aspect  (rim radius is taken from the wheel mesh)


def run_obj(a, p3d, out):
    subprocess.check_call([sys.executable, os.path.join(HERE, 'p3d_to_obj.py'), '--pkg-dir', a.pkg_dir, '--toc-dir', a.toc_dir,
                           '--out', out, p3d], stdout=subprocess.DEVNULL)
    return os.path.join(out, os.path.splitext(p3d.split(BS)[-1])[0])


def read_obj(path):
    v, vn, vt, body = [], [], [], []
    for ln in open(path + '.obj'):
        s = ln.split()
        if not s:
            continue
        if s[0] == 'v':
            v.append([float(x) for x in s[1:4]])
        elif s[0] == 'vn':
            vn.append([float(x) for x in s[1:4]])
        elif s[0] == 'vt':
            vt.append([float(x) for x in s[1:3]])
        elif s[0] in ('f', 'o', 'usemtl'):
            body.append(s)
    return np.array(v), np.array(vn), np.array(vt), body


def tyre(width, aspect, rim_r, seg=48):
    """Lathe tyre around the X axis: bead at the rim radius, sidewalls, flat tread. -> v, vn, vt, faces (0-based)."""
    h = width * aspect
    R = rim_r + h * 1.0
    prof = [(-width * 0.5, rim_r + 0.01), (-width * 0.5, rim_r + h * 0.35), (-width * 0.46, R - h * 0.12), (-width * 0.36, R),
            (width * 0.36, R), (width * 0.46, R - h * 0.12), (width * 0.5, rim_r + h * 0.35), (width * 0.5, rim_r + 0.01)]
    v, vn, vt, f = [], [], [], []
    for i, (x, r) in enumerate(prof):
        nx, nr = 0.0, 1.0
        if i == 0 or i == 1:
            nx, nr = -1.0, 0.2
        elif i >= len(prof) - 2:
            nx, nr = 1.0, 0.2
        for k in range(seg + 1):
            a = 2 * math.pi * k / seg
            v.append([x, r * math.cos(a), r * math.sin(a)])
            vn.append([nx, nr * math.cos(a), nr * math.sin(a)])
            vt.append([k / seg, i / (len(prof) - 1)])
    for i in range(len(prof) - 1):
        for k in range(seg):
            a, b = i * (seg + 1) + k, (i + 1) * (seg + 1) + k
            f.append((a, b, b + 1))
            f.append((a, b + 1, a + 1))
    return np.array(v), np.array(vn), np.array(vt), f, R


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pkg-dir', required=True)
    ap.add_argument('--toc-dir', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--car', required=True, help='game folder of the car, e.g. WIN32\\GRAPH\\CARS\\LAMBORGHINI\\HURACAN')
    ap.add_argument('--stem', required=True, help='e.g. HURACAN_LP640_4_PERFORMANTE')
    ap.add_argument('--wheel', default='01', help='wheel design 01..06')
    a = ap.parse_args()
    t = Tdusc(a.pkg_dir, a.toc_dir)
    mesh = a.car + BS + 'MESH' + BS
    lr = t.read(mesh + a.stem + '_LR.P3D')
    import struct
    anchors = {n['name']: n['world'][3, :3] for n in p3d_to_obj.parse_nodes(lr[:struct.unpack_from('<I', lr, 0x34)[0]])}
    fl, rl = anchors['Fork_FL'], anchors['Fork_RL']
    wstem = re.sub(r'_\d+_', '_', a.stem, count=1) if False else a.stem.replace('LP640_4_', 'LP640_')
    body = run_obj(a, mesh + a.stem + '_HR.P3D', a.out)
    parts_out = [read_obj(body)]
    info = {'anchors': {k: [round(float(x), 4) for x in v] for k, v in (('FL', fl), ('RL', rl))}, 'tyre': {}}
    verts = [parts_out[0]]
    mtl_files = [body + '.mtl']
    placements = []
    for axle, anc in (('F', fl), ('R', rl)):
        wp = run_obj(a, mesh + '%s_W_%s_%s.P3D' % (wstem, axle, a.wheel), a.out)
        mtl_files.append(wp + '.mtl')
        v, vn, vt, bd = read_obj(wp)
        rim_r = float(np.max(np.hypot(v[:, 1], v[:, 2])))
        width, aspect = TYRES[axle]
        tv, tn, tt, tf, R = tyre(width, aspect, rim_r)
        info['tyre'][axle] = {'width': width, 'radius': round(R, 4), 'rim_radius': round(rim_r, 4), 'hub_z': round(R, 4)}
        # wheel mesh = right wheel: disc at +X. right: x = -half track, left: mirrored
        for side, sx in (('R', -1.0), ('L', 1.0)):
            c = np.array([sx * abs(anc[0]), anc[1], R])
            mir = sx > 0
            wv, wn = v.copy(), vn.copy()
            tv2, tn2 = tv.copy(), tn.copy()
            if mir:
                wv[:, 0] *= -1
                wn[:, 0] *= -1
                tv2[:, 0] *= -1
                tn2[:, 0] *= -1
            placements.append((axle + side, wv + c, wn, vt, bd, mir, tv2 + c, tn2, tt, tf))
            info.setdefault('hubs', {})[axle + side] = [round(float(x), 4) for x in c]
    # write the assembled OBJ
    out = os.path.join(a.out, a.stem + '_assembled')
    lines = ['mtllib %s_assembled.mtl' % a.stem]
    bv, bn, bt, bb = parts_out[0]
    for x in bv:
        lines.append('v %.5f %.5f %.5f' % tuple(x))
    for x in bn:
        lines.append('vn %.4f %.4f %.4f' % tuple(x))
    for x in bt:
        lines.append('vt %.5f %.5f' % tuple(x))
    for s in bb:
        lines.append(' '.join(s))
    ov, ot = len(bv), len(bt)
    for name, wv, wn, wt, bd, mir, tv, tn, tt, tf in placements:
        lines.append('o wheel_' + name)
        for x in wv:
            lines.append('v %.5f %.5f %.5f' % tuple(x))
        for x in wn:
            lines.append('vn %.4f %.4f %.4f' % tuple(x))
        for x in wt:
            lines.append('vt %.5f %.5f' % tuple(x))
        for s in bd:
            if s[0] == 'f':
                ids = [[int(q) for q in tok.split('/')] for tok in s[1:4]]
                if mir:
                    ids = ids[::-1]
                lines.append('f ' + ' '.join('%d/%d/%d' % (i[0] + ov, i[1] + ot, i[2] + ov) for i in ids))
            else:
                lines.append(' '.join(s))
        ov += len(wv)
        ot += len(wt)
        lines.append('o tyre_' + name)
        lines.append('usemtl TYRE')
        for x in tv:
            lines.append('v %.5f %.5f %.5f' % tuple(x))
        for x in tn:
            lines.append('vn %.4f %.4f %.4f' % tuple(x))
        for x in tt:
            lines.append('vt %.5f %.5f' % tuple(x))
        for tri in tf:
            ids = tri[::-1] if mir else tri
            lines.append('f ' + ' '.join('%d/%d/%d' % (i + 1 + ov, i + 1 + ot, i + 1 + ov) for i in ids))
        ov += len(tv)
        ot += len(tt)
    open(out + '.obj', 'w').write('\n'.join(lines) + '\n')
    seen, mt = set(), []
    for f in mtl_files:
        for blk in open(f).read().split('newmtl ')[1:]:
            nm = blk.split()[0]
            if nm not in seen:
                seen.add(nm)
                mt.append('newmtl ' + blk.rstrip() + '\n')
    mt.append('newmtl TYRE\nKd 0.02 0.02 0.02\nPm 0.0\nPr 0.8\n')
    open(out + '.mtl', 'w').write('\n'.join(mt))
    json.dump(info, open(os.path.join(a.out, a.stem + '_wheels.json'), 'w'), indent=1)
    print('wrote', out + '.obj', json.dumps(info['hubs']))


if __name__ == '__main__':
    main()
