#!/usr/bin/env python3
"""Build a car-swap pack (source/game/freeride/car_swap.h) from a decoded TDUSC car (docs/re/tdusc_pkg_format.md section 7.7).

    python car_assemble.py ... --out C:/temp/tdusc-car --car ... --stem HURACAN_LP640_4_PERFORMANTE     (writes the parts)
    python swap_pack.py --src C:/temp/tdusc-car --stem HURACAN_LP640_4_PERFORMANTE --out C:/temp/otdu2-tdusc-mods2/carswap [--donor 458_italia]

Writes `<out>/tdusc_body.obj` (everything that is not glass), `tdusc_glass.obj`, `tdusc_rimF/R.obj` + `tdusc_tireF/R.obj` (centred on the
wheel, built for the engine-negative-x side = the car's LEFT wheel, outer face +X in file space) and `carswap.txt`; point the game at the
folder's parent with `-mods`. The OBJ axes follow blender_car_split.py: file Y up, +Z forward, the loader mirrors X; TDUSC space
(Z up, front = -Y, left = +X) maps by (x, y, z) -> (x, z, -y), a proper rotation (winding kept). Every material gets an opaque PNG next to the OBJ
(the port's no-texture shader ignores Kd): the converted PMA diffuse texture (<= 1024 px) or a solid Kd colour; glass is a translucent colour
(`d 0.35`). Reads only files `p3d_to_obj.py` / `car_assemble.py` wrote; nothing from the install.
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import car_assemble  # noqa: E402


def read_mtl(path):
    out, cur = {}, None
    for ln in open(path):
        s = ln.split()
        if not s:
            continue
        if s[0] == 'newmtl':
            cur = out[s[1]] = {'kd': (0.8, 0.8, 0.8), 'tex': None, 'd': 1.0}
        elif cur is not None and s[0] == 'Kd':
            cur['kd'] = tuple(float(x) for x in s[1:4])
        elif cur is not None and s[0] == 'map_Kd':
            cur['tex'] = s[1]
        elif cur is not None and s[0] == 'd':
            cur['d'] = float(s[1])
    return out


def read_obj(path):
    """-> v, vn, vt (arrays), faces [(material, (a, b, c) 0-based indices (v == vt == vn))]."""
    v, vn, vt, faces, mat = [], [], [], [], 'default'
    for ln in open(path):
        s = ln.split()
        if not s:
            continue
        if s[0] == 'v':
            v.append([float(x) for x in s[1:4]])
        elif s[0] == 'vn':
            vn.append([float(x) for x in s[1:4]])
        elif s[0] == 'vt':
            vt.append([float(x) for x in s[1:3]])
        elif s[0] == 'usemtl':
            mat = s[1]
        elif s[0] == 'f':
            faces.append((mat, tuple(int(t.split('/')[0]) - 1 for t in s[1:4])))
    return np.array(v), np.array(vn), np.array(vt), faces


def to_file_space(a):
    return np.stack([a[:, 0], a[:, 2], -a[:, 1]], axis=1)


def write_obj(path, name, v, vn, vt, faces, mtls, src_tex_dir, out_dir):
    """One OBJ + MTL; every material -> opaque PNG (or solid colour / translucent for glass)."""
    used = sorted({m for m, _ in faces})
    lines = ['mtllib %s.mtl' % name]
    mtl = []
    for m in used:
        e = mtls.get(m, {'kd': (0.5, 0.5, 0.5), 'tex': None, 'd': 1.0})
        png = 'tdusc_%s.png' % m
        glass = e['d'] < 1.0
        if e['tex'] and not glass and os.path.exists(os.path.join(src_tex_dir, e['tex'])):
            im = Image.open(os.path.join(src_tex_dir, e['tex'])).convert('RGB')
            if max(im.size) > 1024:
                im = im.resize((1024, 1024 * im.size[1] // im.size[0]))
            im.save(os.path.join(out_dir, png))
        else:
            c = tuple(int(round(x * 255)) for x in e['kd']) + (95 if glass else 255,)
            Image.new('RGBA', (8, 8), c).save(os.path.join(out_dir, png))
        mtl += ['newmtl ' + m, 'Ka 1.0 1.0 1.0', 'Kd 1.0 1.0 1.0', 'd %.2f' % (0.35 if glass else 1.0), 'map_Kd ' + png, '']
    lines += ['v %.5f %.5f %.5f' % tuple(x) for x in v]
    lines += ['vn %.4f %.4f %.4f' % tuple(x) for x in vn]
    lines += ['vt %.5f %.5f' % tuple(x) for x in vt]
    cur = None
    lines.append('o ' + name)
    for m, (a, b, c) in faces:
        if m != cur:
            lines.append('usemtl ' + m)
            cur = m
        lines.append('f %d/%d/%d %d/%d/%d %d/%d/%d' % (a + 1, a + 1, a + 1, b + 1, b + 1, b + 1, c + 1, c + 1, c + 1))
    open(os.path.join(out_dir, name + '.obj'), 'w').write('\n'.join(lines) + '\n')
    open(os.path.join(out_dir, name + '.mtl'), 'w').write('\n'.join(mtl))
    return len(faces)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', required=True, help='car_assemble.py output folder')
    ap.add_argument('--stem', required=True)
    ap.add_argument('--out', required=True, help='the carswap folder (inside the mods overlay)')
    ap.add_argument('--donor', default='458_italia')
    ap.add_argument('--wheel', default='01')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    info = json.load(open(os.path.join(a.src, a.stem + '_wheels.json')))
    # --- body / glass
    v, vn, vt, faces = read_obj(os.path.join(a.src, a.stem + '_HR.obj'))
    mt = read_mtl(os.path.join(a.src, a.stem + '_HR.mtl'))
    V, N = to_file_space(v), to_file_space(vn)
    isglass = lambda m: m.startswith('GLASS') or m == 'WINDSCREEN'  # noqa: E731
    nb = write_obj('', 'tdusc_body', V, N, vt, [f for f in faces if not isglass(f[0])], mt, a.src, a.out)
    ng = write_obj('', 'tdusc_glass', V, N, vt, [f for f in faces if isglass(f[0])], mt, a.src, a.out)
    print('body %d triangles, glass %d' % (nb, ng))
    # --- wheels (left wheel = engine-negative-x side)
    wstem = a.stem.replace('LP640_4_', 'LP640_')
    pos = {}
    for axle in 'FR':
        wv, wn, wt, wf = read_obj(os.path.join(a.src, '%s_W_%s_%s.obj' % (wstem, axle, a.wheel)))
        wm = read_mtl(os.path.join(a.src, '%s_W_%s_%s.mtl' % (wstem, axle, a.wheel)))
        wvn = np.array([[float(x) for x in ln.split()[1:4]] for ln in open(os.path.join(a.src, '%s_W_%s_%s.obj' % (wstem, axle, a.wheel)))
                        if ln.startswith('vn ')])
        wv = wv * [-1, 1, 1]            # the mesh is the right wheel: mirror to the left one
        wvn = wvn * [-1, 1, 1]
        wf = [(m, (c, b, x)) for m, (x, b, c) in wf]
        write_obj('', 'tdusc_rim' + axle, to_file_space(wv), to_file_space(wvn), wt, wf, wm, a.src, a.out)
        t = info['tyre'][axle]
        tv, tn, tt, tf, _R = car_assemble.tyre(t['width'], t['radius'] and (t['radius'] - t['rim_radius']) / t['width'], t['rim_radius'])
        write_obj('', 'tdusc_tire' + axle, to_file_space(tv), to_file_space(tn), tt, [('TYRE', tri) for tri in tf],
                  {'TYRE': {'kd': (0.03, 0.03, 0.03), 'tex': None, 'd': 1.0}}, a.src, a.out)
        for side, k in (('neg', 'F' + 'L' if axle == 'F' else 'RL'), ('pos', 'FR' if axle == 'F' else 'RR')):
            h = info['hubs'][k]
            pos['%s_%s' % (axle.lower(), side)] = (-h[0], h[2], -h[1])   # engine space: x mirrored, file y = TDUSC z, file z = -TDUSC y
    lines = ['[swap:%s]' % a.donor, 'body = tdusc_body', 'body = tdusc_glass', 'wheel_front = tdusc_rimF tdusc_tireF',
             'wheel_rear = tdusc_rimR tdusc_tireR']
    for k, p in sorted(pos.items()):
        lines.append('wheel_pos_%s = %.4f %.4f %.4f' % ((k,) + tuple(p)))
    open(os.path.join(a.out, 'carswap.txt'), 'w').write('\n'.join(lines) + '\n')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
