#!/usr/bin/env python3
"""Extract props from a TDUSC scene capture (.glb) into the port's OBJ mod route.

Why this exists: TDUSC (Unreal Engine) assets can reach OpenTDU2 the same way the
TDU2 files do - the **user supplies them from their own copy** and nothing
extracted ships with the project. A scene rip exported as glTF (Ninja Ripper
draw-call capture, FModel export, ...) is parsed here and re-emitted as
`props/<name>.obj` + `.mtl` + loose PNGs: exactly what the port's mod overlay
loads (`source/game/spots/obj_mesh.h`, placed via `props.txt`,
`source/game/spots/mod_props.h`). The tool only ever reads the user's own files
and writes into a mods folder it is pointed at.

The OBJ convention matches a Blender glTF -> OBJ round trip (which is the
identity on vertex coordinates: the importer's Y-up -> Z-up and the exporter's
Z-up -> Y-up cancel), so glTF vertices are written as-is and glTF `vt`
coordinates are flipped to `vt = (u, 1 - v)` like Blender does. Winding stays
CCW; the loader mirrors X and flips winding itself, like it does for the game's
own 3DG meshes.

Ninja Ripper rips capture the same draw twice (a material-less copy beside a
textured one); `--dedupe` collapses nodes with identical vertex counts, bounds
and centre, keeping the copy that has materials.

Selection (repeatable, all hits are pooled):
  --near x,y,z:radius     nodes whose centre is within radius metres
  --names mesh_27,...     nodes by glTF node name (Blender's object names)
  --min-size / --max-size bounding-box max-dimension filters, metres
Nodes are grouped into props by `--group radius` (single linkage); each prop
becomes one OBJ (one draw per material, like the loader expects).

Example (marina district of a Ibiza.glb capture):
  python tools/tdusc/glb_props.py --glb "E:/TDUSC Rips/Ibiza.glb" \
      --out C:/temp/otdu2-tdusc-mods --near "-32,75,3:2.5" --names mesh_27 \
      --group 4 --prop-prefix tdusc --write-props
"""
import argparse
import json
import math
import os
import struct
import sys

COMPONENT_FLOATS = {5120: 'h', 5121: 'B', 5122: 'H', 5123: 'I', 5126: 'f'}
COMPONENT_SIZE = {5120: 2, 5121: 1, 5122: 2, 5123: 4, 5126: 4}
TYPE_COUNT = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def load_glb(path):
    """Returns (gltf dict, raw file bytes, bin data offset candidates).

    Scene-capture GLBs are often written by tools with sloppy chunk layout
    (padded JSON chunks, a BIN header whose length field is garbage), so the
    BIN data start is *validated* later, per accessor, against candidate bases.
    """
    with open(path, 'rb') as f:
        raw = f.read()
    magic, version, length = struct.unpack('<III', raw[:12])
    if magic != 0x46546C67:
        sys.exit('not a GLB: %s' % path)
    js = None
    bases = []
    off = 12
    json_end = None
    while off + 8 <= len(raw):
        clen, ctype = struct.unpack('<II', raw[off:off + 8])
        if ctype == 0x4E4F534A:  # JSON
            js = json.loads(raw[off + 8:off + 8 + clen])
            json_end = off + 8 + ((clen + 3) // 4) * 4
        elif ctype == 0x004E4942:  # BIN
            bases.append(off + 8)
        if clen > len(raw):
            break
        off += 8 + ((clen + 3) // 4) * 4
    if js is None:
        sys.exit('no JSON chunk in %s' % path)
    if not bases and json_end is not None:
        # malformed writers: probe for the BIN magic near the JSON end
        for probe in range(json_end, min(json_end + 64, len(raw) - 4), 4):
            if struct.unpack_from('<I', raw, probe)[0] == 0x004E4942:
                bases.append(probe + 8)
                break
    if not bases:
        sys.exit('no BIN chunk in %s' % path)
    # standard layout first, then the "data right after the magic" variant
    ordered = []
    for b in bases:
        ordered += [b, b - 4]
    return js, raw, ordered


class BinAccess:
    """Resolves accessors against the (broken) layout of a capture GLB: each
    buffer view's true start is found by validating a sample of its data
    against the accessor's own metadata, trying a few base/delta candidates."""

    def __init__(self, js, raw, bases):
        self.js = js
        self.raw = raw
        self.bases = bases
        self.resolved = {}        # bufferView index -> delta from declared offset
        self.default_delta = None  # learned from accessors that have metadata

    def _validate_positions(self, off, acc, ncomp, stride):
        if 'min' not in acc or 'max' not in acc:
            return -1.0   # no metadata: can't judge, let another candidate win
        mn, mx = acc['min'], acc['max']
        sample = min(acc['count'], 99)
        ok = 0
        total = 0
        for i in range(sample):
            try:
                vals = struct.unpack_from('<%df' % ncomp, self.raw, off + i * stride)
            except struct.error:
                return 0.0
            for j in range(min(ncomp, 3)):
                total += 1
                if mn[j] - 1e-3 <= vals[j] <= mx[j] + 1e-3:
                    ok += 1
        return ok / float(max(total, 1))

    def _validate_indices(self, off, fmt, count, nv, stride):
        sample = min(count, 300)
        ok = 0
        try:
            vals = struct.unpack_from('<%d%s' % (sample, fmt), self.raw, off)
        except struct.error:
            return 0.0
        for v in vals:
            if v < nv:
                ok += 1
        return ok / float(sample)

    def view_offset(self, bv_index, acc, nv, bytes_per):
        """Absolute file offset of an accessor's data, repairing the layout."""
        if bv_index in self.resolved:
            return self.bases[0] + self.js['bufferViews'][bv_index].get('byteOffset', 0) \
                + acc.get('byteOffset', 0) + self.resolved[bv_index]
        bv = self.js['bufferViews'][bv_index]
        declared = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
        stride = bv.get('byteStride') or bytes_per * TYPE_COUNT[acc['type']]
        deltas = [self.default_delta] if self.default_delta is not None else []
        deltas += [d for d in (0, -4, 4, -8, 8) if d not in deltas]
        best = (self.bases[0] + declared, -1.0, 0)
        for base in self.bases:
            for delta in deltas:
                off = base + declared + delta
                if off < 0 or off + stride > len(self.raw):
                    continue
                if acc['type'] == 'SCALAR' and bytes_per in (1, 2, 4):
                    score = self._validate_indices(off, {1: 'B', 2: 'H', 4: 'I'}[bytes_per],
                                                   acc['count'], nv, stride)
                else:
                    score = self._validate_positions(off, acc, TYPE_COUNT[acc['type']], stride)
                if score > best[1]:
                    best = (off, score, delta)
        off, score, delta = best
        if score >= 0.999:
            if self.default_delta is None or delta != self.default_delta:
                self.default_delta = delta
                print('  [capture repair] buffer views need %+d bytes from the declared '
                      'layout - applying to the rest' % delta)
        elif score >= 0.0:
            print('  [capture repair] view %d: best offset validation %.2f (delta %+d) - '
                  'data may be damaged' % (bv_index, score, delta))
        self.resolved[bv_index] = delta
        return off

    def accessor(self, acc_idx, nv=None):
        """Returns a flat list of tuples for the accessor (positions, normals,
        uvs or indices). The index component type is derived from the buffer
        view size when it contradicts the declared componentType (a common
        capture-writer bug: u16 data declared as u32)."""
        acc = self.js['accessors'][acc_idx]
        ncomp = TYPE_COUNT[acc['type']]
        bv = self.js['bufferViews'][acc['bufferView']]
        comp = acc['componentType']
        bytes_per = COMPONENT_SIZE[comp]
        if acc['type'] == 'SCALAR' and acc.get('byteOffset') is None:
            per = bv['byteLength'] / float(acc['count'])
            if per in (1.0, 2.0, 4.0) and per != bytes_per:
                bytes_per = int(per)
        off = self.view_offset(acc['bufferView'], acc, nv if nv is not None else acc['count'],
                               bytes_per)
        fmt = {1: 'B', 2: 'H', 4: 'I', 8: 'f'}[bytes_per] if acc['type'] == 'SCALAR' \
            else COMPONENT_FLOATS[comp]
        stride = bv.get('byteStride') or bytes_per * ncomp
        norm = acc.get('normalized', False)
        out = []
        for i in range(acc['count']):
            vals = struct.unpack_from('<' + fmt * ncomp, self.raw, off + i * stride)
            if norm and comp != 5126:
                denom = float(2 ** (8 * bytes_per) - 1)
                if comp == 5120:
                    denom = float(2 ** (8 * bytes_per - 1) - 1)
                vals = tuple(v / denom for v in vals)
            out.append(vals[:ncomp])
        return out


def node_bounds(js, ni):
    """World-space (min, max, verts, materials) of a node's mesh, from accessor
    min/max (the captures bake all transforms into the vertices; node matrices
    are applied when present)."""
    node = js['nodes'][ni]
    mi = node.get('mesh')
    if mi is None:
        return None
    # node transform (identity for Ninja Ripper captures, but be correct anyway)
    m = node.get('matrix')
    if m:
        mat = [[m[0], m[4], m[8], m[12]], [m[1], m[5], m[9], m[13]],
               [m[2], m[6], m[10], m[14]], [m[3], m[7], m[11], m[15]]]
        for j in range(3):
            if any(abs(mat[j][k]) > 1e-9 for k in range(4)) or mat[j][3] != 0:
                break
        else:
            mat = None
    else:
        mat = None
    if mat is None:
        t = node.get('translation', [0, 0, 0])
        r = node.get('rotation', [0, 0, 0, 1])
        s = node.get('scale', [1, 1, 1])
        x, y, z, w = r
        rot = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
               [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
               [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
        mat = [[rot[i][j] * s[j] for j in range(3)] + [t[i]] for i in range(3)]
        mat.append([0, 0, 0, 1])

    def apply(v):
        return tuple(mat[j][0] * v[0] + mat[j][1] * v[1] + mat[j][2] * v[2] + mat[j][3]
                     for j in range(3))

    mn = [1e30] * 3
    mx = [-1e30] * 3
    verts = 0
    mats = set()
    for prim in js['meshes'][mi]['primitives']:
        acc = js['accessors'][prim['attributes']['POSITION']]
        verts += acc['count']
        mats.add(prim.get('material'))
        for corner in (tuple(acc['min']), tuple(acc['max'])):
            w = apply(corner)
            for j in range(3):
                mn[j] = min(mn[j], w[j])
                mx[j] = max(mx[j], w[j])
    return mn, mx, verts, mats


def pick_nodes(js, opts):
    """Selects node indices and collapses the double captures."""
    bounds = {}
    for ni, node in enumerate(js['nodes']):
        b = node_bounds(js, ni)
        if b is not None:
            bounds[ni] = b

    picked = set()
    for spec in opts.near or []:
        xyz, radius = spec.rsplit(':', 1)
        c = [float(v) for v in xyz.split(',')]
        r = float(radius)
        for ni, (mn, mx, nv, mats) in bounds.items():
            cc = [(mn[j] + mx[j]) * 0.5 for j in range(3)]
            if math.dist(cc, c) <= r:
                picked.add(ni)
    for spec in opts.names or []:
        for name in spec.split(','):
            name = name.strip()
            for ni, node in enumerate(js['nodes']):
                if node.get('name') == name:
                    picked.add(ni)

    def maxdim(ni):
        mn, mx, _, _ = bounds[ni]
        return max(mx[j] - mn[j] for j in range(3))

    if opts.min_size is not None:
        picked = {ni for ni in picked if maxdim(ni) >= opts.min_size}
    if opts.max_size is not None:
        picked = {ni for ni in picked if maxdim(ni) <= opts.max_size}

    if opts.dedupe:
        groups = {}
        for ni in sorted(picked):
            mn, mx, nv, mats = bounds[ni]
            cc = tuple(round((mn[j] + mx[j]) * 0.5, 1) for j in range(3))
            dims = tuple(round(mx[j] - mn[j], 1) for j in range(3))
            groups.setdefault((nv, dims, cc), []).append(ni)
        picked = set()
        for members in groups.values():
            # keep the copy that has materials (the textured half of the pair)
            withmat = [ni for ni in members
                       if any(m is not None for m in bounds[ni][3])]
            picked.add((withmat or members)[0])
    return sorted(picked), bounds


def group_props(js, picked, bounds, radius):
    """Single-linkage clustering of picked nodes by centre distance."""
    centres = {ni: [(bounds[ni][0][j] + bounds[ni][1][j]) * 0.5 for j in range(3)]
               for ni in picked}
    props = []
    for ni in picked:
        merged = None
        for prop in props:
            for other in prop:
                if math.dist(centres[ni], centres[other]) <= radius:
                    prop.append(ni)
                    merged = prop
                    break
            if merged:
                break
        if not merged:
            props.append([ni])
    return props


def write_prop(js, bins, nodes, out_dir, name, up_axis):
    """Writes one prop as OBJ + MTL (+ the PNGs its materials reference).
    Returns (source-space centre, ground, dims, verts, tris, texture files)."""

    def to_obj(p):
        # glTF -> OBJ as a Blender round trip does; a z-up capture is remapped
        # to (x, z, -y) so its elevation lands on OBJ's Y-up convention
        if up_axis == 'y':
            return (p[0], p[1], p[2])
        return (p[0], p[2], -p[1])

    used_mats = set()
    mesh_data = []   # (positions, normals, uvs, indices, material) in OBJ space
    bmin = [1e30] * 3
    bmax = [-1e30] * 3

    for ni in nodes:
        node = js['nodes'][ni]
        mi = node.get('mesh')
        m = node.get('matrix')
        mat = None
        if m:
            mat = [[m[0], m[4], m[8], m[12]], [m[1], m[5], m[9], m[13]],
                   [m[2], m[6], m[10], m[14]], [m[3], m[7], m[11], m[15]]]
        elif 'translation' in node or 'rotation' in node or 'scale' in node:
            t = node.get('translation', [0, 0, 0])
            r = node.get('rotation', [0, 0, 0, 1])
            s = node.get('scale', [1, 1, 1])
            x, y, z, w = r
            rot = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                   [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                   [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
            mat = [[rot[i][j] * s[j] for j in range(3)] + [t[i]] for i in range(3)]

        for prim in js['meshes'][mi]['primitives']:
            positions = bins.accessor(prim['attributes']['POSITION'])
            if mat is not None:
                positions = [tuple(sum(mat[j][k] * p[k] for k in range(3)) + mat[j][3]
                                   for j in range(3)) for p in positions]
            normals = None
            if 'NORMAL' in prim['attributes']:
                normals = bins.accessor(prim['attributes']['NORMAL'])
                if mat is not None:
                    normals = [tuple(sum(mat[j][k] * n[k] for k in range(3))
                                     for j in range(3)) for n in normals]
            uvs = None
            if 'TEXCOORD_0' in prim['attributes']:
                uvs = bins.accessor(prim['attributes']['TEXCOORD_0'])
            if 'indices' in prim:
                idx = [i[0] for i in bins.accessor(prim['indices'], nv=len(positions))]
            else:
                idx = list(range(len(positions)))
            mid = prim.get('material')
            used_mats.add(mid)
            opos = [to_obj(p) for p in positions]
            onrm = [to_obj(n) for n in normals] if normals else None
            mesh_data.append((opos, onrm, uvs, idx, mid))
            for p in opos:
                for j in range(3):
                    bmin[j] = min(bmin[j], p[j])
                    bmax[j] = max(bmax[j], p[j])

    if not mesh_data:
        return None
    centre = [(bmin[j] + bmax[j]) * 0.5 for j in range(3)]  # obj space
    src_centre = [0, 0, 0]
    for ni in nodes:
        mn, mx, _, _ = node_bounds(js, ni)
        for j in range(3):
            src_centre[j] += (mn[j] + mx[j]) * 0.5 / len(nodes)
    ground = bmin[1]

    # extract the textures this prop's materials reference
    texs = js.get('textures', [])
    imgs = js.get('images', [])
    obj_textures = {}
    for mid in sorted(m for m in used_mats if m is not None):
        matdef = js['materials'][mid] if mid < len(js['materials']) else {}
        texidx = matdef.get('pbrMetallicRoughness', {}).get('baseColorTexture', {}).get('index')
        if texidx is None:
            continue
        src = texs[texidx].get('source') if texidx < len(texs) else None
        if src is None or src in obj_textures.values():
            continue
        img = imgs[src]
        bv = img.get('bufferView')
        if bv is None:
            continue
        view = js['bufferViews'][bv]
        # capture GLB layouts are broken; the PNG magic finds the true start
        start = None
        for base in bins.bases:
            for delta in (-8, -4, 0, 4, 8):
                off = base + view.get('byteOffset', 0) + delta
                if bins.raw[off:off + 4] == b'\x89PNG':
                    start = off
                    break
            if start is not None:
                break
        if start is None:
            start = bins.bases[0] + view.get('byteOffset', 0)
        fname = '%s_m%d.png' % (name, mid)
        with open(os.path.join(out_dir, fname), 'wb') as f:
            f.write(bins.raw[start:start + view['byteLength']])
        obj_textures[mid] = fname

    obj_name = name + '.obj'
    mtl_name = name + '.mtl'
    with open(os.path.join(out_dir, obj_name), 'w') as fo:
        fo.write('# OpenTDU2 prop extracted from a user-supplied TDUSC capture (glb_props.py)\n')
        fo.write('mtllib %s\n' % mtl_name)
        base_v = base_vt = base_vn = 1
        for positions, normals, uvs, idx, mid in mesh_data:
            fo.write('o %s\n' % name)
            for p in positions:
                fo.write('v %.3f %.3f %.3f\n' % (p[0] - centre[0], p[1] - ground, p[2] - centre[2]))
            if normals:
                for n in normals:
                    fo.write('vn %.3f %.3f %.3f\n' % n)
            if uvs:
                for uv in uvs:
                    fo.write('vt %.5f %.5f\n' % (uv[0], 1.0 - uv[1]))
            fo.write('usemtl mat_%s\n' % mid)
            for t in range(0, len(idx), 3):
                face = []
                for k in (idx[t], idx[t + 1], idx[t + 2]):
                    v = base_v + k
                    vt = base_vt + k if uvs is not None else ''
                    vn = base_vn + k if normals is not None else ''
                    if uvs is not None and normals is not None:
                        face.append('%d/%d/%d' % (v, vt, vn))
                    elif normals is not None:
                        face.append('%d//%d' % (v, vn))
                    elif uvs is not None:
                        face.append('%d/%d' % (v, vt))
                    else:
                        face.append('%d' % v)
                fo.write('f %s\n' % ' '.join(face))
            base_v += len(positions)
            if normals is not None:
                base_vn += len(normals)
            if uvs is not None:
                base_vt += len(uvs)

    with open(os.path.join(out_dir, mtl_name), 'w') as fm:
        if None in used_mats:
            fm.write('newmtl mat_None\nKd 1 1 1\nKa 1 1 1\n\n')
        for mid in sorted(m for m in used_mats if m is not None):
            matdef = js['materials'][mid] if mid < len(js['materials']) else {}
            kd = matdef.get('pbrMetallicRoughness', {}).get('baseColorFactor', [1, 1, 1])
            fm.write('newmtl mat_%s\n' % mid)
            fm.write('Kd %.3f %.3f %.3f\n' % tuple(kd[:3]))
            fm.write('Ka %.3f %.3f %.3f\n' % tuple(kd[:3]))
            if mid in obj_textures:
                fm.write('map_Kd %s\n' % obj_textures[mid])
            fm.write('\n')

    dims = [bmax[j] - bmin[j] for j in range(3)]
    tris = sum(len(idx) // 3 for _, _, _, idx, _ in mesh_data)
    verts = sum(len(p) for p, _, _, _, _ in mesh_data)
    return src_centre, ground, dims, verts, tris, sorted(obj_textures.values())


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--glb', required=True, help='the user\'s own TDUSC scene capture (.glb)')
    ap.add_argument('--out', required=True, help='mods folder; props land in <out>/props/')
    ap.add_argument('--near', action='append', help='x,y,z:radius metres (repeatable)')
    ap.add_argument('--names', action='append', help='node names, comma separated (repeatable)')
    ap.add_argument('--min-size', type=float, help='keep nodes with bbox max dim >= this')
    ap.add_argument('--max-size', type=float, help='keep nodes with bbox max dim <= this')
    ap.add_argument('--dedupe', action='store_true', help='collapse double captures')
    ap.add_argument('--group', type=float, default=0, help='merge nodes within this distance into one prop')
    ap.add_argument('--prop-prefix', default='tdusc', help='prop name prefix')
    ap.add_argument('--up-axis', choices=['y', 'z'], default='y',
                    help="up axis of the capture's coordinates (glTF standard is y)")
    ap.add_argument('--write-props', action='store_true',
                    help='append the props.txt lines to <out>/props.txt directly')
    ap.add_argument('--report', action='store_true', help='list the selection, write nothing')
    opts = ap.parse_args()

    js, raw, bases = load_glb(opts.glb)
    bins = BinAccess(js, raw, bases)
    picked, bounds = pick_nodes(js, opts)
    if not picked:
        sys.exit('nothing selected')
    print('selected %d nodes:' % len(picked))
    for ni in picked:
        mn, mx, nv, mats = bounds[ni]
        cc = [(mn[j] + mx[j]) * 0.5 for j in range(3)]
        dims = [mx[j] - mn[j] for j in range(3)]
        print('  %-14s verts %6d dims [%5.1f %5.1f %5.1f] at (%9.1f %6.1f %9.1f) mats %s' % (
            js['nodes'][ni].get('name'), nv, *dims, *cc, sorted(m for m in mats if m is not None)))
    if opts.report:
        return

    props_dir = os.path.join(opts.out, 'props')
    os.makedirs(props_dir, exist_ok=True)
    groups = group_props(js, picked, bounds, opts.group) if opts.group else [[ni] for ni in picked]
    lines = []
    for gi, nodes in enumerate(groups):
        name = opts.prop_prefix + ('_%d' % gi if len(groups) > 1 else '')
        result = write_prop(js, bins, nodes, props_dir, name, opts.up_axis)
        if result is None:
            print('%s: no geometry, skipped' % name)
            continue
        centre, ground, dims, verts, tris, texs = result
        print('wrote %s: %d nodes, %d verts, %d tris, dims [%.1f %.1f %.1f], textures %s' % (
            name, len(nodes), verts, tris, *dims, texs or '-'))
        print('  source-space centre (%.1f, %.1f, %.1f), ground y %.1f - place with:' % (*centre, ground))
        line = 'ibiza  %s  X Y Z HEADING' % name
        print('    %s   (set X/Z to your target spot, Y to its terrain height)' % line)
        lines.append('# %s: source centre (%.1f, %.1f, %.1f), dims [%.1f %.1f %.1f]' % (
            name, *centre, *dims))
        lines.append('%s  X Y Z HEADING' % name)
    if opts.write_props and lines:
        with open(os.path.join(opts.out, 'props.txt'), 'a') as f:
            f.write('\n'.join(lines) + '\n')
        print('appended to %s' % os.path.join(opts.out, 'props.txt'))


if __name__ == '__main__':
    main()
