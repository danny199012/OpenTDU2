#!/usr/bin/env python3
"""TDUSC `PSI` (scene instance: node name -> material per part) and `PMA` (material: shader + parameter -> texture slots), as a
Wavefront `.mtl` for tools/tdusc/p3d_to_obj.py (docs/re/tdusc_pkg_format.md section 7.4).

PSI: after the `DATA:/.../MESH/<x>.P3D` string comes a flat list of (node name, `DATA:/.../PMA/<set>/<name>.PMA`) pairs. The k-th pair
with the same node name is **part k of that node's mesh** (HR `Body` has 14 pairs for 14 parts, `Door_L` 6 for 6).
PMA: RIFF `PA__` (LZ4 frame): the shader (`DATA:/EFFECTS/VEHICLECARPAINT.PFX`), `Use ...` flags, then parameter name strings, each
texture parameter followed by its `DATA:/.../*.PTX` path (`DiffuseMap`, `NormalMap`, `StructureMap` = the `_IOR` packed sheet,
`OcclusionMap`, `DirtMask`, `ScratchMap` ...). Colours / floats (ExternalColor, Gloss1 ...) are binary and not decoded yet.
"""
import os
import re

import ptx_to_dds

BS = chr(92)


def _strings(d):
    return [m.group().decode('latin1') for m in re.finditer(rb'[ -~]{3,}', d)]


def _game_path(s):
    """`DATA:/GRAPH/X` -> `WIN32\\GRAPH\\X`."""
    return 'WIN32' + BS + s[len('DATA:/'):].replace('/', BS)


def _find(t, ci, s):
    return ci.get(_game_path(s).upper())


def parse_psi(d):
    """-> {node name: [pma path, ...]} in part order."""
    s = _strings(d)
    out = {}
    for i, x in enumerate(s):
        if x.startswith('DATA:/') and x.upper().endswith('.PMA') and i > 0:
            out.setdefault(s[i - 1], []).append(x)
    return out


def parse_pma(d):
    """-> {texture parameter: DATA path}."""
    s = _strings(d)
    return {s[i - 1]: x for i, x in enumerate(s) if x.startswith('DATA:/') and x.upper().endswith('.PTX') and i > 0}


def build(t, p3d_path, outdir):
    """Reads the PSI next to the P3D (`MESH\\X.P3D` -> `X.PSI`), converts the textures it needs, writes `<outdir>/<x>.mtl`.
    Returns ({node: [material name per part]}, mtl file name) or ({}, None) when there is no PSI."""
    ci = {k.upper(): k for k in t.entries}
    parts = p3d_path.split(BS)
    psi_path = ci.get((BS.join(parts[:-2] + [os.path.splitext(parts[-1])[0] + '.PSI'])).upper())
    if not psi_path:
        return {}, None
    psi = parse_psi(t.read(psi_path))
    texdir = os.path.join(outdir, 'tex')
    mats = {}
    done = {}
    for node, pmas in psi.items():
        for pma in pmas:
            name = os.path.splitext(pma.rsplit('/', 1)[-1])[0]
            if pma in done:
                continue
            slots = {}
            key = _find(t, ci, pma)
            if key:
                slots = parse_pma(t.read(key))
            ent = {'name': name, 'kd': None, 'bump': None}
            for slot, dsts in (('kd', ('DiffuseMap', 'DiffuseMapTile')), ('bump', ('NormalMap', 'NormalMapTile'))):
                dst = next((x for x in dsts if x in slots and _find(t, ci, slots[x])), None)
                if dst:
                    try:
                        ptx = _find(t, ci, slots[dst])
                        ptx_to_dds.convert(t, ptx, texdir, None, True)
                        ent[slot] = 'tex/' + os.path.splitext(ptx.split(BS)[-1])[0] + '.png'
                    except Exception as e:  # noqa: BLE001
                        print('texture failed', slots[dst], e)
            done[pma] = ent
            mats[name] = ent
    lines = []
    for name, e in sorted(mats.items()):
        lines.append('newmtl ' + name)
        lines.append('Kd 0.8 0.8 0.8')
        if e['kd']:
            lines.append('map_Kd ' + e['kd'])
        if e['bump']:
            lines.append('map_Bump ' + e['bump'])
        lines.append('')
    stem = os.path.splitext(parts[-1])[0]
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, stem + '.mtl'), 'w') as f:
        f.write('\n'.join(lines))
    per_node = {n: [os.path.splitext(p.rsplit('/', 1)[-1])[0] for p in ps] for n, ps in psi.items()}
    return per_node, stem + '.mtl'
