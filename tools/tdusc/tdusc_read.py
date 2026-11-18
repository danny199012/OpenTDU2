#!/usr/bin/env python3
"""Read files out of a TDUSC install by their game path (docs/re/tdusc_pkg_format.md).

Needs the table-of-contents CSVs first:  python pkg_scan.py --pkg <part.PKG> --out <tocdir> --toc  (every part)

    python tdusc_read.py --pkg-dir "<TDUSC>/WIN32/PKG" --toc-dir C:/temp/tdusc-scan ls "WIN32\\GRAPH\\CARS\\LAMBORGHINI\\HURACAN\\"
    python tdusc_read.py ... info "WIN32\\GRAPH\\CARS\\LAMBORGHINI\\HURACAN\\MESH\\HURACAN_LP640_4_PERFORMANTE_LR.P3D"
    python tdusc_read.py ... strings "WIN32\\...\\HURACAN_LP640_4_PERFORMANTE_LR.PSI"
    python tdusc_read.py ... export "<game path>" --out C:/temp/tdusc-export       (the decoded bytes, into a folder you name)

It only reads the install; `export` writes into the folder you pass, never into the repo or the game folder.
"""
import argparse
import csv
import glob
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkg_scan import lz4_frame  # noqa: E402


class Tdusc:
    def __init__(self, pkg_dir, toc_dir):
        self.pkg_dir = pkg_dir
        self.entries = {}
        for f in sorted(glob.glob(os.path.join(toc_dir, '*_toc.csv'))):
            part = os.path.basename(f)[:-len('_toc.csv')]
            with open(f, newline='') as fh:
                for r in csv.DictReader(fh):
                    r['part'] = part
                    self.entries[r['path']] = r

    def ls(self, prefix):
        return sorted(k for k in self.entries if k.upper().startswith(prefix.upper()))

    def read(self, path):
        r = self.entries[path]
        fn = os.path.join(self.pkg_dir, r['part'][:7], r['part'] + '.PKG')
        n = int(r['decoded_bytes'])
        fs = int(r['frame_bytes'])
        with open(fn, 'rb') as fh:
            fh.seek(int(r['offset'], 16))
            buf = fh.read(fs + 64)
        if buf[:4] != b'PKGB':
            raise ValueError('offset does not point at PKGB')
        if r['kind'] == '2':
            return lz4_frame(buf, 4)[0]
        return buf[4:4 + n]


def p3d_info(d):
    """Plaintext part of a P3D (`SCENE___`): the GDeflate blob header, mesh boxes and vertex layouts. [DATA]"""
    out = []
    if d[:8] != b'SCENE___':
        return ['not a SCENE___ container']
    w = struct.unpack_from('<24I', d, 0)
    blob = w[13]
    out.append('plain section %d bytes, blob at 0x%x, blob header words %s' % (w[8], blob, [hex(x) for x in w[17:19]]))
    if blob + 8 <= len(d):
        ident, magic, tiles = struct.unpack_from('<BBH', d, blob)
        last = struct.unpack_from('<I', d, blob + 4)[0]
        out.append('blob: id %d magic 0x%02x (id^0xff = %s) tiles %d tileSizeIdx %d lastTile %d -> %d bytes decompressed'
                   % (ident, magic, ident ^ 0xff == magic, tiles, last & 3, last >> 2, (tiles - 1) * 65536 + (last >> 2)))
    for m in re.finditer(rb'(?:COLO)?(?:TANB)?(?:NORM)?POS_[A-Z0-9_]*', d[:blob]):
        out.append('vertex format %s at 0x%x' % (m.group().decode(), m.start()))
    names = [m.group().decode() for m in re.finditer(rb'[A-Za-z_$][ -~]{3,40}', d[:blob])]
    out.append('names: %s' % names[:40])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pkg-dir', required=True)
    ap.add_argument('--toc-dir', required=True)
    ap.add_argument('cmd', choices=['ls', 'info', 'strings', 'export'])
    ap.add_argument('path')
    ap.add_argument('--out')
    a = ap.parse_args()
    t = Tdusc(a.pkg_dir, a.toc_dir)
    if a.cmd == 'ls':
        for p in t.ls(a.path):
            r = t.entries[p]
            print('%s  %s  %s -> %s' % (r['part'][-9:], p, r['frame_bytes'], r['decoded_bytes']))
        return
    d = t.read(a.path)
    if a.cmd == 'info':
        print('\n'.join(p3d_info(d)) if a.path.upper().endswith('.P3D') else d[:96].hex(' ', 4))
    elif a.cmd == 'strings':
        for m in re.finditer(rb'[ -~]{4,}', d):
            print(m.group().decode())
    elif a.cmd == 'export':
        if not a.out:
            sys.exit('--out <folder> is required')
        os.makedirs(a.out, exist_ok=True)
        dst = os.path.join(a.out, a.path.replace('\\', '__'))
        open(dst, 'wb').write(d)
        print('wrote', dst, len(d))


if __name__ == '__main__':
    main()
