#!/usr/bin/env python3
"""Convert TDUSC `.PTX` textures (`PATX-SP2`, docs/re/tdusc_pkg_format.md section 2.2 / 9) to DDS (+ optional PNG preview).

    python ptx_to_dds.py --pkg-dir "<TDUSC>/WIN32/PKG" --toc-dir C:/temp/tdusc-scan --out C:/temp/tdusc-tex --png \
        "WIN32\\GRAPH\\CARS\\LAMBORGHINI\\HURACAN\\TEXTURES\\HURACAN_LP640_4_PERFORMANTE_BODY_LR.PTX"
    python ptx_to_dds.py ... --out C:/temp/tdusc-tex --all "WIN32\\GRAPH\\CARS\\LAMBORGHINI\\HURACAN\\TEXTURES\\"   (every PTX under a prefix)

Layout (decoded from the Huracan textures, all `[DATA]`): `PATX-SP2` `ENDI` u32 3, ..., then a table of 12-word entries from word 11:
`u32 width, height, depth(1), fileOffset, 0, compressedSize, uncompressedSize, 0 x 5`; the entries are mip 0, mip 1, ..., and
the last one is the packed tail of the small mips (width 0x80, 0x5e10 bytes). Every entry is a DirectStorage **GDeflate**
stream (`04 fb numTiles ...`, tools/tdusc/gdeflate.py) of block-compressed pixels. The header names **no pixel format**; the
uncompressed size gives the bits per pixel (4 = BC1 / BC4, 8 = BC3 / BC5 / BC7) and the file name the use
(`_NRM` -> BC5 SIGNED (DXGI 84, verified: flat normal = 0,0), `_DIFF` 8 bpp -> BC3 (verified: BC7 decodes to noise, BC3 shows the Huracan logo sheet), `_IOR` / `_AO` / `_MASK` / `_EMI` / `_DIRT` / `_SCRATCHES` ... 4 bpp). `--fmt` overrides
the guess (bc1 bc3 bc4 bc5 bc5s bc7). The DDS written holds mip 0 only (a DX10 header), which every viewer / engine accepts.
Reads the install read-only; writes only into the folder you name.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gdeflate  # noqa: E402
from tdusc_read import Tdusc  # noqa: E402

DXGI = {'bc1': 71, 'bc3': 77, 'bc4': 80, 'bc5': 83, 'bc5s': 84, 'bc7': 98}
BPB = {'bc1': 8, 'bc4': 8, 'bc3': 16, 'bc5': 16, 'bc5s': 16, 'bc7': 16}


def parse_ptx(d):
    if d[:12] != b'PATX-SP2ENDI':
        raise ValueError('not a PATX-SP2 file')
    mips = []
    p = 44
    while p + 48 <= len(d):
        w, h, dep, off, _z, comp, unc = struct.unpack_from('<7I', d, p)
        if w == 0 or h == 0 or off == 0 or off + comp > len(d):
            break
        mips.append(dict(w=w, h=h, off=off, comp=comp, unc=unc))
        p += 48
    return mips


def guess_fmt(name, w, h, unc):
    bpp = unc * 8 // (w * h)
    n = name.upper()
    if bpp == 8:
        return 'bc5s' if '_NRM' in n else 'bc3'
    if any(k in n for k in ('_AO', 'MASK', 'DIRT', 'SCRATCH', '_MR', 'EMI', 'ALPHA')):
        return 'bc4'
    return 'bc1'


def write_dds(path, w, h, fmt, data):
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000            # caps, height, width, pixelformat, linearsize
    pitch = len(data)
    hdr = b'DDS ' + struct.pack('<7I', 124, flags, h, w, pitch, 0, 1) + b'\0' * 44
    hdr += struct.pack('<2I4s5I', 32, 4, b'DX10', 0, 0, 0, 0, 0)       # pixelformat: FourCC = DX10
    hdr += struct.pack('<5I', 0x1000, 0, 0, 0, 0)                      # caps = texture
    hdr += struct.pack('<5I', DXGI[fmt], 3, 0, 1, 0)                   # DX10: format, 2D, misc, arraySize, alphaMode
    with open(path, 'wb') as f:
        f.write(hdr + data)


def convert(t, path, out, fmt, png):
    d = t.read(path)
    mips = parse_ptx(d)
    m = mips[0]
    raw = gdeflate.decompress(d[m['off']:m['off'] + m['comp']])
    name = path.replace('\\', '/').split('/')[-1]
    f = fmt or guess_fmt(name, m['w'], m['h'], m['unc'])
    if len(raw) != m['unc']:
        raise ValueError('%s: decoded %d bytes, expected %d' % (name, len(raw), m['unc']))
    exp = max(1, (m['w'] + 3) // 4) * max(1, (m['h'] + 3) // 4) * BPB[f]
    if exp != len(raw):
        raise ValueError('%s: %s needs %d bytes for %dx%d, have %d' % (name, f, exp, m['w'], m['h'], len(raw)))
    os.makedirs(out, exist_ok=True)
    stem = os.path.join(out, os.path.splitext(name)[0])
    write_dds(stem + '.dds', m['w'], m['h'], f, raw)
    if png:
        from PIL import Image
        im = Image.open(stem + '.dds')
        im.load()
        if f == 'bc4':
            im = im.convert('L')
        elif f in ('bc5', 'bc5s'):
            im = im.convert('RGB')
        im.save(stem + '.png')
    return '%s: %dx%d %s (%d mips in the file) -> %s.dds' % (name, m['w'], m['h'], f, len(mips), stem)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pkg-dir', required=True)
    ap.add_argument('--toc-dir', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--fmt', choices=sorted(DXGI))
    ap.add_argument('--png', action='store_true', help='also write a PNG preview (needs Pillow)')
    ap.add_argument('--all', action='store_true', help='path is a prefix: convert every .PTX under it')
    ap.add_argument('path')
    a = ap.parse_args()
    t = Tdusc(a.pkg_dir, a.toc_dir)
    paths = [p for p in t.ls(a.path) if p.upper().endswith('.PTX')] if a.all else [a.path]
    bad = 0
    for p in paths:
        try:
            print(convert(t, p, a.out, a.fmt, a.png))
        except Exception as e:  # noqa: BLE001
            bad += 1
            print('FAILED', p, e)
    print('%d converted, %d failed' % (len(paths) - bad, bad))


if __name__ == '__main__':
    main()
