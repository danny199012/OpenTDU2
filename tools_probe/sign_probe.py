"""Road-sign text probe: what the direction signs need to draw their names.

The sector `.pmi` "A" lists place road-sign actors (`RS_DirL_act`... from
`worldactors.bnk`, docs/re/pmi_spec.md 2.2). Type-4 records carry the sign
text in a payload (`pmi_spec.md` 1.5: byte 0 = layout code, then a 1-byte
charset) that the port does not consume yet - the plates draw blank. This
probe answers, from the shipped data, what a face composer needs:

    python tools_probe/sign_probe.py [C:\\TDU2-Gamedata\\TestDrive2] [-b]

  1. `rs_maps.2db` (commonworld.bnk) - dimensions, format, ASCII map: plate
     backgrounds / glyph sheet / baked faces?
  2. `rs_dirl` / `rs_dirr` `.3dg` (worldactors.bnk) - per primitive the
     position + texcoord bounds: full-plate UV (own texture per sign) vs an
     `rs_maps` sub-rect (shared atlas).
  3. type-4 payloads of a few sector `-a.pmi` lists, decoded through the
     charset, with the layout-code histogram.

`-b` writes PNGs of the decoded atlases next to the script (`_sign_*.png`,
gitignored scratch). Everything else is read-only.
"""
import glob
import os
import struct
import sys

K_POSITION = 0.0156252384185791
K_POSITION_OFFSET = 128.0

# ---------------------------------------------------------------- BNK bank
# Port of source/filesystem/bank.cpp (KNAB little endian, prefix-tree
# directory, u8/16/32 indexes by entry count).


def decode_bank_path(encoded):
    out, ext = [], ''
    for c in encoded.split('\\'):
        if c and c[0] == '.':
            ext = c
            continue
        out.append(c)
    return '\\'.join(out) + ext


def _parse_node(data, p, end, prefix, paths):
    if p >= end:
        return None
    ln = data[p]
    p += 1
    if ln >= 128:
        # Inner node: -length (0x80 => next byte + 0x80), varint children, name.
        nlen = (data[p] + 128) if ln == 128 else (256 - ln)
        if ln == 128:
            p += 1
        n = 0
        sh = 0
        while True:
            b = data[p]
            p += 1
            n |= (b & 0x7F) << sh
            sh += 7
            if b < 128:
                break
        name = data[p:p + nlen].decode('latin1')
        p += nlen
        for _ in range(n):
            if p is None:
                break
            p = _parse_node(data, p, end, prefix + name + '\\', paths)
        return p
    nlen = (data[p] + 0x7F) if ln == 0x7F else ln
    if ln == 0x7F:
        p += 1
    paths.append(prefix + data[p:p + nlen].decode('latin1'))
    return p + nlen


def load_bank(path):
    """Returns {path: bytes} of every entry of the bank."""
    buf = open(path, 'rb').read()
    if buf[8:12] != b'KNAB':
        raise ValueError(f'{path}: not a little endian KNAB bank')
    (_magic, _flags, _ver, _t, _tu, _s, _s2, _a, _da, num, _cd,
     off_table, off_types, off_dir, off_idx, _rev, _dat) = struct.unpack_from('<4sIIhhiiiiiiiiiiii', buf, 8)
    sect = {}
    for key, off in (('table', off_table), ('types', off_types), ('dir', off_dir), ('idx', off_idx)):
        if off < 8 or off + 8 > len(buf):
            sect[key] = (b'', 0)
        else:
            sect[key] = (buf[off + 8:], struct.unpack_from('<I', buf, off)[0])
    table, tsize = sect['table']
    directory, dsize = sect['dir']
    indexes, _ = sect['idx']
    stride = 0x14
    if num > 0 and tsize // (num + 1) == 0x10:
        stride = 0x10  # TDU1 layout
    entries = []
    for i in range(num):
        off, size = struct.unpack_from('<ii', table, i * stride)
        data = buf[off:off + size] if 0 <= off < off + size <= len(buf) else b''
        entries.append(data)
    if directory and indexes and num:
        paths = []
        end = min(dsize, len(directory))
        p = 0
        while p < end and directory[p] != 0:
            p = _parse_node(directory, p, end, '', paths)
            if p is None:
                break
        for i, path in enumerate(paths[:num]):
            if num < 0x100:
                ei = indexes[i]
            elif num < 0x10000:
                ei = struct.unpack_from('<H', indexes, i * 2)[0]
            else:
                ei = struct.unpack_from('<I', indexes, i * 4)[0]
            if ei < num:
                entries[ei] = (decode_bank_path(path), entries[ei])
    out = {}
    for e in entries:
        if isinstance(e, tuple):
            out[e[0].lower()] = e[1]
    return out


# ------------------------------------------------------------------- 2DB
# docs/formats.md: texture header (0x30) at file offset 0x20, pixels at 0x50.
# Formats follow TDU1 eViewFormat (51 = SRGB_DXT1, 55 = SRGB_DXT5).


def read_2db(data):
    thash, w, h, depth, mips, packed = struct.unpack_from('<QHHHBB', data, 0x20)
    flags, = struct.unpack_from('<I', data, 0x30)
    fmt, = struct.unpack_from('<I', data, 0x3C)
    return {'hash': thash, 'w': w, 'h': h, 'depth': depth, 'mips': mips,
            'flags': flags, 'fmt': fmt, 'texels': data[0x50:]}


def _expand565(c):
    return ((c >> 11) & 0x1F) * 255 // 31, ((c >> 5) & 0x3F) * 255 // 63, (c & 0x1F) * 255 // 31


def decode_dxt(px, w, h, fmt):
    """BC1 (51/23) or BC3/BC2 alpha (55/53) mip 0 -> rows of (r, g, b, a)."""
    dxt1 = fmt in (23, 51)
    alpha = fmt in (53, 55)
    rows = [[(0, 0, 0, 255)] * w for _ in range(h)]
    bw, bh = (w + 3) // 4, (h + 3) // 4
    block = 8 if dxt1 else 16
    for by in range(bh):
        for bx in range(bw):
            o = (by * bw + bx) * block
            if o + block > len(px):
                return rows
            a = [255] * 16
            if alpha:
                a0, a1 = px[o], px[o + 1]
                apal = [a0, a1]
                if a0 > a1:
                    for i in range(6):
                        apal.append(((6 - i) * a0 + (i + 1) * a1) // 7)
                else:
                    for i in range(4):
                        apal.append(((4 - i) * a0 + (i + 1) * a1) // 5)
                    apal += [0, 255]
                abits = int.from_bytes(px[o + 2:o + 8], 'little')
                for k in range(16):
                    a[k] = apal[(abits >> (3 * k)) & 7]
                o += 8
            c0, c1, bits = struct.unpack_from('<HHI', px, o)
            p0, p1 = _expand565(c0), _expand565(c1)
            pal = [p0, p1]
            if c0 > c1:
                pal += [tuple((2 * p0[k] + p1[k] + 1) // 3 for k in range(3)),
                        tuple((p0[k] + 2 * p1[k] + 1) // 3 for k in range(3))]
            else:
                pal += [tuple((p0[k] + p1[k]) // 2 for k in range(3)), (0, 0, 0)]
            for py in range(4):
                yy = by * 4 + py
                if yy >= h:
                    break
                for pxx in range(4):
                    xx = bx * 4 + pxx
                    if xx >= w:
                        break
                    k = (bits >> (2 * (py * 4 + pxx))) & 3
                    r, g, b = pal[k]
                    rows[yy][xx] = (r, g, b, a[py * 4 + pxx])
    return rows


def ascii_map(img, w, h, cols=96, rows=48):
    ramp = ' .:-=+*#%@'
    lines = []
    for ry in range(rows):
        line = ''
        for rx in range(cols):
            x0, x1 = rx * w // cols, (rx + 1) * w // cols
            y0, y1 = ry * h // rows, (ry + 1) * h // rows
            r = g = b = n = 0
            for y in range(y0, y1, max(1, (y1 - y0) // 3)):
                for x in range(x0, x1, max(1, (x1 - x0) // 3)):
                    p = img[y][x]
                    r += p[0]
                    g += p[1]
                    b += p[2]
                    n += 1
            r, g, b = r // n, g // n, b // n
            line += ramp[min(9, ((r * 3 + g * 6 + b) // 10) * 10 // 256)]
        lines.append(line)
    return '\n'.join(lines)


def write_png(path, img, w, h):
    import zlib
    raw = b''.join(b'\x00' + bytes(v for px in row for v in px) for row in img)

    def chunk(tag, payload):
        c = tag + payload
        return struct.pack('>I', len(payload)) + c + struct.pack('>I', zlib.crc32(c))
    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
    open(path, 'wb').write(png)
    print(f'  wrote {path}')


# ------------------------------------------------------------------- 3DG
# docs/formats.md / source/render/3dg.h: GEOA/HASH/GEOM/PRIM + DXVB streams.
FCC = lambda s: struct.unpack('<I', s)[0]
M_GEOM, M_PRIM, M_DXVB, M_DXIB = FCC(b'GEOM'), FCC(b'PRIM'), FCC(b'DXVB'), FCC(b'DXIB')
ATTR_SIZE = {0: 12, 0x0B: 8, 0x02: 4, 0x0C: 8, 0x17: 8, 0x11: 4, 0x12: 4}


def visit_sections(data):
    """(type, offset, size, dataSize, depth) of every section, outermost first."""
    out = []

    def rng(begin, end, depth):
        off = begin
        while off + 16 <= end:
            typ, _u1, _u2, size, dsize = struct.unpack_from('<IHHII', data, off)
            if size < 16 or off + size > end:
                return
            out.append((typ, off, size, dsize, depth))
            ds = max(dsize, 16)
            if size > ds:
                rng(off + ds, off + size, depth + 1)
            off += size
    total = min(struct.unpack_from('<I', data, 0)[0], len(data)) if len(data) >= 16 else 0
    rng(16, total, 0)
    return out


def walk_3dg(data):
    """Per PRIM: material index, vertex/index counts, position + uv bounds, uv list."""
    prims = []

    def collect_prims(begin, end):
        off = begin + 16
        while off + 16 <= end:
            typ, _u1, _u2, size, dsize = struct.unpack_from('<IHHII', data, off)
            if size < 16 or off + size > end:
                break
            if typ == M_PRIM:
                prims.append(off)
            ds = max(dsize, 16)
            if size > ds:
                collect_prims(off + ds, off + size)
            off += size

    for (typ, off, size, dsize, _depth) in visit_sections(data):
        if typ == M_GEOM:
            collect_prims(off, off + size)
    result = []
    for poff in prims:
        pay = poff + 16
        iboff, vboff = struct.unpack_from('<II', data, pay)
        matidx, ptype = struct.unpack_from('<HH', data, pay + 0x10)
        numv, = struct.unpack_from('<I', data, pay + 0x1C)
        numi, = struct.unpack_from('<I', data, pay + 0x28)
        if vboff + 0x30 > len(data) or struct.unpack_from('<I', data, vboff)[0] != M_DXVB:
            continue
        numvert, = struct.unpack_from('<I', data, vboff + 0x10)
        sem = [(data[vboff + 0x14 + i * 2], data[vboff + 0x15 + i * 2]) for i in range(12)]
        flags, = struct.unpack_from('<I', data, vboff + 0x2C)
        uv0 = (flags & 0x3800) != 0
        skin0 = (flags & 0x2200) != 0
        nrm0 = (flags & 0x0800) != 0
        stride = [0, 0]
        aoff, sof = [0] * 12, [0] * 12
        for i, (_cnt, _fmt) in enumerate(sem):
            s0 = i == 0 or (uv0 and i == 4) or (nrm0 and i == 1) or (skin0 and i in (7, 8))
            sof[i] = 0 if s0 else 1
            aoff[i] = stride[sof[i]]
            stride[sof[i]] += sem[i][0] * ATTR_SIZE.get(sem[i][1], 12)
        doff = vboff + 0x30
        base = [doff, doff + numvert * stride[0]]
        if doff + numvert * (stride[0] + stride[1]) > len(data):
            continue

        def attr(i, v):
            return base[sof[i]] + v * stride[sof[i]] + aoff[i]
        uvs, poss = [], []
        for v in range(numvert):
            if sem[4][0] and sem[4][1] == 0x0C:
                uvs.append(struct.unpack_from('<ff', data, attr(4, v)))
            if sem[0][0] and sem[0][1] == 0:
                poss.append(struct.unpack_from('<fff', data, attr(0, v)))
        indices = []
        if iboff and iboff + 0x20 <= len(data) and struct.unpack_from('<I', data, iboff)[0] == M_DXIB:
            numi2, isize = struct.unpack_from('<II', data, iboff + 0x10)
            fmt = '<H' if isize == 1 else '<I'
            indices = list(struct.unpack_from(f'<{numi2}{"H" if isize == 1 else "I"}', data, iboff + 0x20))
        result.append({
            'mat': matidx, 'type': ptype, 'numv': numvert, 'numi': numi, 'flags': flags,
            'uvfmt': sem[4][1],
            'uvlist': uvs,
            'indices': indices,
            'quads': strip_quads(indices, uvs) if indices and uvs else [],
            'uv': (min(u[0] for u in uvs), min(u[1] for u in uvs),
                   max(u[0] for u in uvs), max(u[1] for u in uvs)) if uvs else None,
            'pos': ((min(p[0] for p in poss), min(p[1] for p in poss), min(p[2] for p in poss)),
                    (max(p[0] for p in poss), max(p[1] for p in poss), max(p[2] for p in poss))) if poss else None,
        })
    return result


def strip_quads(indices, uvs):
    """UV rects of the quads of a triangle strip: consecutive triangles that
    share an edge merge into one quad (2 new vertices per step)."""
    quads = []
    tris = []
    for t in range(len(indices) - 2):
        a, b, c = indices[t], indices[t + 1], indices[t + 2]
        tris.append(frozenset((a, b, c)))
    used = [False] * len(tris)
    for i, t1 in enumerate(tris):
        if used[i]:
            continue
        merged = False
        for j in range(i + 1, len(tris)):
            if used[j]:
                continue
            shared = t1 & tris[j]
            if len(shared) == 2:
                corners = tuple(t1 | tris[j])
                if len(corners) == 4:
                    us = [uvs[c][0] for c in corners]
                    vs = [uvs[c][1] for c in corners]
                    quads.append((min(us), min(vs), max(us), max(vs)))
                    used[i] = used[j] = True
                    merged = True
                    break
        if not merged:
            corners = tuple(t1)
            us = [uvs[c][0] for c in corners]
            vs = [uvs[c][1] for c in corners]
            quads.append((min(us), min(vs), max(us), max(vs)))
            used[i] = True
    return quads


def uv_v_clusters(uvs, tolerance=0.01):
    """Distinct v bands of a plate prim (the blank bars): cluster v centres."""
    if not uvs:
        return []
    vs = sorted(set(round((u[1] + u[3]) * 0.5, 4) if False else u[1] for u in uvs))
    clusters = []
    for v in vs:
        if clusters and v - clusters[-1][1] <= tolerance:
            clusters[-1][1] = max(clusters[-1][1], v)
        else:
            clusters.append([v, v])
    return [(a, b) for a, b in clusters]


# ------------------------------------------------------------------ PMI
# tools_probe/pmi_probe.py parse() with the payload bytes kept.
def decode_payload(payload):
    char = {}
    for i in range(0x1A):
        char[1 + i] = chr(65 + i)
    for i in range(10):
        char[0x1B + i] = chr(48 + i)
    for i in range(0x1A):
        char[0x2B + i] = chr(97 + i)
    char[0x47] = 'ñ'
    char[0x50] = 'À'
    out = []
    for c in payload:
        if c == 0:
            out.append('|')
        elif c in char:
            out.append(char[c])
        else:
            out.append(f'<{c:02x}>')
    return ''.join(out)


def pmi_type4(path, data, max_show=3):
    """Yields (extraSize, layoutCode, rawPayload) of every type-4 MOB record."""
    if len(data) < 0x1D0 or data[0x0C:0x10] != b'.PMI':
        return
    num_mob, hmin, hmax = struct.unpack_from('<Iff', data, 0x20)
    off = 0x10 + struct.unpack_from('<I', data, 0x1C)[0]
    shown = 0
    for _ in range(num_mob):
        if data[off:off + 4] != b'MOB.':
            return
        size, = struct.unpack_from('<I', data, off + 8)
        hashcode, = struct.unpack_from('<Q', data, off + 0x10)
        count, extra, _unk = struct.unpack_from('<HHH', data, off + 0x18)
        ptype = data[off + 0x1E]
        stride = 0x20 + ((extra + 15) & ~15)
        if ptype == 4 and extra > 0:
            for i in range(count):
                rec = off + 0x20 + i * stride
                payload = bytes(data[rec + 0x20:rec + 0x20 + extra])
                yield hashcode, extra, payload
                if shown < max_show:
                    shown += 1
        off += size


# ------------------------------------------------------------------ main
def main():
    root = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else r'C:\TDU2-Gamedata\TestDrive2'
    want_png = '-b' in sys.argv
    island = 'hawai' if '-hawai' in sys.argv else 'ibiza'
    isl = os.path.join(root, 'Euro', 'Bnk', 'islands', island)

    print('=== rs_maps.2db (commonworld.bnk, the sign plate atlas) ===')
    cw = load_bank(os.path.join(isl, 'level', 'commonworld.bnk'))
    textures = {p: d for p, d in cw.items() if p.endswith('.2db')}
    rs = None
    for p in sorted(textures):
        base = os.path.basename(p)
        if base.startswith('rs_maps'):
            print(f'  {base}  ({len(textures[p])} B, {p})')
            if base == 'rs_maps.2db':
                rs = textures[p]
    if rs is None:
        print('  rs_maps.2db NOT FOUND')
    else:
        t = read_2db(rs)
        print(f'  hash {t["hash"]:016x}  {t["w"]}x{t["h"]} depth {t["depth"]} mips {t["mips"]} '
              f'flags {t["flags"]:#x} fmt {t["fmt"]}')
        if t['fmt'] in (23, 51, 53, 55) and t['depth'] == 1:
            img = decode_dxt(t['texels'], t['w'], t['h'], t['fmt'])
            print(ascii_map(img, t['w'], t['h']))
            if want_png:
                write_png(os.path.join(os.path.dirname(__file__), '_sign_rs_maps.png'), img, t['w'], t['h'])

    print('\n=== direction sign geometry (worldactors.bnk) ===')
    wa = load_bank(os.path.join(isl, 'scenarics', 'packs', 'worldactors.bnk'))
    geoms = {p: d for p, d in wa.items() if p.endswith('.3dg')}
    print(f'  {len(geoms)} .3dg total, showing RS_Dir*:')
    for p in sorted(geoms):
        low = os.path.basename(p).lower()
        if 'dirl' in low or 'dirr' in low:
            for q in walk_3dg(geoms[p]):
                po = q['pos']
                uv = q['uv']
                print(f'  {os.path.basename(p)}  mat {q["mat"]} type {q["type"]} v {q["numv"]} i {q["numi"]} '
                      f'flags {q["flags"]:#06x} uvfmt {q["uvfmt"]:#04x}')
                if po:
                    print(f'    pos ({po[0][0]:.2f},{po[0][1]:.2f},{po[0][2]:.2f}) .. ({po[1][0]:.2f},{po[1][1]:.2f},{po[1][2]:.2f})')
                print(f'    uv  {tuple(round(v, 3) for v in uv)}' if uv else '    uv  none')
                if uv:
                    seen = set()
                    rects = []
                    for r in q['quads']:
                        key = tuple(round(v, 3) for v in r)
                        if key not in seen and (key[2] - key[0]) > 0.02:
                            seen.add(key)
                            rects.append(key)
                    print(f'    quads: {rects}')

    print(f'\n=== type-4 payloads (every sector -a.pmi of {island}) ===')
    layouts = {}
    uniques = {}
    total = 0
    for area_dir in sorted(glob.glob(os.path.join(isl, 'level', 'area-*'))):
        sub = os.path.basename(area_dir) + '-*'  # area-3 -> area-3-3
        for bank_path in sorted(glob.glob(os.path.join(area_dir, sub, '*.bnk'))):
            try:
                ent = load_bank(bank_path)
            except Exception:
                continue
            for p, d in ent.items():
                if p.endswith('-a.pmi'):
                    for _hashcode, extra, payload in pmi_type4(p, d, max_show=0):
                        total += 1
                        key = (extra, payload[0])
                        layouts.setdefault(extra, {}).setdefault(payload[0], 0)
                        layouts[extra][payload[0]] += 1
                        uniques.setdefault(payload, 0)
                        uniques[payload] += 1
    print(f'  {total} type-4 records, {len(uniques)} unique payloads')
    print('\n  layout codes by extraSize:')
    for extra in sorted(layouts):
        codes = ', '.join(f'{c:#04x} x{n}' for c, n in sorted(layouts[extra].items()))
        print(f'    extra {extra:3d}: {codes}')
    print('\n  first 12 unique payloads (raw + decoded):')
    for payload in list(sorted(uniques))[:12]:
        hexs = payload.hex(' ')
        print(f'    [{payload[0]:#04x}] "{decode_payload(payload)}"')
        print(f'         {hexs}')

    print('\n  two samples per (extraSize, code), with a decimal ruler:')
    groups = {}
    for payload in uniques:
        groups.setdefault((len(payload), payload[0]), []).append(payload)
    for key in sorted(groups):
        extra, code = key
        print(f'  -- extra {extra} code {code:#04x} ({len(groups[key])} uniques)')
        for payload in groups[key][:2]:
            ruler = '     ' + ''.join(str(i // 10 % 10) for i in range(len(payload)))
            ruler2 = '     ' + ''.join(str(i % 10) for i in range(len(payload)))
            print(ruler)
            print(ruler2)
            print('     ' + payload.hex(' '))
            print(f'     "{decode_payload(payload)}"')


if __name__ == '__main__':
    main()
